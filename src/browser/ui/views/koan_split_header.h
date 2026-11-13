// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPLIT_HEADER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPLIT_HEADER_H_

#include <array>
#include <memory>
#include <optional>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_widget_host_view.h"
#include "koan/browser/ui/views/koan_split_rearrange.h"
#include "ui/events/event_observer.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/view_observer.h"

class BrowserView;
class ContentsContainerView;

namespace views {
class EventMonitor;
class Widget;
}  // namespace views

namespace koan {

class KoanSplitHeaderView;
class KoanSplitRearrangeView;

// Zen's pane header unsplit (removeTabFromSplit()): `tab` leaves its split
// and is selected, unless `keep_selection` (Shift held): then the selection
// stays where it was. Chromium's split has two tabs, so it is separated.
void UnsplitFromHeader(TabStripModel* tab_strip,
                       tabs::TabInterface* tab,
                       bool keep_selection);

// Zen's rearrange drop (onBrowserDrop()) of `dragged`'s pane on `side` of
// the other pane of its split (RearrangeSplit()), through TabStripModel's
// split APIs, so the sidebar, the session and window sync follow.
void RearrangeSplitPanes(TabStripModel* tab_strip,
                         tabs::TabInterface* dragged,
                         RearrangeSide side,
                         bool rtl);

// Zen's split pane header (ZenViewSplitter's _createHeader(),
// zen-split-view.css .zen-view-splitter-header) for one window: while the
// pointer is over a pane of the split shown, a small tab hangs from the
// middle of its top edge with a grip and an unsplit button, fading in after
// 0.1 s. The unsplit button takes the pane's tab out of the split and
// selects it (Shift: keeps the selection), as Zen's removeTabFromSplit().
// Dragging the grip is Zen's rearrange view (enableTabRearrangeView() from a
// header drag, onBrowserDragOver(), onBrowserDrop()): the panes dim, the
// dragged one most, a thumbnail of it follows the pointer, and over the
// other pane a drop zone covers its half on the side the pointer is near
// (koan_split_rearrange.h) or all of it; the drop swaps the panes or puts
// the dragged one on that side, switching between side by side and stacked.
// Esc, a tab change or the split going away cancel it.
//
// Each header is a child widget over the page (as the loading indicator
// and the toast), so it takes the pointer before the page does, and with it
// before the window drag from the page's top (koan_window_drag.h). Not in
// the page's own fullscreen (Zen's inDOMFullscreen), not while Glance is
// open over the split (its card covers the panes) or a tab is dragged.
class KoanSplitHeaderController : public TabStripModelObserver,
                                  public views::ViewObserver,
                                  public ui::EventObserver {
 public:
  explicit KoanSplitHeaderController(BrowserView* browser_view);
  KoanSplitHeaderController(const KoanSplitHeaderController&) = delete;
  KoanSplitHeaderController& operator=(const KoanSplitHeaderController&) =
      delete;
  ~KoanSplitHeaderController() override;

  // Called by KoanBrowserViewController.
  void OnAddedToWidget();
  void OnBrowserViewLayout();
  void OnBrowserViewDestroying();

  // Whether `widget` is one of the headers or the rearrange view: the
  // window's own chrome, on the Space's colour scheme (KoanThemeController).
  bool OwnsWidget(const views::Widget* widget) const;

  // From the header of `pane` (0 or 1, its index in panes_).
  void OnHeaderHovered(int pane, bool hovered);
  void OnUnsplitPressed(int pane, bool keep_selection);
  void StartRearrange(int pane);
  void UpdateRearrange(const gfx::Point& screen_point);
  void FinishRearrange(const gfx::Point& screen_point);
  void CancelRearrange();

 private:
  // A pane of MultiContentsView (which keeps its two containers for the
  // window's life; swapping panes swaps their places) and its header.
  struct Pane {
    Pane();
    ~Pane();

    raw_ptr<ContentsContainerView> container = nullptr;
    std::unique_ptr<views::Widget> widget;
    raw_ptr<KoanSplitHeaderView> view = nullptr;
    // The pointer is over the header itself.
    bool header_hovered = false;
    // The header should show.
    bool wanted = false;
    // The fade-in's delay, or the end of the fade-out.
    base::OneShotTimer timer;
  };

  struct Rearrange {
    int dragged = 0;
    tabs::TabHandle tab;
    // The other pane's side under the pointer, if it is over that pane.
    std::optional<RearrangeSide> side;
  };

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnSplitTabChanged(const SplitTabChange& change) override;

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewIsDeleting(views::View* observed_view) override;

  // ui::EventObserver:
  void OnEvent(const ui::Event& event) override;

  // Update() once every observer has seen the tab strip's change, so that
  // MultiContentsView shows the new state.
  void PostUpdate();
  // Starts or stops watching the pointer, hides what can't show and places
  // the rest.
  void Update();
  // The split is shown and its headers may appear.
  bool IsActive() const;
  // Which headers the pointer asks for.
  void UpdateHover();
  bool IsPointerOverPane(const Pane& pane, const gfx::Point& screen) const;
  void SetWanted(Pane& pane, bool wanted);
  // Hides the header of panes_[index] once it has faded out.
  void HideHeader(int index);
  void EnsureWidget(int index);
  void LayoutHeaders();
  void LayoutRearrangeView();
  tabs::TabInterface* TabOf(const Pane& pane) const;
  // Ends the rearrange view; `drop` applies the drop the pointer is over.
  void EndRearrange(bool drop);
  void CaptureThumbnail(content::WebContents* contents, int generation);
  void OnThumbnail(int generation,
                   float scale,
                   const content::CopyFromSurfaceResult& result);

  const raw_ptr<BrowserView> browser_view_;
  std::array<Pane, 2> panes_;
  std::unique_ptr<views::EventMonitor> monitor_;
  // The pointer was last seen over the browser window (its own views or the
  // page), not over another window.
  bool pointer_in_window_ = false;
  std::optional<Rearrange> rearrange_;
  // Counts rearranges, so a late thumbnail of an earlier one is dropped.
  int rearrange_generation_ = 0;
  std::unique_ptr<views::Widget> rearrange_widget_;
  raw_ptr<KoanSplitRearrangeView> rearrange_view_ = nullptr;
  base::OneShotTimer rearrange_hide_timer_;
  base::CallbackListSubscription fullscreen_subscription_;
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      container_observations_{this};
  bool update_posted_ = false;
  bool destroying_ = false;
  base::WeakPtrFactory<KoanSplitHeaderController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPLIT_HEADER_H_
