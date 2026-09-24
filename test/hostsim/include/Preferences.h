#pragma once
// In-memory NVS: settings live as long as the simulator process.
#include <Arduino.h>
#include <map>
#include <string>
#include <vector>
class Preferences {
 public:
  bool begin(const char *ns, bool = false) { ns_ = ns; return true; }
  void end() {}
  bool clear() { for (auto it = store().begin(); it != store().end();) it = it->first.rfind(ns_ + "/", 0) == 0 ? store().erase(it) : std::next(it); return true; }
  bool remove(const char *k) { return store().erase(key(k)) > 0; }
  size_t putString(const char *k, const String &v) { store()[key(k)] = v.c_str(); return v.length(); }
  String getString(const char *k, const String &d = String()) { auto it = store().find(key(k)); return it == store().end() ? d : String(it->second.c_str()); }
  size_t putBool(const char *k, bool v) { store()[key(k)] = v ? "1" : "0"; return 1; }
  bool getBool(const char *k, bool d = false) { auto it = store().find(key(k)); return it == store().end() ? d : it->second == "1"; }
  size_t putBytes(const char *k, const void *p, size_t n) { store()[key(k)] = std::string((const char *)p, n); return n; }
  size_t getBytesLength(const char *k) { auto it = store().find(key(k)); return it == store().end() ? 0 : it->second.size(); }
  size_t getBytes(const char *k, void *p, size_t n) {
    auto it = store().find(key(k)); if (it == store().end()) return 0;
    size_t c = std::min(n, it->second.size()); memcpy(p, it->second.data(), c); return c;
  }
 private:
  static std::map<std::string, std::string> &store() { static std::map<std::string, std::string> s; return s; }
  std::string key(const char *k) const { return ns_ + "/" + k; }
  std::string ns_;
};
