#!/usr/bin/env python3
"""Run the WUI web interface on your laptop, backed by a local folder.

    python3 tools/mock_server.py --root /tmp/fakesd --password test1234

It speaks exactly the same API as the firmware (same paths, same JSON, same
chunked-upload and Range semantics), so the UI can be exercised — and its
design iterated on — without flashing anything. It is a development aid only.
"""
import argparse, json, os, secrets, time, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = "/tmp/fakesd"
PASSWORD = "test1234"
SESSIONS = set()
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def safe(path):
    """Mirror of wui_safe_path(): clamp to ROOT, reject FAT-illegal names."""
    parts = []
    for seg in (path or "/").split("/"):
        if seg in ("", "."):
            continue
        if seg == "..":
            if parts:
                parts.pop()
            continue
        if len(seg) > 64 or any(c in seg for c in '\\:*?"<>|') or any(ord(c) < 32 for c in seg):
            return None
        parts.append(seg)
    rel = os.path.join(ROOT, *parts)
    return os.path.normpath(rel), "/" + "/".join(parts)


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *a):
        print("  %s" % (fmt % a))

    # -- plumbing ---------------------------------------------------------
    def q(self, key, default=""):
        qs = urllib.parse.urlparse(self.path).query
        return urllib.parse.parse_qs(qs).get(key, [default])[0]

    def body(self):
        n = int(self.headers.get("Content-Length", 0))
        return self.rfile.read(n) if n else b""

    def send_json(self, code, obj, extra=None):
        raw = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(raw)

    def authed(self):
        cookie = self.headers.get("Cookie", "")
        for part in cookie.split(";"):
            part = part.strip()
            if part.startswith("wui=") and part[4:] in SESSIONS:
                return True
        self.send_json(401, {"error": "not signed in"})
        return False

    # -- routes -----------------------------------------------------------
    def do_GET(self):
        route = urllib.parse.urlparse(self.path).path
        if route == "/":
            raw = open(os.path.join(HERE, "web", "index.html"), "rb").read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
            return
        if route == "/api/status":
            if not self.authed():
                return
            total = shutil_total()
            return self.send_json(200, {
                "fw": "mock", "chip": "host", "ssid": "mock-net", "ap": False,
                "ip": "127.0.0.1", "rssi": -42, "heap": 180000, "psram": 2000000,
                "uptime": int(time.time() - T0), "sd_type": "MOCK",
                "sd_total": total[0], "sd_used": total[1]})
        if route == "/api/list":
            if not self.authed():
                return
            s = safe(self.q("path", "/"))
            if not s:
                return self.send_json(400, {"error": "bad path"})
            real, shown = s
            if not os.path.isdir(real):
                return self.send_json(404, {"error": "no such directory: " + shown})
            entries = []
            for name in os.listdir(real):
                p = os.path.join(real, name)
                st = os.stat(p)
                entries.append({"name": name, "dir": os.path.isdir(p),
                                "size": st.st_size, "mtime": int(st.st_mtime)})
            return self.send_json(200, {"path": shown, "entries": entries})
        if route == "/api/download":
            if not self.authed():
                return
            s = safe(self.q("path"))
            if not s or not os.path.isfile(s[0]):
                return self.send_json(404, {"error": "no such file"})
            real = s[0]
            size = os.path.getsize(real)
            start, end, partial = 0, max(size - 1, 0), False
            rng = self.headers.get("Range", "")
            if rng.startswith("bytes=") and size:
                a, _, b = rng[6:].partition("-")
                start = int(a or 0)
                end = min(int(b), size - 1) if b else size - 1
                partial = True
            cap = int(self.q("max", "0") or 0)
            if cap and not partial and size > cap:
                end = cap - 1
            length = (end - start + 1) if size else 0
            self.send_response(206 if partial else 200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(length))
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Content-Disposition",
                             'attachment; filename="%s"' % os.path.basename(real))
            if partial:
                self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, size))
            self.end_headers()
            with open(real, "rb") as f:
                f.seek(start)
                left = length
                while left > 0:
                    chunk = f.read(min(65536, left))
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    left -= len(chunk)
            return
        self.send_response(302)
        self.send_header("Location", "/")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_POST(self):
        route = urllib.parse.urlparse(self.path).path
        if route == "/api/login":
            pw = json.loads(self.body() or b"{}").get("password", "")
            if not secrets.compare_digest(pw, PASSWORD):
                return self.send_json(401, {"error": "wrong password"})
            tok = secrets.token_hex(16)
            SESSIONS.add(tok)
            return self.send_json(200, {"ok": True}, {
                "Set-Cookie": "wui=%s; Path=/; HttpOnly; SameSite=Strict" % tok})
        if route == "/api/logout":
            SESSIONS.clear()
            return self.send_json(200, {"ok": True})
        if not self.authed():
            return
        if route == "/api/upload":
            s = safe(self.q("path"))
            if not s:
                return self.send_json(400, {"error": "bad path"})
            real = s[0]
            off = int(self.q("offset", "0"))
            data = self.body()
            have = os.path.getsize(real) if os.path.exists(real) else 0
            if off == 0:
                open(real, "wb").close()
                have = 0
            if have != off:
                return self.send_json(409, {"error": "offset mismatch", "size": have})
            with open(real, "ab") as f:
                f.write(data)
            return self.send_json(200, {"ok": True, "size": off + len(data)})
        if route in ("/api/mkdir", "/api/touch", "/api/delete"):
            s = safe(self.q("path"))
            if not s or s[1] == "/":
                return self.send_json(400, {"error": "bad path"})
            real = s[0]
            try:
                if route == "/api/mkdir":
                    if os.path.isdir(real):        # idempotent, like the firmware
                        return self.send_json(200, {"ok": True})
                    if os.path.exists(real):
                        return self.send_json(409, {"error": "already exists"})
                    os.mkdir(real)
                elif route == "/api/touch":
                    if os.path.exists(real):
                        return self.send_json(409, {"error": "already exists"})
                    open(real, "wb").close()
                else:
                    import shutil
                    shutil.rmtree(real) if os.path.isdir(real) else os.remove(real)
            except OSError as e:
                return self.send_json(500, {"error": str(e)})
            return self.send_json(200, {"ok": True})
        if route == "/api/rename":
            a, b = safe(self.q("from")), safe(self.q("to"))
            if not a or not b or a[1] == "/" or b[1] == "/":
                return self.send_json(400, {"error": "bad path"})
            if not os.path.exists(a[0]):
                return self.send_json(404, {"error": "no such path: " + a[1]})
            if os.path.exists(b[0]):
                return self.send_json(409, {"error": "already exists: " + b[1]})
            try:
                os.rename(a[0], b[0])
            except OSError as e:
                return self.send_json(500, {"error": str(e)})
            return self.send_json(200, {"ok": True})
        if route in ("/api/wifi", "/api/passwd", "/api/reboot"):
            return self.send_json(200, {"ok": True, "connected": True, "ip": "127.0.0.1"})
        self.send_json(404, {"error": "no such endpoint"})


def shutil_total():
    import shutil
    u = shutil.disk_usage(ROOT)
    return [u.total, u.used]


T0 = time.time()

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="/tmp/fakesd")
    ap.add_argument("--port", type=int, default=8791)
    ap.add_argument("--password", default="test1234")
    a = ap.parse_args()
    ROOT, PASSWORD = os.path.abspath(a.root), a.password
    os.makedirs(ROOT, exist_ok=True)
    print("WUI mock on http://127.0.0.1:%d/  root=%s  password=%s" % (a.port, ROOT, PASSWORD))
    ThreadingHTTPServer(("127.0.0.1", a.port), H).serve_forever()
