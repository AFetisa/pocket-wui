#include "stream.h"
#include "config.h"
#include "http_util.h"
#include "storage.h"
#include "textutil.h"
#include "threads.h"
#include "zipstream.h"
#include <SD.h>
#include <atomic>
#include <memory>

namespace {

std::atomic<int> g_active{0};

struct FileJob {
  String path;
  uint64_t start = 0, length = 0;
};

struct ZipJob {
  std::vector<ZipEntry> entries;
  std::vector<String> sources;       // card path per entry ("" for directories)
};

// Hands `work(req, buf)` to a background thread with its own buffer. Returns
// false (nothing taken over) when no worker is free, so the caller serves inline.
bool handOff(httpd_req_t *req, std::function<void(httpd_req_t *, uint8_t *)> work) {
  if (g_active.fetch_add(1) >= WUI_STREAM_WORKERS) { g_active--; return false; }
  uint8_t *buf = (uint8_t *)malloc(WUI_STREAM_BUF);
  httpd_req_t *copy = nullptr;
  if (!buf || httpd_req_async_handler_begin(req, &copy) != ESP_OK) {
    free(buf);
    g_active--;
    return false;
  }
  bool ok = wui_spawn("wui-stream", 6144, [copy, buf, work]() {
    work(copy, buf);
    httpd_req_async_handler_complete(copy);
    free(buf);
    g_active--;
  });
  if (!ok) {
    httpd_req_async_handler_complete(copy);   // the connection is dropped; the client retries
    free(buf);
    g_active--;
  }
  return true;
}

void pumpFile(httpd_req_t *req, const FileJob &job, uint8_t *buf, size_t bufLen) {
  File f;
  {
    storage::Guard g;
    f = SD.open(job.path, FILE_READ);
    if (f && job.start) f.seek(job.start);
  }
  if (!f) return;
  uint64_t left = job.length;
  while (left > 0) {
    size_t want = (size_t)std::min<uint64_t>(bufLen, left);
    int got;
    {
      storage::Guard g;
      got = f.read(buf, want);
    }
    if (got <= 0) break;
    if (!http::rawSend(req, (const char *)buf, got)) break;
    left -= got;
    delay(0);
  }
  storage::Guard g;
  f.close();
}

// Batches the ZIP writer's many small header writes into one socket write.
class SocketSink : public ZipSink {
 public:
  explicit SocketSink(httpd_req_t *r) : req_(r) {}
  bool write(const uint8_t *d, size_t n) override {
    if (!ok_) return false;
    if (n >= sizeof(buf_)) return flush() && (ok_ = http::rawSend(req_, (const char *)d, n));
    if (used_ + n > sizeof(buf_) && !flush()) return false;
    memcpy(buf_ + used_, d, n);
    used_ += n;
    return true;
  }
  bool flush() {
    if (used_ && ok_) ok_ = http::rawSend(req_, (const char *)buf_, used_);
    used_ = 0;
    return ok_;
  }
 private:
  httpd_req_t *req_;
  uint8_t buf_[1400];
  size_t used_ = 0;
  bool ok_ = true;
};

void pumpZip(httpd_req_t *req, ZipJob &job, uint8_t *buf, size_t bufLen) {
  SocketSink sink(req);
  ZipWriter w(sink);
  for (size_t i = 0; i < job.entries.size(); ++i) {
    ZipEntry &e = job.entries[i];
    if (!w.beginEntry(e)) return;
    if (!job.sources[i].isEmpty() && e.size) {
      File f;
      { storage::Guard g; f = SD.open(job.sources[i], FILE_READ); }
      uint64_t left = e.size;
      while (left > 0) {
        size_t want = (size_t)std::min<uint64_t>(bufLen, left);
        int got = 0;
        if (f) { storage::Guard g; got = f.read(buf, want); }
        if (got <= 0) {
          // The file shrank (or vanished) since the size went into the
          // Content-Length: pad so the archive stays well-formed. Its CRC
          // will not match, so an unzipper flags exactly this entry.
          log_w("zip: %s ended early", job.sources[i].c_str());
          memset(buf, 0, want);
          got = want;
        }
        if (!w.data(buf, got)) { if (f) { storage::Guard g; f.close(); } return; }
        left -= got;
        delay(0);
      }
      if (f) { storage::Guard g; f.close(); }
    }
    w.endEntry();
  }
  w.finish(job.entries);
  sink.flush();
}

// "bytes=a-b" / "bytes=a-" / "bytes=-n". Returns false for no/ignored Range.
bool parseRange(const String &h, uint64_t size, uint64_t &start, uint64_t &end, bool &openEnded) {
  if (!h.startsWith("bytes=") || h.indexOf(',') >= 0 || size == 0) return false;
  String spec = h.substring(6);
  int dash = spec.indexOf('-');
  if (dash < 0) return false;
  String a = spec.substring(0, dash), b = spec.substring(dash + 1);
  a.trim(); b.trim();
  openEnded = false;
  if (a.isEmpty()) {                                   // suffix: last n bytes
    uint64_t n = strtoull(b.c_str(), nullptr, 10);
    if (!n) return false;
    start = n >= size ? 0 : size - n;
    end = size - 1;
    return true;
  }
  start = strtoull(a.c_str(), nullptr, 10);
  if (b.isEmpty()) { end = size - 1; openEnded = true; }
  else end = std::min<uint64_t>(strtoull(b.c_str(), nullptr, 10), size - 1);
  return start <= end || start >= size;                // start >= size -> 416 below
}

}  // namespace

namespace stream {

int active() { return g_active.load(); }

esp_err_t sendFile(httpd_req_t *req, const String &path, const FileOpts &o) {
  const bool inlineView = o.inlineView, headOnly = o.headOnly;
  const uint64_t cap = o.maxBytes;
  uint64_t size;
  time_t mtime;
  {
    storage::Guard g;
    File f = SD.open(path, FILE_READ);
    if (!f) return http::sendErr(req, "404 Not Found", "no such file: " + path);
    if (f.isDirectory()) { f.close(); return http::sendErr(req, "400 Bad Request", path + " is a folder"); }
    size = f.size();
    mtime = f.getLastWrite();
    f.close();
  }

  uint64_t start = 0, end = size ? size - 1 : 0;
  bool openEnded = false;
  bool partial = parseRange(http::header(req, "Range", 96), size, start, end, openEnded);
  if (partial && start >= size) {
    String h = "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" +
               String((unsigned long long)size) + "\r\nContent-Length: 0\r\n\r\n";
    http::rawSend(req, h.c_str(), h.length());
    return ESP_OK;
  }
  // A media element asks for "bytes=N-" and reads as far as it likes; capping
  // the reply lets it come back for the next piece instead of pinning a worker
  // for the whole file. Only for the WUI's own previews: download managers and
  // WebDAV clients resuming with the same header expect the rest of the file.
  if (partial && openEnded && o.capOpenRanges && end - start + 1 > WUI_RANGE_CAP) end = start + WUI_RANGE_CAP - 1;
  if (cap && !partial && size > cap) end = cap - 1;
  uint64_t length = size ? (end - start + 1) : 0;

  String name = path.substring(path.lastIndexOf('/') + 1);
  const char *mime = inlineView ? wui_mime_for(name) : "application/octet-stream";
  String type = mime;
  if (inlineView && wui_mime_is_text(mime) && !wui_mime_needs_sandbox(mime)) type = "text/plain; charset=utf-8";

  String head;
  head.reserve(512);
  head += partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
  head += "Content-Type: " + type + "\r\n";
  head += "Content-Length: " + String((unsigned long long)length) + "\r\n";
  head += "Accept-Ranges: bytes\r\n";
  head += "Cache-Control: private, no-cache\r\n";
  head += "ETag: \"" + String((unsigned long long)size) + "-" + String((unsigned long)mtime) + "\"\r\n";
  if (mtime > 0) head += "Last-Modified: " + wui_http_date(mtime) + "\r\n";
  head += "Content-Disposition: " + wui_content_disposition(name, inlineView) + "\r\n";
  head += "X-Content-Type-Options: nosniff\r\n";
  // Anything that could run script is shown as an inert, origin-less document.
  if (inlineView && wui_mime_needs_sandbox(mime)) head += "Content-Security-Policy: sandbox\r\n";
  if (partial)
    head += "Content-Range: bytes " + String((unsigned long long)start) + "-" + String((unsigned long long)end) +
            "/" + String((unsigned long long)size) + "\r\n";
  head += "Connection: keep-alive\r\n\r\n";

  if (!http::rawSend(req, head.c_str(), head.length())) return ESP_FAIL;
  if (headOnly || !length) return ESP_OK;

  FileJob job{path, start, length};
  if (length > 64 * 1024 &&
      handOff(req, [job](httpd_req_t *r, uint8_t *buf) { pumpFile(r, job, buf, WUI_STREAM_BUF); }))
    return ESP_OK;
  pumpFile(req, job, http::g_buf, WUI_IO_BUF);
  return ESP_OK;
}

esp_err_t sendZip(httpd_req_t *req, const std::vector<String> &paths, const String &zipName) {
  auto job = std::make_shared<ZipJob>();
  size_t nameBytes = 0;
  String err;
  {
    // Walk everything first: the central directory needs every entry, and the
    // Content-Length needs every size.
    storage::Guard g;
    struct Todo { String src, arc; };
    std::vector<Todo> todo;
    for (const String &p : paths) {
      if (p == "/") { err = "pick a folder, not the whole card"; break; }
      todo.push_back({p, p.substring(p.lastIndexOf('/') + 1)});
    }
    while (err.isEmpty() && !todo.empty()) {
      Todo t = todo.back();
      todo.pop_back();
      File f = SD.open(t.src);
      if (!f) { err = "no such path: " + t.src; break; }
      bool dir = f.isDirectory();
      ZipEntry e;
      e.dosTime = wui_zip_dos_time(f.getLastWrite());
      e.size = dir ? 0 : f.size();
      f.close();
      e.name = dir ? t.arc + "/" : t.arc;
      nameBytes += e.name.length() + 48;
      job->entries.push_back(e);
      job->sources.push_back(dir ? String() : t.src);
      if (job->entries.size() > WUI_ZIP_MAX_ENTRIES || nameBytes > 160 * 1024) {
        err = "too many files for one download (max " + String(WUI_ZIP_MAX_ENTRIES) + ") — pick a smaller folder";
        break;
      }
      if (dir) {
        std::vector<storage::DirEntry> kids;
        storage::listDir(t.src, kids);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it)
          todo.push_back({t.src + "/" + it->name, t.arc + "/" + it->name});
      }
    }
  }
  if (!err.isEmpty()) return http::sendErr(req, "400 Bad Request", err);

  uint64_t total = ZipWriter::totalSize(job->entries);
  String head = "HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\nContent-Length: " +
                String((unsigned long long)total) + "\r\nCache-Control: no-store\r\n" +
                "Content-Disposition: " + wui_content_disposition(zipName, false) +
                "\r\nX-Content-Type-Options: nosniff\r\nConnection: keep-alive\r\n\r\n";
  if (!http::rawSend(req, head.c_str(), head.length())) return ESP_FAIL;
  if (handOff(req, [job](httpd_req_t *r, uint8_t *buf) { pumpZip(r, *job, buf, WUI_STREAM_BUF); }))
    return ESP_OK;
  pumpZip(req, *job, http::g_buf, WUI_IO_BUF);
  return ESP_OK;
}

}  // namespace stream
