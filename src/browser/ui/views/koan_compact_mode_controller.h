// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_CONTROLLER_H_

#include <memory>
#include <optional>

#include <vector>

#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/events/event_observer.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_tracker.h"

class BrowserView;
class ExpandOnHoverLock;

namespace views {
class EventMonitor;
}

namespace koan {

class KoanHotZone;
class KoanSidebarNavButtons;
class KoanSidebarNavView;

// Zen's compact mode and single-toolbar layout for one window.
//
// Single toolbar (Zen's default layout; Ctrl+Alt+B, pref koan.single_toolbar):
// there is no top toolbar. The top container is hidden in the overlay
// (below), the page card reaches the top of the window, and
// KoanSidebarNavView at the top of the sidebar has the navigation buttons
// and a URL pill.
//
// Compact mode (Ctrl+S like Zen, pref koan.compact_mode.enabled):
// - the vertical tab strip is collapsed to zero width and slides over the
//   page (upstream's expand-on-hover overlay) when the pointer reaches the
//   leading edge;
// - with single toolbar, the sidebar keeps its header, so that is all;
// - otherwise the top container (toolbar, bookmarks bar) moves into the
//   BrowserView's overlay view, above the page, and slides down with a
//   compositor transform when the pointer reaches the top edge;
// - the page is a card with a gutter on every side; the edge hot zones live
//   in that gutter, outside the page's native window.
//
// In both, focusing the omnibox (the URL pill, Ctrl+L) shows Zen's floating
// URL bar: just the location bar, in a card centred over the window; it goes
// away when focus leaves it.
//
// Both states are profile prefs shared by all windows. Keyboard shortcuts
// live in KoanShortcutController.
class KoanCompactModeController : public views::FocusChangeListener,
                                  public views::ViewObserver,
                                  public ui::EventObserver {
 public:
  explicit KoanCompactModeController(BrowserView* browser_view);
  KoanCompactModeController(const KoanCompactModeController&) = delete;
  KoanCompactModeController& operator=(const KoanCompactModeController&) =
      delete;
  ~KoanCompactModeController() override;

  enum class Mode { kNone, kCompact, kSingleToolbar };

  Mode mode() const { return mode_; }
  // Compact mode: the sidebar takes no layout space.
  bool hides_sidebar() const { return mode_ == Mode::kCompact; }
  // Compact mode or single toolbar: the top toolbar takes no layout space.
  bool hides_toolbar() const { return mode_ != Mode::kNone; }
  bool is_toolbar_revealed() const { return toolbar_revealed_; }
  bool is_url_bar_floating() const { return url_bar_floating_; }
  // Single toolbar (also in compact mode): the caption-button strip is
  // collapsed until the pointer reaches the top edge.
  bool caption_strip_collapsed() const {
    return caption_strip_collapsible_ && !caption_strip_revealed_;
  }

  // Called by KoanBrowserViewController.
  void OnAddedToWidget();
  void OnBrowserViewLayout();
  void OnBrowserViewDestroying();

  // Flip the profile prefs.
  void ToggleCompactMode();
  void ToggleSingleToolbar();

  // Compact mode: slides the hidden sidebar out (Zen's Ctrl+Alt+S); it
  // slides away once the pointer isn't over it. Returns false outside
  // compact mode.
  bool PeekSidebar();

  // views::FocusChangeListener:
  void OnDidChangeFocus(views::View* focused_before,
                        views::View* focused_now) override;

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewVisibilityChanged(views::View* observed_view,
                               views::View* starting_view,
                               bool visible) override;
  void OnViewIsDeleting(views::View* observed_view) override;

  // ui::EventObserver:
  void OnEvent(const ui::Event& event) override;

 private:
  Mode DesiredMode() const;
  // In compact mode: keep the sidebar header (single toolbar is on).
  bool DesiredCompactKeepsSidebarNav() const;
  void UpdateActiveSoon();
  void UpdateActive();
  void Activate(Mode mode);
  void Deactivate();

  void SetToolbarRevealed(bool revealed, bool animate);
  void ApplyToolbarTransform(bool animate);
  bool ShouldKeepToolbarRevealed() const;
  void CheckToolbarHide();

  // While the toolbar, the caption strip or a peeked sidebar is out, watches
  // the pointer so each hides once it has been away long enough. Nothing
  // runs while they are hidden or while the pointer rests over them.
  void UpdatePointerMonitor();
  void CheckRevealedHides();

  // The floating URL bar: shows only the location bar, centred.
  void SetUrlBarFloating(bool floating);
  void HideToolbarForFloating();
  void LayoutFloatingUrlBar();

  KoanSidebarNavView* sidebar_nav();

  // Single toolbar: adds KoanSidebarNavView to the top of the sidebar.
  void InstallSidebarNav();

  void CheckSidebarPeek();

  // The caption-button strip (single toolbar).
  void OnCaptionHotZoneHovered(bool hovered);
  void SetCaptionStripRevealed(bool revealed);
  void CheckCaptionStripHide();
  // Fades the frame's caption buttons in or out with the strip.
  void UpdateCaptionButtons();
  // The x of the page's left edge in `frame`'s coordinates.
  int PageLeftInFrame(const views::View* frame) const;
  // Whether the frame has caption buttons over the page side (to reveal).
  bool HasPageSideCaptionButtons() const;
  // The content card's insets changed: lay out the contents area and Glance.
  void OnContentCardChanged();

  void OnSidebarHotZoneHovered(bool hovered);
  void OnToolbarHotZoneHovered(bool hovered);
  void LayoutHotZones();

  const raw_ptr<BrowserView> browser_view_;
  PrefChangeRegistrar pref_change_registrar_;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;

  Mode mode_ = Mode::kNone;
  bool toolbar_revealed_ = false;
  bool url_bar_floating_ = false;
  // Whether compact mode keeps the sidebar header (single toolbar pref).
  bool compact_keeps_sidebar_nav_ = false;
  // While the URL bar floats: the toolbar's other views, hidden, and whether
  // each is to be visible afterwards. Entries go when a view is deleted.
  base::flat_map<views::View*, bool> floating_restore_;
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      floating_siblings_observation_{this};
  bool changing_toolbar_visibility_ = false;
  bool laying_out_floating_ = false;
  base::ScopedObservation<views::View, views::ViewObserver>
      top_container_observation_{this};
  base::ScopedObservation<views::View, views::ViewObserver>
      toolbar_observation_{this};
  std::optional<bool> collapsed_before_;
  std::optional<size_t> top_container_index_;

  raw_ptr<KoanHotZone> sidebar_hot_zone_ = nullptr;
  raw_ptr<KoanHotZone> toolbar_hot_zone_ = nullptr;
  raw_ptr<KoanHotZone> caption_hot_zone_ = nullptr;
  bool caption_strip_collapsible_ = false;
  bool caption_strip_revealed_ = false;
  base::OneShotTimer caption_hide_timer_;
  std::optional<base::TimeTicks> caption_unwanted_since_;
  // Koan views inside upstream's tab strip region (KoanSidebarNavView,
  // KoanSidebarNavButtons).
  views::ViewTracker sidebar_nav_;
  views::ViewTracker sidebar_nav_buttons_;
  // Separate locks so the hot zone and a keyboard peek can't release each
  // other's hold on the sidebar.
  std::unique_ptr<ExpandOnHoverLock> hover_lock_;
  std::unique_ptr<ExpandOnHoverLock> peek_lock_;

  base::OneShotTimer toolbar_hide_timer_;
  base::OneShotTimer sidebar_peek_timer_;
  std::optional<base::TimeTicks> sidebar_unhovered_since_;
  base::TimeTicks sidebar_peek_started_;
  std::optional<base::TimeTicks> toolbar_unwanted_since_;
  std::unique_ptr<views::EventMonitor> pointer_monitor_;

  base::WeakPtrFactory<KoanCompactModeController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_CONTROLLER_H_
