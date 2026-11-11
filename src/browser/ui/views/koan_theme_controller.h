// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_THEME_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_THEME_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/tab_groups/tab_group_id.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/views/koan_space_theme.h"
#include "ui/color/color_provider_key.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"

class BrowserView;
class Profile;

namespace gfx {
class Canvas;
}  // namespace gfx

namespace views {
class Widget;
class View;
class ViewTracker;
}  // namespace views

namespace koan {

class KoanThemePicker;
class KoanWorkspaceController;

// Zen's per-Space themes for one window (Zen's nsZenThemePicker, minus the
// panel, which is KoanThemePicker). Each Space keeps a gradient theme; the
// window paints the active Space's gradient behind its sidebar, toolbar and
// the gutter around the page, cross-fades to the next one when the Space
// changes, and takes the theme's light/dark mode and accent colour.
//
// A Space is a tab group (see KoanWorkspaceController), and its theme is
// the Space's theme field (KoanWorkspaceController::SpaceTheme), which the
// Spaces keep with the session.
//
// A theme gives the window tokens (theme::WindowTokens): its background,
// light/dark mode and colours. The mode and the primary colour reach every
// colour ID through the widget's colour key. A window whose profile uses a
// Chrome Web Store theme keeps that theme.
class KoanThemeController : public TabStripModelObserver,
                            public ThemeServiceObserver,
                            public ui::NativeThemeObserver,
                            public look::Observer,
                            public gfx::AnimationDelegate {
 public:
  using Space = std::optional<tab_groups::TabGroupId>;

  // Zen's zen.view.window.scheme, which is also the website appearance.
  enum class Scheme { kAutomatic, kLight, kDark };

  explicit KoanThemeController(BrowserView* browser_view);
  KoanThemeController(const KoanThemeController&) = delete;
  KoanThemeController& operator=(const KoanThemeController&) = delete;
  ~KoanThemeController() override;

  void OnAddedToWidget();
  void OnBrowserViewDestroying();

  // Whether this window shows Space themes (Zen's style of Koan's look,
  // Spaces on in this window, koan.workspaces.tint on, no Chrome Web Store
  // theme). The shadcn style is flat.
  bool IsEnabled() const;

  // The window's colour mode and accent, in its colour key
  // (AdjustWindowColorKey()): this controller is their one writer, from the
  // Space theme shown. Re-applies them and re-themes the window and its
  // child widgets (from upstream's focus mode, koan::ApplySpaceColor()).
  void ApplyWindowColors();
  // Whether nothing set the window's colour overrides (the Spaces leak
  // detector, KoanWorkspaceController::CheckSpaces()): this controller sets
  // the colours in the window's key instead.
  bool HasAppliedWindowColors() const;
  // The accent the Space theme puts in the window's colour key, if any.
  std::optional<SkColor> window_accent() const { return applied_color_; }
  // From BrowserWidget::GetColorProviderKey() (koan::AdjustWindowColorKey()):
  // the Space theme's colour mode and accent in the window's key.
  void AdjustWindowColorKey(ui::ColorProviderKey* key) const;
  // For a child widget of the window (koan_widget_color_key.h): back to the
  // window's own scheme, unless it is the window's chrome.
  void AdjustChildColorKey(const views::Widget* child,
                           ui::ColorProviderKey* key) const;

  // Paints the window's background (the active Space's gradient and grain)
  // behind `view`, aligned to the window.
  void PaintBackground(gfx::Canvas* canvas, const views::View* view) const;

  // Opens Zen's theme picker for `space` ("Theme…"), next to the
  // sidebar. The command palette and the Space menus call this.
  void ShowThemePicker(Space space);
  // The Create-a-Space form's Theme…: the picker for a Space not
  // created yet, from `stored` (a KoanWorkspaceController::SpaceTheme's).
  // The window previews it; `on_done` gets the theme when the picker
  // closes.
  // The welcome screen's colours step (Zen's ZenWelcome.mjs "colors"): the
  // picker for the active Space over `anchor_in_screen`, which it overlaps,
  // as Zen's openPopup(panel, anchor, {position: "overlap"}). It stays open
  // while the welcome page is used and on Esc (the page handles Esc). Called
  // again while it is open, it moves to the new rect (on a resize).
  void ShowThemePickerForWelcome(const gfx::Rect& anchor_in_screen);
  // Closes it and saves the theme. Does nothing if it isn't open.
  void CloseThemePickerForWelcome();
  // The panel's size, to size the anchor (Zen measures the panel). Empty
  // when the picker can't open (ShowThemePickerForWelcome() does nothing).
  gfx::Size ThemePickerSize() const;

  void ShowThemePickerForNewSpace(
      const std::string& stored,
      base::OnceCallback<void(std::string stored)> on_done);
  // The same for the active Space.
  void ShowThemePickerForActiveSpace();
  bool IsThemePickerShowing() const;
  KoanThemePicker* theme_picker() const { return picker_.get(); }

  // The window scheme (Automatic / Light / Dark), which websites follow.
  Scheme GetScheme() const;
  void SetScheme(Scheme scheme);
  // Zen's dark theme styles (Settings > Look and Feel).
  theme::DarkStyle GetDarkStyle() const;

  // The colours' inputs for this window.
  theme::Environment GetEnvironment() const;
  // Whether the picker shows the custom colour list.
  bool ShowsCustomColors() const;

  // The theme picker: live preview of `theme` for the Space being edited,
  // and saving it.
  // While `dragging`, only the background follows; the window's colours
  // (mode and accent) follow once the drag ends, so a drag doesn't restyle
  // the whole window at every step.
  void PreviewTheme(Space space, const theme::SpaceTheme& theme, bool dragging);
  void SaveTheme(Space space, const theme::SpaceTheme& theme);
  theme::SpaceTheme GetTheme(Space space) const;

  // Whether the window shows a Space theme now (IsEnabled() as of the last
  // update, without its pref and service lookups, for every paint).
  bool paints_background() const { return current_.has_value(); }
  // Whether it paints the window translucent (over the glass).
  bool PaintsTranslucent() const;
  // Whether the active Space's theme sets the window's light/dark mode.
  bool sets_color_mode() const { return current_ && current_->dark; }

  // The background the window paints now (the target of a running
  // animation), described like CSS, for tests.
  std::string DescribeCurrentBackground() const;
  // The mode the active Space's theme gives the window ("dark", "light"),
  // or "" where the window's scheme decides, for tests.
  std::string DescribeCurrentMode() const;

  // TabStripModelObserver:
  void OnTabGroupFocusChanged(
      std::optional<tab_groups::TabGroupId> new_focused_group_id,
      std::optional<tab_groups::TabGroupId> old_focused_group_id) override;
  void TabGroupedStateChanged(TabStripModel* tab_strip_model,
                              std::optional<tab_groups::TabGroupId> old_group,
                              std::optional<tab_groups::TabGroupId> new_group,
                              tabs::TabInterface* tab,
                              int index) override;

  // ThemeServiceObserver:
  void OnThemeChanged() override;

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

  // look::Observer:
  void OnLookChanged() override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

 private:
  // Whether `child` is the window's own chrome in another widget, which
  // keeps the Space's mode.
  bool IsWindowChrome(const views::Widget* child) const;
  // Whether `view` floats over the page: the sidebar, or the top container
  // and toolbar, in compact mode.
  bool FloatsOverPage(const views::View* view) const;
  Profile* profile() const;
  KoanWorkspaceController* workspaces() const;
  Space active_space() const;
  // The Space's theme as stored (theme::GetTokens()), empty for the default
  // theme.
  base::DictValue GetStoredTheme(Space space) const;
  // `welcome_anchor`: for the welcome screen, over that rect.
  void OpenPicker(Space space,
                  const theme::SpaceTheme& theme,
                  std::optional<gfx::Rect> welcome_anchor = std::nullopt);

  // Shows the active Space's theme (or the one being previewed), cross-
  // fading from the previous one when `animate`.
  void Update(bool animate);
  // Cross-fades from what the window shows now to `current_`'s successor.
  void StartSwitchAnimation();
  // The cross-fade's progress, 1 when none runs.
  double SwitchProgress() const;
  // The grain strength the window shows, fading to `current_`'s.
  double DisplayedGrain() const;
  // Repaints every view that paints the window background.
  void InvalidateBackground();
  // Restores the user's colour if the workspace tint of earlier Koan
  // versions is still applied.
  void OnPickerClosed();
  void DeleteClosedPicker();

  const raw_ptr<BrowserView> browser_view_;
  bool observing_tab_strip_ = false;
  bool observing_look_ = false;
  base::CallbackListSubscription glass_subscription_;
  base::CallbackListSubscription paint_as_active_subscription_;

  // What the window shows: `current_` fading in over the backgrounds in
  // `fading_` (weighted by `opacity`) while the animation runs.
  struct Fading {
    theme::Background background;
    double opacity = 1;
  };
  std::optional<theme::WindowTokens> current_;
  std::vector<Fading> fading_;
  gfx::LinearAnimation switch_animation_;
  // Fades the grain from `grain_from_` to `current_`'s.
  gfx::LinearAnimation grain_animation_;
  double grain_from_ = 0;
  // The views that paint the window background, as they paint.
  mutable std::vector<std::unique_ptr<views::ViewTracker>> painters_;

  // The Space the picker edits and its unsaved theme.
  // Sets the overrides from `current_` (none without it); whether they
  // changed.
  bool SetWindowColors();
  // The Space `current_` is for, and the overrides it set.
  Space current_space_;
  std::optional<ui::ColorProviderKey::ColorMode> applied_mode_;
  std::optional<SkColor> applied_color_;
  // The window's scheme before the Space's mode, as its key last had it.
  mutable ui::ColorProviderKey::ColorMode window_mode_ =
      ui::ColorProviderKey::ColorMode::kLight;

  Space editing_space_;
  std::optional<theme::SpaceTheme> preview_;
  bool preview_dragging_ = false;
  std::unique_ptr<KoanThemePicker> picker_;
  // A picker whose widget is closing, until it can be deleted.
  std::unique_ptr<KoanThemePicker> closed_picker_;
  // Set while the picker edits a Space the Create-a-Space form will make:
  // where the theme goes instead of a Space.
  base::OnceCallback<void(std::string stored)> new_space_done_;

  PrefChangeRegistrar pref_change_registrar_;
  base::ScopedObservation<ThemeService, ThemeServiceObserver>
      theme_observation_{this};
  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observation_{this};

  base::WeakPtrFactory<KoanThemeController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_THEME_CONTROLLER_H_
