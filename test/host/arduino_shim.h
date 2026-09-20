// Minimal Arduino String/stubs so the pure-logic modules can be unit-tested
// on the host with g++ (see test/host/README.md).
#pragma once
#include <string>
#include <cstring>
#include <cstdio>

struct String : std::string {
  String() {}
  String(const char *s) : std::string(s ? s : "") {}
  String(const std::string &s) : std::string(s) {}
  int indexOf(char c, int from = 0) const { auto p = find(c, from); return p == npos ? -1 : (int)p; }
  String substring(int a, int b = -1) const {
    if (b < 0) b = (int)size();
    if (a > (int)size()) a = size();
    if (b > (int)size()) b = size();
    return String(std::string(substr(a, b - a)));
  }
  bool endsWith(const String &s) const { return size() >= s.size() && compare(size()-s.size(), s.size(), s)==0; }
  void reserve(size_t n) { std::string::reserve(n); }
  size_t length() const { return size(); }
};
inline String operator+(const String &a, const String &b) { return String(std::string(a) + std::string(b)); }
