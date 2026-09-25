// Host stand-in for the Arduino core: just enough for PocketWUI's server code.
#pragma once
#include "../../host/arduino_shim.h"
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <thread>

using std::max;
using std::min;

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NOT_FOUND 0x105

inline unsigned long millis() {
  using namespace std::chrono;
  static const auto t0 = steady_clock::now();
  return (unsigned long)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}
inline void delay(unsigned long ms) {
  if (ms) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  else std::this_thread::yield();
}

extern int wui_sim_verbose;
#define log_e(fmt, ...) fprintf(stderr, "[E] " fmt "\n", ##__VA_ARGS__)
#define log_w(fmt, ...) fprintf(stderr, "[W] " fmt "\n", ##__VA_ARGS__)
#define log_i(fmt, ...) do { if (wui_sim_verbose) fprintf(stderr, "[I] " fmt "\n", ##__VA_ARGS__); } while (0)

#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline void *heap_caps_malloc(size_t n, int) { return nullptr; }   // "no PSRAM": falls back to malloc

#define OUTPUT 1
#define HIGH 1
#define LOW 0
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}

struct SimEsp {
  const char *getChipModel() { return "host-sim"; }
  uint32_t getCpuFreqMHz() { return 240; }
  uint32_t getFreeHeap() { return 180000; }
  uint32_t getMinFreeHeap() { return 150000; }
  uint32_t getFreePsram() { return 0; }
  void restart();
};
extern SimEsp ESP;

struct SimSerial {
  void begin(unsigned long) {}
  int printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int n = vfprintf(stdout, fmt, ap); va_end(ap); fflush(stdout); return n;
  }
};
extern SimSerial Serial;
