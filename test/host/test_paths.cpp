#include "arduino_shim.h"
#define WUI_MAX_PATH 192
#include "../../src/paths.cpp"
#include <cassert>
#include <iostream>

static int fails = 0;
static void expectOk(const char *in, const char *want) {
  String out;
  bool ok = wui_safe_path(in, out);
  if (!ok || out != String(want)) {
    std::cout << "FAIL  " << in << " -> " << (ok ? out.c_str() : "<rejected>")
              << "  (want " << want << ")\n";
    fails++;
  }
}
static void expectReject(const char *in) {
  String out;
  if (wui_safe_path(in, out)) {
    std::cout << "FAIL  " << in << " should be rejected, got " << out.c_str() << "\n";
    fails++;
  }
}

int main() {
  expectOk("/", "/");
  expectOk("", "/");
  expectOk("notes.txt", "/notes.txt");
  expectOk("/a//b///c", "/a/b/c");
  expectOk("/a/b/", "/a/b");
  expectOk("/a/./b", "/a/b");
  expectOk("/a/b/..", "/a");                 // stays inside the card
  expectOk("/../../etc/passwd", "/etc/passwd");
  expectOk("/a/../../..", "/");
  expectOk("/dir/sub/file name (1).bin", "/dir/sub/file name (1).bin");
  expectReject("/a\\b");                     // backslash
  expectReject("/a:b");                      // FAT-illegal
  expectReject("/a\x01b");                   // control char
  expectReject("/a*b");
  expectReject("/a?b");
  {  // 64-char segment limit and total length limit
    String longSeg = "/";
    for (int i = 0; i < 70; ++i) longSeg = longSeg + "x";
    expectReject(longSeg.c_str());
    String deep;
    for (int i = 0; i < 30; ++i) deep = deep + "/dir";
    expectReject(deep.c_str());
  }
  // url decoding
  if (wui_url_decode("%2Ffoo%20bar%2Ebin") != String("/foo bar.bin")) { std::cout << "FAIL decode\n"; fails++; }
  if (wui_url_decode("100%") != String("100%")) { std::cout << "FAIL decode trailing %\n"; fails++; }

  std::cout << (fails ? "FAILURES: " : "all path tests passed") << (fails ? std::to_string(fails) : "") << "\n";
  return fails ? 1 : 0;
}
