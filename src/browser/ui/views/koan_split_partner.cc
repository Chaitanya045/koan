// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_split_partner.h"

namespace koan {

std::optional<size_t> PickSplitPartner(
    const std::vector<SplitPartnerCandidate>& tabs,
    size_t active) {
  if (active >= tabs.size()) {
    return std::nullopt;
  }
  const bool pinned = tabs[active].pinned;
  for (size_t step = 1; step < tabs.size(); ++step) {
    const size_t i = (active + step) % tabs.size();
    const SplitPartnerCandidate& tab = tabs[i];
    if (tab.in_active_space && !tab.hidden && !tab.split &&
        tab.pinned == pinned) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace koan
