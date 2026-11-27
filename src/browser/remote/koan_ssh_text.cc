// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_text.h"

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"

namespace koan::remote {

namespace {

bool IsBidiControl(char16_t c) {
  return c == 0x061C || c == 0x200E || c == 0x200F ||
         (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069);
}

}  // namespace

std::string SanitizeSshText(std::string_view text,
                            size_t max_bytes,
                            bool keep_newlines) {
  const std::string_view cut = base::TruncateUTF8ToByteSize(text, max_bytes);
  // Invalid sequences come out as U+FFFD.
  std::u16string wide;
  base::UTF8ToUTF16(cut.data(), cut.size(), &wide);
  std::u16string clean;
  clean.reserve(wide.size());
  for (char16_t c : wide) {
    if (c == u'\n' && keep_newlines) {
      clean.push_back(c);
    } else if (c == u'\t') {
      clean.push_back(u' ');
    } else if (c < 0x20 || (c >= 0x7F && c < 0xA0) || IsBidiControl(c)) {
      continue;
    } else {
      clean.push_back(c);
    }
  }
  return base::UTF16ToUTF8(clean);
}

}  // namespace koan::remote
