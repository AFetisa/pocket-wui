#pragma once
#include <Arduino.h>

// Normalise and validate a client-supplied path. Returns false for anything
// that escapes the card root, contains control characters, or is too long.
// On success `out` is an absolute, collapsed path ("/" or "/a/b", never "/a/").
bool wui_safe_path(const char *raw, String &out);

// Percent-decoding for query-string values ("%2F", "+" -> space is NOT applied;
// query values here are encodeURIComponent output).
String wui_url_decode(const char *src);
