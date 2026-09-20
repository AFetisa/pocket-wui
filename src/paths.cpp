#include "paths.h"
#include "config.h"

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

String wui_url_decode(const char *src) {
  String out;
  if (!src) return out;
  out.reserve(strlen(src));
  for (const char *p = src; *p; ++p) {
    if (*p == '%' && p[1] && p[2]) {
      int hi = hexval(p[1]), lo = hexval(p[2]);
      if (hi >= 0 && lo >= 0) { out += (char)((hi << 4) | lo); p += 2; continue; }
    }
    out += *p;
  }
  return out;
}

bool wui_safe_path(const char *raw, String &out) {
  if (!raw) return false;
  String in = raw;
  if (in.length() == 0) in = "/";
  if (in[0] != '/') in = "/" + in;

  // Split on '/', resolving "." and ".." and rejecting anything suspicious.
  String segs[24];
  int n = 0;
  int i = 0;
  while (i < (int)in.length()) {
    int j = in.indexOf('/', i);
    if (j < 0) j = in.length();
    String s = in.substring(i, j);
    i = j + 1;
    if (s.length() == 0 || s == ".") continue;
    if (s == "..") { if (n > 0) n--; continue; }   // clamp at root, never escape
    if (s.length() > 64) return false;
    for (size_t k = 0; k < s.length(); ++k) {
      unsigned char c = (unsigned char)s[k];
      if (c < 0x20 || c == 0x7f || c == '\\' || c == ':' || c == '*' ||
          c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
    }
    if (n >= 24) return false;
    segs[n++] = s;
  }

  out = "";
  for (int k = 0; k < n; ++k) { out += '/'; out += segs[k]; }
  if (out.length() == 0) out = "/";
  return out.length() < WUI_MAX_PATH;
}
