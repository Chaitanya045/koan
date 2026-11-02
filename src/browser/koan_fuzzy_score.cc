// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenUBActionsProvider.sys.mjs (fuzzy scoring)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/koan_fuzzy_score.h"

#include <algorithm>

namespace koan {

int FuzzyScore(std::u16string_view target, std::u16string_view query) {
  // A VS Code-style score, as in Zen (ZenUBActionsProvider).
  if (target.empty() || query.empty() || query.size() > target.size()) {
    return 0;
  }
  if (target == query) {
    return kExactMatchScore;
  }
  if (target.starts_with(query)) {
    return kPrefixMatchScore + static_cast<int>(query.size());
  }
  int score = 0;
  size_t matched = 0;
  // -1 before the first match; Zen's run bonus then also counts a match at
  // index 0 as continuing a run.
  int last_match = -1;
  int run = 0;
  for (int i = 0; i < static_cast<int>(target.size()) && matched < query.size();
       ++i) {
    if (target[i] != query[matched]) {
      continue;
    }
    int bonus = 10;
    if (i == 0 || target[i - 1] == u' ' || target[i - 1] == u'-' ||
        target[i - 1] == u'_') {
      bonus += 15;  // Start of a word.
    }
    if (last_match == i - 1) {
      ++run;
      bonus += 20 * run;
    } else {
      run = 0;
    }
    if (last_match != -1) {
      bonus -= std::min(i - last_match - 1, 10);  // Gap since the last match.
    }
    score += bonus;
    last_match = i;
    ++matched;
  }
  return matched == query.size() ? score : 0;
}

}  // namespace koan
