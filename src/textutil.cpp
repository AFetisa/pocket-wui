#include "textutil.h"
#include "paths.h"
#include <ctype.h>
#include <string.h>

namespace {

struct MimeRow { const char *ext; const char *type; };
const MimeRow kMime[] = {
  {"txt", "text/plain"},        {"md", "text/markdown"},      {"markdown", "text/markdown"},
  {"log", "text/plain"},        {"csv", "text/csv"},          {"tsv", "text/tab-separated-values"},
  {"json", "application/json"}, {"xml", "application/xml"},   {"html", "text/html"},
  {"htm", "text/html"},         {"css", "text/css"},          {"js", "text/javascript"},
  {"ini", "text/plain"},        {"cfg", "text/plain"},        {"conf", "text/plain"},
  {"yaml", "text/plain"},       {"yml", "text/plain"},        {"toml", "text/plain"},
  {"c", "text/plain"},          {"h", "text/plain"},          {"cpp", "text/plain"},
  {"py", "text/plain"},         {"sh", "text/plain"},         {"gcode", "text/plain"},
  {"pdf", "application/pdf"},
  {"png", "image/png"},         {"jpg", "image/jpeg"},        {"jpeg", "image/jpeg"},
  {"gif", "image/gif"},         {"webp", "image/webp"},       {"bmp", "image/bmp"},
  {"ico", "image/x-icon"},      {"svg", "image/svg+xml"},     {"avif", "image/avif"},
  {"mp3", "audio/mpeg"},        {"wav", "audio/wav"},         {"ogg", "audio/ogg"},
  {"oga", "audio/ogg"},         {"opus", "audio/ogg"},        {"flac", "audio/flac"},
  {"m4a", "audio/mp4"},         {"aac", "audio/aac"},
  {"mp4", "video/mp4"},         {"m4v", "video/mp4"},         {"webm", "video/webm"},
  {"mov", "video/quicktime"},   {"ogv", "video/ogg"},
  {"zip", "application/zip"},   {"bin", "application/octet-stream"},
};

char lower(char c) { return (char)tolower((unsigned char)c); }

bool globMatch(const char *p, const char *s) {
  // Iterative '*' backtracking: linear-ish, no recursion on the task stack.
  const char *star = nullptr, *resume = nullptr;
  while (*s) {
    if (*p == '?' || (*p && *p != '*' && lower(*p) == lower(*s))) { ++p; ++s; continue; }
    if (*p == '*') { star = p++; resume = s; continue; }
    if (star) { p = star + 1; s = ++resume; continue; }
    return false;
  }
  while (*p == '*') ++p;
  return *p == 0;
}

int b64val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;
  if (c == '/' || c == '_') return 63;
  return -1;
}

bool unreserved(unsigned char c) {
  return isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

}  // namespace

const char *wui_mime_for(const String &name) {
  int dot = name.lastIndexOf('.');
  int slash = name.lastIndexOf('/');
  if (dot < 0 || dot < slash) return "application/octet-stream";
  String ext = name.substring(dot + 1);
  ext.toLowerCase();
  for (const auto &r : kMime)
    if (ext == r.ext) return r.type;
  return "application/octet-stream";
}

bool wui_mime_is_text(const char *m) {
  return strncmp(m, "text/", 5) == 0 || strcmp(m, "application/json") == 0 ||
         strcmp(m, "application/xml") == 0;
}

bool wui_mime_needs_sandbox(const char *m) {
  return strcmp(m, "text/html") == 0 || strcmp(m, "image/svg+xml") == 0 ||
         strcmp(m, "application/xml") == 0;
}

bool wui_name_matches(const char *name, const char *pattern) {
  if (!pattern || !*pattern) return true;
  if (strpbrk(pattern, "*?")) return globMatch(pattern, name);
  size_t n = strlen(name), m = strlen(pattern);
  for (size_t i = 0; i + m <= n; ++i) {
    size_t j = 0;
    while (j < m && lower(name[i + j]) == lower(pattern[j])) ++j;
    if (j == m) return true;
  }
  return false;
}

bool wui_b64_decode(const char *in, String &out) {
  out = "";
  uint32_t acc = 0;
  int bits = 0;
  for (; *in; ++in) {
    if (*in == '=') break;
    if (*in == ' ' || *in == '\r' || *in == '\n') continue;
    int v = b64val(*in);
    if (v < 0) return false;
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += (char)((acc >> bits) & 0xFF);
    }
  }
  return true;
}

String wui_http_date(time_t t) {
  static const char *kDay[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *kMon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  struct tm g;
  gmtime_r(&t, &g);
  char b[40];
  snprintf(b, sizeof(b), "%s, %02d %s %04d %02d:%02d:%02d GMT", kDay[g.tm_wday], g.tm_mday,
           kMon[g.tm_mon], g.tm_year + 1900, g.tm_hour, g.tm_min, g.tm_sec);
  return String(b);
}

String wui_xml_escape(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (unsigned i = 0; i < s.length(); ++i) {
    char c = s[i];
    switch (c) {
      case '&':  o += "&amp;"; break;
      case '<':  o += "&lt;"; break;
      case '>':  o += "&gt;"; break;
      case '"':  o += "&quot;"; break;
      case '\'': o += "&apos;"; break;
      default:   o += c;
    }
  }
  return o;
}

String wui_url_encode_path(const String &path) {
  static const char *hex = "0123456789ABCDEF";
  String o;
  o.reserve(path.length() + 16);
  for (unsigned i = 0; i < path.length(); ++i) {
    unsigned char c = (unsigned char)path[i];
    if (unreserved(c) || c == '/') { o += (char)c; continue; }
    o += '%';
    o += hex[c >> 4];
    o += hex[c & 15];
  }
  return o;
}

String wui_content_disposition(const String &name, bool inlineView) {
  String ascii;
  for (unsigned i = 0; i < name.length(); ++i) {
    unsigned char c = (unsigned char)name[i];
    ascii += (c < 0x20 || c > 0x7e || c == '"' || c == '\\') ? '_' : (char)c;
  }
  String enc = wui_url_encode_path(name);
  enc.replace("/", "%2F");
  return String(inlineView ? "inline" : "attachment") + "; filename=\"" + ascii +
         "\"; filename*=UTF-8''" + enc;
}

String wui_dav_destination(const char *header, const char *prefix) {
  if (!header) return String();
  const char *p = header;
  const char *scheme = strstr(p, "://");
  if (scheme) {
    p = strchr(scheme + 3, '/');
    if (!p) return String();
  }
  size_t n = strlen(prefix);
  if (strncmp(p, prefix, n) != 0) return String();
  p += n;
  if (*p && *p != '/') return String();
  String path = wui_url_decode(*p ? p : "/");
  return path;
}
