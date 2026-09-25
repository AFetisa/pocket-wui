// Builds ZIPs with the firmware's writer and checks them with Python's zipfile.
//   g++ -std=c++17 -I test/host -o /tmp/tz test/host/test_zip.cpp && /tmp/tz
// Set WUI_ZIP64_TEST=1 to also build a >4 GiB archive (sparse, ~10 s).
#include "arduino_shim.h"
#include "../../src/zipstream.cpp"
#include <cstdio>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { std::cout << "FAIL  " << msg << "\n"; fails++; } } while (0)

// Writes to a file; long runs of zero bytes become holes so a 4 GiB test
// archive costs no disk.
class FileSink : public ZipSink {
 public:
  explicit FileSink(const char *path) { f_ = fopen(path, "wb"); }
  ~FileSink() { if (f_) { fflush(f_); if (ftruncate(fileno(f_), pos_)) {} fclose(f_); } }
  bool write(const uint8_t *d, size_t n) override {
    bool zero = n >= 4096;
    for (size_t i = 0; zero && i < n; ++i) zero = d[i] == 0;
    if (zero) { fseeko(f_, (off_t)n, SEEK_CUR); pos_ += n; return true; }
    pos_ += n;
    return fwrite(d, 1, n, f_) == n;
  }
  uint64_t pos_ = 0;
 private:
  FILE *f_;
};

struct Src { std::string name; std::string content; uint64_t zeros = 0; };

static uint64_t build(const char *out, std::vector<Src> srcs) {
  std::vector<ZipEntry> entries;
  for (auto &s : srcs) {
    ZipEntry e;
    e.name = s.name.c_str();
    e.size = s.zeros ? s.zeros : s.content.size();
    e.dosTime = wui_zip_dos_time(1700000000);
    entries.push_back(e);
  }
  uint64_t predicted = ZipWriter::totalSize(entries);
  FileSink sink(out);
  ZipWriter w(sink);
  static uint8_t zeros[1 << 20];
  for (size_t i = 0; i < entries.size(); ++i) {
    CHECK(w.beginEntry(entries[i]), "beginEntry " << srcs[i].name);
    if (srcs[i].zeros) {
      for (uint64_t left = srcs[i].zeros; left;) {
        size_t n = left > sizeof(zeros) ? sizeof(zeros) : (size_t)left;
        w.data(zeros, n);
        left -= n;
      }
    } else if (!srcs[i].content.empty()) {
      w.data((const uint8_t *)srcs[i].content.data(), srcs[i].content.size());
    }
    CHECK(w.endEntry(), "endEntry " << srcs[i].name);
  }
  CHECK(w.finish(entries), "finish");
  CHECK(w.offset() == predicted, "size prediction " << predicted << " != written " << w.offset());
  return w.offset();
}

static bool pyCheck(const char *zip, const char *expect) {
  std::string cmd = std::string("python3 - '") + zip + "' <<'PY'\n"
    "import sys, zipfile, json\n"
    "z = zipfile.ZipFile(sys.argv[1])\n"
    "bad = z.testzip()\n"
    "assert bad is None, 'crc mismatch in %s' % bad\n"
    "print(json.dumps(sorted((i.filename, i.file_size) for i in z.infolist())))\n"
    "PY";
  FILE *p = popen(cmd.c_str(), "r");
  char buf[8192] = {0};
  size_t n = fread(buf, 1, sizeof(buf) - 1, p);
  buf[n] = 0;
  int rc = pclose(p);
  std::string got(buf);
  while (!got.empty() && got.back() == '\n') got.pop_back();
  if (rc != 0 || got != expect) {
    std::cout << "FAIL  zipfile check of " << zip << "\n  got:  " << got << "\n  want: " << expect << "\n";
    return false;
  }
  return true;
}

int main() {
  CHECK(ZipWriter::crc32(0, (const uint8_t *)"123456789", 9) == 0xCBF43926u, "crc32 check value");

  const char *z1 = "/tmp/wui_test1.zip";
  build(z1, {{"photos/", ""}, {"photos/a.txt", "hello"}, {"photos/sub/", ""},
             {"photos/sub/b.bin", std::string(100000, 'x')}, {"photos/empty", ""},
             {"photos/ünïcödé ✓.txt", "utf8"}});
  if (!pyCheck(z1, "[[\"photos/\", 0], [\"photos/a.txt\", 5], [\"photos/empty\", 0], "
                   "[\"photos/sub/\", 0], [\"photos/sub/b.bin\", 100000], "
                   "[\"photos/\\u00fcn\\u00efc\\u00f6d\\u00e9 \\u2713.txt\", 4]]")) fails++;

  // 70000 entries: forces the ZIP64 end records via the entry count alone.
  std::vector<Src> many;
  for (int i = 0; i < 70000; ++i) many.push_back({"f" + std::to_string(i), "x"});
  build("/tmp/wui_test2.zip", many);
  {
    FILE *p = popen("python3 -c \"import zipfile;z=zipfile.ZipFile('/tmp/wui_test2.zip');"
                    "print(len(z.infolist()), z.read('f69999'))\"", "r");
    char b[128] = {0};
    if (!fread(b, 1, sizeof(b) - 1, p)) b[0] = 0;
    pclose(p);
    CHECK(std::string(b) == "70000 b'x'\n", "70000-entry zip64 archive, got " << b);
  }

  if (getenv("WUI_ZIP64_TEST")) {
    // A FAT32-maximum file (4 GiB - 1) plus one after it, whose offset is past 4 GiB.
    build("/tmp/wui_test3.zip", {{"big.bin", "", 0xFFFFFFFFull}, {"after.txt", "tail"}});
    if (!pyCheck("/tmp/wui_test3.zip", "[[\"after.txt\", 4], [\"big.bin\", 4294967295]]")) fails++;
    remove("/tmp/wui_test3.zip");
  }
  remove(z1);
  remove("/tmp/wui_test2.zip");
  std::cout << (fails ? "zip tests FAILED\n" : "zip tests ok\n");
  return fails ? 1 : 0;
}
