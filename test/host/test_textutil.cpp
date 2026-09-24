#include "arduino_shim.h"
#define WUI_MAX_PATH 192
#include "../../src/paths.cpp"
#include "../../src/textutil.cpp"
#include <iostream>

static int fails = 0;
#define EQ(a, b) do { String _a = (a); String _b = (b); if (_a != _b) { \
  std::cout << "FAIL  line " << __LINE__ << ": " << _a << "  !=  " << _b << "\n"; fails++; } } while (0)
#define OK(c) do { if (!(c)) { std::cout << "FAIL  line " << __LINE__ << ": " #c "\n"; fails++; } } while (0)

int main() {
  EQ(wui_mime_for("a/b/Photo.JPG"), "image/jpeg");
  EQ(wui_mime_for("song.mp3"), "audio/mpeg");
  EQ(wui_mime_for("noext"), "application/octet-stream");
  EQ(wui_mime_for("dir.d/noext"), "application/octet-stream");
  OK(wui_mime_needs_sandbox(wui_mime_for("x.html")));
  OK(wui_mime_needs_sandbox(wui_mime_for("x.svg")));
  OK(!wui_mime_needs_sandbox(wui_mime_for("x.png")));
  OK(wui_mime_is_text(wui_mime_for("x.md")));

  OK(wui_name_matches("Holiday Photo.JPG", "photo"));
  OK(!wui_name_matches("notes.txt", "photo"));
  OK(wui_name_matches("IMG_0042.jpg", "img_*.JPG"));
  OK(!wui_name_matches("IMG_0042.jpg.bak", "*.jpg"));
  OK(wui_name_matches("a.bin", "?.bin"));
  OK(!wui_name_matches("ab.bin", "?.bin"));
  OK(wui_name_matches("anything", ""));
  OK(wui_name_matches("aaaaaaaaaaaaaaaaaaaaaaaaaaaaab", "*a*a*a*a*a*b"));

  String out;
  OK(wui_b64_decode("OnMzY3JldA==", out)); EQ(out, ":s3cret");
  OK(wui_b64_decode("dXNlcjpwYXNz", out)); EQ(out, "user:pass");
  OK(!wui_b64_decode("bad$", out));

  EQ(wui_http_date(784111777), "Sun, 06 Nov 1994 08:49:37 GMT");
  EQ(wui_xml_escape("a<b>&\"c'"), "a&lt;b&gt;&amp;&quot;c&apos;");
  EQ(wui_url_encode_path("/My Files/ü.txt"), "/My%20Files/%C3%BC.txt");
  EQ(wui_content_disposition("my \"file\".txt", false),
     "attachment; filename=\"my _file_.txt\"; filename*=UTF-8''my%20%22file%22.txt");
  EQ(wui_content_disposition("ü.jpg", true),
     "inline; filename=\"__.jpg\"; filename*=UTF-8''%C3%BC.jpg");

  EQ(wui_dav_destination("http://cardputer.local/dav/new%20name.txt", "/dav"), "/new name.txt");
  EQ(wui_dav_destination("https://1.2.3.4:8080/dav", "/dav"), "/");
  EQ(wui_dav_destination("/dav/a/b", "/dav"), "/a/b");
  EQ(wui_dav_destination("http://host/other/x", "/dav"), "");
  EQ(wui_dav_destination("http://host/davx/x", "/dav"), "");

  std::cout << (fails ? "textutil tests FAILED\n" : "textutil tests ok\n");
  return fails ? 1 : 0;
}
