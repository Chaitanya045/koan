// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_library_logic.h"

#include <string.h>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "url/gurl.h"

namespace koan::library {

std::u16string FormatUrl(const GURL& url) {
  std::string text = url.spec();
  for (const char* prefix : {"https://", "http://"}) {
    if (base::StartsWith(text, prefix)) {
      text.erase(0, strlen(prefix));
      if (base::StartsWith(text, "www.")) {
        text.erase(0, 4);
      }
      break;
    }
  }
  if (base::EndsWith(text, "/")) {
    text.pop_back();
  }
  return base::UTF8ToUTF16(text);
}

std::u16string SiteOf(const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return std::u16string();
  }
  std::string host(url.host());
  if (base::StartsWith(host, "www.")) {
    host.erase(0, 4);
  }
  return base::UTF8ToUTF16(host);
}

int DropSlot(base::span<const int> centers, int drag_index, int center) {
  int slot = 0;
  for (int i = 0; i < static_cast<int>(centers.size()); ++i) {
    if (i != drag_index && center > centers[i]) {
      ++slot;
    }
  }
  return slot;
}

int CardShift(int index, int drag_index, int drop_index, int step) {
  if (index == drag_index) {
    return 0;
  }
  if (drag_index < index && index <= drop_index) {
    return -step;
  }
  if (drop_index <= index && index < drag_index) {
    return step;
  }
  return 0;
}

}  // namespace koan::library
