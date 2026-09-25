// WebDAV at /dav/ — mount the card as a network drive (Finder, Windows via
// WinSCP/Cyberduck/RaiDrive, Linux file managers, iOS/Android file apps, VLC,
// Infuse, rclone). Class 1 + the fake locking Finder and Windows insist on.
//
// Auth: HTTP Basic with the WUI password (any user name), or the WUI's own
// session cookie. Basic credentials that a browser caches cannot be abused
// cross-site here: every state-changing DAV method is non-simple (PUT, DELETE,
// MOVE, …) and would need a CORS preflight we never grant.
#include "api.h"
#include "auth.h"
#include "config.h"
#include "http_util.h"
#include "paths.h"
#include "storage.h"
#include "stream.h"
#include "textutil.h"
#include <SD.h>
#include <esp_random.h>

#if WUI_WEBDAV

using namespace http;

namespace {

constexpr const char *kPrefix = "/dav";

bool davAuth(httpd_req_t *req) {
  if (authed(req)) return true;
  String h = header(req, "Authorization", 300);
  if (h.startsWith("Basic ") && !auth::lockedOut()) {
    String creds;
    if (wui_b64_decode(h.c_str() + 6, creds)) {
      int colon = creds.indexOf(':');
      String pw = colon >= 0 ? creds.substring(colon + 1) : creds;
      if (auth::checkPassword(pw)) { auth::noteSuccess(); return true; }
      auth::noteFailure();
      delay(400);
    }
  }
  httpd_resp_set_status(req, "401 Unauthorized");
  httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"" WUI_NAME "\", charset=\"UTF-8\"");
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, auth::lockedOut() ? "too many attempts — wait a minute\n" : "password required\n");
  return false;
}

// Card path for the request URI ("/dav/a%20b/" -> "/a b"), or false.
bool davPath(httpd_req_t *req, String &out) {
  String uri = req->uri;
  int q = uri.indexOf('?');
  if (q >= 0) uri = uri.substring(0, q);
  if (!uri.startsWith(kPrefix)) return false;
  String rest = uri.substring(strlen(kPrefix));
  if (rest.length() && rest[0] != '/') return false;
  return wui_safe_path(wui_url_decode(rest.length() ? rest.c_str() : "/").c_str(), out);
}

String hrefFor(const String &path, bool dir) {
  String h = String(kPrefix) + wui_url_encode_path(path);
  if (dir && !h.endsWith("/")) h += "/";
  return h;
}

String iso8601(time_t t) {
  struct tm g;
  gmtime_r(&t, &g);
  char b[32];
  strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", &g);
  return b;
}

String propXml(const String &path, bool dir, uint64_t size, time_t mtime, bool root) {
  String name = path == "/" ? String(WUI_NAME) : path.substring(path.lastIndexOf('/') + 1);
  String x = "<D:response><D:href>" + wui_xml_escape(hrefFor(path, dir)) + "</D:href><D:propstat><D:prop>";
  x += "<D:displayname>" + wui_xml_escape(name) + "</D:displayname>";
  if (dir) {
    x += "<D:resourcetype><D:collection/></D:resourcetype>";
  } else {
    x += "<D:resourcetype/><D:getcontentlength>" + String((unsigned long long)size) + "</D:getcontentlength>";
    x += "<D:getcontenttype>" + String(wui_mime_for(name)) + "</D:getcontenttype>";
    x += "<D:getetag>\"" + String((unsigned long long)size) + "-" + String((unsigned long)mtime) + "\"</D:getetag>";
  }
  if (mtime > 0) {
    x += "<D:getlastmodified>" + wui_http_date(mtime) + "</D:getlastmodified>";
    x += "<D:creationdate>" + iso8601(mtime) + "</D:creationdate>";
  }
  x += "<D:supportedlock><D:lockentry><D:lockscope><D:exclusive/></D:lockscope>"
       "<D:locktype><D:write/></D:locktype></D:lockentry></D:supportedlock>";
  if (root) {
    uint64_t total = storage::totalBytes(), used = storage::usedBytes();
    x += "<D:quota-available-bytes>" + String((unsigned long long)(total > used ? total - used : 0)) +
         "</D:quota-available-bytes><D:quota-used-bytes>" + String((unsigned long long)used) + "</D:quota-used-bytes>";
  }
  x += "</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>";
  return x;
}

esp_err_t replyStatus(httpd_req_t *req, const char *status) {
  httpd_resp_set_status(req, status);
  return httpd_resp_send(req, nullptr, 0);
}

bool statPath(const String &path, bool &dir, uint64_t &size, time_t &mtime) {
  storage::Guard g;
  if (path == "/") { dir = true; size = 0; mtime = 0; return true; }
  File f = SD.open(path);
  if (!f) return false;
  dir = f.isDirectory();
  size = dir ? 0 : f.size();
  mtime = f.getLastWrite();
  f.close();
  return true;
}

bool parentExists(const String &path) {
  String parent = path.substring(0, path.lastIndexOf('/'));
  if (parent.isEmpty()) return true;
  storage::Guard g;
  File f = SD.open(parent);
  bool ok = f && f.isDirectory();
  if (f) f.close();
  return ok;
}

// ------------------------------------------------------------------ methods

esp_err_t hOptions(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "DAV", "1, 2");
  httpd_resp_set_hdr(req, "MS-Author-Via", "DAV");
  httpd_resp_set_hdr(req, "Allow",
                     "OPTIONS, GET, HEAD, PUT, DELETE, PROPFIND, PROPPATCH, MKCOL, COPY, MOVE, LOCK, UNLOCK");
  return replyStatus(req, "200 OK");
}

esp_err_t hPropfind(httpd_req_t *req) {
  if (!davAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!davPath(req, path)) return replyStatus(req, "400 Bad Request");
  readBody(req, 4096);                                  // allprop, whatever was asked
  bool dir; uint64_t size; time_t mtime;
  if (!statPath(path, dir, size, mtime)) return replyStatus(req, "404 Not Found");
  bool depth0 = header(req, "Depth", 16) == "0";

  httpd_resp_set_status(req, "207 Multi-Status");
  httpd_resp_set_type(req, "application/xml; charset=\"utf-8\"");
  String out = "<?xml version=\"1.0\" encoding=\"utf-8\"?><D:multistatus xmlns:D=\"DAV:\">";
  out += propXml(path, dir, size, mtime, path == "/");
  if (dir && !depth0) {
    std::vector<storage::DirEntry> kids;
    { storage::Guard g; storage::listDir(path, kids); }
    for (const auto &k : kids) {
      out += propXml((path == "/" ? "/" : path + "/") + k.name, k.dir, k.size, k.mtime, false);
      if (out.length() > 1400) { httpd_resp_send_chunk(req, out.c_str(), out.length()); out = ""; }
    }
  }
  out += "</D:multistatus>";
  httpd_resp_send_chunk(req, out.c_str(), out.length());
  return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t hProppatch(httpd_req_t *req) {
  if (!davAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!davPath(req, path)) return replyStatus(req, "400 Bad Request");
  readBody(req, 8192);
  // Accept and ignore (Windows sets its own timestamps this way).
  String x = "<?xml version=\"1.0\" encoding=\"utf-8\"?><D:multistatus xmlns:D=\"DAV:\"><D:response><D:href>" +
             wui_xml_escape(hrefFor(path, false)) +
             "</D:href><D:propstat><D:prop/><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response></D:multistatus>";
  httpd_resp_set_status(req, "207 Multi-Status");
  httpd_resp_set_type(req, "application/xml; charset=\"utf-8\"");
  return httpd_resp_send(req, x.c_str(), x.length());
}

esp_err_t getOrHead(httpd_req_t *req, bool head) {
  if (!davAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!davPath(req, path)) return replyStatus(req, "400 Bad Request");
  bool dir; uint64_t size; time_t mtime;
  if (!statPath(path, dir, size, mtime)) return replyStatus(req, "404 Not Found");
  if (dir) {
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, head ? "" : WUI_NAME " WebDAV folder — open it with a WebDAV client.\n");
  }
  stream::FileOpts o;
  o.inlineView = true;            // real content type for players; sandboxed if active
  o.headOnly = head;
  return stream::sendFile(req, path, o);
}
esp_err_t hGet(httpd_req_t *req)  { return getOrHead(req, false); }
esp_err_t hHead(httpd_req_t *req) { return getOrHead(req, true); }

esp_err_t hPut(httpd_req_t *req) {
  if (!davAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!davPath(req, path) || path == "/") return replyStatus(req, "400 Bad Request");
  if (header(req, "Transfer-Encoding", 32).length())
    return replyStatus(req, "501 Not Implemented");   // chunked uploads: esp_http_server cannot read them
  if (!parentExists(path)) return replyStatus(req, "409 Conflict");
  bool existed;
  File f;
  {
    storage::Guard g;
    storage::closeUpload(true);
    existed = SD.exists(path);
    if (existed) {
      File probe = SD.open(path);
      bool isDir = probe && probe.isDirectory();
      if (probe) probe.close();
      if (isDir) return replyStatus(req, "405 Method Not Allowed");
    }
    f = SD.open(path, FILE_WRITE);
  }
  if (!f) return replyStatus(req, "500 Internal Server Error");
  size_t remaining = req->content_len;
  int timeouts = 0;
  bool ok = true;
  while (remaining > 0) {
    int got = httpd_req_recv(req, (char *)g_buf, std::min((size_t)WUI_IO_BUF, remaining));
    if (got == HTTPD_SOCK_ERR_TIMEOUT) { if (++timeouts > 2) { ok = false; break; } continue; }
    if (got <= 0) { ok = false; break; }
    timeouts = 0;
    size_t w;
    { storage::Guard g; w = f.write(g_buf, got); }
    if (w != (size_t)got) { ok = false; break; }
    remaining -= got;
    delay(0);
  }
  {
    storage::Guard g;
    f.close();
    if (!ok) SD.remove(path);         // never leave a silently truncated file
    storage::invalidateUsage();
  }
  if (!ok) return replyStatus(req, "500 Internal Server Error");
  return replyStatus(req, existed ? "204 No Content" : "201 Created");
}

esp_err_t hDelete(httpd_req_t *req) {
  if (!davAuth(req) || !requireSD(req)) return ESP_OK;
  String path, err;
  if (!davPath(req, path) || path == "/") return replyStatus(req, "403 Forbidden");
  storage::Guard g;
  storage::closeUpload(true);
  if (!SD.exists(path)) return replyStatus(req, "404 Not Found");
  return replyStatus(req, storage::removeRecursive(path, err) ? "204 No Content" : "500 Internal Server Error");
}

esp_err_t hMkcol(httpd_req_t *req) {
  if (!davAuth(req) || !requireSD(req)) return ESP_OK;
  String path;
  if (!davPath(req, path) || path == "/") return replyStatus(req, "405 Method Not Allowed");
  if (req->content_len) return replyStatus(req, "415 Unsupported Media Type");
  if (!parentExists(path)) return replyStatus(req, "409 Conflict");
  storage::Guard g;
  if (SD.exists(path)) return replyStatus(req, "405 Method Not Allowed");
  return replyStatus(req, SD.mkdir(path) ? "201 Created" : "500 Internal Server Error");
}

esp_err_t moveOrCopy(httpd_req_t *req, bool copy) {
  if (!davAuth(req) || !requireSD(req)) return ESP_OK;
  String from;
  if (!davPath(req, from) || from == "/") return replyStatus(req, "403 Forbidden");
  String destRaw = header(req, "Destination", 768), to;
  String decoded = wui_dav_destination(destRaw.c_str(), kPrefix);
  if (decoded.isEmpty() || !wui_safe_path(decoded.c_str(), to) || to == "/") return replyStatus(req, "400 Bad Request");
  if (to == from) return replyStatus(req, "403 Forbidden");
  if (to.startsWith(from + "/")) return replyStatus(req, "409 Conflict");
  bool overwrite = header(req, "Overwrite", 8) != "F";
  if (!parentExists(to)) return replyStatus(req, "409 Conflict");
  bool existed;
  String err;
  {
    storage::Guard g;
    storage::closeUpload(true);
    if (!SD.exists(from)) return replyStatus(req, "404 Not Found");
    existed = SD.exists(to);
    if (existed && !overwrite) return replyStatus(req, "412 Precondition Failed");
    if (existed && !storage::removeRecursive(to, err)) return replyStatus(req, "500 Internal Server Error");
    if (!copy) {
      bool ok = SD.rename(from, to);
      return replyStatus(req, !ok ? "500 Internal Server Error" : existed ? "204 No Content" : "201 Created");
    }
  }
  bool ok = storage::copyTree(from, to, g_buf, WUI_IO_BUF, nullptr, err);
  return replyStatus(req, !ok ? "500 Internal Server Error" : existed ? "204 No Content" : "201 Created");
}
esp_err_t hMove(httpd_req_t *req) { return moveOrCopy(req, false); }
esp_err_t hCopy(httpd_req_t *req) { return moveOrCopy(req, true); }

esp_err_t hLock(httpd_req_t *req) {
  if (!davAuth(req)) return ESP_OK;
  String path;
  if (!davPath(req, path)) return replyStatus(req, "400 Bad Request");
  readBody(req, 4096);
  // We never enforce locks (one user, one card); clients just need a token.
  static String token;
  char t[48];
  snprintf(t, sizeof(t), "opaquelocktoken:%08lx-%04lx-wui", (unsigned long)esp_random(),
           (unsigned long)(esp_random() & 0xffff));
  token = String("<") + t + ">";
  String x = "<?xml version=\"1.0\" encoding=\"utf-8\"?><D:prop xmlns:D=\"DAV:\"><D:lockdiscovery><D:activelock>"
             "<D:locktype><D:write/></D:locktype><D:lockscope><D:exclusive/></D:lockscope><D:depth>0</D:depth>"
             "<D:timeout>Second-3600</D:timeout><D:locktoken><D:href>" + String(t) +
             "</D:href></D:locktoken><D:lockroot><D:href>" + wui_xml_escape(hrefFor(path, false)) +
             "</D:href></D:lockroot></D:activelock></D:lockdiscovery></D:prop>";
  httpd_resp_set_hdr(req, "Lock-Token", token.c_str());
  httpd_resp_set_type(req, "application/xml; charset=\"utf-8\"");
  return httpd_resp_send(req, x.c_str(), x.length());
}

esp_err_t hUnlock(httpd_req_t *req) {
  if (!davAuth(req)) return ESP_OK;
  return replyStatus(req, "204 No Content");
}

}  // namespace

namespace api {

void registerDav(httpd_handle_t h) {
  struct { httpd_method_t m; esp_err_t (*fn)(httpd_req_t *); } routes[] = {
    {HTTP_OPTIONS, hOptions}, {HTTP_PROPFIND, hPropfind}, {HTTP_PROPPATCH, hProppatch},
    {HTTP_GET, hGet},         {HTTP_HEAD, hHead},         {HTTP_PUT, hPut},
    {HTTP_DELETE, hDelete},   {HTTP_MKCOL, hMkcol},       {HTTP_MOVE, hMove},
    {HTTP_COPY, hCopy},       {HTTP_LOCK, hLock},         {HTTP_UNLOCK, hUnlock},
  };
  for (auto &r : routes) {
    reg(h, "/dav", r.m, r.fn);
    reg(h, "/dav/*", r.m, r.fn);
  }
}

}  // namespace api

#else
namespace api { void registerDav(httpd_handle_t) {} }
#endif
