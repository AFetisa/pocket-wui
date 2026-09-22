#include "server.h"
#include "config.h"
#include "auth.h"
#include "net.h"
#include "paths.h"
#include "storage.h"
#include "web_assets.h"

#include <Arduino.h>
#include <SD.h>
#include <esp_http_server.h>
#include <esp_system.h>

namespace {

httpd_handle_t g_httpd = nullptr;
uint8_t *g_buf = nullptr;          // shared staging buffer; the HTTP server runs
                                   // every handler on one task, so this is safe.

// ------------------------------------------------------------------ helpers

String jsonEscape(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    switch (c) {
      case '"':  o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n";  break;
      case '\r': o += "\\r";  break;
      case '\t': o += "\\t";  break;
      default:
        if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += c;
    }
  }
  return o;
}

esp_err_t sendJson(httpd_req_t *req, const char *status, const String &body) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, body.c_str(), body.length());
}

esp_err_t sendErr(httpd_req_t *req, const char *status, const String &msg) {
  return sendJson(req, status, "{\"error\":\"" + jsonEscape(msg) + "\"}");
}

esp_err_t sendOk(httpd_req_t *req) { return sendJson(req, "200 OK", "{\"ok\":true}"); }

// Returns "" when the key is absent.
String query(httpd_req_t *req, const char *key) {
  size_t len = httpd_req_get_url_query_len(req) + 1;
  if (len <= 1) return String();
  String raw;
  {
    char *q = (char *)malloc(len);
    if (!q) return String();
    String out;
    if (httpd_req_get_url_query_str(req, q, len) == ESP_OK) {
      char val[WUI_MAX_PATH * 3 + 8];
      if (httpd_query_key_value(q, key, val, sizeof(val)) == ESP_OK) out = val;
    }
    free(q);
    raw = out;
  }
  return wui_url_decode(raw.c_str());
}

bool queryPath(httpd_req_t *req, const char *key, String &out) {
  String raw = query(req, key);
  if (raw.isEmpty()) raw = "/";
  return wui_safe_path(raw.c_str(), out);
}

String cookieToken(httpd_req_t *req) {
  size_t len = httpd_req_get_hdr_value_len(req, "Cookie") + 1;
  if (len <= 1 || len > 512) return String();
  char *buf = (char *)malloc(len);
  if (!buf) return String();
  String tok;
  if (httpd_req_get_hdr_value_str(req, "Cookie", buf, len) == ESP_OK) {
    String c = buf;
    int i = c.indexOf("wui=");
    if (i >= 0 && (i == 0 || c[i - 1] == ' ' || c[i - 1] == ';')) {
      int end = c.indexOf(';', i);
      tok = c.substring(i + 4, end < 0 ? c.length() : end);
      tok.trim();
    }
  }
  free(buf);
  return tok;
}

bool authed(httpd_req_t *req) { return auth::validSession(cookieToken(req)); }

bool requireAuth(httpd_req_t *req) {
  if (authed(req)) return true;
  sendErr(req, "401 Unauthorized", "not signed in");
  return false;
}

bool requireSD(httpd_req_t *req) {
  if (storage::mounted()) return true;
  // The card is only mounted at boot, so one inserted (or reseated) later was
  // unreachable until a reboot. Retry here, at most every few seconds.
  static uint32_t lastTry = 0;
  if (!lastTry || millis() - lastTry > 3000) {
    lastTry = millis();
    if (storage::remount()) return true;
  }
  sendErr(req, "503 Service Unavailable", "no SD card mounted");
  return false;
}

String readBody(httpd_req_t *req, size_t max) {
  if (req->content_len == 0 || req->content_len > max) return String();
  String body;
  body.reserve(req->content_len + 1);
  char chunk[256];
  size_t got = 0;
  int timeouts = 0;
  while (got < req->content_len) {
    int r = httpd_req_recv(req, chunk, std::min(sizeof(chunk), req->content_len - got));
    if (r == HTTPD_SOCK_ERR_TIMEOUT) { if (++timeouts > 2) return String(); continue; }
    if (r <= 0) return String();
    timeouts = 0;
    body.concat(chunk, r);
    got += r;
  }
  return body;
}

// Minimal JSON string-field extraction — the request bodies here are two
// fields wide and always produced by our own UI.
String jsonField(const String &body, const char *key) {
  String pat = String("\"") + key + "\"";
  int k = body.indexOf(pat);
  if (k < 0) return String();
  int c = body.indexOf(':', k + pat.length());
  if (c < 0) return String();
  int q = body.indexOf('"', c);
  if (q < 0) return String();
  String out;
  for (size_t i = q + 1; i < body.length(); ++i) {
    char ch = body[i];
    if (ch == '\\' && i + 1 < body.length()) {
      char n = body[++i];
      switch (n) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'u': i += 4; break;        // not needed for our fields
        default:  out += n;
      }
      continue;
    }
    if (ch == '"') break;
    out += ch;
  }
  return out;
}

// --------------------------------------------------------------- UI + auth

esp_err_t hIndex(httpd_req_t *req) {
  char etag[40];
  snprintf(etag, sizeof(etag), "\"%s\"", WEB_INDEX_ETAG);
  char inm[48] = {0};
  if (httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK &&
      strcmp(inm, etag) == 0) {
    httpd_resp_set_status(req, "304 Not Modified");
    httpd_resp_set_hdr(req, "ETag", etag);
    return httpd_resp_send(req, nullptr, 0);
  }
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "ETag", etag);
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  return httpd_resp_send(req, (const char *)WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
}

esp_err_t hLogin(httpd_req_t *req) {
  if (auth::lockedOut())
    return sendErr(req, "429 Too Many Requests", "too many attempts — wait a minute");
  String body = readBody(req, 512);
  String pw = jsonField(body, "password");
  if (pw.isEmpty() || !auth::checkPassword(pw)) {
    auth::noteFailure();
    delay(400);                         // blunt the guess rate
    return sendErr(req, "401 Unauthorized", "wrong password");
  }
  auth::noteSuccess();
  String tok = auth::newSession();
  String cookie = "wui=" + tok + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" +
                  String(WUI_SESSION_TTL_S);
  httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());
  return sendOk(req);
}

esp_err_t hLogout(httpd_req_t *req) {
  auth::dropSession(cookieToken(req));
  httpd_resp_set_hdr(req, "Set-Cookie", "wui=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
  return sendOk(req);
}

esp_err_t hStatus(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  String j = "{";
  j += "\"fw\":\"" WUI_FW_VERSION "\",";
  j += "\"chip\":\"" + String(ESP.getChipModel()) + " @" + String(ESP.getCpuFreqMHz()) + "MHz\",";
  j += "\"ssid\":\"" + jsonEscape(net::ssid()) + "\",";
  j += "\"ap\":" + String(net::isAP() ? "true" : "false") + ",";
  j += "\"ip\":\"" + net::ip() + "\",";
  j += "\"rssi\":" + String(net::rssi()) + ",";
  j += "\"heap\":" + String((uint32_t)ESP.getFreeHeap()) + ",";
  j += "\"psram\":" + String((uint32_t)ESP.getFreePsram()) + ",";
  j += "\"uptime\":" + String(millis() / 1000) + ",";
  j += "\"sd_type\":\"" + String(storage::cardType()) + "\",";
  j += "\"sd_hz\":" + String((unsigned long)storage::clockHz()) + ",";
  j += "\"sd_total\":" + String((unsigned long long)storage::totalBytes()) + ",";
  j += "\"sd_used\":" + String((unsigned long long)storage::usedBytes());
  j += "}";
  return sendJson(req, "200 OK", j);
}

// -------------------------------------------------------------- filesystem

esp_err_t hList(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  if (!requireSD(req)) return ESP_OK;
  String path;
  if (!queryPath(req, "path", path)) return sendErr(req, "400 Bad Request", "bad path");

  storage::Guard g;
  File dir = SD.open(path);
  if (!dir) return sendErr(req, "404 Not Found", "no such directory: " + path);
  if (!dir.isDirectory()) { dir.close(); return sendErr(req, "400 Bad Request", path + " is a file"); }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  String head = "{\"path\":\"" + jsonEscape(path) + "\",\"entries\":[";
  httpd_resp_send_chunk(req, head.c_str(), head.length());

  bool first = true;
  String out;
  while (true) {
    File f = dir.openNextFile();
    if (!f) break;
    String name = f.name();
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    out += first ? "" : ",";
    first = false;
    out += "{\"name\":\"" + jsonEscape(name) + "\",\"dir\":" + (f.isDirectory() ? "true" : "false") +
           ",\"size\":" + String((unsigned long long)f.size()) +
           ",\"mtime\":" + String((unsigned long)f.getLastWrite()) + "}";
    f.close();
    if (out.length() > 1024) { httpd_resp_send_chunk(req, out.c_str(), out.length()); out = ""; }
  }
  dir.close();
  out += "]}";
  httpd_resp_send_chunk(req, out.c_str(), out.length());
  return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t hMkdir(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!queryPath(req, "path", path) || path == "/") return sendErr(req, "400 Bad Request", "bad path");
  storage::Guard g;
  if (SD.exists(path)) {
    // Idempotent for a directory: a folder upload re-asserts every parent it
    // walks past. Only a name already taken by a *file* is a real conflict.
    File probe = SD.open(path);
    bool isDir = probe && probe.isDirectory();
    if (probe) probe.close();
    if (isDir) return sendOk(req);
    return sendErr(req, "409 Conflict", "already exists: " + path);
  }
  if (!SD.mkdir(path)) return sendErr(req, "500 Internal Server Error", "mkdir failed: " + path);
  return sendOk(req);
}

esp_err_t hTouch(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!queryPath(req, "path", path) || path == "/") return sendErr(req, "400 Bad Request", "bad path");
  storage::Guard g;
  if (SD.exists(path)) return sendErr(req, "409 Conflict", "already exists: " + path);
  File f = SD.open(path, FILE_WRITE);
  if (!f) return sendErr(req, "500 Internal Server Error", "cannot create " + path);
  f.close();
  storage::invalidateUsage();
  return sendOk(req);
}

esp_err_t hDelete(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!queryPath(req, "path", path) || path == "/") return sendErr(req, "400 Bad Request", "bad path");
  storage::Guard g;
  storage::closeUpload(true);
  String err;
  if (!storage::removeRecursive(path, err)) return sendErr(req, "500 Internal Server Error", err);
  return sendOk(req);
}

esp_err_t hRename(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String from, to;
  if (!queryPath(req, "from", from) || !queryPath(req, "to", to) || from == "/" || to == "/")
    return sendErr(req, "400 Bad Request", "bad path");
  storage::Guard g;
  storage::closeUpload(true);
  if (!SD.exists(from)) return sendErr(req, "404 Not Found", "no such path: " + from);
  if (SD.exists(to))    return sendErr(req, "409 Conflict", "already exists: " + to);
  if (!SD.rename(from, to)) return sendErr(req, "500 Internal Server Error", "rename failed");
  return sendOk(req);
}

// ------------------------------------------------------------------ upload
// POST /api/upload?path=/x&offset=N[&final=1] with a raw body chunk.
// offset==0 truncates; any other offset must match the file's current size,
// so a failed chunk is simply retried at the same offset — free resume.

esp_err_t hUpload(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!queryPath(req, "path", path) || path == "/") return sendErr(req, "400 Bad Request", "bad path");
  uint64_t offset = strtoull(query(req, "offset").c_str(), nullptr, 10);
  bool final_ = query(req, "final") == "1";

  storage::Guard g;
  uint64_t actual = 0;
  String err;
  File *f = storage::uploadHandle(path, offset, &actual, err);
  if (!f) {
    if (err == "offset mismatch")
      return sendJson(req, "409 Conflict",
                      "{\"error\":\"offset mismatch\",\"size\":" + String((unsigned long long)actual) + "}");
    return sendErr(req, "500 Internal Server Error", err);
  }

  size_t remaining = req->content_len;
  size_t written = 0;
  int timeouts = 0;
  while (remaining > 0) {
    size_t want = std::min((size_t)WUI_IO_BUF, remaining);
    int got = httpd_req_recv(req, (char *)g_buf, want);
    if (got == HTTPD_SOCK_ERR_TIMEOUT) {
      // A client that announces a Content-Length and then goes quiet must not
      // hold this task (and the SD lock) forever — the whole server is one task.
      if (++timeouts > 2) { f->flush(); return sendErr(req, "408 Request Timeout", "client stalled"); }
      continue;
    }
    timeouts = 0;
    if (got <= 0) { f->flush(); return sendErr(req, "408 Request Timeout", "connection dropped"); }
    size_t w = f->write(g_buf, got);
    storage::uploadWrote(w);            // count it before any error exit: a short
    written += w;                       // write still put those bytes on the card
    if (w != (size_t)got) {
      f->flush();
      return sendErr(req, "507 Insufficient Storage", "short write — card full?");
    }
    remaining -= got;
    delay(0);                           // let the IDLE/wifi tasks breathe
  }
  if (final_) { f->flush(); storage::closeUpload(true); storage::invalidateUsage(); }
  return sendJson(req, "200 OK",
                  "{\"ok\":true,\"size\":" + String((unsigned long long)(offset + written)) + "}");
}

// ---------------------------------------------------------------- download
// Written with raw socket writes so the response carries a real Content-Length
// and supports Range — a 1 GB download survives a Wi-Fi blip instead of
// restarting. httpd_resp_send_chunk() cannot do either.

esp_err_t hDownload(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!queryPath(req, "path", path) || path == "/") return sendErr(req, "400 Bad Request", "bad path");

  storage::Guard g;
  File f = SD.open(path, FILE_READ);
  if (!f) return sendErr(req, "404 Not Found", "no such file: " + path);
  if (f.isDirectory()) { f.close(); return sendErr(req, "400 Bad Request", path + " is a directory"); }

  uint64_t size = f.size();
  uint64_t start = 0, end = size ? size - 1 : 0;
  bool partial = false;

  char range[64] = {0};
  if (httpd_req_get_hdr_value_str(req, "Range", range, sizeof(range)) == ESP_OK &&
      strncmp(range, "bytes=", 6) == 0 && size > 0) {
    const char *p = range + 6;
    char *endp = nullptr;
    uint64_t s = strtoull(p, &endp, 10);
    uint64_t e = end;
    if (endp && *endp == '-' && endp[1]) e = strtoull(endp + 1, nullptr, 10);
    if (s < size) {
      start = s;
      end = std::min(e, size - 1);
      partial = true;
    }
  }

  // "max" caps the body for the console's `cat` — never a partial-content reply.
  uint64_t cap = strtoull(query(req, "max").c_str(), nullptr, 10);
  if (cap && !partial && size > cap) end = cap - 1;

  uint64_t length = size ? (end - start + 1) : 0;
  String name = path.substring(path.lastIndexOf('/') + 1);

  String head;
  head.reserve(320);
  head += partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
  head += "Content-Type: application/octet-stream\r\n";
  head += "Content-Length: " + String((unsigned long long)length) + "\r\n";
  head += "Accept-Ranges: bytes\r\n";
  head += "Cache-Control: no-store\r\n";
  head += "Content-Disposition: attachment; filename=\"" + name + "\"\r\n";
  if (partial)
    head += "Content-Range: bytes " + String((unsigned long long)start) + "-" +
            String((unsigned long long)end) + "/" + String((unsigned long long)size) + "\r\n";
  head += "Connection: keep-alive\r\n\r\n";

  auto rawSend = [&](const char *data, size_t len) -> bool {
    size_t sent = 0;
    while (sent < len) {
      int n = httpd_send(req, data + sent, len - sent);
      if (n <= 0) return false;
      sent += n;
    }
    return true;
  };

  if (!rawSend(head.c_str(), head.length())) { f.close(); return ESP_FAIL; }

  if (length && start) f.seek(start);
  uint64_t left = length;
  while (left > 0) {
    size_t want = (size_t)std::min<uint64_t>(WUI_IO_BUF, left);
    int got = f.read(g_buf, want);
    if (got <= 0) break;
    if (!rawSend((const char *)g_buf, got)) { f.close(); return ESP_FAIL; }
    left -= got;
    delay(0);
  }
  f.close();
  return ESP_OK;
}

// ------------------------------------------------------------ device admin

esp_err_t hWifi(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  String body = readBody(req, 512);
  String ssid = jsonField(body, "ssid"), pass = jsonField(body, "password");
  String err;
  bool ok = net::join(ssid, pass, err);
  return sendJson(req, "200 OK",
                  String("{\"connected\":") + (ok ? "true" : "false") +
                  ",\"ip\":\"" + net::ip() + "\",\"ssid\":\"" + jsonEscape(net::ssid()) +
                  "\",\"detail\":\"" + jsonEscape(ok ? String("joined") : err) + "\"}");
}

esp_err_t hPasswd(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  String pw = jsonField(readBody(req, 512), "password");
  String err;
  if (!auth::setPassword(pw, err)) return sendErr(req, "400 Bad Request", err);
  return sendOk(req);
}

esp_err_t hReboot(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  sendOk(req);
  delay(250);
  storage::closeUpload(true);
  ESP.restart();
  return ESP_OK;
}

esp_err_t hNotFound(httpd_req_t *req, httpd_err_code_t) {
  // Anything unknown bounces to the UI, which keeps captive-portal probes happy.
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/");
  return httpd_resp_send(req, nullptr, 0);
}

void reg(const char *uri, httpd_method_t m, esp_err_t (*h)(httpd_req_t *)) {
  httpd_uri_t u = {};
  u.uri = uri;
  u.method = m;
  u.handler = h;
  httpd_register_uri_handler(g_httpd, &u);
}

}  // namespace

namespace server {

bool begin() {
  if (!g_buf) g_buf = (uint8_t *)heap_caps_malloc(WUI_IO_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!g_buf) g_buf = (uint8_t *)malloc(WUI_IO_BUF);
  if (!g_buf) { log_e("no memory for the I/O buffer"); return false; }

  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port       = WUI_HTTP_PORT;
  cfg.ctrl_port         = 32768;
  cfg.max_uri_handlers  = 16;
  cfg.max_open_sockets  = 5;
  cfg.lru_purge_enable  = true;
  cfg.stack_size        = 8192;
  cfg.recv_wait_timeout = 15;
  cfg.send_wait_timeout = 15;

  if (httpd_start(&g_httpd, &cfg) != ESP_OK) { log_e("httpd_start failed"); return false; }

  reg("/",              HTTP_GET,  hIndex);
  reg("/api/status",    HTTP_GET,  hStatus);
  reg("/api/list",      HTTP_GET,  hList);
  reg("/api/download",  HTTP_GET,  hDownload);
  reg("/api/login",     HTTP_POST, hLogin);
  reg("/api/logout",    HTTP_POST, hLogout);
  reg("/api/mkdir",     HTTP_POST, hMkdir);
  reg("/api/touch",     HTTP_POST, hTouch);
  reg("/api/delete",    HTTP_POST, hDelete);
  reg("/api/rename",    HTTP_POST, hRename);
  reg("/api/upload",    HTTP_POST, hUpload);
  reg("/api/wifi",      HTTP_POST, hWifi);
  reg("/api/passwd",    HTTP_POST, hPasswd);
  reg("/api/reboot",    HTTP_POST, hReboot);
  httpd_register_err_handler(g_httpd, HTTPD_404_NOT_FOUND, hNotFound);
  return true;
}

void stop() {
  if (g_httpd) { httpd_stop(g_httpd); g_httpd = nullptr; }
}

}  // namespace server
