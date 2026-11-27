// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_PROFILES_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_PROFILES_H_

#include <optional>

#include "koan/browser/containers/koan_user_containers.h"

class BrowserView;
class TabStripView;

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace views {
class View;
}  // namespace views

namespace koan {

// Zen's container-specific Essentials (zen.workspaces.separate-essentials,
// on by default; prefs::kWorkspacesSeparateEssentials, #479): every profile
// (a Space's container, #318) has a grid of its own, and the window shows the
// active Space's. Spaces with the same profile share a grid; a Space without
// a profile shows the Essentials of the default one. An Essential keeps the
// partition it had when it was pinned (window sync and session restore keep
// it), so its profile is the one its tab is in. Its grid is that profile's
// if a Space of the window has it, else the default one's
// (containers::EssentialsProfiles).
//
// Zen's limit of 12 is counted per grid (KoanPinnedTabController), a tab
// becomes an Essential only in its own profile's grid while that grid shows
// (Zen's canEssentialBeAdded()), and ⌘1-⌘9 and Ctrl+Tab count only the
// Essentials shown (IsHiddenTab(), koan_spaces.h). Hidden tiles are left out
// of the grid's layout (koan_essentials.h).

// The grids of `browser_view`'s window (KoanWorkspaceController::
// essentials_profiles()): one grid with every Essential when the setting is
// off or the window has no Spaces.
const containers::EssentialsProfiles& GetEssentialsProfiles(
    const BrowserView* browser_view);

// The grid `tab` is in, as an Essential or as a tab that would become one, in
// the window it is in (nullopt: the default profile's, and the one grid with
// the setting off).
std::optional<containers::ContainerId> GetEssentialsGrid(
    const tabs::TabInterface* tab);

// Whether `tab` shows in its window's sidebar as far as its profile goes:
// false only for an Essential of a grid the window doesn't show.
bool IsEssentialShown(const tabs::TabInterface* tab);

// Whether the Essentials grid's child `tile` (a tab, or a split view of two)
// shows.
bool IsEssentialsTileShown(const views::View* tile);

// Lays `strip`'s Essentials grid out again at once, without animating: the
// tiles of the grid now shown take their places and the others hide, as the
// grid changes with the Space (or the setting).
void SnapEssentialsGrid(TabStripView* strip);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_PROFILES_H_
