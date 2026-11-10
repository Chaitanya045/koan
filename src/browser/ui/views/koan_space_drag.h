// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_DRAG_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_DRAG_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/gfx/geometry/point.h"

class BrowserView;

namespace ui {
class MouseEvent;
}

namespace views {
class View;
}

namespace koan {

class KoanEssentialsPromoView;
class KoanWorkspaceController;

// Zen's tab drag across Spaces for one window (ZenDragAndDrop.js):
// - a tab held within kSpaceDragSwitchPadding of the sidebar's left or right
//   edge for kSpaceDragSwitchDelay switches to the previous or next Space
//   (and again after each delay while it stays), not over the Essentials, not
//   in a collapsed sidebar (#handle_sidebarDragOver());
// - a tab over a Space button in the bottom bar switches to that Space at
//   once, but not for Essentials (handle_spaceIconDragOver());
// - with no Essentials, a tab over the top of the sidebar shows the
//   Essentials promo, and dropping it there makes it an Essential
//   (ZenEssentialsPromo.mjs).
// The dragged tabs go with the switch, so the drag goes on in the new Space
// and drops there (Zen moves them on the drop; Chromium's drag needs them in
// the Space it shows).
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
  // Whether `point` (in `region`) is over the Essentials grid or the promo.
  bool IsOverEssentials(views::View* region, const gfx::Point& point) const;
  void UpdatePromo(views::View* region, const gfx::Point& point);
  void RemovePromo();
  // After the drop: the dragged tabs become Essentials.
  void AddDroppedToEssentials(std::vector<tabs::TabHandle> tabs);
  bool IsSidebarCollapsed() const;

  const raw_ptr<KoanWorkspaceController> controller_;
  const raw_ptr<BrowserView> browser_view_;
  base::OneShotTimer switch_timer_;
  base::RepeatingTimer watch_timer_;
  // The edge the pointer is at (-1 left, 1 right, 0 neither).
  int edge_ = 0;
  bool dragging_ = false;
  raw_ptr<KoanEssentialsPromoView> promo_ = nullptr;
  base::WeakPtrFactory<KoanSpaceDrag> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_DRAG_H_
