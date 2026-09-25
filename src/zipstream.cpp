#include "zipstream.h"
#include <time.h>

namespace {

constexpr uint32_t kMax32 = 0xFFFFFFFFu;
constexpr uint16_t kFlags = (1u << 3) | (1u << 11);   // data descriptor, UTF-8 names

bool isZip64Size(uint64_t size) { return size >= kMax32; }

uint32_t g_table[256];
bool g_tableReady = false;

void buildTable() {
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t c = i;
    for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    g_table[i] = c;
  }
  g_tableReady = true;
}

}  // namespace

uint32_t ZipWriter::crc32(uint32_t crc, const uint8_t *p, size_t n) {
  if (!g_tableReady) buildTable();
  crc = ~crc;
  while (n--) crc = g_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

uint32_t ZipWriter::dosTime(time_t t) { return wui_zip_dos_time(t); }

uint32_t wui_zip_dos_time(time_t t) {
  struct tm tm;
  if (t <= 0 || !localtime_r(&t, &tm) || tm.tm_year < 80) return (1u << 5 | 1u) << 16;  // 1980-01-01
  uint32_t date = ((uint32_t)(tm.tm_year - 80) << 9) | ((uint32_t)(tm.tm_mon + 1) << 5) | (uint32_t)tm.tm_mday;
  uint32_t tim = ((uint32_t)tm.tm_hour << 11) | ((uint32_t)tm.tm_min << 5) | (uint32_t)(tm.tm_sec / 2);
  return (date << 16) | tim;
}

bool ZipWriter::put16(uint16_t v) {
  uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
  return put(b, 2);
}
bool ZipWriter::put32(uint32_t v) {
  uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
  return put(b, 4);
}
bool ZipWriter::put64(uint64_t v) { return put32((uint32_t)v) && put32((uint32_t)(v >> 32)); }

bool ZipWriter::beginEntry(ZipEntry &e) {
  cur_ = &e;
  crc_ = 0;
  got_ = 0;
  e.offset = off_;
  bool z64 = isZip64Size(e.size);
  bool ok = put32(0x04034b50) && put16(z64 ? 45 : 20) && put16(kFlags) && put16(0) &&
            put16((uint16_t)e.dosTime) && put16((uint16_t)(e.dosTime >> 16)) &&
            put32(0) &&                                  // CRC: in the descriptor
            put32(z64 ? kMax32 : 0) && put32(z64 ? kMax32 : 0) &&
            put16((uint16_t)e.name.length()) && put16(z64 ? 20 : 0) &&
            put((const uint8_t *)e.name.c_str(), e.name.length());
  if (ok && z64) ok = put16(0x0001) && put16(16) && put64(0) && put64(0);
  return ok;
}

bool ZipWriter::data(const uint8_t *p, size_t n) {
  crc_ = crc32(crc_, p, n);
  got_ += n;
  return put(p, n);
}

bool ZipWriter::endEntry() {
  if (!cur_) return false;
  ZipEntry &e = *cur_;
  e.crc = crc_;
  // The declared size wins: it went into the Content-Length. A file that
  // changed underneath us is the caller's to detect (it compares got_).
  bool z64 = isZip64Size(e.size);
  bool ok = put32(0x08074b50) && put32(e.crc);
  ok = ok && (z64 ? (put64(e.size) && put64(e.size)) : (put32((uint32_t)e.size) && put32((uint32_t)e.size)));
  cur_ = nullptr;
  return ok && got_ == e.size;
}

bool ZipWriter::finish(std::vector<ZipEntry> &entries) {
  uint64_t cdStart = off_;
  for (const ZipEntry &e : entries) {
    bool bigSize = isZip64Size(e.size), bigOff = e.offset >= kMax32;
    uint16_t extra = (bigSize ? 16 : 0) + (bigOff ? 8 : 0);
    bool isDir = e.name.endsWith("/");
    bool ok = put32(0x02014b50) && put16(0x003F) && put16((bigSize || bigOff) ? 45 : 20) &&
              put16(kFlags) && put16(0) && put16((uint16_t)e.dosTime) &&
              put16((uint16_t)(e.dosTime >> 16)) && put32(e.crc) &&
              put32(bigSize ? kMax32 : (uint32_t)e.size) && put32(bigSize ? kMax32 : (uint32_t)e.size) &&
              put16((uint16_t)e.name.length()) && put16(extra ? extra + 4 : 0) && put16(0) &&
              put16(0) && put16(0) && put32(isDir ? 0x10 : 0x20) &&
              put32(bigOff ? kMax32 : (uint32_t)e.offset) &&
              put((const uint8_t *)e.name.c_str(), e.name.length());
    if (ok && extra) {
      ok = put16(0x0001) && put16(extra);
      if (ok && bigSize) ok = put64(e.size) && put64(e.size);
      if (ok && bigOff) ok = put64(e.offset);
    }
    if (!ok) return false;
  }
  uint64_t cdSize = off_ - cdStart;
  uint64_t n = entries.size();
  if (n >= 0xFFFF || cdStart >= kMax32 || cdSize >= kMax32) {
    uint64_t z64End = off_;
    if (!(put32(0x06064b50) && put64(44) && put16(45) && put16(45) && put32(0) && put32(0) &&
          put64(n) && put64(n) && put64(cdSize) && put64(cdStart)))
      return false;
    if (!(put32(0x07064b50) && put32(0) && put64(z64End) && put32(1))) return false;
  }
  uint16_t n16 = n >= 0xFFFF ? 0xFFFF : (uint16_t)n;
  return put32(0x06054b50) && put16(0) && put16(0) && put16(n16) && put16(n16) &&
         put32(cdSize >= kMax32 ? kMax32 : (uint32_t)cdSize) &&
         put32(cdStart >= kMax32 ? kMax32 : (uint32_t)cdStart) && put16(0);
}

uint64_t ZipWriter::totalSize(const std::vector<ZipEntry> &entries) {
  // Same layout as beginEntry/endEntry/finish, counted instead of written.
  uint64_t off = 0, cd = 0;
  for (const ZipEntry &e : entries) {
    bool z64 = isZip64Size(e.size), bigOff = off >= kMax32;
    uint32_t extra = (z64 ? 16 : 0) + (bigOff ? 8 : 0);
    cd += 46 + e.name.length() + (extra ? extra + 4 : 0);
    off += 30 + e.name.length() + (z64 ? 20 : 0) + e.size + (z64 ? 24 : 16);
  }
  uint64_t n = entries.size();
  bool z64End = n >= 0xFFFF || off >= kMax32 || cd >= kMax32;
  return off + cd + (z64End ? 56 + 20 : 0) + 22;
}
