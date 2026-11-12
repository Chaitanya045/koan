// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_IMPORTER_KOAN_IMPORTED_SPACES_H_
#define KOAN_BROWSER_IMPORTER_KOAN_IMPORTED_SPACES_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "url/gurl.h"

// The "Spaces and pinned tabs" import (#363): another browser's Spaces, their
// pinned tabs and folders, and its Essentials, as a plan that Koan's Spaces
// carry out (koan/browser/ui/views/koan_space_import.h), as Zen's import
// does with Arc's sidebar (ZenMigrationUtils' import plan). Read from Arc's
// StorableSidebar.json and Zen's zen-sessions.jsonlz4. Pure: unit-tested in
// //koan/test:koan_unittests.
namespace koan::importer {

// A pinned tab, or a folder of them.
struct ImportedItem {
  ImportedItem();
  ImportedItem(const ImportedItem&);
  ImportedItem& operator=(const ImportedItem&);
  ImportedItem(ImportedItem&&);
  ImportedItem& operator=(ImportedItem&&);
  ~ImportedItem();

  // A folder (with `children`), else a tab.
  bool is_folder = false;
  // A tab's page: the URL it was pinned on.
  GURL url;
  // A tab's page title, or a folder's name (empty: unnamed).
  std::u16string title;
  // A tab's own label (Zen's Change Label…), shown instead of its title.
  std::u16string label;
  // A folder shown collapsed.
  bool collapsed = false;
  std::vector<ImportedItem> children;
};

struct ImportedSpace {
  ImportedSpace();
  ImportedSpace(const ImportedSpace&);
  ImportedSpace& operator=(const ImportedSpace&);
  ImportedSpace(ImportedSpace&&);
  ImportedSpace& operator=(ImportedSpace&&);
  ~ImportedSpace();

  std::u16string name;
  // An emoji, or empty.
  std::u16string icon;
  // Its theme as Zen's theme object (ZenGradientGenerator's getTheme()) in
  // JSON, the way Koan stores a Space's theme (koan_space_theme.h); empty for
  // the default theme.
  std::string theme;
  // Its pinned tabs and folders, in order.
  std::vector<ImportedItem> items;
};

struct ImportedSpaces {
  ImportedSpaces();
  ImportedSpaces(const ImportedSpaces&);
  ImportedSpaces& operator=(const ImportedSpaces&);
  ImportedSpaces(ImportedSpaces&&);
  ImportedSpaces& operator=(ImportedSpaces&&);
  ~ImportedSpaces();

  bool empty() const { return essentials.empty() && spaces.empty(); }

  // The Essentials (Arc's Favorites). Arc's may be in folders, which the
  // Essentials don't have: their tabs go in order.
  std::vector<ImportedItem> essentials;
  std::vector<ImportedSpace> spaces;
};

// Arc's Spaces of profile `profile_dir_name` (its folder in "User Data",
// "Default" for the main one) from StorableSidebar.json: the "global"
// container's Spaces with their pinned containers (folders kept, collapsed;
// split views' tabs side by side; empty folders and pages Koan can't open
// left out), title (Arc's default Space "Personal", an untitled one
// "Space <n>"), emoji and window colours, and its Favorites (top apps).
// Little Arc windows' containers and unpinned tabs are left out.
ImportedSpaces ParseArcSpaces(std::string_view json,
                              std::string_view profile_dir_name);

// Zen's Spaces from its session file (zen-sessions.jsonlz4, decompressed;
// Zen 1.24's ZenSessionManager): every Space of `spaces` in order (name,
// emoji icon, theme), each with its pinned tabs (`tabs` with `pinned`, by
// `zenWorkspace`) at their pinned URL, in their folders (`folders`, nested
// by `parentId`; a split view's tabs in the folder holding it), and the
// Essentials (`zenEssential`). Unpinned tabs, Zen's empty tabs, Glance
// tabs and live folders are left out.
ImportedSpaces ParseZenSessions(std::string_view json);

// The same from a Zen profile folder's zen-sessions.jsonlz4 (blocking);
// empty if it has none or it can't be read.
ImportedSpaces ReadZenSpaces(const base::FilePath& profile_dir);

// Zen's session file, in a profile folder.
inline constexpr char kZenSessionsFile[] = "zen-sessions.jsonlz4";
// Arc's sidebar, beside its "User Data" folder.
inline constexpr char kArcSidebarFile[] = "StorableSidebar.json";

}  // namespace koan::importer

#endif  // KOAN_BROWSER_IMPORTER_KOAN_IMPORTED_SPACES_H_
