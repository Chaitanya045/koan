// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_stored_space.h"

#include <algorithm>

namespace koan {

std::optional<size_t> StoredSpaceIndex(
    const std::vector<std::string>& space_ids,
    const std::string* id,
    std::optional<int> place) {
  if (space_ids.empty()) {
    return std::nullopt;
  }
  if (id && !id->empty()) {
    if (const auto it = std::ranges::find(space_ids, *id);
        it != space_ids.end()) {
      return static_cast<size_t>(it - space_ids.begin());
    }
  }
  if (!place || *place < 0) {
    return std::nullopt;
  }
  return std::min(static_cast<size_t>(*place), space_ids.size() - 1);
}

}  // namespace koan
