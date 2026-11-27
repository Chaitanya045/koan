// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_bookmark_spaces_rule.h"

#include <algorithm>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace koan::bookmark_spaces {

std::vector<std::string> Parse(std::string_view value) {
  std::vector<std::string> ids;
  for (std::string_view id : base::SplitStringPiece(
           value, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (std::ranges::find(ids, id) == ids.end()) {
      ids.emplace_back(id);
    }
  }
  return ids;
}

std::string Serialize(const std::vector<std::string>& ids) {
  std::vector<std::string> kept;
  for (const std::string& id : ids) {
    // A sync id is a UUID, so never has a comma; anything else is dropped
    // rather than read back as two ids.
    if (!id.empty() && id.find(',') == std::string::npos &&
        std::ranges::find(kept, id) == kept.end()) {
      kept.push_back(id);
    }
  }
  return base::JoinString(kept, ",");
}

bool IsShown(std::string_view value, const Filter& filter) {
  bool in_other = false;
  for (const std::string& id : Parse(value)) {
    if (id == filter.active) {
      return true;
    }
    in_other = in_other || filter.known.contains(id);
  }
  return !in_other;
}

std::string Without(std::string_view value, std::string_view space) {
  std::vector<std::string> ids = Parse(value);
  std::erase(ids, space);
  return Serialize(ids);
}

}  // namespace koan::bookmark_spaces
