#pragma once
#include <Arduino.h>

// Sanity checks for an uploaded firmware file before anything is flashed.
// Pure byte inspection, unit-tested on the host (test/host/test_fwimage.cpp).

constexpr size_t WUI_FW_HEAD = 128;   // bytes the check needs from the file start

struct FwInfo {
  String project;    // esp_app_desc_t.project_name
  String version;    // esp_app_desc_t.version
};

// Accepts an ESP32-S3 *application* image (what M5Launcher calls firmware.bin).
// Rejects other chips, bootloaders and merged/factory images (bootloader at 0,
// app at 0x10000), which cannot be written into an app partition.
bool wui_check_app_image(const uint8_t *head, size_t n, FwInfo &info, String &err);
