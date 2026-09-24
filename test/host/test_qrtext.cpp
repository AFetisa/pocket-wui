#include "arduino_shim.h"
#include "../../src/qrtext.cpp"
#include <iostream>

static int fails = 0;
static void expect(const char *ssid, const char *pass, const char *want) {
  String got = wui_wifi_qr(ssid, pass);
  if (got != String(want)) {
    std::cout << "FAIL  [" << ssid << "] [" << pass << "] -> " << got << "  (want " << want << ")\n";
    fails++;
  }
}

int main() {
  expect("Cardputer-WUI", "k3yk3yk3yk", "WIFI:T:WPA;S:Cardputer-WUI;P:k3yk3yk3yk;;");
  expect("a;b,c", "p:q\\r\"s", "WIFI:T:WPA;S:a\\;b\\,c;P:p\\:q\\\\r\\\"s;;");
  expect("open net", "", "WIFI:T:nopass;S:open net;;");
  std::cout << (fails ? "qrtext tests FAILED\n" : "qrtext tests ok\n");
  return fails ? 1 : 0;
}
