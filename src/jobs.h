#pragma once
#include <Arduino.h>
#include <vector>

// One background job at a time — copy, checksum, or firmware install — run on
// its own thread so the web server keeps answering. The UI polls status().
namespace jobs {

struct CopyItem { String from, to; };

bool startCopy(const std::vector<CopyItem> &items, String &err);
bool startHash(const String &path, String &err);
bool startFirmware(const String &path, String &err);   // flashes, then reboots

bool busy();
void cancel();
// {"kind":"copy","state":"running|done|error|idle","done":N,"total":N,
//  "label":"…","result":"…","error":"…"}
String statusJson();

}  // namespace jobs
