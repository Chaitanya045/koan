// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenFolder.mjs
//   ZenFolders.mjs
//   zen-folders.ftl
//   zen-workspaces.ftl
//   zen-vertical-tabs.ftl
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_FOLDER_ZEN_ASSETS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_FOLDER_ZEN_ASSETS_H_

#include <string>
#include <string_view>

#include "base/time/time.h"

class SkPathBuilder;

namespace koan {

// Zen's folder icon, in its 28x28 viewBox: the back (the folder with its
// tab), the front (its cover), and the three dots on the cover while a
// collapsed folder shows its active tab.
void AddZenFolderBack(SkPathBuilder& path);
void AddZenFolderFront(SkPathBuilder& path);
void AddZenFolderDots(SkPathBuilder& path);

// Zen's formatRelativeTime(): "Just now", "5 minutes ago", ...
std::u16string FormatRelativeTime(base::Time time);

// Zen's labels: the folder menu (zen-folders.ftl), the Spaces' "+" menu
// (zen-workspaces.ftl) and the tab menu (zen-vertical-tabs.ftl), whose "New
// Folder" is also a new folder's name.
inline constexpr char16_t kZenRenameFolder[] = u"Rename Folder…";
inline constexpr char16_t kZenUnloadAllTabs[] = u"Unload All Tabs";
inline constexpr char16_t kZenNewSubfolder[] = u"New Subfolder…";
inline constexpr char16_t kZenChangeSpace[] = u"Change Space";
inline constexpr char16_t kZenConvertFolderToSpace[] =
    u"Convert folder to Space";
inline constexpr char16_t kZenUnpackFolder[] = u"Unpack Folder";
inline constexpr char16_t kZenDeleteFolder[] = u"Delete Folder";
inline constexpr char16_t kZenCreateFolder[] = u"Create Folder";
inline constexpr char16_t kZenMoveToFolder[] = u"Move to Folder";
inline constexpr char16_t kZenNewFolder[] = u"New Folder";

// The folder search's placeholder ("Search <folder>...") and its text when
// nothing matches.
std::u16string FolderSearchPlaceholder(std::u16string_view folder_name);
inline constexpr char16_t kFolderSearchNoResults[] =
    u"No tabs matching that search 🤔";

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_FOLDER_ZEN_ASSETS_H_
