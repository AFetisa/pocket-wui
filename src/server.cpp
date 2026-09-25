#include "server.h"
#include "api.h"
#include "config.h"
#include "http_util.h"
#include <Arduino.h>
#include <esp_http_server.h>

namespace {
httpd_handle_t g_httpd = nullptr;
}

namespace server {

bool begin() {
  if (!http::g_buf) http::g_buf = (uint8_t *)heap_caps_malloc(WUI_IO_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!http::g_buf) http::g_buf = (uint8_t *)malloc(WUI_IO_BUF);
  if (!http::g_buf) { log_e("no memory for the I/O buffer"); return false; }

  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port       = WUI_HTTP_PORT;
  cfg.ctrl_port         = 32768;
  cfg.max_uri_handlers  = 64;
  cfg.max_open_sockets  = 10;          // browser tabs + background downloads + a WebDAV client
  cfg.lru_purge_enable  = true;
  cfg.stack_size        = 10240;
  cfg.recv_wait_timeout = 15;
  cfg.send_wait_timeout = 15;
  cfg.uri_match_fn      = httpd_uri_match_wildcard;   // "/dav/*"; everything else is exact
  // WebDAV clients send long headers (Authorization, a full Destination URL),
  // and a deep path percent-encodes to three times its length.
  cfg.max_req_hdr_len   = 2048;
  cfg.max_uri_len       = WUI_MAX_PATH * 3 + 64;
  cfg.max_resp_headers  = 12;

  if (httpd_start(&g_httpd, &cfg) != ESP_OK) { log_e("httpd_start failed"); return false; }
  api::registerDevice(g_httpd);
  api::registerFiles(g_httpd);
  api::registerDav(g_httpd);
  return true;
}

void stop() {
  if (g_httpd) { httpd_stop(g_httpd); g_httpd = nullptr; }
}

}  // namespace server
