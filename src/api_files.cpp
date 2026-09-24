// /api/* endpoints that work on the SD card's files.
#include "api.h"
#include "config.h"
#include "http_util.h"
#include "jobs.h"
#include "paths.h"
#include "storage.h"
#include "stream.h"
#include "textutil.h"
#include <SD.h>
#include <esp_random.h>

using namespace http;

namespace {

String entryJson(const String &name, bool dir, uint64_t size, time_t mtime) {
  return "{\"name\":\"" + jsonEscape(name) + "\",\"dir\":" + (dir ? "true" : "false") +
         ",\"size\":" + String((unsigned long long)size) + ",\"mtime\":" + String((unsigned long)mtime) + "}";
}

// Splits a text/plain body into sanitised card paths, one per line.
bool bodyPaths(httpd_req_t *req, std::vector<String> &out, String &err) {
  String body = readBody(req, 16 * 1024);
  int start = 0;
  while (start < (int)body.length()) {
    int nl = body.indexOf('\n', start);
    if (nl < 0) nl = body.length();
    String line = body.substring(start, nl);
    line.trim();
    start = nl + 1;
    if (line.isEmpty()) continue;
    String p;
    if (!wui_safe_path(line.c_str(), p) || p == "/") { err = "bad path: " + line; return false; }
    out.push_back(p);
  }
  if (out.empty()) { err = "no paths given"; return false; }
  return true;
}

// ------------------------------------------------------------------ listing

esp_err_t hList(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!queryPath(req, "path", path)) return sendErr(req, "400 Bad Request", "bad path");
  std::vector<storage::DirEntry> kids;
  {
    storage::Guard g;
    File d = SD.open(path);
    if (!d) return sendErr(req, "404 Not Found", "no such folder: " + path);
    bool isDir = d.isDirectory();
    d.close();
    if (!isDir) return sendErr(req, "400 Bad Request", path + " is a file");
    storage::listDir(path, kids);
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  String out = "{\"path\":\"" + jsonEscape(path) + "\",\"entries\":[";
  for (size_t i = 0; i < kids.size(); ++i) {
    out += (i ? "," : "") + entryJson(kids[i].name, kids[i].dir, kids[i].size, kids[i].mtime);
    if (out.length() > 1400) { httpd_resp_send_chunk(req, out.c_str(), out.length()); out = ""; }
  }
  out += "]}";
  httpd_resp_send_chunk(req, out.c_str(), out.length());
  return httpd_resp_send_chunk(req, nullptr, 0);
}

// GET /api/find?path=/&q=holiday   (substring, or a glob with * and ?)
esp_err_t hFind(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String root, q = query(req, "q");
  if (!queryPath(req, "path", root)) return sendErr(req, "400 Bad Request", "bad path");
  if (q.isEmpty()) return sendErr(req, "400 Bad Request", "type something to search for");
  const bool inTrash = root.startsWith(WUI_TRASH_DIR);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  String out = "{\"results\":[";
  int found = 0, visited = 0;
  bool truncated = false;
  uint32_t t0 = millis();
  std::vector<String> todo{root};
  std::vector<storage::DirEntry> kids;
  while (!todo.empty()) {
    String dir = todo.back();
    todo.pop_back();
    { storage::Guard g; storage::listDir(dir, kids); }
    for (const auto &k : kids) {
      String full = (dir == "/" ? "/" : dir + "/") + k.name;
      if (++visited > WUI_FIND_VISIT_MAX || millis() - t0 > 20000) { truncated = true; break; }
      if (wui_name_matches(k.name.c_str(), q.c_str())) {
        if (++found > WUI_FIND_MAX) { truncated = true; break; }
        out += (found > 1 ? "," : "");
        out += "{\"path\":\"" + jsonEscape(full) + "\",\"dir\":" + (k.dir ? "true" : "false") +
               ",\"size\":" + String((unsigned long long)k.size) + ",\"mtime\":" + String((unsigned long)k.mtime) + "}";
        if (out.length() > 1400) { httpd_resp_send_chunk(req, out.c_str(), out.length()); out = ""; }
      }
      if (k.dir && (inTrash || full != WUI_TRASH_DIR) && k.name != "System Volume Information")
        todo.push_back(full);
    }
    if (truncated) break;
    delay(0);
  }
  out += "],\"truncated\":" + String(truncated ? "true" : "false") + ",\"visited\":" + String(visited) + "}";
  httpd_resp_send_chunk(req, out.c_str(), out.length());
  return httpd_resp_send_chunk(req, nullptr, 0);
}

// GET /api/du?path=  -> {"bytes":N}
esp_err_t hDu(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!queryPath(req, "path", path)) return sendErr(req, "400 Bad Request", "bad path");
  return sendJson(req, "200 OK", "{\"bytes\":" + String((unsigned long long)storage::treeBytes(path)) + "}");
}

// ------------------------------------------------------------------ changes

esp_err_t hMkdir(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!queryPath(req, "path", path) || path == "/") return sendErr(req, "400 Bad Request", "bad path");
  storage::Guard g;
  if (SD.exists(path)) {
    // Idempotent for a directory: a folder upload re-asserts every parent it
    // walks past. Only a name already taken by a *file* is a real conflict.
    File probe = SD.open(path);
    bool isDir = probe && probe.isDirectory();
    if (probe) probe.close();
    if (isDir) return sendOk(req);
    return sendErr(req, "409 Conflict", "already exists: " + path);
  }
  if (!SD.mkdir(path)) return sendErr(req, "500 Internal Server Error", "cannot create folder " + path);
  return sendOk(req);
}

esp_err_t hTouch(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!queryPath(req, "path", path) || path == "/") return sendErr(req, "400 Bad Request", "bad path");
  storage::Guard g;
  if (SD.exists(path)) return sendErr(req, "409 Conflict", "already exists: " + path);
  File f = SD.open(path, FILE_WRITE);
  if (!f) return sendErr(req, "500 Internal Server Error", "cannot create " + path);
  f.close();
  storage::invalidateUsage();
  return sendOk(req);
}

// POST /api/delete?path=…[&trash=1]
esp_err_t hDelete(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!queryPath(req, "path", path) || path == "/") return sendErr(req, "400 Bad Request", "bad path");
  bool toTrash = query(req, "trash") == "1";
  storage::Guard g;
  storage::closeUpload(true);
  String err;
  bool ok = toTrash ? storage::moveToTrash(path, err) : storage::removeRecursive(path, err);
  return ok ? sendOk(req) : sendErr(req, "500 Internal Server Error", err);
}

esp_err_t hEmptyTrash(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  storage::Guard g;
  if (!SD.exists(WUI_TRASH_DIR)) return sendOk(req);
  String err;
  return storage::removeRecursive(WUI_TRASH_DIR, err) ? sendOk(req) : sendErr(req, "500 Internal Server Error", err);
}

esp_err_t hRename(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String from, to;
  if (!queryPath(req, "from", from) || !queryPath(req, "to", to) || from == "/" || to == "/")
    return sendErr(req, "400 Bad Request", "bad path");
  if (to.startsWith(from + "/")) return sendErr(req, "400 Bad Request", "cannot move a folder inside itself");
  storage::Guard g;
  storage::closeUpload(true);
  if (!SD.exists(from)) return sendErr(req, "404 Not Found", "no such path: " + from);
  if (SD.exists(to))    return sendErr(req, "409 Conflict", "already exists: " + to);
  if (!SD.rename(from, to)) return sendErr(req, "500 Internal Server Error", "rename failed");
  return sendOk(req);
}

// POST /api/copy?from=&to=   or a body of "from\tto" lines (multi-select)
esp_err_t hCopy(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  std::vector<jobs::CopyItem> items;
  String from, to;
  if (!query(req, "from").isEmpty()) {
    if (!queryPath(req, "from", from) || !queryPath(req, "to", to) || from == "/" || to == "/")
      return sendErr(req, "400 Bad Request", "bad path");
    items.push_back({from, to});
  } else {
    String body = readBody(req, 16 * 1024);
    int start = 0;
    while (start < (int)body.length()) {
      int nl = body.indexOf('\n', start);
      if (nl < 0) nl = body.length();
      String line = body.substring(start, nl);
      start = nl + 1;
      int tab = line.indexOf('\t');
      if (tab < 0) continue;
      String a = line.substring(0, tab), b = line.substring(tab + 1);
      b.trim();
      if (!wui_safe_path(a.c_str(), from) || !wui_safe_path(b.c_str(), to) || from == "/" || to == "/")
        return sendErr(req, "400 Bad Request", "bad path: " + line);
      items.push_back({from, to});
    }
  }
  for (const auto &it : items)
    if (it.to == it.from || it.to.startsWith(it.from + "/"))
      return sendErr(req, "400 Bad Request", "cannot copy a folder into itself");
  String err;
  if (!jobs::startCopy(items, err)) return sendErr(req, "409 Conflict", err);
  return sendOk(req);
}

esp_err_t hHash(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String path, err;
  if (!queryPath(req, "path", path) || path == "/") return sendErr(req, "400 Bad Request", "bad path");
  if (!jobs::startHash(path, err)) return sendErr(req, "409 Conflict", err);
  return sendOk(req);
}

esp_err_t hJob(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  return sendJson(req, "200 OK", jobs::statusJson());
}

esp_err_t hJobCancel(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  jobs::cancel();
  return sendOk(req);
}

// ------------------------------------------------------------------ upload
// POST /api/upload?path=/x&offset=N[&final=1] with a raw body chunk.
// offset==0 truncates; any other offset must match the file's current size,
// so a failed chunk is simply retried at the same offset — free resume.
// The SD lock is taken per write, not for the whole chunk, so a slow client
// never stalls a download streaming on another task.

esp_err_t hUpload(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!queryPath(req, "path", path) || path == "/") return sendErr(req, "400 Bad Request", "bad path");
  uint64_t offset = strtoull(query(req, "offset").c_str(), nullptr, 10);
  bool final_ = query(req, "final") == "1";

  // Held from before we take the handle until we return, so the idle closer
  // in loop() never shuts the file under us.
  struct Busy { Busy() { storage::uploadBusy(true); } ~Busy() { storage::uploadBusy(false); } } busy;
  uint64_t actual = 0;
  String err;
  File *f;
  {
    storage::Guard g;
    f = storage::uploadHandle(path, offset, &actual, err);
  }
  if (!f) {
    if (err == "offset mismatch")
      return sendJson(req, "409 Conflict",
                      "{\"error\":\"offset mismatch\",\"size\":" + String((unsigned long long)actual) + "}");
    return sendErr(req, "500 Internal Server Error", err);
  }

  size_t remaining = req->content_len;
  size_t written = 0;
  int timeouts = 0;
  while (remaining > 0) {
    size_t want = std::min((size_t)WUI_IO_BUF, remaining);
    int got = httpd_req_recv(req, (char *)g_buf, want);
    if (got == HTTPD_SOCK_ERR_TIMEOUT) {
      // A client that announces a Content-Length and then goes quiet must not
      // hold this task forever — every synchronous handler shares it.
      if (++timeouts > 2) { storage::Guard g; f->flush(); return sendErr(req, "408 Request Timeout", "client stalled"); }
      continue;
    }
    timeouts = 0;
    if (got <= 0) { storage::Guard g; f->flush(); return sendErr(req, "408 Request Timeout", "connection dropped"); }
    size_t w;
    {
      storage::Guard g;
      w = f->write(g_buf, got);
      storage::uploadWrote(w);        // count it before any error exit: a short
    }                                 // write still put those bytes on the card
    written += w;
    if (w != (size_t)got) {
      storage::Guard g;
      f->flush();
      return sendErr(req, "507 Insufficient Storage", "short write — card full?");
    }
    remaining -= got;
    delay(0);                         // let the IDLE/wifi tasks breathe
  }
  if (final_) {
    storage::Guard g;
    f->flush();
    storage::closeUpload(true);
    storage::invalidateUsage();
  }
  return sendJson(req, "200 OK",
                  "{\"ok\":true,\"size\":" + String((unsigned long long)(offset + written)) + "}");
}

// ---------------------------------------------------------------- download

// GET /api/download?path=…[&inline=1][&max=N]
esp_err_t hDownload(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!queryPath(req, "path", path) || path == "/") return sendErr(req, "400 Bad Request", "bad path");
  stream::FileOpts o;
  o.inlineView = o.capOpenRanges = query(req, "inline") == "1";
  o.maxBytes = strtoull(query(req, "max").c_str(), nullptr, 10);
  return stream::sendFile(req, path, o);
}

// Multi-item ZIPs are two steps: POST the list, then navigate to the ticket,
// so the browser's own download manager handles the (possibly huge) file.
String   g_ticket, g_ticketPaths;
uint32_t g_ticketAt = 0;

esp_err_t hZipTicket(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  std::vector<String> paths;
  String err;
  if (!bodyPaths(req, paths, err)) return sendErr(req, "400 Bad Request", err);
  g_ticketPaths = "";
  for (auto &p : paths) g_ticketPaths += p + "\n";
  char t[17];
  snprintf(t, sizeof(t), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
  g_ticket = t;
  g_ticketAt = millis();
  return sendJson(req, "200 OK", "{\"t\":\"" + g_ticket + "\"}");
}

// GET /api/zip?path=/folder   or   GET /api/zip?t=<ticket>[&name=x.zip]
esp_err_t hZip(httpd_req_t *req) {
  if (!requireAuth(req) || !requireSD(req)) return ESP_OK;
  std::vector<String> paths;
  String t = query(req, "t");
  if (!t.isEmpty()) {
    if (t != g_ticket || millis() - g_ticketAt > 120000) return sendErr(req, "410 Gone", "download link expired — try again");
    g_ticket = "";
    int start = 0;
    while (start < (int)g_ticketPaths.length()) {
      int nl = g_ticketPaths.indexOf('\n', start);
      paths.push_back(g_ticketPaths.substring(start, nl));
      start = nl + 1;
    }
  } else {
    String p;
    if (!queryPath(req, "path", p) || p == "/") return sendErr(req, "400 Bad Request", "pick a folder");
    paths.push_back(p);
  }
  String name = query(req, "name");
  if (name.isEmpty()) name = paths.size() == 1 ? paths[0].substring(paths[0].lastIndexOf('/') + 1) : "files";
  if (!name.endsWith(".zip")) name += ".zip";
  return stream::sendZip(req, paths, name);
}

}  // namespace

namespace api {

void registerFiles(httpd_handle_t h) {
  reg(h, "/api/list",        HTTP_GET,  hList);
  reg(h, "/api/find",        HTTP_GET,  hFind);
  reg(h, "/api/du",          HTTP_GET,  hDu);
  reg(h, "/api/download",    HTTP_GET,  hDownload);
  reg(h, "/api/zip",         HTTP_GET,  hZip);
  reg(h, "/api/zipticket",   HTTP_POST, hZipTicket);
  reg(h, "/api/mkdir",       HTTP_POST, hMkdir);
  reg(h, "/api/touch",       HTTP_POST, hTouch);
  reg(h, "/api/delete",      HTTP_POST, hDelete);
  reg(h, "/api/trash/empty", HTTP_POST, hEmptyTrash);
  reg(h, "/api/rename",      HTTP_POST, hRename);
  reg(h, "/api/copy",        HTTP_POST, hCopy);
  reg(h, "/api/hash",        HTTP_POST, hHash);
  reg(h, "/api/job",         HTTP_GET,  hJob);
  reg(h, "/api/job/cancel",  HTTP_POST, hJobCancel);
  reg(h, "/api/upload",      HTTP_POST, hUpload);
}

}  // namespace api
