// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_theme_controller.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
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
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "koan/browser/ui/views/koan_glass_state.h"
#include "koan/browser/ui/views/koan_library.h"
#include "koan/browser/ui/views/koan_space_background.h"
#include "koan/browser/ui/views/koan_system_accent.h"
#include "koan/browser/ui/views/koan_theme_picker.h"
#include "koan/browser/ui/views/koan_theme_platform.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "koan/browser/ui/views/koan_widget_color_key.h"
#include "koan/browser/ui/views/koan_window_background.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_provider_manager.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/cubic_bezier.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// zen.workspaces.switch-animation-duration.
constexpr base::TimeDelta kSwitchDuration = base::Milliseconds(250);
// Zen fades the grain to a new strength (zen-browser-ui.css:
// `.zen-browser-grain { transition: opacity 0.2s ease-in-out }`). Turning it
// on or off shows or hides it at once (`display: none`).
constexpr base::TimeDelta kGrainFadeDuration = base::Milliseconds(200);

// Zen animates the switch with Motion's spring (bounce 0) over
// kSwitchDuration: a critically damped spring that settles (to 0.1%) at
// the end.
double SpringProgress(double t) {
  constexpr double kOmega = 9.23;
  return t >= 1 ? 1 : 1 - (1 + kOmega * t) * std::exp(-kOmega * t);
}

// Fading-out backgrounds weaker than this are dropped.
constexpr double kMinFadingOpacity = 0.01;

}  // namespace

KoanThemeController::KoanThemeController(BrowserView* browser_view)
    : browser_view_(browser_view),
      switch_animation_(kSwitchDuration,
                        gfx::LinearAnimation::kDefaultFrameRate,
                        this),
      grain_animation_(kGrainFadeDuration,
                       gfx::LinearAnimation::kDefaultFrameRate,
                       this) {}

KoanThemeController::~KoanThemeController() = default;

Profile* KoanThemeController::profile() const {
  return browser_view_->GetProfile();
}

KoanWorkspaceController* KoanThemeController::workspaces() const {
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  KoanWorkspaceController* workspaces =
      controller ? controller->workspaces() : nullptr;
  return workspaces && workspaces->IsEnabled() ? workspaces : nullptr;
}

bool KoanThemeController::IsEnabled() const {
  // Zen's per-Space gradients are the Zen style's; the shadcn style is flat.
  // A Space theme is a Space's: only where the window has Spaces.
  // (A Chrome Web Store theme doesn't turn them off: Koan windows don't take
  // one, LocksExtensionTheme().)
  return !look::IsShadcnStyle() && workspaces() &&
         profile()->GetPrefs()->GetBoolean(prefs::kWorkspaceTint);
}

void KoanThemeController::OnAddedToWidget() {
  if (!features::IsWorkspacesEnabled() || !browser_view_->GetIsNormalType() ||
      profile()->IsOffTheRecord()) {
    return;
  }
  InstallChildColorKeyAdjuster();
  browser_view_->browser()->GetTabStripModel()->AddObserver(this);
  observing_tab_strip_ = true;
  look::AddObserver(this);
  observing_look_ = true;
  // The blur behind the window comes and goes (the setting, Reduce
  // Transparency, fullscreen): Zen's transparent or opaque maths.
  // An inactive translucent window is greyed out (PaintBackground()).
  paint_as_active_subscription_ =
      browser_view_->GetWidget()->RegisterPaintAsActiveChangedCallback(
          base::BindRepeating(&KoanThemeController::InvalidateBackground,
                              base::Unretained(this)));
  glass_subscription_ = AddWindowGlassObserver(
      browser_view_,
      base::BindRepeating(&KoanThemeController::Update, base::Unretained(this),
                          /*animate=*/false));
  pref_change_registrar_.Init(profile()->GetPrefs());
  for (const char* pref : {prefs::kDarkThemeStyle, prefs::kWorkspaceTint}) {
    pref_change_registrar_.Add(
        pref, base::BindRepeating(&KoanThemeController::Update,
                                  base::Unretained(this), /*animate=*/false));
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
  new_space_done_.Reset();
  switch_animation_.Stop();
  grain_animation_.Stop();
  if (observing_tab_strip_) {
    browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
    observing_tab_strip_ = false;
  }
  if (observing_look_) {
    look::RemoveObserver(this);
    observing_look_ = false;
  }
  glass_subscription_ = {};
  paint_as_active_subscription_ = {};
  pref_change_registrar_.RemoveAll();
  theme_observation_.Reset();
  native_theme_observation_.Reset();
}

KoanThemeController::Space KoanThemeController::active_space() const {
  return browser_view_->browser()->GetTabStripModel()->GetFocusedGroup();
}

base::DictValue KoanThemeController::GetStoredTheme(Space space) const {
  KoanWorkspaceController* spaces = workspaces();
  if (!space || !spaces) {
    return base::DictValue();
  }
  const std::string stored = spaces->GetSpaceTheme(*space).stored;
  std::optional<base::DictValue> dict =
      stored.empty() ? std::nullopt
                     : base::JSONReader::ReadDict(
                           stored, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  return dict ? std::move(*dict) : base::DictValue();
}

theme::SpaceTheme KoanThemeController::GetTheme(Space space) const {
  return theme::SpaceTheme::FromStored(GetStoredTheme(space));
}

void KoanThemeController::SaveTheme(Space space,
                                    const theme::SpaceTheme& theme) {
  KoanWorkspaceController* spaces = workspaces();
  if (!space || !spaces) {
    return;
  }
  // The Space keeps its theme (in the session, see
  // KoanWorkspaceController::ReadSpace()). The default theme is kept too
  // once its opacity or grain was changed (the glass slider works without
  // colours).
  const bool untouched = theme.is_default() && theme == theme::SpaceTheme();
  spaces->SetSpaceTheme(
      *space,
      {.stored = untouched ? std::string()
                           : base::WriteJson(theme.ToDict()).value_or("")});
  Update(/*animate=*/false);
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
  // The blurred desktop behind the window (Koan's glass): Zen's maths for a
  // transparent window.
  // Zen's transparent maths whenever the window is in glass mode; while it
  // is inactive PaintBackground() paints Zen's flat grey instead.
  env.transparent = IsWindowGlassEnabled(browser_view_);
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
      grain_animation_.Stop();
      fading_.clear();
      // (A change of colours re-themes the window.) Views that cache the
      // window's solid colour pick it up again.
      if (!SetWindowColors() && widget) {
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
          : theme::GetTokens(env, GetStoredTheme(space));
  if (preview_dragging_ && current_) {
    tokens.dark = current_->dark;
    tokens.colors = current_->colors;
  }
  if (picker_) {
    picker_->OnEnvironmentChanged();
  }
  if (current_ == tokens) {
    current_space_ = space;
    return;
  }
  const bool first = !current_;
  current_space_ = space;
  // No cross-fade with reduced motion.
  const bool rich = gfx::Animation::ShouldRenderRichAnimation();
  if (animate && current_ && current_->background != tokens.background &&
      rich) {
    grain_animation_.Stop();
    StartSwitchAnimation();
  } else if (current_ && !switch_animation_.is_animating() &&
             current_->background.grain != tokens.background.grain) {
    // A new grain strength on the same Space fades in, as in Zen.
    grain_from_ = DisplayedGrain();
    grain_animation_.Stop();
    if (rich && grain_from_ > 0 && tokens.background.grain > 0) {
      grain_animation_.Start();
    }
  }
  current_ = std::move(tokens);

  // The theme's mode and accent reach every colour of the window (Chrome's
  // and Koan's colour IDs) through its colour key.
  if (widget) {
    // (A change of colours re-themes the window.)
    if (!SetWindowColors() && first) {
      // Views that cache the window's solid colour (the gutter's layer)
      // switch to painting the background.
      widget->ThemeChanged();
    }
  }
  InvalidateBackground();
}

void KoanThemeController::ApplyWindowColors() {
  // Upstream's focus mode (koan::ApplySpaceColor()) may run before this
  // controller has seen the new Space: it then re-applies nothing old, and
  // Update() applies the new Space's colours.
  if (current_ && current_space_ != active_space()) {
    return;
  }
  SetWindowColors();
}

bool KoanThemeController::PaintsTranslucent() const {
  return current_ && SkColorGetA(current_->background.base) < SK_AlphaOPAQUE;
}

bool KoanThemeController::HasAppliedWindowColors() const {
  // Koan sets the window's colours in its key (AdjustWindowColorKey()), never
  // through the widget's overrides, and upstream's focus mode doesn't get to
  // (koan::ApplySpaceColor()).
  const views::Widget* widget = browser_view_->GetWidget();
  return !widget || (!widget->color_mode_override().has_value() &&
                     !widget->user_color_override().has_value());
}

void KoanThemeController::AdjustWindowColorKey(
    ui::ColorProviderKey* key) const {
  // The window's own scheme, which its panels keep (AdjustChildColorKey()).
  window_mode_ = key->color_mode;
  if (applied_mode_) {
    key->color_mode = *applied_mode_;
  }
  if (applied_color_) {
    key->user_color = *applied_color_;
    key->user_color_source = ui::ColorProviderKey::UserColorSource::kAccent;
  }
}

void KoanThemeController::AdjustChildColorKey(const views::Widget* child,
                                              ui::ColorProviderKey* key) const {
  // Zen sets zen-should-be-dark-mode on #browser only: panels, menus,
  // dialogs, the find bar and the open URL bar keep the window's scheme
  // (zen-theme.css), and the accent reaches them all. The window's own
  // chrome in other widgets keeps the Space's: macOS's fullscreen overlay
  // with the sidebar and toolbar, Glance and the Library (zen-glance.css,
  // zen-library.css: --toolbar-color-scheme), and the toast (inside
  // #browser; on the window's scheme only for the default theme, where no
  // mode is applied). A panel anchored in one of those (a bubble in the
  // fullscreen overlay, in Glance) is back on the window's scheme.
  if (!applied_mode_ || IsWindowChrome(child)) {
    return;
  }
  key->color_mode = window_mode_;
}

bool KoanThemeController::IsWindowChrome(const views::Widget* child) const {
#if BUILDFLAG(IS_MAC)
  if (child == browser_view_->overlay_widget() ||
      child == browser_view_->tab_overlay_widget()) {
    return true;
  }
#endif
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  return controller &&
         ((controller->glance() && child == controller->glance()->widget()) ||
          (controller->library() && child == controller->library()->widget()) ||
          (controller->toast() && child == controller->toast()->widget()));
}

bool KoanThemeController::SetWindowColors() {
  views::Widget* widget = browser_view_->GetWidget();
  // None for Zen's default theme or with Space themes off: the user's
  // colour and mode.
  std::optional<ui::ColorProviderKey::ColorMode> mode;
  std::optional<SkColor> color;
  if (current_) {
    if (current_->dark) {
      mode = *current_->dark ? ui::ColorProviderKey::ColorMode::kDark
                             : ui::ColorProviderKey::ColorMode::kLight;
    }
    // The theme's primary colour is the window's accent (Chrome's and
    // Koan's colour IDs derive from it).
    if (const auto primary = current_->colors.find(theme::Token::kPrimary);
        primary != current_->colors.end()) {
      color = primary->second;
    }
  }
  if (mode == applied_mode_ && color == applied_color_) {
    return false;
  }
  applied_mode_ = mode;
  applied_color_ = color;
  if (!widget || widget->IsClosed()) {
    return false;
  }
  // One re-theme for both, under the new key (two widget overrides would
  // re-theme the window twice, once under an in-between key).
  widget->ThemeChanged();
  // Its child widgets take the colours through their parent's key but aren't
  // told (upstream re-themes them only with kThemeChangeOptimization): on
  // macOS the fullscreen overlay with the sidebar and toolbar, Glance, the
  // floating URL bar, the bubbles. (Weak pointers: a child's ThemeChanged()
  // may close another. Off macOS, desktop Aura's bubbles aren't among the
  // child widgets and keep the old accent until reopened; not a product
  // platform.)
  std::vector<base::WeakPtr<views::Widget>> children;
  for (views::Widget* child :
       views::Widget::GetAllChildWidgets(widget->GetNativeView())) {
    if (child != widget) {
      children.push_back(child->GetWeakPtr());
    }
  }
  for (const base::WeakPtr<views::Widget>& child : children) {
    if (child && !child->IsClosed()) {
      child->ThemeChanged();
    }
  }
  return true;
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

double KoanThemeController::DisplayedGrain() const {
  if (!current_) {
    return 0;
  }
  if (!grain_animation_.is_animating()) {
    return current_->background.grain;
  }
  // CSS's ease-in-out.
  static const gfx::CubicBezier ease_in_out(0.42, 0, 0.58, 1);
  const double t = ease_in_out.Solve(grain_animation_.GetCurrentValue());
  return grain_from_ + (current_->background.grain - grain_from_) * t;
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
  // On a translucent window their layers must not claim to be opaque, or
  // the blur behind them is never composited; Koan never makes one opaque
  // (koan::MakeTranslucentLayer()). The vertical tab strip region and the
  // top container (CustomCornersBackground) are translucent from creation;
  // the gutter's re-created layer is made translucent where it is created
  // (koan::OnWindowBackgroundLayerCreated()). Every change (theme, opacity,
  // activation) repaints them all.
  if (PaintsTranslucent()) {
    for (const std::unique_ptr<views::ViewTracker>& painter : painters_) {
      MakeTranslucentLayer(painter->view());
    }
  }
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
  // A translucent paint on an opaque layer: some creation site missed
  // MakeTranslucentLayer() (a detector only; the layer is fixed where it is
  // created).
  DCHECK(!view->layer() || !view->layer()->fills_bounds_opaquely() ||
         SkColorGetA(current_->background.base) == SK_AlphaOPAQUE)
      << view->GetClassName()
      << " paints a translucent theme on an opaque layer";
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
  // Zen greys an inactive translucent window out
  // (zen.view.grey-out-inactive-windows): flat and opaque, no blur and no
  // theme. The blur goes flat by itself.
  if (SkColorGetA(now.base) < SK_AlphaOPAQUE &&
      !widget->ShouldPaintAsActive()) {
    // InactiveCaption: macOS's window background colour in the window's
    // appearance (nsLookAndFeel.mm), Zen 1.24b's measured grey elsewhere
    // (only the test glass gets here off macOS).
    const ui::ColorProvider* colors = view->GetColorProvider();
    const bool dark =
        color_utils::IsDark(colors->GetColor(kColorKoanThemeWindow));
    theme::PaintBase(canvas, box,
                     GetInactiveCaptionColor(dark).value_or(
                         colors->GetColor(kColorKoanThemeInactiveWindow)));
    return;
  }
  const double progress = SwitchProgress();
  // Zen fades the old gradient out while the new one fades in, over the
  // window's own background (none on a translucent window: the blur).
  if (SkColorGetA(now.base) > 0) {
    theme::PaintBase(canvas, box, now.base);
  }
  double grain = now.grain * progress;
  if (progress < 1) {
    for (const Fading& fading : fading_) {
      const double opacity = fading.opacity * (1 - progress);
      theme::PaintLayers(canvas, box, fading.background.layers, opacity);
      grain += fading.background.grain * opacity;
    }
  } else {
    grain = DisplayedGrain();
  }
  theme::PaintLayers(canvas, box, now.layers, progress);
  // Above Zen's opacity range on a translucent window, the opaque paint
  // fades in over the blur.
  if (now.cover > 0) {
    theme::PaintBase(
        canvas, box,
        SkColorSetA(now.cover_base,
                    static_cast<SkAlpha>(now.cover * progress *
                                         SkColorGetA(now.cover_base))));
    theme::PaintLayers(canvas, box, now.cover_layers, now.cover * progress);
  }
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
  if (!space || !IsEnabled() || !browser_view_->GetWidget()) {
    return;
  }
  OpenPicker(space, GetTheme(space));
}

void KoanThemeController::ShowThemePickerForNewSpace(
    const std::string& stored,
    base::OnceCallback<void(std::string stored)> on_done) {
  if (!IsEnabled() || !browser_view_->GetWidget()) {
    return;
  }
  // Previewed on the active Space, which the new one will replace on
  // screen.
  OpenPicker(active_space(), theme::SpaceTheme::FromStoredJson(stored));
  new_space_done_ = std::move(on_done);
}

void KoanThemeController::ShowThemePickerForWelcome(
    const gfx::Rect& anchor_in_screen) {
  if (picker_ && picker_->welcome_mode()) {
    // (Back on the colours step while it fades out: it stays.)
    picker_->CancelClose();
    picker_->SetAnchorRect(anchor_in_screen);
    return;
  }
  const Space space = active_space();
  if (!space || !IsEnabled() || !browser_view_->GetWidget()) {
    return;
  }
  OpenPicker(space, GetTheme(space), anchor_in_screen);
}

void KoanThemeController::CloseThemePickerForWelcome() {
  if (picker_ && picker_->welcome_mode()) {
    picker_->Close();
  }
}

gfx::Size KoanThemeController::ThemePickerSize() const {
  // Empty when ShowThemePickerForWelcome() would show nothing (Space themes
  // off, the shadcn look, an extension theme).
  if (!active_space() || !IsEnabled() || !browser_view_->GetWidget()) {
    return gfx::Size();
  }
  if (picker_) {
    return picker_->GetSize();
  }
  // Without one open: the size a picker for the active Space would have.
  KoanThemePicker sizer(const_cast<KoanThemeController*>(this), active_space(),
                        GetTheme(active_space()), base::DoNothing());
  return sizer.GetSize();
}

void KoanThemeController::OpenPicker(Space space,
                                     const theme::SpaceTheme& theme,
                                     std::optional<gfx::Rect> welcome_anchor) {
  if (picker_) {
    // An open picker saves its edits and closes first.
    OnPickerClosed();
    closed_picker_.reset();
  }
  editing_space_ = space;
  preview_.reset();
  preview_dragging_ = false;

  picker_ = std::make_unique<KoanThemePicker>(
      this, space, theme,
      base::BindOnce(&KoanThemeController::OnPickerClosed,
                     base::Unretained(this)));
  if (welcome_anchor) {
    picker_->SetWelcomeMode();
    picker_->SetAnchorRect(*welcome_anchor);
  } else {
    // Zen anchors the panel to #TabsToolbar: the sidebar below the URL
    // pill, inside the toolbox's padding (vertical-tabs.css
    // --zen-min-toolbox-padding). Without a sidebar (compact mode), the
    // window's edge.
#if BUILDFLAG(IS_MAC)
    constexpr int kToolboxPadding = 6;
#else
    constexpr int kToolboxPadding = 5;
#endif
    const bool on_right = IsSidebarOnRight(browser_view_);
    gfx::Rect tabs = browser_view_->GetBoundsInScreen();
    tabs.set_x(on_right ? tabs.right() : tabs.x());
    tabs.set_width(0);
    if (views::View* region = browser_view_->tab_strip_view();
        region && region->GetVisible() && region->width() > 0) {
      tabs = region->GetBoundsInScreen();
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
      int top = tabs.y();
      if (views::View* nav = find_nav(find_nav, browser_view_.get());
          nav && nav->GetVisible()) {
        top = nav->GetBoundsInScreen().bottom();
      }
      tabs.Inset(gfx::Insets::TLBR(top - tabs.y(), kToolboxPadding,
                                   kToolboxPadding, kToolboxPadding));
    }
    picker_->PlaceBeside(tabs, on_right);
  }
  picker_->Show(browser_view_->GetWidget());
}

bool KoanThemeController::IsThemePickerShowing() const {
  return !!picker_;
}

void KoanThemeController::OnPickerClosed() {
  // Zen saves the theme when the panel closes.
  const theme::SpaceTheme edited = picker_->GetTheme();
  const Space space = editing_space_;
  // A theme for a Space still to be created goes back to the form.
  base::OnceCallback<void(std::string)> new_space_done =
      std::move(new_space_done_);
  const bool changed = !new_space_done && edited != GetTheme(space);
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
  if (new_space_done) {
    std::move(new_space_done)
        .Run(edited.is_default()
                 ? std::string()
                 : base::WriteJson(edited.ToDict()).value_or(""));
  }
}

void KoanThemeController::DeleteClosedPicker() {
  closed_picker_.reset();
}

void KoanThemeController::OnTabGroupFocusChanged(
    std::optional<tab_groups::TabGroupId> new_focused_group_id,
    std::optional<tab_groups::TabGroupId> old_focused_group_id) {
  // A switch can leave no group focused for a moment; the window keeps the
  // old Space's theme until the new one shows.
  if (new_focused_group_id) {
    Update(/*animate=*/true);
  }
}

void KoanThemeController::TabGroupedStateChanged(
    TabStripModel* tab_strip_model,
    std::optional<tab_groups::TabGroupId> old_group,
    std::optional<tab_groups::TabGroupId> new_group,
    tabs::TabInterface* tab,
    int index) {
  // A restored Space's theme comes back with its tabs.
  if (new_group && new_group == active_space()) {
    Update(/*animate=*/false);
  }
}

void KoanThemeController::OnThemeChanged() {
  // The window scheme (Automatic / Light / Dark) may have changed.
  Update(/*animate=*/false);
}

void KoanThemeController::OnNativeThemeUpdated(
    ui::NativeTheme* observed_theme) {
  Update(/*animate=*/false);
}

void KoanThemeController::OnLookChanged() {
  // The style may have changed: Space themes come or go (Update() clears
  // the window's overrides when they go).
  if (!IsEnabled() && picker_) {
    OnPickerClosed();
    closed_picker_.reset();
  }
  Update(/*animate=*/false);
}

void KoanThemeController::AnimationProgressed(const gfx::Animation* animation) {
  InvalidateBackground();
}

void KoanThemeController::AnimationEnded(const gfx::Animation* animation) {
  if (animation == &switch_animation_) {
    fading_.clear();
  }
  InvalidateBackground();
}

void AdjustWindowColorKey(const BrowserView* browser_view,
                          ui::ColorProviderKey* key) {
  const KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  if (controller) {
    controller->theme()->AdjustWindowColorKey(key);
  }
}

bool WindowSchemeIsDark(const BrowserView* browser_view, bool fallback) {
  const KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  return controller ? controller->theme()->window_scheme_is_dark() : fallback;
}

namespace {

void AdjustChildColorKey(const views::Widget* parent,
                         const views::Widget* child,
                         ui::ColorProviderKey* key) {
  // The browser window `child` is in, at any depth (a bubble anchored in the
  // fullscreen overlay, in Glance).
  for (const views::Widget* widget = parent; widget;
       widget = widget->parent()) {
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForNativeWindow(widget->GetNativeWindow());
    if (browser_view && browser_view->GetWidget() == widget) {
      if (const KoanBrowserViewController* controller =
              KoanBrowserViewController::From(browser_view)) {
        controller->theme()->AdjustChildColorKey(child, key);
      }
      return;
    }
  }
}

}  // namespace

void InstallChildColorKeyAdjuster() {
  static bool installed = false;
  if (!installed) {
    installed = true;
    views::SetChildColorProviderKeyAdjuster(&AdjustChildColorKey);
  }
}

bool LocksExtensionTheme(const BrowserWindowInterface* browser) {
  // Zen disables lightweight themes (zen.theme.disable-lightweight;
  // LightweightThemeConsumer-sys-mjs.patch). Web apps keep theirs.
  return browser &&
         (browser->GetType() == BrowserWindowInterface::TYPE_NORMAL ||
          browser->GetType() == BrowserWindowInterface::TYPE_POPUP);
}

void OnWindowBackgroundLayerCreated(views::View* view,
                                    const BrowserView* browser_view) {
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view);
  if (controller && controller->theme()->PaintsTranslucent()) {
    MakeTranslucentLayer(view);
  }
}

bool PaintWindowBackground(gfx::Canvas* canvas,
                           const views::View* view,
                           const BrowserView* browser_view) {
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view);
  if (!controller || !controller->theme()->paints_background()) {
    return false;
  }
  controller->theme()->PaintBackground(canvas, view);
  return true;
}

bool PaintsWindowBackground(const BrowserView* browser_view) {
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view);
  return controller && controller->theme()->paints_background();
}

std::optional<bool> PrefersDarkWebColorScheme(
    content::WebContents* web_contents,
    bool is_web_ui) {
  if (is_web_ui) {
    return std::nullopt;
  }
  const ThemeService* theme_service = ThemeServiceFactory::GetForProfile(
      Profile::FromBrowserContext(web_contents->GetBrowserContext()));
  if (!theme_service) {
    return std::nullopt;
  }
  // The tab's window (also for a tab that isn't showing).
  const tabs::TabInterface* tab =
      tabs::TabInterface::MaybeGetFromContents(web_contents);
  if (!tab) {
    return std::nullopt;
  }
  const BrowserWindowInterface* window = tab->GetBrowserWindowInterface();
  if (!window) {
    // A tab between windows (dragged out, moved): no Space theme sets its
    // mode, so Chrome decides.
    return std::nullopt;
  }
  const BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(window);
  const KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  // Only where a Space theme sets the window's light/dark mode.
  if (!controller || !controller->theme()->sets_color_mode()) {
    return std::nullopt;
  }
  return theme_service->BrowserUsesDarkColors();
}

}  // namespace koan
