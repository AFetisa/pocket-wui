// Sign-in, status and device settings: Wi-Fi, clock, USB, firmware, Launcher.
#include "api.h"
#include "auth.h"
#include "config.h"
#include "firmware.h"
#include "fwimage.h"
#include "http_util.h"
#include "jobs.h"
#include "net.h"
#include "storage.h"
#include "stream.h"
#include "ui.h"
#include "usb.h"
#include "web_assets.h"
#include <SD.h>

using namespace http;

namespace {

const char *b(bool v) { return v ? "true" : "false"; }

// ------------------------------------------------------------------ UI + auth

esp_err_t hIndex(httpd_req_t *req) {
  char etag[40];
  snprintf(etag, sizeof(etag), "\"%s\"", WEB_INDEX_ETAG);
  if (header(req, "If-None-Match", 48) == etag) {
    httpd_resp_set_status(req, "304 Not Modified");
    httpd_resp_set_hdr(req, "ETag", etag);
    return httpd_resp_send(req, nullptr, 0);
  }
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "ETag", etag);
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
  return httpd_resp_send(req, (const char *)WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
}

esp_err_t hLogin(httpd_req_t *req) {
  if (auth::lockedOut())
    return sendErr(req, "429 Too Many Requests", "too many attempts — wait a minute");
  String pw = jsonField(readBody(req, 512), "password");
  if (pw.isEmpty() || !auth::checkPassword(pw)) {
    auth::noteFailure();
    delay(400);                         // blunt the guess rate
    return sendErr(req, "401 Unauthorized", "wrong password");
  }
  auth::noteSuccess();
  setSessionCookie(req);
  return sendOk(req);
}

// GET /login?k=<token> — the URL inside the on-screen sign-in QR. A good token
// becomes a session cookie and a redirect to the UI; anything else lands on the
// normal password prompt with a hint about why.
esp_err_t hQrLogin(httpd_req_t *req) {
  const char *to = "/";
  if (auth::lockedOut()) {
    to = "/?qr=locked";
  } else if (auth::consumeLoginToken(query(req, "k"))) {
    auth::noteSuccess();
    setSessionCookie(req);
  } else {
    auth::noteFailure();
    to = "/?qr=expired";
  }
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", to);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
  return httpd_resp_send(req, nullptr, 0);
}

esp_err_t hLogout(httpd_req_t *req) {
  String c = header(req, "Cookie");
  int i = c.indexOf("wui=");
  if (i >= 0) {
    int e = c.indexOf(';', i);
    auth::dropSession(c.substring(i + 4, e < 0 ? c.length() : e));
  }
  httpd_resp_set_hdr(req, "Set-Cookie", "wui=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
  return sendOk(req);
}

// Whether this browser is signed in — 200 either way, so the sign-in page can
// ask without an error showing up in the browser console.
esp_err_t hSession(httpd_req_t *req) {
  return sendJson(req, "200 OK", String("{\"signed_in\":") + b(authed(req)) + "}");
}

// ------------------------------------------------------------------ status

esp_err_t hStatus(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  bool sd = storage::mounted() && !storage::lentToUsb();
  String j = "{";
  j += "\"name\":\"" WUI_NAME "\",\"fw\":\"" WUI_FW_VERSION "\",";
  j += "\"chip\":\"" + String(ESP.getChipModel()) + " @" + String(ESP.getCpuFreqMHz()) + "MHz\",";
  j += "\"heap\":" + String((uint32_t)ESP.getFreeHeap()) + ",";
  j += "\"heap_min\":" + String((uint32_t)ESP.getMinFreeHeap()) + ",";
  j += "\"psram\":" + String((uint32_t)ESP.getFreePsram()) + ",";
  j += "\"uptime\":" + String((unsigned long)(millis() / 1000)) + ",";
  j += "\"host\":\"" + jsonEscape(net::hostname()) + "\",";
  j += "\"ssid\":\"" + jsonEscape(net::ssid()) + "\",";
  j += "\"ap\":" + String(b(net::isAP())) + ",";
  j += "\"ap_up\":" + String(b(net::apUp())) + ",";
  j += "\"ap_ssid\":\"" + jsonEscape(net::apSsid()) + "\",";
  j += "\"ap_ip\":\"" + net::apIp() + "\",";
  j += "\"ap_clients\":" + String(net::apClients()) + ",";
  j += "\"keep_ap\":" + String(b(net::keepAp())) + ",";
  j += "\"ip\":\"" + net::ip() + "\",";
  j += "\"rssi\":" + String(net::rssi()) + ",";
  j += "\"sd\":" + String(b(sd)) + ",";
  j += "\"sd_lent\":" + String(b(storage::lentToUsb())) + ",";
  j += "\"sd_type\":\"" + String(sd ? storage::cardType() : "none") + "\",";
  j += "\"sd_hz\":" + String((unsigned long)storage::clockHz()) + ",";
  j += "\"sd_total\":" + String((unsigned long long)(sd ? storage::totalBytes() : 0)) + ",";
  j += "\"sd_used\":" + String((unsigned long long)(sd ? storage::usedBytes() : 0)) + ",";
  j += "\"bat\":" + String(ui::batteryLevel()) + ",";
  j += "\"bat_mv\":" + String(ui::batteryMv()) + ",";
  j += "\"charging\":" + String(b(ui::charging())) + ",";
  j += "\"usb\":{\"enabled\":" + String(b(usb::enabled())) + ",\"running\":" + b(usb::running()) +
       ",\"host\":" + b(usb::hostConnected()) + ",\"ip\":\"" + usb::ip() + "\",\"drive\":" + b(usb::driveMode()) +
       ",\"drive_ok\":" + b(usb::driveAvailable()) + "},";
  j += "\"launcher\":" + String(b(fw::underLauncher())) + ",";
  j += "\"can_update\":" + String(b(fw::canSelfUpdate())) + ",";
  j += "\"slot\":\"" + fw::runningSlot() + "\",";
  j += "\"time_ok\":" + String(b(net::timeValid())) + ",";
  j += "\"now\":" + String((unsigned long)time(nullptr)) + ",";
  j += "\"dav\":" + String(b(WUI_WEBDAV)) + ",";
  j += "\"job\":" + String(b(jobs::busy())) + ",";
  j += "\"streams\":" + String(stream::active());
  j += "}";
  return sendJson(req, "200 OK", j);
}

// ------------------------------------------------------------------ Wi-Fi

esp_err_t hWifi(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  String body = readBody(req, 512);
  String ssid = jsonField(body, "ssid"), pass = jsonField(body, "password");
  String err;
  bool ok = net::join(ssid, pass, err);
  return sendJson(req, "200 OK",
                  String("{\"connected\":") + b(ok) + ",\"ip\":\"" + net::ip() + "\",\"ssid\":\"" +
                  jsonEscape(net::ssid()) + "\",\"host\":\"" + jsonEscape(net::hostname()) +
                  "\",\"detail\":\"" + jsonEscape(ok ? String("joined") : err) + "\"}");
}

esp_err_t hWifiScan(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  String out, err;
  if (!net::scanJson(out, err)) return sendErr(req, "503 Service Unavailable", err);
  return sendJson(req, "200 OK", "{\"networks\":" + out + "}");
}

esp_err_t hWifiSaved(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  String j = "{\"saved\":[";
  auto v = net::savedNetworks();
  for (size_t i = 0; i < v.size(); ++i) j += (i ? ",\"" : "\"") + jsonEscape(v[i]) + "\"";
  return sendJson(req, "200 OK", j + "]}");
}

esp_err_t hWifiForget(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  String ssid = jsonField(readBody(req, 256), "ssid");
  if (!net::forget(ssid)) return sendErr(req, "404 Not Found", "not a saved network: " + ssid);
  return sendOk(req);
}

// POST /api/wifi/ap {"password":"…"} and/or {"keep":true}
esp_err_t hWifiAp(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  String body = readBody(req, 256);
  String pw = jsonField(body, "password"), keep = jsonField(body, "keep"), err;
  if (!pw.isEmpty() && !net::setApPassword(pw, err)) return sendErr(req, "400 Bad Request", err);
  if (!keep.isEmpty()) net::setKeepAp(keep == "true");
  return sendOk(req);
}

esp_err_t hName(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  String err;
  if (!net::setHostname(jsonField(readBody(req, 256), "name"), err)) return sendErr(req, "400 Bad Request", err);
  return sendOk(req);
}

// POST /api/time {"epoch":1727000000,"tz":-180}
esp_err_t hTime(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  String body = readBody(req, 128);
  net::setTime((time_t)strtoull(jsonField(body, "epoch").c_str(), nullptr, 10), jsonField(body, "tz").toInt());
  return sendOk(req);
}

// ------------------------------------------------------------------ device

esp_err_t hPasswd(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  String err;
  if (!auth::setPassword(jsonField(readBody(req, 512), "password"), err)) return sendErr(req, "400 Bad Request", err);
  setSessionCookie(req);                // the old sessions are gone; keep this one signed in
  return sendOk(req);
}

esp_err_t hReboot(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  sendOk(req);
  delay(300);
  fw::restart();
  return ESP_OK;
}

esp_err_t hLauncher(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  if (!fw::underLauncher()) return sendErr(req, "409 Conflict", "M5Launcher is not installed on this device");
  sendOk(req);
  delay(300);
  fw::rebootToLauncher();
  return ESP_OK;
}

// POST /api/firmware/check?path=  -> what the file is, and what we can do with it
esp_err_t hFwCheck(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!queryPath(req, "path", path) || path == "/") return sendErr(req, "400 Bad Request", "bad path");
  uint8_t head[WUI_FW_HEAD];
  size_t n = 0;
  uint64_t size = 0;
  {
    storage::Guard g;
    File f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory()) return sendErr(req, "404 Not Found", "no such file: " + path);
    size = f.size();
    n = f.read(head, sizeof(head));
    f.close();
  }
  FwInfo info;
  String err;
  bool ok = wui_check_app_image(head, n, info, err);
  return sendJson(req, "200 OK",
                  String("{\"ok\":") + b(ok) + ",\"reason\":\"" + jsonEscape(err) + "\",\"size\":" +
                  String((unsigned long long)size) + ",\"version\":\"" + jsonEscape(info.version) +
                  "\",\"launcher\":" + b(fw::underLauncher()) + ",\"can_update\":" + b(fw::canSelfUpdate()) + "}");
}

esp_err_t hFwInstall(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String path, err;
  if (!queryPath(req, "path", path) || path == "/") return sendErr(req, "400 Bad Request", "bad path");
  if (!jobs::startFirmware(path, err)) return sendErr(req, "409 Conflict", err);
  return sendOk(req);
}

// POST /api/usb {"drive":true|false} and/or {"enabled":true|false}
esp_err_t hUsb(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  String body = readBody(req, 128);
  String drive = jsonField(body, "drive"), en = jsonField(body, "enabled"), err;
  if (!en.isEmpty()) usb::setEnabled(en == "true");
  if (!drive.isEmpty() && !usb::setDriveMode(drive == "true", err)) return sendErr(req, "409 Conflict", err);
  return sendOk(req);
}

esp_err_t hNotFound(httpd_req_t *req, httpd_err_code_t) {
  // Anything unknown bounces to the UI. That is also what makes the captive
  // portal work: a phone's connectivity probe gets redirected here.
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/");
  return httpd_resp_send(req, nullptr, 0);
}

}  // namespace

namespace api {

void registerDevice(httpd_handle_t h) {
  reg(h, "/",                 HTTP_GET,  hIndex);
  reg(h, "/login",            HTTP_GET,  hQrLogin);
  reg(h, "/api/login",        HTTP_POST, hLogin);
  reg(h, "/api/logout",       HTTP_POST, hLogout);
  reg(h, "/api/session",      HTTP_GET,  hSession);
  reg(h, "/api/status",       HTTP_GET,  hStatus);
  reg(h, "/api/wifi",         HTTP_POST, hWifi);
  reg(h, "/api/wifi/scan",    HTTP_GET,  hWifiScan);
  reg(h, "/api/wifi/saved",   HTTP_GET,  hWifiSaved);
  reg(h, "/api/wifi/forget",  HTTP_POST, hWifiForget);
  reg(h, "/api/wifi/ap",      HTTP_POST, hWifiAp);
  reg(h, "/api/name",         HTTP_POST, hName);
  reg(h, "/api/time",         HTTP_POST, hTime);
  reg(h, "/api/passwd",       HTTP_POST, hPasswd);
  reg(h, "/api/reboot",       HTTP_POST, hReboot);
  reg(h, "/api/launcher",     HTTP_POST, hLauncher);
  reg(h, "/api/firmware/check",   HTTP_POST, hFwCheck);
  reg(h, "/api/firmware/install", HTTP_POST, hFwInstall);
  reg(h, "/api/usb",          HTTP_POST, hUsb);
  httpd_register_err_handler(h, HTTPD_404_NOT_FOUND, hNotFound);
}

}  // namespace api
