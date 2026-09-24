#include "net.h"
#include "config.h"
#include "http_util.h"
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_sntp.h>
#include <sys/time.h>

namespace {

Preferences prefs;
String   g_apPass, g_apSsid, g_host, g_tz = "UTC0";
bool     g_apUp = false;
bool     g_keepAp = false;
DNSServer g_dns;
bool     g_dnsOn = false;

// Rejoin state machine (runs while there is no station link).
bool     g_trying = false;
uint32_t g_tryDeadline = 0, g_nextTry = 0, g_lostAt = 0, g_linkUpAt = 0;
int      g_tryIdx = 0;
bool     g_wasLinked = false;

String randomPass() {
  static const char *A = "abcdefghijkmnopqrstuvwxyz23456789";
  String s;
  for (int i = 0; i < 10; ++i) s += A[esp_random() % strlen(A)];
  return s;
}

String slotKey(int i, char kind) { return String("n") + String(i) + kind; }

std::vector<std::pair<String, String>> g_saved;   // RAM copy of the NVS list

void readSaved() {
  g_saved.clear();
  for (int i = 0; i < WUI_MAX_NETWORKS; ++i) {
    String s = prefs.getString(slotKey(i, 's').c_str(), "");
    if (s.length()) g_saved.push_back({s, prefs.getString(slotKey(i, 'p').c_str(), "")});
  }
}

const std::vector<std::pair<String, String>> &loadSaved() { return g_saved; }

void storeSaved(const std::vector<std::pair<String, String>> &v) {
  g_saved = v;
  for (int i = 0; i < WUI_MAX_NETWORKS; ++i) {
    if (i < (int)v.size()) {
      prefs.putString(slotKey(i, 's').c_str(), v[i].first);
      prefs.putString(slotKey(i, 'p').c_str(), v[i].second);
    } else {
      prefs.remove(slotKey(i, 's').c_str());
      prefs.remove(slotKey(i, 'p').c_str());
    }
  }
}

void startMDNS() {
  MDNS.end();
  if (MDNS.begin(g_host.c_str())) {
    MDNS.addService("http", "tcp", WUI_HTTP_PORT);
#if WUI_WEBDAV
    MDNS.addService("webdav", "tcp", WUI_HTTP_PORT);
    MDNS.addServiceTxt("webdav", "tcp", "path", "/dav/");
#endif
  }
}

void startDNS() {
  if (g_dnsOn) return;
  // Captive portal: every name resolves to us, so a phone that joins the AP
  // pops up "sign in to network" and lands straight on the WUI.
  g_dns.setErrorReplyCode(DNSReplyCode::NoError);
  g_dnsOn = g_dns.start(53, "*", WiFi.softAPIP());
}

void stopDNS() {
  if (!g_dnsOn) return;
  g_dns.stop();
  g_dnsOn = false;
}

void startAP() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(g_apSsid.c_str(), g_apPass.c_str());
  g_apUp = true;
  startDNS();
  log_i("AP up: %s -> %s", g_apSsid.c_str(), WiFi.softAPIP().toString().c_str());
}

void stopAP() {
  stopDNS();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  g_apUp = false;
}

void onLinked() {
  g_wasLinked = true;
  g_linkUpAt = millis();
  g_lostAt = 0;
  startMDNS();
  configTzTime(g_tz.c_str(), "pool.ntp.org", "time.google.com");
  log_i("joined %s -> %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

bool tryJoinBlocking(const String &ssid, const String &pass) {
  if (ssid.isEmpty()) return false;
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WUI_STA_TIMEOUT_MS) delay(120);
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect(false, true);
  return false;
}

}  // namespace

namespace net {

void begin() {
  prefs.begin("wui-net", false);
  g_apPass = prefs.getString("appass", "");
  if (g_apPass.length() < 8) { g_apPass = randomPass(); prefs.putString("appass", g_apPass); }
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  char suffix[8];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  g_apSsid = String(WUI_AP_PREFIX) + suffix;
  g_host = prefs.getString("host", WUI_HOSTNAME);
  g_keepAp = prefs.getBool("keepap", false);
  g_tz = prefs.getString("tz", "UTC0");
  setenv("TZ", g_tz.c_str(), 1);
  tzset();

  readSaved();
  // Firmware 1.x kept a single network under "ssid"/"pass".
  String legacy = prefs.getString("ssid", "");
  if (legacy.length()) {
    auto v = loadSaved();
    v.insert(v.begin(), {legacy, prefs.getString("pass", "")});
    if (v.size() > WUI_MAX_NETWORKS) v.resize(WUI_MAX_NETWORKS);
    storeSaved(v);
    prefs.remove("ssid");
    prefs.remove("pass");
  }

  WiFi.persistent(false);
  WiFi.setHostname(g_host.c_str());
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);            // sleep murders upload throughput

  auto saved = loadSaved();
  bool linked = false;
  if (!saved.empty()) {
    // Strongest saved network in range first; then one blind try at the first
    // saved network in case it is hidden.
    int n = WiFi.scanNetworks();
    std::vector<std::pair<int, int>> order;   // rssi, index
    for (int i = 0; i < n; ++i)
      for (int k = 0; k < (int)saved.size(); ++k)
        if (WiFi.SSID(i) == saved[k].first) order.push_back({WiFi.RSSI(i), k});
    WiFi.scanDelete();
    std::sort(order.begin(), order.end(), [](auto &a, auto &b) { return a.first > b.first; });
    for (auto &o : order)
      if ((linked = tryJoinBlocking(saved[o.second].first, saved[o.second].second))) break;
    if (!linked && order.empty()) linked = tryJoinBlocking(saved[0].first, saved[0].second);
  }
  if (linked) {
    if (g_keepAp) startAP();
    onLinked();
  } else {
    startAP();
    startMDNS();
    g_nextTry = millis() + WUI_REJOIN_EVERY_MS;
  }
}

void tick() {
  if (g_dnsOn) g_dns.processNextRequest();
  uint32_t now = millis();
  bool linked = WiFi.status() == WL_CONNECTED;

  if (g_trying) {
    if (linked) {
      g_trying = false;
      onLinked();
    } else if ((int32_t)(now - g_tryDeadline) >= 0) {
      WiFi.disconnect(false, true);
      g_trying = false;
      if (++g_tryIdx >= (int)loadSaved().size()) { g_tryIdx = 0; g_nextTry = now + WUI_REJOIN_EVERY_MS; }
      else g_nextTry = now;
    }
    return;
  }

  if (linked) {
    if (!g_wasLinked) onLinked();
    // Back on the home network: drop the AP we raised for the outage, but never
    // while someone is still on it.
    if (g_apUp && !g_keepAp && apClients() == 0 && now - g_linkUpAt > 5000) stopAP();
    return;
  }

  if (g_wasLinked) {
    // Short drops are the station's own auto-reconnect's job. A long one
    // raises the AP so the device stays reachable, and our rotation takes over.
    if (!g_lostAt) g_lostAt = now;
    if (now - g_lostAt < WUI_LINK_LOST_MS) return;
    g_wasLinked = false;
    log_w("station link lost — raising the AP");
    if (!g_apUp) startAP();
    g_nextTry = now;
  }

  const auto &saved = loadSaved();
  if (saved.empty() || apClients() > 0 || (int32_t)(now - g_nextTry) < 0) return;
  if (g_tryIdx >= (int)saved.size()) g_tryIdx = 0;
  if (!g_apUp) startAP();
  WiFi.begin(saved[g_tryIdx].first.c_str(), saved[g_tryIdx].second.c_str());
  g_trying = true;
  g_tryDeadline = now + WUI_STA_TIMEOUT_MS;
}

bool isAP()       { return WiFi.status() != WL_CONNECTED; }
bool apUp()       { return g_apUp; }
String apSsid()   { return g_apSsid; }
String apPassword() { return g_apPass; }
String apIp()     { return WiFi.softAPIP().toString(); }
int apClients()   { return g_apUp ? WiFi.softAPgetStationNum() : 0; }
String ssid()     { return isAP() ? g_apSsid : WiFi.SSID(); }
String ip()       { return isAP() ? apIp() : WiFi.localIP().toString(); }
int rssi()        { return isAP() ? 0 : WiFi.RSSI(); }
String hostname() { return g_host; }
bool keepAp()     { return g_keepAp; }

bool setApPassword(const String &pw, String &err) {
  if (pw.length() < 8 || pw.length() > 63) { err = "the Wi-Fi key must be 8–63 characters"; return false; }
  g_apPass = pw;
  prefs.putString("appass", pw);
  if (g_apUp) { stopDNS(); WiFi.softAP(g_apSsid.c_str(), g_apPass.c_str()); startDNS(); }
  return true;
}

bool setHostname(const String &name, String &err) {
  String n = name;
  n.toLowerCase();
  if (n.isEmpty() || n.length() > 32) { err = "name must be 1–32 characters"; return false; }
  for (unsigned i = 0; i < n.length(); ++i) {
    char c = n[i];
    if (!(isalnum((unsigned char)c) || (c == '-' && i > 0 && i + 1 < n.length()))) {
      err = "use letters, digits and dashes only";
      return false;
    }
  }
  g_host = n;
  prefs.putString("host", n);
  WiFi.setHostname(n.c_str());
  startMDNS();
  return true;
}

void setKeepAp(bool on) {
  g_keepAp = on;
  prefs.putBool("keepap", on);
  if (on && !g_apUp) startAP();
  // Turning it off drops the AP from tick(), once nobody is on it.
}

bool join(const String &s, const String &p, String &err) {
  if (s.isEmpty()) { err = "network name required"; return false; }
  g_trying = false;
  // Keep the AP up while we try, so a phone configuring us from the AP keeps
  // its connection and gets this answer. tick() drops it once it is empty.
  if (!g_apUp) WiFi.mode(WIFI_STA);
  if (!tryJoinBlocking(s, p)) {
    err = "could not join " + s + " (wrong password, or out of range?)";
    auto saved = loadSaved();
    if (!saved.empty() && saved[0].first != s) tryJoinBlocking(saved[0].first, saved[0].second);
    if (WiFi.status() == WL_CONNECTED) onLinked();
    else if (!g_apUp) startAP();
    return false;
  }
  auto v = loadSaved();
  v.erase(std::remove_if(v.begin(), v.end(), [&](auto &e) { return e.first == s; }), v.end());
  v.insert(v.begin(), {s, p});
  if (v.size() > WUI_MAX_NETWORKS) v.resize(WUI_MAX_NETWORKS);
  storeSaved(v);
  onLinked();
  return true;
}

std::vector<String> savedNetworks() {
  std::vector<String> out;
  for (auto &e : loadSaved()) out.push_back(e.first);
  return out;
}

bool forget(const String &s) {
  auto v = loadSaved();
  size_t before = v.size();
  v.erase(std::remove_if(v.begin(), v.end(), [&](auto &e) { return e.first == s; }), v.end());
  storeSaved(v);
  return v.size() != before;
}

bool scanJson(String &out, String &err) {
  if (g_trying) { err = "busy reconnecting — try again in a few seconds"; return false; }
  int n = WiFi.scanNetworks(false, false);
  if (n < 0) { err = "scan failed"; return false; }
  auto saved = savedNetworks();
  std::vector<int> best;                      // strongest entry per SSID
  for (int i = 0; i < n; ++i) {
    if (WiFi.SSID(i).isEmpty()) continue;
    bool dup = false;
    for (int &b : best)
      if (WiFi.SSID(b) == WiFi.SSID(i)) { if (WiFi.RSSI(i) > WiFi.RSSI(b)) b = i; dup = true; break; }
    if (!dup) best.push_back(i);
  }
  std::sort(best.begin(), best.end(), [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });
  out = "[";
  for (size_t k = 0; k < best.size(); ++k) {
    int i = best[k];
    bool isSaved = std::find(saved.begin(), saved.end(), WiFi.SSID(i)) != saved.end();
    out += (k ? "," : "");
    out += "{\"ssid\":\"" + http::jsonEscape(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
           ",\"secure\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") +
           ",\"saved\":" + (isSaved ? "true" : "false") + "}";
  }
  out += "]";
  WiFi.scanDelete();
  return true;
}

void setTime(time_t epoch, int tzOffsetMin) {
  // JS getTimezoneOffset() is minutes *behind* UTC; POSIX TZ uses the same sign.
  char tz[24];
  int m = tzOffsetMin < 0 ? -tzOffsetMin : tzOffsetMin;
  snprintf(tz, sizeof(tz), "UTC%c%02d:%02d", tzOffsetMin < 0 ? '-' : '+', m / 60, m % 60);
  if (g_tz != tz) { g_tz = tz; prefs.putString("tz", g_tz); }
  setenv("TZ", g_tz.c_str(), 1);
  tzset();
  if (epoch > 1700000000) {
    struct timeval tv = {epoch, 0};
    settimeofday(&tv, nullptr);
  }
}

bool timeValid() { return time(nullptr) > 1700000000; }

}  // namespace net
