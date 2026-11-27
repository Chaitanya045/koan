// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_BOOKMARK_SPACES_RULE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_BOOKMARK_SPACES_RULE_H_

#include <set>
#include <string>
#include <string_view>
#include <vector>

// Zen's bookmarks per Space (ZenSpaceBookmarksStorage.js, gZenWorkspaces.
// isBookmarkInAnotherWorkspace()): a bookmark or a folder can belong to some
// of the Spaces, and the bookmarks toolbar and menus show those of the
// active Space and those that belong to none. Plain strings here, so the
// rule is unit tested on its own; koan_bookmark_spaces.h applies it to the
// bookmarks of a window. Design: .agent/notes/upstream/bookmarks-per-space.md.
namespace koan::bookmark_spaces {

// The BookmarkNode meta info key (kept in the Bookmarks file with the
// bookmark) of the Spaces a bookmark or folder belongs to: their sync ids
// (window_sync::SpaceRecord::id, the same in every window), comma-separated.
// No key: the bookmark belongs to no Space and shows in every one.
inline constexpr char kMetaInfoKey[] = "koan_spaces";

// What a window shows: the sync id of its active Space, and the ids of the
// profile's Spaces (window_sync::WindowSyncModel::spaces()).
struct Filter {
  std::string active;
  std::set<std::string> known;
};

// The ids in a kMetaInfoKey value, in order, without empty or repeated ones.
std::vector<std::string> Parse(std::string_view value);
// The kMetaInfoKey value for `ids` (empty for none: the key is removed).
std::string Serialize(const std::vector<std::string>& ids);

// Whether a bookmark or folder whose kMetaInfoKey value is `value` shows
// under `filter`: unless it belongs to other Spaces and not to the active one
// (Zen's rule). Ids of Spaces that are gone don't count, so a bookmark left
// with no Space shows everywhere.
bool IsShown(std::string_view value, const Filter& filter);

// `value` without `space` (a deleted Space).
std::string Without(std::string_view value, std::string_view space);

}  // namespace koan::bookmark_spaces

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_BOOKMARK_SPACES_RULE_H_
