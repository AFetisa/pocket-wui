#include "storage.h"
#include "config.h"
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <mutex>
#include <vector>

namespace {

std::mutex g_mutex;             // one SD transaction at a time, across tasks
bool     g_mounted = false;
uint32_t g_hz = 0;
int8_t   g_sck = -1, g_mosi = -1, g_miso = -1, g_cs = -1;

// A mount only proves the card answered its init sequence; data transfers at
// the same clock can still corrupt. Round-trip one block before trusting it.
bool probeCard() {
  static const char *kProbe = "/.wui_probe";
  uint8_t out[512], in[512];
  for (int i = 0; i < 512; ++i) out[i] = (uint8_t)(i * 7 + 3);
  File f = SD.open(kProbe, FILE_WRITE);
  if (!f) return false;
  bool ok = f.write(out, sizeof(out)) == sizeof(out);
  f.close();
  if (ok) {
    f = SD.open(kProbe, FILE_READ);
    ok = f && f.read(in, sizeof(in)) == (int)sizeof(in) && memcmp(in, out, sizeof(in)) == 0;
    if (f) f.close();
  }
  SD.remove(kProbe);
  return ok;
}

volatile bool g_lent = false;   // SD card handed to the USB host

File     g_up;                  // cached upload handle
String   g_upPath;
uint64_t g_upSize = 0;          // bytes accepted on this handle — see uploadHandle()
uint32_t g_upTouched = 0;

}  // namespace

namespace storage {

void lock()   { g_mutex.lock(); }
void unlock() { g_mutex.unlock(); }

bool begin() {
  // M5Unified knows the SD pin map for the board it detected (Cardputer ADV
  // reports SCK 40 / MOSI 14 / MISO 39 / CS 12); never guess these.
  g_sck  = M5.getPin(m5::pin_name_t::sd_spi_sclk);
  g_mosi = M5.getPin(m5::pin_name_t::sd_spi_mosi);
  g_miso = M5.getPin(m5::pin_name_t::sd_spi_miso);
  g_cs   = M5.getPin(m5::pin_name_t::sd_spi_ss);
  if (g_sck < 0 || g_mosi < 0 || g_miso < 0 || g_cs < 0) {
    log_e("no SD pin map for this board");
    return false;
  }
  // On the ADV the SD card shares this SPI bus with the EXT header, whose
  // peripheral chip-select is G5 (the LoRa cap, for one). Floating low, that
  // device answers on MISO too and corrupts SD traffic. Every firmware proven
  // on this hardware deselects it first. (On the v1.1, G5 is a keyboard line —
  // leave it alone there.)
  if (M5.getBoard() == m5::board_t::board_M5CardputerADV) {
    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH);
    delay(50);
  }
  SPI.end();
  SPI.begin(g_sck, g_miso, g_mosi, g_cs);
  Guard g;
  // Fastest clock that survives a write/read-back wins. 4 MHz is the floor the
  // sibling firmwares run on this unit, so it stays mounted even if its probe
  // fails (a full or write-protected card still browses). SDFS::begin() returns
  // true at once if a card is already attached, so each try starts from end().
  static const uint32_t kHz[] = {WUI_SD_HZ_LADDER};
  const size_t n = sizeof(kHz) / sizeof(kHz[0]);
  for (size_t i = 0; i < n; ++i) {
    SD.end();
    g_mounted = false;
    if (!SD.begin(g_cs, SPI, kHz[i]) || SD.cardType() == CARD_NONE) continue;
    g_mounted = true;
    g_hz = kHz[i];
    if (probeCard()) break;
    log_w("SD at %lu Hz failed its write probe", (unsigned long)kHz[i]);
  }
  if (g_mounted) log_i("SD mounted at %lu Hz: %llu bytes", (unsigned long)g_hz, SD.totalBytes());
  else           log_w("SD mount failed (card inserted?)");
  return g_mounted;
}

uint32_t clockHz() { return g_mounted ? g_hz : 0; }

bool mounted() { return g_mounted; }

bool remount() {
  closeUpload(true);
  { Guard g; SD.end(); g_mounted = false; }
  return begin();
}

// SD.usedBytes() walks the allocation table and can take seconds on a large
// card, so the pair is sampled at most twice a minute and served from cache.
static uint64_t g_total = 0, g_used = 0;
static uint32_t g_usageAt = 0;

static void sampleUsage(bool force) {
  if (!g_mounted) { g_total = g_used = 0; return; }
  if (!force && g_usageAt && (millis() - g_usageAt) < 30000) return;
  Guard g;
  g_total = SD.totalBytes();
  g_used  = SD.usedBytes();
  g_usageAt = millis();
  if (!g_usageAt) g_usageAt = 1;
}

uint64_t totalBytes() { sampleUsage(false); return g_total; }
uint64_t usedBytes()  { sampleUsage(false); return g_used; }
void     invalidateUsage() { g_usageAt = 0; }

const char *cardType() {
  switch (SD.cardType()) {
    case CARD_MMC:  return "MMC";
    case CARD_SD:   return "SDSC";
    case CARD_SDHC: return "SDHC";
    case CARD_NONE: return "none";
    default:        return "unknown";
  }
}

File *uploadHandle(const String &path, uint64_t offset, uint64_t *actual, String &err) {
  if (g_up && g_upPath != path) closeUpload(true);

  if (g_up && offset == 0) closeUpload(true);   // a restarted upload truncates

  if (!g_up) {
    if (offset == 0) {
      File probe = SD.open(path, FILE_WRITE);       // truncates
      if (!probe) { err = "cannot create " + path; return nullptr; }
      g_up = probe;
      g_upSize = 0;
    } else {
      // Nothing open: the file on the card is settled, so its own size is the
      // truth to resume from.
      uint64_t have = 0;
      { File probe = SD.open(path, FILE_READ); if (probe) { have = probe.size(); probe.close(); } }
      if (have != offset) { *actual = have; err = "offset mismatch"; return nullptr; }
      g_up = SD.open(path, FILE_APPEND);
      if (!g_up) { err = "cannot append to " + path; return nullptr; }
      g_upSize = have;
    }
    g_upPath = path;
  } else if (g_upSize != offset) {
    // Deliberately NOT g_up.size(): that re-stats the path, and FATFS only
    // updates a file's directory entry on flush, so mid-upload it reports the
    // last flushed size. Answering a client resync with that stale number sends
    // it backwards, it re-sends bytes the handle already holds, and the file
    // ends up longer than the source. Count what we accepted instead.
    *actual = g_upSize;
    err = "offset mismatch";
    return nullptr;
  }
  g_upTouched = millis();
  return &g_up;
}

void uploadWrote(size_t n) { g_upSize += n; g_upTouched = millis(); }

void closeUpload(bool force) {
  (void)force;
  if (g_up) { g_up.flush(); g_up.close(); }
  g_upPath = "";
  g_upSize = 0;
}

void tickUploadIdle() {
  if (g_up && (millis() - g_upTouched) > WUI_IDLE_FILE_MS) {
    Guard g;
    closeUpload(true);
  }
}

bool listDir(const String &path, std::vector<DirEntry> &out) {
  out.clear();
  File d = SD.open(path);
  if (!d) return false;
  if (!d.isDirectory()) { d.close(); return false; }
  while (true) {
    File f = d.openNextFile();
    if (!f) break;
    String name = f.name();
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    out.push_back({name, f.isDirectory(), f.isDirectory() ? 0 : (uint64_t)f.size(), f.getLastWrite()});
    f.close();
  }
  d.close();
  return true;
}

static String childPath(const String &dir, const String &name) {
  return (dir.endsWith("/") ? dir : dir + "/") + name;
}

bool removeRecursive(const String &path, String &err) {
  if (path == "/") { err = "refusing to delete the card root"; return false; }
  File f = SD.open(path);
  if (!f) { err = "no such path: " + path; return false; }
  bool isDir = f.isDirectory();
  f.close();
  if (!isDir) {
    if (SD.remove(path)) { invalidateUsage(); return true; }
    err = "cannot delete " + path;
    return false;
  }
  std::vector<DirEntry> kids;
  listDir(path, kids);
  for (const auto &k : kids) {
    String full = childPath(path, k.name);
    if (k.dir) { if (!removeRecursive(full, err)) return false; }
    else if (!SD.remove(full)) { err = "cannot delete " + full; return false; }
  }
  if (SD.rmdir(path)) { invalidateUsage(); return true; }
  err = "cannot remove directory " + path;
  return false;
}

bool moveToTrash(const String &path, String &err) {
  if (path == "/" || path == WUI_TRASH_DIR) { err = "refusing to trash " + path; return false; }
  if (path.startsWith(String(WUI_TRASH_DIR) + "/")) return removeRecursive(path, err);
  if (!SD.exists(path)) { err = "no such path: " + path; return false; }
  if (!SD.exists(WUI_TRASH_DIR) && !SD.mkdir(WUI_TRASH_DIR)) { err = "cannot create " WUI_TRASH_DIR; return false; }
  String name = path.substring(path.lastIndexOf('/') + 1);
  String dest = childPath(WUI_TRASH_DIR, name);
  for (int n = 2; SD.exists(dest) && n < 1000; ++n) {
    // "photo.jpg" -> "photo (2).jpg", keeping the extension usable.
    int dot = name.lastIndexOf('.');
    String stem = dot > 0 ? name.substring(0, dot) : name, ext = dot > 0 ? name.substring(dot) : String();
    dest = childPath(WUI_TRASH_DIR, stem + " (" + String(n) + ")" + ext);
  }
  if (!SD.rename(path, dest)) { err = "cannot move " + path + " to the trash"; return false; }
  return true;
}

uint64_t treeBytes(const String &path) {
  std::vector<DirEntry> kids;
  bool isDir;
  {
    Guard g;
    File f = SD.open(path);
    if (!f) return 0;
    isDir = f.isDirectory();
    uint64_t sz = isDir ? 0 : f.size();
    f.close();
    if (!isDir) return sz;
    listDir(path, kids);
  }
  uint64_t total = 0;
  for (const auto &k : kids) total += k.dir ? treeBytes(childPath(path, k.name)) : k.size;
  return total;
}

static bool copyFile(const String &from, const String &to, uint8_t *buf, size_t bufLen,
                     const CopyProgress &progress, String &err) {
  File src, dst;
  {
    Guard g;
    if (SD.exists(to)) { err = "already exists: " + to; return false; }
    src = SD.open(from, FILE_READ);
    if (!src) { err = "cannot read " + from; return false; }
    dst = SD.open(to, FILE_WRITE);
    if (!dst) { src.close(); err = "cannot create " + to; return false; }
  }
  bool ok = true;
  while (ok) {
    size_t w = 0;
    int r;
    {
      Guard g;
      r = src.read(buf, bufLen);
      if (r > 0) w = dst.write(buf, r);
    }
    if (r <= 0) break;
    if (w != (size_t)r) { err = "short write — card full?"; ok = false; break; }
    if (progress && !progress(r)) { err = "cancelled"; ok = false; break; }
    delay(0);
  }
  {
    Guard g;
    src.close();
    dst.close();
    if (!ok) SD.remove(to);
  }
  invalidateUsage();
  return ok;
}

bool copyTree(const String &from, const String &to, uint8_t *buf, size_t bufLen,
              const CopyProgress &progress, String &err) {
  if (to == from || to.startsWith(from + "/")) { err = "cannot copy a folder into itself"; return false; }
  std::vector<DirEntry> kids;
  bool isDir;
  {
    Guard g;
    File f = SD.open(from);
    if (!f) { err = "no such path: " + from; return false; }
    isDir = f.isDirectory();
    f.close();
    if (isDir) {
      if (SD.exists(to)) { err = "already exists: " + to; return false; }
      if (!SD.mkdir(to)) { err = "cannot create " + to; return false; }
      listDir(from, kids);
    }
  }
  if (!isDir) return copyFile(from, to, buf, bufLen, progress, err);
  for (const auto &k : kids)
    if (!copyTree(childPath(from, k.name), childPath(to, k.name), buf, bufLen, progress, err)) return false;
  return true;
}

bool lentToUsb() { return g_lent; }
void setLentToUsb(bool lent) { g_lent = lent; }

}  // namespace storage
