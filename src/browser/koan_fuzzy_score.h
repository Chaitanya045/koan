// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenUBActionsProvider.sys.mjs (fuzzy scoring)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#ifndef KOAN_BROWSER_KOAN_FUZZY_SCORE_H_
#define KOAN_BROWSER_KOAN_FUZZY_SCORE_H_

#include <string_view>

namespace koan {

// Exact and prefix matches outrank any fuzzy match.
inline constexpr int kExactMatchScore = 1'000'000;
inline constexpr int kPrefixMatchScore = 100'000;

// Zen's fuzzy score of `query` against `target`, both lower case: exact and
// prefix matches win, then matches at word starts and in runs. 0 for no
// match. The command palette ranks its actions by it.
int FuzzyScore(std::u16string_view target, std::u16string_view query);

}  // namespace koan

#endif  // KOAN_BROWSER_KOAN_FUZZY_SCORE_H_
