#pragma once
// Arduino fs::File over a host directory tree (see sim_sd.cpp).
#include <Arduino.h>
#include <memory>

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

namespace fs {

struct FileImpl;

class File {
 public:
  File() {}
  explicit File(std::shared_ptr<FileImpl> p) : p_(std::move(p)) {}
  operator bool() const;
  size_t read(uint8_t *buf, size_t n);
  size_t write(const uint8_t *buf, size_t n);
  size_t write(uint8_t c) { return write(&c, 1); }
  bool seek(uint32_t pos);
  size_t position() const;
  size_t size() const;
  void flush();
  void close();
  bool isDirectory() const;
  File openNextFile(const char *mode = FILE_READ);
  const char *name() const;
  const char *path() const;
  time_t getLastWrite();
 private:
  std::shared_ptr<FileImpl> p_;
};

}  // namespace fs
using fs::File;
