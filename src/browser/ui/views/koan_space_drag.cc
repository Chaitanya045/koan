// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_drag.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/pinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_context.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h"
#include "components/tabs/public/tab_group.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/views/koan_essentials_promo.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_space_bar.h"
#include "koan/browser/ui/views/koan_space_drag_logic.h"
#include "koan/browser/ui/views/koan_space_header.h"
#include "koan/browser/ui/views/koan_space_navigation.h"
#include "koan/browser/ui/views/koan_tab_drag_hooks.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/models/list_selection_model.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/range/range.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// How often a running drag is checked for having ended out of sight.
constexpr base::TimeDelta kWatchInterval = base::Milliseconds(250);

gfx::Point InView(const views::View* from,
                  const views::View* to,
                  const gfx::Point& point) {
  gfx::Point converted = point;
  views::View::ConvertPointToTarget(from, to, &converted);
  return converted;
}

// Half the gap between two Essentials tiles (Zen's 4px), where the drop
// indicator goes between them.
constexpr int kTileGapHalf = 2;

// A drop place: a tab's row or tile (a split view's two tabs as one), in the
// sidebar region's coordinates.
struct DropUnit {
  tabs::TabHandle first;
  gfx::Rect bounds;
};

// The shown rows or tiles of the tabs at `indices` (in model order).
std::vector<DropUnit> DropUnits(TabStripModel* model,
                                TabDragContext* context,
                                const views::View* region,
                                const gfx::Range& indices) {
  std::vector<DropUnit> units;
  std::optional<split_tabs::SplitTabId> last_split;
  for (size_t index = indices.start(); index < indices.end(); ++index) {
    tabs::TabInterface* tab = model->GetTabAtIndex(static_cast<int>(index));
    views::View* view =
        tab ? context->GetTabForContents(tab->GetContents()) : nullptr;
    if (!view || !view->IsDrawn()) {
      continue;
    }
    const gfx::Rect bounds =
        views::View::ConvertRectToTarget(view, region, view->GetLocalBounds());
    if (tab->GetSplit() && tab->GetSplit() == last_split && !units.empty()) {
      units.back().bounds.Union(bounds);
      continue;
    }
    last_split = tab->GetSplit();
    units.push_back({tab->GetHandle(), bounds});
  }
  return units;
}

std::vector<gfx::Rect> UnitBounds(const std::vector<DropUnit>& units) {
  std::vector<gfx::Rect> bounds;
  for (const DropUnit& unit : units) {
    bounds.push_back(unit.bounds);
  }
  return bounds;
}

}  // namespace

bool KeepsTabDragInWindow(views::Widget* widget) {
  if (!features::IsSidebarEnabled() || !widget ||
      !widget->GetTopLevelWidget()) {
    return false;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForNativeWindow(
      widget->GetTopLevelWidget()->GetNativeWindow());
  return browser_view && browser_view->ShouldDrawVerticalTabStrip();
}

KoanSpaceDrag::KoanSpaceDrag(KoanWorkspaceController* controller,
                             BrowserView* browser_view)
    : controller_(controller), browser_view_(browser_view) {}

KoanSpaceDrag::~KoanSpaceDrag() = default;

void KoanSpaceDrag::OnRegionMouseEvent(views::View* region,
                                       const ui::MouseEvent& event) {
  switch (event.type()) {
    case ui::EventType::kMouseDragged: {
      // Upstream's tab drag, not a press that hasn't moved enough yet, and
      // not a Space button's own reorder.
      if (!TabDragController::IsActive()) {
        return;
      }
      if (!dragging_) {
        dragging_ = true;
        // The drag may end where this never sees it (the tab torn off into
        // a window of its own, Esc, capture lost).
        watch_timer_.Start(FROM_HERE, kWatchInterval, this,
                           &KoanSpaceDrag::CheckDragActive);
      }
      OnTabDragged(region, InView(static_cast<views::View*>(event.target()),
                                  region, event.location()));
      return;
    }
    case ui::EventType::kMouseReleased:
      if (dragging_) {
        OnDragEnded(/*dropped=*/true);
      }
      return;
    default:
      return;
  }
}

void KoanSpaceDrag::CheckDragActive() {
  if (dragging_ && !TabDragController::IsActive()) {
    OnDragEnded(/*dropped=*/false);
  }
}

void KoanSpaceDrag::Reset() {
  watch_timer_.Stop();
  switch_timer_.Stop();
  dragging_ = false;
  edge_ = 0;
  drop_target_ = DropTarget::kNone;
  RemoveDropIndicator();
  RemovePromo();
}

void KoanSpaceDrag::OnTabDragged(views::View* region, const gfx::Point& point) {
  const std::vector<KoanWorkspaceController::SpaceInfo> spaces =
      controller_->GetSpaces();
  const std::vector<tabs::TabInterface*> tabs = DraggedTabs();
  const bool only_essentials =
      std::ranges::all_of(tabs, &tabs::TabInterface::IsPinned);

  // Over a Space button: that Space, at once (not for Essentials, which
  // every Space shows).
  if (KoanSpaceBarView* bar = controller_->space_bar();
      bar && bar->GetVisible() && !only_essentials && spaces.size() > 1) {
    if (const std::optional<tab_groups::TabGroupId> space =
            bar->SpaceAt(InView(region, bar, point));
        space && space != controller_->GetActiveSpace()) {
      switch_timer_.Stop();
      edge_ = 0;
      SwitchWithDraggedTabs(*space);
      return;
    }
  }

  UpdatePromo(region, point);
  UpdateDropTarget(region, point);

  // Near an edge of an expanded sidebar, away from the Essentials: the
  // previous or next Space after the delay.
  const int edge = spaces.size() > 1 && !IsSidebarCollapsed() &&
                           !IsOverEssentials(region, point)
                       ? SpaceDragEdge(point.x(), region->width())
                       : 0;
  if (edge == 0) {
    switch_timer_.Stop();
    edge_ = 0;
    return;
  }
  if (edge != edge_ || !switch_timer_.IsRunning()) {
    edge_ = edge;
    switch_timer_.Start(FROM_HERE, kSpaceDragSwitchDelay, this,
                        &KoanSpaceDrag::OnSwitchTimer);
  }
}

void KoanSpaceDrag::OnSwitchTimer() {
  if (!dragging_ || !TabDragController::IsActive() || edge_ == 0) {
    return;
  }
  const std::vector<KoanWorkspaceController::SpaceInfo> spaces =
      controller_->GetSpaces();
  const auto active = std::ranges::find(
      spaces, true, &KoanWorkspaceController::SpaceInfo::active);
  if (active == spaces.end()) {
    return;
  }
  // As the Space shortcuts: wrapping around only if the setting says so.
  const std::optional<size_t> target =
      NextSpaceIndex(static_cast<size_t>(active - spaces.begin()),
                     spaces.size(), edge_ > 0, controller_->WrapsAround());
  if (!target) {
    return;
  }
  SwitchWithDraggedTabs(spaces[*target].id);
  // Zen's dragover keeps coming while the tab stays at the edge, and each
  // one starts the delay again: so does this, until the pointer moves away.
  switch_timer_.Start(FROM_HERE, kSpaceDragSwitchDelay, this,
                      &KoanSpaceDrag::OnSwitchTimer);
}

std::vector<tabs::TabInterface*> KoanSpaceDrag::DraggedTabs() const {
  TabStripModel* model = controller_->tab_strip_model();
  std::vector<tabs::TabInterface*> tabs;
  for (size_t index :
       model->selection_model().GetListSelectionModel().selected_indices()) {
    if (tabs::TabInterface* tab =
            model->GetTabAtIndex(static_cast<int>(index))) {
      tabs.push_back(tab);
    }
  }
  // The tab that was picked up (the active one) last: the new Space shows
  // it.
  if (tabs::TabInterface* active = model->GetActiveTab()) {
    if (auto it = std::ranges::find(tabs, active); it != tabs.end()) {
      std::rotate(it, it + 1, tabs.end());
    }
  }
  return tabs;
}

void KoanSpaceDrag::SwitchWithDraggedTabs(const tab_groups::TabGroupId& space) {
  std::vector<tabs::TabInterface*> tabs = DraggedTabs();
  std::erase_if(tabs,
                [](const tabs::TabInterface* tab) { return tab->IsPinned(); });
  if (tabs.empty()) {
    // Essentials are in every Space: just the switch.
    controller_->SwitchTo(space);
    return;
  }
  // (Moves them to the end of the Space and shows it on the picked-up tab,
  // keeping them the selection; the drag then places them.)
  controller_->MoveDraggedTabsToSpace(tabs, space);
}

gfx::Rect KoanSpaceDrag::EmptyEssentialsZone(views::View* region) const {
  KoanSpaceHeaderView* header = controller_->space_header();
  if (!header || !header->GetVisible()) {
    return gfx::Rect();
  }
  std::optional<gfx::Rect> promo;
  if (promo_) {
    promo = views::View::ConvertRectToTarget(promo_, region,
                                             promo_->GetLocalBounds());
  }
  return EmptyEssentialsDropZone(region->size(),
                                 views::View::ConvertRectToTarget(
                                     header, region, header->GetLocalBounds()),
                                 promo);
}

bool KoanSpaceDrag::IsOverEssentials(views::View* region,
                                     const gfx::Point& point) const {
  // With the promo: it, or the sidebar above it.
  if (promo_ && promo_->GetVisible() &&
      EmptyEssentialsZone(region).Contains(point)) {
    return true;
  }
  KoanSpaceHeaderView* header = controller_->space_header();
  auto* strip =
      header ? views::AsViewClass<TabStripView>(header->parent()) : nullptr;
  if (!strip) {
    return false;
  }
  views::View* essentials = strip->pinned_tabs_scroll_view();
  if (!essentials) {
    essentials = strip->GetPinnedTabsContainer();
  }
  if (!essentials || !essentials->GetVisible()) {
    return false;
  }
  // The grid, or the sidebar above it (a tab dragged straight up, over the
  // URL bar).
  return essentials->HitTestPoint(InView(region, essentials, point)) ||
         EssentialsTopDropZone(
             region->size(),
             views::View::ConvertRectToTarget(essentials, region,
                                              essentials->GetLocalBounds()))
             .Contains(point);
}

TabStripView* KoanSpaceDrag::GetStrip() const {
  KoanSpaceHeaderView* header = controller_->space_header();
  return header ? views::AsViewClass<TabStripView>(header->parent()) : nullptr;
}

bool KoanSpaceDrag::IsOverSpaceList(views::View* region,
                                    const gfx::Point& point) const {
  TabStripView* strip = GetStrip();
  views::ScrollView* list =
      strip ? strip->unpinned_tabs_scroll_view() : nullptr;
  return list && list->IsDrawn() &&
         list->HitTestPoint(InView(region, list, point));
}

void KoanSpaceDrag::UpdateDropTarget(views::View* region,
                                     const gfx::Point& point) {
  drop_target_ = DropTarget::kNone;
  drop_before_ = tabs::TabHandle();
  TabStripModel* model = controller_->tab_strip_model();
  const std::vector<tabs::TabInterface*> tabs = DraggedTabs();
  KoanPinnedTabController* pinned =
      tabs.empty() ? nullptr : KoanPinnedTabController::ForTab(tabs.back());
  TabStripRegionView* tab_strip = browser_view_->tab_strip_view();
  TabDragContext* context = tab_strip ? tab_strip->GetDragContext() : nullptr;
  if (!pinned || !context || IsSidebarCollapsed()) {
    RemoveDropIndicator();
    return;
  }
  // (Upstream's drag takes Essentials or Space tabs, never both.)
  const bool essentials =
      std::ranges::all_of(tabs, &tabs::TabInterface::IsPinned);
  gfx::Rect indicator;
  if (!essentials && IsOverEssentials(region, point)) {
    // Zen: only while one more fits (canEssentialBeAdded()); a tab over a
    // full grid drops back into the Space.
    if (pinned->CanAddToEssentials()) {
      drop_target_ = DropTarget::kEssentials;
      const std::vector<DropUnit> tiles =
          DropUnits(model, context, region,
                    gfx::Range(0, model->IndexOfFirstNonPinnedTab()));
      if (!tiles.empty()) {
        const bool rtl = base::i18n::IsRTL();
        const size_t slot = EssentialsDropSlot(UnitBounds(tiles), point, rtl);
        // Upright between two tiles: before the slot's tile, or after the
        // last one.
        const bool after = slot == tiles.size();
        const gfx::Rect& tile = tiles[after ? slot - 1 : slot].bounds;
        const int edge = after != rtl ? tile.right() + kTileGapHalf
                                      : tile.x() - kTileGapHalf;
        indicator =
            gfx::Rect(edge - KoanTabDropIndicatorView::kThickness / 2, tile.y(),
                      KoanTabDropIndicatorView::kThickness, tile.height());
        if (!after) {
          drop_before_ = tiles[slot].first;
        }
      }
    }
  } else if (essentials && IsOverSpaceList(region, point)) {
    const std::optional<tab_groups::TabGroupId> space =
        controller_->GetActiveSpace();
    const TabGroup* group =
        space ? model->group_model()->GetTabGroup(*space) : nullptr;
    if (group) {
      drop_target_ = DropTarget::kSpace;
      const std::vector<DropUnit> rows =
          DropUnits(model, context, region, group->ListTabs());
      if (!rows.empty()) {
        const size_t slot = SpaceListDropSlot(UnitBounds(rows), point.y());
        // Across the list, above the slot's row or below the last one.
        const bool after = slot == rows.size();
        const gfx::Rect& row = rows[after ? slot - 1 : slot].bounds;
        const int edge = after ? row.bottom() : row.y();
        indicator =
            gfx::Rect(row.x(), edge - KoanTabDropIndicatorView::kThickness / 2,
                      row.width(), KoanTabDropIndicatorView::kThickness);
        if (!after) {
          drop_before_ = rows[slot].first;
        }
      }
    }
  }
  ShowDropIndicator(region, indicator);
}

void KoanSpaceDrag::ShowDropIndicator(views::View* region,
                                      const gfx::Rect& bounds) {
  TabStripView* strip = GetStrip();
  if (bounds.IsEmpty() || !strip) {
    RemoveDropIndicator();
    return;
  }
  if (!indicator_) {
    indicator_ =
        strip->AddChildView(std::make_unique<KoanTabDropIndicatorView>());
  }
  indicator_->SetSlot(views::View::ConvertRectToTarget(region, strip, bounds));
}

void KoanSpaceDrag::RemoveDropIndicator() {
  KoanTabDropIndicatorView* indicator = std::exchange(indicator_, nullptr);
  if (!indicator) {
    return;
  }
  views::View* strip = indicator->parent();
  strip->RemoveChildViewT(indicator);
  strip->InvalidateLayout();
}

void KoanSpaceDrag::UpdatePromo(views::View* region, const gfx::Point& point) {
  KoanSpaceHeaderView* header = controller_->space_header();
  auto* strip =
      header ? views::AsViewClass<TabStripView>(header->parent()) : nullptr;
  const std::vector<tabs::TabInterface*> tabs = DraggedTabs();
  KoanPinnedTabController* pinned =
      tabs.empty() ? nullptr : KoanPinnedTabController::ForTab(tabs.back());
  // Zen: only while there are no Essentials, for a tab that can become one,
  // in an expanded sidebar.
  const bool can_promo =
      strip && pinned && pinned->EssentialsCount() == 0 &&
      !std::ranges::all_of(tabs, &tabs::TabInterface::IsPinned) &&
      !IsSidebarCollapsed();
  if (!can_promo) {
    RemovePromo();
    return;
  }
  if (!promo_) {
    // Zen shows it once the tab is over the (empty) Essentials: here, the
    // top of the sidebar, down to the Space header's middle.
    if (!EmptyEssentialsZone(region).Contains(point)) {
      return;
    }
    // Before the header, which PlaceSpaceHeader() puts below it.
    promo_ = strip->AddChildViewAt(std::make_unique<KoanEssentialsPromoView>(),
                                   strip->GetIndexOf(header).value_or(0));
    strip->InvalidateLayout();
  }
  // Like Zen's, it stays for the rest of the drag; lit while a drop would
  // land in it (over it, or the sidebar above it).
  promo_->SetDragOver(IsOverEssentials(region, point));
}

void KoanSpaceDrag::RemovePromo() {
  KoanEssentialsPromoView* promo = std::exchange(promo_, nullptr);
  if (!promo) {
    return;
  }
  views::View* strip = promo->parent();
  strip->RemoveChildViewT(promo);
  strip->InvalidateLayout();
}

void KoanSpaceDrag::OnDragEnded(bool dropped) {
  dragging_ = false;
  watch_timer_.Stop();
  switch_timer_.Stop();
  edge_ = 0;
  // (A drag cancelled with Esc is over before its release.)
  const DropTarget target = dropped && TabDragController::IsActive()
                                ? drop_target_
                                : DropTarget::kNone;
  drop_target_ = DropTarget::kNone;
  std::vector<tabs::TabHandle> handles;
  if (target != DropTarget::kNone) {
    // In the sidebar's order (Zen keeps the dragged tabs' order).
    TabStripModel* model = controller_->tab_strip_model();
    for (size_t index :
         model->selection_model().GetListSelectionModel().selected_indices()) {
      if (tabs::TabInterface* tab =
              model->GetTabAtIndex(static_cast<int>(index))) {
        handles.push_back(tab->GetHandle());
      }
    }
  }
  // Zen: the drop is done, so its indicator goes before the tabs land.
  RemoveDropIndicator();
  // After upstream's drag has handled the release (this sees it first), and
  // not from inside the event's dispatch: the promo's strip lays out again.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&KoanSpaceDrag::FinishDrop, weak_factory_.GetWeakPtr(),
                     target, std::move(handles), drop_before_));
}

void KoanSpaceDrag::FinishDrop(DropTarget target,
                               std::vector<tabs::TabHandle> handles,
                               tabs::TabHandle before) {
  RemovePromo();
  std::vector<tabs::TabInterface*> tabs;
  for (const tabs::TabHandle& handle : handles) {
    if (tabs::TabInterface* tab = handle.Get()) {
      tabs.push_back(tab);
    }
  }
  KoanPinnedTabController* pinned =
      tabs.empty() ? nullptr : KoanPinnedTabController::ForTab(tabs.front());
  if (!pinned) {
    return;
  }
  switch (target) {
    case DropTarget::kNone:
      return;
    case DropTarget::kEssentials:
      pinned->DropInEssentials(tabs, before.Get());
      return;
    case DropTarget::kSpace:
      for (tabs::TabInterface* tab : tabs) {
        pinned->DropEssentialInSpace(tab, before.Get());
      }
      return;
  }
}

bool KoanSpaceDrag::IsSidebarCollapsed() const {
  // Zen's _prefsSidebarExpanded.
  const auto* state =
      tabs::VerticalTabStripStateController::From(browser_view_->browser());
  return state && state->IsCollapsed();
}

}  // namespace koan
