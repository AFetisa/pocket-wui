#pragma once
// Host stand-in for ESP-IDF's esp_http_server, faithful where the firmware
// depends on it: one server thread runs every handler in turn, async requests
// hand a socket to another thread, header/URI size limits apply, and anything
// the handler does not read of a body is discarded. See sim_httpd.cpp.
#include <Arduino.h>
#include <cstddef>

enum http_method {
  HTTP_DELETE = 0, HTTP_GET, HTTP_HEAD, HTTP_POST, HTTP_PUT, HTTP_CONNECT, HTTP_OPTIONS, HTTP_TRACE,
  HTTP_COPY, HTTP_LOCK, HTTP_MKCOL, HTTP_MOVE, HTTP_PROPFIND, HTTP_PROPPATCH, HTTP_SEARCH, HTTP_UNLOCK,
};
typedef enum http_method httpd_method_t;
typedef void *httpd_handle_t;

typedef struct httpd_req {
  httpd_handle_t handle;
  int method;
  char uri[1025];
  size_t content_len;
  void *aux;
  void *user_ctx;
  void *sess_ctx;
} httpd_req_t;

typedef struct httpd_uri {
  const char *uri;
  httpd_method_t method;
  esp_err_t (*handler)(httpd_req_t *r);
  void *user_ctx;
} httpd_uri_t;

typedef enum {
  HTTPD_500_INTERNAL_SERVER_ERROR = 0, HTTPD_501_METHOD_NOT_IMPLEMENTED, HTTPD_505_VERSION_NOT_SUPPORTED,
  HTTPD_400_BAD_REQUEST, HTTPD_401_UNAUTHORIZED, HTTPD_403_FORBIDDEN, HTTPD_404_NOT_FOUND,
  HTTPD_405_METHOD_NOT_ALLOWED, HTTPD_408_REQ_TIMEOUT, HTTPD_411_LENGTH_REQUIRED, HTTPD_414_URI_TOO_LONG,
  HTTPD_431_REQ_HDR_FIELDS_TOO_LARGE, HTTPD_ERR_CODE_MAX
} httpd_err_code_t;

typedef esp_err_t (*httpd_err_handler_func_t)(httpd_req_t *req, httpd_err_code_t error);
typedef bool (*httpd_uri_match_func_t)(const char *reference_uri, const char *uri_to_match, size_t match_upto);

typedef struct httpd_config {
  unsigned task_priority = 5;
  size_t stack_size = 4096;
  int core_id = 0;
  size_t max_req_hdr_len = 1024;
  size_t max_uri_len = 512;
  uint16_t server_port = 80;
  uint16_t ctrl_port = 32768;
  uint16_t max_open_sockets = 7;
  uint16_t max_uri_handlers = 8;
  uint16_t max_resp_headers = 8;
  uint16_t backlog_conn = 5;
  bool lru_purge_enable = false;
  uint16_t recv_wait_timeout = 5;
  uint16_t send_wait_timeout = 5;
  httpd_uri_match_func_t uri_match_fn = nullptr;
} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() httpd_config_t{}

#define HTTPD_SOCK_ERR_FAIL -1
#define HTTPD_SOCK_ERR_INVALID -2
#define HTTPD_SOCK_ERR_TIMEOUT -3
#define HTTPD_RESP_USE_STRLEN -1
#define ESP_ERR_HTTPD_BASE 0xb000
#define ESP_ERR_HTTPD_HANDLERS_FULL (ESP_ERR_HTTPD_BASE + 1)
#define ESP_ERR_HTTPD_HANDLER_EXISTS (ESP_ERR_HTTPD_BASE + 2)
#define ESP_ERR_HTTPD_RESULT_TRUNC (ESP_ERR_HTTPD_BASE + 4)
#define ESP_ERR_HTTPD_RESP_HDR (ESP_ERR_HTTPD_BASE + 5)

esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config);
esp_err_t httpd_stop(httpd_handle_t handle);
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler);
esp_err_t httpd_register_err_handler(httpd_handle_t handle, httpd_err_code_t error, httpd_err_handler_func_t handler);
bool httpd_uri_match_wildcard(const char *uri_template, const char *uri_to_match, size_t match_upto);

esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status);
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value);
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t buf_len);
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, ssize_t buf_len);
inline esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *str) {
  return httpd_resp_send(r, str, str ? HTTPD_RESP_USE_STRLEN : 0);
}

size_t httpd_req_get_url_query_len(httpd_req_t *r);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t buf_len);
esp_err_t httpd_query_key_value(const char *qry, const char *key, char *val, size_t val_size);
size_t httpd_req_get_hdr_value_len(httpd_req_t *r, const char *field);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *field, char *val, size_t val_size);
int httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len);
int httpd_send(httpd_req_t *r, const char *buf, size_t buf_len);

esp_err_t httpd_req_async_handler_begin(httpd_req_t *r, httpd_req_t **out);
esp_err_t httpd_req_async_handler_complete(httpd_req_t *r);
