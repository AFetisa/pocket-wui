#pragma once
#include <Arduino.h>

// USB cable features (WUI_USB):
//  * Network adapter (CDC-NCM): the computer gets an address from us and the
//    WUI is at http://192.168.7.1/ — no Wi-Fi involved, and the computer keeps
//    its own internet (we never offer ourselves as its gateway).
//  * USB drive (mass storage): on request the SD card is lent to the computer
//    as a plain removable drive. While lent, the WUI keeps working for status
//    and settings but cannot touch files; ejecting on the computer (or "Give
//    back" in the WUI) returns the card.
namespace usb {

void begin();            // after storage::begin(), so the drive knows the card
void tick();             // from loop(): finishes an eject requested by the host

bool enabled();          // user setting (Settings → USB); applies at next boot
void setEnabled(bool on);
bool running();          // USB stack started this boot
bool hostConnected();    // a computer has configured us
String ip();             // "192.168.7.1" when running, else ""

bool driveAvailable();   // a card was present at boot, so the drive exists
bool driveMode();
bool setDriveMode(bool on, String &err);

}  // namespace usb
