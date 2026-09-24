#pragma once
#include <Arduino.h>

// Payload for the "join Wi-Fi" QR that phone cameras understand
// (WIFI:T:WPA;S:<ssid>;P:<key>;;). \ ; , : and " are backslash-escaped as the
// format requires. An empty key produces an open-network (T:nopass) payload.
String wui_wifi_qr(const char *ssid, const char *pass);
