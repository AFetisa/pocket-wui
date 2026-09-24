// Hardware the simulator does not have — Wi-Fi, USB, battery, Launcher —
// replaced by small fakes with the same interfaces, so the real server code
// above them runs unchanged.
#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>
#include "../../src/firmware.h"
#include "../../src/fwimage.h"
#include "../../src/net.h"
#include "../../src/storage.h"
#include "../../src/ui.h"
#include "../../src/usb.h"
#include <map>
#include <vector>

int wui_sim_verbose = 0;
int wui_sim_port = 8791;
SimEsp ESP;
SimSerial Serial;
SimM5 M5;
void SimEsp::restart() { printf("[sim] restart requested (ignored)\n"); fflush(stdout); }

// ------------------------------------------------------------------ net
namespace {
std::vector<String> g_saved;
String g_host = "cardputer", g_apPass = "simulated1";
bool g_keepAp = false;
}
namespace net {
void begin() {}
void tick() {}
bool isAP() { return false; }
bool apUp() { return g_keepAp; }
String apSsid() { return "PocketWUI-51M0"; }
String apPassword() { return g_apPass; }
String apIp() { return "192.168.4.1"; }
int apClients() { return 0; }
bool setApPassword(const String &pw, String &err) {
  if (pw.length() < 8 || pw.length() > 63) { err = "the Wi-Fi key must be 8–63 characters"; return false; }
  g_apPass = pw;
  return true;
}
String ssid() { return "SimNet"; }
String ip() { return "127.0.0.1"; }
int rssi() { return -52; }
String hostname() { return g_host; }
bool setHostname(const String &n, String &err) { if (n.isEmpty()) { err = "name required"; return false; } g_host = n; return true; }
bool keepAp() { return g_keepAp; }
void setKeepAp(bool on) { g_keepAp = on; }
bool join(const String &s, const String &p, String &err) {
  if (p == "wrong") { err = "could not join " + s + " (wrong password, or out of range?)"; return false; }
  for (size_t i = 0; i < g_saved.size(); ++i) if (g_saved[i] == s) g_saved.erase(g_saved.begin() + i);
  g_saved.insert(g_saved.begin(), s);
  return true;
}
std::vector<String> savedNetworks() { return g_saved; }
bool forget(const String &s) {
  for (size_t i = 0; i < g_saved.size(); ++i) if (g_saved[i] == s) { g_saved.erase(g_saved.begin() + i); return true; }
  return false;
}
bool scanJson(String &out, String &err) {
  out = "[{\"ssid\":\"SimNet\",\"rssi\":-52,\"secure\":true,\"saved\":true},"
        "{\"ssid\":\"Cafe Guest\",\"rssi\":-71,\"secure\":false,\"saved\":false},"
        "{\"ssid\":\"Neighbour 5G\",\"rssi\":-83,\"secure\":true,\"saved\":false}]";
  return true;
}
void setTime(time_t, int) {}
bool timeValid() { return true; }
}  // namespace net

// ------------------------------------------------------------------ usb
namespace { bool g_usbOn = true, g_drive = false; }
namespace usb {
void begin() {}
void tick() {}
bool enabled() { return g_usbOn; }
void setEnabled(bool on) { g_usbOn = on; }
bool running() { return true; }
bool hostConnected() { return true; }
String ip() { return "192.168.7.1"; }
bool driveAvailable() { return true; }
bool driveMode() { return g_drive; }
bool setDriveMode(bool on, String &err) {
  if (on == g_drive) return true;
  g_drive = on;
  storage::setLentToUsb(on);
  if (!on) storage::remount();
  return true;
}
}  // namespace usb

// ------------------------------------------------------------------ ui
namespace ui {
void begin() {}
void draw() {}
void tick() {}
void toast(const char *) {}
int batteryLevel() { return 76; }
int batteryMv() { return 3980; }
bool charging() { return false; }
}  // namespace ui

// ------------------------------------------------------------------ firmware
namespace fw {
bool underLauncher() { return getenv("WUI_SIM_LAUNCHER") != nullptr; }
bool canSelfUpdate() { return !underLauncher(); }
String runningSlot() { return "app0"; }
bool flashFromFile(const String &path, uint8_t *buf, size_t bufLen, const Progress &progress, String &err) {
  File f;
  uint64_t size;
  {
    storage::Guard g;
    f = SD.open(path, FILE_READ);
    if (!f) { err = "cannot read " + path; return false; }
    size = f.size();
    size_t n = f.read(buf, WUI_FW_HEAD);
    FwInfo info;
    if (!wui_check_app_image(buf, n, info, err)) return false;
    f.seek(0);
  }
  for (;;) {
    int got;
    { storage::Guard g; got = f.read(buf, bufLen); }
    if (got <= 0) break;
    if (progress && !progress(got, size)) { err = "cancelled"; return false; }
    delay(2);
  }
  printf("[sim] would flash %s (%llu bytes)\n", path.c_str(), (unsigned long long)size);
  return true;
}
void rebootToLauncher() { printf("[sim] reboot to Launcher requested (ignored)\n"); }
void restart() { ESP.restart(); }
}  // namespace fw
