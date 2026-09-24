// The simulated SD card: a host directory, with FAT-like semantics where they
// matter to the firmware (rename refuses to overwrite, mkdir needs a parent).
#include <SD.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <cstdio>

SDFS SD;
SPIClass SPI;
std::string wui_sim_root = "/tmp/pocketwui-sd";

namespace fs {

struct FileImpl {
  std::string path;       // card path, "/a/b"
  std::string name;       // basename
  FILE *f = nullptr;
  DIR *d = nullptr;
  bool dir = false;
  ~FileImpl() { if (f) fclose(f); if (d) closedir(d); }
};

}  // namespace fs

static std::string host(const std::string &p) { return wui_sim_root + (p.empty() || p[0] != '/' ? "/" : "") + p; }

static std::shared_ptr<fs::FileImpl> openImpl(const std::string &cardPath, const char *mode) {
  auto impl = std::make_shared<fs::FileImpl>();
  impl->path = cardPath.empty() ? "/" : cardPath;
  size_t s = impl->path.find_last_of('/');
  impl->name = impl->path == "/" ? "/" : impl->path.substr(s + 1);
  struct stat st;
  bool exists = stat(host(impl->path).c_str(), &st) == 0;
  if (exists && S_ISDIR(st.st_mode)) {
    if (mode[0] != 'r') return nullptr;
    impl->dir = true;
    impl->d = opendir(host(impl->path).c_str());
    return impl->d ? impl : nullptr;
  }
  if (!exists && mode[0] == 'r') return nullptr;
  const char *m = mode[0] == 'r' ? "rb" : mode[0] == 'a' ? "ab" : "wb";
  impl->f = fopen(host(impl->path).c_str(), m);
  return impl->f ? impl : nullptr;
}

namespace fs {

File::operator bool() const { return p_ && (p_->f || p_->d); }
size_t File::read(uint8_t *buf, size_t n) { return p_ && p_->f ? fread(buf, 1, n, p_->f) : 0; }
size_t File::write(const uint8_t *buf, size_t n) { return p_ && p_->f ? fwrite(buf, 1, n, p_->f) : 0; }
bool File::seek(uint32_t pos) { return p_ && p_->f && fseeko(p_->f, pos, SEEK_SET) == 0; }
size_t File::position() const { return p_ && p_->f ? (size_t)ftello(p_->f) : 0; }
size_t File::size() const {
  // Like FATFS: the directory entry (stat) only reflects flushed data.
  struct stat st;
  if (!p_ || stat(host(p_->path).c_str(), &st) != 0) return 0;
  return S_ISDIR(st.st_mode) ? 0 : (size_t)st.st_size;
}
void File::flush() { if (p_ && p_->f) fflush(p_->f); }
void File::close() { p_.reset(); }
bool File::isDirectory() const { return p_ && p_->dir; }
const char *File::name() const { return p_ ? p_->name.c_str() : ""; }
const char *File::path() const { return p_ ? p_->path.c_str() : ""; }
time_t File::getLastWrite() {
  struct stat st;
  if (!p_ || stat(host(p_->path).c_str(), &st) != 0) return 0;
  return st.st_mtime;
}
File File::openNextFile(const char *mode) {
  if (!p_ || !p_->d) return File();
  while (struct dirent *e = readdir(p_->d)) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    std::string child = (p_->path == "/" ? "" : p_->path) + "/" + e->d_name;
    auto impl = openImpl(child, mode);
    if (impl) return File(impl);
  }
  return File();
}

}  // namespace fs

bool SDFS::begin(uint8_t, SPIClass &, uint32_t, const char *, uint8_t, bool) {
  ::mkdir(wui_sim_root.c_str(), 0755);
  struct stat st;
  return stat(wui_sim_root.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
uint64_t SDFS::totalBytes() { struct statvfs v; return statvfs(wui_sim_root.c_str(), &v) ? 0 : (uint64_t)v.f_blocks * v.f_frsize; }
uint64_t SDFS::usedBytes() { struct statvfs v; return statvfs(wui_sim_root.c_str(), &v) ? 0 : (uint64_t)(v.f_blocks - v.f_bfree) * v.f_frsize; }
File SDFS::open(const String &path, const char *mode) { auto i = openImpl(path.c_str(), mode); return i ? File(i) : File(); }
bool SDFS::exists(const String &path) { struct stat st; return stat(host(path.c_str()).c_str(), &st) == 0; }
bool SDFS::mkdir(const String &path) { return ::mkdir(host(path.c_str()).c_str(), 0755) == 0; }
bool SDFS::remove(const String &path) { return ::unlink(host(path.c_str()).c_str()) == 0; }
bool SDFS::rmdir(const String &path) { return ::rmdir(host(path.c_str()).c_str()) == 0; }
bool SDFS::rename(const String &from, const String &to) {
  if (exists(to)) return false;          // FAT's f_rename refuses to overwrite
  return ::rename(host(from.c_str()).c_str(), host(to.c_str()).c_str()) == 0;
}
