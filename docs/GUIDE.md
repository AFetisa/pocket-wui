# PocketWUI guide

Everything past the basics. Start with the [README](../README.md).

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
