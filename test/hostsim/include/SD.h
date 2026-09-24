#pragma once
#include <FS.h>
#include <SPI.h>

typedef enum { CARD_NONE, CARD_MMC, CARD_SD, CARD_SDHC, CARD_UNKNOWN } sdcard_type_t;

class SDFS {
 public:
  bool begin(uint8_t ss = SS, SPIClass &spi = SPI, uint32_t hz = 4000000, const char *mp = "/sd", uint8_t maxFiles = 5, bool fmt = false);
  void end() {}
  sdcard_type_t cardType() { return CARD_SDHC; }
  uint64_t totalBytes();
  uint64_t usedBytes();
  File open(const String &path, const char *mode = FILE_READ);
  bool exists(const String &path);
  bool mkdir(const String &path);
  bool remove(const String &path);
  bool rmdir(const String &path);
  bool rename(const String &from, const String &to);
};
extern SDFS SD;
extern std::string wui_sim_root;   // host directory standing in for the card
