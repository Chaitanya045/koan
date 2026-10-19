// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_CONTROLLER_H_

#include <memory>
#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/views/focus/focus_manager.h"

class BrowserView;
class ExpandOnHoverLock;

namespace koan {

class KoanHotZone;

// Zen's compact mode and single-toolbar layout for one window.
//
// Single toolbar (Ctrl+Alt+B, pref koan.single_toolbar): there is no top
// toolbar. The top container is hidden in the overlay (below), the page card
// reaches the top of the window, and KoanSidebarNavView at the top of the
// sidebar has the navigation buttons and a URL pill; focusing the omnibox
// slides the toolbar down as a floating URL bar.
//
// Compact mode (Ctrl+S like Zen, pref koan.compact_mode.enabled; wins over
// single toolbar):
// - the vertical tab strip is collapsed to zero width and slides over the
//   page (upstream's expand-on-hover overlay) when the pointer reaches the
//   leading edge;
// - the top container (toolbar, bookmarks bar) moves into the BrowserView's
//   overlay view, above the page, and slides down with a compositor
//   transform when the pointer reaches the top edge or the omnibox is
//   focused;
// - the page is a card with a gutter on every side; the edge hot zones live
//   in that gutter, outside the page's native window.
// Both states are profile prefs shared by all windows. Keyboard shortcuts
// live in KoanShortcutController.
class KoanCompactModeController : public views::FocusChangeListener {
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

 private:
  Mode DesiredMode() const;
  void UpdateActiveSoon();
  void UpdateActive();
  void Activate(Mode mode);
  void Deactivate();

  void SetToolbarRevealed(bool revealed, bool animate);
  void ApplyToolbarTransform(bool animate);
  bool ShouldKeepToolbarRevealed() const;
  void CheckToolbarHide();

  // Single toolbar: adds KoanSidebarNavView to the top of the sidebar.
  void InstallSidebarNav();

  void CheckSidebarPeek();

  void OnSidebarHotZoneHovered(bool hovered);
  void OnToolbarHotZoneHovered(bool hovered);
  void LayoutHotZones();

  const raw_ptr<BrowserView> browser_view_;
  PrefChangeRegistrar pref_change_registrar_;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;

  Mode mode_ = Mode::kNone;
  bool toolbar_revealed_ = false;
  std::optional<bool> collapsed_before_;
  std::optional<size_t> top_container_index_;

  raw_ptr<KoanHotZone> sidebar_hot_zone_ = nullptr;
  raw_ptr<KoanHotZone> toolbar_hot_zone_ = nullptr;
  raw_ptr<views::View> sidebar_nav_ = nullptr;
  // Separate locks so the hot zone and a keyboard peek can't release each
  // other's hold on the sidebar.
  std::unique_ptr<ExpandOnHoverLock> hover_lock_;
  std::unique_ptr<ExpandOnHoverLock> peek_lock_;

  base::RepeatingTimer toolbar_hide_timer_;
  base::RepeatingTimer sidebar_peek_timer_;
  std::optional<base::TimeTicks> sidebar_unhovered_since_;
  base::TimeTicks sidebar_peek_started_;
  std::optional<base::TimeTicks> toolbar_unwanted_since_;

  base::WeakPtrFactory<KoanCompactModeController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_CONTROLLER_H_
