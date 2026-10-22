// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_THEME_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_THEME_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/saved_tab_groups/public/tab_group_sync_service.h"
#include "components/tab_groups/tab_group_id.h"
#include "koan/browser/ui/views/koan_space_theme.h"
#include "koan/browser/ui/views/koan_space_theme_store.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"

class BrowserView;
class Profile;

namespace gfx {
class Canvas;
class Point;
}  // namespace gfx

namespace views {
class MenuRunner;
class View;
class ViewTracker;
}  // namespace views

namespace koan {

class KoanThemePicker;

// Zen's per-Space themes for one window (Zen's nsZenThemePicker, minus the
// panel, which is KoanThemePicker). Each Space keeps a gradient theme; the
// window paints the active Space's gradient behind its sidebar, toolbar and
// the gutter around the page, cross-fades to the next one when the Space
// changes, and takes the theme's light/dark mode and accent colour.
//
// A Space is a tab group (see KoanWorkspaceController); nullopt is the
// default Space ("All tabs"). Themes live in the profile pref
// koan.spaces.themes, keyed by the group's saved-tab-group GUID, which
// survives restarts (tab group ids don't).
//
// A theme gives the window tokens (theme::WindowTokens): its background,
// light/dark mode and colours. The mode and the primary colour reach every
// colour ID through the widget's colour key. A window whose profile uses a
// Chrome Web Store theme keeps that theme.
class KoanThemeController : public TabStripModelObserver,
                            public tab_groups::TabGroupSyncService::Observer,
                            public ThemeServiceObserver,
                            public ui::NativeThemeObserver,
                            public ui::SimpleMenuModel::Delegate,
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

  // Whether this window shows Space themes (feature on, normal window,
  // koan.workspaces.tint on, no Chrome Web Store theme).
  bool IsEnabled() const;

  // Paints the window's background (the active Space's gradient and grain)
  // behind `view`, aligned to the window.
  void PaintBackground(gfx::Canvas* canvas, const views::View* view) const;

  // Opens Zen's theme picker for `space` ("Edit Theme…"), next to the
  // sidebar. The command palette and the Space menus call this.
  void ShowThemePicker(Space space);
  // The same for the active Space.
  void ShowThemePickerForActiveSpace();
  bool IsThemePickerShowing() const;
  KoanThemePicker* theme_picker() const { return picker_.get(); }

  // The Space menu: Edit Theme… and the website appearance. Shown by
  // right-clicking a Space in the switcher.
  void ShowSpaceMenu(Space space,
                     views::View* source,
                     const gfx::Point& point,
                     ui::mojom::MenuSourceType source_type);

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
  // Whether the active Space's theme sets the window's light/dark mode.
  bool sets_color_mode() const { return current_ && current_->dark; }

  // The background the window paints now (the target of a running
  // animation), described like CSS, for tests.
  std::string DescribeCurrentBackground() const;
  // The mode the active Space's theme gives the window ("dark", "light"),
  // or "" where the window's scheme decides, for tests.
  std::string DescribeCurrentMode() const;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;

  // TabStripModelObserver:
  void OnTabGroupFocusChanged(
      std::optional<tab_groups::TabGroupId> new_focused_group_id,
      std::optional<tab_groups::TabGroupId> old_focused_group_id) override;

  // tab_groups::TabGroupSyncService::Observer:
  void OnInitialized() override;
  void OnTabGroupLocalIdChanged(
      const base::Uuid& sync_id,
      const std::optional<tab_groups::LocalTabGroupID>& local_id) override;
  void OnWillBeDestroyed() override;

  // ThemeServiceObserver:
  void OnThemeChanged() override;

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

 private:
  Profile* profile() const;
  Space active_space() const;

  // Shows the active Space's theme (or the one being previewed), cross-
  // fading from the previous one when `animate`.
  void Update(bool animate);
  // Cross-fades from what the window shows now to `current_`'s successor.
  void StartSwitchAnimation();
  // The cross-fade's progress, 1 when none runs.
  double SwitchProgress() const;
  // Repaints every view that paints the window background.
  void InvalidateBackground();
  // Restores the user's colour if the workspace tint of earlier Koan
  // versions is still applied.
  void MigrateWorkspaceTint();
  void OnPickerClosed();
  void DeleteClosedPicker();

  const raw_ptr<BrowserView> browser_view_;
  bool observing_tab_strip_ = false;

  KoanSpaceThemeStore store_;

  // What the window shows: `current_` fading in over the backgrounds in
  // `fading_` (weighted by `opacity`) while the animation runs.
  struct Fading {
    theme::Background background;
    double opacity = 1;
  };
  std::optional<theme::WindowTokens> current_;
  std::vector<Fading> fading_;
  gfx::LinearAnimation switch_animation_;
  // The views that paint the window background, as they paint.
  mutable std::vector<std::unique_ptr<views::ViewTracker>> painters_;

  // The Space the picker edits and its unsaved theme.
  Space editing_space_;
  std::optional<theme::SpaceTheme> preview_;
  bool preview_dragging_ = false;
  std::unique_ptr<KoanThemePicker> picker_;
  // A picker whose widget is closing, until it can be deleted.
  std::unique_ptr<KoanThemePicker> closed_picker_;

  // The Space menu.
  Space menu_space_;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> appearance_menu_;
  std::unique_ptr<views::MenuRunner> menu_runner_;

  PrefChangeRegistrar pref_change_registrar_;
  base::ScopedObservation<tab_groups::TabGroupSyncService,
                          tab_groups::TabGroupSyncService::Observer>
      sync_observation_{this};
  base::ScopedObservation<ThemeService, ThemeServiceObserver>
      theme_observation_{this};
  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observation_{this};

  base::WeakPtrFactory<KoanThemeController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_THEME_CONTROLLER_H_
