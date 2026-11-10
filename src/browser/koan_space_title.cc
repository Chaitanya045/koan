// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_space_title.h"

#include "base/i18n/break_iterator.h"
#include "base/i18n/char_iterator.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "third_party/icu/source/common/unicode/uchar.h"

namespace koan {

namespace {

// Whether `grapheme` (one grapheme cluster) is an emoji: a flag, a
// pictograph shown as an emoji by default, or one made an emoji with the
// variation selector or in a ZWJ sequence. Symbols that are text by default,
// like (c), (R), TM and arrows, start plain names.
bool IsEmoji(const std::u16string& grapheme) {
  if (grapheme.empty()) {
    return false;
  }
  const int32_t first = base::i18n::UTF16CharIterator(grapheme).get();
  if (u_hasBinaryProperty(first, UCHAR_REGIONAL_INDICATOR) ||
      u_hasBinaryProperty(first, UCHAR_EMOJI_PRESENTATION)) {
    return true;
  }
  return u_hasBinaryProperty(first, UCHAR_EXTENDED_PICTOGRAPHIC) &&
         (grapheme.find(u'\uFE0F') != std::u16string::npos ||
          grapheme.find(u'\u200D') != std::u16string::npos);
}

}  // namespace

std::pair<std::u16string, std::u16string> SplitSpaceTitle(
    const std::u16string& raw_title) {
  std::u16string title;
  base::TrimWhitespace(raw_title, base::TRIM_ALL, &title);
  base::i18n::BreakIterator iter(title,
                                 base::i18n::BreakIterator::BREAK_CHARACTER);
  if (title.empty() || !iter.Init() || !iter.Advance()) {
    return {std::u16string(), title};
  }
  std::u16string first(iter.GetString());
  if (!IsEmoji(first)) {
    return {std::u16string(), title};
  }
  std::u16string name;
  base::TrimWhitespace(title.substr(first.size()), base::TRIM_LEADING, &name);
  return {first, name};
}

std::u16string JoinSpaceTitle(const std::u16string& icon,
                              const std::u16string& name) {
  if (icon.empty() || name.empty()) {
    return icon + name;
  }
  return base::StrCat({icon, u" ", name});
}

}  // namespace koan
