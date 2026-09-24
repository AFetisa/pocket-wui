#include "jobs.h"
#include "config.h"
#include "firmware.h"
#include "http_util.h"
#include "storage.h"
#include "threads.h"
#include <SD.h>
#include <atomic>
#include <mbedtls/sha256.h>
#include <mutex>

namespace {

std::mutex g_mu;                 // guards the strings below
String g_kind = "none", g_state = "idle", g_label, g_result, g_error;
std::atomic<uint64_t> g_done{0}, g_total{0};
std::atomic<bool> g_busy{false}, g_cancel{false};

void finish(bool ok, const String &result, const String &err) {
  std::lock_guard<std::mutex> l(g_mu);
  g_state = ok ? "done" : "error";
  g_result = result;
  g_error = err;
  g_busy = false;
}

bool begin(const char *kind, const String &label, String &err) {
  bool expected = false;
  if (!g_busy.compare_exchange_strong(expected, true)) { err = "another job is still running"; return false; }
  std::lock_guard<std::mutex> l(g_mu);
  g_kind = kind;
  g_state = "running";
  g_label = label;
  g_result = g_error = "";
  g_done = g_total = 0;
  g_cancel = false;
  return true;
}

bool progress(size_t n) {
  g_done += n;
  return !g_cancel;
}

bool run(const char *name, std::function<void(uint8_t *)> body) {
  return wui_spawn(name, 8192, [body]() {
    uint8_t *buf = (uint8_t *)malloc(WUI_JOB_BUF);
    if (!buf) { finish(false, "", "out of memory"); return; }
    body(buf);
    free(buf);
  });
}

}  // namespace

namespace jobs {

bool busy() { return g_busy; }
void cancel() { g_cancel = true; }

bool startCopy(const std::vector<CopyItem> &items, String &err) {
  if (items.empty()) { err = "nothing to copy"; return false; }
  String label = items.size() == 1 ? items[0].from.substring(items[0].from.lastIndexOf('/') + 1)
                                   : String((unsigned)items.size()) + " items";
  if (!begin("copy", label, err)) return false;
  bool ok = run("wui-copy", [items](uint8_t *buf) {
    uint64_t total = 0;
    for (const auto &it : items) total += storage::treeBytes(it.from);
    g_total = total;
    String e;
    for (const auto &it : items) {
      if (!storage::copyTree(it.from, it.to, buf, WUI_JOB_BUF, progress, e)) { finish(false, "", e); return; }
    }
    finish(true, "copied", "");
  });
  if (!ok) { finish(false, "", "cannot start a thread"); err = "cannot start a thread"; }
  return ok;
}

bool startHash(const String &path, String &err) {
  if (!begin("hash", path.substring(path.lastIndexOf('/') + 1), err)) return false;
  bool ok = run("wui-hash", [path](uint8_t *buf) {
    File f;
    {
      storage::Guard g;
      f = SD.open(path, FILE_READ);
      if (f && f.isDirectory()) { f.close(); f = File(); }
      if (f) g_total = f.size();
    }
    if (!f) { finish(false, "", "cannot read " + path); return; }
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    bool cancelled = false;
    while (true) {
      int got;
      { storage::Guard g; got = f.read(buf, WUI_JOB_BUF); }
      if (got <= 0) break;
      mbedtls_sha256_update(&ctx, buf, got);
      if (!progress(got)) { cancelled = true; break; }
      delay(0);
    }
    uint8_t out[32];
    mbedtls_sha256_finish(&ctx, out);
    mbedtls_sha256_free(&ctx);
    { storage::Guard g; f.close(); }
    if (cancelled) { finish(false, "", "cancelled"); return; }
    char hex[65];
    for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", out[i]);
    finish(true, hex, "");
  });
  if (!ok) { finish(false, "", "cannot start a thread"); err = "cannot start a thread"; }
  return ok;
}

bool startFirmware(const String &path, String &err) {
  if (!fw::canSelfUpdate()) {
    err = fw::underLauncher() ? "running under M5Launcher: install the file from Launcher's SD menu"
                              : "this build has no second app slot to update into";
    return false;
  }
  if (!begin("firmware", path.substring(path.lastIndexOf('/') + 1), err)) return false;
  bool ok = run("wui-fw", [path](uint8_t *buf) {
    String e;
    if (!fw::flashFromFile(path, buf, WUI_JOB_BUF, [](size_t n, uint64_t total) {
          g_total = total;
          return progress(n);
        }, e)) {
      finish(false, "", e);
      return;
    }
    finish(true, "installed — restarting", "");
    delay(1500);
    fw::restart();
  });
  if (!ok) { finish(false, "", "cannot start a thread"); err = "cannot start a thread"; }
  return ok;
}

String statusJson() {
  std::lock_guard<std::mutex> l(g_mu);
  return "{\"kind\":\"" + g_kind + "\",\"state\":\"" + g_state + "\",\"done\":" +
         String((unsigned long long)g_done.load()) + ",\"total\":" + String((unsigned long long)g_total.load()) +
         ",\"label\":\"" + http::jsonEscape(g_label) + "\",\"result\":\"" + http::jsonEscape(g_result) +
         "\",\"error\":\"" + http::jsonEscape(g_error) + "\"}";
}

}  // namespace jobs
