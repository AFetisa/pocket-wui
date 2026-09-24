#pragma once
// ---------------------------------------------------------------------------
// Cardputer ADV — WUI build-time configuration.
// Anything a user may reasonably want to change lives here.
// ---------------------------------------------------------------------------

#define WUI_FW_VERSION      "1.0.0"

#define WUI_HOSTNAME        "cardputer"          // -> http://cardputer.local
#define WUI_AP_SSID         "Cardputer-WUI"      // fallback access point
#define WUI_HTTP_PORT       80

#define WUI_STA_TIMEOUT_MS  12000                // give up joining a saved network
#define WUI_SESSION_TTL_S   (12 * 3600)          // cookie lifetime
#define WUI_MAX_SESSIONS    4
#define WUI_MAX_FAILS       8                    // then lock out for the window below
#define WUI_LOCKOUT_MS      60000
#define WUI_MIN_PASSWORD    8

// QR sign-in: the device screen shows a QR of http://<ip>/login?k=<token>.
// The token is single-use, only exists while that screen is up, and is replaced
// after this long even if nobody scans it. Set WUI_QR_LOGIN to 0 to drop the
// feature entirely (the /login route then always refuses).
#define WUI_QR_LOGIN        1
#define WUI_QR_TOKEN_TTL_MS 120000

#define WUI_IO_BUF          (24 * 1024)          // SD read/write staging buffer
#define WUI_MAX_PATH        192                  // FAT full-path budget
#define WUI_IDLE_FILE_MS    8000                 // close a cached upload handle after

// SD SPI clocks to try, fastest first; the first that survives a write/read-back
// probe is kept. 4 MHz is what every sibling firmware runs on this unit, so it is
// the floor. Drop the faster entries if uploads still misbehave on your card.
#define WUI_SD_HZ_LADDER    20000000, 10000000, 4000000
