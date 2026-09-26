# Changelog

## 2.1.0

**New**
- Join Wi‑Fi from the Cardputer itself: press `w` on the main screen, pick a
  network (`;` `.` or fn+arrows, `r` rescans, ` goes back), type its key
  (Tab shows it, fn+` goes back) and press Enter. Joined networks are saved
  like ones added from the web UI.

**Changed**
- Uses M5Cardputer 1.2.0 for the keyboard (Cardputer and Cardputer ADV).

## 2.0.0 — PocketWUI

The WUI grows up and gets a name.

**New**
- USB cable: the Cardputer becomes a network adapter (`http://192.168.7.1`,
  your computer keeps its internet), and can lend its SD card as a USB drive.
- Scan-to-connect QR codes on the device: join its Wi‑Fi, then sign in
  without typing the password (single-use, 2-minute codes).
- Search the whole card by name (substring or `*.glob`).
- Download folders or any selection as one ZIP (exact size, ZIP64).
- Previews: photo gallery, video and music with seeking, PDF, Markdown, text.
- Trash, copy, "Replace?" on name clashes, SHA-256 checksums, folder sizes.
- WebDAV network drive at `/dav/`.
- Up to five saved Wi‑Fi networks with automatic rejoin; captive portal on the
  Cardputer's own hotspot; Wi‑Fi scan; hotspot key, device name, keep-hotspot-on.
- M5Launcher: "Back to Launcher", and firmware files are handed to Launcher
  instead of being flashed over another app. Standalone installs update
  themselves.
- New web UI: friendly, light/dark, phone-sized, keyboard shortcuts, paste to
  upload, settings panel, background job progress.
- Device screen: battery, USB status, dims when idle, USB-drive screen.

**Changed**
- Arduino core 3.3.12 (pioarduino); USB-OTG mode (no USB serial console).
- Standalone flash layout has two 3 MB app slots (`partitions.csv`).
- Hotspot is now `PocketWUI-XXXX` (unique per device).
- Long downloads run in the background, so the UI stays responsive.

**For developers**
- `test/hostsim`: the real server code running on a PC; end-to-end API and
  browser tests; CI builds firmware and runs everything.

## 1.x — Cardputer ADV WUI

Password-protected web file manager: chunked resumable uploads, Range
downloads, folder upload, drag to move, text editor, console.
