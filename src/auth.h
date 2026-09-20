#pragma once
#include <Arduino.h>

namespace auth {

// Loads (or, on first boot, generates) the access password. Returns the
// plaintext password only when it was just generated — it is never recoverable
// afterwards, only replaced.
void begin();

bool  isFreshPassword();      // true until the generated password is changed
String freshPassword();       // the generated password, for the on-device screen

bool  checkPassword(const String &pw);
bool  setPassword(const String &pw, String &err);   // >= WUI_MIN_PASSWORD chars

String newSession();                  // returns an opaque token
bool   validSession(const String &t); // refreshes idle timer
void   dropSession(const String &t);
void   dropAllSessions();

bool lockedOut();        // too many bad guesses recently
void noteFailure();
void noteSuccess();

}  // namespace auth
