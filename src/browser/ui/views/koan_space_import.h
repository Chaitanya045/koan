// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_IMPORT_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_IMPORT_H_

#include <stddef.h>

class Profile;

namespace koan {

namespace importer {
struct ImportedSpaces;
}  // namespace importer

// The "Spaces and pinned tabs" import from Arc and Zen (#363; called by
// koan/browser/importer/koan_chromium_profile_importer.cc once `plan` is
// read). Makes the plan in `profile`'s most recently active window with
// Spaces, as Zen's import does (ZenMigrationUtils' applyArcSidebar()):
// - the Essentials first, at most Koan's 12; the others become pinned tabs
//   of the first imported Space (of the active one without any);
// - each Space at the end of the Spaces, not shown, with its name, emoji and
//   theme; the first one takes the active Space instead while that has no
//   pinned tabs and no folders (a fresh window's untouched Space);
// - its pinned tabs and folders (nested at most KoanFolderController's 5
//   deep; deeper ones' tabs go in the deepest folder), each tab unloaded,
//   with its pinned URL, title and label.
// Nothing changes on screen but the sidebar: the window stays on its tab.
// Returns how many Spaces it made or took; 0 without a window to make them
// in.
size_t ImportSpaces(Profile* profile, const importer::ImportedSpaces& plan);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_IMPORT_H_
