#include "ui.h"
#include "config.h"
#include "auth.h"
#include "net.h"
#include "storage.h"
#include "qrtext.h"
#include "usb.h"
#include <M5Cardputer.h>
#include <Preferences.h>

namespace {

constexpr uint16_t BG    = 0x0861;   // near-black
constexpr uint16_t FG    = 0xCE79;   // pale mint
constexpr uint16_t DIM   = 0x6B2D;
constexpr uint16_t ACC   = 0x27E6;   // green
constexpr uint16_t WARN  = 0xFCC0;

// Text sizes, smallest first. "+" / "-" on the main screen step through them
// and the choice survives reboots (and factory reset: it's about the reader,
// not the device). Layout is derived from the font's line height, g_line.
// QR screens always use the first: their text column beside the code is
// ~95px wide.
const lgfx::IFont *const SIZES[] = {&fonts::Font2, &fonts::DejaVu18, &fonts::DejaVu24};
constexpr int N_SIZES = sizeof(SIZES) / sizeof(SIZES[0]);
int g_size = 0;
int g_line = 16;                       // row pitch of the current font
int g_headH = 18;                      // title bar

void useFont(int i) {
  M5.Display.setFont(SIZES[i]);
  g_line = M5.Display.fontHeight();
  g_headH = g_line + 2;
}

int footY() { return M5.Display.height() - g_line - 1; }
void drawFooter(const char *hint);

// s cut to fit w pixels, "~" marking the cut. The fonts are proportional, so
// widths are measured, not counted.
String fit(const String &s, int w) {
  auto &d = M5.Display;
  if (d.textWidth(s) <= w) return s;
  String t = s;
  while (t.length() && d.textWidth(t + "~") > w) t.remove(t.length() - 1);
  return t + "~";
}

// s broken into lines of at most w pixels: at spaces where possible, mid-word
// (URLs, keys) where not.
std::vector<String> wrap(const String &s, int w) {
  auto &d = M5.Display;
  std::vector<String> out;
  String rest = s;
  while (rest.length()) {
    unsigned n = rest.length();
    while (n > 1 && d.textWidth(rest.substring(0, n)) > w) --n;
    if (n < rest.length()) {
      int sp = rest.substring(0, n + 1).lastIndexOf(' ');
      if (sp > 0) n = sp;
    }
    out.push_back(rest.substring(0, n));
    rest = rest.substring(n);
    rest.trim();
  }
  return out;
}

// Draws s wrapped to w from (x, y); returns the y below it.
int drawWrapped(const String &s, int x, int y, int w) {
  for (const String &l : wrap(s, w)) { M5.Display.setCursor(x, y); M5.Display.print(l); y += g_line; }
  return y;
}

// The button cycles through these. WIFI_QR only appears in access-point mode:
// in station mode the phone is already on your network and the home Wi-Fi key
// should not be on the screen.
// WIFI_PICK / WIFI_PASS join a network from the keyboard ("w" on INFO).
enum Screen { INFO, WIFI_QR, LOGIN_QR, WIFI_PICK, WIFI_PASS };
Screen   g_screen = INFO;
uint32_t g_next = 0;
uint32_t g_shownSecs = UINT32_MAX;     // countdown last drawn on LOGIN_QR
String   g_shownLink;                  // what the LOGIN_QR on screen encodes
bool     g_viaAp = false;              // reached LOGIN_QR from the join-Wi-Fi QR
uint32_t g_holdStart = 0;
String   g_toast;
uint32_t g_toastUntil = 0;
uint32_t g_lastInput = 0;              // for dimming
bool     g_dimmed = false;
bool     g_driveShown = false;
std::vector<net::ScanResult> g_nets;   // WIFI_PICK list
int      g_sel = 0;
String   g_pass;                       // WIFI_PASS entry, cleared on leaving
bool     g_showPass = false;
int      g_scroll = 0;                 // first INFO line shown, when they overflow

String url() { return "http://" + net::ip() + "/"; }

void factoryReset() {
  Preferences p;
  p.begin("wui-auth", false); p.clear(); p.end();
  p.begin("wui-net", false);  p.clear(); p.end();
  p.begin("wui-usb", false);  p.clear(); p.end();
  M5.Display.fillScreen(BG);
  M5.Display.setTextColor(WARN, BG);
  M5.Display.setCursor(8, 56);
  M5.Display.print("reset - rebooting");
  delay(900);
  ESP.restart();
}

// QR on the left, a narrow text column on the right. Returns the column's x.
int drawQRPanel(const String &payload, const char *step, const char *title) {
  auto &d = M5.Display;
  useFont(0);
  d.fillScreen(BG);
  int side = d.height() - 8;
  d.fillRect(0, 0, side + 8, d.height(), 0xFFFF);       // quiet zone for the scanner
  d.qrcode(payload, 4, 4, side, 1);                     // grows the version to fit
  int x = side + 14;
  d.setTextColor(DIM, BG); d.setCursor(x, 4);  d.print(step);
  d.setTextColor(ACC, BG); d.setCursor(x, 20); d.print(title);
  return x;
}

int panelW(int x) { return M5.Display.width() - x - 2; }

void drawWifiQR() {
  auto &d = M5.Display;
  int x = drawQRPanel(wui_wifi_qr(net::apSsid().c_str(), net::apPassword().c_str()), "step 1 of 2", "join wi-fi");
  d.setTextColor(DIM, BG); d.setCursor(x, 42); d.print("network");
  d.setTextColor(FG, BG);  d.setCursor(x, 58); d.print(fit(net::apSsid(), panelW(x)));
  d.setTextColor(DIM, BG); d.setCursor(x, 78); d.print("key");
  d.setTextColor(FG, BG);  d.setCursor(x, 94); d.print(fit(net::apPassword(), panelW(x)));
  d.setTextColor(DIM, BG); d.setCursor(x, footY()); d.print("btn: next");
}

void drawLoginCountdown() {
  auto &d = M5.Display;
  int x = d.height() + 6;                               // drawQRPanel's text column
  uint32_t secs = (auth::loginTokenMsLeft() + 999) / 1000;
  g_shownSecs = secs;
  char b[24];
  snprintf(b, sizeof(b), "new code %lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
  d.fillRect(x, 94, panelW(x), g_line, BG);           // proportional digits: clear, don't overprint
  d.setTextColor(DIM, BG); d.setCursor(x, 94); d.print(b);
}

// A phone that just joined our AP (step 1) can only reach the AP address, even
// when "keep AP on" means we also have a home-network address.
String loginLink() {
  return "http://" + (g_viaAp ? net::apIp() : net::ip()) + "/login?k=" + auth::loginToken();
}

void drawLoginQR() {
  auto &d = M5.Display;
  g_shownLink = loginLink();
  int x = drawQRPanel(g_shownLink, g_viaAp ? "step 2 of 2" : "scan to", "sign in");
  d.setTextColor(FG, BG);  d.setCursor(x, 40); d.print("signed in,");
  d.setCursor(x, 56);      d.print("no password");
  d.setTextColor(WARN, BG); d.setCursor(x, 74); d.print("works once");
  drawLoginCountdown();
  d.setTextColor(DIM, BG); d.setCursor(x, footY()); d.print("btn: back");
}

void drawInfo() {
  auto &d = M5.Display;
  d.fillScreen(BG);
  d.setTextSize(1);

  d.fillRect(0, 0, d.width(), g_headH, 0x0000);
  d.setTextColor(ACC, 0x0000);
  d.setCursor(6, 1);
  d.print(WUI_NAME);
  d.setTextColor(DIM, 0x0000);
  d.setCursor(d.width() - 6 - d.textWidth("v" WUI_FW_VERSION), 1);
  d.print("v" WUI_FW_VERSION);

  // Rows become lines first: a value too wide to sit beside its label goes
  // on wrapped lines of its own below it. Then show the lines that fit.
  struct Line { const char *k; String v; uint16_t col; };
  std::vector<Line> lines;
  int vx = 6 + d.textWidth("password") + 8;             // value column
  auto row = [&](const char *k, const String &v, uint16_t col) {
    if (vx + d.textWidth(v) <= d.width() - 4) { lines.push_back({k, v, col}); return; }
    lines.push_back({k, "", col});
    for (const String &part : wrap(v, d.width() - 18)) lines.push_back({"", part, col});
  };
  row(net::isAP() ? "ap" : "wifi", net::ssid(), FG);
  row("url", url(), ACC);
  if (net::apUp()) row("ap key", net::apPassword(), FG);
  if (usb::hostConnected()) row("usb", "http://" + usb::ip() + "/", ACC);
  if (storage::mounted()) {
    uint64_t t = storage::totalBytes(), u = storage::usedBytes();
    char b[40];
    snprintf(b, sizeof(b), "%llu / %llu MB", u / 1048576ULL, t / 1048576ULL);
    row("sd", b, FG);
  } else {
    row("sd", "not mounted", WARN);
  }
  int bat = ui::batteryLevel();
  if (bat >= 0) row("battery", String(bat) + "%" + (ui::charging() ? " charging" : ""), bat < 15 ? WARN : FG);
  if (auth::isFreshPassword()) row("password", auth::freshPassword(), WARN);
  else                         row("password", "(set by you)", DIM);

  int top = g_headH + 3;
  int fits = std::max(1, (footY() - top) / g_line);
  int over = std::max(0, (int)lines.size() - fits);
  g_scroll = std::min(g_scroll, over);
  for (int i = 0, y = top; i < fits && g_scroll + i < (int)lines.size(); ++i, y += g_line) {
    const Line &l = lines[g_scroll + i];
    d.setTextColor(DIM, BG);   d.setCursor(6, y); d.print(l.k);
    d.setTextColor(l.col, BG); d.setCursor(*l.k ? vx : 14, y); d.print(l.v);
  }
  // Size keys lead the hint so they survive being cut at the larger sizes.
  drawFooter(over ? "+/- size  ;/. scroll  btn: QR" : "+/- size  btn: QR  w: wi-fi");
}

void drawHeader(const String &title) {
  auto &d = M5.Display;
  d.fillScreen(BG);
  d.fillRect(0, 0, d.width(), g_headH, 0x0000);
  d.setTextColor(ACC, 0x0000);
  d.setCursor(6, 1);
  d.print(fit(title, d.width() - 12));
}

void drawFooter(const char *hint) {
  auto &d = M5.Display;
  d.setTextColor(DIM, BG);
  d.setCursor(6, footY());
  d.print(fit(hint, d.width() - 12));
}

void drawWifiPick() {
  auto &d = M5.Display;
  drawHeader("join wi-fi");
  int top = g_headH + 4, pitch = g_line + 3;
  int rows = std::max(1, (footY() - top) / pitch);
  if (g_nets.empty()) {
    d.setTextColor(FG, BG); d.setCursor(6, top); d.print("no networks found");
  }
  int first = std::max(0, std::min(g_sel - rows / 2, (int)g_nets.size() - rows));
  for (int i = first, y = top; i < (int)g_nets.size() && i < first + rows; ++i, y += pitch) {
    const auto &n = g_nets[i];
    bool sel = i == g_sel;
    uint16_t bg = sel ? 0x2124 : BG;
    if (sel) d.fillRect(0, y - 1, d.width(), g_line + 2, bg);
    char r[16];
    snprintf(r, sizeof(r), "%s%d", n.saved ? "saved " : (n.secure ? "" : "open "), n.rssi);
    int rx = d.width() - 6 - d.textWidth(r);
    d.setTextColor(sel ? ACC : FG, bg); d.setCursor(6, y); d.print(fit(n.ssid, rx - 12));
    d.setTextColor(DIM, bg); d.setCursor(rx, y); d.print(r);
  }
  drawFooter(";/. move  ok join  r scan  ` back");
}

void drawWifiPass() {
  auto &d = M5.Display;
  drawHeader("join " + g_nets[g_sel].ssid);
  int y = g_headH + 8;
  d.setTextColor(DIM, BG); d.setCursor(6, y); d.print("password");
  String shown;
  if (g_showPass) shown = g_pass;
  else for (size_t i = 0; i < g_pass.length(); ++i) shown += '*';
  shown += "_";
  while (d.textWidth(shown) > d.width() - 16) shown.remove(0, 1);   // keep the typing end visible
  y += g_line + 2;
  d.drawRect(4, y, d.width() - 8, g_line + 6, DIM);
  d.setTextColor(FG, BG); d.setCursor(8, y + 3); d.print(shown);
  drawFooter("ok: join  tab: show  fn+`: back");
}

void drawJoining(const String &ssid) {
  auto &d = M5.Display;
  drawHeader("join wi-fi");
  d.setTextColor(FG, BG);
  int y = drawWrapped("joining " + ssid + "...", 6, g_headH + 8, d.width() - 12);
  d.setTextColor(DIM, BG);
  drawWrapped("this can take a few seconds", 6, y + 4, d.width() - 12);
}

void drawDrive() {
  auto &d = M5.Display;
  d.fillScreen(BG);
  d.setTextDatum(textdatum_t::top_center);
  int scale = g_size ? 1 : 2;                           // Font2 doubled ~ the larger fonts
  auto body = wrap("SD card is on your computer. Eject it there.", d.width() - 12);
  int y = (d.height() - g_line * (scale + (int)body.size()) - 8) / 2;
  d.setTextSize(scale);
  d.setTextColor(ACC, BG);
  d.drawString("USB DRIVE", d.width() / 2, y);
  d.setTextSize(1);
  d.setTextColor(FG, BG);
  y += g_line * scale + 8;
  for (const String &l : body) { d.drawString(l, d.width() / 2, y); y += g_line; }
  d.setTextDatum(textdatum_t::top_left);
}

void drawCurrent() {
  g_driveShown = usb::driveMode();
  useFont(g_size);                                      // QR screens drop back to the first
  if (g_driveShown) { drawDrive(); return; }
  if (g_screen != LOGIN_QR) auth::disarmLoginToken();   // no valid code off-screen
  switch (g_screen) {
    case INFO:     drawInfo();    break;
    case WIFI_QR:  drawWifiQR();  break;
    case LOGIN_QR: drawLoginQR(); break;
    case WIFI_PICK: drawWifiPick(); break;
    case WIFI_PASS: drawWifiPass(); break;
  }
}

// True when the press only woke a dimmed screen (and should do nothing else).
bool wake() {
  g_lastInput = millis();
  if (!g_dimmed) return false;
  M5.Display.setBrightness(90);
  g_dimmed = false;
  return true;
}

void leaveWifi() {
  g_pass = "";
  g_showPass = false;
  g_screen = INFO;
}

void openWifiPick() {
  auto &d = M5.Display;
  drawHeader("join wi-fi");
  d.setTextColor(FG, BG); d.setCursor(6, 40); d.print("scanning...");
  String err;
  if (!net::scan(g_nets, err)) { leaveWifi(); drawCurrent(); ui::toast(err.c_str()); return; }
  g_sel = 0;
  g_screen = WIFI_PICK;
  drawCurrent();
}

void joinSelected() {
  String ssid = g_nets[g_sel].ssid, err;
  drawJoining(ssid);
  bool ok = net::join(ssid, g_pass, err);   // blocking; saves the network on success
  leaveWifi();
  drawCurrent();
  ui::toast(ok ? ("joined " + ssid).c_str() : err.c_str());
}

// A key went down. Plain ; . ` work as arrows/back in the list; in the
// password field every printable key is text, so only fn+` goes back.
void onKey(const Keyboard_Class::KeysState &k) {
  char c = k.word.empty() ? 0 : k.word.back();
  int n = (int)g_nets.size();
  switch (g_screen) {
    case INFO:
      if (c == 'w' || c == 'W') openWifiPick();
      else if (k.up || c == ';')   { if (g_scroll) { --g_scroll; drawCurrent(); } }
      else if (k.down || c == '.') { ++g_scroll; drawCurrent(); }   // drawInfo clamps it
      else if (c == '=' || c == '+' || c == '-' || c == '_') {
        int s = std::max(0, std::min(N_SIZES - 1, g_size + ((c == '-' || c == '_') ? -1 : 1)));
        if (s != g_size) {
          g_size = s;
          g_scroll = 0;
          Preferences p;
          p.begin("wui-ui", false); p.putUChar("size", s); p.end();
          drawCurrent();
        }
      }
      break;
    case WIFI_PICK:
      if (k.esc || c == '`' || k.backspace) { leaveWifi(); drawCurrent(); }
      else if (c == 'r' || c == 'R') openWifiPick();
      else if (n && (k.up || c == ';'))   { g_sel = (g_sel + n - 1) % n; drawCurrent(); }
      else if (n && (k.down || c == '.')) { g_sel = (g_sel + 1) % n; drawCurrent(); }
      else if (n && k.enter) {
        g_pass = "";
        if (g_nets[g_sel].secure) { g_screen = WIFI_PASS; drawCurrent(); }
        else joinSelected();
      }
      break;
    case WIFI_PASS:
      if (k.esc) { g_pass = ""; g_showPass = false; g_screen = WIFI_PICK; drawCurrent(); }
      else if (k.enter) joinSelected();
      else if (k.tab) { g_showPass = !g_showPass; drawCurrent(); }
      else if (k.backspace) { if (g_pass.length()) g_pass.remove(g_pass.length() - 1); drawCurrent(); }
      else if (!k.fn && !k.ctrl && !k.alt && !k.opt && !k.word.empty()) {
        for (char ch : k.word)
          if (ch >= 32 && ch < 127 && g_pass.length() < 63) g_pass += ch;   // WPA2 keys: 8-63 chars
        drawCurrent();
      }
      break;
    default:
      break;
  }
}

}  // namespace

namespace ui {

void begin() {
  auto &d = M5.Display;
  d.setRotation(1);
  d.setBrightness(90);
  Preferences p;
  p.begin("wui-ui", true);
  g_size = std::min<int>(N_SIZES - 1, p.getUChar("size", 0));
  p.end();
  useFont(g_size);
  d.fillScreen(BG);
  g_lastInput = millis();
}

int batteryLevel() {
  int v = M5.Power.getBatteryLevel();
  return (v < 0 || v > 100) ? -1 : v;
}
int batteryMv() { return M5.Power.getBatteryVoltage(); }
bool charging() { return M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging; }

void draw() { drawCurrent(); }

void toast(const char *line) {
  g_toast = line;
  g_toastUntil = millis() + 2500;
  auto &d = M5.Display;
  d.fillRect(0, footY() - 2, d.width(), d.height() - footY() + 2, 0x0000);
  d.setTextColor(ACC, 0x0000);
  d.setCursor(6, footY());
  d.print(fit(line, d.width() - 12));
}

void tick() {
  M5Cardputer.update();

  // Act on key-downs only, and only on characters not already held: with the
  // usual isChange() pattern, releasing one of two rolled-over keys would
  // "type" the other again.
  static size_t prevHeld = 0;
  static std::vector<char> prevWord;
  auto &kb = M5Cardputer.Keyboard;
  if (kb.isChange()) {
    const auto &ks = kb.keysState();
    size_t held = kb.isPressed();
    if (held > prevHeld && !wake() && !g_driveShown) {
      Keyboard_Class::KeysState k = ks;
      k.word.clear();
      for (char ch : ks.word)
        if (std::find(prevWord.begin(), prevWord.end(), ch) == prevWord.end()) k.word.push_back(ch);
      onKey(k);
    }
    prevHeld = held;
    prevWord = ks.word;
  }

  if (M5.BtnA.wasPressed() && !wake()) {
    switch (g_screen) {
      case INFO:     g_screen = net::apUp() ? WIFI_QR : (WUI_QR_LOGIN ? LOGIN_QR : INFO); g_viaAp = false; break;
      case WIFI_QR:  g_screen = WUI_QR_LOGIN ? LOGIN_QR : INFO; g_viaAp = true; break;
      case LOGIN_QR: g_screen = INFO; break;
      default:       leaveWifi(); break;
    }
    draw();
    g_next = millis() + 2000;
  }

  if (M5.BtnA.isPressed()) {
    if (!g_holdStart) g_holdStart = millis();
    uint32_t held = millis() - g_holdStart;
    if (held > 1200) {
      char b[40];
      snprintf(b, sizeof(b), "hold to reset: %lus", (unsigned long)(5 - std::min<uint32_t>(5, held / 1000)));
      toast(b);
      if (held > 5000) factoryReset();
    }
  } else {
    g_holdStart = 0;
  }

  uint32_t idle = millis() - g_lastInput;
  if (usb::driveMode() != g_driveShown) { wake(); draw(); }
  if (g_screen != INFO && idle > 5 * 60000UL) { leaveWifi(); draw(); }   // no QR or half-typed key left up
  if (!g_dimmed && g_screen == INFO && idle > WUI_DIM_AFTER_MS) { M5.Display.setBrightness(12); g_dimmed = true; }

  if (g_toastUntil && millis() > g_toastUntil) { g_toastUntil = 0; draw(); }
  if (g_driveShown) return;
  if (g_screen == LOGIN_QR && !g_toastUntil) {
    // Code used or expired (loginToken() re-arms, so the link changes) or the IP
    // moved: redraw. Otherwise only tick the countdown, since repainting the
    // whole QR every few seconds makes it flicker under a camera.
    if (loginLink() != g_shownLink) {
      drawLoginQR();
    } else if ((auth::loginTokenMsLeft() + 999) / 1000 != g_shownSecs) {
      drawLoginCountdown();
    }
    return;
  }
  if (g_screen == WIFI_PICK || g_screen == WIFI_PASS) return;   // redrawn on key presses
  if (g_screen == WIFI_QR) {                 // static unless the AP went away
    if (!net::apUp()) { g_screen = INFO; draw(); }
    return;
  }
  if (millis() > g_next && !g_toastUntil) { g_next = millis() + 5000; draw(); }
}

}  // namespace ui
