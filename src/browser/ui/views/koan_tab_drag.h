// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_TAB_DRAG_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_TAB_DRAG_H_

#include <memory>
#include <optional>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "koan/browser/ui/views/koan_essentials_promo.h"
#include "koan/browser/ui/views/koan_tab_drag_image.h"
#include "ui/events/event_observer.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/view_tracker.h"

class BrowserView;
class TabDragContext;
class TabDragController;
class TabStripView;

namespace gfx {
class Rect;
}

namespace ui {
class MouseEvent;
}

namespace views {
class EventMonitor;
class View;
}  // namespace views

namespace koan {

class KoanPinnedTabController;
class KoanTabDragCaptureView;
class KoanWorkspaceController;
class SidebarHold;
struct SpaceListLayout;
struct SpaceListRow;

// Whether upstream's tab drag (TabDragController) is moving tabs: started,
// not just waiting on a press for the pointer to move. Koan's own drag keeps
// upstream's waiting one (see KoanTabDrag), so TabDragController::IsActive()
// is true during it too.
bool IsUpstreamTabDragRunning();

// Koan's tab drag for one window's sidebar, Zen's drag and drop
// (ZenDragAndDrop.js, ZenPinnedTabManager.mjs, ZenFolders.mjs):
// - the tab stays in place (an Essential's tile is hidden) and an opaque copy
//   of its row follows the pointer, a stack with the count for several tabs
//   (KoanTabDragImage);
// - nothing reorders while it moves: Zen's line shows where the tabs drop,
//   before or after the row under the pointer by its halves, a bar lights the
//   folder (or the Space header) they drop into (PlaceInSpaceList());
// - the section under the pointer pins or unpins them, a drop on the
//   Essentials grid or the promo makes them Essentials, an Essential dropped
//   on the Space's list leaves the grid (#319, #357);
// - a tab over a Space button switches Space at once, one held at the
//   sidebar's left or right edge after a delay, again and again; the tabs
//   join the Space shown when they drop;
// - on the drop the image glides into the tab's new slot while the tab is
//   hidden (Zen's landing on macOS); dropped elsewhere on the sidebar after
//   a switch (a Space button) they join the Space shown; Esc, a lost capture
//   or a drop off the sidebar puts everything back, the Space included, the
//   image gliding home;
// - a haptic tick on each new place but the first; the sidebar stays out
//   while it runs (compact mode);
// - split by drag: held half a second over the middle of another tab's row,
//   Zen's fake tab shows in the row and the drop splits the two (the side by
//   the row's half); over the page's edge the image turns into Zen's card, a
//   fake pane opens there and the drop splits the dragged tab with the one
//   the page showed; one tab of a split view dragged elsewhere leaves the
//   split.
//
// Upstream's drag (TabDragController) stays for one thing: tabs dragged to
// another Koan window's sidebar. A press on a tab starts upstream's drag
// waiting for the pointer to move (TabView::OnMousePressed()); the sidebar's
// pre-target handler passes the press and the moves here first, and Koan
// takes the moves (so upstream's never starts) and, past Views' drag
// threshold, the mouse capture. Over another window's sidebar Koan steps back
// and hands upstream's waiting drag the pointer (TabDragHandler::
// ContinueDrag()), which then moves the tabs there as before. Tabs dragged
// out of a window never tear off (KeepsTabDragInWindow(),
// koan_tab_drag_hooks.h).
class KoanTabDrag : public ui::EventObserver {
 public:
  KoanTabDrag(KoanWorkspaceController* controller, BrowserView* browser_view);
  KoanTabDrag(const KoanTabDrag&) = delete;
  KoanTabDrag& operator=(const KoanTabDrag&) = delete;
  ~KoanTabDrag() override;

  // A mouse event on its way to a view in the sidebar `region` (the vertical
  // tab strip region). Stops the moves of a press on a tab.
  void OnRegionMouseEvent(views::View* region, ui::MouseEvent* event);

  // The window's views are going: cancels a drag.
  void Reset();

  // Whether Koan's drag is on, from its start until its image has landed.
  bool IsDragging() const;

  // The promo, while it shows (for tests).
  KoanEssentialsPromoView* promo() const { return promo_; }

  // ui::EventObserver: Esc cancels.
  void OnEvent(const ui::Event& event) override;

  // For tests: drags the selection (which holds `tab`) as a press on `tab`
  // and a move would, without the pointer; switches Space as a Space button
  // would; cancels as Esc does.
  bool StartForTesting(tabs::TabInterface* tab);
  void SwitchSpaceForTesting(const tab_groups::TabGroupId& space);
  void CancelForTesting();

 private:
  friend class KoanTabDragCaptureView;

  enum class State {
    kIdle,
    // A tab was pressed: its moves are Koan's, not upstream's.
    kPressed,
    kDragging,
  };

  // Where a drop at the pointer's place goes.
  struct DropTarget {
    // kSplitTab: onto a tab's row, to split with it; kSplitPage: onto the
    // page's edge, to split with the tab it shows.
    enum class Kind { kNone, kEssentials, kSpace, kSplitTab, kSplitPage };
    Kind kind = Kind::kNone;
    // Before this tab (none: at the end of the grid or the section).
    tabs::TabHandle before;
    // A drop on the Space's list: its section and folder.
    bool pinned = false;
    int folder = 0;
    // A split: the tab to split with, and whether the dragged tab goes first
    // (left or top). A split on the page's edge stacks it on the top or
    // bottom edge.
    tabs::TabHandle split_with;
    bool split_first = false;
    bool split_stacked = false;
    // Where its line or bar shows (in the region): a haptic ticks as it
    // moves (Zen: a new line place or lit row).
    gfx::Rect feedback;
  };

  // The capture view's events, in screen coordinates.
  void OnCaptureDragged(const gfx::Point& screen_point,
                        const ui::MouseEvent& event);
  void OnCaptureReleased();
  void OnCaptureLost();

  // Starts dragging the selection from `tab_view` (pressed at press_point_),
  // taking the mouse capture unless `for_testing`.
  bool Start(views::View* tab_view, bool for_testing);
  void DragTo(const gfx::Point& screen_point, const ui::MouseEvent* event);
  void Drop();
  void Cancel();
  // Stops the drag's feedback, timers and Esc, and the image unless it
  // lands next.
  void StopFeedback();
  // Lets go of upstream's waiting drag and of the capture, before a drop.
  void ReleaseUpstreamAndCapture();
  // The image glides to the row of `tab` (where it dropped, or back home).
  void Land(tabs::TabHandle tab);
  std::optional<gfx::Rect> LandingTarget(tabs::TabHandle tab) const;
  void OnLanded();
  // The view of the row (or tile) that shows `tab`: its TabView, or its split
  // view's.
  views::View* RowViewFor(const tabs::TabInterface* tab) const;

  // The tabs dragged, in tab strip order.
  std::vector<tabs::TabInterface*> DraggedTabs() const;
  bool DragsEssentials() const;
  // Shows `space` (the tabs join it when they drop).
  void SwitchSpace(const tab_groups::TabGroupId& space);
  void OnSwitchTimer();
  // Back to the Space, active tab and selection the drag started with.
  void RestoreOrigin();
  // Over another Koan window's sidebar: steps back for upstream's drag.
  bool MaybeHandOff(const gfx::Point& screen_point,
                    const ui::MouseEvent& event);
  // Whether the drag takes every tab of a Space (upstream would drag the
  // Space's group itself).
  bool DragsWholeSpace() const;

  // Split by drag (Zen's _handle_tabDragOverToSplit(),
  // onBrowserDragOverToSplit()). The tab the drag can split: one tab, not an
  // Essential nor in a split.
  tabs::TabInterface* SplittableTab() const;
  // Over the page's edge: shows the card and the fake pane and returns true.
  bool UpdatePageSplit(const gfx::Point& screen_point);
  // Over the middle band of the row `rows[row]` (its first tab `tab`), at
  // `point`: whether the held split shows there (after Zen's delay).
  bool UpdateRowSplit(const SpaceListRow& row,
                      tabs::TabInterface* tab,
                      const gfx::Point& point);
  void OnSplitTimer();
  void ClearRowSplit();
  // The dragged tab is one tab of a split view: the drag takes it out of the
  // split, unless it drops back in its place.
  tabs::TabInterface* LoneSplitTab() const;
  bool DropsBackInSplit(const DropTarget& target) const;

  // Where the pointer at `point` (in the region) drops, with its feedback.
  void UpdateTarget(const gfx::Point& point);
  void UpdatePromo(const gfx::Point& point);
  void RemovePromo();
  gfx::Rect EmptyEssentialsZone() const;
  bool IsOverEssentials(const gfx::Point& point) const;
  bool IsOverSpace(const gfx::Point& point) const;
  // The active Space's rows (koan_space_drag_logic.h) and, for each, its
  // first tab (none for a folder's row).
  std::vector<SpaceListRow> SpaceRows(std::vector<tabs::TabHandle>& firsts);
  SpaceListLayout SpaceLayout(const std::vector<SpaceListRow>& rows) const;
  // Shows the line at `line` or the bar at `bar` (in the region); empty
  // hides them.
  // (`line` in `line_style`: across, upright or Zen's fake tab.)
  void ShowFeedback(const gfx::Rect& line,
                    KoanTabDropIndicatorView::Style line_style,
                    const gfx::Rect& bar);

  // Hides `view` (a tab's row or tile) by its layer while the drag or the
  // landing needs it hidden; ShowHiddenViews() brings them back.
  void HideView(views::View* view);
  void ShowHiddenViews();

  TabStripView* GetStrip() const;
  TabDragContext* GetDragContext() const;
  // Upstream's drag waiting on the press, if any.
  TabDragController* GetWaitingUpstreamDrag() const;
  views::View* region() const;
  KoanPinnedTabController* pinned_tabs() const;
  bool IsSidebarCollapsed() const;

  const raw_ptr<KoanWorkspaceController> controller_;
  const raw_ptr<BrowserView> browser_view_;
  State state_ = State::kIdle;
  // (Mutable: the const lookups use the region's view.)
  mutable views::ViewTracker region_;
  views::ViewTracker pressed_;
  views::ViewTracker capture_;
  gfx::Point press_point_;
  gfx::Vector2d grab_;
  // The tile the image becomes over the Essentials.
  gfx::Size tile_size_;
  // The pointer's last place, in the region.
  gfx::Point last_point_;

  // The drag.
  std::vector<tabs::TabHandle> dragged_;
  tabs::TabHandle source_;
  std::optional<tab_groups::TabGroupId> origin_space_;
  tabs::TabHandle origin_active_;
  tabs::TabHandle origin_anchor_;
  std::vector<tabs::TabHandle> origin_selection_;
  DropTarget target_;
  // Zen skips the first haptic tick.
  bool haptic_skipped_ = false;
  // The edge the pointer is at (-1 left, 1 right, 0 neither).
  int edge_ = 0;
  base::OneShotTimer switch_timer_;
  // The tab the page showed before the press selected the dragged one
  // (Zen's _lastOpenedTab): a page-edge split is with it.
  tabs::TabHandle shown_before_press_;
  // A row split waiting for Zen's delay, and the one showing.
  base::OneShotTimer split_timer_;
  tabs::TabHandle split_candidate_;
  bool split_candidate_first_ = false;
  tabs::TabHandle split_row_;
  bool split_row_first_ = false;
  // The page edge showing the fake pane, if any (a SplitEdge).
  std::optional<int> page_edge_;
  bool limit_toast_shown_ = false;
  std::unique_ptr<views::EventMonitor> escape_monitor_;
  std::unique_ptr<SidebarHold> hold_;
  KoanTabDragImage image_;
  raw_ptr<KoanEssentialsPromoView> promo_ = nullptr;
  raw_ptr<KoanTabDropIndicatorView> line_ = nullptr;
  raw_ptr<KoanTabDropIndicatorView> bar_ = nullptr;
  // Rows hidden by their layer, and whether Koan gave them the layer.
  struct HiddenView {
    std::unique_ptr<views::ViewTracker> view;
    bool own_layer = false;
  };
  std::vector<HiddenView> hidden_;
  bool landing_ = false;
  base::OneShotTimer landing_failsafe_;

  base::WeakPtrFactory<KoanTabDrag> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_TAB_DRAG_H_
