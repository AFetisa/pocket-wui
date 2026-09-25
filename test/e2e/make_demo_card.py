"""Fills a folder with friendly demo content for the simulator (and README screenshots).
   python3 test/e2e/make_demo_card.py /tmp/demo-sd [firmware.bin]"""
import os, shutil, struct, sys, zlib

def png(path, w, h, top, bottom):
    rows = []
    for y in range(h):
        t = y / (h - 1)
        c = bytes(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        rows.append(b"\x00" + c * w)
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))

def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "/tmp/demo-sd"
    shutil.rmtree(root, ignore_errors=True)
    for d in ["Photos/Road trip", "Music", "Documents", "downloads", "firmware"]:
        os.makedirs(os.path.join(root, d), exist_ok=True)
    pics = {"beach.png": ((125, 211, 252), (14, 116, 144)), "sunset.png": ((253, 186, 116), (190, 24, 93)),
            "forest.png": ((134, 239, 172), (21, 94, 57)), "mountains.png": ((226, 232, 240), (71, 85, 105))}
    for name, (a, b) in pics.items():
        png(os.path.join(root, "Photos/Road trip", name), 480, 320, a, b)
    png(os.path.join(root, "Photos/cat.png"), 320, 320, (251, 207, 232), (219, 39, 119))
    open(os.path.join(root, "Documents/Shopping list.md"), "w").write(
        "# Shopping list\n\n- Coffee beans\n- **Oat milk**\n- Batteries for the *Cardputer*\n\n> Remember the reusable bags.\n")
    open(os.path.join(root, "Documents/notes.txt"), "w").write("Meeting at 10:00\nBring the Cardputer demo.\n")
    with open(os.path.join(root, "Music/theme.mp3"), "wb") as f:
        f.write(os.urandom(2_400_000))
    with open(os.path.join(root, "downloads/Bruce.bin"), "wb") as f:
        f.write(os.urandom(1_300_000))
    open(os.path.join(root, "README.txt"), "w").write("This card belongs to Alex.\n")
    if len(sys.argv) > 2 and os.path.exists(sys.argv[2]):
        shutil.copy(sys.argv[2], os.path.join(root, "firmware/PocketWUI.bin"))

main()
