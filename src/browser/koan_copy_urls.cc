// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_copy_urls.h"

#include "base/strings/strcat.h"

namespace koan {

namespace {

// A ")" or a space would end the link's destination early.
std::u16string EscapeMarkdownLinkUrl(const std::u16string& url) {
  std::u16string out;
  for (char16_t c : url) {
    if (c == u'(') {
      out += u"%28";
    } else if (c == u')') {
      out += u"%29";
    } else if (c == u' ') {
      out += u"%20";
    } else {
      out += c;
    }
  }
  return out;
}

}  // namespace

std::u16string EscapeMarkdownLinkText(const std::u16string& title) {
  std::u16string out;
  for (char16_t c : title) {
    switch (c) {
      case u'\\':
      case u'[':
      case u']':
      case u'(':
      case u')':
        out += u'\\';
        out += c;
        break;
      case u'\r':
      case u'\n':
        out += u' ';
        break;
      default:
        out += c;
    }
  }
  return out;
}

std::u16string FormatCopiedUrls(const std::vector<CopiedTab>& tabs,
                                bool markdown,
                                size_t* count) {
  std::u16string text;
  size_t lines = 0;
  for (const CopiedTab& tab : tabs) {
    if (tab.url.empty()) {
      continue;
    }
    if (lines++) {
      text += u'\n';
    }
    if (!markdown) {
      text += tab.url;
      continue;
    }
    base::StrAppend(
        &text,
        {u"[", EscapeMarkdownLinkText(tab.title.empty() ? tab.url : tab.title),
         u"](", EscapeMarkdownLinkUrl(tab.url), u")"});
  }
  if (count) {
    *count = lines;
  }
  return text;
}

}  // namespace koan
