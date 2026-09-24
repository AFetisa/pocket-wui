#pragma once
// std::thread with an explicit stack size, on the device and on the host.
// On ESP-IDF a pthread's stack comes from the creating thread's pthread config,
// so it is set right before spawning.
#include <functional>
#include <thread>
#ifdef ESP_PLATFORM
#include <esp_pthread.h>
#endif

inline bool wui_spawn(const char *name, size_t stack, std::function<void()> fn) {
#ifdef ESP_PLATFORM
  esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
  cfg.stack_size = stack;
  cfg.prio = 4;
  cfg.thread_name = name;
  cfg.inherit_cfg = false;
  esp_pthread_set_cfg(&cfg);
#else
  (void)name; (void)stack;
#endif
  try {
    std::thread(std::move(fn)).detach();
    return true;
  } catch (...) {
    return false;
  }
}
