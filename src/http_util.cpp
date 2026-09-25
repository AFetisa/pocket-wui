#include "http_util.h"
#include "auth.h"
#include "config.h"
#include "paths.h"
#include "storage.h"

namespace http {

uint8_t *g_buf = nullptr;

String jsonEscape(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (unsigned i = 0; i < s.length(); ++i) {
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

String query(httpd_req_t *req, const char *key) {
  size_t len = httpd_req_get_url_query_len(req) + 1;
  if (len <= 1) return String();
  char *q = (char *)malloc(len);
  if (!q) return String();
  String out;
  if (httpd_req_get_url_query_str(req, q, len) == ESP_OK) {
    char val[WUI_MAX_PATH * 3 + 8];
    if (httpd_query_key_value(q, key, val, sizeof(val)) == ESP_OK) out = val;
  }
  free(q);
  return wui_url_decode(out.c_str());
}

bool queryPath(httpd_req_t *req, const char *key, String &out) {
  String raw = query(req, key);
  if (raw.isEmpty()) raw = "/";
  return wui_safe_path(raw.c_str(), out);
}

String header(httpd_req_t *req, const char *name, size_t max) {
  size_t len = httpd_req_get_hdr_value_len(req, name) + 1;
  if (len <= 1 || len > max) return String();
  char *buf = (char *)malloc(len);
  if (!buf) return String();
  String v;
  if (httpd_req_get_hdr_value_str(req, name, buf, len) == ESP_OK) v = buf;
  free(buf);
  return v;
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

String jsonField(const String &body, const char *key) {
  String pat = String("\"") + key + "\"";
  int k = body.indexOf(pat);
  if (k < 0) return String();
  int c = body.indexOf(':', k + pat.length());
  if (c < 0) return String();
  int i = c + 1;
  while (i < (int)body.length() && body[i] == ' ') ++i;
  if (i < (int)body.length() && body[i] != '"') {           // bare number / true / false
    int e = i;
    while (e < (int)body.length() && body[e] != ',' && body[e] != '}' && body[e] != ' ') ++e;
    return body.substring(i, e);
  }
  String out;
  for (unsigned j = i + 1; j < body.length(); ++j) {
    char ch = body[j];
    if (ch == '\\' && j + 1 < body.length()) {
      char n = body[++j];
      switch (n) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'u': j += 4; break;        // not needed for our fields
        default:  out += n;
      }
      continue;
    }
    if (ch == '"') break;
    out += ch;
  }
  return out;
}

bool rawSend(httpd_req_t *req, const char *data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    int n = httpd_send(req, data + sent, len - sent);
    if (n <= 0) return false;
    sent += n;
  }
  return true;
}

static String cookieToken(httpd_req_t *req) {
  String c = header(req, "Cookie");
  int i = c.indexOf("wui=");
  if (i < 0 || (i != 0 && c[i - 1] != ' ' && c[i - 1] != ';')) return String();
  int end = c.indexOf(';', i);
  String tok = c.substring(i + 4, end < 0 ? c.length() : end);
  tok.trim();
  return tok;
}

bool authed(httpd_req_t *req) { return auth::validSession(cookieToken(req)); }

bool requireAuth(httpd_req_t *req) {
  if (authed(req)) return true;
  sendErr(req, "401 Unauthorized", "not signed in");
  return false;
}

bool requireSD(httpd_req_t *req) {
  if (storage::lentToUsb()) {
    sendErr(req, "423 Locked", "the SD card is in USB drive mode — eject it on the computer first");
    return false;
  }
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

void setSessionCookie(httpd_req_t *req) {
  // The header value must outlive the call that sends the response, hence static.
  static String cookie;
  cookie = "wui=" + auth::newSession() + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" +
           String(WUI_SESSION_TTL_S);
  httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());
}

void reg(httpd_handle_t h, const char *uri, httpd_method_t m, esp_err_t (*fn)(httpd_req_t *)) {
  httpd_uri_t u = {};
  u.uri = uri;
  u.method = m;
  u.handler = fn;
  if (httpd_register_uri_handler(h, &u) != ESP_OK) log_e("cannot register %s", uri);
}

}  // namespace http
