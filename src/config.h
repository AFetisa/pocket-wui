#pragma once
// ---------------------------------------------------------------------------
// PocketWUI — build-time configuration.
// Anything a user may reasonably want to change lives here.
// ---------------------------------------------------------------------------

#define WUI_NAME            "PocketWUI"
#define WUI_FW_VERSION      "2.0.0"

#define WUI_HOSTNAME        "cardputer"          // -> http://cardputer.local (changeable in Settings)
#define WUI_AP_PREFIX       "PocketWUI-"         // fallback access point: PocketWUI-<4 hex digits>
#ifndef WUI_HTTP_PORT
#define WUI_HTTP_PORT       80
#endif

#define WUI_STA_TIMEOUT_MS  12000                // give up joining one saved network
#define WUI_MAX_NETWORKS    5                    // remembered Wi-Fi networks
#define WUI_REJOIN_EVERY_MS 60000                // from AP mode, retry saved networks this often
#define WUI_LINK_LOST_MS    30000                // station link down this long -> bring up the AP too

#define WUI_SESSION_TTL_S   (12 * 3600)          // cookie lifetime
#define WUI_MAX_SESSIONS    6
#define WUI_MAX_FAILS       8                    // then lock out for the window below
#define WUI_LOCKOUT_MS      60000
#define WUI_MIN_PASSWORD    8

// QR sign-in: the device screen shows a QR of http://<ip>/login?k=<token>.
// The token is single-use, only exists while that screen is up, and is replaced
// after this long even if nobody scans it. Set WUI_QR_LOGIN to 0 to drop the
// feature entirely (the /login route then always refuses).
#ifndef WUI_QR_LOGIN
#define WUI_QR_LOGIN        1
#endif
#define WUI_QR_TOKEN_TTL_MS 120000

// USB: plugged into a computer, the Cardputer shows up as a network adapter
// (the WUI is then at http://192.168.7.1/, and the computer keeps its own
// internet) and, on demand, as a USB drive for the SD card. Needs the native
// USB port, so while it is on, flashing over the cable needs the G0 button held
// at power-up (or turn USB off in Settings first). 0 removes it from the build.
#ifndef WUI_USB
#define WUI_USB             1
#endif
#define WUI_USB_IP          192, 168, 7, 1

#ifndef WUI_WEBDAV
#define WUI_WEBDAV          1                    // http://cardputer.local/dav/ for Finder, Files apps, rclone…
#endif

#define WUI_IO_BUF          (24 * 1024)          // SD read/write staging buffer (web server task)
#define WUI_JOB_BUF         (16 * 1024)          // same, for background copy/hash/firmware jobs
#define WUI_STREAM_BUF      (16 * 1024)          // same, for each background download stream
#define WUI_STREAM_WORKERS  2                    // downloads served off the web server task
#define WUI_MAX_PATH        192                  // FAT full-path budget
#define WUI_IDLE_FILE_MS    8000                 // close a cached upload handle after

#define WUI_RANGE_CAP       (2 * 1024 * 1024)    // max bytes per open-ended Range reply (video seek)
#define WUI_FIND_MAX        400                  // search results returned
#define WUI_FIND_VISIT_MAX  30000                // directory entries a search may walk
#define WUI_ZIP_MAX_ENTRIES 4000                 // folder download limit
#define WUI_TRASH_DIR       "/.trash"

#define WUI_DIM_AFTER_MS    60000                // screen dims after this long without a button press

// SD SPI clocks to try, fastest first; the first that survives a write/read-back
// probe is kept. 4 MHz is what every sibling firmware runs on this unit, so it is
// the floor. Drop the faster entries if uploads still misbehave on your card.
#define WUI_SD_HZ_LADDER    20000000, 10000000, 4000000
