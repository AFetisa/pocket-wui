# PocketWUI: developer notes

Everything that doesn't belong in the friendly README: how to build, how to
test without hardware, the HTTP API, and how the pieces fit together.

## Build

```bash
pip install platformio
pio run                      # -> .pio/build/cardputer-adv/firmware.bin (+ firmware.factory.bin)
pio run -t upload            # hold G0 while plugging in (see "USB" below)
```

The platform is [pioarduino](https://github.com/pioarduino/platform-espressif32)
`55.03.312-1` (Arduino core 3.3.12 / ESP-IDF 5.5), pinned in `platformio.ini`.
The official PlatformIO platform ships an older core, and EspUsbDevice needs
3.3.9 or later.

CI (`.github/workflows/build.yml`) runs every test below, builds the firmware,
and uploads `PocketWUI.bin` (the app, for M5Launcher) and
`PocketWUI-full-flash.bin` (bootloader + partitions + app, for `0x0`) as
workflow artifacts. Pushing a tag `v*` publishes them as a GitHub release.

`web/index.html` is gzipped into `src/web_assets.h` by `tools/embed_web.py` on
every build. It's deterministic, so an unchanged page doesn't trigger a
rebuild.

## Try it without hardware: the simulator

`test/hostsim` compiles the firmware's **real** server sources (HTTP handlers,
uploads, downloads, ZIP, search, trash, jobs, WebDAV, auth) for your PC. Small
stand-ins replace what a PC doesn't have: `esp_http_server` (same API and the
same one-task-at-a-time behaviour), the SD card (a folder), Wi‑Fi, USB, the
screen and the flash.

```bash
make -C test/hostsim run           # demo card in /tmp/pocketwui-sd, 32 GB pretend card
# -> http://127.0.0.1:8791/   password: test1234

test/hostsim/pocketwui-sim --root ~/some/folder --port 8080 --password hunter22 [--card-gb 64] [-v]
WUI_SIM_LAUNCHER=1 test/hostsim/pocketwui-sim ...   # behave as if running under M5Launcher
```

## Tests

```bash
make -C test host    # unit tests: paths, QR payloads, text helpers, ZIP (+ZIP64), firmware images, upload loop
make -C test sim     # end-to-end on the simulator: test/e2e/test_api.py + test/e2e/test_ui.mjs (Chromium)
WUI_ZIP64_TEST=1 make -C test host    # also builds a real >4 GiB archive (sparse file, ~30 s)
SHOTS=/tmp/shots node test/e2e/test_ui.mjs   # keep screenshots of every step
```

`test_ui.mjs` needs Playwright (`npm i -D playwright && npx playwright install chromium`).
The README screenshots come from the simulator, filled with
`test/e2e/make_demo_card.py`.

## Layout

```
src/main.cpp          setup/loop
src/config.h          every knob: names, limits, buffer sizes, timeouts, feature switches
src/net.*             Wi‑Fi: saved networks, AP + captive DNS, rejoin state machine, mDNS, clock
src/usb.*             USB network adapter (CDC-NCM) + SD-card USB drive (MSC) via EspUsbDevice
src/storage.*         SD mount (pin map from M5Unified), the SD lock, upload handles, trash, copy
src/server.cpp        esp_http_server setup; routes come from api_files / api_device / dav
src/http_util.*       request/response helpers shared by the handlers
src/api_files.cpp     list, find, du, upload, download, zip, mkdir/touch/delete/rename/copy, jobs
src/api_device.cpp    page, sign-in, status, Wi‑Fi, name, time, password, USB, firmware, Launcher
src/dav.cpp           WebDAV at /dav/
src/stream.*          downloads/ZIPs on background threads (async requests), Range handling
src/jobs.*            one background job at a time: copy, SHA-256, firmware install
src/zipstream.*       streaming stored-ZIP writer with ZIP64 and exact size prediction
src/firmware.*        self-update (standalone), M5Launcher detection and "back to Launcher"
src/fwimage.*         sanity checks for uploaded .bin files (chip, app vs merged image)
src/auth.*            password hash, sessions, lockout, single-use QR sign-in token
src/paths.*           path sanitiser: every path from the network goes through it
src/textutil.*        MIME types, glob/substring match, base64, HTTP dates, XML, URL encoding
src/ui.*              the Cardputer's own screen: info, QR codes, USB-drive screen, dimming
web/index.html        the whole web UI (one file, no external assets)
test/                 host unit tests, the simulator, end-to-end tests
```

## HTTP API

The web UI uses these; you can too. Authenticate with `POST /api/login
{"password": …}` and keep the `wui` cookie, **or** use the WebDAV side with
HTTP Basic auth (any user name, the WUI password), which is easier from
scripts.

| Method & path | Does |
|---|---|
| `GET /api/session` | `{"signed_in": bool}` (always 200) |
| `POST /api/login` · `/api/logout` | JSON `{"password"}` · drop the session |
| `GET /login?k=TOKEN` | QR sign-in (the token is on the device screen) |
| `GET /api/status` | device, network, SD, battery, USB, Launcher info |
| `GET /api/list?path=` | folder entries `{name, dir, size, mtime}` |
| `GET /api/find?path=&q=` | recursive name search; `q` is a substring or a `*`/`?` glob |
| `GET /api/du?path=` | total bytes under a path |
| `GET /api/download?path=[&inline=1][&max=N]` | file, with `Range`; `inline` for previews |
| `GET /api/zip?path=` · `POST /api/zipticket` + `GET /api/zip?t=` | folder / several paths as one ZIP |
| `POST /api/upload?path=&offset=[&final=1]` | raw chunk; `409 {size}` tells you where to resume |
| `POST /api/mkdir` · `/api/touch` · `/api/rename?from=&to=` | the obvious |
| `POST /api/delete?path=[&trash=1]` · `/api/trash/empty` | delete (or move to `/.trash`) |
| `POST /api/copy?from=&to=` (or body `from\tto` lines) · `/api/hash?path=` | background jobs |
| `GET /api/job` · `POST /api/job/cancel` | job progress / cancel |
| `POST /api/wifi {ssid,password}` · `GET /api/wifi/scan` · `/api/wifi/saved` · `POST /api/wifi/forget` | Wi‑Fi |
| `POST /api/wifi/ap {password?, keep?}` · `/api/name {name}` · `/api/time {epoch,tz}` · `/api/passwd` | settings |
| `POST /api/usb {drive?, enabled?}` | lend the card over USB / switch USB features |
| `POST /api/firmware/check?path=` · `/api/firmware/install?path=` | firmware files |
| `POST /api/launcher` · `/api/reboot` | leave for M5Launcher / restart |

### From a terminal (WebDAV + Basic auth)

```bash
H=http://cardputer.local/dav
curl -u :PASSWORD -T holiday.mp4 "$H/Videos/holiday.mp4"      # upload
curl -u :PASSWORD -O "$H/Videos/holiday.mp4"                  # download
curl -u :PASSWORD -X MKCOL "$H/Backups"                       # new folder
curl -u :PASSWORD -X MOVE -H "Destination: $H/old.txt" "$H/new.txt"
curl -u :PASSWORD -X DELETE "$H/old.txt"
rclone config create card webdav url=$H vendor=other user=me pass=$(rclone obscure PASSWORD)
rclone sync ./photos card:Photos
```

WebDAV supports `OPTIONS PROPFIND PROPPATCH GET HEAD PUT DELETE MKCOL COPY MOVE`
plus the fake `LOCK/UNLOCK` that Finder and Windows expect. `PUT` needs a
`Content-Length`; chunked request bodies get `501`, because esp_http_server
can't read them.

### Console

`` Ctrl+` `` in the web UI opens a console: `ls cd cat head edit find mkdir
touch mv cp rm (rm -f) get zip put du sha256 df info scan wifi passwd name
usbdrive launcher reboot`. Type `help`.

## How it works

**One web server task.** esp_http_server runs every handler on one task, one
at a time. Anything slow is taken off it:

- downloads bigger than 64 KB and all ZIPs are handed to a background thread
  with `httpd_req_async_handler_begin()` (at most `WUI_STREAM_WORKERS`; beyond
  that they're served inline);
- copies, checksums and firmware installs run as a *job* on their own thread;
  the UI polls `/api/job`.

**One SD lock.** Every SD operation takes `storage::Guard` for as long as one
chunk takes, never for a whole transfer. That's what lets an upload, a
download and a copy make progress side by side.

**Uploads** are 256 KB chunks written straight to the card through a cached
file handle. The handle counts accepted bytes itself (FATFS only updates a
file's size on flush), and a wrong offset returns `409` with the real size,
so the browser resumes exactly where the card is.

**ZIPs** are *stored* (no compression). Card contents rarely compress, and
the SD card, not the CPU, is the bottleneck. Storing means the archive size is
known exactly before streaming, so the download shows real progress. CRCs go
in data descriptors, with ZIP64 records wherever 32-bit fields would overflow.

**Video previews.** A browser asks for `bytes=N-` and reads as much as it
likes. Preview requests (`inline=1`) get at most 2 MB per reply, so the
player keeps coming back for more instead of holding a worker for the whole
film. Downloads and WebDAV clients always get the full range they asked for.

**Safety of previews.** Files are served as `application/octet-stream`
attachments unless previewed. Previews of HTML/SVG/XML carry
`Content-Security-Policy: sandbox`, so a hostile file on the card can't run
script as the WUI.

**Wi‑Fi.** At boot the strongest saved network in range is joined. Otherwise
the AP `PocketWUI-XXXX` comes up with a captive-portal DNS, and saved networks
are retried every minute, but only while nobody is connected to the AP, since
a retry changes channels. When a station link drops for 30 s, the AP comes back
too.

**USB.** EspUsbDevice owns the native USB port, which needs `ARDUINO_USB_MODE=0`
(TinyUSB) and no CDC console; a compile-time guard in `usb.cpp` checks this.
Consequences:

- logs go to the internal UART, and everything a user needs is on the screen;
- flashing over the cable needs **G0 held at plug-in**, unless USB features are
  switched off in Settings (then the USB-Serial/JTAG port stays, as usual).

The network adapter runs a DHCP server on `192.168.7.0/24` and deliberately
offers no gateway or DNS, so the computer's internet stays where it was. The
USB drive is always present but reports "no media" until you lend the card.
While it's lent, every file endpoint answers `423`, and handing it back
remounts the card to drop stale FAT caches.

**Updates roll back.** A self-installed update stays "pending" until it has
brought the web server up (`verifyRollbackLater()` + `esp_ota_mark_app_valid_cancel_rollback()`).
If it crashes first, the bootloader returns to the previous version.

**USB drive safety.** The host's sector reads and writes run under the same SD
lock as everything else, and only while the card is lent. Taking the card back
flips that state under the lock, so no transfer is in flight when the card is
remounted. A card that is missing or swapped since start-up is never offered.
Pulling the cable hands the card back automatically after 3 s.

**Launcher.** M5Launcher lives in an app partition of subtype `test`. When
that exists, PocketWUI never flashes anything itself: it could overwrite
another installed app. "Back to Launcher" is a 200 ms deep sleep, which
Launcher's default "DeepSleep starts Launcher" setting catches. Flashed
standalone, `partitions.csv` provides two 3 MB app slots, and
`/api/firmware/install` updates in place.

## Hardware notes

- M5Unified reports the SD pin map; nothing is hard-coded. On the ADV the SD
  card shares its SPI bus with the EXT header, whose chip-select G5 is driven
  high before mounting.
- The SD clock is picked from `WUI_SD_HZ_LADDER` (20 → 10 → 4 MHz) by a
  write/read-back probe. The pins go through the GPIO matrix, so faster clocks
  aren't reliable.
- No PSRAM on the StampS3, so heap is the scarce resource. Every buffer size in
  `config.h` was chosen with that in mind. The About tab in Settings shows the
  free heap and its low-water mark, so you can check it on a real device.

## Known limitations

- Plain HTTP only (TLS would cost much of the free RAM and give certificate
  warnings anyway).
- WebDAV `PUT` with chunked encoding (macOS Finder) is refused.
- One background job at a time; two background download streams.
- Folder ZIPs are limited to `WUI_ZIP_MAX_ENTRIES` (4000) items per download.
- Joining a home network from a phone that is on the Cardputer's hotspot can
  drop that phone: the hotspot moves to the home network's channel. The new
  address is on the device screen.
- The captive-portal DNS answers on every interface (arduino-esp32 3.x's
  DNSServer binds to all of them). Only the hotspot's clients ever ask it.
- At boot, joining saved networks blocks start-up for up to ~12 s per network
  in range. The screen says so.
