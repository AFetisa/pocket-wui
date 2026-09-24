#pragma once
#include <Arduino.h>
#include <time.h>

// Small text helpers with no hardware dependencies, so they are unit-tested on
// the host (test/host/test_textutil.cpp).

// Content type for a file name, by extension. Unknown -> application/octet-stream.
const char *wui_mime_for(const String &name);

// True when a browser can show this type inline without executing anything we
// did not write: images, audio, video, PDF, plain text. HTML/SVG/XML are also
// shown, but only ever with a `Content-Security-Policy: sandbox` header.
bool wui_mime_is_text(const char *mime);
bool wui_mime_needs_sandbox(const char *mime);

// Case-insensitive name match. A pattern containing '*' or '?' is a glob that
// must match the whole name; anything else is a substring search.
bool wui_name_matches(const char *name, const char *pattern);

// RFC 4648 base64 decode (padding optional). Returns false on bad input.
bool wui_b64_decode(const char *in, String &out);

// "Sun, 06 Nov 1994 08:49:37 GMT" for HTTP / WebDAV dates.
String wui_http_date(time_t t);

// Escapes & < > " ' for XML text and attributes.
String wui_xml_escape(const String &s);

// Percent-encodes everything except unreserved characters and '/', for hrefs.
String wui_url_encode_path(const String &path);

// Content-Disposition value with an ASCII fallback plus RFC 5987 filename*.
String wui_content_disposition(const String &name, bool inlineView);

// Extracts the path from a WebDAV Destination header ("http://host/dav/a%20b"
// or "/dav/a%20b"), stripping scheme, host and the given prefix, and
// percent-decoding it. Returns "" if the prefix is missing.
String wui_dav_destination(const char *header, const char *prefix);
