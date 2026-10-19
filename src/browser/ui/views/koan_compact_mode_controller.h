// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
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
#include "ui/base/accelerators/accelerator.h"
#include "ui/views/focus/focus_manager.h"

class BrowserView;
class ExpandOnHoverLock;

namespace koan {

class KoanHotZone;

// Zen-style compact mode for one window. When active:
// - the vertical tab strip is collapsed to zero width and slides over the
//   page (upstream's expand-on-hover overlay) when the pointer reaches the
//   leading edge;
// - the top container (toolbar, bookmarks bar) moves into the BrowserView's
//   overlay view, above the page, and slides down with a compositor
//   transform when the pointer reaches the top edge or the omnibox is
//   focused;
// - the page is a card with a gutter on every side; the edge hot zones live
//   in that gutter, outside the page's native window.
// Toggled with Ctrl+Alt+C; the state is the profile pref
// koan::prefs::kCompactModeEnabled, shared by all windows.
class KoanCompactModeController : public views::FocusChangeListener,
                                  public ui::AcceleratorTarget {
 public:
  explicit KoanCompactModeController(BrowserView* browser_view);
  KoanCompactModeController(const KoanCompactModeController&) = delete;
  KoanCompactModeController& operator=(const KoanCompactModeController&) =
      delete;
  ~KoanCompactModeController() override;

  bool is_active() const { return active_; }
  bool is_toolbar_revealed() const { return toolbar_revealed_; }

  // Called by KoanBrowserViewController.
  void OnAddedToWidget();
  void OnBrowserViewLayout();
  void OnBrowserViewDestroying();

  // Flips the profile pref.
  void Toggle();

  // ui::AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;

  // views::FocusChangeListener:
  void OnDidChangeFocus(views::View* focused_before,
                        views::View* focused_now) override;

 private:
  bool ShouldBeActive() const;
  void UpdateActiveSoon();
  void UpdateActive();
  void Activate();
  void Deactivate();

  void SetToolbarRevealed(bool revealed, bool animate);
  void ApplyToolbarTransform(bool animate);
  bool ShouldKeepToolbarRevealed() const;
  void CheckToolbarHide();

  // Koan's own minimise/maximise/close buttons, in the top container's
  // trailing corner where the frame's caption buttons would be: in compact
  // mode the page covers the frame-drawn ones, so these slide in with the
  // toolbar instead. Only shown when the frame reserves room for caption
  // buttons there (custom, client-side-decorated frame).
  void UpdateWindowControls();

  void OnSidebarHotZoneHovered(bool hovered);
  void OnToolbarHotZoneHovered(bool hovered);
  void LayoutHotZones();

  const raw_ptr<BrowserView> browser_view_;
  PrefChangeRegistrar pref_change_registrar_;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;
  const ui::Accelerator toggle_accelerator_;

  bool active_ = false;
  bool toolbar_revealed_ = false;
  std::optional<bool> collapsed_before_;
  std::optional<size_t> top_container_index_;

  raw_ptr<KoanHotZone> sidebar_hot_zone_ = nullptr;
  raw_ptr<KoanHotZone> toolbar_hot_zone_ = nullptr;
  raw_ptr<views::View> window_controls_ = nullptr;
  std::unique_ptr<ExpandOnHoverLock> sidebar_lock_;

  base::RepeatingTimer toolbar_hide_timer_;
  std::optional<base::TimeTicks> toolbar_unwanted_since_;

  base::WeakPtrFactory<KoanCompactModeController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_CONTROLLER_H_
