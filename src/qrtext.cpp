#include "qrtext.h"

namespace {
void appendEscaped(String &out, const char *s) {
  for (; *s; ++s) {
    if (*s == '\\' || *s == ';' || *s == ',' || *s == ':' || *s == '"') out += '\\';
    out += *s;
  }
}
}  // namespace

String wui_wifi_qr(const char *ssid, const char *pass) {
  String out;
  out.reserve(32 + strlen(ssid) + strlen(pass));
  out += (pass && *pass) ? "WIFI:T:WPA;S:" : "WIFI:T:nopass;S:";
  appendEscaped(out, ssid);
  out += ';';
  if (pass && *pass) {
    out += "P:";
    appendEscaped(out, pass);
    out += ';';
  }
  out += ';';
  return out;
}
