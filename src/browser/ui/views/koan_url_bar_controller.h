// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_URL_BAR_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_URL_BAR_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string>

#include "base/callback_list.h"
#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/events/event_handler.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_tracker.h"

class BrowserView;
class LocationBarView;

namespace ui {
class Layer;
}

namespace views {
class View;
}  // namespace views

namespace koan {

class KoanCompactModeController;
class KoanUrlBarBackdrop;
class KoanUrlBarCardDecoration;
class KoanUrlBarCardFill;

// Zen's URL bar for one window. In single toolbar / compact mode the
// omnibox (the real one, lifted out of the hidden toolbar) opens in one of
// two places while it has focus:
//
// - in the pill: over the sidebar's URL pill, as if the pill were edited;
// - floating: Zen's card, centred over the window (min(2/3 of its width,
//   750 DIP) wide, larger font, round corners, Zen's long soft shadow over
//   the page). Clicking outside closes it.
//
// Where it opens follows koan.urlbar.behavior (Zen's zen.urlbar.behavior):
// "normal" edits in the pill, "floating-on-type" (the default) edits in the
// pill and floats on the first keystroke, "float" floats right away. Without
// a pill to edit in (compact mode with the sidebar hidden) it floats.
//
// With koan.urlbar.replace_newtab (Zen's zen.urlbar.replace-newtab), Ctrl+T
// and the sidebar's new-tab button open the bar empty instead of a tab;
// opening something from it opens it in a new tab, closing it (Esc, a click
// outside, Ctrl+T again) creates nothing, and an unfinished search comes
// back if the bar opens for a new tab again within Zen's 45 seconds.
//
// In the multiple-toolbars layout the omnibox stays in the toolbar; only the
// new-tab behaviour applies.
class KoanUrlBarController : public views::ViewObserver,
                             public ui::EventHandler,
                             public ui::NativeThemeObserver {
 public:
  enum class Placement { kNone, kInPill, kFloating };

  KoanUrlBarController(BrowserView* browser_view,
                       KoanCompactModeController* compact_mode);
  KoanUrlBarController(const KoanUrlBarController&) = delete;
  KoanUrlBarController& operator=(const KoanUrlBarController&) = delete;
  ~KoanUrlBarController() override;

  Placement placement() const { return placement_; }
  // Whether the omnibox is lifted out of the toolbar (in the pill or
  // floating).
  bool is_open() const { return placement_ != Placement::kNone; }
  // Whether the omnibox is open in place of a new tab.
  bool is_new_tab() const { return new_tab_; }

  // Opens the omnibox in place of a new tab (Ctrl+T, the new-tab button);
  // closes it if it is already open for one, like Zen. Returns false if the
  // window should open a tab as usual (replace-newtab off, ...).
  bool OpenForNewTab();
  // Zen's "search tabs" (Shift+Cmd+A): the bar opens with the open tabs as its
  // scope (Chromium's "@tabs"). False if the bar can't open now (the caller
  // falls back to focusing the omnibox).
  bool OpenForTabSearch();
  // Closes the omnibox without opening anything: the page gets focus back.
  void Close();

  // Called by KoanCompactModeController.
  void OnBrowserViewLayout();
  // The layout mode or the behaviour pref changed.
  void OnModeChanged();
  // Focus moved to `focused_now` (null if focus left the window).
  void OnFocusChanged(views::View* focused_now);
  // Puts the omnibox back into the toolbar (before the top container is
  // re-parented, or the window goes).
  void EndEditing();

  // koan_url_bar.h hooks.
  void OnOmniboxChanged(bool user_input_in_progress);

  // Whether the floating bar is the command-dialog card (in either look): it
  // has a magnifier, a "Search..." placeholder and a divider.
  bool IsCommandCard() const;
  // Whether it is Zen's (not shadcn's) card: 62pt tall.
  bool IsFloatingZenCard() const;
  // Whether the open card is a glass pane (Zen's acrylic-urlbar, see
  // UpdateGlassCard()).
  bool IsGlassCard() const { return glass_card_; }
  // The card's layer while it is open, null otherwise: the one a glass card
  // blurs behind (Layer::SetBackgroundBlur), over a fill of
  // kColorKoanFloatingUrlBarGlass (the layer itself stays at full opacity). It
  // is clipped to the card's rounded corners.
  ui::Layer* card_layer() const;
  // Whether what the user opens now should go to a new tab; ends the new-tab
  // session if so.
  bool TakeNewTabDisposition();

  // views::ViewObserver:
  void OnViewVisibilityChanged(views::View* observed_view,
                               views::View* starting_view,
                               bool visible) override;
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewIsDeleting(views::View* observed_view) override;

  // ui::EventHandler (the omnibox's keys while the bar is open):
  void OnKeyEvent(ui::KeyEvent* event) override;

  // ui::NativeThemeObserver (Reduce Transparency changes with the system's
  // settings):
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

 private:
  // Routes the window's "New tab" action (the sidebar's new-tab button, the
  // app menu) through OpenForNewTab().
  void InstallNewTabAction();
  void OnNewTabAction();

  // Whether the floating bar can open now: the feature, a normal window with
  // tabs and an omnibox, not in a fullscreen that hides the toolbar's view.
  bool CanOpenBar() const;
  Placement DesiredPlacement() const;
  void SetPlacement(Placement placement);
  bool IsPillAvailable() const;
  std::string Behavior() const;
  LocationBarView* location_bar() const;

  void ApplyCardStyle();
  // Makes the open card a glass pane or not, live: Zen's acrylic-urlbar (a
  // pref), not with macOS's Reduce Transparency on, in an active window
  // (zen-omnibox.css: `prefers-reduced-transparency`, `:-moz-window-inactive`).
  // It is the window's own state: a translucent fill, the blur behind it, and
  // the views over the fill clear (the location bar, the toolbar and the top
  // container), with no colour provider rebuilt and no other window touched.
  void UpdateGlassCard();
  void Layout();
  gfx::Rect CardBounds() const;
  void HideToolbarSiblings();
  // Shows or hides the location bar's page actions for the placement (Zen's
  // URL bar has none).
  void RefreshPageActions();
  // Handles the omnibox's keys (Esc) while the bar is open or open for a new
  // tab.
  void UpdateKeyHandler();
  void FloatOnType();
  void EndNewTabSession(bool committed);

  const raw_ptr<BrowserView> browser_view_;
  const raw_ptr<KoanCompactModeController> compact_mode_;
  Placement placement_ = Placement::kNone;
  // Once floating, the bar stays floating until it closes, like Zen's.
  bool floated_ = false;
  bool user_input_in_progress_ = false;

  bool new_tab_ = false;
  // Zen's zen.urlbar.wait-to-clear: what was typed into a new-tab bar that
  // closed without opening anything, offered again for a while.
  std::u16string last_search_;
  base::TimeTicks last_search_time_;

  // While open: the toolbar's other views, hidden, and whether each is to be
  // visible afterwards. Entries go when a view is deleted.
  base::flat_map<views::View*, bool> restore_;
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      sibling_observation_{this};
  bool changing_toolbar_visibility_ = false;
  bool laying_out_ = false;
  // See IsGlassCard(); kept in step by UpdateGlassCard(), per window.
  bool glass_card_ = false;
  PrefChangeRegistrar prefs_registrar_;
  base::CallbackListSubscription paint_as_active_subscription_;
  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      theme_observation_{this};
  base::ScopedObservation<views::View, views::ViewObserver>
      top_container_observation_{this};
  base::ScopedObservation<views::View, views::ViewObserver>
      toolbar_observation_{this};

  // The floating card's larger font: the omnibox font from before.
  std::optional<gfx::FontList> omnibox_font_;
  // Behind the floating card: catches clicks outside it and paints Zen's
  // shadow. A child of the top container's parent (the overlay).
  raw_ptr<KoanUrlBarBackdrop> backdrop_ = nullptr;
  // The shadcn card's magnifier and divider, over the toolbar.
  raw_ptr<KoanUrlBarCardDecoration> decoration_ = nullptr;
  // The card colour behind the location bar, in the toolbar.
  raw_ptr<KoanUrlBarCardFill> fill_ = nullptr;
  // The card's size at the last layout (empty while closed).
  gfx::Size card_size_;
  // The omnibox, while this handles its keys (Esc).
  views::ViewTracker key_target_;

  base::WeakPtrFactory<KoanUrlBarController> weak_factory_{this};
};

// Copies the active tab's URL (or a Markdown link to it) and confirms with
// Zen's toast. Ctrl+Shift+C, Ctrl+Shift+Alt+C and the pill's copy button.
void CopyCurrentUrl(BrowserView* browser_view, bool markdown);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_URL_BAR_CONTROLLER_H_
