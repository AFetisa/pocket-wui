#pragma once
#include <Arduino.h>
#include <functional>

// Firmware updates and M5Launcher integration.
//
// Under M5Launcher (it lives in an app partition of subtype "test") this app
// shares the flash with other installed apps, so it never writes firmware
// itself: an uploaded .bin is left on the SD card for Launcher to install, and
// "back to Launcher" uses Launcher's own wake-from-deep-sleep entry point.
// Flashed standalone (partitions.csv has two app slots) it updates itself.
namespace fw {

bool underLauncher();
bool canSelfUpdate();
String runningSlot();          // partition label, for the status page

using Progress = std::function<bool(size_t n, uint64_t total)>;
bool flashFromFile(const String &path, uint8_t *buf, size_t bufLen, const Progress &progress, String &err);

void rebootToLauncher();       // does not return
void restart();                // does not return

}  // namespace fw
