// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_welcome.h"

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ref.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "cc/paint/paint_shader.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout_params.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/vector_icons/vector_icons.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_welcome_icons_data.h"
#include "koan/browser/ui/views/koan_zen_welcome.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// The colours offered for the browser (Koan tints with one colour where Zen
// has a gradient picker), with their names for screen readers.
struct SpaceColor {
  SkColor color;
  const char16_t* name;
};
constexpr auto kSpaceColors = std::to_array<SpaceColor>({
    {SkColorSetRGB(0xd9, 0x6c, 0x4f), u"Coral"},
    {SkColorSetRGB(0xe0, 0xa1, 0x3a), u"Amber"},
    {SkColorSetRGB(0x6f, 0xa8, 0x55), u"Green"},
    {SkColorSetRGB(0x3e, 0x9d, 0x92), u"Teal"},
    {SkColorSetRGB(0x4f, 0x86, 0xd9), u"Blue"},
    {SkColorSetRGB(0x7a, 0x6c, 0xe0), u"Violet"},
    {SkColorSetRGB(0xb4, 0x5f, 0xc9), u"Purple"},
    {SkColorSetRGB(0xd9, 0x5f, 0x93), u"Pink"},
    {SkColorSetRGB(0x8a, 0x8f, 0x98), u"Grey"},
});

// zen-welcome.css; the sizes are in koan_sidebar_metrics.h.
constexpr auto kSidebarPadding = gfx::Insets::TLBR(kWelcomeSidebarPaddingTop,
                                                   kWelcomeSidebarPaddingSide,
                                                   kWelcomeSidebarPaddingBottom,
                                                   kWelcomeSidebarPaddingSide);
// Zen lists every engine and scrolls; Koan shows the first few.
constexpr size_t kMaxEngines = 6;

constexpr base::TimeDelta kCharStagger = base::Milliseconds(35);
constexpr base::TimeDelta kCharStartDelay = base::Milliseconds(800);
constexpr base::TimeDelta kCharDuration = base::Milliseconds(450);
constexpr base::TimeDelta kCharExitStagger = base::Milliseconds(12);
constexpr base::TimeDelta kCharExitDuration = base::Milliseconds(300);
constexpr base::TimeDelta kTextStagger = base::Milliseconds(30);
constexpr base::TimeDelta kTextDuration = base::Milliseconds(300);
constexpr base::TimeDelta kFadeDuration = base::Milliseconds(250);
constexpr base::TimeDelta kPagesFadeDuration = base::Milliseconds(450);
constexpr base::TimeDelta kFinishFadeDuration = base::Milliseconds(300);

gfx::FontList Font(int size, gfx::Font::Weight weight) {
  const gfx::FontList base;
  return base.DeriveWithSizeDelta(size - base.GetFontSize())
      .DeriveWithWeight(weight);
}

struct Palette {
  SkColor accent;
  SkColor accent_hovered;
  SkColor on_accent;
  SkColor sidebar;
  SkColor content;
  SkColor surface;
  SkColor tile;
  SkColor mock;
  SkColor border;
  SkColor shadow;
  SkColor text;
  SkColor text_secondary;
  SkColor radio;
};

// The welcome screen's colours (koan_color_id.h).
Palette GetPalette(const views::View* view) {
  const ui::ColorProvider* provider = view->GetColorProvider();
  return {
      .accent = provider->GetColor(kColorKoanWelcomeAccent),
      .accent_hovered = provider->GetColor(kColorKoanWelcomeAccentHovered),
      .on_accent = provider->GetColor(kColorKoanWelcomeOnAccent),
      .sidebar = provider->GetColor(kColorKoanWelcomeSidebar),
      .content = provider->GetColor(kColorKoanWelcomeContent),
      .surface = provider->GetColor(kColorKoanWelcomeSurface),
      .tile = provider->GetColor(kColorKoanWelcomeTile),
      .mock = provider->GetColor(kColorKoanWelcomeMockWindow),
      .border = provider->GetColor(kColorKoanWelcomeBorder),
      .shadow = provider->GetColor(kColorKoanWelcomeShadow),
      .text = provider->GetColor(kColorKoanWelcomeText),
      .text_secondary = provider->GetColor(kColorKoanWelcomeTextSecondary),
      .radio = provider->GetColor(kColorKoanWelcomeRadio),
  };
}

// Whether to animate (not with the system's reduced motion setting).
bool ShouldAnimate() {
  return gfx::Animation::ShouldRenderRichAnimation();
}

// The screen's sizes, shapes and motion in the current look: Zen's
// (zen-welcome.css), or shadcn's components with koan::look's radii. The
// pages and their flow are the same in both.
struct Metrics {
  bool shadcn = false;
  gfx::Insets sidebar_padding;
  int heading_size;
  int heading_min_size;
  gfx::Font::Weight heading_weight;
  int description_size;
  int description_min_size;
  float description_line_height;
  int button_height;
  int button_font_size;
  gfx::Font::Weight button_weight;
  int button_gap;
  int button_radius;
  int back_font_size;
  int back_height;
  int back_radius;
  int option_height;
  int engine_height;
  int option_font_size;
  int option_padding;
  int option_gap;
  int engine_gap;
  int option_radius;
  int radio_size;
  int radio_dot_inset;
  int card_radius;
  int tile_radius;
  int mock_radius;
  // Motion: how far things rise and slide in, and the fades.
  int rise;
  int slide;
  base::TimeDelta fade;
};

Metrics GetMetrics() {
  using look::Step;
  Metrics m;
  m.shadcn = look::IsShadcnStyle();
  m.button_radius = look::StyleRadius(kWelcomeButtonCornerRadius, Step::kMd);
  m.back_radius = look::StyleRadius(kWelcomeBackCornerRadius, Step::kMd);
  m.option_radius = look::StyleRadius(kWelcomeOptionCornerRadius, Step::kLg);
  m.card_radius = look::StyleRadius(kWelcomeCardCornerRadius, Step::kXl);
  m.tile_radius = look::StyleRadius(kWelcomeTileCornerRadius, Step::kLg);
  m.mock_radius = look::StyleRadius(kWelcomeMockCornerRadius, Step::kXl);
  if (m.shadcn) {
    m.sidebar_padding = gfx::Insets(kWelcomeShadcnSidebarPadding);
    m.heading_size = kWelcomeShadcnHeadingSize;
    m.heading_min_size = kWelcomeShadcnHeadingMinSize;
    m.heading_weight = gfx::Font::Weight::SEMIBOLD;
    m.description_size = kWelcomeShadcnDescriptionSize;
    m.description_min_size = kWelcomeShadcnDescriptionMinSize;
    m.description_line_height = kWelcomeShadcnDescriptionLineHeight;
    m.button_height = kWelcomeShadcnButtonHeight;
    m.button_font_size = kWelcomeShadcnButtonFontSize;
    m.button_weight = gfx::Font::Weight::MEDIUM;
    m.button_gap = kWelcomeShadcnButtonGap;
    m.back_font_size = kWelcomeShadcnBackFontSize;
    m.back_height = kWelcomeShadcnBackHeight;
    m.option_height = kWelcomeShadcnOptionHeight;
    m.engine_height = kWelcomeShadcnEngineHeight;
    m.option_font_size = kWelcomeShadcnOptionFontSize;
    m.option_padding = kWelcomeShadcnOptionPadding;
    m.option_gap = kWelcomeShadcnOptionGap;
    m.engine_gap = kWelcomeShadcnOptionGap;
    m.radio_size = kWelcomeShadcnRadioSize;
    m.radio_dot_inset = kWelcomeShadcnRadioDotInset;
    m.rise = kWelcomeShadcnRise;
    m.slide = kWelcomeShadcnSlide;
    m.fade = base::Milliseconds(kWelcomeShadcnFadeMs);
    return m;
  }
  m.sidebar_padding = kSidebarPadding;
  m.heading_size = kWelcomeHeadingSize;
  m.heading_min_size = kWelcomeHeadingMinSize;
  m.heading_weight = gfx::Font::Weight::BOLD;
  m.description_size = kWelcomeDescriptionSize;
  m.description_min_size = kWelcomeDescriptionSize;
  m.description_line_height = kWelcomeDescriptionLineHeight;
  m.button_height = kWelcomeButtonHeight;
  m.button_font_size = kWelcomeButtonFontSize;
  m.button_weight = gfx::Font::Weight::SEMIBOLD;
  m.button_gap = kWelcomeButtonGap;
  m.back_font_size = kWelcomeBackFontSize;
  m.back_height = kWelcomeBackHeight;
  m.option_height = kWelcomeOptionHeight;
  m.engine_height = kWelcomeEngineHeight;
  m.option_font_size = kWelcomeOptionFontSize;
  m.option_padding = kWelcomeOptionPadding;
  m.option_gap = kWelcomeOptionGap;
  m.engine_gap = kWelcomeEngineGap;
  m.radio_size = kWelcomeRadioSize;
  m.radio_dot_inset = kWelcomeRadioDotInset;
  m.rise = 50;
  m.slide = 120;
  m.fade = kFadeDuration;
  return m;
}

// A 1px outline just inside `rect`.
void StrokeRoundRect(gfx::Canvas* canvas,
                     gfx::RectF rect,
                     float radius,
                     SkColor color,
                     float width = 1) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(width);
  flags.setColor(color);
  rect.Inset(width / 2);
  canvas->DrawRoundRect(rect, std::max(0.0f, radius - width / 2), flags);
}

// Type-ahead in a list of choices: the first after `from` (wrapping around)
// whose name starts with `c`, ignoring case.
std::optional<size_t> FindByFirstLetter(
    const std::vector<std::u16string>& names,
    size_t from,
    char16_t c) {
  if (names.empty() || !base::IsAsciiAlpha(c)) {
    return std::nullopt;
  }
  const char16_t lower = base::ToLowerASCII(c);
  for (size_t step = 1; step <= names.size(); ++step) {
    const size_t i = (from + step) % names.size();
    if (!names[i].empty() && base::ToLowerASCII(names[i][0]) == lower) {
      return i;
    }
  }
  return std::nullopt;
}

// The focus groups of the choice lists: each list is one Tab stop, and the
// arrow keys move within it (as radio buttons do).
enum FocusGroup { kOptionsGroup = 1, kSwatchesGroup, kTilesGroup };

// An Essential's icon from 1x and 2x PNGs.
gfx::ImageSkia DecodeIcon(base::span<const uint8_t> png_1x,
                          base::span<const uint8_t> png_2x) {
  gfx::ImageSkia image;
  const SkBitmap bitmap_1x = gfx::PNGCodec::Decode(png_1x);
  const SkBitmap bitmap_2x = gfx::PNGCodec::Decode(png_2x);
  if (!bitmap_1x.isNull()) {
    image.AddRepresentation(gfx::ImageSkiaRep(bitmap_1x, 1.0f));
  }
  if (!bitmap_2x.isNull()) {
    image.AddRepresentation(gfx::ImageSkiaRep(bitmap_2x, 2.0f));
  }
  return image;
}

// Zen's favicons for the Essentials (welcome_icons/), in the order of
// kWelcomeEssentialApps; `dark` for a dark surface (GitHub's mark is white
// there, as in Zen's dark mode).
gfx::ImageSkia GetEssentialIcon(size_t index, bool dark) {
  namespace icons = welcome_icons;
  switch (index) {
    case 0:
      return DecodeIcon(icons::kObsidian32, icons::kObsidian64);
    case 1:
      return DecodeIcon(icons::kDiscord32, icons::kDiscord64);
    case 2:
      return DecodeIcon(icons::kTrello32, icons::kTrello64);
    case 3:
      return DecodeIcon(icons::kSlack32, icons::kSlack64);
    case 4:
      return dark ? DecodeIcon(icons::kGithubDark32, icons::kGithubDark64)
                  : DecodeIcon(icons::kGithub32, icons::kGithub64);
    case 5:
      return DecodeIcon(icons::kTuta32, icons::kTuta64);
    case 6:
      return DecodeIcon(icons::kNotion32, icons::kNotion64);
    case 7:
      return DecodeIcon(icons::kCalendar32, icons::kCalendar64);
    case 8:
      return DecodeIcon(icons::kFigma32, icons::kFigma64);
    default:
      return gfx::ImageSkia();
  }
}

// Lets the keyboard reach `button` (also on macOS, where buttons otherwise
// only take focus with Full Keyboard Access), with a focus ring of its
// shape.
void MakeFocusable(views::Button* button, int corner_radius) {
  button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  views::InstallRoundRectHighlightPathGenerator(button, gfx::Insets(),
                                                corner_radius);
}

// Plays `view` in: from `offset` and transparent to where it is, after
// `delay`; at once with reduced motion.
void AnimateIn(views::View* view,
               const gfx::Vector2dF& offset,
               base::TimeDelta delay,
               base::TimeDelta duration) {
  if (!view->layer()) {
    view->SetPaintToLayer();
    view->layer()->SetFillsBoundsOpaquely(false);
  }
  if (!ShouldAnimate()) {
    view->layer()->SetTransform(gfx::Transform());
    view->layer()->SetOpacity(1);
    return;
  }
  gfx::Transform start;
  start.Translate(offset);
  view->layer()->SetTransform(start);
  view->layer()->SetOpacity(0);
  views::AnimationBuilder()
      .Once()
      .At(delay)
      .SetDuration(duration)
      .SetTransform(view, gfx::Transform(), gfx::Tween::EASE_OUT_3)
      .SetOpacity(view, 1, gfx::Tween::EASE_OUT);
}

// A page's button. The primary action is filled: Zen's accent button, or
// shadcn's default button (`primary` with `primary-foreground` text). The
// other ("Skip for now") is Zen's accent text, or shadcn's ghost button. A
// disabled button is drawn at half opacity (shadcn's `disabled:opacity-50`).
class KoanWelcomeButton : public views::Button {
  METADATA_HEADER(KoanWelcomeButton, views::Button)

 public:
  KoanWelcomeButton(PressedCallback callback, std::u16string text, bool primary)
      : views::Button(std::move(callback)),
        text_(std::move(text)),
        primary_(primary) {
    GetViewAccessibility().SetName(text_);
    MakeFocusable(this, GetMetrics().button_radius);
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, GetMetrics().button_height);
  }
  void OnEnabledChanged() override {
    views::Button::OnEnabledChanged();
    SchedulePaint();
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const Metrics metrics = GetMetrics();
    const Palette palette = GetPalette(this);
    const bool hovered = GetState() == STATE_HOVERED;
    const gfx::RectF bounds(GetLocalBounds());
    if (!GetEnabled()) {
      canvas->SaveLayerAlpha(0x80);
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    SkColor text = metrics.shadcn ? palette.text : palette.accent;
    if (primary_) {
      flags.setColor(hovered ? palette.accent_hovered : palette.accent);
      canvas->DrawRoundRect(bounds, metrics.button_radius, flags);
      text = palette.on_accent;
    } else if (metrics.shadcn && hovered) {
      // shadcn's ghost button: `accent` behind it on hover only.
      flags.setColor(GetColorProvider()->GetColor(kColorKoanWelcomeHover));
      canvas->DrawRoundRect(bounds, metrics.button_radius, flags);
    }
    canvas->DrawStringRectWithFlags(
        text_, Font(metrics.button_font_size, metrics.button_weight), text,
        GetLocalBounds(), gfx::Canvas::TEXT_ALIGN_CENTER);
    if (!GetEnabled()) {
      canvas->Restore();
    }
  }

 private:
  const std::u16string text_;
  const bool primary_;
};

BEGIN_METADATA(KoanWelcomeButton)
END_METADATA

// The green round-cornered button with an arrow on the first screen.
class KoanWelcomeStartButton : public views::Button {
  METADATA_HEADER(KoanWelcomeStartButton, views::Button)

 public:
  explicit KoanWelcomeStartButton(PressedCallback callback)
      : views::Button(std::move(callback)) {
    GetViewAccessibility().SetName(u"Start");
    MakeFocusable(this, GetMetrics().button_radius);
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kWelcomeStartButtonWidth, kWelcomeStartButtonHeight);
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(GetState() == STATE_HOVERED
                                        ? kColorKoanWelcomeStartButtonHovered
                                        : kColorKoanWelcomeStartButton));
    canvas->DrawRoundRect(GetLocalBounds(), GetMetrics().button_radius, flags);
    canvas->DrawImageInt(
        gfx::CreateVectorIcon(
            vector_icons::kArrowForwardIcon, kWelcomeStartButtonIconSize,
            colors->GetColor(kColorKoanWelcomeStartButtonIcon)),
        (width() - kWelcomeStartButtonIconSize) / 2,
        (height() - kWelcomeStartButtonIconSize) / 2);
  }
};

BEGIN_METADATA(KoanWelcomeStartButton)
END_METADATA

// "< Back" at the top of the pages' sidebar: Zen's accent text, or shadcn's
// ghost button (muted text, `accent` behind it on hover).
class KoanWelcomeBackButton : public views::Button {
  METADATA_HEADER(KoanWelcomeBackButton, views::Button)

 public:
  explicit KoanWelcomeBackButton(PressedCallback callback)
      : views::Button(std::move(callback)) {
    GetViewAccessibility().SetName(kWelcomeBack);
    MakeFocusable(this, GetMetrics().back_radius);
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(2 * Padding() + kWelcomeBackIconSize +
                         kWelcomeBackIconGap +
                         gfx::GetStringWidth(kWelcomeBack, GetFont()),
                     GetMetrics().back_height);
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const Metrics metrics = GetMetrics();
    const Palette palette = GetPalette(this);
    const bool hovered = GetState() == STATE_HOVERED;
    SkColor color = palette.accent;
    if (metrics.shadcn) {
      color = hovered ? palette.text : palette.text_secondary;
      if (hovered) {
        cc::PaintFlags flags;
        flags.setAntiAlias(true);
        flags.setColor(GetColorProvider()->GetColor(kColorKoanWelcomeHover));
        canvas->DrawRoundRect(GetLocalBounds(), metrics.back_radius, flags);
      }
    }
    canvas->DrawImageInt(gfx::CreateVectorIcon(vector_icons::kArrowBackIcon,
                                               kWelcomeBackIconSize, color),
                         Padding(), (height() - kWelcomeBackIconSize) / 2);
    gfx::Rect text = GetLocalBounds();
    text.Inset(gfx::Insets::TLBR(
        0, Padding() + kWelcomeBackIconSize + kWelcomeBackIconGap, 0, 0));
    canvas->DrawStringRect(kWelcomeBack, GetFont(), color, text);
  }

  // Where the icon starts: the ghost button pads its text.
  static int Padding() {
    return GetMetrics().shadcn ? kWelcomeShadcnBackPadding : 0;
  }

 private:
  static gfx::FontList GetFont() {
    const Metrics metrics = GetMetrics();
    return Font(metrics.back_font_size, metrics.shadcn
                                            ? gfx::Font::Weight::MEDIUM
                                            : gfx::Font::Weight::SEMIBOLD);
  }
};

BEGIN_METADATA(KoanWelcomeBackButton)
END_METADATA

// One choice of a page: a card with a radio circle and its text (e.g.
// "YES, import from another browser."), optionally with an engine's tile.
class KoanWelcomeOption : public views::Button {
  METADATA_HEADER(KoanWelcomeOption, views::Button)

 public:
  KoanWelcomeOption(PressedCallback callback,
                    std::u16string strong,
                    std::u16string text,
                    bool engine)
      : views::Button(std::move(callback)),
        strong_(std::move(strong)),
        text_(std::move(text)),
        engine_(engine) {
    GetViewAccessibility().SetName(strong_.empty() ? text_
                                                   : strong_ + u" " + text_);
    GetViewAccessibility().SetRole(ax::mojom::Role::kRadioButton);
    MakeFocusable(this, GetMetrics().option_radius);
    SetGroup(kOptionsGroup);
  }

  bool selected() const { return selected_; }
  // What typing a letter matches: "YES" / "NO", or the engine's name.
  const std::u16string& name() const {
    return strong_.empty() ? text_ : strong_;
  }
  // For the e2e tests (UI DevTools).
  bool GetSelected() const { return selected_; }
  std::u16string GetChoiceName() const { return name(); }
  void SetSelected(bool selected) {
    selected_ = selected;
    GetViewAccessibility().SetCheckedState(
        selected ? ax::mojom::CheckedState::kTrue
                 : ax::mojom::CheckedState::kFalse);
    SchedulePaint();
  }

  // The list's keys: the arrows, Enter and type-ahead (see
  // KoanWelcomeOptionsView::OnOptionKey). Returns whether it took the key.
  void set_on_key(base::RepeatingCallback<bool(KoanWelcomeOption*,
                                               const ui::KeyEvent&)> on_key) {
    on_key_ = std::move(on_key);
  }

  // views::Button:
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (on_key_ && on_key_.Run(this, event)) {
      return true;
    }
    return views::Button::OnKeyPressed(event);
  }
  // One Tab stop for the list: the chosen option.
  bool IsGroupFocusTraversable() const override { return false; }
  views::View* GetSelectedViewForGroup(int group) override {
    for (views::View* sibling : parent()->children()) {
      if (static_cast<KoanWelcomeOption*>(sibling)->selected()) {
        return sibling;
      }
    }
    return parent()->children().front();
  }
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const Metrics metrics = GetMetrics();
    return engine_ ? gfx::Size(kWelcomeEngineMaxWidth, metrics.engine_height)
                   : gfx::Size(kWelcomeOptionMaxWidth, metrics.option_height);
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const Metrics metrics = GetMetrics();
    const Palette palette = GetPalette(this);
    const ui::ColorProvider* colors = GetColorProvider();
    const float radius = metrics.option_radius;
    const gfx::RectF bounds(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    gfx::RectF card = bounds;
    card.Inset(1);
    if (metrics.shadcn) {
      // shadcn's radio card: an input's fill, `accent` on hover.
      flags.setColor(colors->GetColor(GetState() == STATE_HOVERED
                                          ? kColorKoanWelcomeHover
                                          : kColorKoanWelcomeOptionFill));
    } else {
      flags.setColor(palette.surface);
      flags.setLooper(gfx::CreateShadowDrawLooper(
          {gfx::ShadowValue(gfx::Vector2d(0, kWelcomeOptionShadowY),
                            kWelcomeOptionShadowBlur, palette.shadow)}));
    }
    canvas->DrawRoundRect(card, radius, flags);
    flags.setLooper(nullptr);

    // The outline: the accent (shadcn's `primary`) when chosen.
    StrokeRoundRect(canvas, card, radius,
                    selected_ ? palette.accent : palette.border);

    // The radio circle, filled with a dot when chosen.
    const gfx::PointF radio(metrics.option_padding + metrics.radio_size / 2.0f,
                            height() / 2.0f);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(selected_ ? palette.accent : palette.radio);
    canvas->DrawCircle(radio, metrics.radio_size / 2.0f - 0.5f, flags);
    if (selected_) {
      flags.setStyle(cc::PaintFlags::kFill_Style);
      canvas->DrawCircle(
          radio, metrics.radio_size / 2.0f - metrics.radio_dot_inset, flags);
    }

    // The text. Zen sets "YES," and "NO," in bold; shadcn sets the whole
    // label in one weight.
    const SkColor color =
        selected_ && !metrics.shadcn ? palette.accent : palette.text;
    int x =
        metrics.option_padding + metrics.radio_size +
        (metrics.shadcn ? metrics.option_padding / 2 : kWelcomeOptionTextGap);
    if (engine_) {
      // The engine's icon before its name. Chromium bundles the engines'
      // logos only in Google Chrome builds (enable_builtin_search_provider_
      // assets), so Koan shows a search glyph in a round badge.
      const int badge = kWelcomeEngineBadgeSize;
      const gfx::PointF center(x + badge / 2.0f, height() / 2.0f);
      cc::PaintFlags badge_flags;
      badge_flags.setAntiAlias(true);
      badge_flags.setColor(palette.tile);
      canvas->DrawCircle(center, badge / 2.0f, badge_flags);
      canvas->DrawImageInt(
          gfx::CreateVectorIcon(vector_icons::kSearchIcon,
                                kWelcomeEngineBadgeIconSize,
                                palette.text_secondary),
          static_cast<int>(center.x()) - kWelcomeEngineBadgeIconSize / 2,
          static_cast<int>(center.y()) - kWelcomeEngineBadgeIconSize / 2);
      x += badge + metrics.option_padding / 2;
    }
    const gfx::Font::Weight weight =
        metrics.shadcn ? gfx::Font::Weight::MEDIUM
                       : (engine_ ? gfx::Font::Weight::SEMIBOLD
                                  : gfx::Font::Weight::NORMAL);
    const gfx::FontList regular = Font(metrics.option_font_size, weight);
    if (!strong_.empty()) {
      const gfx::FontList strong_font =
          metrics.shadcn
              ? regular
              : Font(metrics.option_font_size, gfx::Font::Weight::BOLD);
      const std::u16string strong = strong_ + u", ";
      canvas->DrawStringRectWithFlags(strong, strong_font, color,
                                      gfx::Rect(x, 0, width() - x, height()),
                                      gfx::Canvas::NO_SUBPIXEL_RENDERING);
      x += gfx::GetStringWidth(strong, strong_font);
    }
    const int right = width() - metrics.option_padding;
    canvas->DrawStringRectWithFlags(text_, regular, color,
                                    gfx::Rect(x, 0, right - x, height()),
                                    gfx::Canvas::NO_SUBPIXEL_RENDERING);
  }

 private:
  const std::u16string strong_;
  const std::u16string text_;
  const bool engine_;
  bool selected_ = false;
  base::RepeatingCallback<bool(KoanWelcomeOption*, const ui::KeyEvent&)>
      on_key_;
};

BEGIN_METADATA(KoanWelcomeOption)
ADD_READONLY_PROPERTY_METADATA(bool, Selected)
ADD_READONLY_PROPERTY_METADATA(std::u16string, ChoiceName)
END_METADATA

// A column of options, centred: a radio group named after the page.
class KoanWelcomeOptionsView : public views::View {
  METADATA_HEADER(KoanWelcomeOptionsView, views::View)

 public:
  KoanWelcomeOptionsView(int gap, const std::u16string& name) : gap_(gap) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kRadioGroup);
    GetViewAccessibility().SetName(name);
  }

  // Adds an option; `on_select` runs when it gets selected.
  void AddOption(std::u16string strong,
                 std::u16string text,
                 bool selected,
                 bool engine,
                 base::RepeatingClosure on_select) {
    auto* option = AddChildView(std::make_unique<KoanWelcomeOption>(
        views::Button::PressedCallback(), std::move(strong), std::move(text),
        engine));
    on_selects_.push_back(on_select);
    option->SetCallback(base::BindRepeating(
        &KoanWelcomeOptionsView::Select, base::Unretained(this),
        base::Unretained(option), std::move(on_select)));
    option->set_on_key(base::BindRepeating(&KoanWelcomeOptionsView::OnOptionKey,
                                           base::Unretained(this)));
    option->SetSelected(selected);
    const int count = static_cast<int>(children().size());
    for (int i = 0; i < count; ++i) {
      children()[i]->GetViewAccessibility().SetPosInSet(i + 1);
      children()[i]->GetViewAccessibility().SetSetSize(count);
    }
  }

  // The selected option, or the first: where focus goes on this page.
  views::View* GetSelectedOption() {
    for (views::View* child : children()) {
      if (static_cast<KoanWelcomeOption*>(child)->selected()) {
        return child;
      }
    }
    return children().empty() ? nullptr : children().front().get();
  }

  // views::View:
  void Layout(PassKey) override {
    int total = 0;
    for (const views::View* child : children()) {
      total += child->GetPreferredSize().height() + gap_;
    }
    total -= gap_;
    int y = std::max(0, (height() - total) / 2);
    for (views::View* child : children()) {
      const gfx::Size size = child->GetPreferredSize();
      const int width =
          std::min(size.width(), this->width() - 2 * kWelcomeOptionSideMargin);
      child->SetBounds((this->width() - width) / 2, y, width, size.height());
      y += size.height() + gap_;
    }
  }

 private:
  void Select(KoanWelcomeOption* selected, base::RepeatingClosure on_select) {
    for (views::View* child : children()) {
      static_cast<KoanWelcomeOption*>(child)->SetSelected(child == selected);
    }
    on_select.Run();
  }

  // A radio group's keys on `option`: Up/Left and Down/Right move the
  // choice, Enter (and Space, as for any button) chooses, and a letter jumps
  // to the next choice starting with it ("G" for Google, "N" for NO).
  bool OnOptionKey(KoanWelcomeOption* option, const ui::KeyEvent& event) {
    switch (event.key_code()) {
      case ui::VKEY_UP:
      case ui::VKEY_LEFT:
        Move(option, -1);
        return true;
      case ui::VKEY_DOWN:
      case ui::VKEY_RIGHT:
        Move(option, 1);
        return true;
      case ui::VKEY_RETURN: {
        const std::optional<size_t> index = GetIndexOf(option);
        if (index) {
          Select(option, on_selects_[*index]);
        }
        return true;
      }
      default:
        break;
    }
    if (event.IsControlDown() || event.IsAltDown() || event.IsCommandDown()) {
      return false;
    }
    std::vector<std::u16string> names;
    for (const views::View* child : children()) {
      names.push_back(static_cast<const KoanWelcomeOption*>(child)->name());
    }
    const std::optional<size_t> from = GetIndexOf(option);
    const std::optional<size_t> match =
        FindByFirstLetter(names, from.value_or(0), event.GetCharacter());
    if (!match) {
      return false;
    }
    Move(option, static_cast<int>(*match) - static_cast<int>(*from));
    return true;
  }

  // Selects and focuses the option `delta` away from `from`.
  void Move(KoanWelcomeOption* from, int delta) {
    const std::optional<size_t> index = GetIndexOf(from);
    if (!index) {
      return;
    }
    const size_t next = static_cast<size_t>(
        std::clamp<int>(static_cast<int>(*index) + delta, 0,
                        static_cast<int>(children().size()) - 1));
    auto* option = static_cast<KoanWelcomeOption*>(children()[next].get());
    option->RequestFocus();
    Select(option, on_selects_[next]);
  }

  const int gap_;
  std::vector<base::RepeatingClosure> on_selects_;
};

BEGIN_METADATA(KoanWelcomeOptionsView)
END_METADATA

// A colour to pick on the colours page.
class KoanWelcomeSwatch : public views::Button {
  METADATA_HEADER(KoanWelcomeSwatch, views::Button)

 public:
  KoanWelcomeSwatch(PressedCallback callback, const SpaceColor& color)
      : views::Button(std::move(callback)),
        color_(color.color),
        name_(color.name) {
    GetViewAccessibility().SetName(color.name);
    GetViewAccessibility().SetRole(ax::mojom::Role::kRadioButton);
    SetFocusBehavior(FocusBehavior::ALWAYS);
    views::InstallCircleHighlightPathGenerator(this);
    SetGroup(kSwatchesGroup);
  }

  bool selected() const { return selected_; }
  const std::u16string& name() const { return name_; }
  bool GetSelected() const { return selected_; }
  std::u16string GetChoiceName() const { return name_; }
  // The colours' keys (KoanWelcomeColorsView::OnSwatchKey).
  void set_on_key(base::RepeatingCallback<bool(KoanWelcomeSwatch*,
                                               const ui::KeyEvent&)> on_key) {
    on_key_ = std::move(on_key);
  }

  // views::Button: one Tab stop for the colours, the chosen one.
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (on_key_ && on_key_.Run(this, event)) {
      return true;
    }
    return views::Button::OnKeyPressed(event);
  }
  bool IsGroupFocusTraversable() const override { return false; }
  views::View* GetSelectedViewForGroup(int group) override {
    for (views::View* sibling : parent()->children()) {
      if (static_cast<KoanWelcomeSwatch*>(sibling)->selected()) {
        return sibling;
      }
    }
    return parent()->children().front();
  }

  void SetSelected(bool selected) {
    selected_ = selected;
    GetViewAccessibility().SetCheckedState(
        selected ? ax::mojom::CheckedState::kTrue
                 : ax::mojom::CheckedState::kFalse);
    SchedulePaint();
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kWelcomeSwatchSize, kWelcomeSwatchSize);
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::PointF center(width() / 2.0f, height() / 2.0f);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color_);
    canvas->DrawCircle(center, kWelcomeSwatchSize / 2.0f - 6, flags);
    if (selected_ || GetState() == STATE_HOVERED) {
      // Zen: a ring in the colour, white when chosen. shadcn: a 2px ring in
      // `foreground` 2px off the colour when chosen, `border` on hover.
      const Palette palette = GetPalette(this);
      const bool shadcn = GetMetrics().shadcn;
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(shadcn ? 2.0f : 2.5f);
      if (shadcn) {
        flags.setColor(selected_ ? palette.text : palette.border);
      } else {
        flags.setColor(selected_ ? palette.text : SkColorSetA(color_, 0x80));
      }
      canvas->DrawCircle(center, kWelcomeSwatchSize / 2.0f - 1.5f, flags);
    }
  }

 private:
  const SkColor color_;
  const std::u16string name_;
  bool selected_ = false;
  base::RepeatingCallback<bool(KoanWelcomeSwatch*, const ui::KeyEvent&)>
      on_key_;
};

BEGIN_METADATA(KoanWelcomeSwatch)
ADD_READONLY_PROPERTY_METADATA(bool, Selected)
ADD_READONLY_PROPERTY_METADATA(std::u16string, ChoiceName)
END_METADATA

// A panel of colours, like a small colour picker.
class KoanWelcomeColorsView : public views::View {
  METADATA_HEADER(KoanWelcomeColorsView, views::View)

 public:
  explicit KoanWelcomeColorsView(base::RepeatingCallback<void(SkColor)> pick,
                                 std::optional<SkColor> selected) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kRadioGroup);
    GetViewAccessibility().SetName(u"Colors");
    int position = 0;
    for (const SpaceColor& color : kSpaceColors) {
      auto* swatch = AddChildView(std::make_unique<KoanWelcomeSwatch>(
          views::Button::PressedCallback(), color));
      swatch->SetCallback(base::BindRepeating(
          &KoanWelcomeColorsView::Pick, base::Unretained(this),
          base::Unretained(swatch), color.color, pick));
      swatch->set_on_key(base::BindRepeating(
          &KoanWelcomeColorsView::OnSwatchKey, base::Unretained(this)));
      picks_.push_back(base::BindRepeating(
          &KoanWelcomeColorsView::Pick, base::Unretained(this),
          base::Unretained(swatch), color.color, pick));
      swatch->SetSelected(selected == color.color);
      swatch->GetViewAccessibility().SetPosInSet(++position);
      swatch->GetViewAccessibility().SetSetSize(
          static_cast<int>(kSpaceColors.size()));
    }
  }

  // views::View:
  void Layout(PassKey) override {
    const gfx::Rect panel = PanelBounds();
    for (size_t i = 0; i < children().size(); ++i) {
      children()[i]->SetBounds(
          panel.x() + kWelcomePanelPadding +
              (i % kColumns) * (kWelcomeSwatchSize + kWelcomeSwatchGap),
          panel.y() + kWelcomePanelPadding +
              (i / kColumns) * (kWelcomeSwatchSize + kWelcomeSwatchGap),
          kWelcomeSwatchSize, kWelcomeSwatchSize);
    }
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const Metrics metrics = GetMetrics();
    const Palette palette = GetPalette(this);
    const gfx::RectF panel(PanelBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    // A card: Zen's raised surface, or shadcn's `card` with its border.
    flags.setColor(palette.surface);
    flags.setLooper(gfx::CreateShadowDrawLooper(
        {gfx::ShadowValue(gfx::Vector2d(0, kWelcomeOptionShadowY),
                          kWelcomeOptionShadowBlur, palette.shadow)}));
    canvas->DrawRoundRect(panel, metrics.option_radius, flags);
    StrokeRoundRect(canvas, panel, metrics.option_radius, palette.border);
  }

 private:
  gfx::Rect PanelBounds() const {
    const int size = 2 * kWelcomePanelPadding + 3 * kWelcomeSwatchSize +
                     2 * kWelcomeSwatchGap;
    return gfx::Rect((width() - size) / 2, (height() - size) / 2, size, size);
  }

  void Pick(KoanWelcomeSwatch* picked,
            SkColor color,
            base::RepeatingCallback<void(SkColor)> pick) {
    for (views::View* child : children()) {
      static_cast<KoanWelcomeSwatch*>(child)->SetSelected(child == picked);
    }
    pick.Run(color);
  }

  // The grid's keys: Left/Right one colour, Up/Down a row, Enter (and Space)
  // picks, a letter jumps to the next colour named with it.
  bool OnSwatchKey(KoanWelcomeSwatch* swatch, const ui::KeyEvent& event) {
    const std::optional<size_t> index = GetIndexOf(swatch);
    if (!index) {
      return false;
    }
    const int count = static_cast<int>(children().size());
    int target = static_cast<int>(*index);
    switch (event.key_code()) {
      case ui::VKEY_LEFT:
        target -= 1;
        break;
      case ui::VKEY_RIGHT:
        target += 1;
        break;
      case ui::VKEY_UP:
        target -= kColumns;
        break;
      case ui::VKEY_DOWN:
        target += kColumns;
        break;
      case ui::VKEY_RETURN:
        break;
      default: {
        if (event.IsControlDown() || event.IsAltDown() ||
            event.IsCommandDown()) {
          return false;
        }
        std::vector<std::u16string> names;
        for (const views::View* child : children()) {
          names.push_back(static_cast<const KoanWelcomeSwatch*>(child)->name());
        }
        const std::optional<size_t> match =
            FindByFirstLetter(names, *index, event.GetCharacter());
        if (!match) {
          return false;
        }
        target = static_cast<int>(*match);
      }
    }
    if (target < 0 || target >= count) {
      return true;
    }
    children()[static_cast<size_t>(target)]->RequestFocus();
    picks_[static_cast<size_t>(target)].Run();
    return true;
  }

  static constexpr int kColumns = 3;
  std::vector<base::RepeatingClosure> picks_;
};

BEGIN_METADATA(KoanWelcomeColorsView)
END_METADATA

// An app on the Essentials page; pressing toggles it.
class KoanWelcomeEssentialTile : public views::Button {
  METADATA_HEADER(KoanWelcomeEssentialTile, views::Button)

 public:
  KoanWelcomeEssentialTile(PressedCallback callback,
                           const WelcomeEssentialApp& app,
                           size_t index)
      : views::Button(std::move(callback)),
        app_(app),
        icon_(GetEssentialIcon(index, /*dark=*/false)),
        dark_icon_(GetEssentialIcon(index, /*dark=*/true)) {
    GetViewAccessibility().SetName(app.name);
    SetTooltipText(app.name);
    GetViewAccessibility().SetRole(ax::mojom::Role::kToggleButton);
    MakeFocusable(this, GetMetrics().tile_radius);
    SetGroup(kTilesGroup);
  }

  bool selected() const { return selected_; }
  std::u16string name() const { return app_->name; }
  bool GetSelected() const { return selected_; }
  std::u16string GetChoiceName() const { return name(); }
  // The grid's keys (KoanWelcomeMockBrowserView::OnTileKey).
  void set_on_key(base::RepeatingCallback<bool(KoanWelcomeEssentialTile*,
                                               const ui::KeyEvent&)> on_key) {
    on_key_ = std::move(on_key);
  }
  // The tile the grid's Tab stop is on: the last one focused.
  void set_roving(bool roving) { roving_ = roving; }
  bool roving() const { return roving_; }

  // views::Button: one Tab stop for the grid.
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (on_key_ && on_key_.Run(this, event)) {
      return true;
    }
    return views::Button::OnKeyPressed(event);
  }
  bool IsGroupFocusTraversable() const override { return false; }
  views::View* GetSelectedViewForGroup(int group) override {
    for (views::View* sibling : parent()->children()) {
      if (static_cast<KoanWelcomeEssentialTile*>(sibling)->roving()) {
        return sibling;
      }
    }
    return parent()->children().front();
  }
  void SetSelected(bool selected) {
    selected_ = selected;
    GetViewAccessibility().SetCheckedState(
        selected ? ax::mojom::CheckedState::kTrue
                 : ax::mojom::CheckedState::kFalse);
    SchedulePaint();
  }

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const Metrics metrics = GetMetrics();
    const Palette palette = GetPalette(this);
    const gfx::RectF tile(GetLocalBounds());
    const float radius = std::min(metrics.tile_radius, width() / 3);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (metrics.shadcn) {
      // shadcn's toggle card: `muted`, outlined; `primary` outline when on.
      flags.setColor(GetState() == STATE_HOVERED
                         ? GetColorProvider()->GetColor(kColorKoanWelcomeHover)
                         : palette.tile);
      canvas->DrawRoundRect(tile, radius, flags);
      StrokeRoundRect(canvas, tile, radius,
                      selected_ ? palette.accent : palette.border,
                      selected_ ? 2 : 1);
    } else {
      flags.setColor(selected_ ? color_utils::AlphaBlend(app_->color,
                                                         palette.surface, 0.22f)
                               : palette.tile);
      canvas->DrawRoundRect(tile, radius, flags);
      if (selected_) {
        StrokeRoundRect(canvas, tile, radius, SkColorSetA(app_->color, 0x99),
                        kWelcomeTileBorder);
      }
    }
    // The app's icon (Zen's favicon), its name below.
    const gfx::ImageSkia& icon =
        color_utils::IsDark(palette.tile) ? dark_icon_ : icon_;
    const int icon_size = std::max(16, width() * 36 / 100);
    if (!icon.isNull()) {
      canvas->DrawImageInt(icon, 0, 0, icon.width(), icon.height(),
                           (width() - icon_size) / 2,
                           height() * 3 / 8 - icon_size / 2, icon_size,
                           icon_size, /*filter=*/true);
    }
    gfx::Rect name = GetLocalBounds();
    name.Inset(gfx::Insets::TLBR(height() * 3 / 5, 4, 0, 4));
    canvas->DrawStringRectWithFlags(
        app_->name, Font(kWelcomeTileNameFontSize, gfx::Font::Weight::MEDIUM),
        selected_ && metrics.shadcn ? palette.text : palette.text_secondary,
        name, gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  const raw_ref<const WelcomeEssentialApp> app_;
  const gfx::ImageSkia icon_;
  const gfx::ImageSkia dark_icon_;
  bool selected_ = false;
  bool roving_ = false;
  base::RepeatingCallback<bool(KoanWelcomeEssentialTile*, const ui::KeyEvent&)>
      on_key_;
};

BEGIN_METADATA(KoanWelcomeEssentialTile)
ADD_READONLY_PROPERTY_METADATA(bool, Selected)
ADD_READONLY_PROPERTY_METADATA(std::u16string, ChoiceName)
END_METADATA

// The Essentials page: a mock browser whose sidebar holds the apps, at the
// bottom right of the page.
class KoanWelcomeMockBrowserView : public views::View {
  METADATA_HEADER(KoanWelcomeMockBrowserView, views::View)

 public:
  explicit KoanWelcomeMockBrowserView(std::set<size_t>* selected) {
    for (size_t i = 0; i < std::size(kWelcomeEssentialApps); ++i) {
      auto* tile = AddChildView(std::make_unique<KoanWelcomeEssentialTile>(
          views::Button::PressedCallback(), kWelcomeEssentialApps[i], i));
      tile->set_on_key(base::BindRepeating(
          &KoanWelcomeMockBrowserView::OnTileKey, base::Unretained(this)));
      tile->set_roving(i == 0);
      toggles_.push_back(base::BindRepeating(
          [](KoanWelcomeEssentialTile* tile, std::set<size_t>* selected,
             size_t index) {
            tile->SetSelected(!tile->selected());
            if (tile->selected()) {
              selected->insert(index);
            } else {
              selected->erase(index);
            }
          },
          base::Unretained(tile), base::Unretained(selected), i));
      tile->SetCallback(toggles_.back());
      tile->SetSelected(selected->contains(i));
    }
  }

  // The grid's keys: the arrows move between apps (three to a row), Enter
  // (and Space) adds or removes the focused one, a letter jumps to the next
  // app named with it.
  bool OnTileKey(KoanWelcomeEssentialTile* tile, const ui::KeyEvent& event) {
    const std::optional<size_t> index = GetIndexOf(tile);
    if (!index) {
      return false;
    }
    const int count = static_cast<int>(children().size());
    int target = static_cast<int>(*index);
    switch (event.key_code()) {
      case ui::VKEY_LEFT:
        target -= 1;
        break;
      case ui::VKEY_RIGHT:
        target += 1;
        break;
      case ui::VKEY_UP:
        target -= kColumns;
        break;
      case ui::VKEY_DOWN:
        target += kColumns;
        break;
      case ui::VKEY_RETURN:
        toggles_[*index].Run();
        return true;
      default: {
        if (event.IsControlDown() || event.IsAltDown() ||
            event.IsCommandDown()) {
          return false;
        }
        std::vector<std::u16string> names;
        for (const views::View* child : children()) {
          names.push_back(
              static_cast<const KoanWelcomeEssentialTile*>(child)->name());
        }
        const std::optional<size_t> match =
            FindByFirstLetter(names, *index, event.GetCharacter());
        if (!match) {
          return false;
        }
        target = static_cast<int>(*match);
      }
    }
    if (target < 0 || target >= count) {
      return true;
    }
    for (views::View* child : children()) {
      static_cast<KoanWelcomeEssentialTile*>(child)->set_roving(false);
    }
    auto* next = static_cast<KoanWelcomeEssentialTile*>(
        children()[static_cast<size_t>(target)].get());
    next->set_roving(true);
    next->RequestFocus();
    return true;
  }

  // views::View:
  void Layout(PassKey) override {
    // The whole window fits the pane (Zen lets it run off the bottom): the
    // tiles are as big as the width allows, or smaller to fit the height;
    // the two mock tabs below them go first when there is no room.
    gfx::Rect area = GetLocalBounds();
    const int margin =
        std::min(kWelcomeMockPaddingTop, std::min(width(), height()) / 12);
    area.Inset(margin);
    window_ = area;
    sidebar_ = window_;
    sidebar_.set_width(
        static_cast<int>(window_.width() * kWelcomeMockSidebarWidth));
    const gfx::Insets insets = Insets(sidebar_.size());
    const int inner_width = sidebar_.width() - insets.width();
    const int header =
        kWelcomeMockDotSize + kWelcomeMockDotsMargin + kWelcomeMockGap;
    const int room = area.height() - insets.height() - header;
    const auto fit = [&](int tabs, int& tile, int& gap, int& tab_height) {
      tile = inner_width / kColumns;
      for (; tile > 0; --tile) {
        gap = std::min(kWelcomeMockGridGap, tile / 4);
        const int grid_width = kColumns * tile + (kColumns - 1) * gap;
        tab_height = std::min(kWelcomeMockTabHeight, tile * 3 / 5);
        const int rows =
            (static_cast<int>(children().size()) + kColumns - 1) / kColumns;
        const int grid_height = rows * tile + (rows - 1) * gap;
        const int needed = grid_height + tabs * (tab_height + kWelcomeMockGap);
        if (grid_width <= inner_width && needed <= room) {
          return;
        }
      }
    };
    tabs_ = 2;
    fit(tabs_, tile_, gap_, tab_height_);
    if (tile_ < kWelcomeMockMinTileWithTabs) {
      tabs_ = 0;
      fit(tabs_, tile_, gap_, tab_height_);
    }
    const int rows =
        (static_cast<int>(children().size()) + kColumns - 1) / kColumns;
    const int content_height = header + rows * tile_ + (rows - 1) * gap_ +
                               tabs_ * (tab_height_ + kWelcomeMockGap);
    // The window as tall as its content, centred in the pane.
    window_.set_height(
        std::min(area.height(), content_height + insets.height()));
    window_.set_y(area.y() + (area.height() - window_.height()) / 2);
    sidebar_.set_y(window_.y());
    sidebar_.set_height(window_.height());
    const int grid_x =
        sidebar_.x() + insets.left() +
        (inner_width - (kColumns * tile_ + (kColumns - 1) * gap_)) / 2;
    const int grid_y = sidebar_.y() + insets.top() + header;
    for (size_t i = 0; i < children().size(); ++i) {
      children()[i]->SetBounds(
          grid_x + static_cast<int>(i % kColumns) * (tile_ + gap_),
          grid_y + static_cast<int>(i / kColumns) * (tile_ + gap_), tile_,
          tile_);
    }
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const Palette palette = GetPalette(this);
    const float corner =
        std::min<float>(GetMetrics().mock_radius, window_.height() / 4.0f);
    const SkRRect rounded =
        SkRRect::MakeRectXY(gfx::RectToSkRect(window_), corner, corner);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    canvas->sk_canvas()->save();
    canvas->sk_canvas()->clipRRect(rounded, true);
    flags.setColor(palette.mock);
    canvas->DrawRect(window_, flags);
    flags.setColor(palette.surface);
    canvas->DrawRect(sidebar_, flags);
    // Three window dots, and the mock tabs under the grid.
    const gfx::Insets insets = Insets(sidebar_.size());
    flags.setColor(palette.tile);
    for (int i = 0; i < 3; ++i) {
      canvas->DrawCircle(
          gfx::PointF(sidebar_.x() + insets.left() +
                          kWelcomeMockDotSize / 2.0f +
                          i * (kWelcomeMockDotSize + kWelcomeMockDotGap),
                      sidebar_.y() + insets.top() + kWelcomeMockDotSize / 2.0f),
          kWelcomeMockDotSize / 2.0f, flags);
    }
    int y = children().empty()
                ? sidebar_.y()
                : children().back()->bounds().bottom() + kWelcomeMockGap;
    for (int i = 0; i < tabs_; ++i) {
      canvas->DrawRoundRect(
          gfx::Rect(sidebar_.x() + insets.left(), y,
                    sidebar_.width() - insets.width(), tab_height_),
          std::min(kWelcomeMockTabCornerRadius, tab_height_ / 2), flags);
      y += tab_height_ + kWelcomeMockGap;
    }
    canvas->sk_canvas()->restore();
    // The window's outline, all the way round.
    cc::PaintFlags line;
    line.setAntiAlias(true);
    line.setStyle(cc::PaintFlags::kStroke_Style);
    line.setStrokeWidth(1);
    line.setColor(palette.border);
    SkRRect outline = rounded;
    outline.inset(0.5f, 0.5f);
    canvas->sk_canvas()->drawRRect(outline, line);
  }

 private:
  static constexpr int kColumns = 3;

  // Zen's padding of the mock sidebar, less in a small one.
  static gfx::Insets Insets(const gfx::Size& sidebar) {
    return gfx::Insets::VH(
        std::min(kWelcomeMockSidebarPaddingVertical, sidebar.height() / 12),
        std::min(kWelcomeMockSidebarPaddingHorizontal, sidebar.width() / 10));
  }

  std::vector<base::RepeatingClosure> toggles_;
  // Set by Layout().
  gfx::Rect window_;
  gfx::Rect sidebar_;
  int tile_ = 0;
  int gap_ = 0;
  int tabs_ = 0;
  int tab_height_ = 0;
};

BEGIN_METADATA(KoanWelcomeMockBrowserView)
END_METADATA

// The last page's content: what the user chose on the pages before, as rows
// in a card (Zen shows nothing there).
class KoanWelcomeSummaryView : public views::View {
  METADATA_HEADER(KoanWelcomeSummaryView, views::View)

 public:
  using Rows = std::vector<std::pair<std::u16string, std::u16string>>;

  explicit KoanWelcomeSummaryView(Rows rows) : rows_(std::move(rows)) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    std::u16string description;
    for (const auto& [label, value] : rows_) {
      description += label + u": " + value + u". ";
    }
    GetViewAccessibility().SetName(u"Your choices");
    GetViewAccessibility().SetDescription(description);
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const Metrics metrics = GetMetrics();
    const Palette palette = GetPalette(this);
    const gfx::RectF card(CardBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(palette.surface);
    flags.setLooper(gfx::CreateShadowDrawLooper(
        {gfx::ShadowValue(gfx::Vector2d(0, kWelcomeOptionShadowY),
                          kWelcomeOptionShadowBlur, palette.shadow)}));
    canvas->DrawRoundRect(card, metrics.option_radius, flags);
    flags.setLooper(nullptr);
    StrokeRoundRect(canvas, card, metrics.option_radius, palette.border);

    const gfx::FontList label_font =
        Font(kWelcomeSummaryFontSize, gfx::Font::Weight::NORMAL);
    const gfx::FontList value_font =
        Font(kWelcomeSummaryFontSize, gfx::Font::Weight::MEDIUM);
    gfx::Rect row = gfx::ToEnclosingRect(card);
    row.Inset(gfx::Insets::VH(0, kWelcomeSummaryPadding));
    row.set_height(kWelcomeSummaryRowHeight);
    for (size_t i = 0; i < rows_.size(); ++i) {
      if (i > 0) {
        flags.setColor(palette.border);
        canvas->DrawRect(gfx::RectF(row.x(), row.y() - 0.5f, row.width(), 1),
                         flags);
      }
      const int label_width = row.width() * 2 / 5;
      canvas->DrawStringRectWithFlags(
          rows_[i].first, label_font, palette.text_secondary,
          gfx::Rect(row.x(), row.y(), label_width, row.height()),
          gfx::Canvas::NO_SUBPIXEL_RENDERING);
      canvas->DrawStringRectWithFlags(
          rows_[i].second, value_font, palette.text,
          gfx::Rect(row.x() + label_width, row.y(), row.width() - label_width,
                    row.height()),
          gfx::Canvas::TEXT_ALIGN_RIGHT | gfx::Canvas::NO_SUBPIXEL_RENDERING);
      row.Offset(0, kWelcomeSummaryRowHeight);
    }
  }

 private:
  gfx::Rect CardBounds() const {
    const int card_width =
        std::min(kWelcomeSummaryMaxWidth, width() - kWelcomeOptionSideMargin);
    const int card_height =
        static_cast<int>(rows_.size()) * kWelcomeSummaryRowHeight;
    return gfx::Rect((width() - card_width) / 2, (height() - card_height) / 2,
                     card_width, card_height);
  }

  const Rows rows_;
};

BEGIN_METADATA(KoanWelcomeSummaryView)
END_METADATA

// The pages: a rounded card with a sidebar (back, the page's title and
// description, its buttons) and the page's content beside it.
class KoanWelcomePagesView : public views::View {
  METADATA_HEADER(KoanWelcomePagesView, views::View)

 public:
  explicit KoanWelcomePagesView(KoanWelcome* welcome) : welcome_(welcome) {
    back_ = AddChildView(
        std::make_unique<KoanWelcomeBackButton>(base::BindRepeating(
            &KoanWelcomePagesView::PostShowPage, base::Unretained(this), -1)));
    buttons_ = AddChildView(std::make_unique<views::View>());
    Profile* profile = welcome_->browser_view()->GetProfile();
    if (TemplateURLService* service =
            TemplateURLServiceFactory::GetForProfile(profile)) {
      service->Load();
    }
  }

  void Start() { ShowPage(0, 1); }

  // views::View:
  void Layout(PassKey) override {
    const Metrics metrics = GetMetrics();
    const gfx::Rect sidebar = SidebarBounds();
    gfx::Rect inner = sidebar;
    inner.Inset(metrics.sidebar_padding);
    // The ghost back button's text lines up with the title.
    back_->SetBoundsRect(gfx::Rect(
        gfx::Point(inner.x() - KoanWelcomeBackButton::Padding(), inner.y()),
        back_->GetPreferredSize()));

    int buttons_height = 0;
    for (const views::View* button : buttons_->children()) {
      buttons_height +=
          button->GetPreferredSize().height() + metrics.button_gap;
    }
    buttons_height = std::max(0, buttons_height - metrics.button_gap);
    buttons_->SetBounds(inner.x(), inner.bottom() - buttons_height,
                        inner.width(), buttons_height);
    int y = 0;
    for (views::View* button : buttons_->children()) {
      button->SetBounds(0, y, inner.width(), metrics.button_height);
      y += metrics.button_height + metrics.button_gap;
    }

    // The title (Zen's 3rem, shadcn's text-4xl) and then the description get
    // smaller in a small window, so that the text stays clear of the
    // buttons.
    y = back_->bounds().bottom() + HeadingGap(metrics);
    const int room = buttons_->y() - metrics.button_gap - y;
    if (!text_.empty()) {
      int heading = metrics.heading_size;
      int description = metrics.description_size;
      while (true) {
        SetTextSizes(metrics, heading, description);
        if (TextHeight(inner.width()) <= room) {
          break;
        }
        if (heading > metrics.heading_min_size) {
          heading = std::max(metrics.heading_min_size, heading - 2);
        } else if (description > metrics.description_min_size) {
          --description;
        } else {
          break;
        }
      }
    }
    for (size_t i = 0; i < text_.size(); ++i) {
      const int height = text_[i]->GetHeightForWidth(inner.width());
      text_[i]->SetBounds(inner.x(), y, inner.width(), height);
      y += height + (i == 0 ? DescriptionGap(metrics) : kWelcomeDescriptionGap);
    }

    if (content_) {
      content_->SetBoundsRect(ContentBounds());
    }
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const Metrics metrics = GetMetrics();
    const Palette palette = GetPalette(this);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (!metrics.shadcn) {
      // Zen: the content's colour, the sidebar raised on it.
      canvas->DrawColor(palette.content);
      flags.setColor(palette.sidebar);
      flags.setLooper(gfx::CreateShadowDrawLooper(
          {gfx::ShadowValue(gfx::Vector2d(kWelcomeSidebarShadowX, 0),
                            kWelcomeSidebarShadowBlur, palette.shadow)}));
      canvas->DrawRect(SidebarBounds(), flags);
      return;
    }
    // shadcn: one `card` with its border; the page's content in a preview
    // pane on `background`, dotted so that a page with little in it isn't a
    // flat panel.
    const gfx::RectF card(GetLocalBounds());
    canvas->DrawColor(palette.surface);
    const gfx::RectF pane(ContentBounds());
    const float pane_radius =
        std::max(0, metrics.card_radius - kWelcomeShadcnPreviewInset);
    flags.setColor(palette.content);
    canvas->DrawRoundRect(pane, pane_radius, flags);
    canvas->Save();
    canvas->sk_canvas()->clipRRect(
        SkRRect::MakeRectXY(gfx::RectFToSkRect(pane), pane_radius, pane_radius),
        true);
    flags.setColor(palette.border);
    for (float dot_y = pane.y() + kWelcomeShadcnPreviewDotSpacing / 2.0f;
         dot_y < pane.bottom(); dot_y += kWelcomeShadcnPreviewDotSpacing) {
      for (float dot_x = pane.x() + kWelcomeShadcnPreviewDotSpacing / 2.0f;
           dot_x < pane.right(); dot_x += kWelcomeShadcnPreviewDotSpacing) {
        canvas->DrawCircle(gfx::PointF(dot_x, dot_y), 1.0f, flags);
      }
    }
    canvas->Restore();
    StrokeRoundRect(canvas, pane, pane_radius, palette.border);
    StrokeRoundRect(canvas, card, metrics.card_radius, palette.border);
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    UpdateTextColors();
  }

 private:
  gfx::Rect SidebarBounds() const {
    return gfx::Rect(0, 0, static_cast<int>(width() * kWelcomeSidebarWidth),
                     height());
  }

  // Where the page's content goes: the rest of the card, or (shadcn) the
  // preview pane inset in it.
  gfx::Rect ContentBounds() const {
    gfx::Rect content(SidebarBounds().right(), 0,
                      width() - SidebarBounds().right(), height());
    if (GetMetrics().shadcn) {
      content.Inset(gfx::Insets::TLBR(kWelcomeShadcnPreviewInset, 0,
                                      kWelcomeShadcnPreviewInset,
                                      kWelcomeShadcnPreviewInset));
    }
    return content;
  }

  static int HeadingGap(const Metrics& metrics) {
    return metrics.shadcn ? 2 * kWelcomeShadcnButtonGap : kWelcomeBackGap;
  }
  static int DescriptionGap(const Metrics& metrics) {
    return metrics.shadcn ? kWelcomeShadcnButtonGap : kWelcomeHeadingGap;
  }

  void SetTextSizes(const Metrics& metrics, int heading, int description) {
    text_[0]->SetFontList(Font(heading, metrics.heading_weight));
    if (text_.size() > 1) {
      text_[1]->SetFontList(Font(description, gfx::Font::Weight::NORMAL));
      text_[1]->SetLineHeight(
          static_cast<int>(description * metrics.description_line_height));
    }
  }

  // The title and description's height at `width`, with the gap between
  // them.
  int TextHeight(int width) const {
    int height = 0;
    for (size_t i = 0; i < text_.size(); ++i) {
      height += text_[i]->GetHeightForWidth(width) +
                (i == 0 ? DescriptionGap(GetMetrics()) : 0);
    }
    return height;
  }

  void UpdateTextColors() {
    if (!GetColorProvider()) {
      return;
    }
    const Palette palette = GetPalette(this);
    for (size_t i = 0; i < text_.size(); ++i) {
      text_[i]->SetEnabledColor(i == 0 ? palette.text : palette.text_secondary);
    }
  }

  // Page changes destroy the button that asked for them: run them after the
  // click.
  void PostShowPage(int delta) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanWelcomePagesView::Go,
                                  weak_factory_.GetWeakPtr(), delta));
  }

  void Go(int delta) {
    if (delta > 0) {
      Commit();
    }
    const int page = page_ + delta;
    if (page < 0) {
      return;
    }
    if (page >= kWelcomePageCount) {
      welcome_->Finish(choices_);
      return;
    }
    ShowPage(page, delta > 0 ? 1 : -1);
  }

  // Leaving the current page with Next.
  void Commit() {
    if (page_ == kWelcomePageDefaultBrowser && default_browser_) {
      base::MakeRefCounted<shell_integration::DefaultBrowserWorker>()
          ->StartSetAsDefault(base::DoNothing());
    }
    if (page_ == kWelcomePageEssentials) {
      choices_.essentials.clear();
      for (size_t index : essentials_) {
        choices_.essentials.emplace_back(kWelcomeEssentialApps[index].url);
      }
    }
  }

  void ShowPage(int page, int direction) {
    page_ = page;
    for (views::Label* label : text_) {
      RemoveChildViewT(label);
    }
    text_.clear();
    if (views::View* content = content_) {
      content_ = nullptr;
      RemoveChildViewT(content);
    }
    buttons_->RemoveAllChildViews();

    const WelcomePageText& text = kWelcomePageText[page];
    for (const char16_t* string : {text.title, text.description}) {
      auto* label = AddChildView(std::make_unique<views::Label>(string));
      label->SetMultiLine(true);
      label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      label->SetAutoColorReadabilityEnabled(false);
      label->SetSubpixelRenderingEnabled(false);
      text_.push_back(label);
    }
    const Metrics metrics = GetMetrics();
    SetTextSizes(metrics, metrics.heading_size, metrics.description_size);
    UpdateTextColors();

    if (page == kWelcomePageFinish) {
      AddButton(kWelcomeDiveIn, /*primary=*/true, 1);
    } else {
      AddButton(kWelcomeNext, /*primary=*/true, 1);
    }
    if (page == kWelcomePageEssentials) {
      // Zen's skip clears the selection, then goes on.
      AddButton(kWelcomeSkip, /*primary=*/false, 1)
          ->SetCallback(base::BindRepeating(
              [](KoanWelcomePagesView* view) {
                view->essentials_.clear();
                view->PostShowPage(1);
              },
              base::Unretained(this)));
    }

    content_ = AddChildView(CreateContent(page));
    // Tab goes from Back to the page's choices, then to its buttons.
    content_->InsertBeforeInFocusList(buttons_.get());
    InvalidateLayout();

    for (size_t i = 0; i < text_.size(); ++i) {
      AnimateIn(text_[i], gfx::Vector2dF(metrics.slide * direction, 0),
                kTextStagger * i,
                metrics.shadcn ? metrics.fade : kTextDuration);
    }
    AnimateIn(content_,
              gfx::Vector2dF(0, metrics.shadcn ? metrics.rise / 2 : 0),
              base::TimeDelta(), metrics.fade);

    // The keyboard stays on the screen: on the page's primary button, with
    // the new page read out. Back hides only after that: hiding it while
    // focused would pass the focus on, out of the screen into the page.
    buttons_->children().front()->RequestFocus();
    back_->SetVisible(page > 0);
    GetViewAccessibility().AnnouncePolitely(
        base::StrCat({text.title, u" ", text.description}));
  }

  KoanWelcomeButton* AddButton(std::u16string text, bool primary, int delta) {
    return buttons_->AddChildView(std::make_unique<KoanWelcomeButton>(
        base::BindRepeating(&KoanWelcomePagesView::PostShowPage,
                            base::Unretained(this), delta),
        std::move(text), primary));
  }

  std::unique_ptr<views::View> CreateContent(int page) {
    switch (page) {
      case kWelcomePageImport: {
        auto options = std::make_unique<KoanWelcomeOptionsView>(
            GetMetrics().option_gap, kWelcomePageText[page].title);
        options->AddOption(kWelcomeYes, kWelcomeImportYes, choices_.import,
                           false,
                           base::BindRepeating(&KoanWelcomePagesView::SetImport,
                                               base::Unretained(this), true));
        options->AddOption(kWelcomeNo, kWelcomeImportNo, !choices_.import,
                           false,
                           base::BindRepeating(&KoanWelcomePagesView::SetImport,
                                               base::Unretained(this), false));
        return options;
      }
      case kWelcomePageColors:
        return std::make_unique<KoanWelcomeColorsView>(
            base::BindRepeating(&KoanWelcomePagesView::SetColor,
                                base::Unretained(this)),
            color_);
      case kWelcomePageSearch:
        return CreateSearchContent();
      case kWelcomePageEssentials:
        return std::make_unique<KoanWelcomeMockBrowserView>(&essentials_);
      case kWelcomePageDefaultBrowser: {
        auto options = std::make_unique<KoanWelcomeOptionsView>(
            GetMetrics().option_gap, kWelcomePageText[page].title);
        options->AddOption(
            kWelcomeYes, kWelcomeDefaultBrowserYes, default_browser_, false,
            base::BindRepeating(&KoanWelcomePagesView::SetDefaultBrowser,
                                base::Unretained(this), true));
        options->AddOption(
            kWelcomeNo, kWelcomeDefaultBrowserNo, !default_browser_, false,
            base::BindRepeating(&KoanWelcomePagesView::SetDefaultBrowser,
                                base::Unretained(this), false));
        return options;
      }
      case kWelcomePageFinish:
        return std::make_unique<KoanWelcomeSummaryView>(SummaryRows());
      default:
        return std::make_unique<views::View>();
    }
  }

  // What the user chose, for the last page.
  KoanWelcomeSummaryView::Rows SummaryRows() const {
    KoanWelcomeSummaryView::Rows rows;
    rows.emplace_back(
        u"Import", choices_.import ? u"From another browser" : u"Fresh start");
    std::u16string color = u"Your theme";
    for (const SpaceColor& space_color : kSpaceColors) {
      if (color_ == space_color.color) {
        color = space_color.name;
      }
    }
    rows.emplace_back(u"Color", color);
    if (const TemplateURLService* service =
            TemplateURLServiceFactory::GetForProfile(
                welcome_->browser_view()->GetProfile())) {
      if (const TemplateURL* engine = service->GetDefaultSearchProvider()) {
        rows.emplace_back(u"Search engine", engine->short_name());
      }
    }
    std::u16string essentials;
    for (size_t index : essentials_) {
      essentials += (essentials.empty() ? u"" : u", ") +
                    std::u16string(kWelcomeEssentialApps[index].name);
    }
    rows.emplace_back(u"Essentials", essentials.empty() ? u"None" : essentials);
    rows.emplace_back(u"Default browser",
                      default_browser_ ? u"Koan" : u"Not now");
    return rows;
  }

  std::unique_ptr<views::View> CreateSearchContent() {
    auto options = std::make_unique<KoanWelcomeOptionsView>(
        GetMetrics().engine_gap, kWelcomePageText[kWelcomePageSearch].title);
    TemplateURLService* service = TemplateURLServiceFactory::GetForProfile(
        welcome_->browser_view()->GetProfile());
    if (!service) {
      return options;
    }
    const TemplateURL* default_engine = service->GetDefaultSearchProvider();
    size_t count = 0;
    for (const TemplateURL* engine : service->GetTemplateURLs()) {
      const std::u16string name = base::ToLowerASCII(engine->short_name());
      if (engine->prepopulate_id() <= 0 ||
          std::ranges::any_of(kWelcomeExcludedEngines,
                              [&name](const char16_t* excluded) {
                                return name.find(excluded) !=
                                       std::u16string::npos;
                              }) ||
          count++ >= kMaxEngines) {
        continue;
      }
      options->AddOption(
          std::u16string(), engine->short_name(), engine == default_engine,
          /*engine=*/true,
          base::BindRepeating(
              [](TemplateURLService* service, int prepopulate_id) {
                service->SetUserSelectedDefaultSearchProviderByPrepopulateId(
                    prepopulate_id);
              },
              base::Unretained(service), engine->prepopulate_id()));
    }
    return options;
  }

  void SetImport(bool import) { choices_.import = import; }
  void SetDefaultBrowser(bool set) { default_browser_ = set; }
  void SetColor(SkColor color) {
    color_ = color;
    if (ThemeService* theme = ThemeServiceFactory::GetForProfile(
            welcome_->browser_view()->GetProfile())) {
      theme->SetUserColorAndBrowserColorVariant(
          color, ui::mojom::BrowserColorVariant::kTonalSpot);
    }
  }

  const raw_ptr<KoanWelcome> welcome_;
  int page_ = -1;
  raw_ptr<KoanWelcomeBackButton> back_;
  raw_ptr<views::View> buttons_;
  std::vector<views::Label*> text_;
  raw_ptr<views::View> content_ = nullptr;

  KoanWelcomeChoices choices_;
  bool default_browser_ = false;
  std::optional<SkColor> color_;
  std::set<size_t> essentials_;

  base::WeakPtrFactory<KoanWelcomePagesView> weak_factory_{this};
};

BEGIN_METADATA(KoanWelcomePagesView)
END_METADATA

// The whole screen: a calm gradient, the title spelled out character by
// character with the start button, then the pages.
class KoanWelcomeView : public views::View {
  METADATA_HEADER(KoanWelcomeView, views::View)

 public:
  explicit KoanWelcomeView(KoanWelcome* welcome) : welcome_(welcome) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
    GetViewAccessibility().SetName(
        base::StrCat({kWelcomeTitleLine1, u" ", kWelcomeTitleLine2}));
    GetViewAccessibility().SetIsModal(true);
    // Esc skips the rest of the welcome.
    AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
    // Zen spells the title out character by character. shadcn's title is
    // set whole, so that Geist keeps its kerning, and each line fades up.
    const bool whole_lines = GetMetrics().shadcn;
    for (const char16_t* line : {kWelcomeTitleLine1, kWelcomeTitleLine2}) {
      std::vector<views::Label*> chars;
      std::vector<std::u16string> parts;
      if (whole_lines) {
        parts.emplace_back(line);
      } else {
        for (char16_t c : std::u16string(line)) {
          parts.emplace_back(1, c);
        }
      }
      for (const std::u16string& part : parts) {
        auto* label = AddChildView(std::make_unique<views::Label>(part));
        label->SetAutoColorReadabilityEnabled(false);
        label->SetSubpixelRenderingEnabled(false);
        label->SetEnabledColor(kColorKoanWelcomeTitle);
        label->GetViewAccessibility().SetIsIgnored(true);
        label->SetPaintToLayer();
        label->layer()->SetFillsBoundsOpaquely(false);
        label->layer()->SetOpacity(0);
        chars.push_back(label);
      }
      lines_.push_back(std::move(chars));
    }
    start_ = AddChildView(
        std::make_unique<KoanWelcomeStartButton>(base::BindRepeating(
            &KoanWelcomeView::OnStart, base::Unretained(this))));
    start_->SetPaintToLayer();
    start_->layer()->SetFillsBoundsOpaquely(false);
    start_->layer()->SetOpacity(0);
    pages_ = AddChildView(std::make_unique<KoanWelcomePagesView>(welcome));
    pages_->SetVisible(false);
  }

  // Takes the keyboard: the start button (it works before it shows, to skip
  // the intro).
  void FocusStart() { start_->RequestFocus(); }

  void PlayIntro() {
    if (!ShouldAnimate()) {
      for (const auto& line : lines_) {
        for (views::Label* label : line) {
          label->layer()->SetOpacity(1);
        }
      }
      start_->layer()->SetOpacity(1);
      return;
    }
    // Characters (shadcn: lines) rise in one after another; then the
    // button.
    const Metrics metrics = GetMetrics();
    const base::TimeDelta start =
        metrics.shadcn ? base::Milliseconds(150) : kCharStartDelay;
    const base::TimeDelta stagger =
        metrics.shadcn ? base::Milliseconds(90) : kCharStagger;
    const base::TimeDelta duration =
        metrics.shadcn ? 2 * metrics.fade : kCharDuration;
    views::AnimationBuilder builder;
    builder.OnEnded(base::BindOnce(&KoanWelcomeView::ShowStartButton,
                                   weak_factory_.GetWeakPtr()));
    views::AnimationSequenceBlock* block = &builder.Once();
    int index = 0;
    for (const auto& line : lines_) {
      for (views::Label* label : line) {
        gfx::Transform below;
        below.Translate(0, metrics.rise);
        label->layer()->SetTransform(below);
        block = &block->At(start + stagger * index++);
        block->SetDuration(duration)
            .SetOpacity(label, 1, gfx::Tween::EASE_OUT)
            .SetTransform(label, gfx::Transform(), gfx::Tween::EASE_OUT_3);
      }
    }
  }

  // views::View:
  void Layout(PassKey) override {
    const Metrics metrics = GetMetrics();
    const int size =
        std::clamp(width() / 12, kWelcomeTitleMinSize, kWelcomeTitleMaxSize);
    const gfx::FontList font = Font(size, gfx::Font::Weight::BOLD);
    const int line_height = size * kWelcomeTitleLineHeight;
    // Zen's letter spacing between characters; whole lines have their own.
    const int spacing = metrics.shadcn ? 0 : size * kWelcomeTitleLetterSpacing;
    const int block = line_height * static_cast<int>(lines_.size()) +
                      kWelcomeStartButtonGap + kWelcomeStartButtonHeight;
    // Centred, raised a little (translateY(-5%)).
    int y = (height() - block) / 2 - line_height / 10;
    for (const auto& line : lines_) {
      int line_width = 0;
      for (views::Label* label : line) {
        label->SetFontList(font);
        line_width += label->GetPreferredSize().width() + spacing;
      }
      int x = (width() - line_width + spacing) / 2;
      for (views::Label* label : line) {
        const int char_width = label->GetPreferredSize().width();
        label->SetBounds(x, y, char_width, line_height);
        x += char_width + spacing;
      }
      y += line_height;
    }
    start_->SetBounds((width() - kWelcomeStartButtonWidth) / 2,
                      y + kWelcomeStartButtonGap, kWelcomeStartButtonWidth,
                      kWelcomeStartButtonHeight);

    // The pages fill most of the window, or all of a small one.
    gfx::Rect card = GetLocalBounds();
    if (width() >= kWelcomeCardFullWindowBelow) {
      card.ClampToCenteredSize(
          gfx::Size(static_cast<int>(width() * kWelcomeCardWidth),
                    static_cast<int>(height() * kWelcomeCardHeight)));
    }
    pages_->SetBoundsRect(card);
    if (pages_->layer()) {
      pages_->layer()->SetRoundedCornerRadius(
          gfx::RoundedCornersF(CardRadius()));
    }
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* provider = GetColorProvider();
    if (GetMetrics().shadcn) {
      // shadcn: the window hidden behind `background`, the card lifted off
      // it by a small shadow.
      canvas->DrawColor(provider->GetColor(kColorKoanWelcomeScrim));
      if (pages_->GetVisible() && CardRadius() > 0) {
        cc::PaintFlags flags;
        flags.setAntiAlias(true);
        flags.setColor(provider->GetColor(kColorKoanWelcomeSurface));
        flags.setLooper(gfx::CreateShadowDrawLooper({gfx::ShadowValue(
            gfx::Vector2d(0, kWelcomeOptionShadowY), kWelcomeOptionShadowBlur,
            provider->GetColor(kColorKoanWelcomeShadow))}));
        canvas->DrawRoundRect(pages_->bounds(), CardRadius(), flags);
      }
      return;
    }
    // Zen plays a calm video here; Koan paints a calm gradient.
    const SkPoint points[2] = {SkPoint::Make(0, 0),
                               SkPoint::Make(width(), height())};
    const SkColor4f colors[2] = {
        SkColor4f::FromColor(provider->GetColor(kColorKoanWelcomeBackdropTop)),
        SkColor4f::FromColor(
            provider->GetColor(kColorKoanWelcomeBackdropBottom))};
    cc::PaintFlags flags;
    flags.setShader(cc::PaintShader::MakeLinearGradient(points, colors, nullptr,
                                                        2, SkTileMode::kClamp));
    canvas->DrawRect(GetLocalBounds(), flags);
  }

 private:
  // The card's corners: none when it fills a small window.
  int CardRadius() const {
    return width() >= kWelcomeCardFullWindowBelow ? GetMetrics().card_radius
                                                  : 0;
  }

  // views::View:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    welcome_->Skip();
    return true;
  }

  void ShowStartButton() {
    const Metrics metrics = GetMetrics();
    AnimateIn(start_, gfx::Vector2dF(0, metrics.shadcn ? metrics.rise : 20),
              base::Milliseconds(100),
              metrics.shadcn ? 2 * metrics.fade : base::Milliseconds(400));
  }

  void OnStart() {
    // Not by disabling the button: a focused view that is disabled or hidden
    // passes the focus on, out of the screen and into the page.
    if (std::exchange(started_, true)) {
      return;
    }
    if (!ShouldAnimate()) {
      ShowPagesSoon();
      return;
    }
    // Everything rises out, one after another; then the pages.
    views::AnimationBuilder builder;
    builder.OnEnded(base::BindOnce(&KoanWelcomeView::ShowPagesSoon,
                                   weak_factory_.GetWeakPtr()));
    views::AnimationSequenceBlock* block = &builder.Once();
    gfx::Transform above;
    above.Translate(0, GetMetrics().shadcn ? -GetMetrics().rise / 2 : -14);
    int index = 0;
    for (const auto& line : lines_) {
      for (views::Label* label : line) {
        block = &block->At(kCharExitStagger * index++);
        block->SetDuration(kCharExitDuration)
            .SetOpacity(label, 0, gfx::Tween::EASE_IN)
            .SetTransform(label, above, gfx::Tween::EASE_IN);
      }
    }
    block = &block->At(kCharExitStagger * index);
    block->SetDuration(kCharExitDuration)
        .SetOpacity(start_.get(), 0, gfx::Tween::EASE_IN)
        .SetTransform(start_.get(), above, gfx::Tween::EASE_IN);
  }

  void ShowPagesSoon() {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanWelcomeView::ShowPages,
                                  weak_factory_.GetWeakPtr()));
  }

  void ShowPages() {
    pages_->SetVisible(true);
    pages_->SetPaintToLayer();
    pages_->layer()->SetFillsBoundsOpaquely(false);
    pages_->layer()->SetIsFastRoundedCorner(true);
    InvalidateLayout();
    const Metrics metrics = GetMetrics();
    AnimateIn(pages_, gfx::Vector2dF(0, metrics.shadcn ? metrics.rise : 0),
              base::TimeDelta(),
              metrics.shadcn ? 2 * metrics.fade : kPagesFadeDuration);
    // The pages take the keyboard before the start button hides.
    pages_->Start();
    for (const auto& line : lines_) {
      for (views::Label* label : line) {
        label->SetVisible(false);
      }
    }
    start_->SetVisible(false);
  }

  const raw_ptr<KoanWelcome> welcome_;
  std::vector<std::vector<views::Label*>> lines_;
  raw_ptr<KoanWelcomeStartButton> start_;
  raw_ptr<KoanWelcomePagesView> pages_;
  bool started_ = false;

  base::WeakPtrFactory<KoanWelcomeView> weak_factory_{this};
};

BEGIN_METADATA(KoanWelcomeView)
END_METADATA

// The title bar row on macOS, which the screen covers too, except for the
// traffic lights: it shows the scrim, and dragging it moves the window, as
// the title bar it covers would.
class KoanWelcomeTitleBarArea : public views::View {
  METADATA_HEADER(KoanWelcomeTitleBarArea, views::View)

 public:
  explicit KoanWelcomeTitleBarArea(BrowserView* browser_view)
      : browser_view_(browser_view) {
    GetViewAccessibility().SetIsIgnored(true);
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    canvas->DrawColor(GetColorProvider()->GetColor(kColorKoanWelcomeScrim));
  }
  bool OnMousePressed(const ui::MouseEvent& event) override {
    press_in_screen_ = event.location();
    views::View::ConvertPointToScreen(this, &*press_in_screen_);
    return true;
  }
  bool OnMouseDragged(const ui::MouseEvent& event) override {
    gfx::Point point = event.location();
    views::View::ConvertPointToScreen(this, &point);
    views::Widget* window = browser_view_->GetWidget();
    if (!press_in_screen_ || !window || !window->IsMoveLoopSupported() ||
        (point - *press_in_screen_).Length() < kMoveThreshold) {
      return true;
    }
    const gfx::Vector2d offset =
        *press_in_screen_ - window->GetWindowBoundsInScreen().origin();
    press_in_screen_.reset();
    // After this event, as for a page dragged from its top (koan_window_
    // drag.cc).
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<views::Widget> window, gfx::Vector2d offset) {
              if (window) {
                window->RunMoveLoop(
                    offset, views::Widget::MoveLoopSource::kMouse,
                    views::Widget::MoveLoopEscapeBehavior::kDontHide);
              }
            },
            window->GetWeakPtr(), offset));
    return true;
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
    press_in_screen_.reset();
  }

 private:
  // How far the pointer moves before the window does.
  static constexpr int kMoveThreshold = 4;

  const raw_ptr<BrowserView> browser_view_;
  std::optional<gfx::Point> press_in_screen_;
};

BEGIN_METADATA(KoanWelcomeTitleBarArea)
END_METADATA

// Shown at most once per browser session, in the first window.
bool g_shown_this_session = false;

}  // namespace

KoanWelcomeChoices::KoanWelcomeChoices() = default;
KoanWelcomeChoices::KoanWelcomeChoices(const KoanWelcomeChoices&) = default;
KoanWelcomeChoices& KoanWelcomeChoices::operator=(const KoanWelcomeChoices&) =
    default;
KoanWelcomeChoices::~KoanWelcomeChoices() = default;

KoanWelcome::KoanWelcome(BrowserView* browser_view)
    : browser_view_(browser_view) {}

KoanWelcome::~KoanWelcome() = default;

void KoanWelcome::OnAddedToWidget() {
  // After the window has been laid out and shown.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&KoanWelcome::MaybeShow, weak_factory_.GetWeakPtr()));
}

void KoanWelcome::OnBrowserViewLayout() {
  LayoutWidget();
}

void KoanWelcome::OnBrowserViewDestroying() {
  Close();
}

void KoanWelcome::MaybeShow() {
  Profile* profile = browser_view_->GetProfile();
  PrefService* prefs = profile->GetPrefs();
  if (g_shown_this_session || !features::IsWelcomeEnabled() ||
      !browser_view_->GetWidget() || profile->IsOffTheRecord() ||
      browser_view_->browser()->GetType() !=
          BrowserWindowInterface::TYPE_NORMAL ||
      prefs->GetBoolean(prefs::kWelcomeScreenSeen) ||
      !first_run::IsChromeFirstRun()) {
    return;
  }
  g_shown_this_session = true;
  Show();
}

void KoanWelcome::Show() {
#if BUILDFLAG(IS_MAC)
  // The first window opens filling the screen, for the welcome and after.
  if (!browser_view_->GetWidget()->IsMaximized()) {
    browser_view_->GetWidget()->Maximize();
  }
#endif
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  params.parent = browser_view_->GetWidget()->GetNativeView();
  // The card's rounded corners and shadow show the scrim behind them.
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.name = "KoanWelcome";
  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(params));
#if BUILDFLAG(IS_MAC)
  views::Widget::InitParams title_bar_params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  title_bar_params.parent = browser_view_->GetWidget()->GetNativeView();
  title_bar_params.opacity =
      views::Widget::InitParams::WindowOpacity::kTranslucent;
  title_bar_params.activatable = views::Widget::InitParams::Activatable::kNo;
  title_bar_params.name = "KoanWelcomeTitleBar";
  title_bar_widget_ = std::make_unique<views::Widget>();
  title_bar_widget_->Init(std::move(title_bar_params));
  title_bar_widget_->SetContentsView(
      std::make_unique<KoanWelcomeTitleBarArea>(browser_view_));
  title_bar_corner_ = browser_view_->AddChildView(
      std::make_unique<KoanWelcomeTitleBarArea>(browser_view_));
  title_bar_corner_->SetPaintToLayer();
  title_bar_corner_->layer()->SetFillsBoundsOpaquely(false);
#endif
  auto* view =
      widget_->SetContentsView(std::make_unique<KoanWelcomeView>(this));
  LayoutWidget();
  widget_->Show();
  widget_->LayoutRootViewIfNecessary();
  view->FocusStart();
  view->PlayIntro();
  // Seen once it is actually on screen (not merely decided on).
  if (widget_->IsVisible()) {
    browser_view_->GetProfile()->GetPrefs()->SetBoolean(
        prefs::kWelcomeScreenSeen, true);
  }
}

void KoanWelcome::Finish(const KoanWelcomeChoices& choices) {
  // "Dive in!" stays until the fade ends: a double-click or a held Enter
  // would otherwise apply the choices twice (pin every Essential twice).
  if (!widget_ || std::exchange(finishing_, true)) {
    return;
  }
  const auto close_and_apply = [](base::WeakPtr<KoanWelcome> welcome,
                                  KoanWelcomeChoices choices) {
    if (welcome) {
      welcome->Close();
      welcome->Apply(choices);
    }
  };
  if (!ShouldAnimate()) {
    // After the click that finished it: the button goes with the widget.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(close_and_apply, weak_factory_.GetWeakPtr(), choices));
    return;
  }
  views::View* view = widget_->GetContentsView();
  if (!view->layer()) {
    view->SetPaintToLayer();
  }
  // The title bar row fades with it.
  for (views::View* part :
       {title_bar_widget_ ? title_bar_widget_->GetContentsView() : nullptr,
        title_bar_corner_.get()}) {
    if (!part) {
      continue;
    }
    if (!part->layer()) {
      part->SetPaintToLayer();
      part->layer()->SetFillsBoundsOpaquely(false);
    }
    views::AnimationBuilder()
        .Once()
        .SetDuration(kFinishFadeDuration)
        .SetOpacity(part, 0, gfx::Tween::EASE_IN);
  }
  views::AnimationBuilder()
      .OnEnded(base::BindOnce(
          [](decltype(close_and_apply) then, base::WeakPtr<KoanWelcome> welcome,
             KoanWelcomeChoices choices) {
            // Not from inside the widget's own animation.
            base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                FROM_HERE,
                base::BindOnce(then, std::move(welcome), std::move(choices)));
          },
          close_and_apply, weak_factory_.GetWeakPtr(), choices))
      .Once()
      .SetDuration(kFinishFadeDuration)
      .SetOpacity(view, 0, gfx::Tween::EASE_IN);
}

void KoanWelcome::Skip() {
  // From the screen's own key handling: close once it has returned.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&KoanWelcome::Close, weak_factory_.GetWeakPtr()));
}

void KoanWelcome::Close() {
  if (widget_ && !widget_->IsClosed()) {
    widget_->CloseNow();
  }
  widget_.reset();
  if (title_bar_widget_ && !title_bar_widget_->IsClosed()) {
    title_bar_widget_->CloseNow();
  }
  title_bar_widget_.reset();
  if (title_bar_corner_) {
    browser_view_->RemoveChildViewT(std::exchange(title_bar_corner_, nullptr));
  }
}

void KoanWelcome::LayoutWidget() {
  if (!widget_) {
    return;
  }
  gfx::Rect bounds = browser_view_->GetLocalBounds();
#if BUILDFLAG(IS_MAC)
  // All of the window, the title bar row included, but the traffic lights,
  // which stay above the screen and usable: the screen below the row, the
  // title bar parts in the row around the lights.
  const gfx::Rect lights = TrafficLightsBounds();
  const int row = lights.IsEmpty()
                      ? 0
                      : std::max(lights.bottom(), kWelcomeMacTitleBarHeight);
  bounds.Inset(gfx::Insets::TLBR(row, 0, 0, 0));
  if (title_bar_widget_) {
    const gfx::Rect beside(lights.right(), 0,
                           browser_view_->width() - lights.right(), row);
    title_bar_widget_->SetBounds(browser_view_->ConvertRectToWidget(beside));
    if (beside.IsEmpty()) {
      title_bar_widget_->Hide();
    } else if (!title_bar_widget_->IsVisible()) {
      title_bar_widget_->ShowInactive();
    }
  }
  if (title_bar_corner_) {
    title_bar_corner_->SetBoundsRect(gfx::Rect(0, 0, lights.right(), row));
  }
#endif
  // Elsewhere all of the window: Esc closes the screen.
  widget_->SetBounds(browser_view_->ConvertRectToWidget(bounds));
}

gfx::Rect KoanWelcome::TrafficLightsBounds() const {
  const BrowserFrameView* frame =
      browser_view_->browser_widget()
          ? browser_view_->browser_widget()->GetFrameView()
          : nullptr;
  if (!frame) {
    return gfx::Rect();
  }
  // The frame's leading exclusion: where the traffic lights are, with the
  // room it keeps around them (at the lower x, also right-to-left).
  const BrowserLayoutParams params = frame->GetBrowserLayoutParams();
  if (params.leading_exclusion.IsEmpty()) {
    return gfx::Rect();
  }
  gfx::Point origin = params.visual_client_area.origin();
  views::View::ConvertPointFromWidget(browser_view_, &origin);
  return gfx::Rect(
      origin, gfx::ToCeiledSize(params.leading_exclusion.ContentWithPadding()));
}

void KoanWelcome::Apply(const KoanWelcomeChoices& choices) {
  BrowserWindowInterface* browser = browser_view_->browser();
  for (const GURL& url : choices.essentials) {
    chrome::AddTabAt(browser, url, -1, /*foreground=*/false, std::nullopt,
                     /*pinned=*/true);
  }
  if (choices.import) {
    chrome::ExecuteCommand(browser, IDC_IMPORT_SETTINGS);
  }
}

}  // namespace koan
