// Arduino String and friends for compiling the firmware's pure-logic modules
// (and, in test/hostsim, the whole web server) on a PC with g++.
//
// String is modelled on the Arduino class, not derived from std::string, so
// code that would not compile on the device does not compile here either.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <string>

class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const char *s, size_t n) : s_(s, n) {}
  String(const String &o) = default;
  String(String &&o) = default;
  explicit String(char c) : s_(1, c) {}
  String(int v) : s_(std::to_string(v)) {}
  String(unsigned v) : s_(std::to_string(v)) {}
  String(long v) : s_(std::to_string(v)) {}
  String(unsigned long v) : s_(std::to_string(v)) {}
  String(long long v) : s_(std::to_string(v)) {}
  String(unsigned long long v) : s_(std::to_string(v)) {}
  String(double v, unsigned places = 2) {
    char b[64];
    snprintf(b, sizeof(b), "%.*f", (int)places, v);
    s_ = b;
  }
  String &operator=(const String &o) = default;
  String &operator=(String &&o) = default;
  String &operator=(const char *s) { s_ = s ? s : ""; return *this; }

  const char *c_str() const { return s_.c_str(); }
  unsigned length() const { return (unsigned)s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  void reserve(unsigned n) { s_.reserve(n); }

  char operator[](unsigned i) const { return i < s_.size() ? s_[i] : 0; }
  char &operator[](unsigned i) { return s_[i]; }
  char charAt(unsigned i) const { return (*this)[i]; }
  void setCharAt(unsigned i, char c) { if (i < s_.size()) s_[i] = c; }

  bool concat(const char *p, unsigned n) { s_.append(p, n); return true; }
  bool concat(const String &o) { s_ += o.s_; return true; }
  bool concat(const char *p) { if (p) s_ += p; return true; }
  bool concat(char c) { s_ += c; return true; }
  template <typename T> bool concat(T v) { s_ += String(v).s_; return true; }
  String &operator+=(const String &o) { s_ += o.s_; return *this; }
  String &operator+=(const char *p) { if (p) s_ += p; return *this; }
  String &operator+=(char c) { s_ += c; return *this; }
  String &operator+=(int v) { return *this += String(v); }
  String &operator+=(unsigned v) { return *this += String(v); }
  String &operator+=(long v) { return *this += String(v); }
  String &operator+=(unsigned long v) { return *this += String(v); }
  String &operator+=(long long v) { return *this += String(v); }
  String &operator+=(unsigned long long v) { return *this += String(v); }

  bool equals(const String &o) const { return s_ == o.s_; }
  bool equals(const char *p) const { return s_ == (p ? p : ""); }
  bool equalsIgnoreCase(const String &o) const {
    if (s_.size() != o.s_.size()) return false;
    for (size_t i = 0; i < s_.size(); ++i)
      if (tolower((unsigned char)s_[i]) != tolower((unsigned char)o.s_[i])) return false;
    return true;
  }
  bool operator==(const String &o) const { return s_ == o.s_; }
  bool operator==(const char *p) const { return equals(p); }
  bool operator!=(const String &o) const { return s_ != o.s_; }
  bool operator!=(const char *p) const { return !equals(p); }
  bool operator<(const String &o) const { return s_ < o.s_; }
  int compareTo(const String &o) const { return s_.compare(o.s_); }

  int indexOf(char c, unsigned from = 0) const { return pos(s_.find(c, from)); }
  int indexOf(const String &t, unsigned from = 0) const { return pos(s_.find(t.s_, from)); }
  int indexOf(const char *t, unsigned from = 0) const { return pos(s_.find(t, from)); }
  int lastIndexOf(char c) const { return pos(s_.rfind(c)); }
  int lastIndexOf(char c, unsigned from) const { return pos(s_.rfind(c, from)); }
  int lastIndexOf(const String &t) const { return pos(s_.rfind(t.s_)); }
  bool startsWith(const String &t) const { return s_.compare(0, t.s_.size(), t.s_) == 0; }
  bool startsWith(const String &t, unsigned off) const {
    return off <= s_.size() && s_.compare(off, t.s_.size(), t.s_) == 0;
  }
  bool endsWith(const String &t) const {
    return s_.size() >= t.s_.size() && s_.compare(s_.size() - t.s_.size(), t.s_.size(), t.s_) == 0;
  }
  String substring(unsigned a) const { return substring(a, length()); }
  String substring(unsigned a, unsigned b) const {
    if (a > b) std::swap(a, b);
    if (a > s_.size()) return String();
    if (b > s_.size()) b = s_.size();
    return String(s_.substr(a, b - a).c_str(), b - a);
  }
  void remove(unsigned idx) { if (idx < s_.size()) s_.erase(idx); }
  void remove(unsigned idx, unsigned n) { if (idx < s_.size()) s_.erase(idx, n); }
  void replace(const String &a, const String &b) {
    if (a.s_.empty()) return;
    size_t p = 0;
    while ((p = s_.find(a.s_, p)) != std::string::npos) { s_.replace(p, a.s_.size(), b.s_); p += b.s_.size(); }
  }
  void replace(char a, char b) { for (auto &c : s_) if (c == a) c = b; }
  void trim() {
    size_t a = 0, b = s_.size();
    while (a < b && isspace((unsigned char)s_[a])) ++a;
    while (b > a && isspace((unsigned char)s_[b - 1])) --b;
    s_ = s_.substr(a, b - a);
  }
  void toLowerCase() { for (auto &c : s_) c = (char)tolower((unsigned char)c); }
  void toUpperCase() { for (auto &c : s_) c = (char)toupper((unsigned char)c); }
  long toInt() const { return strtol(s_.c_str(), nullptr, 10); }

  friend String operator+(const String &a, const String &b) { String r(a); r += b; return r; }
  friend String operator+(const String &a, const char *b) { String r(a); r += b; return r; }
  friend String operator+(const char *a, const String &b) { String r(a); r += b; return r; }
  friend String operator+(const String &a, char b) { String r(a); r += b; return r; }
  template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
  friend String operator+(const String &a, T b) { String r(a); r += String(b); return r; }

 private:
  static int pos(size_t p) { return p == std::string::npos ? -1 : (int)p; }
  std::string s_;
};

inline std::ostream &operator<<(std::ostream &o, const String &s) { return o << s.c_str(); }

#ifndef F
#define F(x) (x)
#endif

inline size_t wui_host_strlcpy(char *d, const char *s, size_t n) {
  size_t l = strlen(s);
  if (n) { size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; }
  return l;
}
#define strlcpy wui_host_strlcpy
