#pragma once
// Request/response helpers shared by every HTTP module (api_files, api_device,
// dav, stream). All handlers run on the esp_http_server task unless noted.
#include <Arduino.h>
#include <esp_http_server.h>

namespace http {

// Shared WUI_IO_BUF staging buffer. Only the server task may use it: every
// synchronous handler runs there, one at a time.
extern uint8_t *g_buf;

String jsonEscape(const String &s);
esp_err_t sendJson(httpd_req_t *req, const char *status, const String &body);
esp_err_t sendErr(httpd_req_t *req, const char *status, const String &msg);
esp_err_t sendOk(httpd_req_t *req);

// Query-string value, percent-decoded; "" when absent.
String query(httpd_req_t *req, const char *key);
// Query value as a sanitised card path ("/" when absent). False if it escapes.
bool queryPath(httpd_req_t *req, const char *key, String &out);
// Request header, "" when absent or longer than max.
String header(httpd_req_t *req, const char *name, size_t max = 512);
// Whole body (up to max bytes), "" when larger or on error.
String readBody(httpd_req_t *req, size_t max);
// Minimal JSON string/number field extraction for our own two-field bodies.
String jsonField(const String &body, const char *key);
// Writes raw bytes to the socket (for hand-built responses). False on error.
bool rawSend(httpd_req_t *req, const char *data, size_t len);

bool authed(httpd_req_t *req);        // valid session cookie
bool requireAuth(httpd_req_t *req);   // sends 401 when not
bool requireSD(httpd_req_t *req);     // sends 503/423 when the card is unavailable
void setSessionCookie(httpd_req_t *req);

void reg(httpd_handle_t h, const char *uri, httpd_method_t m, esp_err_t (*fn)(httpd_req_t *));

}  // namespace http
