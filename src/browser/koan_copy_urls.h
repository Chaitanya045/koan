// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_COPY_URLS_H_
#define KOAN_BROWSER_KOAN_COPY_URLS_H_

#include <string>
#include <vector>

namespace koan {

// A tab whose URL is copied: its title and its URL as copied (already
// stripped of tracking parameters).
struct CopiedTab {
  std::u16string title;
  std::u16string url;
};

// `title` as a Markdown link's text: backslashes, brackets and parentheses
// escaped, line breaks turned into spaces.
std::u16string EscapeMarkdownLinkText(const std::u16string& title);

// The clipboard text for Copy current URL with several tabs selected: one
// URL per line, or one "[title](url)" per line (an empty title shows the
// URL), in `tabs`' order. Tabs without a URL are left out. `count`, if
// given, is set to the number of lines.
std::u16string FormatCopiedUrls(const std::vector<CopiedTab>& tabs,
                                bool markdown,
                                size_t* count = nullptr);

}  // namespace koan

#endif  // KOAN_BROWSER_KOAN_COPY_URLS_H_
