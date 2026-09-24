#include "usb.h"
#include "config.h"
#include "jobs.h"
#include "storage.h"
#include "stream.h"
#include <Preferences.h>
#include <mutex>

#if WUI_USB
#if ARDUINO_USB_MODE != 0 || ARDUINO_USB_CDC_ON_BOOT != 0
#error "USB features need ARDUINO_USB_MODE=0 (USB-OTG/TinyUSB) and ARDUINO_USB_CDC_ON_BOOT=0 — see platformio.ini"
#endif
#include <EspUsbDevice.h>
#include <SD.h>

namespace {

EspUsbDevice          g_dev;
EspUsbDeviceNet       g_net(g_dev);
EspUsbDeviceMsc       g_msc(g_dev);
EspUsbDeviceMscSdCard g_sdMsc(SD);

bool g_enabled = true, g_running = false, g_driveOk = false;
volatile bool g_drive = false;         // only changed under storage::lock
volatile bool g_ejected = false;
std::mutex g_modeMu;                   // one setDriveMode() at a time (web task vs loop)
uint32_t g_unpluggedAt = 0;

// Sector access for the host. Replaces the adapter's own callbacks so every
// access happens under the SD lock and only while the card is lent: once
// setDriveMode(false) has flipped g_drive (under the same lock), no transfer
// can still be running when the card is unmounted and remounted.
int32_t sectorIo(uint32_t lba, uint32_t offset, uint8_t *buf, uint32_t size, bool write) {
  storage::Guard g;
  if (!g_drive || offset >= 512) return -1;
  uint8_t sector[512];
  uint32_t left = size;
  while (left > 0) {
    uint32_t chunk = std::min<uint32_t>(left, 512 - offset);
    if (write) {
      if (chunk != 512 && !SD.readRAW(sector, lba)) return -1;
      memcpy(sector + offset, buf, chunk);
      if (!SD.writeRAW(sector, lba)) return -1;
    } else {
      if (!SD.readRAW(sector, lba)) return -1;
      memcpy(buf, sector + offset, chunk);
    }
    buf += chunk;
    left -= chunk;
    offset = 0;
    lba++;
  }
  return (int32_t)size;
}

}  // namespace

namespace usb {

void begin() {
  Preferences p;
  p.begin("wui-usb", true);
  g_enabled = p.getBool("on", true);
  p.end();
  if (!g_enabled) return;

  // The card stays mounted by us; the drive reads raw sectors, and the host
  // only sees media while drive mode is on.
  if (storage::mounted() && g_sdMsc.begin(SS, SPI, storage::clockHz())) {
    g_msc.vendorID("M5Stack");
    g_msc.productID(WUI_NAME " SD");
    g_msc.productRevision("2.0");
    g_msc.mediaPresent(false);
    g_msc.isWritable(true);
    g_driveOk = g_sdMsc.attach(g_msc);
    if (g_driveOk) {
      g_msc.onRead([](uint32_t lba, uint32_t off, void *buf, uint32_t n) {
        return sectorIo(lba, off, (uint8_t *)buf, n, false);
      });
      g_msc.onWrite([](uint32_t lba, uint32_t off, uint8_t *buf, uint32_t n) {
        return sectorIo(lba, off, buf, n, true);
      });
      // Only a real eject hands the card back — not a host's power-saving STOP UNIT.
      g_msc.onStartStop([](uint8_t, bool start, bool loadEject) {
        if (!start && loadEject) g_ejected = true;
        return true;
      });
    }
  }

  // Never the computer's gateway or DNS: its internet stays on its own uplink.
  g_net.ipConfig(IPAddress(WUI_USB_IP), IPAddress(WUI_USB_IP), IPAddress(255, 255, 255, 0));
  g_net.dhcpServer(true);
  g_net.dhcpAdvertiseGateway(false);

  EspUsbDeviceConfig cfg;
  cfg.manufacturer = "M5Stack";
  cfg.product = WUI_NAME;
  cfg.serialNumber = "pocketwui";
  cfg.pid = 0x4032;
  if (!g_dev.begin(cfg)) {
    log_e("USB start failed: %s", g_dev.lastErrorName());
    return;
  }
  g_running = g_net.beginNetwork();
  if (!g_running) log_e("USB network failed to start");
}

void tick() {
  String err;
  if (g_ejected) {
    g_ejected = false;
    setDriveMode(false, err);
  }
  // Cable pulled while lent: the host can never eject, so take the card back.
  if (g_drive && !g_dev.ready()) {
    if (!g_unpluggedAt) g_unpluggedAt = millis();
    if (millis() - g_unpluggedAt > 3000) { g_unpluggedAt = 0; setDriveMode(false, err); }
  } else {
    g_unpluggedAt = 0;
  }
}

bool enabled() { return g_enabled; }

void setEnabled(bool on) {
  Preferences p;
  p.begin("wui-usb", false);
  p.putBool("on", on);
  p.end();
  g_enabled = on;
}

bool running() { return g_running; }
bool hostConnected() { return g_running && g_dev.ready(); }
String ip() { return g_running ? g_net.localIP().toString() : String(); }
bool driveAvailable() { return g_running && g_driveOk; }
bool driveMode() { return g_drive; }

bool setDriveMode(bool on, String &err) {
  std::lock_guard<std::mutex> serial(g_modeMu);
  if (on == g_drive) return true;
  if (on) {
    if (!driveAvailable()) { err = "USB drive is not available (USB off, or no card at start-up)"; return false; }
    if (!g_dev.ready()) { err = "plug the Cardputer into a computer first"; return false; }
    if (jobs::busy()) { err = "wait for the running copy/checksum to finish"; return false; }
    if (stream::active()) { err = "wait for the running download to finish"; return false; }
    storage::Guard g;
    // The drive's size was fixed at start-up: a missing or swapped card must
    // never be offered (raw reads on an unmounted card crash).
    if (!storage::mounted() || (uint32_t)SD.numSectors() != g_sdMsc.blockCount()) {
      err = "the card changed since start-up — restart to use it as a USB drive";
      return false;
    }
    storage::closeUpload(true);
    storage::setLentToUsb(true);
    g_drive = true;
    g_msc.mediaPresent(true);
    return true;
  }
  g_msc.mediaPresent(false);
  {
    storage::Guard g;            // waits for any host transfer in flight
    g_drive = false;
  }
  // The computer may have changed anything: drop every cached FAT sector.
  // Remount first and only then open the card to the web again.
  storage::remount();
  storage::setLentToUsb(false);
  return true;
}

}  // namespace usb

#else  // !WUI_USB

namespace usb {
void begin() {}
void tick() {}
bool enabled() { return false; }
void setEnabled(bool) {}
bool running() { return false; }
bool hostConnected() { return false; }
String ip() { return String(); }
bool driveAvailable() { return false; }
bool driveMode() { return false; }
bool setDriveMode(bool, String &err) { err = "built without USB support"; return false; }
}  // namespace usb

#endif
