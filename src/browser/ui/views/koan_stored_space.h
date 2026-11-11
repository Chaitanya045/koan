// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_STORED_SPACE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_STORED_SPACE_H_

#include <stddef.h>

#include <optional>
#include <string>
#include <vector>

namespace koan {

// Which of the Spaces a fresh start recreated a stored entry belongs to (the
// folders in prefs::kFolderList, stored next to Spaces' kWorkspaceList).
// `space_ids` are the recreated Spaces' stored ids, in order
// (KoanWorkspaceController::GetStoredSpaceId()). The Space whose id is `id`,
// if it came back; otherwise the one at `place` (an entry from before ids,
// or one whose Space is gone), or the last one if fewer came back. nullopt
// without Spaces, or with neither a known id nor a place.
std::optional<size_t> StoredSpaceIndex(
    const std::vector<std::string>& space_ids,
    const std::string* id,
    std::optional<int> place);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_STORED_SPACE_H_
