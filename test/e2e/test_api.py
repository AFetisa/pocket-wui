"""End-to-end API tests: the firmware's real server code (test/hostsim) driven
over HTTP. No dependencies beyond Python 3.

    make -C test sim            # builds the simulator, then runs this
    python3 test/e2e/test_api.py
"""
import base64, hashlib, http.client, http.cookiejar, io, json, os, shutil, socket, subprocess
import sys, tempfile, time, urllib.parse, urllib.request, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.join(HERE, "..", "hostsim", "pocketwui-sim")
PASSWORD = "e2e-pass-123"
failures = []


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Client:
    def __init__(self, port):
        self.base = "http://127.0.0.1:%d" % port
        self.jar = http.cookiejar.CookieJar()
        self.op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar))

    def req(self, method, path, body=None, headers=None, auth=None):
        h = dict(headers or {})
        if auth is not None:
            h["Authorization"] = "Basic " + base64.b64encode((":" + auth).encode()).decode()
        r = urllib.request.Request(self.base + path, data=body, method=method, headers=h)
        try:
            with self.op.open(r, timeout=30) as resp:
                return resp.status, dict(resp.headers), resp.read()
        except urllib.error.HTTPError as e:
            return e.code, dict(e.headers), e.read()

    def json(self, method, path, body=None, **kw):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        elif isinstance(body, str):
            body = body.encode()
        st, h, data = self.req(method, path, body, **kw)
        try:
            return st, json.loads(data or b"{}")
        except ValueError:
            return st, {"raw": data.decode(errors="replace")}


def check(cond, what):
    if not cond:
        failures.append(what)
        print("FAIL ", what)
    else:
        print("ok   ", what)


def q(p):
    return urllib.parse.quote(p, safe="")


def wait_job(c, timeout=30):
    t0 = time.time()
    while time.time() - t0 < timeout:
        st, j = c.json("GET", "/api/job")
        if j.get("state") != "running":
            return j
        time.sleep(0.1)
    return {"state": "timeout"}


def main():
    card = tempfile.mkdtemp(prefix="pocketwui-card-")
    os.makedirs(os.path.join(card, "Photos", "Trip"))
    open(os.path.join(card, "notes.txt"), "w").write("hello\n")
    big = os.urandom(700_000)
    open(os.path.join(card, "Photos", "Trip", "IMG_0001.jpg"), "wb").write(big)
    open(os.path.join(card, "Photos", "holiday.png"), "wb").write(b"x" * 10)
    port = free_port()
    proc = subprocess.Popen([SIM, "--root", card, "--port", str(port), "--password", PASSWORD],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        for _ in range(50):
            try:
                socket.create_connection(("127.0.0.1", port), 0.2).close()
                break
            except OSError:
                time.sleep(0.1)
        run(Client(port), card, big)
    finally:
        proc.terminate()
        proc.wait()
        shutil.rmtree(card, ignore_errors=True)
    print("\n%d failure(s)" % len(failures) if failures else "\nall API tests passed")
    sys.exit(1 if failures else 0)


def run(c, card, big):
    # --- auth
    st, j = c.json("GET", "/api/session")
    check(st == 200 and j == {"signed_in": False}, "session probe says signed out, with a 200")
    st, _ = c.json("GET", "/api/list?path=/")
    check(st == 401, "API refuses without a session")
    st, _ = c.json("POST", "/api/login", {"password": "nope"})
    check(st == 401, "wrong password refused")
    st, j = c.json("POST", "/api/login", {"password": PASSWORD})
    check(st == 200 and j.get("ok"), "login")
    st, j = c.json("GET", "/api/session")
    check(j == {"signed_in": True}, "session probe says signed in")
    st, h, _ = c.req("GET", "/login?k=not-a-real-token")
    check(st in (200, 302), "bad QR token lands somewhere sane")

    # --- listing, traversal
    st, j = c.json("GET", "/api/list?path=/")
    names = sorted(e["name"] for e in j.get("entries", []))
    check(names == ["Photos", "notes.txt"], "list root: %s" % names)
    st, j = c.json("GET", "/api/list?path=" + q("/../../etc"))
    check(st == 200 and j.get("path") == "/etc" or st in (400, 404), "path traversal clamped to the card")

    # --- search
    st, j = c.json("GET", "/api/find?path=/&q=img")
    check([r["path"] for r in j.get("results", [])] == ["/Photos/Trip/IMG_0001.jpg"], "find substring")
    st, j = c.json("GET", "/api/find?path=/&q=" + q("*.PNG"))
    check([r["path"] for r in j.get("results", [])] == ["/Photos/holiday.png"], "find glob, case-insensitive")

    # --- upload with resume
    data = os.urandom(300_000)
    st, j = c.json("POST", "/api/upload?path=/up.bin&offset=0", data[:100_000])
    check(j.get("size") == 100_000, "upload chunk 1")
    st, j = c.json("POST", "/api/upload?path=/up.bin&offset=5", data[5:])
    check(st == 409 and j.get("size") == 100_000, "wrong offset -> 409 with the real size")
    st, j = c.json("POST", "/api/upload?path=/up.bin&offset=100000&final=1", data[100_000:])
    check(j.get("size") == 300_000, "upload final chunk")
    check(open(os.path.join(card, "up.bin"), "rb").read() == data, "uploaded bytes identical")

    # --- download, ranges, preview
    st, h, body = c.req("GET", "/api/download?path=" + q("/Photos/Trip/IMG_0001.jpg"))
    check(st == 200 and body == big and h.get("Content-Length") == str(len(big)), "download (background stream) identical")
    st, h, body = c.req("GET", "/api/download?path=/up.bin", headers={"Range": "bytes=10-19"})
    check(st == 206 and body == data[10:20] and h.get("Content-Range") == "bytes 10-19/300000", "Range a-b")
    st, h, body = c.req("GET", "/api/download?path=/up.bin", headers={"Range": "bytes=-7"})
    check(st == 206 and body == data[-7:], "suffix Range")
    st, h, body = c.req("GET", "/api/download?path=/up.bin", headers={"Range": "bytes=999999-"})
    check(st == 416, "unsatisfiable Range -> 416")
    st, h, body = c.req("GET", "/api/download?path=/notes.txt&inline=1")
    check(h.get("Content-Type", "").startswith("text/plain") and "inline" in h.get("Content-Disposition", ""), "inline text preview")
    open(os.path.join(card, "evil.html"), "w").write("<script>alert(1)</script>")
    st, h, body = c.req("GET", "/api/download?path=/evil.html&inline=1")
    check(h.get("Content-Security-Policy") == "sandbox", "HTML preview is sandboxed")
    st, h, body = c.req("GET", "/api/download?path=/notes.txt&max=3")
    check(body == b"hel", "max= caps the body (console cat)")

    # --- zip
    st, h, body = c.req("GET", "/api/zip?path=/Photos")
    z = zipfile.ZipFile(io.BytesIO(body))
    check(st == 200 and int(h.get("Content-Length", -1)) == len(body), "zip Content-Length exact")
    check(z.testzip() is None and z.read("Photos/Trip/IMG_0001.jpg") == big, "zip contents intact")
    st, j = c.json("POST", "/api/zipticket", "/up.bin\n/notes.txt")
    st, h, body = c.req("GET", "/api/zip?t=%s&name=mine" % j.get("t"))
    z = zipfile.ZipFile(io.BytesIO(body))
    check(sorted(z.namelist()) == ["notes.txt", "up.bin"] and "mine.zip" in h.get("Content-Disposition", ""), "multi-item zip via ticket")
    st, _ = c.json("GET", "/api/zip?t=%s" % j.get("t"))
    check(st == 410, "zip ticket is single-use")
    st, _ = c.json("GET", "/api/zip?path=/")
    check(st == 400, "refuses to zip the whole card")

    # --- copy / hash jobs
    st, j = c.json("POST", "/api/copy?from=/Photos&to=" + q("/Photos copy"))
    job = wait_job(c)
    check(st == 200 and job.get("state") == "done", "copy folder job")
    check(open(os.path.join(card, "Photos copy", "Trip", "IMG_0001.jpg"), "rb").read() == big, "copied bytes identical")
    st, j = c.json("POST", "/api/copy?from=/Photos&to=/Photos/inside")
    check(st == 400, "copy into itself refused up front")
    st, j = c.json("POST", "/api/hash?path=/up.bin")
    job = wait_job(c)
    check(job.get("result") == hashlib.sha256(data).hexdigest(), "sha256 job matches")
    st, j = c.json("GET", "/api/du?path=/Photos")
    check(j.get("bytes") == len(big) + 10, "folder size")

    # --- trash, rename, mkdir
    st, _ = c.json("POST", "/api/delete?path=/notes.txt&trash=1")
    check(os.path.exists(os.path.join(card, ".trash", "notes.txt")), "delete moves to the trash")
    open(os.path.join(card, "notes.txt"), "w").write("second")
    c.json("POST", "/api/delete?path=/notes.txt&trash=1")
    check(os.path.exists(os.path.join(card, ".trash", "notes (2).txt")), "trash keeps both copies")
    st, _ = c.json("POST", "/api/rename?from=/Photos&to=/Photos/sub")
    check(st == 400, "move a folder into itself refused")
    st, _ = c.json("POST", "/api/rename?from=/up.bin&to=" + q("/Photos copy"))
    check(st == 409, "rename onto an existing name refused")
    st, _ = c.json("POST", "/api/mkdir?path=/A")
    st2, _ = c.json("POST", "/api/mkdir?path=/A")
    check(st == 200 and st2 == 200, "mkdir is idempotent for folders")
    st, _ = c.json("POST", "/api/trash/empty")
    check(not os.path.exists(os.path.join(card, ".trash")), "empty trash")

    # --- WebDAV
    st, h, _ = c.req("OPTIONS", "/dav/")
    check(st == 200 and "PROPFIND" in h.get("Allow", ""), "DAV OPTIONS")
    d = Client(int(c.base.rsplit(":", 1)[1]))           # a separate client: no cookie
    st, h, _ = d.req("PROPFIND", "/dav/")
    check(st == 401 and "Basic" in h.get("WWW-Authenticate", ""), "DAV asks for Basic auth")
    st, h, _ = d.req("PROPFIND", "/dav/", auth="wrong")
    check(st == 401, "DAV wrong password refused")
    st, h, body = d.req("PROPFIND", "/dav/", headers={"Depth": "1"}, auth=PASSWORD)
    check(st == 207 and b"<D:href>/dav/Photos/</D:href>" in body, "DAV PROPFIND depth 1")
    st, h, _ = d.req("PUT", "/dav/" + q("new file.bin"), body=data, auth=PASSWORD)
    check(st == 201 and open(os.path.join(card, "new file.bin"), "rb").read() == data, "DAV PUT")
    st, h, body = d.req("GET", "/dav/" + q("new file.bin"), auth=PASSWORD)
    check(st == 200 and body == data, "DAV GET")
    st, _, _ = d.req("MKCOL", "/dav/Docs", auth=PASSWORD)
    check(st == 201, "DAV MKCOL")
    st, _, _ = d.req("MOVE", "/dav/" + q("new file.bin"), headers={"Destination": "http://x/dav/Docs/moved.bin"}, auth=PASSWORD)
    check(st == 201 and os.path.exists(os.path.join(card, "Docs", "moved.bin")), "DAV MOVE")
    st, _, _ = d.req("COPY", "/dav/Docs/moved.bin", headers={"Destination": "/dav/copy.bin", "Overwrite": "F"}, auth=PASSWORD)
    check(st == 201, "DAV COPY")
    st, _, _ = d.req("COPY", "/dav/Docs/moved.bin", headers={"Destination": "/dav/copy.bin", "Overwrite": "F"}, auth=PASSWORD)
    check(st == 412, "DAV COPY honours Overwrite: F")
    st, _, _ = d.req("DELETE", "/dav/Docs", auth=PASSWORD)
    check(st == 204 and not os.path.exists(os.path.join(card, "Docs")), "DAV DELETE")

    # --- USB drive mode locks the web out of the card
    c.json("POST", "/api/usb", {"drive": True})
    st, j = c.json("GET", "/api/list?path=/")
    check(st == 423, "card lent to USB -> 423")
    st, j = c.json("GET", "/api/status")
    check(j.get("sd_lent") is True, "status reports the card as lent")
    c.json("POST", "/api/usb", {"drive": False})
    st, j = c.json("GET", "/api/list?path=/")
    check(st == 200, "card back after drive mode")

    # --- device settings
    st, j = c.json("GET", "/api/wifi/scan")
    check(st == 200 and j["networks"][0]["ssid"] == "Home Wi-Fi", "wifi scan")
    st, j = c.json("POST", "/api/wifi", {"ssid": "Home", "password": "pw"})
    check(j.get("connected") is True, "wifi join")
    st, j = c.json("GET", "/api/wifi/saved")
    check(j.get("saved") == ["Home"], "wifi saved list")
    st, j = c.json("POST", "/api/wifi/ap", {"password": "short"})
    check(st == 400, "AP key too short refused")
    st, j = c.json("POST", "/api/passwd", {"password": "tiny"})
    check(st == 400, "short WUI password refused")
    st, j = c.json("POST", "/api/passwd", {"password": PASSWORD + "-2"})
    st2, j2 = c.json("GET", "/api/status")
    check(st == 200 and st2 == 200, "password change keeps this browser signed in")

    # --- firmware check
    open(os.path.join(card, "fake.bin"), "wb").write(b"not firmware" * 20)
    st, j = c.json("POST", "/api/firmware/check?path=/fake.bin")
    check(j.get("ok") is False and j.get("reason"), "non-firmware .bin rejected with a reason")

    # --- lockout
    e = Client(int(c.base.rsplit(":", 1)[1]))
    codes = [e.json("POST", "/api/login", {"password": "x"})[0] for _ in range(9)]
    check(codes[-1] == 429, "repeated bad passwords lock out (%s)" % codes[-1])


if __name__ == "__main__":
    main()
