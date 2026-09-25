#pragma once
#include <esp_http_server.h>

// Route registration, one call per module (see server.cpp).
namespace api {
void registerFiles(httpd_handle_t h);    // api_files.cpp
void registerDevice(httpd_handle_t h);   // api_device.cpp
void registerDav(httpd_handle_t h);      // dav.cpp
}  // namespace api
