// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_theme_picker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_space_background.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/cursor/mojom/cursor_type.mojom-shared.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rrect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/style/platform_style.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

using theme::PickerModel;

// Sizes from zen-gradient-generator.css, as Zen lays the panel out.
constexpr int kPanelPadding = 10;
constexpr int kPanelRadius = 8;
// The panel is 380px wide: 10px padding and a 1px border on each side.
constexpr int kContentWidth = 358;
constexpr int kFieldSize = 358;
constexpr int kFieldRadius = kPanelRadius - 4;
// The gap between the panel's rows.
constexpr int kRowGap = 10;
// Scheme and colour action buttons.
constexpr int kButtonSize = 30;
constexpr int kButtonPitch = 51;
constexpr int kButtonIconSize = 18;
constexpr float kButtonRadius = 5.2f;
constexpr int kSchemeTop = 15;
constexpr int kActionsBottom = 12;
// Dots: 38px + 6px border for the big one, 20px + 3px for the others.
constexpr float kPrimaryDotRadius = 25;
constexpr float kPrimaryDotBorder = 6;
constexpr float kDotRadius = 13;
constexpr float kDotBorder = 3;
// The preset pages.
constexpr int kArrowSize = 28;
constexpr int kArrowIconSize = 16;
constexpr int kPagesMargin = 10;
constexpr int kPageWidth = kContentWidth - 2 * (kArrowSize + kPagesMargin);
constexpr int kSwatchSize = 26;
// The opacity slider and the grain dial: the dial 5rem, 1.5rem apart, or
// 6rem, 2rem apart on macOS, in Zen's chrome rem (the system font size:
// 13px on macOS, 11pt on Linux).
constexpr int kControlsPadding = 10;
#if BUILDFLAG(IS_MAC)
constexpr int kDialSize = 78;
constexpr int kControlsGap = 26;
#else
constexpr int kDialSize = 73;
constexpr int kControlsGap = 22;
#endif
constexpr int kDialMargin = 8;
constexpr int kControlsHeight = kDialSize;
constexpr int kTrackHeight = 16;
// Zen's grain dial has 16 steps.
constexpr int kGrainSteps = 16;
// Custom colours.
constexpr int kCustomRowHeight = 30;
constexpr int kCustomSwatchSize = 20;

// Panel animations.
constexpr base::TimeDelta kDotAnimation = base::Milliseconds(400);
constexpr base::TimeDelta kPageAnimation = base::Milliseconds(250);

// Keyboard steps: the big dot moves this far per arrow key (more with
// Shift); the opacity by 0.05 (the range input's step, 0.001, is too fine
// for keys).
constexpr float kDotKeyStep = 4;
constexpr float kDotKeyStepLarge = 20;
constexpr double kOpacityKeyStep = 0.05;

// Makes `view` keyboard-focusable like the panel's buttons (on macOS with
// Full Keyboard Access), with a focus ring.
void MakeFocusable(views::View* view) {
  view->SetFocusBehavior(views::PlatformStyle::kDefaultFocusBehavior);
  views::FocusRing::Install(view);
}

// "40%".
std::u16string Percent(double value) {
  return base::UTF8ToUTF16(
      base::StringPrintf("%d%%", static_cast<int>(std::round(value * 100))));
}

// Motion's spring with `bounce` over a duration, as Zen animates the dots
// (duration 0.4s, bounce 0.3): damping ratio 1 - bounce, settled (0.1%) at
// the end.
double Spring(double t, double bounce) {
  if (t >= 1) {
    return 1;
  }
  const double zeta = 1 - bounce;
  const double omega = 6.9 / zeta;
  if (zeta >= 1) {
    return 1 - (1 + omega * t) * std::exp(-omega * t);
  }
  const double damped = omega * std::sqrt(1 - zeta * zeta);
  return 1 - std::exp(-zeta * omega * t) *
                 (std::cos(damped * t) +
                  zeta * omega / damped * std::sin(damped * t));
}

// The panel's colours (Koan colour IDs, Zen's zen-gradient-generator.css
// values), resolved from the panel's colour provider, which follows the
// window's scheme, not the Space theme.
struct Palette {
  SkColor panel = SK_ColorTRANSPARENT;
  SkColor text = SK_ColorTRANSPARENT;
  SkColor field = SK_ColorTRANSPARENT;
  SkColor field_grid = SK_ColorTRANSPARENT;
  SkColor icon = SK_ColorTRANSPARENT;
  SkColor hover = SK_ColorTRANSPARENT;
  SkColor dot_border = SK_ColorTRANSPARENT;
  SkColor wave = SK_ColorTRANSPARENT;
  SkColor wave_dim = SK_ColorTRANSPARENT;
  SkColor track = SK_ColorTRANSPARENT;
  SkColor thumb = SK_ColorTRANSPARENT;
  SkColor dial_dot = SK_ColorTRANSPARENT;
  SkColor dial_handle = SK_ColorTRANSPARENT;
  SkColor dial_border = SK_ColorTRANSPARENT;
  SkColor dial_shine = SK_ColorTRANSPARENT;
  SkColor swatch_shadow = SK_ColorTRANSPARENT;
};

Palette GetPalette(const ui::ColorProvider& provider) {
  return {
      .panel = provider.GetColor(kColorKoanThemePickerBackground),
      .text = provider.GetColor(kColorKoanThemePickerForeground),
      .field = provider.GetColor(kColorKoanThemePickerField),
      .field_grid = provider.GetColor(kColorKoanThemePickerFieldGrid),
      .icon = provider.GetColor(kColorKoanThemePickerIcon),
      .hover = provider.GetColor(kColorKoanThemePickerButtonHovered),
      .dot_border = provider.GetColor(kColorKoanThemePickerDotBorder),
      .wave = provider.GetColor(kColorKoanThemePickerWave),
      .wave_dim = provider.GetColor(kColorKoanThemePickerWaveDim),
      .track = provider.GetColor(kColorKoanThemePickerTrack),
      .thumb = provider.GetColor(kColorKoanThemePickerThumb),
      .dial_dot = provider.GetColor(kColorKoanThemePickerDialDot),
      .dial_handle = provider.GetColor(kColorKoanThemePickerDialHandle),
      .dial_border = provider.GetColor(kColorKoanThemePickerDialBorder),
      .dial_shine = provider.GetColor(kColorKoanThemePickerDialShine),
      .swatch_shadow = provider.GetColor(kColorKoanThemePickerSwatchShadow),
  };
}

gfx::FontList SemiboldFont(int size) {
  const gfx::FontList base;
  return base.Derive(size - base.GetFontSize(), gfx::Font::NORMAL,
                     gfx::Font::Weight::SEMIBOLD);
}

// Zen's names for the harmonies (zen-workspaces.ftl).
std::u16string HarmonyName(const std::string& type) {
  if (type == "complementary") {
    return u"Complementary";
  }
  if (type == "splitComplementary") {
    return u"Split";
  }
  if (type == "analogous" || type == "singleAnalogous") {
    return u"Analogous";
  }
  if (type == "triadic") {
    return u"Triadic";
  }
  return u"Floating";
}

}  // namespace

class KoanThemePickerView;

// A square icon button of the panel (scheme, colour actions, page arrows):
// a rounded highlight when hovered or selected, half-transparent when
// disabled.
class KoanThemePickerButton : public views::Button {
  METADATA_HEADER(KoanThemePickerButton, views::Button)

 public:
  KoanThemePickerButton(PressedCallback callback,
                        const gfx::VectorIcon& icon,
                        int size,
                        int icon_size,
                        float disabled_opacity,
                        std::u16string label)
      : views::Button(std::move(callback)),
        icon_(icon),
        size_(size),
        icon_size_(icon_size),
        disabled_opacity_(disabled_opacity),
        label_(std::move(label)) {
    SetTooltipText(label_);
    GetViewAccessibility().SetName(label_);
  }

  void SetPalette(const Palette& palette, SkColor icon_color) {
    palette_ = palette;
    icon_color_ = icon_color;
    SchedulePaint();
  }

  void SetSelected(bool selected) {
    if (selected_ != selected) {
      selected_ = selected;
      SchedulePaint();
    }
  }

  void SetLabel(std::u16string label) {
    label_ = std::move(label);
    SetTooltipText(label_);
    GetViewAccessibility().SetName(label_);
  }
  const std::u16string& GetLabel() const { return label_; }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(size_, size_);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const bool enabled = GetEnabled();
    if (enabled && (selected_ || GetState() == STATE_HOVERED ||
                    GetState() == STATE_PRESSED)) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(palette_.hover);
      canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kButtonRadius, flags);
    }
    const SkColor color =
        enabled ? icon_color_
                : SkColorSetA(icon_color_,
                              static_cast<U8CPU>(SkColorGetA(icon_color_) *
                                                 disabled_opacity_));
    const gfx::ImageSkia image =
        gfx::CreateVectorIcon(*icon_, icon_size_, color);
    canvas->DrawImageInt(image, (width() - icon_size_) / 2,
                         (height() - icon_size_) / 2);
  }

 private:
  const raw_ref<const gfx::VectorIcon> icon_;
  const int size_;
  const int icon_size_;
  const float disabled_opacity_;
  std::u16string label_;
  Palette palette_;
  SkColor icon_color_ = SK_ColorWHITE;
  bool selected_ = false;
};

BEGIN_METADATA(KoanThemePickerButton)
ADD_READONLY_PROPERTY_METADATA(std::u16string, Label)
END_METADATA

// The panel's contents. Owns the dots' model and lays out Zen's rows.
class KoanThemePickerView : public views::View {
  METADATA_HEADER(KoanThemePickerView, views::View)

 public:
  KoanThemePickerView(KoanThemeController* controller,
                      KoanThemeController::Space space,
                      const theme::SpaceTheme& theme);
  KoanThemePickerView(const KoanThemePickerView&) = delete;
  KoanThemePickerView& operator=(const KoanThemePickerView&) = delete;
  ~KoanThemePickerView() override;

  PickerModel& model() { return model_; }
  const Palette& palette() const { return palette_; }

  // The window scheme (the panel's scheme buttons).
  KoanThemeController::Scheme scheme() const;
  void SetScheme(KoanThemeController::Scheme scheme);

  // The dots, opacity, grain or custom colours changed: update the panel
  // and preview the theme on the window (Zen's updateCurrentWorkspace).
  // `animate` is false during a drag, which ends with OnModelChanged(true).
  void OnModelChanged(bool animate);
  void UpdateControls();

  // The colour actions are disabled without colours (Zen's
  // onWorkspaceChange).
  size_t color_count() const {
    return model_.dots().size() + model_.custom_colors().size();
  }

  // For tests: the dots as "r,g,b@x,y" in field order, the harmony, the
  // opacity and grain, the preset page and the window's background and
  // mode.
  std::string GetDots() const;
  std::string GetAlgorithm() const { return model_.algorithm(); }
  double GetOpacity() const { return model_.opacity(); }
  double GetTexture() const { return model_.texture(); }
  int GetPage() const;
  std::string GetWindowBackground() const {
    return controller_->DescribeCurrentBackground();
  }
  std::string GetWindowMode() const {
    return controller_->DescribeCurrentMode();
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnThemeChanged() override;

 private:
  const raw_ptr<KoanThemeController> controller_;
  const KoanThemeController::Space space_;
  PickerModel model_{kFieldSize};
  Palette palette_;

  raw_ptr<views::View> field_ = nullptr;
  raw_ptr<KoanThemePickerButton> page_left_ = nullptr;
  raw_ptr<views::View> pages_ = nullptr;
  raw_ptr<KoanThemePickerButton> page_right_ = nullptr;
  raw_ptr<views::View> slider_ = nullptr;
  raw_ptr<views::View> dial_ = nullptr;
  raw_ptr<views::View> custom_ = nullptr;
};

// The colour field (Zen's .zen-theme-picker-gradient): a dotted square
// with the scheme buttons on top, the colour actions at the bottom, and
// the colour dots.
class KoanThemeGradientField : public views::View,
                               public gfx::AnimationDelegate {
  METADATA_HEADER(KoanThemeGradientField, views::View)

 public:
  explicit KoanThemeGradientField(KoanThemePickerView* picker)
      : picker_(picker) {
    auto add_scheme = [&](const gfx::VectorIcon& icon, const char16_t* label,
                          KoanThemeController::Scheme scheme) {
      scheme_buttons_.push_back(
          AddChildView(std::make_unique<KoanThemePickerButton>(
              base::BindRepeating(&KoanThemeGradientField::OnSchemePressed,
                                  base::Unretained(this), scheme),
              icon, kButtonSize, kButtonIconSize, 0.5f, label)));
    };
    add_scheme(kKoanThemeSparklesIcon, u"Automatic",
               KoanThemeController::Scheme::kAutomatic);
    add_scheme(kKoanThemeFaceSunIcon, u"Light",
               KoanThemeController::Scheme::kLight);
    add_scheme(kKoanThemeMoonStarsIcon, u"Dark",
               KoanThemeController::Scheme::kDark);
    add_ = AddChildView(std::make_unique<KoanThemePickerButton>(
        base::BindRepeating(&KoanThemeGradientField::OnAction,
                            base::Unretained(this), Action::kAdd),
        kKoanThemePlusIcon, kButtonSize, kButtonIconSize, 0.5f, u"Add color"));
    remove_ = AddChildView(std::make_unique<KoanThemePickerButton>(
        base::BindRepeating(&KoanThemeGradientField::OnAction,
                            base::Unretained(this), Action::kRemove),
        kKoanThemeUnpinIcon, kButtonSize, kButtonIconSize, 0.5f,
        u"Remove color"));
    algorithm_ = AddChildView(std::make_unique<KoanThemePickerButton>(
        base::BindRepeating(&KoanThemeGradientField::OnAction,
                            base::Unretained(this), Action::kAlgorithm),
        kKoanThemeAlgorithmIcon, kButtonSize, kButtonIconSize, 0.5f,
        u"Floating"));
    click_to_add_ =
        AddChildView(std::make_unique<views::Label>(u"Click to add a color"));
    click_to_add_->SetFontList(SemiboldFont(13));
    click_to_add_->SetAutoColorReadabilityEnabled(false);
    click_to_add_->SetCanProcessEventsWithinSubtree(false);

    // From the keyboard: Enter or Space adds the first colour, the arrows
    // move the big dot (further with Shift), Delete removes it.
    MakeFocusable(this);
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  kFieldRadius);
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(u"Theme colors");
  }

  KoanThemeGradientField(const KoanThemeGradientField&) = delete;
  KoanThemeGradientField& operator=(const KoanThemeGradientField&) = delete;
  ~KoanThemeGradientField() override = default;

  // Moves the drawn dots to the model's, springing like Zen's unless
  // `animate` is false (dragging).
  void SyncDots(bool animate) {
    animate &= gfx::Animation::ShouldRenderRichAnimation();
    std::map<int, DrawnDot> drawn;
    bool moving = false;
    for (const PickerModel::Dot& dot : picker_->model().dots()) {
      const auto it = drawn_.find(dot.serial);
      DrawnDot entry;
      if (it == drawn_.end()) {
        entry.from = entry.current = dot.spawn_position;
      } else {
        entry = it->second;
        entry.from = entry.current;
      }
      entry.to = dot.position;
      if (!animate) {
        entry.from = entry.current = entry.to;
      }
      moving |= entry.current != entry.to;
      drawn[dot.serial] = entry;
    }
    drawn_ = std::move(drawn);
    if (moving) {
      animation_.Start();
    } else {
      animation_.Stop();
    }
    SchedulePaint();
  }

  void SetPalette(const Palette& palette) {
    for (KoanThemePickerButton* button : scheme_buttons_) {
      button->SetPalette(palette, palette.icon);
    }
    for (KoanThemePickerButton* button : {add_, remove_, algorithm_}) {
      button->SetPalette(palette, palette.icon);
    }
    click_to_add_->SetEnabledColor(palette.text);
    SchedulePaint();
  }

  void UpdateControls() {
    const KoanThemeController::Scheme scheme = picker_->scheme();
    for (size_t i = 0; i < scheme_buttons_.size(); ++i) {
      scheme_buttons_[i]->SetSelected(static_cast<size_t>(scheme) == i);
    }
    const size_t colors = picker_->color_count();
    add_->SetEnabled(colors > 0 && colors < PickerModel::kMaxDots);
    remove_->SetEnabled(colors > 0);
    algorithm_->SetEnabled(colors >= 2);
    algorithm_->SetLabel(HarmonyName(picker_->model().algorithm()));
    click_to_add_->SetVisible(colors == 0);
    // What a screen reader says about the field.
    const PickerModel::Dot* big = picker_->model().DraggableDot();
    GetViewAccessibility().SetDescription(
        big ? base::UTF8ToUTF16(base::StrCat(
                  {theme::HexColor(theme::ToSkColor(big->color)), ", ",
                   base::NumberToString(picker_->model().dots().size()),
                   picker_->model().dots().size() == 1 ? " color" : " colors",
                   ", ",
                   base::UTF16ToUTF8(
                       HarmonyName(picker_->model().algorithm()))}))
            : std::u16string(u"No colors. Press Enter to add one."));
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kFieldSize, kFieldSize);
  }

  void Layout(PassKey) override {
    const int center = width() / 2;
    for (size_t i = 0; i < 3; ++i) {
      const int x =
          center + (static_cast<int>(i) - 1) * kButtonPitch - kButtonSize / 2;
      scheme_buttons_[i]->SetBounds(x, kSchemeTop, kButtonSize, kButtonSize);
    }
    const int actions_y = height() - kActionsBottom - kButtonSize;
    const std::array<KoanThemePickerButton*, 3> actions = {
        add_.get(), remove_.get(), algorithm_.get()};
    for (size_t i = 0; i < 3; ++i) {
      const int x =
          center + (static_cast<int>(i) - 1) * kButtonPitch - kButtonSize / 2;
      actions[i]->SetBounds(x, actions_y, kButtonSize, kButtonSize);
    }
    const gfx::Size label = click_to_add_->GetPreferredSize();
    click_to_add_->SetBounds((width() - label.width()) / 2,
                             (height() - label.height()) / 2, label.width(),
                             label.height());
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const Palette& palette = picker_->palette();
    const gfx::RectF bounds(GetLocalBounds());
    gfx::ScopedCanvas scoped(canvas);
    canvas->ClipPath(
        SkPath::RRect(SkRRect::MakeRectXY(gfx::RectFToSkRect(bounds),
                                          kFieldRadius, kFieldRadius)),
        true);
    canvas->DrawColor(palette.field);
    // Zen's dotted background: a 1px dot in each 6px tile, offset by -23px.
    cc::PaintFlags grid;
    grid.setAntiAlias(true);
    grid.setColor(palette.field_grid);
    for (int y = 4; y < height(); y += 6) {
      for (int x = 4; x < width(); x += 6) {
        canvas->DrawCircle(gfx::PointF(x, y), 1, grid);
      }
    }

    // The other dots, then the big one on top.
    const PickerModel::Dot* big = picker_->model().DraggableDot();
    for (const PickerModel::Dot& dot : picker_->model().dots()) {
      if (&dot != big) {
        PaintDot(canvas, dot, /*big=*/false);
      }
    }
    if (big) {
      PaintDot(canvas, *big, /*big=*/true);
    }
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    const gfx::PointF point(event.location());
    if (event.IsOnlyRightMouseButton()) {
      right_pressed_on_dot_ = picker_->model().HitsPrimaryDot(point);
      return right_pressed_on_dot_;
    }
    if (!event.IsOnlyLeftMouseButton()) {
      return false;
    }
    dragging_ = picker_->model().HitsPrimaryDot(point);
    SchedulePaint();
    return true;
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    if (dragging_) {
      picker_->model().DragTo(gfx::PointF(event.location()));
      picker_->OnModelChanged(/*animate=*/false);
    }
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    const gfx::PointF point(event.location());
    if (event.IsOnlyRightMouseButton()) {
      // Zen removes the dot on a right click.
      if (right_pressed_on_dot_ && picker_->model().HitsPrimaryDot(point)) {
        picker_->model().RemovePrimaryDot();
        picker_->OnModelChanged(/*animate=*/true);
      }
      right_pressed_on_dot_ = false;
      return;
    }
    if (dragging_) {
      // A press on the dot is a drag, never a click (Zen's recentlyDragged).
      dragging_ = false;
      picker_->OnModelChanged(/*animate=*/true);
      SchedulePaint();
      return;
    }
    if (HitTestPoint(event.location()) && picker_->model().Click(point)) {
      picker_->OnModelChanged(/*animate=*/true);
    }
  }

  void OnMouseCaptureLost() override {
    right_pressed_on_dot_ = false;
    if (dragging_) {
      // The drag ends here: the window's colours follow now.
      dragging_ = false;
      picker_->OnModelChanged(/*animate=*/true);
    }
    SchedulePaint();
  }

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    PickerModel& model = picker_->model();
    const PickerModel::Dot* big = model.DraggableDot();
    const float step = event.IsShiftDown() ? kDotKeyStepLarge : kDotKeyStep;
    gfx::Vector2dF delta;
    switch (event.key_code()) {
      case ui::VKEY_RETURN:
      case ui::VKEY_SPACE:
        // The first colour, in the middle (Zen's first click).
        if (big || !model.Click(gfx::RectF(GetLocalBounds()).CenterPoint())) {
          return false;
        }
        picker_->OnModelChanged(/*animate=*/true);
        return true;
      case ui::VKEY_DELETE:
      case ui::VKEY_BACK:
        if (!big) {
          return false;
        }
        model.RemovePrimaryDot();
        picker_->OnModelChanged(/*animate=*/true);
        return true;
      case ui::VKEY_LEFT:
        delta.set_x(-step);
        break;
      case ui::VKEY_RIGHT:
        delta.set_x(step);
        break;
      case ui::VKEY_UP:
        delta.set_y(-step);
        break;
      case ui::VKEY_DOWN:
        delta.set_y(step);
        break;
      default:
        return false;
    }
    if (!big) {
      return false;
    }
    // Like a drag, without the spring.
    model.DragTo(big->position + delta);
    picker_->OnModelChanged(/*animate=*/false);
    return true;
  }

  bool OnKeyReleased(const ui::KeyEvent& event) override {
    switch (event.key_code()) {
      case ui::VKEY_LEFT:
      case ui::VKEY_RIGHT:
      case ui::VKEY_UP:
      case ui::VKEY_DOWN:
        // The moves are over: the window's colours follow now.
        if (picker_->model().DraggableDot()) {
          picker_->OnModelChanged(/*animate=*/true);
          return true;
        }
        return false;
      default:
        return false;
    }
  }

  void OnMouseMoved(const ui::MouseEvent& event) override {
    SetHoverBig(picker_->model().HitsPrimaryDot(gfx::PointF(event.location())));
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    SetHoverBig(false);
  }

  ui::Cursor GetCursor(const ui::MouseEvent& event) override {
    return picker_->model().HitsPrimaryDot(gfx::PointF(event.location()))
               ? ui::Cursor(ui::mojom::CursorType::kHand)
               : ui::Cursor();
  }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override {
    const double t = Spring(animation->GetCurrentValue(), 0.3);
    for (auto& [serial, dot] : drawn_) {
      dot.current = gfx::PointF(dot.from.x() + (dot.to.x() - dot.from.x()) * t,
                                dot.from.y() + (dot.to.y() - dot.from.y()) * t);
    }
    SchedulePaint();
  }

  void AnimationEnded(const gfx::Animation* animation) override {
    for (auto& [serial, dot] : drawn_) {
      dot.current = dot.to;
    }
    SchedulePaint();
  }

 private:
  enum class Action { kAdd, kRemove, kAlgorithm };

  struct DrawnDot {
    gfx::PointF from;
    gfx::PointF to;
    gfx::PointF current;
  };

  void OnSchemePressed(KoanThemeController::Scheme scheme) {
    picker_->SetScheme(scheme);
  }

  void OnAction(Action action) {
    PickerModel& model = picker_->model();
    switch (action) {
      case Action::kAdd:
        model.AddDot();
        break;
      case Action::kRemove:
        model.RemoveDot();
        break;
      case Action::kAlgorithm:
        model.ToggleAlgorithm();
        break;
    }
    picker_->OnModelChanged(/*animate=*/true);
  }

  void SetHoverBig(bool hover) {
    if (hover_big_ != hover) {
      hover_big_ = hover;
      SchedulePaint();
    }
  }

  void PaintDot(gfx::Canvas* canvas, const PickerModel::Dot& dot, bool big) {
    const auto it = drawn_.find(dot.serial);
    const gfx::PointF center =
        it == drawn_.end() ? dot.position : it->second.current;
    float scale = 1;
    if (big && dragging_) {
      scale = 1.2f;
    } else if (big && hover_big_) {
      scale = 1.05f;
    }
    const float radius = (big ? kPrimaryDotRadius : kDotRadius) * scale;
    const float border = (big ? kPrimaryDotBorder : kDotBorder) * scale;
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(picker_->palette().dot_border);
    canvas->DrawCircle(center, radius, flags);
    flags.setColor(theme::ToSkColor(dot.color));
    canvas->DrawCircle(center, radius - border, flags);
  }

  const raw_ptr<KoanThemePickerView> picker_;
  std::vector<raw_ptr<KoanThemePickerButton>> scheme_buttons_;
  raw_ptr<KoanThemePickerButton> add_ = nullptr;
  raw_ptr<KoanThemePickerButton> remove_ = nullptr;
  raw_ptr<KoanThemePickerButton> algorithm_ = nullptr;
  raw_ptr<views::Label> click_to_add_ = nullptr;

  std::map<int, DrawnDot> drawn_;
  gfx::LinearAnimation animation_{
      kDotAnimation, gfx::LinearAnimation::kDefaultFrameRate, this};
  bool dragging_ = false;
  bool right_pressed_on_dot_ = false;
  bool hover_big_ = false;
};

BEGIN_METADATA(KoanThemeGradientField)
END_METADATA

// A preset swatch: one colour, or Zen's three-colour gradient circle.
class KoanThemeSwatch : public views::Button {
  METADATA_HEADER(KoanThemeSwatch, views::Button)

 public:
  KoanThemeSwatch(PressedCallback callback,
                  const theme::Preset& preset,
                  int index)
      : views::Button(std::move(callback)), preset_(preset), index_(index) {
    GetViewAccessibility().SetName(
        base::UTF8ToUTF16(theme::HexColor(preset.swatch[0])));
  }

  void SetPalette(const Palette& palette) {
    palette_ = palette;
    SchedulePaint();
  }

  int GetIndex() const { return index_; }

  // The swatches are one tab stop: the arrows move between them (across
  // pages) and only the last one focused takes the focus.
  void SetArrowCallback(
      base::RepeatingCallback<void(int index, int step)> callback) {
    on_arrow_ = std::move(callback);
  }
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (on_arrow_ && (event.key_code() == ui::VKEY_LEFT ||
                      event.key_code() == ui::VKEY_RIGHT)) {
      on_arrow_.Run(index_, event.key_code() == ui::VKEY_LEFT ? -1 : 1);
      return true;
    }
    return views::Button::OnKeyPressed(event);
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kSwatchSize, kSwatchSize);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    // Zen scales the swatch on hover and while pressed.
    float scale = 1;
    if (GetState() == STATE_PRESSED) {
      scale = 0.95f;
    } else if (GetState() == STATE_HOVERED) {
      scale = 1.05f;
    }
    const gfx::PointF center = gfx::RectF(GetLocalBounds()).CenterPoint();
    const float radius = kSwatchSize / 2.f * scale;
    // box-shadow: 0 0 1px 1px rgba(0, 0, 0, 0.1).
    cc::PaintFlags shadow;
    shadow.setAntiAlias(true);
    shadow.setColor(palette_.swatch_shadow);
    canvas->DrawCircle(center, radius + 1, shadow);

    const gfx::RectF box(center.x() - radius, center.y() - radius, 2 * radius,
                         2 * radius);
    gfx::ScopedCanvas scoped(canvas);
    canvas->ClipPath(SkPath::Circle(center.x(), center.y(), radius), true);
    if (preset_.swatch.size() < 3) {
      canvas->DrawColor(preset_.swatch[0]);
      return;
    }
    // radial-gradient(circle at 0% 0%, c1, transparent 100%),
    // radial-gradient(circle at 100% 0%, c2, transparent 100%),
    // linear-gradient(to top, c3 0%, transparent 60%).
    auto stop = [](SkColor color, float position) {
      return theme::GradientLayer::Stop{SkColor4f::FromColor(color), position};
    };
    constexpr SkColor4f kTransparent = {0, 0, 0, 0};
    std::vector<theme::GradientLayer> layers(3);
    layers[0].kind = theme::GradientLayer::Kind::kRadial;
    layers[0].center = gfx::PointF(0, 0);
    layers[0].stops = {stop(preset_.swatch[0], 0), {kTransparent, 1}};
    layers[1].kind = theme::GradientLayer::Kind::kRadial;
    layers[1].center = gfx::PointF(1, 0);
    layers[1].stops = {stop(preset_.swatch[1], 0), {kTransparent, 1}};
    layers[2].kind = theme::GradientLayer::Kind::kLinear;
    layers[2].angle = 0;
    layers[2].stops = {stop(preset_.swatch[2], 0), {kTransparent, 0.6f}};
    theme::PaintLayers(canvas, box, layers);
  }

 private:
  const theme::Preset preset_;
  const int index_;
  Palette palette_;
  base::RepeatingCallback<void(int index, int step)> on_arrow_;
};

BEGIN_METADATA(KoanThemeSwatch)
ADD_READONLY_PROPERTY_METADATA(int, Index)
END_METADATA

// The preset pages (Zen's PanelUI-zen-gradient-generator-color-pages): one
// page of swatches at a time, scrolled smoothly by the arrows.
class KoanThemePresetPages : public views::View, public gfx::AnimationDelegate {
  METADATA_HEADER(KoanThemePresetPages, views::View)

 public:
  explicit KoanThemePresetPages(KoanThemePickerView* picker) : picker_(picker) {
    strip_ = AddChildView(std::make_unique<views::View>());
    int index = 0;
    const auto& pages = theme::PresetPages();
    for (size_t page = 0; page < pages.size(); ++page) {
      const auto& presets = pages[page];
      // justify-content: space-between, with 1px padding.
      const int count = static_cast<int>(presets.size());
      const float step =
          count > 1
              ? static_cast<float>(kPageWidth - 2 - kSwatchSize) / (count - 1)
              : 0;
      for (int i = 0; i < count; ++i) {
        KoanThemeSwatch* swatch =
            strip_->AddChildView(std::make_unique<KoanThemeSwatch>(
                base::BindRepeating(&KoanThemePresetPages::OnSwatchPressed,
                                    base::Unretained(this), page, i),
                presets[i], index++));
        swatch->SetBounds(static_cast<int>(page) * kPageWidth + 1 +
                              static_cast<int>(std::round(i * step)),
                          (kArrowSize - kSwatchSize) / 2, kSwatchSize,
                          kSwatchSize);
        swatch->SetArrowCallback(base::BindRepeating(
            &KoanThemePresetPages::MoveFocus, base::Unretained(this)));
        swatch->GetViewAccessibility().SetDescription(base::UTF8ToUTF16(
            base::StringPrintf("Page %zu of %zu, %d of %d", page + 1,
                               pages.size(), i + 1, count)));
        pages_of_.push_back(static_cast<int>(page));
      }
    }
    strip_->SetBounds(0, 0, static_cast<int>(pages.size()) * kPageWidth,
                      kArrowSize);
    SetFocusableSwatch(0);
  }

  KoanThemePresetPages(const KoanThemePresetPages&) = delete;
  KoanThemePresetPages& operator=(const KoanThemePresetPages&) = delete;
  ~KoanThemePresetPages() override = default;

  int page() const { return page_; }
  int page_count() const {
    return static_cast<int>(theme::PresetPages().size());
  }

  // Zen's arrows: left wraps around, right stops at the last page (its
  // button is disabled there).
  void Scroll(bool forward) {
    ScrollTo(forward ? (page_ + 1) % page_count()
                     : (page_ - 1 + page_count()) % page_count());
  }

  void SetPalette(const Palette& palette) {
    for (views::View* child : strip_->children()) {
      static_cast<KoanThemeSwatch*>(child)->SetPalette(palette);
    }
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kPageWidth, kArrowSize);
  }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override {
    const double t = gfx::Tween::CalculateValue(gfx::Tween::EASE_IN_OUT,
                                                animation->GetCurrentValue());
    strip_->SetX(gfx::Tween::IntValueBetween(t, from_, -page_ * kPageWidth));
  }

  void AnimationEnded(const gfx::Animation* animation) override {
    strip_->SetX(-page_ * kPageWidth);
  }

 private:
  void ScrollTo(int page) {
    if (page == page_) {
      return;
    }
    page_ = page;
    // The focusable swatch is the page's first.
    const auto first = std::ranges::find(pages_of_, page_);
    SetFocusableSwatch(static_cast<int>(first - pages_of_.begin()));
    if (!gfx::Animation::ShouldRenderRichAnimation()) {
      strip_->SetX(-page_ * kPageWidth);
      return;
    }
    from_ = strip_->x();
    animation_.Stop();
    animation_.Start();
  }

  void SetFocusableSwatch(int index) {
    const auto& swatches = strip_->children();
    for (size_t i = 0; i < swatches.size(); ++i) {
      swatches[i]->SetFocusBehavior(
          static_cast<int>(i) == index
              ? views::PlatformStyle::kDefaultFocusBehavior
              : FocusBehavior::NEVER);
    }
  }

  // An arrow key on swatch `index`: focus the next or previous one, on its
  // page.
  void MoveFocus(int index, int step) {
    const int count = static_cast<int>(strip_->children().size());
    const int next = (index + step + count) % count;
    ScrollTo(pages_of_[next]);
    SetFocusableSwatch(next);
    strip_->children()[next]->RequestFocus();
    picker_->UpdateControls();
  }

  void OnSwatchPressed(size_t page, int index) {
    picker_->model().ApplyPreset(theme::PresetPages()[page][index]);
    picker_->OnModelChanged(/*animate=*/true);
  }

  const raw_ptr<KoanThemePickerView> picker_;
  raw_ptr<views::View> strip_ = nullptr;
  // Each swatch's page.
  std::vector<int> pages_of_;
  int page_ = 0;
  int from_ = 0;
  gfx::LinearAnimation animation_{
      kPageAnimation, gfx::LinearAnimation::kDefaultFrameRate, this};
};

BEGIN_METADATA(KoanThemePresetPages)
END_METADATA

// Zen's opacity slider: a range input over a sine wave that flattens as the
// opacity goes down. Hidden (a full grey wave) for the default theme.
class KoanThemeOpacitySlider : public views::View {
  METADATA_HEADER(KoanThemeOpacitySlider, views::View)

 public:
  explicit KoanThemeOpacitySlider(KoanThemePickerView* picker)
      : picker_(picker) {
    MakeFocusable(this);
    views::InstallPillHighlightPathGenerator(this);
    GetViewAccessibility().SetRole(ax::mojom::Role::kSlider);
    GetViewAccessibility().SetName(u"Opacity");
    GetViewAccessibility().AddAction(ax::mojom::Action::kIncrement);
    GetViewAccessibility().AddAction(ax::mojom::Action::kDecrement);
    GetViewAccessibility().SetMinValueForRange(
        static_cast<float>(PickerModel::kMinOpacity));
    GetViewAccessibility().SetMaxValueForRange(
        static_cast<float>(PickerModel::kMaxOpacity));
  }

  // The value changed (or whether there's one: none for the default theme).
  void Update() {
    const bool enabled = picker_->color_count() > 0;
    GetViewAccessibility().SetIsEnabled(enabled);
    GetViewAccessibility().SetValueForRange(
        static_cast<float>(picker_->model().opacity()));
    GetViewAccessibility().SetValue(Percent(picker_->model().opacity()));
    SchedulePaint();
  }

  KoanThemeOpacitySlider(const KoanThemeOpacitySlider&) = delete;
  KoanThemeOpacitySlider& operator=(const KoanThemeOpacitySlider&) = delete;
  ~KoanThemeOpacitySlider() override = default;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const Palette& palette = picker_->palette();
    const bool is_default = picker_->color_count() == 0;
    const double progress = is_default ? 1 : Progress();
    const float center_y = height() / 2.f;

    // The track: a pill behind the wave (the wave box starts 5px left of
    // the input, the pill 8px into it).
    cc::PaintFlags track;
    track.setAntiAlias(true);
    track.setColor(palette.track);
    canvas->DrawRoundRect(
        gfx::RectF(3, center_y - kTrackHeight / 2.f, width() - 8, kTrackHeight),
        kTrackHeight / 2.f, track);

    // The wave's SVG (viewBox 0 -7.605 455 70) is 110% of the box wide,
    // starts 4px into it and is scaled by 1.2 around its centre.
    const float svg_width = width() * 1.1f;
    const float svg_center_x = -5 + 4 + svg_width / 2;
    const float scale = 1.2f * svg_width / 455;
    auto map = [&](float x, float y) {
      return SkPoint::Make(svg_center_x + (x - 227.5f) * scale,
                           center_y + (y - theme::kWaveCenterY) * scale);
    };
    SkPathBuilder path;
    for (const theme::WaveSegment& segment :
         theme::InterpolateWavePath(progress)) {
      const auto& p = segment.points;
      switch (segment.type) {
        case 'M':
          path.moveTo(map(p[0], p[1]));
          break;
        case 'L':
          path.lineTo(map(p[0], p[1]));
          break;
        default:
          path.cubicTo(map(p[0], p[1]), map(p[2], p[3]), map(p[4], p[5]));
          break;
      }
    }
    cc::PaintFlags stroke;
    stroke.setAntiAlias(true);
    stroke.setStyle(cc::PaintFlags::kStroke_Style);
    stroke.setStrokeWidth(8 * scale);
    stroke.setStrokeCap(cc::PaintFlags::kRound_Cap);
    stroke.setStrokeJoin(cc::PaintFlags::kRound_Join);
    if (is_default || progress <= 0.001) {
      stroke.setColor(palette.wave_dim);
    } else {
      // Solid up to the thumb, dimmed after it (the stops sit at the
      // opacity along the wave's bounding box).
      const float split = static_cast<float>(progress);
      const SkPoint points[] = {map(theme::kWaveStartX, 0),
                                map(theme::kWaveEndX, 0)};
      const SkColor4f colors[] = {SkColor4f::FromColor(palette.wave),
                                  SkColor4f::FromColor(palette.wave),
                                  SkColor4f::FromColor(palette.wave_dim),
                                  SkColor4f::FromColor(palette.wave_dim)};
      const SkScalar positions[] = {0, split, split, 1};
      stroke.setShader(cc::PaintShader::MakeLinearGradient(
          points, colors, positions, 4, SkTileMode::kClamp));
    }
    canvas->DrawPath(path.detach(), stroke);

    if (is_default) {
      return;
    }
    // The thumb grows with the opacity.
    const gfx::RectF thumb = ThumbBounds();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(palette.thumb);
    canvas->DrawRoundRect(thumb, thumb.width() / 2, flags);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!event.IsOnlyLeftMouseButton() || picker_->color_count() == 0) {
      return false;
    }
    dragging_ = true;
    SetFromX(event.x());
    return true;
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    SetFromX(event.x());
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override { EndDrag(); }

  void OnMouseCaptureLost() override { EndDrag(); }

  // Left/Down and Right/Up step by 0.05, Home and End go to the ends.
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    const double opacity = picker_->model().opacity();
    switch (event.key_code()) {
      case ui::VKEY_LEFT:
      case ui::VKEY_DOWN:
        return SetOpacity(opacity - kOpacityKeyStep);
      case ui::VKEY_RIGHT:
      case ui::VKEY_UP:
        return SetOpacity(opacity + kOpacityKeyStep);
      case ui::VKEY_HOME:
        return SetOpacity(PickerModel::kMinOpacity);
      case ui::VKEY_END:
        return SetOpacity(PickerModel::kMaxOpacity);
      default:
        return false;
    }
  }

  bool HandleAccessibleAction(const ui::AXActionData& action_data) override {
    const double opacity = picker_->model().opacity();
    switch (action_data.action) {
      case ax::mojom::Action::kIncrement:
        return SetOpacity(opacity + kOpacityKeyStep);
      case ax::mojom::Action::kDecrement:
        return SetOpacity(opacity - kOpacityKeyStep);
      default:
        return views::View::HandleAccessibleAction(action_data);
    }
  }

 private:
  // The opacity's position in the slider's range.
  double Progress() const {
    return std::clamp((picker_->model().opacity() - PickerModel::kMinOpacity) /
                          (PickerModel::kMaxOpacity - PickerModel::kMinOpacity),
                      0.0, 1.0);
  }

  gfx::RectF ThumbBounds() const {
    const double progress = Progress();
    const float thumb_width = 10 + 15 * progress;
    const float thumb_height = 40 + 15 * progress;
    // The input has 5px padding; the thumb stays inside it.
    const float x = 5 + (width() - 10 - thumb_width) * progress;
    return gfx::RectF(x, (height() - thumb_height) / 2, thumb_width,
                      thumb_height);
  }

  void SetFromX(int x) {
    const float thumb_width = ThumbBounds().width();
    const double progress = std::clamp(
        (x - 5 - thumb_width / 2) / (width() - 10 - thumb_width), 0.f, 1.f);
    // step="0.001"
    const double opacity = std::round((PickerModel::kMinOpacity +
                                       progress * (PickerModel::kMaxOpacity -
                                                   PickerModel::kMinOpacity)) *
                                      1000) /
                           1000;
    if (opacity != picker_->model().opacity()) {
      picker_->model().set_opacity(opacity);
      picker_->OnModelChanged(/*animate=*/false);
    }
  }

  void EndDrag() {
    if (dragging_) {
      // The drag is over: the window's colours follow now.
      dragging_ = false;
      picker_->OnModelChanged(/*animate=*/true);
    }
  }

  // A key or assistive tech: the window's colours follow at once.
  bool SetOpacity(double opacity) {
    if (picker_->color_count() == 0) {
      return false;
    }
    opacity = std::round(std::clamp(opacity, PickerModel::kMinOpacity,
                                    PickerModel::kMaxOpacity) *
                         1000) /
              1000;
    if (opacity != picker_->model().opacity()) {
      picker_->model().set_opacity(opacity);
      picker_->OnModelChanged(/*animate=*/true);
    }
    return true;
  }

  const raw_ptr<KoanThemePickerView> picker_;
  bool dragging_ = false;
};

BEGIN_METADATA(KoanThemeOpacitySlider)
END_METADATA

// Zen's grain dial: 16 steps around a knob; drag the handle around it.
class KoanThemeGrainDial : public views::View {
  METADATA_HEADER(KoanThemeGrainDial, views::View)

 public:
  explicit KoanThemeGrainDial(KoanThemePickerView* picker) : picker_(picker) {
    MakeFocusable(this);
    // Around the step dots.
    views::InstallCircleHighlightPathGenerator(this,
                                               gfx::Insets(kDialMargin - 5));
    GetViewAccessibility().SetRole(ax::mojom::Role::kSlider);
    GetViewAccessibility().SetName(u"Grain");
    GetViewAccessibility().AddAction(ax::mojom::Action::kIncrement);
    GetViewAccessibility().AddAction(ax::mojom::Action::kDecrement);
    GetViewAccessibility().SetMinValueForRange(0);
    GetViewAccessibility().SetMaxValueForRange(1);
  }

  void Update() {
    GetViewAccessibility().SetValueForRange(
        static_cast<float>(picker_->model().texture()));
    GetViewAccessibility().SetValue(Percent(picker_->model().texture()));
    SchedulePaint();
  }

  KoanThemeGrainDial(const KoanThemeGrainDial&) = delete;
  KoanThemeGrainDial& operator=(const KoanThemeGrainDial&) = delete;
  ~KoanThemeGrainDial() override = default;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const Palette& palette = picker_->palette();
    const double texture = picker_->model().texture();
    const float size = kDialSize;
    const gfx::PointF center(kDialMargin + size / 2, kDialMargin + size / 2);

    // The 16 step dots, lit from the top clockwise up to the value.
    cc::PaintFlags dot;
    dot.setAntiAlias(true);
    for (int i = 0; i < kGrainSteps; ++i) {
      const double angle = i * 2 * std::numbers::pi / kGrainSteps;
      const int step = (i + 4) % kGrainSteps;
      const bool active = static_cast<double>(step) / kGrainSteps <= texture;
      dot.setColor(active
                       ? palette.dial_dot
                       : SkColorSetA(palette.dial_dot,
                                     static_cast<U8CPU>(
                                         SkColorGetA(palette.dial_dot) * 0.4)));
      canvas->DrawCircle(gfx::PointF(center.x() + std::cos(angle) * size / 2,
                                     center.y() + std::sin(angle) * size / 2),
                         2, dot);
    }

    // The knob (60%): the grain at the current opacity (hard-light), a
    // shine and a thin border.
    const float knob = size * 0.3f;
    {
      gfx::ScopedCanvas scoped(canvas);
      canvas->ClipPath(SkPath::Circle(center.x(), center.y(), knob), true);
      const gfx::RectF box(center.x() - knob, center.y() - knob, 2 * knob,
                           2 * knob);
      if (texture > 0) {
        canvas->SaveLayerWithFlags([&] {
          cc::PaintFlags blend;
          blend.setBlendMode(SkBlendMode::kHardLight);
          return blend;
        }());
        theme::PaintGrain(canvas, box, texture);
        canvas->Restore();
      }
      theme::GradientLayer shine;
      shine.kind = theme::GradientLayer::Kind::kLinear;
      shine.angle = -45;
      shine.stops = {{{0, 0, 0, 0}, -0.1f},
                     {SkColor4f::FromColor(palette.dial_shine), 1.1f}};
      theme::PaintLayers(canvas, box, {shine});
    }
    cc::PaintFlags border;
    border.setAntiAlias(true);
    border.setStyle(cc::PaintFlags::kStroke_Style);
    border.setStrokeWidth(1);
    border.setColor(palette.dial_border);
    canvas->DrawCircle(center, knob - 0.5f, border);

    // The handle, at the value's angle and turned with it.
    const gfx::RectF handle = HandleBounds();
    const gfx::PointF handle_center = handle.CenterPoint();
    gfx::ScopedCanvas scoped(canvas);
    canvas->sk_canvas()->translate(handle_center.x(), handle_center.y());
    canvas->sk_canvas()->rotate(texture * 360);
    canvas->sk_canvas()->translate(-handle_center.x(), -handle_center.y());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(palette.dial_handle);
    canvas->DrawRoundRect(handle, 2, flags);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    // Only the handle takes the mouse, like Zen's.
    const gfx::PointF handle = HandleBounds().CenterPoint();
    if (!event.IsOnlyLeftMouseButton() ||
        (gfx::PointF(event.location()) - handle).Length() > 10) {
      return false;
    }
    dragging_ = true;
    return true;
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    const gfx::PointF center(kDialMargin + kDialSize / 2.f,
                             kDialMargin + kDialSize / 2.f);
    const double degrees =
        std::atan2(event.y() - center.y(), event.x() - center.x()) * 180 /
        std::numbers::pi;
    if (picker_->model().SetTextureFromAngle(degrees)) {
      picker_->OnModelChanged(/*animate=*/false);
    }
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override { EndDrag(); }

  void OnMouseCaptureLost() override { EndDrag(); }

  // The arrows turn the dial by a step (wrapping around, as dragging does),
  // Home and End go to no grain and the most.
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    switch (event.key_code()) {
      case ui::VKEY_LEFT:
      case ui::VKEY_DOWN:
        return Step(-1);
      case ui::VKEY_RIGHT:
      case ui::VKEY_UP:
        return Step(1);
      case ui::VKEY_HOME:
        return SetSteps(0);
      case ui::VKEY_END:
        return SetSteps(kGrainSteps - 1);
      default:
        return false;
    }
  }

  bool HandleAccessibleAction(const ui::AXActionData& action_data) override {
    switch (action_data.action) {
      case ax::mojom::Action::kIncrement:
        return Step(1);
      case ax::mojom::Action::kDecrement:
        return Step(-1);
      default:
        return views::View::HandleAccessibleAction(action_data);
    }
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    OnMouseMoved(event);
  }

  void OnMouseMoved(const ui::MouseEvent& event) override {
    const bool hover =
        (gfx::PointF(event.location()) - HandleBounds().CenterPoint())
            .Length() <= 10;
    if (hover != hover_) {
      hover_ = hover;
      SchedulePaint();
    }
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    if (hover_) {
      hover_ = false;
      SchedulePaint();
    }
  }

 private:
  // Zen's handle: 6x12 (14 when hovered), its top-left on the circle at the
  // value's angle, offset by half its size.
  gfx::RectF HandleBounds() const {
    const float size = kDialSize;
    const double rotation =
        (picker_->model().texture() * 360 - 90) * std::numbers::pi / 180;
    const float top = std::sin(rotation) * (size / 2) - 6;
    const float left = std::cos(rotation) * (size / 2) - 3;
    return gfx::RectF(kDialMargin + size / 2 + left,
                      kDialMargin + size / 2 + top, 6, hover_ ? 14 : 12);
  }

  void EndDrag() {
    if (dragging_) {
      dragging_ = false;
      picker_->OnModelChanged(/*animate=*/true);
    }
  }

  bool Step(int step) {
    const int steps =
        static_cast<int>(std::round(picker_->model().texture() * kGrainSteps));
    return SetSteps((steps + step + kGrainSteps) % kGrainSteps);
  }

  bool SetSteps(int steps) {
    const double texture = static_cast<double>(steps) / kGrainSteps;
    if (texture != picker_->model().texture()) {
      picker_->model().set_texture(texture);
      picker_->OnModelChanged(/*animate=*/true);
    }
    return true;
  }

  const raw_ptr<KoanThemePickerView> picker_;
  bool hover_ = false;
  bool dragging_ = false;
};

BEGIN_METADATA(KoanThemeGrainDial)
END_METADATA

// The custom colour list (Zen's zen.theme.gradient.show-custom-colors).
class KoanThemeCustomColors : public views::View {
  METADATA_HEADER(KoanThemeCustomColors, views::View)

 public:
  explicit KoanThemeCustomColors(KoanThemePickerView* picker)
      : picker_(picker) {
    title_ = AddChildView(std::make_unique<views::Label>(u"Custom Color"));
    title_->SetFontList(SemiboldFont(12));
    title_->SetAutoColorReadabilityEnabled(false);
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    input_ = AddChildView(std::make_unique<views::Textfield>());
    input_->SetPlaceholderText(u"#rrggbb");
    input_->GetViewAccessibility().SetName(u"Custom color");
    opacity_ = AddChildView(std::make_unique<views::Textfield>());
    opacity_->SetText(u"1");
    opacity_->GetViewAccessibility().SetName(u"Custom color opacity");
    add_ = AddChildView(std::make_unique<KoanThemePickerButton>(
        base::BindRepeating(&KoanThemeCustomColors::AddColor,
                            base::Unretained(this)),
        kKoanThemePlusIcon, kButtonSize, kButtonIconSize, 0.5f,
        u"Add custom color"));
  }

  KoanThemeCustomColors(const KoanThemeCustomColors&) = delete;
  KoanThemeCustomColors& operator=(const KoanThemeCustomColors&) = delete;
  ~KoanThemeCustomColors() override = default;

  // Rebuilds the list if the colours changed, or always after a palette
  // change.
  void Update(bool palette_changed) {
    const std::vector<SkColor>& colors = picker_->model().custom_colors();
    if (!palette_changed && colors == shown_) {
      return;
    }
    shown_ = colors;
    const Palette& palette = picker_->palette();
    title_->SetEnabledColor(palette.text);
    add_->SetPalette(palette, palette.text);
    for (raw_ptr<views::View>& row : rows_) {
      RemoveChildViewT(std::exchange(row, nullptr));
    }
    rows_.clear();
    for (size_t i = 0; i < colors.size(); ++i) {
      auto row = std::make_unique<views::View>();
      auto* swatch = row->AddChildView(std::make_unique<views::View>());
      swatch->SetBounds(5, (kCustomRowHeight - kCustomSwatchSize) / 2,
                        kCustomSwatchSize, kCustomSwatchSize);
      swatch->SetBackground(views::CreateRoundedRectBackground(
          colors[i], 5, /*for_border_thickness=*/0));
      auto* label = row->AddChildView(std::make_unique<views::Label>(
          base::UTF8ToUTF16(theme::HexColor(colors[i]))));
      label->SetFontList(SemiboldFont(12));
      label->SetAutoColorReadabilityEnabled(false);
      label->SetEnabledColor(palette.text);
      label->SetBounds(5 + kCustomSwatchSize + 10, 0, 200, kCustomRowHeight);
      label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      auto* remove = row->AddChildView(std::make_unique<KoanThemePickerButton>(
          base::BindRepeating(&KoanThemeCustomColors::RemoveColor,
                              base::Unretained(this), i),
          kKoanThemeUnpinIcon, kCustomRowHeight - 6, 16, 0.5f,
          u"Remove custom color"));
      remove->SetPalette(palette, palette.text);
      remove->SetBounds(kContentWidth - kCustomRowHeight, 3,
                        kCustomRowHeight - 6, kCustomRowHeight - 6);
      rows_.push_back(AddChildView(std::move(row)));
    }
    PreferredSizeChanged();
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kContentWidth,
                     kRowGap + 20 + kButtonSize +
                         (rows_.empty() ? 0
                                        : 15 + static_cast<int>(rows_.size()) *
                                                   kCustomRowHeight));
  }

  void Layout(PassKey) override {
    title_->SetBounds(0, kRowGap, kContentWidth, 15);
    const int row_y = kRowGap + 20;
    add_->SetBounds(kContentWidth - kButtonSize, row_y, kButtonSize,
                    kButtonSize);
    opacity_->SetBounds(add_->x() - 6 - 50, row_y, 50, kButtonSize);
    input_->SetBounds(0, row_y, opacity_->x() - 6, kButtonSize);
    int y = row_y + kButtonSize + 15;
    for (views::View* row : rows_) {
      row->SetBounds(0, y, kContentWidth, kCustomRowHeight);
      y += kCustomRowHeight;
    }
  }

 private:
  // Zen's addCustomColor(): a hex colour, with the opacity as its alpha.
  void AddColor() {
    std::string color;
    base::TrimWhitespaceASCII(base::UTF16ToUTF8(input_->GetText()),
                              base::TRIM_ALL, &color);
    if (color.empty()) {
      return;
    }
    double opacity = 1;
    if (!base::StringToDouble(base::UTF16ToUTF8(opacity_->GetText()),
                              &opacity)) {
      opacity = 1;
    }
    if (!color.starts_with('#') && color.size() >= 3 && color.size() <= 6 &&
        std::ranges::all_of(color, base::IsHexDigit<char>)) {
      color = "#" + color;
    }
    if (opacity < 1 && color.size() == 7) {
      color += base::StringPrintf(
          "%02X", static_cast<int>(std::round(std::max(0.0, opacity) * 255)));
    }
    const std::optional<SkColor> parsed = theme::ParseHexColor(color);
    if (!parsed) {
      return;
    }
    input_->SetText(u"");
    opacity_->SetText(u"1");
    picker_->model().AddCustomColor(*parsed);
    picker_->OnModelChanged(/*animate=*/true);
  }

  void RemoveColor(size_t index) {
    picker_->model().RemoveCustomColor(index);
    picker_->OnModelChanged(/*animate=*/true);
  }

  const raw_ptr<KoanThemePickerView> picker_;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Textfield> input_ = nullptr;
  raw_ptr<views::Textfield> opacity_ = nullptr;
  raw_ptr<KoanThemePickerButton> add_ = nullptr;
  std::vector<raw_ptr<views::View>> rows_;
  // The colours the list shows.
  std::vector<SkColor> shown_;
};

BEGIN_METADATA(KoanThemeCustomColors)
END_METADATA

KoanThemePickerView::KoanThemePickerView(KoanThemeController* controller,
                                         KoanThemeController::Space space,
                                         const theme::SpaceTheme& theme)
    : controller_(controller), space_(space) {
  model_.Load(theme);
  field_ = AddChildView(std::make_unique<KoanThemeGradientField>(this));
  page_left_ = AddChildView(std::make_unique<KoanThemePickerButton>(
      base::BindRepeating(
          [](KoanThemePickerView* view) {
            static_cast<KoanThemePresetPages*>(view->pages_.get())
                ->Scroll(/*forward=*/false);
            view->UpdateControls();
          },
          base::Unretained(this)),
      kKoanThemeArrowLeftIcon, kArrowSize, kArrowIconSize, 0.4f,
      u"Previous colors"));
  pages_ = AddChildView(std::make_unique<KoanThemePresetPages>(this));
  page_right_ = AddChildView(std::make_unique<KoanThemePickerButton>(
      base::BindRepeating(
          [](KoanThemePickerView* view) {
            static_cast<KoanThemePresetPages*>(view->pages_.get())
                ->Scroll(/*forward=*/true);
            view->UpdateControls();
          },
          base::Unretained(this)),
      kKoanThemeArrowRightIcon, kArrowSize, kArrowIconSize, 0.4f,
      u"Next colors"));
  slider_ = AddChildView(std::make_unique<KoanThemeOpacitySlider>(this));
  dial_ = AddChildView(std::make_unique<KoanThemeGrainDial>(this));
  if (controller_->ShowsCustomColors()) {
    custom_ = AddChildView(std::make_unique<KoanThemeCustomColors>(this));
  }
  static_cast<KoanThemeGradientField*>(field_.get())->SyncDots(false);
  UpdateControls();
}

KoanThemePickerView::~KoanThemePickerView() = default;

std::string KoanThemePickerView::GetDots() const {
  std::vector<const PickerModel::Dot*> dots;
  for (const PickerModel::Dot& dot : model_.dots()) {
    dots.push_back(&dot);
  }
  std::ranges::sort(dots, {}, &PickerModel::Dot::serial);
  std::vector<std::string> parts;
  for (const PickerModel::Dot* dot : dots) {
    parts.push_back(base::StringPrintf(
        "%d,%d,%d@%d,%d", dot->color[0], dot->color[1], dot->color[2],
        static_cast<int>(std::round(dot->position.x())),
        static_cast<int>(std::round(dot->position.y()))));
  }
  return base::JoinString(parts, ";");
}

int KoanThemePickerView::GetPage() const {
  return static_cast<KoanThemePresetPages*>(pages_.get())->page();
}

KoanThemeController::Scheme KoanThemePickerView::scheme() const {
  return controller_->GetScheme();
}

void KoanThemePickerView::SetScheme(KoanThemeController::Scheme scheme) {
  controller_->SetScheme(scheme);
}

void KoanThemePickerView::OnModelChanged(bool animate) {
  static_cast<KoanThemeGradientField*>(field_.get())->SyncDots(animate);
  if (custom_) {
    static_cast<KoanThemeCustomColors*>(custom_.get())
        ->Update(/*palette_changed=*/false);
  }
  UpdateControls();
  controller_->PreviewTheme(space_, model_.ToTheme(), /*dragging=*/!animate);
}

void KoanThemePickerView::OnThemeChanged() {
  views::View::OnThemeChanged();
  palette_ = GetPalette(*GetColorProvider());
  static_cast<KoanThemeGradientField*>(field_.get())->SetPalette(palette_);
  static_cast<KoanThemePresetPages*>(pages_.get())->SetPalette(palette_);
  page_left_->SetPalette(palette_, palette_.text);
  page_right_->SetPalette(palette_, palette_.text);
  if (custom_) {
    static_cast<KoanThemeCustomColors*>(custom_.get())
        ->Update(/*palette_changed=*/true);
  }
  UpdateControls();
}

void KoanThemePickerView::UpdateControls() {
  static_cast<KoanThemeGradientField*>(field_.get())->UpdateControls();
  auto* pages = static_cast<KoanThemePresetPages*>(pages_.get());
  page_left_->SetEnabled(pages->page() != 0);
  page_right_->SetEnabled(pages->page() != pages->page_count() - 1);
  static_cast<KoanThemeOpacitySlider*>(slider_.get())->Update();
  static_cast<KoanThemeGrainDial*>(dial_.get())->Update();
}

gfx::Size KoanThemePickerView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  int height = kFieldSize + kRowGap + kArrowSize + kRowGap + kControlsPadding +
               kControlsHeight + kControlsPadding;
  if (custom_) {
    height += custom_->GetPreferredSize().height();
  }
  return gfx::Size(kContentWidth, height);
}

void KoanThemePickerView::Layout(PassKey) {
  field_->SetBounds(0, 0, kFieldSize, kFieldSize);
  const int pages_y = kFieldSize + kRowGap;
  page_left_->SetBounds(0, pages_y, kArrowSize, kArrowSize);
  pages_->SetBounds(kArrowSize + kPagesMargin, pages_y, kPageWidth, kArrowSize);
  page_right_->SetBounds(kContentWidth - kArrowSize, pages_y, kArrowSize,
                         kArrowSize);
  const int controls_y = pages_y + kArrowSize + kRowGap + kControlsPadding;
  const int dial_x = kContentWidth - kControlsPadding - kDialSize;
  slider_->SetBounds(kControlsPadding, controls_y,
                     dial_x - kControlsGap - kControlsPadding, kControlsHeight);
  dial_->SetBounds(dial_x - kDialMargin, controls_y - kDialMargin,
                   kDialSize + 2 * kDialMargin, kDialSize + 2 * kDialMargin);
  if (custom_) {
    custom_->SetBounds(0, controls_y + kControlsHeight + kControlsPadding,
                       kContentWidth, custom_->GetPreferredSize().height());
  }
}

BEGIN_METADATA(KoanThemePickerView)
ADD_READONLY_PROPERTY_METADATA(std::string, Dots)
ADD_READONLY_PROPERTY_METADATA(std::string, Algorithm)
ADD_READONLY_PROPERTY_METADATA(double, Opacity)
ADD_READONLY_PROPERTY_METADATA(double, Texture)
ADD_READONLY_PROPERTY_METADATA(int, Page)
ADD_READONLY_PROPERTY_METADATA(std::string, WindowBackground)
ADD_READONLY_PROPERTY_METADATA(std::string, WindowMode)
END_METADATA

KoanThemePicker::KoanThemePicker(KoanThemeController* controller,
                                 KoanThemeController::Space space,
                                 const theme::SpaceTheme& theme,
                                 const gfx::Rect& anchor,
                                 bool left_of_anchor,
                                 base::OnceClosure on_closed)
    : controller_(controller), on_closed_(std::move(on_closed)) {
  delegate_ = std::make_unique<views::BubbleDialogDelegate>(
      views::BubbleAnchor(), left_of_anchor ? views::BubbleBorder::RIGHT_TOP
                                            : views::BubbleBorder::LEFT_TOP);
  delegate_->SetAnchorRect(anchor);
  delegate_->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate_->set_margins(gfx::Insets(kPanelPadding));
  delegate_->set_corner_radius(kPanelRadius);
  delegate_->SetShowCloseButton(false);
  delegate_->SetShowTitle(false);
  delegate_->SetAccessibleTitle(u"Edit Theme");
  delegate_->set_internal_name("KoanThemePicker");
  view_ = delegate_->SetContentsView(
      std::make_unique<KoanThemePickerView>(controller, space, theme));
  delegate_->SetBackgroundColor(kColorKoanThemePickerBackground);
}

KoanThemePicker::~KoanThemePicker() {
  // Closing the widget from here must not call back into the controller.
  on_closed_.Reset();
  view_ = nullptr;
  widget_.reset();
}

void KoanThemePicker::Show(views::Widget* parent) {
  delegate_->set_parent_window(parent->GetNativeView());
  widget_ = views::BubbleDialogDelegate::CreateBubble(
      delegate_.get(),
      base::BindOnce(
          [](KoanThemePicker* picker, views::Widget::ClosedReason) {
            picker->OnWidgetClosed();
          },
          base::Unretained(this)));
  OnEnvironmentChanged();
  widget_->Show();
}

theme::SpaceTheme KoanThemePicker::GetTheme() const {
  return view_->model().ToTheme();
}

void KoanThemePicker::OnEnvironmentChanged() {
  // Only the window's scheme matters here (the scheme buttons, and Zen's
  // panel follows the scheme, not the Space theme).
  const KoanThemeController::Scheme scheme = controller_->GetScheme();
  const bool dark = controller_->GetEnvironment().dark;
  if (scheme == shown_scheme_ && dark == shown_dark_) {
    return;
  }
  shown_scheme_ = scheme;
  shown_dark_ = dark;
  widget_->SetColorModeOverride(dark ? ui::ColorProviderKey::ColorMode::kDark
                                     : ui::ColorProviderKey::ColorMode::kLight);
  view_->UpdateControls();
}

void KoanThemePicker::OnWidgetClosed() {
  if (on_closed_) {
    std::move(on_closed_).Run();
  }
}

}  // namespace koan
