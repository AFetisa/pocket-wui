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

// QR sign-in. The screen arms a single-use token while it shows the login QR;
// the web server trades it for a session exactly once. loginToken() re-arms a
// fresh one when the current token is used up or older than WUI_QR_TOKEN_TTL_MS.
String   loginToken();            // current token, arming one if needed
uint32_t loginTokenMsLeft();      // 0 when nothing is armed
void     disarmLoginToken();      // screen left the QR: nothing valid remains
bool     consumeLoginToken(const String &t);

bool lockedOut();        // too many bad guesses recently
void noteFailure();
void noteSuccess();

}  // namespace auth
