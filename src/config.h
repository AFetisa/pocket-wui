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

#define WUI_IO_BUF          (24 * 1024)          // SD read/write staging buffer
#define WUI_MAX_PATH        192                  // FAT full-path budget
#define WUI_IDLE_FILE_MS    8000                 // close a cached upload handle after
