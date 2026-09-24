#include "arduino_shim.h"
#include "../../src/fwimage.cpp"
#include <iostream>
#include <vector>

static int fails = 0;
#define OK(c) do { if (!(c)) { std::cout << "FAIL  line " << __LINE__ << ": " #c "\n"; fails++; } } while (0)

static std::vector<uint8_t> image(uint16_t chip, bool desc) {
  std::vector<uint8_t> b(WUI_FW_HEAD, 0);
  b[0] = 0xE9; b[1] = 5; b[12] = chip & 0xFF; b[13] = chip >> 8;
  if (desc) {
    b[32] = 0x32; b[33] = 0x54; b[34] = 0xCD; b[35] = 0xAB;
    memcpy(&b[48], "2.0.0", 5);
    memcpy(&b[80], "PocketWUI", 9);
  }
  return b;
}

int main() {
  FwInfo info; String err;
  auto good = image(9, true);
  OK(wui_check_app_image(good.data(), good.size(), info, err));
  OK(info.project == "PocketWUI" && info.version == "2.0.0");
  auto merged = image(9, false);
  OK(!wui_check_app_image(merged.data(), merged.size(), info, err) && err.indexOf("merged") >= 0);
  auto c3 = image(5, true);
  OK(!wui_check_app_image(c3.data(), c3.size(), info, err) && err.indexOf("chip") >= 0);
  auto junk = good; junk[0] = 'P';
  OK(!wui_check_app_image(junk.data(), junk.size(), info, err));
  OK(!wui_check_app_image(good.data(), 40, info, err));
  // The real firmware build, when present.
  if (FILE *f = fopen(".pio/build/cardputer-adv/firmware.bin", "rb")) {
    uint8_t h[WUI_FW_HEAD]; size_t n = fread(h, 1, sizeof(h), f); fclose(f);
    OK(wui_check_app_image(h, n, info, err));
    std::cout << "  built firmware: " << info.project << " " << info.version << "\n";
  }
  if (FILE *f = fopen(".pio/build/cardputer-adv/firmware.factory.bin", "rb")) {
    uint8_t h[WUI_FW_HEAD]; size_t n = fread(h, 1, sizeof(h), f); fclose(f);
    OK(!wui_check_app_image(h, n, info, err));
  }
  std::cout << (fails ? "fwimage tests FAILED\n" : "fwimage tests ok\n");
  return fails ? 1 : 0;
}
