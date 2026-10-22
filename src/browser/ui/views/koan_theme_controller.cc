// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_theme_controller.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_group_sync/tab_group_sync_service_factory.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_space_background.h"
#include "koan/browser/ui/views/koan_system_accent.h"
#include "koan/browser/ui/views/koan_theme_picker.h"
#include "koan/browser/ui/views/koan_window_background.h"
#include "ui/base/models/image_model.h"
#include "ui/base/models/menu_separator_types.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// zen.workspaces.switch-animation-duration.
constexpr base::TimeDelta kSwitchDuration = base::Milliseconds(250);

// Zen animates the switch with Motion's spring (bounce 0) over
// kSwitchDuration: a critically damped spring that settles (to 0.1%) at
// the end.
double SpringProgress(double t) {
  constexpr double kOmega = 9.23;
  return t >= 1 ? 1 : 1 - (1 + kOmega * t) * std::exp(-kOmega * t);
}

enum MenuCommand {
  kEditTheme = 1,
  kAppearanceMenu,
  kWebsitesWillUse,
  kAppearanceAutomatic,
  kAppearanceLight,
  kAppearanceDark,
};

constexpr int kAppearanceGroup = 1;

// Fading-out backgrounds weaker than this are dropped.
constexpr double kMinFadingOpacity = 0.01;

}  // namespace

KoanThemeController::KoanThemeController(BrowserView* browser_view)
    : browser_view_(browser_view),
      store_(browser_view->GetProfile()),
      switch_animation_(kSwitchDuration,
                        gfx::LinearAnimation::kDefaultFrameRate,
                        this) {}

KoanThemeController::~KoanThemeController() = default;

Profile* KoanThemeController::profile() const {
  return browser_view_->GetProfile();
}

bool KoanThemeController::IsEnabled() const {
  if (!features::IsWorkspacesEnabled() || !browser_view_->GetIsNormalType() ||
      profile()->IsOffTheRecord() ||
      !profile()->GetPrefs()->GetBoolean(prefs::kWorkspaceTint)) {
    return false;
  }
  // A Chrome Web Store theme keeps deciding the window's colours and images:
  // the Space's accent would turn it off in this window (BrowserWidget drops
  // the custom theme under a user colour override, as for Focus Mode).
  const ThemeService* theme_service =
      ThemeServiceFactory::GetForProfile(profile());
  return !theme_service || !theme_service->UsingExtensionTheme();
}

void KoanThemeController::OnAddedToWidget() {
  if (!features::IsWorkspacesEnabled() || !browser_view_->GetIsNormalType() ||
      profile()->IsOffTheRecord()) {
    return;
  }
  MigrateWorkspaceTint();
  browser_view_->browser()->GetTabStripModel()->AddObserver(this);
  observing_tab_strip_ = true;
  pref_change_registrar_.Init(profile()->GetPrefs());
  for (const char* pref :
       {prefs::kSpaceThemes, prefs::kDarkThemeStyle, prefs::kWorkspaceTint}) {
    pref_change_registrar_.Add(
        pref, base::BindRepeating(&KoanThemeController::Update,
                                  base::Unretained(this), /*animate=*/false));
  }
  if (auto* service =
          tab_groups::TabGroupSyncServiceFactory::GetForProfile(profile())) {
    sync_observation_.Observe(service);
  }
  if (ThemeService* theme_service =
          ThemeServiceFactory::GetForProfile(profile())) {
    theme_observation_.Observe(theme_service);
  }
  native_theme_observation_.Observe(ui::NativeTheme::GetInstanceForNativeUi());
  Update(/*animate=*/false);
}

void KoanThemeController::OnBrowserViewDestroying() {
  picker_.reset();
  closed_picker_.reset();
  menu_runner_.reset();
  switch_animation_.Stop();
  if (observing_tab_strip_) {
    browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
    observing_tab_strip_ = false;
  }
  pref_change_registrar_.RemoveAll();
  sync_observation_.Reset();
  theme_observation_.Reset();
  native_theme_observation_.Reset();
}

KoanThemeController::Space KoanThemeController::active_space() const {
  return browser_view_->browser()->GetTabStripModel()->GetFocusedGroup();
}

theme::SpaceTheme KoanThemeController::GetTheme(Space space) const {
  return theme::SpaceTheme::FromStored(store_.GetStoredTheme(space));
}

void KoanThemeController::SaveTheme(Space space,
                                    const theme::SpaceTheme& theme) {
  // A saved theme reaches every window through the pref observer.
  if (store_.SetSpaceTheme(space, theme)) {
    Update(/*animate=*/false);
  }
}

void KoanThemeController::PreviewTheme(Space space,
                                       const theme::SpaceTheme& theme,
                                       bool dragging) {
  editing_space_ = space;
  preview_ = theme;
  preview_dragging_ = dragging;
  Update(/*animate=*/false);
}

KoanThemeController::Scheme KoanThemeController::GetScheme() const {
  const ThemeService* theme_service =
      ThemeServiceFactory::GetForProfile(profile());
  switch (theme_service ? theme_service->GetBrowserColorScheme()
                        : ThemeService::BrowserColorScheme::kSystem) {
    case ThemeService::BrowserColorScheme::kLight:
      return Scheme::kLight;
    case ThemeService::BrowserColorScheme::kDark:
      return Scheme::kDark;
    case ThemeService::BrowserColorScheme::kSystem:
      return Scheme::kAutomatic;
  }
  return Scheme::kAutomatic;
}

void KoanThemeController::SetScheme(Scheme scheme) {
  ThemeService* theme_service = ThemeServiceFactory::GetForProfile(profile());
  if (!theme_service) {
    return;
  }
  theme_service->SetBrowserColorScheme(
      scheme == Scheme::kLight  ? ThemeService::BrowserColorScheme::kLight
      : scheme == Scheme::kDark ? ThemeService::BrowserColorScheme::kDark
                                : ThemeService::BrowserColorScheme::kSystem);
}

theme::DarkStyle KoanThemeController::GetDarkStyle() const {
  const int style = profile()->GetPrefs()->GetInteger(prefs::kDarkThemeStyle);
  return style == static_cast<int>(theme::DarkStyle::kNight)
             ? theme::DarkStyle::kNight
         : style == static_cast<int>(theme::DarkStyle::kColorful)
             ? theme::DarkStyle::kColorful
             : theme::DarkStyle::kDefault;
}

theme::Environment KoanThemeController::GetEnvironment() const {
  theme::Environment env;
  const ThemeService* theme_service =
      ThemeServiceFactory::GetForProfile(profile());
  env.dark = theme_service ? theme_service->BrowserUsesDarkColors()
                           : ui::NativeTheme::GetInstanceForNativeUi()
                                     ->preferred_color_scheme() ==
                                 ui::NativeTheme::PreferredColorScheme::kDark;
  env.dark_style = GetDarkStyle();
  if (std::optional<SkColor> accent = GetSystemAccentColor()) {
    env.system_accent = theme::FromSkColor(*accent);
  }
  // The window's own colours, in the window's scheme (a theme is blended
  // into them, so not in the mode the Space's theme gives the window). They
  // depend on the mode only.
  ui::ColorProviderKey key;
  key.color_mode = env.dark ? ui::ColorProviderKey::ColorMode::kDark
                            : ui::ColorProviderKey::ColorMode::kLight;
  if (const ui::ColorProvider* provider =
          ui::ColorProviderManager::Get().GetColorProviderFor(key)) {
    env.window = theme::FromSkColor(provider->GetColor(kColorKoanThemeWindow));
    env.default_background = theme::FromSkColor(
        provider->GetColor(kColorKoanThemeDefaultBackground));
    env.tint_base =
        theme::FromSkColor(provider->GetColor(kColorKoanThemeTintBase));
  }
  return env;
}

bool KoanThemeController::ShowsCustomColors() const {
  return profile()->GetPrefs()->GetBoolean(prefs::kShowCustomColors);
}

void KoanThemeController::Update(bool animate) {
  views::Widget* widget = browser_view_->GetWidget();
  if (!IsEnabled()) {
    if (current_) {
      current_.reset();
      switch_animation_.Stop();
      fading_.clear();
      if (widget) {
        widget->SetColorModeOverride(std::nullopt);
        widget->SetUserColorOverride(std::nullopt);
        // Views that cache the window's solid colour pick it up again.
        widget->ThemeChanged();
      }
    }
    return;
  }
  const Space space = active_space();
  const theme::Environment env = GetEnvironment();
  theme::WindowTokens tokens =
      preview_ && editing_space_ == space
          ? theme::GetZenTokens(env, *preview_)
          : theme::GetTokens(env, store_.GetStoredTheme(space));
  if (preview_dragging_ && current_) {
    tokens.dark = current_->dark;
    tokens.colors = current_->colors;
  }
  if (picker_) {
    picker_->OnEnvironmentChanged();
  }
  if (current_ == tokens) {
    return;
  }
  const bool first = !current_;
  // No cross-fade with reduced motion.
  if (animate && current_ && current_->background != tokens.background &&
      gfx::Animation::ShouldRenderRichAnimation()) {
    StartSwitchAnimation();
  }
  current_ = std::move(tokens);

  // The theme's mode and accent reach every colour of the window (Chrome's
  // and Koan's colour IDs) through its colour key.
  if (widget) {
    const auto mode_override = widget->color_mode_override();
    const auto color_override = widget->user_color_override();
    widget->SetColorModeOverride(
        current_->dark
            ? std::make_optional(*current_->dark
                                     ? ui::ColorProviderKey::ColorMode::kDark
                                     : ui::ColorProviderKey::ColorMode::kLight)
            : std::nullopt);
    // The theme's primary colour is the window's accent (Chrome's and
    // Koan's colour IDs derive from it).
    const auto primary = current_->colors.find(theme::Token::kPrimary);
    widget->SetUserColorOverride(primary != current_->colors.end()
                                     ? std::make_optional(primary->second)
                                     : std::nullopt);
    // (Setting an override already updated the window.)
    if (first && widget->color_mode_override() == mode_override &&
        widget->user_color_override() == color_override) {
      // Views that cache the window's solid colour (the gutter's layer)
      // switch to painting the background.
      widget->ThemeChanged();
    }
  }
  InvalidateBackground();
}

void KoanThemeController::StartSwitchAnimation() {
  // What shows now fades out: the current background and, when Spaces
  // change during a cross-fade, the ones still fading out.
  const double progress = SwitchProgress();
  std::vector<Fading> fading = switch_animation_.is_animating()
                                   ? std::move(fading_)
                                   : std::vector<Fading>();
  // (Stopping ends the animation, which clears `fading_`.)
  switch_animation_.Stop();
  for (Fading& entry : fading) {
    entry.opacity *= 1 - progress;
  }
  fading.push_back({current_->background, progress});
  std::erase_if(fading, [](const Fading& entry) {
    return entry.opacity < kMinFadingOpacity;
  });
  fading_ = std::move(fading);
  switch_animation_.Start();
}

double KoanThemeController::SwitchProgress() const {
  return switch_animation_.is_animating()
             ? SpringProgress(switch_animation_.GetCurrentValue())
             : 1;
}

void KoanThemeController::InvalidateBackground() {
  // The views that paint the window background (ThemedBackground's users:
  // the frame, toolbar, sidebar and the gutter behind the page), as they
  // painted it last. One that hasn't yet paints the new one when it does.
  std::erase_if(painters_, [](const std::unique_ptr<views::ViewTracker>& t) {
    return !t->view();
  });
  for (const std::unique_ptr<views::ViewTracker>& painter : painters_) {
    painter->view()->SchedulePaint();
  }
}

void KoanThemeController::PaintBackground(gfx::Canvas* canvas,
                                          const views::View* view) const {
  const views::Widget* widget = browser_view_->GetWidget();
  if (!current_ || !widget || !widget->GetRootView() || !view->GetWidget()) {
    return;
  }
  if (std::ranges::none_of(
          painters_, [view](const std::unique_ptr<views::ViewTracker>& t) {
            return t->view() == view;
          })) {
    // (The tracker only watches for the view's deletion.)
    painters_.push_back(
        std::make_unique<views::ViewTracker>(const_cast<views::View*>(view)));
  }
  // The gradient is sized to the browser window (Zen's
  // #zen-browser-background), also for views in another widget (the
  // toolbar in macOS fullscreen, compact mode's overlay).
  const views::View* root = widget->GetRootView();
  gfx::Point origin;
  views::View::ConvertPointToScreen(view, &origin);
  gfx::Point root_origin;
  views::View::ConvertPointToScreen(root, &root_origin);
  gfx::RectF box(root->GetLocalBounds());
  box.Offset(root_origin.x() - origin.x(), root_origin.y() - origin.y());

  const theme::Background& now = current_->background;
  const double progress = SwitchProgress();
  // Zen fades the old gradient out while the new one fades in, over the
  // window's own background.
  theme::PaintBase(canvas, box, now.base);
  double grain = now.grain * progress;
  if (progress < 1) {
    for (const Fading& fading : fading_) {
      const double opacity = fading.opacity * (1 - progress);
      theme::PaintLayers(canvas, box, fading.background.layers, opacity);
      grain += fading.background.grain * opacity;
    }
  } else {
    grain = now.grain;
  }
  theme::PaintLayers(canvas, box, now.layers, progress);
  theme::PaintGrain(canvas, box, grain);
}

std::string KoanThemeController::DescribeCurrentBackground() const {
  return current_ ? theme::DescribeBackground(current_->background)
                  : std::string();
}

std::string KoanThemeController::DescribeCurrentMode() const {
  if (!current_ || !current_->dark) {
    return std::string();
  }
  return *current_->dark ? "dark" : "light";
}

void KoanThemeController::ShowThemePickerForActiveSpace() {
  ShowThemePicker(active_space());
}

void KoanThemeController::ShowThemePicker(Space space) {
  if (!IsEnabled() || !browser_view_->GetWidget()) {
    return;
  }
  if (picker_) {
    // An open picker saves its edits and closes first.
    OnPickerClosed();
    closed_picker_.reset();
  }
  editing_space_ = space;
  preview_.reset();
  preview_dragging_ = false;

  // Zen opens the panel beside the sidebar's tab list, overlapping the
  // sidebar by 10px, on its other side when the tabs are on the right.
  constexpr int kOverlap = 10;
  const bool on_right = IsSidebarOnRight(browser_view_);
  gfx::Rect anchor = browser_view_->GetBoundsInScreen();
  anchor.set_x(on_right ? anchor.right() : anchor.x());
  anchor.set_width(0);
  if (views::View* region = browser_view_->tab_strip_view();
      region && region->GetVisible() && region->width() > 0) {
    gfx::Rect sidebar = region->GetBoundsInScreen();
    int top = sidebar.y();
    // Below the URL pill, where Zen's tab list starts.
    auto find_nav = [](auto& self, views::View* view) -> views::View* {
      if (view->GetClassName() == "KoanSidebarNavView") {
        return view;
      }
      for (views::View* child : view->children()) {
        if (views::View* found = self(self, child)) {
          return found;
        }
      }
      return nullptr;
    };
    if (views::View* nav = find_nav(find_nav, browser_view_.get());
        nav && nav->GetVisible()) {
      top = nav->GetBoundsInScreen().bottom();
    }
    // The bubble frame puts the panel this far below its anchor's top.
    constexpr int kBubbleOffset = 8;
    top -= kBubbleOffset;
    anchor = gfx::Rect(
        on_right ? sidebar.x() + kOverlap : sidebar.right() - kOverlap, top, 0,
        sidebar.bottom() - top);
  }
  picker_ = std::make_unique<KoanThemePicker>(
      this, space, GetTheme(space), anchor, on_right,
      base::BindOnce(&KoanThemeController::OnPickerClosed,
                     base::Unretained(this)));
  picker_->Show(browser_view_->GetWidget());
}

bool KoanThemeController::IsThemePickerShowing() const {
  return !!picker_;
}

void KoanThemeController::OnPickerClosed() {
  // Zen saves the theme when the panel closes.
  const theme::SpaceTheme edited = picker_->GetTheme();
  const Space space = editing_space_;
  const bool changed = edited != GetTheme(space);
  preview_.reset();
  preview_dragging_ = false;
  editing_space_.reset();
  // The picker's widget is still closing; delete it afterwards (or with
  // this controller, if the window goes first).
  closed_picker_ = std::move(picker_);
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanThemeController::DeleteClosedPicker,
                                weak_factory_.GetWeakPtr()));
  if (changed) {
    SaveTheme(space, edited);
  }
  Update(/*animate=*/false);
}

void KoanThemeController::DeleteClosedPicker() {
  closed_picker_.reset();
}

void KoanThemeController::ShowSpaceMenu(Space space,
                                        views::View* source,
                                        const gfx::Point& point,
                                        ui::mojom::MenuSourceType source_type) {
  if (!IsEnabled() || !source->GetWidget()) {
    return;
  }
  menu_space_ = space;
  if (!menu_model_) {
    // Zen's Space menu item, and its View > Appearance menu (Koan has no
    // menu bar of its own).
    appearance_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
    appearance_menu_->AddItem(kWebsitesWillUse, u"Websites will use:");
    appearance_menu_->AddRadioItem(kAppearanceAutomatic, u"Automatic",
                                   kAppearanceGroup);
    appearance_menu_->AddRadioItem(kAppearanceLight, u"Light",
                                   kAppearanceGroup);
    appearance_menu_->AddRadioItem(kAppearanceDark, u"Dark", kAppearanceGroup);

    menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    menu_model_->AddItemWithIcon(
        kEditTheme, u"Edit Theme…",
        ui::ImageModel::FromVectorIcon(kKoanThemeEditThemeIcon,
                                       ui::kColorMenuIcon, 16));
    menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    menu_model_->AddSubMenu(kAppearanceMenu, u"Appearance",
                            appearance_menu_.get());
  }
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  menu_runner_->RunMenuAt(source->GetWidget(), nullptr,
                          gfx::Rect(point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft, source_type);
}

void KoanThemeController::ExecuteCommand(int command_id, int event_flags) {
  switch (command_id) {
    case kEditTheme:
      // After the menu has closed, so the picker gets the focus.
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&KoanThemeController::ShowThemePicker,
                                    weak_factory_.GetWeakPtr(), menu_space_));
      break;
    case kAppearanceAutomatic:
      SetScheme(Scheme::kAutomatic);
      break;
    case kAppearanceLight:
      SetScheme(Scheme::kLight);
      break;
    case kAppearanceDark:
      SetScheme(Scheme::kDark);
      break;
  }
}

bool KoanThemeController::IsCommandIdChecked(int command_id) const {
  switch (command_id) {
    case kAppearanceAutomatic:
      return GetScheme() == Scheme::kAutomatic;
    case kAppearanceLight:
      return GetScheme() == Scheme::kLight;
    case kAppearanceDark:
      return GetScheme() == Scheme::kDark;
  }
  return false;
}

bool KoanThemeController::IsCommandIdEnabled(int command_id) const {
  // "Websites will use:" is a caption, like Zen's.
  return command_id != kWebsitesWillUse;
}

void KoanThemeController::OnTabGroupFocusChanged(
    std::optional<tab_groups::TabGroupId> new_focused_group_id,
    std::optional<tab_groups::TabGroupId> old_focused_group_id) {
  Update(/*animate=*/true);
}

void KoanThemeController::OnInitialized() {
  // Saved tab groups are known now, and with them the Spaces' themes.
  Update(/*animate=*/false);
}

void KoanThemeController::OnTabGroupLocalIdChanged(
    const base::Uuid& sync_id,
    const std::optional<tab_groups::LocalTabGroupID>& local_id) {
  if (local_id && local_id == active_space()) {
    Update(/*animate=*/false);
  }
}

void KoanThemeController::OnWillBeDestroyed() {
  sync_observation_.Reset();
}

void KoanThemeController::OnThemeChanged() {
  // The window scheme (Automatic / Light / Dark) may have changed.
  Update(/*animate=*/false);
}

void KoanThemeController::OnNativeThemeUpdated(
    ui::NativeTheme* observed_theme) {
  Update(/*animate=*/false);
}

void KoanThemeController::AnimationProgressed(const gfx::Animation* animation) {
  InvalidateBackground();
}

void KoanThemeController::AnimationEnded(const gfx::Animation* animation) {
  fading_.clear();
  InvalidateBackground();
}

void KoanThemeController::MigrateWorkspaceTint() {
  PrefService* prefs = profile()->GetPrefs();
  const SkColor applied =
      static_cast<SkColor>(prefs->GetInteger(prefs::kWorkspaceTintApplied));
  if (!applied) {
    return;
  }
  // Earlier Koan versions tinted the whole profile with the active
  // workspace's colour; put the user's colour back if it still shows.
  ThemeService* theme_service = ThemeServiceFactory::GetForProfile(profile());
  if (theme_service && theme_service->GetUserColor() == applied) {
    const SkColor saved = static_cast<SkColor>(
        prefs->GetInteger(prefs::kWorkspaceTintSavedColor));
    const auto variant = static_cast<ui::mojom::BrowserColorVariant>(
        prefs->GetInteger(prefs::kWorkspaceTintSavedVariant));
    if (saved) {
      theme_service->SetUserColorAndBrowserColorVariant(saved, variant);
    } else {
      theme_service->SetUserColor(std::nullopt);
      theme_service->SetBrowserColorVariant(variant);
    }
  }
  prefs->ClearPref(prefs::kWorkspaceTintApplied);
  prefs->ClearPref(prefs::kWorkspaceTintSavedColor);
  prefs->ClearPref(prefs::kWorkspaceTintSavedVariant);
}

bool PaintWindowBackground(gfx::Canvas* canvas,
                           const views::View* view,
                           const BrowserView* browser_view) {
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view);
  if (!controller || !controller->theme()->IsEnabled()) {
    return false;
  }
  controller->theme()->PaintBackground(canvas, view);
  return true;
}

bool PaintsWindowBackground(const BrowserView* browser_view) {
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view);
  return controller && controller->theme()->IsEnabled();
}

std::optional<bool> PrefersDarkWebColorScheme(
    content::WebContents* web_contents,
    bool is_web_ui) {
  if (is_web_ui) {
    return std::nullopt;
  }
  // The tab's window (also for a tab that isn't showing).
  const tabs::TabInterface* tab =
      tabs::TabInterface::MaybeGetFromContents(web_contents);
  const BrowserView* browser_view = tab ? BrowserView::GetBrowserViewForBrowser(
                                              tab->GetBrowserWindowInterface())
                                        : nullptr;
  const KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  // Only where a Space theme sets the window's light/dark mode.
  if (!controller || !controller->theme()->sets_color_mode()) {
    return std::nullopt;
  }
  const ThemeService* theme_service = ThemeServiceFactory::GetForProfile(
      Profile::FromBrowserContext(web_contents->GetBrowserContext()));
  if (!theme_service) {
    return std::nullopt;
  }
  return theme_service->BrowserUsesDarkColors();
}

}  // namespace koan
