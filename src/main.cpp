// ---------------------------------------------------------------------------
// PocketWUI — the Cardputer's SD card in any browser: files, previews, search,
// ZIP downloads, WebDAV, and a USB cable that works as a network adapter.
//
//   http://cardputer.local/   (or the IP shown on the screen)
// ---------------------------------------------------------------------------
#include <M5Cardputer.h>
#include "config.h"
#include "auth.h"
#include "net.h"
#include "server.h"
#include "storage.h"
#include "ui.h"
#include "usb.h"
#include <esp_ota_ops.h>

// A freshly installed update stays "pending verify" until this build proves it
// can bring the web server up; if it crashes before that, the bootloader rolls
// back to the previous version. (Arduino would otherwise confirm it before setup().)
extern "C" bool verifyRollbackLater() { return true; }

void setup() {
  auto cfg = M5.config();
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  M5Cardputer.begin(cfg, true);        // M5.begin (auto-detects Cardputer / ADV) + keyboard

  ui::begin();
  auth::begin();

  if (!storage::begin())
    log_w("continuing without an SD card — insert one and it is picked up");

  usb::begin();                        // after the card, so the USB drive can offer it
  ui::draw();
  ui::toast("connecting to Wi-Fi...");  // joining can take a few seconds per network
  net::begin();

  if (!server::begin()) {
    M5.Display.fillScreen(0);
    M5.Display.setCursor(6, 40);
    M5.Display.print("http server failed");
  } else {
    esp_ota_mark_app_valid_cancel_rollback();
  }

  ui::draw();
  // No USB serial console in this build (the port is the network adapter), so
  // everything a user needs — address, first-boot password — is on the screen.
  log_i("%s v%s ready on http://%s/", WUI_NAME, WUI_FW_VERSION, net::ip().c_str());
}

void loop() {
  ui::tick();
  net::tick();
  usb::tick();
  storage::tickUploadIdle();
  delay(20);
}
