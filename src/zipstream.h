#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <vector>

// Streaming ZIP writer for folder downloads.
//
// Files are STORED, not deflated: SD reads are the bottleneck, and most of what
// lives on a card (media, firmware, archives) does not compress anyway, so
// deflate would only burn CPU and RAM. Storing also means the archive's exact
// size is known before the first byte goes out, so the download carries a real
// Content-Length and the browser shows honest progress.
//
// Each entry's CRC is only known after its data has streamed, so entries use a
// data descriptor (general-purpose flag bit 3). ZIP64 records are added exactly
// where the classic 32-bit fields would overflow: files of 4 GiB - 1 bytes
// (the FAT32 maximum), offsets past 4 GiB and more than 65535 entries.

struct ZipEntry {
  String   name;       // path inside the archive; directories end with '/'
  uint64_t size = 0;   // 0 for directories
  uint32_t dosTime = 0;
  // Filled in while writing:
  uint32_t crc = 0;
  uint64_t offset = 0;
};

class ZipSink {
 public:
  virtual ~ZipSink() {}
  virtual bool write(const uint8_t *data, size_t len) = 0;
};

class ZipWriter {
 public:
  explicit ZipWriter(ZipSink &sink) : sink_(sink) {}

  bool beginEntry(ZipEntry &e);                  // local file header
  bool data(const uint8_t *p, size_t n);         // file bytes (CRC is accumulated)
  bool endEntry();                               // data descriptor
  bool finish(std::vector<ZipEntry> &entries);   // central directory + end records

  uint64_t offset() const { return off_; }

  // Exact byte length of the archive these entries produce.
  static uint64_t totalSize(const std::vector<ZipEntry> &entries);

  static uint32_t crc32(uint32_t crc, const uint8_t *p, size_t n);
  static uint32_t dosTime(time_t t);

 private:
  bool put(const uint8_t *p, size_t n) { off_ += n; return sink_.write(p, n); }
  bool put16(uint16_t v);
  bool put32(uint32_t v);
  bool put64(uint64_t v);

  ZipSink &sink_;
  uint64_t off_ = 0;
  ZipEntry *cur_ = nullptr;
  uint32_t crc_ = 0;
  uint64_t got_ = 0;
};

// dosTime for a FAT modification time (seconds since epoch, local time).
uint32_t wui_zip_dos_time(time_t t);
