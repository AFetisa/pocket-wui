#include "usb.h"
#include "config.h"
#include "jobs.h"
#include "storage.h"
#include "stream.h"
#include <Preferences.h>

#if WUI_USB
#include <EspUsbDevice.h>
#include <SD.h>

namespace {

EspUsbDevice          g_dev;
EspUsbDeviceNet       g_net(g_dev);
EspUsbDeviceMsc       g_msc(g_dev);
EspUsbDeviceMscSdCard g_sdMsc(SD);

bool g_enabled = true, g_running = false, g_drive = false, g_driveOk = false;
volatile bool g_ejected = false;

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
    g_sdMsc.onEject([]() { g_ejected = true; });
    g_driveOk = g_sdMsc.attach(g_msc);
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
  if (g_ejected) {
    g_ejected = false;
    String err;
    setDriveMode(false, err);
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
  if (on == g_drive) return true;
  if (on) {
    if (!driveAvailable()) { err = "USB drive is not available (USB off, or no card at boot)"; return false; }
    if (jobs::busy()) { err = "wait for the running copy/checksum to finish"; return false; }
    if (stream::active()) { err = "wait for the running download to finish"; return false; }
    {
      storage::Guard g;
      storage::closeUpload(true);
      storage::setLentToUsb(true);
    }
    g_msc.mediaPresent(true);
    g_drive = true;
    return true;
  }
  g_msc.mediaPresent(false);
  g_drive = false;
  // The computer may have changed anything: drop every cached FAT sector.
  storage::setLentToUsb(false);
  storage::remount();
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
