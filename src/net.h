#pragma once
#include <Arduino.h>

namespace net {

// Joins the saved network if there is one, otherwise raises the fallback AP.
void begin();

bool   isAP();
String ssid();
String ip();
int    rssi();
String apPassword();     // the AP's WPA2 key (derived once, stored in NVS)

// Tries to join, and on success stores the credentials. Falls back to the AP.
bool join(const String &ssid, const String &password, String &err);

void tick();             // re-raises the AP if a saved network goes away

}  // namespace net
