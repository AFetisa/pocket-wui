#include "ui.h"
#include "config.h"
#include "auth.h"
#include "net.h"
#include "storage.h"
#include <M5Unified.h>
#include <Preferences.h>

namespace {

constexpr uint16_t BG    = 0x0861;   // near-black
constexpr uint16_t FG    = 0xCE79;   // pale mint
constexpr uint16_t DIM   = 0x6B2D;
constexpr uint16_t ACC   = 0x27E6;   // green
constexpr uint16_t WARN  = 0xFCC0;

bool     g_qr = false;
uint32_t g_next = 0;
uint32_t g_holdStart = 0;
String   g_toast;
uint32_t g_toastUntil = 0;

String url() { return "http://" + net::ip() + "/"; }

void factoryReset() {
  Preferences p;
  p.begin("wui-auth", false); p.clear(); p.end();
  p.begin("wui-net", false);  p.clear(); p.end();
  M5.Display.fillScreen(BG);
  M5.Display.setTextColor(WARN, BG);
  M5.Display.setCursor(8, 56);
  M5.Display.print("reset - rebooting");
  delay(900);
  ESP.restart();
}

void drawQR() {
  auto &d = M5.Display;
  d.fillScreen(BG);
  int side = d.height() - 24;
  d.qrcode(url(), (d.width() - side) / 2, 4, side, 3);
  d.setTextColor(DIM, BG);
  d.setTextDatum(textdatum_t::top_center);
  d.drawString(url(), d.width() / 2, d.height() - 18);
  d.setTextDatum(textdatum_t::top_left);
}

void drawInfo() {
  auto &d = M5.Display;
  d.fillScreen(BG);
  d.setTextSize(1);

  d.fillRect(0, 0, d.width(), 16, 0x0000);
  d.setTextColor(ACC, 0x0000);
  d.setCursor(6, 5);
  d.print("CARDPUTER . WUI");
  d.setTextColor(DIM, 0x0000);
  d.setCursor(d.width() - 52, 5);
  d.print("v" WUI_FW_VERSION);

  int y = 24;
  auto row = [&](const char *k, const String &v, uint16_t col) {
    d.setTextColor(DIM, BG); d.setCursor(6, y);  d.print(k);
    d.setTextColor(col, BG); d.setCursor(64, y); d.print(v);
    y += 14;
  };
  row(net::isAP() ? "ap" : "wifi", net::ssid(), FG);
  row("url", url(), ACC);
  if (net::isAP()) row("ap key", net::apPassword(), FG);
  if (storage::mounted()) {
    uint64_t t = storage::totalBytes(), u = storage::usedBytes();
    char b[40];
    snprintf(b, sizeof(b), "%llu / %llu MB", u / 1048576ULL, t / 1048576ULL);
    row("sd", b, FG);
  } else {
    row("sd", "not mounted", WARN);
  }
  if (auth::isFreshPassword()) row("password", auth::freshPassword(), WARN);
  else                         row("password", "(set by you)", DIM);

  d.setTextColor(DIM, BG);
  d.setCursor(6, d.height() - 12);
  d.print(auth::isFreshPassword() ? "btn: QR  |  hold 5s: reset"
                                  : "btn: QR code");
}

}  // namespace

namespace ui {

void begin() {
  auto &d = M5.Display;
  d.setRotation(1);
  d.setBrightness(90);
  d.setTextFont(&fonts::Font0);
  d.fillScreen(BG);
}

void draw() { g_qr ? drawQR() : drawInfo(); }

void toast(const char *line) {
  g_toast = line;
  g_toastUntil = millis() + 2500;
  auto &d = M5.Display;
  d.fillRect(0, d.height() - 14, d.width(), 14, 0x0000);
  d.setTextColor(ACC, 0x0000);
  d.setCursor(6, d.height() - 11);
  d.print(line);
}

void tick() {
  M5.update();

  if (M5.BtnA.wasPressed()) { g_qr = !g_qr; draw(); g_next = millis() + 2000; }

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

  if (g_toastUntil && millis() > g_toastUntil) { g_toastUntil = 0; draw(); }
  if (millis() > g_next && !g_toastUntil) { g_next = millis() + 5000; draw(); }
}

}  // namespace ui
