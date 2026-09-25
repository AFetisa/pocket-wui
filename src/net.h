#pragma once
#include <Arduino.h>
#include <time.h>
#include <vector>

namespace net {

// Joins the strongest saved network in range, otherwise raises the fallback
// access point (PocketWUI-XXXX) with a captive portal.
void begin();
// Call from loop(): captive DNS, and — while on the access point — quietly
// retries the saved networks (only when nobody is connected to the AP, so a
// retry never knocks your phone off it).
void tick();

bool   isAP();           // no station link: reachable only through our own AP
bool   apUp();           // our AP is broadcasting (AP mode, "keep AP on", or link lost)
String apSsid();
String apPassword();
String apIp();
int    apClients();
bool   setApPassword(const String &pw, String &err);

String ssid();           // the network we are joined to, or apSsid()
String ip();             // station IP, or the AP IP in AP mode
int    rssi();

String hostname();       // mDNS name without ".local"
bool   setHostname(const String &name, String &err);
bool   keepAp();
void   setKeepAp(bool on);

// Blocking join (up to WUI_STA_TIMEOUT_MS). On success the network is saved at
// the front of the list (up to WUI_MAX_NETWORKS are remembered).
bool join(const String &ssid, const String &password, String &err);
std::vector<String> savedNetworks();
bool forget(const String &ssid);
// Blocking scan (a few seconds): [{"ssid":..,"rssi":..,"secure":..,"saved":..}]
bool scanJson(String &out, String &err);

// Clock: from the browser (epoch seconds, JS getTimezoneOffset() minutes) or NTP.
void setTime(time_t epoch, int tzOffsetMin);
bool timeValid();

}  // namespace net
