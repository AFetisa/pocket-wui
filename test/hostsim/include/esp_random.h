#pragma once
#include <cstddef>
#include <cstdint>
#include <random>
inline uint32_t esp_random() {
  static thread_local std::mt19937 rng{std::random_device{}()};
  return rng();
}
inline void esp_fill_random(void *p, size_t n) {
  for (size_t i = 0; i < n; ++i) ((uint8_t *)p)[i] = (uint8_t)esp_random();
}
