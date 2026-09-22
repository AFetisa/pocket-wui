# Cardputer ADV — WUI

A password-protected web interface for the M5Stack **Cardputer ADV**'s microSD card.
Connect the Cardputer to Wi-Fi, open `http://cardputer.local/` from any browser on the
same network, and you get a lean terminal-flavoured file manager: browse, upload and
download files of any size, create and edit text files, and drive it all from a small
built-in command line.

```
  ┌ CARDPUTER · WUI ─────────────── sd 1.2G/32G  heap 168K  ip 192.168.1.54 ┐
  │ sd:/ firmware/                              ↑up  +file  +folder  upload │
  ├─────────────────────────────────────────────────────────────────────────┤
  │ ▸ backups                                              2026-09-18 21:04 │
  │ · notes.md                                    1.4K     2026-09-19 08:11 │
  │ · payload.bin                                  118M     2026-09-19 09:52│
  ├─────────────────────────────────────────────────────────────────────────┤
  │ $ ls /firmware                                                          │
  │ - 118M  payload.bin                                                     │
  │ $ _                                                                     │
  └─────────────────────────────────────────────────────────────────────────┘
```

## Features

- **Password protected.** No default password is baked into the firmware — one is
  generated on first boot and shown on the Cardputer's own screen. Sessions are
  HttpOnly / SameSite=Strict cookies; repeated bad guesses lock the door for a minute.
- **Files of any size, both ways.** Uploads are sliced into 48 KB chunks that are
  written straight to the card, so RAM never limits file size and a dropped chunk is
  simply retried at the same offset — interrupted uploads resume instead of restarting.
  Downloads carry a real `Content-Length` and honour HTTP `Range`, so a big download
  survives a Wi-Fi blip and browsers show accurate progress.
- **Drag and drop whole folders.** Drop a folder — or pick one with *upload folder* —
  and the tree is recreated on the card, subdirectories and all. Individual files and
  multi-file selections still work the same way; each file gets its own progress row
  with cancel. Drop onto a folder row to put it straight in there.
- **Drag to move.** Drag any row onto a folder, a breadcrumb, or *↑ up* to move it
  there; folders move with their contents.
- **Create, rename, delete** (directories delete recursively) and an in-browser text
  editor with `Ctrl-S` to save.
- **Built-in console** — `ls`, `cd`, `cat`, `head`, `mkdir`, `touch`, `rm`, `mv`, `get`,
  `put`, `df`, `info`, `wifi`, `passwd`, `reboot`, with history on ↑/↓. `Ctrl-`` toggles it.
- **Self-contained.** The whole UI is one gzipped 8 KB page embedded in the firmware:
  no CDN, no fonts to fetch, nothing to install. It works with no internet at all.
- **On-device display** shows the SSID, URL, SD usage, the first-boot password, and a
  QR code of the URL (press the button to flip between the two).

## Hardware

M5Stack **Cardputer ADV** (ESP32-S3). The firmware calls `M5.begin()` and lets
M5Unified auto-detect the board, then asks it for the SD pin map
(`M5.getPin(sd_spi_sclk)` and friends) — no pin numbers are hardcoded, so the same
binary also runs on the original Cardputer v1.1.

## Flash it

```bash
pip install platformio           # once
git clone <this folder> && cd Cardputer-ADV
pio run -t upload                # Cardputer connected over USB-C
pio device monitor               # optional: watch the first-boot password
```

Prebuilt binary: `build/firmware.bin` (flash at offset `0x10000`, or use
`build/firmware-merged.bin` at offset `0` with `esptool.py write_flash 0x0 ...`).

## First boot

1. The Cardputer comes up as an access point called **`Cardputer-WUI`**. Its WPA2 key
   is shown on the device screen next to `ap key`.
2. Join it, open `http://192.168.4.1/`, and sign in with the **password on the screen**.
3. In the console, point it at your real network:
   ```
   $ wifi MyNetwork correct-horse-battery
   $ passwd my-own-better-password
   ```
   It rejoins as a station; from then on reach it at `http://cardputer.local/` or the IP
   shown on the screen. If the saved network disappears, the AP comes back automatically.

Hold the Cardputer's side button for 5 seconds to factory-reset the password and the
saved Wi-Fi credentials (an on-screen countdown warns you first). That button is
`M5.BtnA`; if M5Unified does not map it on your ADV unit, `esptool.py erase_flash`
clears the same stored state.

The generated first-boot password is kept in NVS in the clear — it has to be, so it can
survive a reboot and stay readable on the screen. Running `passwd` replaces it with a
salted SHA-256 hash and erases the plaintext copy, so do that once you are in.

## Try the UI without hardware

```bash
python3 tools/mock_server.py --root /tmp/fakesd --password test1234
# -> http://127.0.0.1:8791/
```

The mock speaks the identical API against a local folder — the same paths, JSON,
chunked-upload and Range semantics — so the interface can be used and iterated on
before anything is flashed.

## HTTP API

Everything except `GET /` and `POST /api/login` requires the session cookie.

| Method | Endpoint | Notes |
|---|---|---|
| `GET`  | `/` | the UI (gzipped, ETag-cached) |
| `POST` | `/api/login` | `{"password":"…"}` → `Set-Cookie: wui=…` |
| `POST` | `/api/logout` | drops this session |
| `GET`  | `/api/status` | firmware, Wi-Fi, heap, SD usage, uptime |
| `GET`  | `/api/list?path=` | directory listing |
| `GET`  | `/api/download?path=[&max=]` | `Range` supported; `max` truncates (used by `cat`) |
| `POST` | `/api/upload?path=&offset=[&final=1]` | raw body chunk; `409 {size}` on offset mismatch |
| `POST` | `/api/mkdir?path=` · `/api/touch?path=` | create; `mkdir` is idempotent on an existing directory (a folder upload re-asserts its parents) and `409` only if the name is a file |
| `POST` | `/api/delete?path=` | recursive for directories |
| `POST` | `/api/rename?from=&to=` | move / rename — any `to` under the card root, so this is also how the UI moves a dropped row into a folder |
| `POST` | `/api/wifi` | `{"ssid":"…","password":"…"}` — joins and saves |
| `POST` | `/api/passwd` | `{"password":"…"}` — ≥ 8 chars, signs out other sessions |
| `POST` | `/api/reboot` | restart |

## Known limits — read these

- **HTTP, not HTTPS.** TLS on an ESP32-S3 while streaming to SD is not worth the
  trouble, so the password and the file contents travel in clear over your LAN. Use it
  on a network you trust; don't port-forward it to the internet.
- **FAT32 caps a single file at 4 GB − 1**, and the Arduino `SD` library does not
  support exFAT. "Any size" means "up to 4 GB" — larger files need SdFat and an exFAT
  card.
- **One transfer at a time.** `esp_http_server` serves every request from a single
  task, so a running upload holds the queue until it finishes. A folder upload is
  therefore strictly sequential — one file after another, in the order the browser
  hands them over — and a folder of many small files spends most of its time in
  per-request overhead rather than on the card.
- Changing the Wi-Fi network moves the device to a new IP: the browser tab that issued
  the `wifi` command will lose its connection and has to be reopened on the new address.
- File timestamps come from the SD card's clock, which without an RTC or NTP starts at
  1970 — listings will show epoch dates until the card is written by something else.

## Layout

```
platformio.ini          build config (board: m5stack-stamps3 = the StampS3 in the Cardputer)
web/index.html          the entire UI — edit this, the build re-embeds it automatically
tools/embed_web.py      gzips web/index.html into src/web_assets.h (PlatformIO pre-script)
tools/mock_server.py    host-side stand-in for the firmware API
src/main.cpp            bring-up
src/config.h            timeouts, buffer sizes, hostname, ports
src/auth.*              salted-SHA256 password in NVS, sessions, lockout
src/net.*               station/AP handling, mDNS, credential storage
src/storage.*           SD mount from M5Unified's pin map, path locking, upload handles
src/server.*            esp_http_server handlers
src/paths.*             path sanitiser (everything from the network goes through this)
src/ui.*                on-device screen: status, QR code, factory reset
test/host/              host tests: the path sanitiser and the browser upload loop
build/                  prebuilt firmware images
```

## Development

```bash
pio run                                     # build
python3 tools/embed_web.py                  # re-embed the UI on its own
g++ -std=c++17 -I test/host -o /tmp/t test/host/test_paths.cpp && /tmp/t   # path tests
node test/host/test_upload.mjs              # browser upload loop
```

`src/paths.cpp` is the security boundary: every path from the network is normalised,
`..` is clamped at the card root, and FAT-illegal or control characters are rejected.
The host test suite covers traversal, collapsing, length limits and percent-decoding.

`test_upload.mjs` pulls `putBlob()` straight out of `web/index.html` and runs it against
a fake device, so the chunking, the retry and the `409` re-seek are covered without a
browser. Edit the upload loop and the test follows it.

## Verification status

Built clean with PlatformIO (ESP32-S3, Arduino core 3.x, M5Unified 0.2.22):
`RAM 14.9%, Flash 32.4%`. The path sanitiser passes its host test suite (`test/host/`),
which covers traversal, collapsing, length limits and percent-decoding of the real
`src/paths.cpp`. The upload loop passes `test/host/test_upload.mjs`, which runs the real
`putBlob()` from `web/index.html` against a device that truncates chunks mid-write.

Folder upload and drag-to-move were checked at the API level against
`tools/mock_server.py` — nested `mkdir`, idempotent re-`mkdir`, upload into a created
subtree, `rename` across directories, and its `404`/`409` refusals. In a real
(Playwright/Chromium) browser against the mock: multi-file upload byte-identical,
*upload folder* recreating a nested tree, and dragging a file row onto a folder row.
Dropping a folder from the OS file manager is the one gesture not driven.

The HTTP contract — login, listing, traversal rejection, 300 KB chunked upload,
byte-identical download, `Range` request, stale-offset resync, recursive delete — was
exercised end-to-end against `tools/mock_server.py`. Note what that does and does not
prove: the mock is a **reimplementation** of the same contract, so those runs validate
the API shape and the browser UI against it, not the C++ handlers in `src/server.cpp`.

**It has not been run on a physical Cardputer ADV** — no device was attached to the
machine that built it. Expect to verify the SD mount and the screen layout on first flash.

The SD mount follows the firmwares that *are* proven on this unit: G5 (the EXT
header's SPI chip-select, sharing the SD bus) is driven high before mounting, and
the clock is chosen by write/read-back probe from `WUI_SD_HZ_LADDER` in
`src/config.h`, never below the 4 MHz those firmwares run at. The web console prints
the clock it settled on at sign-in (`sd SDHC · 29G · spi 20 MHz`). If uploads still
misbehave, drop the faster entries from the ladder.
