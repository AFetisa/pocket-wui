#pragma once
#include <Arduino.h>
#include <FS.h>
#include <functional>
#include <vector>

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
void  uploadBusy(bool busy); // an upload handler holds the handle (not idle, whatever the clock says)

bool removeRecursive(const String &path, String &err);

// One directory's children, read in full and closed again (FAT iteration is
// undefined while entries change underneath an open handle). Caller holds the lock.
struct DirEntry { String name; bool dir; uint64_t size; time_t mtime; };
bool listDir(const String &path, std::vector<DirEntry> &out);

// Moves a path into WUI_TRASH_DIR (a rename: instant, whatever the size) under
// a unique name that keeps the original. Caller holds the lock.
bool moveToTrash(const String &path, String &err);

// Recursive copy. Takes and releases the lock per chunk, so the web server stays
// responsive while a long copy runs on another task. `progress(bytes)` is called
// after each chunk; returning false from it cancels. `buf` is a caller-owned
// staging buffer. Refuses to copy a folder into itself.
using CopyProgress = std::function<bool(size_t)>;
bool copyTree(const String &from, const String &to, uint8_t *buf, size_t bufLen,
              const CopyProgress &progress, String &err);
// Total bytes under a path (files only), for progress bars. Locks per directory.
uint64_t treeBytes(const String &path);

// USB drive mode: while the card is lent to a USB host, nothing on the device
// may touch the filesystem (requireSD() refuses with 423).
bool lentToUsb();
void setLentToUsb(bool lent);

}  // namespace storage
