// PocketWUI host simulator: the firmware's real web server, file handling,
// WebDAV, ZIP and jobs code, running on a PC against a folder standing in for
// the SD card. Only the radio, USB, screen and flash are faked (sim_stubs.cpp).
//
//   make -C test/hostsim run            # http://127.0.0.1:8791/  password test1234
//   ./test/hostsim/pocketwui-sim --root /tmp/card --port 8791 --password hunter22
#include <Arduino.h>
#include <SD.h>
#include "../../src/auth.h"
#include "../../src/server.h"
#include "../../src/storage.h"

extern int wui_sim_verbose;
extern int wui_sim_port;

int main(int argc, char **argv) {
  String password = "test1234";
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--root" && i + 1 < argc) wui_sim_root = argv[++i];
    else if (a == "--port" && i + 1 < argc) wui_sim_port = atoi(argv[++i]);
    else if (a == "--password" && i + 1 < argc) password = argv[++i];
    else if (a == "-v") wui_sim_verbose = 1;
    else { fprintf(stderr, "usage: %s [--root DIR] [--port N] [--password PW] [-v]\n", argv[0]); return 2; }
  }
  auth::begin();
  String err;
  if (!auth::setPassword(password, err)) { fprintf(stderr, "password: %s\n", err.c_str()); return 2; }
  if (!storage::begin()) { fprintf(stderr, "cannot use %s as the card\n", wui_sim_root.c_str()); return 1; }
  if (!server::begin()) return 1;
  printf("PocketWUI simulator on http://127.0.0.1:%d/  card=%s  password=%s\n", wui_sim_port,
         wui_sim_root.c_str(), password.c_str());
  fflush(stdout);
  for (;;) {
    storage::tickUploadIdle();
    delay(20);
  }
}
