// ---------------------------------------------------------------------------
// PocketWUI — the Cardputer's SD card in any browser: files, previews, search,
// ZIP downloads, WebDAV, and a USB cable that works as a network adapter.
//
//   http://cardputer.local/   (or the IP shown on the screen)
// ---------------------------------------------------------------------------
#include <M5Unified.h>
#include "config.h"
#include "auth.h"
#include "net.h"
#include "server.h"
#include "storage.h"
#include "ui.h"
#include "usb.h"

void setup() {
  auto cfg = M5.config();
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  M5.begin(cfg);                       // auto-detects Cardputer / Cardputer ADV
  Serial.begin(115200);

  ui::begin();
  auth::begin();

  if (!storage::begin())
    log_w("continuing without an SD card — insert one and it is picked up");

  usb::begin();                        // after the card, so the USB drive can offer it
  net::begin();

  if (!server::begin()) {
    M5.Display.fillScreen(0);
    M5.Display.setCursor(6, 40);
    M5.Display.print("http server failed");
  }

  ui::draw();
  Serial.printf("\n%s v%s ready on http://%s/\n", WUI_NAME, WUI_FW_VERSION, net::ip().c_str());
  if (auth::isFreshPassword())
    Serial.printf("first-boot password: %s  (change it in Settings)\n", auth::freshPassword().c_str());
}

void loop() {
  ui::tick();
  net::tick();
  usb::tick();
  storage::tickUploadIdle();
  delay(20);
}
