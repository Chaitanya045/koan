// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_essentials_profiles.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/pinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_animating_layout_manager.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "components/prefs/pref_service.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_space_header.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace koan {

namespace {

const BrowserView* BrowserViewOf(const tabs::TabInterface* tab) {
  const BrowserWindowInterface* browser =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  return browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
}

// The tab a grid tile shows: the tile's own, or a split view's first.
const tabs::TabInterface* TabOfTile(const views::View* tile) {
  if (const auto* tab_view = views::AsViewClass<TabView>(tile)) {
    return tab_view->GetTabInterface();
  }
  for (const views::View* child : tile->children()) {
    if (const auto* tab_view = views::AsViewClass<TabView>(child)) {
      return tab_view->GetTabInterface();
    }
  }
  return nullptr;
}

}  // namespace

const containers::EssentialsProfiles& GetEssentialsProfiles(
    const BrowserView* browser_view) {
  static const base::NoDestructor<containers::EssentialsProfiles> kOneGrid;
  const KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  return controller && controller->workspaces()
             ? controller->workspaces()->essentials_profiles()
             : *kOneGrid;
}

std::optional<containers::ContainerId> GetEssentialsGrid(
    const tabs::TabInterface* tab) {
  if (!tab) {
    return std::nullopt;
  }
  return GetEssentialsProfiles(BrowserViewOf(tab))
      .GridOf(containers::ContainerOf(tab->GetContents()));
}

bool IsEssentialShown(const tabs::TabInterface* tab) {
  if (!tab || !tab->IsPinned()) {
    return true;
  }
  const containers::EssentialsProfiles& profiles =
      GetEssentialsProfiles(BrowserViewOf(tab));
  return !profiles.separate ||
         profiles.Shows(containers::ContainerOf(tab->GetContents()));
}

bool IsEssentialsTileShown(const views::View* tile) {
  return !tile || IsEssentialShown(TabOfTile(tile));
}

bool IsHiddenTab(const tabs::TabInterface* tab) {
  return IsEmptyTab(tab) || !IsEssentialShown(tab);
}

void SnapEssentialsGrid(TabStripView* strip) {
  PinnedTabContainerView* grid =
      strip ? strip->GetPinnedTabsContainer() : nullptr;
  if (!grid) {
    return;
  }
  if (!grid->GetProperty(kHasAnimatingLayoutManagerKey)) {
    grid->InvalidateLayout();
    return;
  }
  // A new target with every tile at its place in it at once: a tile coming
  // back would otherwise grow from its hidden, empty bounds.
  std::vector<const views::View*> tiles(grid->children().begin(),
                                        grid->children().end());
  static_cast<TabCollectionAnimatingLayoutManager*>(grid->GetLayoutManager())
      ->ResetViewsToTargetLayout(tiles);
}

// KoanWorkspaceController's part (koan_workspaces.h).

containers::EssentialsProfiles
KoanWorkspaceController::ComputeEssentialsProfiles() const {
  containers::EssentialsProfiles profiles;
  if (!IsEnabled() || destroying_ || !features::IsSidebarEnabled() ||
      !browser_view_->GetProfile()
           ->GetOriginalProfile()
           ->GetPrefs()
           ->GetBoolean(prefs::kWorkspacesSeparateEssentials)) {
    return profiles;
  }
  profiles.separate = true;
  for (const SpaceInfo& space : GetSpaces()) {
    const std::optional<containers::ContainerId> container =
        containers::SpaceContainerFromString(space.container);
    if (container && !std::ranges::contains(profiles.spaces, *container)) {
      profiles.spaces.push_back(*container);
    }
    if (space.active) {
      profiles.shown = container;
    }
  }
  return profiles;
}

void KoanWorkspaceController::UpdateEssentialsProfiles() {
  containers::EssentialsProfiles profiles = ComputeEssentialsProfiles();
  const std::optional<tab_groups::TabGroupId> space = GetActiveSpace();
  // The grid changed while the window stayed in the same Space: its profile
  // or the setting did.
  if (profiles != essentials_profiles_ && space && space == essentials_space_) {
    essentials_changed_in_place_ = true;
  }
  essentials_space_ = space;
  if (profiles == essentials_profiles_) {
    return;
  }
  essentials_profiles_ = std::move(profiles);
  if (header_) {
    SnapEssentialsGrid(views::AsViewClass<TabStripView>(header_->parent()));
  }
}

std::optional<containers::ContainerId>
KoanWorkspaceController::EssentialsGridOfSpace(
    const tab_groups::TabGroupId& space) const {
  if (!essentials_profiles_.separate) {
    return std::nullopt;
  }
  return containers::SpaceContainerFromString(GetSpaceContainer(space));
}

void KoanWorkspaceController::ShowActiveEssential() {
  const bool in_place = std::exchange(essentials_changed_in_place_, false);
  TabStripModel* model = tab_strip_model();
  tabs::TabInterface* active = model->GetActiveTab();
  if (!active || IsEssentialShown(active)) {
    return;
  }
  if (in_place) {
    // Its Space's profile (or the setting) changed under it: the Space's own
    // tab.
    if (const std::optional<tab_groups::TabGroupId> space = GetActiveSpace()) {
      if (tabs::TabInterface* tab = GetLastActiveTab(*space)) {
        model->ActivateTabAt(model->GetIndexOfTab(tab));
      }
    }
    return;
  }
  // Selected (Switch to Tab, an extension, a restored window's selection):
  // Zen's switchTabIfNeeded(), the first Space of the Essential's profile, on
  // the Essential (entering the Space selected one of its tabs).
  const std::optional<containers::ContainerId> grid =
      essentials_profiles_.GridOf(
          containers::ContainerOf(active->GetContents()));
  for (const SpaceInfo& space : GetSpaces()) {
    if (containers::SpaceContainerFromString(space.container) == grid) {
      SwitchTo(space.id);
      model->ActivateTabAt(model->GetIndexOfTab(active));
      return;
    }
  }
  // No Space shows its grid (every Space has another profile): it stays
  // active, its tile hidden.
}

}  // namespace koan
