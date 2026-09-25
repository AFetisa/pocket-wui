#include "fwimage.h"
#include <string.h>

namespace {

uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

String cstr(const uint8_t *p, size_t max) {
  size_t n = 0;
  while (n < max && p[n] && p[n] >= 0x20 && p[n] < 0x7f) ++n;
  return String((const char *)p, n);
}

}  // namespace

bool wui_check_app_image(const uint8_t *h, size_t n, FwInfo &info, String &err) {
  if (n < WUI_FW_HEAD) { err = "file is too small to be firmware"; return false; }
  if (h[0] != 0xE9) { err = "not an ESP32 firmware image"; return false; }
  uint16_t chip = (uint16_t)(h[12] | (h[13] << 8));
  if (chip != 0x0009) {
    err = "this firmware is for a different chip (id " + String((unsigned)chip) + "), not the ESP32-S3";
    return false;
  }
  // An application's first segment starts with esp_app_desc_t (magic ABCD5432)
  // right after the 24-byte image header and 8-byte segment header. The
  // bootloader at the front of a merged image has no such descriptor.
  if (le32(h + 32) != 0xABCD5432u) {
    err = "this looks like a merged/factory image — use the plain firmware.bin (the app only)";
    return false;
  }
  info.version = cstr(h + 48, 32);
  info.project = cstr(h + 80, 32);
  return true;
}
