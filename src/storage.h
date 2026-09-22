#pragma once
#include <Arduino.h>
#include <FS.h>

namespace storage {

// Mounts the microSD over SPI using the pin map M5Unified reports for the
// detected board — nothing here is hardcoded per-board.
bool begin();
bool mounted();
uint32_t clockHz();       // SPI clock the card passed its probe at; 0 if unmounted
bool remount();

uint64_t totalBytes();
uint64_t usedBytes();
const char *cardType();
void invalidateUsage();   // after a write, so the next status call re-samples

// All SD access is serialised: an upload and a directory listing will overlap.
void lock();
void unlock();

struct Guard {
  Guard()  { lock(); }
  ~Guard() { unlock(); }
};

// Append-only upload handle cached between chunks of the same file.
// Returns the handle positioned at `offset`, or a falsy File on mismatch
// (in which case *actual is set to the real size so the client can resume).
File *uploadHandle(const String &path, uint64_t offset, uint64_t *actual, String &err);
void  uploadWrote(size_t n);   // report accepted bytes. The handle tracks its own
                               // size: File::size() re-stats the path, and FATFS
                               // only updates that on flush, so mid-upload it lies.
void  closeUpload(bool force = false);
void  tickUploadIdle();      // call from loop(): closes an idle handle

bool removeRecursive(const String &path, String &err);

}  // namespace storage
