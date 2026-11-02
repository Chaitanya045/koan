// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_SPACE_TITLE_H_
#define KOAN_BROWSER_KOAN_SPACE_TITLE_H_

#include <string>
#include <utility>

namespace koan {

// A Space is stored as a tab group, with its emoji icon and name in the
// group's title ("<icon> <name>"). These two convert between the title and
// its parts.

// Splits a group title into its emoji icon (empty for a plain-text name)
// and name. A leading emoji is one grapheme cluster, so ZWJ sequences and
// flags stay whole; a symbol that is text by default (e.g. (c), arrows)
// is only an icon with the emoji variation selector.
std::pair<std::u16string, std::u16string> SplitSpaceTitle(
    const std::u16string& title);

// The inverse of SplitSpaceTitle(): "<icon> <name>", or just one of them
// when the other is empty.
std::u16string JoinSpaceTitle(const std::u16string& icon,
                              const std::u16string& name);

}  // namespace koan

#endif  // KOAN_BROWSER_KOAN_SPACE_TITLE_H_
