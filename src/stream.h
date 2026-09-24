#pragma once
#include <Arduino.h>
#include <esp_http_server.h>
#include <vector>

// Long responses — file downloads, media previews, folder ZIPs — are handed off
// to a background thread (esp_http_server's async requests), so one big
// download never freezes the rest of the UI. When WUI_STREAM_WORKERS are all
// busy the request is simply served inline, as before.
namespace stream {

struct FileOpts {
  bool inlineView = false;   // real content type for preview (sandbox CSP for HTML/SVG/XML)
  bool headOnly = false;     // HEAD
  uint64_t maxBytes = 0;     // > 0 truncates the body (console `cat`)
  bool capOpenRanges = false;// answer "bytes=N-" with at most WUI_RANGE_CAP (browser media)
};

// GET/HEAD of a card file with Range support; an attachment unless inlineView.
esp_err_t sendFile(httpd_req_t *req, const String &path, const FileOpts &o);

// Streams the given card paths (files and/or folders) as one stored ZIP.
esp_err_t sendZip(httpd_req_t *req, const std::vector<String> &paths, const String &zipName);

int active();   // background streams in flight

}  // namespace stream
