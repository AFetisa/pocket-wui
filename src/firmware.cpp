#include "firmware.h"
#include "fwimage.h"
#include "storage.h"
#include <SD.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_sleep.h>

namespace fw {

bool underLauncher() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_TEST, nullptr) != nullptr;
}

bool canSelfUpdate() {
  return !underLauncher() && esp_ota_get_next_update_partition(nullptr) != nullptr;
}

String runningSlot() {
  const esp_partition_t *p = esp_ota_get_running_partition();
  return p ? String(p->label) : String("?");
}

bool flashFromFile(const String &path, uint8_t *buf, size_t bufLen, const Progress &progress, String &err) {
  if (!canSelfUpdate()) { err = "no update slot"; return false; }
  File f;
  uint64_t size = 0;
  {
    storage::Guard g;
    f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory()) { err = "cannot read " + path; return false; }
    size = f.size();
    size_t n = f.read(buf, std::min<size_t>(bufLen, WUI_FW_HEAD));
    FwInfo info;
    if (!wui_check_app_image(buf, n, info, err)) { f.close(); return false; }
    f.seek(0);
  }
  const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
  if (size > slot->size) {
    storage::Guard g;
    f.close();
    err = "firmware is larger than the update slot";
    return false;
  }
  if (!Update.begin(size, U_FLASH)) {
    storage::Guard g;
    f.close();
    err = String("cannot start update: ") + Update.errorString();
    return false;
  }
  bool ok = true;
  while (ok) {
    int got;
    { storage::Guard g; got = f.read(buf, bufLen); }
    if (got <= 0) break;
    if (Update.write(buf, got) != (size_t)got) { err = String("write failed: ") + Update.errorString(); ok = false; }
    else if (progress && !progress(got, size)) { err = "cancelled"; ok = false; }
    delay(1);
  }
  { storage::Guard g; f.close(); }
  if (ok && Update.progress() != size) { err = "could not read the whole file from the card"; ok = false; }
  if (!ok) { Update.abort(); return false; }
  if (!Update.end(true)) { err = String("verify failed: ") + Update.errorString(); return false; }
  return true;
}

void rebootToLauncher() {
  storage::closeUpload(true);
  // M5Launcher takes over on a wake from deep sleep ("DeepSleep starts
  // Launcher", on by default), then offers its menu instead of this app.
  esp_sleep_enable_timer_wakeup(200 * 1000);
  esp_deep_sleep_start();
}

void restart() {
  storage::closeUpload(true);
  ESP.restart();
}

}  // namespace fw
