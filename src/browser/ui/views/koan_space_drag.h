// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_DRAG_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_DRAG_H_

#include <optional>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/gfx/geometry/point.h"

class BrowserView;
class TabStripView;

namespace gfx {
class Rect;
}

namespace ui {
class MouseEvent;
}

namespace views {
class View;
}

namespace koan {

class KoanEssentialsPromoView;
class KoanPinnedTabController;
class KoanTabDropIndicatorView;
class KoanWorkspaceController;

// Zen's tab drag across Spaces for one window (ZenDragAndDrop.js):
// - a tab held within kSpaceDragSwitchPadding of the sidebar's left or right
//   edge for kSpaceDragSwitchDelay switches to the previous or next Space
//   (and again after each delay while it stays), not over the Essentials, not
//   in a collapsed sidebar (#handle_sidebarDragOver());
// - a tab over a Space button in the bottom bar switches to that Space at
//   once, but not for Essentials (handle_spaceIconDragOver());
// - with no Essentials, a tab over the top of the sidebar (down to the Space
//   header's middle, the URL bar included) shows the Essentials promo, and
//   dropping it on the promo or anywhere above it makes it an Essential
//   (ZenEssentialsPromo.mjs; Zen's own target is a 2px strip);
// - tabs dropped on the Essentials grid become Essentials where they drop,
//   before or after the tile under the pointer (before the first one from
//   the sidebar above the grid, the URL bar included), while Zen's 12 fit;
//   and Essentials dropped on the Space's list (or its header) leave the
//   Essentials for that place, pinned to the Space in its pinned section
//   (above the separator's bottom), else normal tabs
//   (ZenPinnedTabManager's moveToAnotherTabContainerIfNecessary()). A line
//   shows where (#zen-drag-indicator);
// - Space tabs dragged across the separator are pinned or unpinned by the
//   section they drop in (the same Zen function): upstream's drag places them
//   among the Space's tabs, and at the separator, where that place is the
//   same on both sides, the pointer decides; the sidebar shows them in that
//   section while they are held (KoanPinnedTabController::DropInSpace()).
// The dragged tabs go with the switch, so the drag goes on in the new Space
// and drops there (Zen moves them on the drop; Chromium's drag needs them in
// the Space it shows). Upstream's drag keeps the tabs in their own section
// (Essentials among the Essentials, Space tabs in the Space): moves between
// the two are Koan's, made once the drag is over.
//
// A tab dragged out of the sidebar or the window never tears off into a new
// window (KeepsTabDragInWindow(), koan_tab_drag_hooks.h).
//
// Upstream's tab drag (TabDragController) gets the mouse events through the
// pressed tab's view; the sidebar's pre-target handler passes them here
// first, which only looks.
class KoanSpaceDrag {
 public:
  KoanSpaceDrag(KoanWorkspaceController* controller, BrowserView* browser_view);
  KoanSpaceDrag(const KoanSpaceDrag&) = delete;
  KoanSpaceDrag& operator=(const KoanSpaceDrag&) = delete;
  ~KoanSpaceDrag();

  // A mouse event on its way to a view in the sidebar `region` (the vertical
  // tab strip region).
  void OnRegionMouseEvent(views::View* region, const ui::MouseEvent& event);

  // The window's views are going: drops the promo and stops.
  void Reset();

  // The promo, while it shows (for tests).
  KoanEssentialsPromoView* promo() const { return promo_; }

 private:
  void OnTabDragged(views::View* region, const gfx::Point& point);
  void OnDragEnded(bool dropped);
  void OnSwitchTimer();
  void CheckDragActive();
  // The dragged tabs: the selection while a drag runs.
  std::vector<tabs::TabInterface*> DraggedTabs() const;
  // Shows `space`, taking the dragged tabs there (Essentials stay put).
  void SwitchWithDraggedTabs(const tab_groups::TabGroupId& space);
  // With no Essentials, the top of the sidebar that counts as them
  // (EmptyEssentialsDropZone()), in `region`.
  gfx::Rect EmptyEssentialsZone(views::View* region) const;
  // Whether `point` (in `region`) is over the Essentials grid or the promo,
  // or the sidebar above either.
  bool IsOverEssentials(views::View* region, const gfx::Point& point) const;
  void UpdatePromo(views::View* region, const gfx::Point& point);
  void RemovePromo();
  // Where a drop at `point` (in `region`) goes, with its indicator.
  void UpdateDropTarget(views::View* region, const gfx::Point& point);
  // Whether `point` (in `region`) is over the active Space's list of tabs
  // or its header.
  bool IsOverSpace(views::View* region, const gfx::Point& point) const;
  // Shows the dragged Space `tabs` in the section they would drop in
  // (`pointer_pinned`: the section under the pointer; nullopt: their own).
  void ShowDropPreview(KoanPinnedTabController* pinned,
                       const std::vector<tabs::TabInterface*>& tabs,
                       std::optional<bool> pointer_pinned);
  // Lays the active Space's list out again (its sections changed).
  void RelayoutSpace();
  // Shows the drop indicator at `bounds` (in `region`); empty hides it.
  void ShowDropIndicator(views::View* region, const gfx::Rect& bounds);
  void RemoveDropIndicator();
  TabStripView* GetStrip() const;

  // Zen's drops between the Essentials and the Space.
  enum class DropTarget {
    kNone,
    // Normal tabs over the Essentials (the promo or the grid).
    kEssentials,
    // Essentials over the Space's list of tabs.
    kSpace,
    // Space tabs over the Space's list of tabs.
    kWithinSpace,
  };
  // After the drop, once upstream's drag is over: the dragged `tabs` go to
  // `target`, before `before` (none: at the end), in the Space's pinned
  // section or not (`pinned_section`, the section under the pointer).
  void FinishDrop(DropTarget target,
                  std::vector<tabs::TabHandle> tabs,
                  tabs::TabHandle before,
                  bool pinned_section);
  bool IsSidebarCollapsed() const;

  const raw_ptr<KoanWorkspaceController> controller_;
  const raw_ptr<BrowserView> browser_view_;
  base::OneShotTimer switch_timer_;
  base::RepeatingTimer watch_timer_;
  // The edge the pointer is at (-1 left, 1 right, 0 neither).
  int edge_ = 0;
  bool dragging_ = false;
  raw_ptr<KoanEssentialsPromoView> promo_ = nullptr;
  raw_ptr<KoanTabDropIndicatorView> indicator_ = nullptr;
  // Where a drop at the pointer's last place goes.
  DropTarget drop_target_ = DropTarget::kNone;
  tabs::TabHandle drop_before_;
  bool drop_pinned_ = false;
  base::WeakPtrFactory<KoanSpaceDrag> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_DRAG_H_
