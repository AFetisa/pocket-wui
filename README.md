# PocketWUI

**Your Cardputer's SD card, in any browser.**

Browse, preview, upload and download the files on your M5Stack Cardputer from
your phone or laptop, over Wi‑Fi or a single USB cable. There's no app to
install, and it works without internet.

<p>
  <img src="docs/screenshots/files.png" alt="PocketWUI in a desktop browser" width="640">
  <img src="docs/screenshots/phone-dark.png" alt="PocketWUI on a phone, dark mode" width="170">
</p>

> **Status:** version 2.0 is ready to try, but it hasn't been tested on a real
> Cardputer yet. Everything that can be tested without the hardware passes
> automatic tests (see [Status](#status)). If something acts up, please
> [open an issue](../../issues).

---

## What you can do

- **Drop files in.** Drag files, or whole folders, onto the page. Any size
  works, and an interrupted upload picks up where it stopped.
- **Look before you download.** Flip through photos with the arrow keys, play
  video and music, read PDFs and notes, and edit text files right there.
- **Find things.** Type to filter the folder you're in; press Enter to search
  the whole card.
- **Grab lots at once.** Tick a few files (or a folder) and download them as
  one ZIP.
- **Change your mind.** Deleted things go to a Trash first.
- **Plug in a cable instead of using Wi‑Fi.** Connect the Cardputer to a
  computer and open `http://192.168.7.1`. Your computer keeps its own internet.
  You can also lend the card to the computer as an ordinary USB drive.
- **Scan to connect.** Press the Cardputer's button for QR codes: one joins
  its Wi‑Fi, the other signs you in without typing a password.
- **Use it as a network drive** in Finder, file-manager apps, VLC, Infuse or
  rclone (WebDAV).
- **Keep your other apps.** It installs from M5Launcher, and a *Back to
  Launcher* button takes you back.

It's password protected, and nothing ever leaves your own network.

---

## Install it

### With M5Launcher (recommended)

1. Download **`PocketWUI.bin`**: from the [latest release](../../releases), or
   from [`build/`](build/) in this repository.
2. Copy it onto the Cardputer's SD card. Any folder works, `downloads/` for
   example.
3. Put the card back, start the Cardputer into **M5Launcher**, choose **SD**,
   pick `PocketWUI.bin` and choose **Install**.
4. Launcher starts PocketWUI. The screen shows everything you need.

Use `PocketWUI.bin`, **not** `PocketWUI-full-flash.bin`. The full image
includes a bootloader, and Launcher can't install that.

### Without Launcher

Flash `PocketWUI-full-flash.bin` at address `0x0`, either in the browser with
[ESP Web Tools / esptool-js](https://espressif.github.io/esptool-js/) or with
`esptool.py --chip esp32s3 write_flash 0x0 PocketWUI-full-flash.bin`. To put
the Cardputer into flashing mode, **hold the G0 button while plugging in the
USB cable**. Installed this way, PocketWUI can update itself later from its
Settings page.

---

## First time

1. **Turn it on.** The screen shows a Wi‑Fi name (`PocketWUI-XXXX`), its key,
   an address and a password.
2. **Press the side button.** The first QR code joins the Cardputer's Wi‑Fi;
   scan it with your phone's camera. Press again for the second QR code, which
   opens PocketWUI already signed in.
   (Or join the Wi‑Fi by hand, open `http://192.168.4.1` and type the
   password.)
3. **Add your home Wi‑Fi** in **Settings → Wi‑Fi → Scan for networks**. From
   then on, open **`http://cardputer.local`** from anything on that network.
4. **Pick your own password** in **Settings → Device**. Until you do, the
   first-boot password stays on the screen.

## Ways to connect

| You're… | Do this | Your internet |
|---|---|---|
| at home or work | Add the Wi‑Fi once, then open `http://cardputer.local` | stays on |
| out, with a phone | Turn on your phone's hotspot and add it as a network on the Cardputer | stays on (mobile data) |
| out, no hotspot | Join the Cardputer's own Wi‑Fi (QR code), open `http://192.168.4.1` | phones keep mobile data; tap "keep connection" if asked |
| at a computer | **Plug in the USB cable**, open `http://192.168.7.1` | stays on |

The Cardputer remembers up to five networks and picks the strongest one in
range. If none is around, it starts its own Wi‑Fi and keeps quietly checking
for them, but never while your phone is connected to it.

## Tips

- **Lots of files?** *Settings → USB → Lend it to the computer* turns the SD
  card into a regular USB drive on your computer. Eject it there when you're
  done.
- **Big files are fine.** Uploads are sent in pieces, so a Wi‑Fi hiccup costs
  seconds, not the whole transfer.
- **Keyboard shortcuts:** `/` searches, `←` `→` flip through photos, `Esc`
  closes, `Ctrl+S` saves in the editor, and `` Ctrl+` `` opens a command
  console for power users (type `help`).
- **Paste to upload:** copy a screenshot and press `Ctrl+V` in PocketWUI.

## Using it alongside M5Launcher

- **Settings → Device → Back to Launcher** returns to M5Launcher's menu. (This
  uses Launcher's "DeepSleep starts Launcher" option, which is on by default.
  If it's off, restart the Cardputer instead.)
- **Updating:** download the new `PocketWUI.bin`, upload it through PocketWUI
  itself (**Settings → Device → Update firmware**), then install it from
  Launcher. PocketWUI tells you what to do.
- Your password and Wi‑Fi settings are normally kept when you switch apps
  (unless another app wipes the Cardputer's shared settings storage).

## Troubleshooting

**`cardputer.local` doesn't open.** Some phones and older Android versions
don't support `.local` names. Use the number shown on the Cardputer's screen
instead, like `http://192.168.1.54`.

**Forgot the password.** Hold the side button for 5 seconds. A countdown
appears, then the password and saved Wi‑Fi networks are reset. Your files are
not touched.

**Can't flash over USB any more.** The USB port is busy being a network
adapter. Hold **G0** while plugging in, or turn USB off in **Settings → USB**
and restart.

**The USB network doesn't show up on Windows.** It needs Windows 11 or a
recent Windows 10; macOS and Linux work out of the box. Try another cable, as
some cables only charge.

**Finder won't copy files onto the network drive.** macOS Finder uploads in a
way the Cardputer can't accept yet. Browsing and opening files works; to add
files, use the web page, Cyberduck, or rclone.

**The page pauses while a big download runs.** Up to two downloads run in the
background without getting in the way. A third one is served directly, and the
page waits until it finishes.

## Privacy and safety

- There's no default password. A random one is created on first boot and
  shown only on the device's screen.
- Sign-in QR codes work once and expire after two minutes.
- Repeated wrong passwords lock sign-in for a minute.
- Deleting goes to the Trash first; "Delete forever" asks again.
- Everything stays between your device and the Cardputer: no cloud, no
  tracking, no internet required.
- It uses plain HTTP, like most gadgets on a home network. Use it on networks
  you trust.

---

## Status

**Version 2.0.** Here's what has and hasn't been verified:

- ✅ Builds cleanly for the ESP32-S3 (CI on every push).
- ✅ The real server code (files, uploads, downloads, ZIP, search, trash,
  copy, WebDAV, settings) is compiled for a PC and passes 59 API checks and 30
  browser checks in Chromium.
- ✅ The ZIP writer is checked with Python's `zipfile`, including archives
  over 4 GB.
- ⚠️ **Not yet run on a real Cardputer:** Wi‑Fi behaviour, the USB network
  adapter and drive, the screen, battery reading, *Back to Launcher* and
  self-update. 2.0 needs its first real-world test.

## For tinkerers

Building from source, the simulator that runs on your PC, the tests, the
HTTP/WebDAV API with `curl` examples, and how it works inside:
**[docs/DEVELOPING.md](docs/DEVELOPING.md)**.

## Thanks

Built on [M5Unified / M5GFX](https://github.com/m5stack/M5Unified),
[EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) (USB network
and drive) and the [pioarduino](https://github.com/pioarduino/platform-espressif32)
platform. It plays nicely with [M5Launcher](https://github.com/bmorcelli/Launcher).

## License

[MIT](LICENSE).
