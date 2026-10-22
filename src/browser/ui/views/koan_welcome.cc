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
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
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
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/vector_icons/vector_icons.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// zen-welcome.ftl.
constexpr char16_t kTitleLine1[] = u"Welcome to";
constexpr char16_t kTitleLine2[] = u"a calmer internet";
constexpr char16_t kBack[] = u"Back";
constexpr char16_t kNext[] = u"Next";
constexpr char16_t kSkip[] = u"Skip for now";
constexpr char16_t kYes[] = u"YES";
constexpr char16_t kNo[] = u"NO";

struct PageText {
  const char16_t* title;
  const char16_t* description;
};

enum Page {
  kImport,
  kColors,
  kSearch,
  kEssentials,
  kDefaultBrowser,
  kFinish,
  kPageCount,
};

// ZenWelcome.mjs's pages, in its order (without the ad blocker).
constexpr std::array<PageText, kPageCount> kPageText = {{
    {u"Bring your stuff along.",
     u"Import your bookmarks, history and passwords from another browser and "
     u"pick up right where you left off."},
    {u"Your space, your colors.",
     u"Personalize your browser by giving each space its own unique color "
     u"identity."},
    {u"Pick your search engine.",
     u"Choose your default search engine. You can always change it later!"},
    {u"Choose the apps you use most.",
     u"Select your favorite apps to keep them handy in your sidebar."},
    {u"Make Koan your default?",
     u"Links from other apps will open in Koan. You can always change this "
     u"later."},
    {u"All set? Let\u2019s get rolling!",
     u"You\u2019re all set up and ready to go. Click the button below to start "
     u"browsing with Koan."},
}};

// ZenWelcome.mjs's Essentials. Koan shows each app's initial and name
// rather than its logo.
struct EssentialApp {
  const char* url;
  const char16_t* name;
  SkColor color;
};
constexpr auto kEssentialApps = std::to_array<EssentialApp>({
    {"https://obsidian.md", u"Obsidian", SkColorSetRGB(0x7c, 0x3a, 0xed)},
    {"https://discord.com", u"Discord", SkColorSetRGB(0x58, 0x65, 0xf2)},
    {"https://trello.com", u"Trello", SkColorSetRGB(0x00, 0x52, 0xcc)},
    {"https://slack.com", u"Slack", SkColorSetRGB(0xe0, 0x1e, 0x5a)},
    {"https://github.com", u"GitHub", SkColorSetRGB(0x57, 0x60, 0x6a)},
    {"https://app.tuta.com/", u"Tuta", SkColorSetRGB(0xc0, 0x00, 0x02)},
    {"https://notion.com", u"Notion", SkColorSetRGB(0x37, 0x35, 0x2f)},
    {"https://calendar.google.com", u"Calendar",
     SkColorSetRGB(0x42, 0x85, 0xf4)},
    {"https://figma.com", u"Figma", SkColorSetRGB(0xf2, 0x4e, 0x1e)},
});

// The colours offered for the browser (Koan tints with one colour where Zen
// has a gradient picker).
constexpr auto kSpaceColors = std::to_array<SkColor>({
    SkColorSetRGB(0xd9, 0x6c, 0x4f),
    SkColorSetRGB(0xe0, 0xa1, 0x3a),
    SkColorSetRGB(0x6f, 0xa8, 0x55),
    SkColorSetRGB(0x3e, 0x9d, 0x92),
    SkColorSetRGB(0x4f, 0x86, 0xd9),
    SkColorSetRGB(0x7a, 0x6c, 0xe0),
    SkColorSetRGB(0xb4, 0x5f, 0xc9),
    SkColorSetRGB(0xd9, 0x5f, 0x93),
    SkColorSetRGB(0x8a, 0x8f, 0x98),
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

// Plays `view` in: from `offset` and transparent to where it is, after
// `delay`.
void AnimateIn(views::View* view,
               const gfx::Vector2dF& offset,
               base::TimeDelta delay,
               base::TimeDelta duration) {
  if (!view->layer()) {
    view->SetPaintToLayer();
    view->layer()->SetFillsBoundsOpaquely(false);
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

// Zen's big accent button: filled for the primary action, text otherwise.
class KoanWelcomeButton : public views::Button {
  METADATA_HEADER(KoanWelcomeButton, views::Button)

 public:
  KoanWelcomeButton(PressedCallback callback, std::u16string text, bool primary)
      : views::Button(std::move(callback)),
        text_(std::move(text)),
        primary_(primary) {
    GetViewAccessibility().SetName(text_);
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, kWelcomeButtonHeight);
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const Palette palette = GetPalette(this);
    SkColor text = palette.accent;
    if (primary_) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(GetState() == STATE_HOVERED ? palette.accent_hovered
                                                 : palette.accent);
      canvas->DrawRoundRect(GetLocalBounds(), kWelcomeButtonCornerRadius,
                            flags);
      text = palette.on_accent;
    }
    canvas->DrawStringRectWithFlags(
        text_, Font(kWelcomeButtonFontSize, gfx::Font::Weight::SEMIBOLD), text,
        GetLocalBounds(), gfx::Canvas::TEXT_ALIGN_CENTER);
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
    canvas->DrawRoundRect(GetLocalBounds(), kWelcomeButtonCornerRadius, flags);
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

// "< Back" at the top of the pages' sidebar.
class KoanWelcomeBackButton : public views::Button {
  METADATA_HEADER(KoanWelcomeBackButton, views::Button)

 public:
  explicit KoanWelcomeBackButton(PressedCallback callback)
      : views::Button(std::move(callback)) {
    GetViewAccessibility().SetName(kBack);
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kWelcomeBackIconSize + kWelcomeBackIconGap +
                         gfx::GetStringWidth(kBack, GetFont()),
                     kWelcomeBackHeight);
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const SkColor accent = GetPalette(this).accent;
    canvas->DrawImageInt(gfx::CreateVectorIcon(vector_icons::kArrowBackIcon,
                                               kWelcomeBackIconSize, accent),
                         0, (height() - kWelcomeBackIconSize) / 2);
    gfx::Rect text = GetLocalBounds();
    text.Inset(
        gfx::Insets::TLBR(0, kWelcomeBackIconSize + kWelcomeBackIconGap, 0, 0));
    canvas->DrawStringRect(kBack, GetFont(), accent, text);
  }

 private:
  static gfx::FontList GetFont() {
    return Font(kWelcomeBackFontSize, gfx::Font::Weight::SEMIBOLD);
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
  }

  bool selected() const { return selected_; }
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
    return engine_ ? gfx::Size(kWelcomeEngineMaxWidth, kWelcomeEngineHeight)
                   : gfx::Size(kWelcomeOptionMaxWidth, kWelcomeOptionHeight);
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const Palette palette = GetPalette(this);
    const gfx::RectF bounds(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(palette.surface);
    flags.setLooper(gfx::CreateShadowDrawLooper(
        {gfx::ShadowValue(gfx::Vector2d(0, kWelcomeOptionShadowY),
                          kWelcomeOptionShadowBlur, palette.shadow)}));
    gfx::RectF card = bounds;
    card.Inset(1);
    canvas->DrawRoundRect(card, kWelcomeOptionCornerRadius, flags);
    flags.setLooper(nullptr);

    // An engine's tile on the trailing side, clipped by the card.
    if (engine_) {
      canvas->Save();
      canvas->sk_canvas()->clipRRect(
          SkRRect::MakeRectXY(gfx::RectFToSkRect(card),
                              kWelcomeOptionCornerRadius,
                              kWelcomeOptionCornerRadius),
          true);
      const float tile_width = card.width() * kWelcomeEngineTileWidth;
      const gfx::RectF tile(card.right() - tile_width, card.y(), tile_width,
                            card.height());
      flags.setColor(palette.tile);
      canvas->DrawRect(tile, flags);
      canvas->DrawStringRectWithFlags(
          text_.substr(0, 1),
          Font(kWelcomeEngineTileFontSize, gfx::Font::Weight::BOLD),
          palette.text_secondary, gfx::ToEnclosingRect(tile),
          gfx::Canvas::TEXT_ALIGN_CENTER);
      canvas->Restore();
    }

    const SkColor color = selected_ ? palette.accent : palette.text;
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(selected_ ? palette.accent : palette.border);
    card.Inset(0.5f);
    canvas->DrawRoundRect(card, kWelcomeOptionCornerRadius, flags);

    const gfx::PointF radio(kWelcomeOptionPadding + kWelcomeRadioSize / 2.0f,
                            height() / 2.0f);
    flags.setColor(selected_ ? palette.accent : palette.radio);
    canvas->DrawCircle(radio, kWelcomeRadioSize / 2.0f - 0.5f, flags);
    if (selected_) {
      flags.setStyle(cc::PaintFlags::kFill_Style);
      canvas->DrawCircle(
          radio, kWelcomeRadioSize / 2.0f - kWelcomeRadioDotInset, flags);
    }

    int x = kWelcomeOptionPadding + kWelcomeRadioSize + kWelcomeOptionTextGap;
    const gfx::FontList regular =
        Font(kWelcomeOptionFontSize,
             engine_ ? gfx::Font::Weight::SEMIBOLD : gfx::Font::Weight::NORMAL);
    if (!strong_.empty()) {
      const gfx::FontList bold =
          Font(kWelcomeOptionFontSize, gfx::Font::Weight::BOLD);
      const std::u16string strong = strong_ + u", ";
      canvas->DrawStringRect(strong, bold, color,
                             gfx::Rect(x, 0, width() - x, height()));
      x += gfx::GetStringWidth(strong, bold);
    }
    const int right =
        engine_ ? static_cast<int>(width() * (1 - kWelcomeEngineTileWidth))
                : width() - kWelcomeOptionPadding;
    canvas->DrawStringRectWithFlags(text_, regular, color,
                                    gfx::Rect(x, 0, right - x, height()),
                                    gfx::Canvas::NO_SUBPIXEL_RENDERING);
  }

 private:
  const std::u16string strong_;
  const std::u16string text_;
  const bool engine_;
  bool selected_ = false;
};

BEGIN_METADATA(KoanWelcomeOption)
END_METADATA

// A column of options, centred; pressing one selects it.
class KoanWelcomeOptionsView : public views::View {
  METADATA_HEADER(KoanWelcomeOptionsView, views::View)

 public:
  explicit KoanWelcomeOptionsView(int gap) : gap_(gap) {}

  // Adds an option; `on_select` runs when it gets selected.
  void AddOption(std::u16string strong,
                 std::u16string text,
                 bool selected,
                 bool engine,
                 base::RepeatingClosure on_select) {
    auto* option = AddChildView(std::make_unique<KoanWelcomeOption>(
        views::Button::PressedCallback(), std::move(strong), std::move(text),
        engine));
    option->SetCallback(base::BindRepeating(
        &KoanWelcomeOptionsView::Select, base::Unretained(this),
        base::Unretained(option), std::move(on_select)));
    option->SetSelected(selected);
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

  const int gap_;
};

BEGIN_METADATA(KoanWelcomeOptionsView)
END_METADATA

// A colour to pick on the colours page.
class KoanWelcomeSwatch : public views::Button {
  METADATA_HEADER(KoanWelcomeSwatch, views::Button)

 public:
  KoanWelcomeSwatch(PressedCallback callback, SkColor color)
      : views::Button(std::move(callback)), color_(color) {
    GetViewAccessibility().SetName(u"Color");
  }

  void SetSelected(bool selected) {
    selected_ = selected;
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
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(2.5f);
      flags.setColor(selected_ ? GetPalette(this).text
                               : SkColorSetA(color_, 0x80));
      canvas->DrawCircle(center, kWelcomeSwatchSize / 2.0f - 1.5f, flags);
    }
  }

 private:
  const SkColor color_;
  bool selected_ = false;
};

BEGIN_METADATA(KoanWelcomeSwatch)
END_METADATA

// A panel of colours, like a small colour picker.
class KoanWelcomeColorsView : public views::View {
  METADATA_HEADER(KoanWelcomeColorsView, views::View)

 public:
  explicit KoanWelcomeColorsView(base::RepeatingCallback<void(SkColor)> pick,
                                 std::optional<SkColor> selected) {
    for (SkColor color : kSpaceColors) {
      auto* swatch = AddChildView(std::make_unique<KoanWelcomeSwatch>(
          views::Button::PressedCallback(), color));
      swatch->SetCallback(base::BindRepeating(
          &KoanWelcomeColorsView::Pick, base::Unretained(this),
          base::Unretained(swatch), color, pick));
      swatch->SetSelected(selected == color);
    }
  }

  // views::View:
  void Layout(PassKey) override {
    const gfx::Rect panel = PanelBounds();
    for (size_t i = 0; i < children().size(); ++i) {
      children()[i]->SetBounds(
          panel.x() + kWelcomePanelPadding +
              (i % 3) * (kWelcomeSwatchSize + kWelcomeSwatchGap),
          panel.y() + kWelcomePanelPadding +
              (i / 3) * (kWelcomeSwatchSize + kWelcomeSwatchGap),
          kWelcomeSwatchSize, kWelcomeSwatchSize);
    }
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const Palette palette = GetPalette(this);
    gfx::RectF panel(PanelBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(palette.surface);
    flags.setLooper(gfx::CreateShadowDrawLooper(
        {gfx::ShadowValue(gfx::Vector2d(0, kWelcomeOptionShadowY),
                          kWelcomeOptionShadowBlur, palette.shadow)}));
    canvas->DrawRoundRect(panel, kWelcomeOptionCornerRadius, flags);
    flags.setLooper(nullptr);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setColor(palette.border);
    panel.Inset(0.5f);
    canvas->DrawRoundRect(panel, kWelcomeOptionCornerRadius, flags);
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
};

BEGIN_METADATA(KoanWelcomeColorsView)
END_METADATA

// An app on the Essentials page; pressing toggles it.
class KoanWelcomeEssentialTile : public views::Button {
  METADATA_HEADER(KoanWelcomeEssentialTile, views::Button)

 public:
  KoanWelcomeEssentialTile(PressedCallback callback, const EssentialApp& app)
      : views::Button(std::move(callback)), app_(app) {
    GetViewAccessibility().SetName(app.name);
    SetTooltipText(app.name);
    GetViewAccessibility().SetRole(ax::mojom::Role::kToggleButton);
  }

  bool selected() const { return selected_; }
  void SetSelected(bool selected) {
    selected_ = selected;
    GetViewAccessibility().SetCheckedState(
        selected ? ax::mojom::CheckedState::kTrue
                 : ax::mojom::CheckedState::kFalse);
    SchedulePaint();
  }

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const Palette palette = GetPalette(this);
    gfx::RectF tile(GetLocalBounds());
    const float radius = std::min(kWelcomeTileCornerRadius, width() / 3);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(
        selected_ ? color_utils::AlphaBlend(app_->color, palette.surface, 0.22f)
                  : palette.tile);
    canvas->DrawRoundRect(tile, radius, flags);
    if (selected_) {
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(kWelcomeTileBorder);
      flags.setColor(SkColorSetA(app_->color, 0x99));
      tile.Inset(1.5f);
      canvas->DrawRoundRect(tile, radius - 1.5f, flags);
    }
    // The app's initial in its colour, its name below.
    const int initial_size = std::max(16, width() * 30 / 100);
    gfx::Rect initial = GetLocalBounds();
    initial.set_height(height() * 3 / 4);
    canvas->DrawStringRectWithFlags(std::u16string(app_->name, 1),
                                    Font(initial_size, gfx::Font::Weight::BOLD),
                                    app_->color, initial,
                                    gfx::Canvas::TEXT_ALIGN_CENTER);
    gfx::Rect name = GetLocalBounds();
    name.Inset(gfx::Insets::TLBR(height() * 3 / 5, 4, 0, 4));
    canvas->DrawStringRectWithFlags(
        app_->name, Font(kWelcomeTileNameFontSize, gfx::Font::Weight::MEDIUM),
        palette.text_secondary, name, gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  const raw_ref<const EssentialApp> app_;
  bool selected_ = false;
};

BEGIN_METADATA(KoanWelcomeEssentialTile)
END_METADATA

// The Essentials page: a mock browser whose sidebar holds the apps, at the
// bottom right of the page.
class KoanWelcomeMockBrowserView : public views::View {
  METADATA_HEADER(KoanWelcomeMockBrowserView, views::View)

 public:
  explicit KoanWelcomeMockBrowserView(std::set<size_t>* selected) {
    for (size_t i = 0; i < std::size(kEssentialApps); ++i) {
      auto* tile = AddChildView(std::make_unique<KoanWelcomeEssentialTile>(
          views::Button::PressedCallback(), kEssentialApps[i]));
      tile->SetCallback(base::BindRepeating(
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
      tile->SetSelected(selected->contains(i));
    }
  }

  // views::View:
  void Layout(PassKey) override {
    const gfx::Rect sidebar = SidebarBounds();
    const int grid_y = sidebar.y() + kSidebarInsets.top() +
                       kWelcomeMockDotSize + kWelcomeMockDotsMargin +
                       kWelcomeMockGap;
    const int tile =
        (sidebar.width() - kSidebarInsets.width() - 2 * kWelcomeMockGridGap) /
        3;
    for (size_t i = 0; i < children().size(); ++i) {
      children()[i]->SetBounds(sidebar.x() + kSidebarInsets.left() +
                                   (i % 3) * (tile + kWelcomeMockGridGap),
                               grid_y + (i / 3) * (tile + kWelcomeMockGridGap),
                               tile, tile);
    }
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const Palette palette = GetPalette(this);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    // The window, rounded at the top left only.
    const SkRect window = gfx::RectToSkRect(MockBounds());
    const SkVector radii[4] = {
        {kWelcomeMockCornerRadius, kWelcomeMockCornerRadius},
        {0, 0},
        {0, 0},
        {0, 0}};
    SkRRect rounded;
    rounded.setRectRadii(window, radii);
    canvas->sk_canvas()->save();
    canvas->sk_canvas()->clipRRect(rounded, true);
    flags.setColor(palette.mock);
    canvas->DrawRect(MockBounds(), flags);
    const gfx::Rect sidebar = SidebarBounds();
    flags.setColor(palette.surface);
    canvas->DrawRect(sidebar, flags);
    // Three window dots, and two tabs under the grid.
    flags.setColor(palette.tile);
    for (int i = 0; i < 3; ++i) {
      canvas->DrawCircle(
          gfx::PointF(
              sidebar.x() + kSidebarInsets.left() + kWelcomeMockDotSize / 2.0f +
                  i * (kWelcomeMockDotSize + kWelcomeMockDotGap),
              sidebar.y() + kSidebarInsets.top() + kWelcomeMockDotSize / 2.0f),
          kWelcomeMockDotSize / 2.0f, flags);
    }
    int y = children().empty()
                ? sidebar.y()
                : children().back()->bounds().bottom() + kWelcomeMockGap;
    for (int i = 0; i < 2; ++i) {
      canvas->DrawRoundRect(gfx::Rect(sidebar.x() + kSidebarInsets.left(), y,
                                      sidebar.width() - kSidebarInsets.width(),
                                      kWelcomeMockTabHeight),
                            kWelcomeMockTabCornerRadius, flags);
      y += kWelcomeMockTabHeight + kWelcomeMockGap;
    }
    canvas->sk_canvas()->restore();
  }

 private:
  static constexpr auto kSidebarInsets =
      gfx::Insets::VH(kWelcomeMockSidebarPaddingVertical,
                      kWelcomeMockSidebarPaddingHorizontal);

  // zen-welcome.css: the page pads 3rem at the top, 6rem on the left; the
  // window is 92% of that wide, aligned to the bottom right.
  gfx::Rect MockBounds() const {
    const int window_width = static_cast<int>(
        (width() - kWelcomeMockPaddingLeft) * kWelcomeMockWidth);
    return gfx::Rect(width() - window_width, kWelcomeMockPaddingTop,
                     window_width, height() - kWelcomeMockPaddingTop);
  }
  gfx::Rect SidebarBounds() const {
    gfx::Rect sidebar = MockBounds();
    sidebar.set_width(
        static_cast<int>(sidebar.width() * kWelcomeMockSidebarWidth));
    return sidebar;
  }
};

BEGIN_METADATA(KoanWelcomeMockBrowserView)
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
    const gfx::Rect sidebar = SidebarBounds();
    gfx::Rect inner = sidebar;
    inner.Inset(kSidebarPadding);
    back_->SetBoundsRect(gfx::Rect(inner.origin(), back_->GetPreferredSize()));

    int buttons_height = 0;
    for (const views::View* button : buttons_->children()) {
      buttons_height += button->GetPreferredSize().height() + kWelcomeButtonGap;
    }
    buttons_height = std::max(0, buttons_height - kWelcomeButtonGap);
    buttons_->SetBounds(inner.x(), inner.bottom() - buttons_height,
                        inner.width(), buttons_height);
    int y = 0;
    for (views::View* button : buttons_->children()) {
      button->SetBounds(0, y, inner.width(), kWelcomeButtonHeight);
      y += kWelcomeButtonHeight + kWelcomeButtonGap;
    }

    y = back_->bounds().bottom() + kWelcomeBackGap;
    for (size_t i = 0; i < text_.size(); ++i) {
      const int height = text_[i]->GetHeightForWidth(inner.width());
      text_[i]->SetBounds(inner.x(), y, inner.width(), height);
      y += height + (i == 0 ? kWelcomeHeadingGap : kWelcomeDescriptionGap);
    }

    if (content_) {
      content_->SetBounds(sidebar.right(), 0, width() - sidebar.right(),
                          height());
    }
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const Palette palette = GetPalette(this);
    canvas->DrawColor(palette.content);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(palette.sidebar);
    flags.setLooper(gfx::CreateShadowDrawLooper(
        {gfx::ShadowValue(gfx::Vector2d(kWelcomeSidebarShadowX, 0),
                          kWelcomeSidebarShadowBlur, palette.shadow)}));
    canvas->DrawRect(SidebarBounds(), flags);
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
    if (page >= kPageCount) {
      welcome_->Finish(choices_);
      return;
    }
    ShowPage(page, delta > 0 ? 1 : -1);
  }

  // Leaving the current page with Next.
  void Commit() {
    if (page_ == kDefaultBrowser && default_browser_) {
      base::MakeRefCounted<shell_integration::DefaultBrowserWorker>()
          ->StartSetAsDefault(base::DoNothing());
    }
    if (page_ == kEssentials) {
      choices_.essentials.clear();
      for (size_t index : essentials_) {
        choices_.essentials.emplace_back(kEssentialApps[index].url);
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

    back_->SetVisible(page > 0);
    const PageText& text = kPageText[page];
    for (const char16_t* string : {text.title, text.description}) {
      const bool heading = string == text.title;
      auto* label = AddChildView(std::make_unique<views::Label>(string));
      label->SetMultiLine(true);
      label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      label->SetAutoColorReadabilityEnabled(false);
      label->SetSubpixelRenderingEnabled(false);
      label->SetFontList(
          heading ? Font(kWelcomeHeadingSize, gfx::Font::Weight::BOLD)
                  : Font(kWelcomeDescriptionSize, gfx::Font::Weight::NORMAL));
      if (!heading) {
        label->SetLineHeight(static_cast<int>(kWelcomeDescriptionSize *
                                              kWelcomeDescriptionLineHeight));
      }
      text_.push_back(label);
    }
    UpdateTextColors();

    if (page == kFinish) {
      AddButton(u"Dive in!", /*primary=*/true, 1);
    } else {
      AddButton(kNext, /*primary=*/true, 1);
    }
    if (page == kEssentials) {
      // Zen's skip clears the selection, then goes on.
      AddButton(kSkip, /*primary=*/false, 1)
          ->SetCallback(base::BindRepeating(
              [](KoanWelcomePagesView* view) {
                view->essentials_.clear();
                view->PostShowPage(1);
              },
              base::Unretained(this)));
    }

    content_ = AddChildView(CreateContent(page));
    InvalidateLayout();

    for (size_t i = 0; i < text_.size(); ++i) {
      AnimateIn(text_[i], gfx::Vector2dF(120 * direction, 0), kTextStagger * i,
                kTextDuration);
    }
    AnimateIn(content_, gfx::Vector2dF(), base::TimeDelta(), kFadeDuration);
  }

  KoanWelcomeButton* AddButton(std::u16string text, bool primary, int delta) {
    return buttons_->AddChildView(std::make_unique<KoanWelcomeButton>(
        base::BindRepeating(&KoanWelcomePagesView::PostShowPage,
                            base::Unretained(this), delta),
        std::move(text), primary));
  }

  std::unique_ptr<views::View> CreateContent(int page) {
    switch (page) {
      case kImport: {
        auto options =
            std::make_unique<KoanWelcomeOptionsView>(kWelcomeOptionGap);
        options->AddOption(kYes, u"import from another browser.",
                           choices_.import, false,
                           base::BindRepeating(&KoanWelcomePagesView::SetImport,
                                               base::Unretained(this), true));
        options->AddOption(kNo, u"start fresh.", !choices_.import, false,
                           base::BindRepeating(&KoanWelcomePagesView::SetImport,
                                               base::Unretained(this), false));
        return options;
      }
      case kColors:
        return std::make_unique<KoanWelcomeColorsView>(
            base::BindRepeating(&KoanWelcomePagesView::SetColor,
                                base::Unretained(this)),
            color_);
      case kSearch:
        return CreateSearchContent();
      case kEssentials:
        return std::make_unique<KoanWelcomeMockBrowserView>(&essentials_);
      case kDefaultBrowser: {
        auto options =
            std::make_unique<KoanWelcomeOptionsView>(kWelcomeOptionGap);
        options->AddOption(
            kYes, u"make Koan default.", default_browser_, false,
            base::BindRepeating(&KoanWelcomePagesView::SetDefaultBrowser,
                                base::Unretained(this), true));
        options->AddOption(
            kNo, u"still exploring my options.", !default_browser_, false,
            base::BindRepeating(&KoanWelcomePagesView::SetDefaultBrowser,
                                base::Unretained(this), false));
        return options;
      }
      default:
        return std::make_unique<views::View>();
    }
  }

  std::unique_ptr<views::View> CreateSearchContent() {
    auto options = std::make_unique<KoanWelcomeOptionsView>(kWelcomeEngineGap);
    TemplateURLService* service = TemplateURLServiceFactory::GetForProfile(
        welcome_->browser_view()->GetProfile());
    if (!service) {
      return options;
    }
    const TemplateURL* default_engine = service->GetDefaultSearchProvider();
    size_t count = 0;
    for (const TemplateURL* engine : service->GetTemplateURLs()) {
      // Zen leaves out Wikipedia and eBay.
      const std::u16string name = base::ToLowerASCII(engine->short_name());
      if (engine->prepopulate_id() <= 0 ||
          name.find(u"wikipedia") != std::u16string::npos ||
          name.find(u"ebay") != std::u16string::npos ||
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
  explicit KoanWelcomeView(KoanWelcome* welcome) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
    GetViewAccessibility().SetName(u"Welcome to Koan");
    for (const char16_t* line : {kTitleLine1, kTitleLine2}) {
      std::vector<views::Label*> chars;
      for (char16_t c : std::u16string(line)) {
        auto* label =
            AddChildView(std::make_unique<views::Label>(std::u16string(1, c)));
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
    start_->SetEnabled(false);
    pages_ = AddChildView(std::make_unique<KoanWelcomePagesView>(welcome));
    pages_->SetVisible(false);
  }

  void PlayIntro() {
    // Characters rise in one after another; then the button.
    views::AnimationBuilder builder;
    builder.OnEnded(base::BindOnce(&KoanWelcomeView::ShowStartButton,
                                   weak_factory_.GetWeakPtr()));
    views::AnimationSequenceBlock* block = &builder.Once();
    int index = 0;
    for (const auto& line : lines_) {
      for (views::Label* label : line) {
        gfx::Transform below;
        below.Translate(0, 50);
        label->layer()->SetTransform(below);
        block = &block->At(kCharStartDelay + kCharStagger * index++);
        block->SetDuration(kCharDuration)
            .SetOpacity(label, 1, gfx::Tween::EASE_OUT)
            .SetTransform(label, gfx::Transform(), gfx::Tween::EASE_OUT_3);
      }
    }
  }

  // views::View:
  void Layout(PassKey) override {
    const int size =
        std::clamp(width() / 12, kWelcomeTitleMinSize, kWelcomeTitleMaxSize);
    const gfx::FontList font = Font(size, gfx::Font::Weight::BOLD);
    const int line_height = size * kWelcomeTitleLineHeight;
    const int spacing = size * kWelcomeTitleLetterSpacing;
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
      pages_->layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(
          width() >= kWelcomeCardFullWindowBelow ? kWelcomeCardCornerRadius
                                                 : 0));
    }
  }

  void OnPaint(gfx::Canvas* canvas) override {
    // Zen plays a calm video here; Koan paints a calm gradient.
    const SkPoint points[2] = {SkPoint::Make(0, 0),
                               SkPoint::Make(width(), height())};
    const ui::ColorProvider* provider = GetColorProvider();
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
  void ShowStartButton() {
    start_->SetEnabled(true);
    AnimateIn(start_, gfx::Vector2dF(0, 20), base::Milliseconds(100),
              base::Milliseconds(400));
  }

  void OnStart() {
    start_->SetEnabled(false);
    // Everything rises out, one after another; then the pages.
    views::AnimationBuilder builder;
    builder.OnEnded(base::BindOnce(&KoanWelcomeView::ShowPagesSoon,
                                   weak_factory_.GetWeakPtr()));
    views::AnimationSequenceBlock* block = &builder.Once();
    gfx::Transform above;
    above.Translate(0, -14);
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
    for (const auto& line : lines_) {
      for (views::Label* label : line) {
        label->SetVisible(false);
      }
    }
    start_->SetVisible(false);
    pages_->SetVisible(true);
    pages_->SetPaintToLayer();
    pages_->layer()->SetFillsBoundsOpaquely(false);
    pages_->layer()->SetIsFastRoundedCorner(true);
    InvalidateLayout();
    AnimateIn(pages_, gfx::Vector2dF(), base::TimeDelta(), kPagesFadeDuration);
    pages_->Start();
  }

  std::vector<std::vector<views::Label*>> lines_;
  raw_ptr<KoanWelcomeStartButton> start_;
  raw_ptr<KoanWelcomePagesView> pages_;

  base::WeakPtrFactory<KoanWelcomeView> weak_factory_{this};
};

BEGIN_METADATA(KoanWelcomeView)
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
  prefs->SetBoolean(prefs::kWelcomeScreenSeen, true);
  Show();
}

void KoanWelcome::Show() {
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  params.parent = browser_view_->GetWidget()->GetNativeView();
  params.name = "KoanWelcome";
  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(params));
  auto* view =
      widget_->SetContentsView(std::make_unique<KoanWelcomeView>(this));
  LayoutWidget();
  widget_->Show();
  widget_->LayoutRootViewIfNecessary();
  view->PlayIntro();
}

void KoanWelcome::Finish(const KoanWelcomeChoices& choices) {
  if (!widget_) {
    return;
  }
  views::View* view = widget_->GetContentsView();
  if (!view->layer()) {
    view->SetPaintToLayer();
  }
  views::AnimationBuilder()
      .OnEnded(base::BindOnce(
          [](base::WeakPtr<KoanWelcome> welcome, KoanWelcomeChoices choices) {
            // Not from inside the widget's own animation.
            base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                FROM_HERE, base::BindOnce(
                               [](base::WeakPtr<KoanWelcome> welcome,
                                  KoanWelcomeChoices choices) {
                                 if (welcome) {
                                   welcome->Close();
                                   welcome->Apply(choices);
                                 }
                               },
                               welcome, std::move(choices)));
          },
          weak_factory_.GetWeakPtr(), choices))
      .Once()
      .SetDuration(kFinishFadeDuration)
      .SetOpacity(view, 0, gfx::Tween::EASE_IN);
}

void KoanWelcome::Close() {
  if (widget_ && !widget_->IsClosed()) {
    widget_->CloseNow();
  }
  widget_.reset();
}

void KoanWelcome::LayoutWidget() {
  if (widget_) {
    widget_->SetBounds(
        browser_view_->ConvertRectToWidget(browser_view_->GetLocalBounds()));
  }
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
