#include "net.h"
#include "config.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_random.h>

namespace {

Preferences prefs;
bool     g_ap = true;
String   g_apPass;
uint32_t g_nextCheck = 0;

String randomPass() {
  static const char *A = "abcdefghijkmnopqrstuvwxyz23456789";
  String s;
  for (int i = 0; i < 10; ++i) s += A[esp_random() % strlen(A)];
  return s;
}

void startAP() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(WUI_AP_SSID, g_apPass.c_str());
  g_ap = true;
  log_i("AP up: %s / %s -> %s", WUI_AP_SSID, g_apPass.c_str(), WiFi.softAPIP().toString().c_str());
}

bool tryJoin(const String &ssid, const String &pass) {
  if (ssid.isEmpty()) return false;
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WUI_STA_TIMEOUT_MS) delay(120);
  return WiFi.status() == WL_CONNECTED;
}

void startMDNS() {
  MDNS.end();
  if (MDNS.begin(WUI_HOSTNAME)) MDNS.addService("http", "tcp", WUI_HTTP_PORT);
}

}  // namespace

namespace net {

void begin() {
  prefs.begin("wui-net", false);
  g_apPass = prefs.getString("appass", "");
  if (g_apPass.length() < 8) { g_apPass = randomPass(); prefs.putString("appass", g_apPass); }

  WiFi.persistent(false);
  WiFi.setHostname(WUI_HOSTNAME);
  WiFi.setSleep(false);            // sleep murders upload throughput

  String ss = prefs.getString("ssid", "");
  String pw = prefs.getString("pass", "");
  if (tryJoin(ss, pw)) {
    g_ap = false;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    log_i("joined %s -> %s", ss.c_str(), WiFi.localIP().toString().c_str());
  } else {
    startAP();
  }
  startMDNS();
}

bool isAP()        { return g_ap; }
String apPassword(){ return g_apPass; }
String ssid()      { return g_ap ? String(WUI_AP_SSID) : WiFi.SSID(); }
String ip()        { return g_ap ? WiFi.softAPIP().toString() : WiFi.localIP().toString(); }
int    rssi()      { return g_ap ? 0 : WiFi.RSSI(); }

bool join(const String &s, const String &p, String &err) {
  if (s.isEmpty()) { err = "ssid required"; return false; }
  String oldS = prefs.getString("ssid", ""), oldP = prefs.getString("pass", "");
  if (tryJoin(s, p)) {
    prefs.putString("ssid", s);
    prefs.putString("pass", p);
    g_ap = false;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    startMDNS();
    return true;
  }
  err = "could not join " + s;
  // Put the radio back the way it was so the client keeps its connection.
  if (!tryJoin(oldS, oldP)) startAP(); else g_ap = false;
  startMDNS();
  return false;
}

void tick() {
  if (g_ap || millis() < g_nextCheck) return;
  g_nextCheck = millis() + 10000;
  if (WiFi.status() == WL_CONNECTED) return;
  log_w("station link lost — retrying");
  String ss = prefs.getString("ssid", ""), pw = prefs.getString("pass", "");
  if (tryJoin(ss, pw)) { startMDNS(); return; }
  startAP();
  startMDNS();
}

}  // namespace net
