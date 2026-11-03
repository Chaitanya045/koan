// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_COLOR_KOAN_TOKENS_H_
#define KOAN_BROWSER_UI_COLOR_KOAN_TOKENS_H_

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "koan/browser/ui/color/koan_oklch.h"
#include "third_party/skia/include/core/SkColor.h"

namespace koan::look {

// shadcn/ui v4's colour tokens (https://ui.shadcn.com/docs/theming), in the
// order of koan_shadcn_palettes.inc. `--radius` is TokenSet::radius.
enum class Token : int {
  kBackground,
  kForeground,
  kCard,
  kCardForeground,
  kPopover,
  kPopoverForeground,
  kPrimary,
  kPrimaryForeground,
  kSecondary,
  kSecondaryForeground,
  kMuted,
  kMutedForeground,
  kAccent,
  kAccentForeground,
  kDestructive,
  kBorder,
  kInput,
  kRing,
  kSidebar,
  kSidebarForeground,
  kSidebarPrimary,
  kSidebarPrimaryForeground,
  kSidebarAccent,
  kSidebarAccentForeground,
  kSidebarBorder,
  kSidebarRing,
};
inline constexpr int kTokenCount = 26;

// The CSS variable of a token without the leading dashes ("card-foreground").
std::string_view TokenCssName(Token token);

// shadcn's default `--radius`, 0.625rem.
inline constexpr float kDefaultRadius = 10;

// Koan's look: Zen's (the default), or shadcn/ui's design system. Both are
// token sets (ResolveTokens), so every surface Koan or Chromium draws follows
// whichever is active.
enum class Style { kShadcn, kZen };
// Light or dark, or following the system (shadcn's `.dark` class).
enum class Mode { kAuto, kLight, kDark };
// shadcn's base colours, plus Koan's own default "Black" (Zinc with
// near-black surfaces: background #09090b, card and popover #18181b, borders
// #27272a, the window's frame as dark as the page), and its accent themes
// (which change the primary colour only; kNone keeps the base colour's own).
enum class BaseColor { kBlack, kNeutral, kZinc, kSlate, kStone, kGray };
enum class Accent {
  kNone,
  kBlue,
  kGreen,
  kOrange,
  kRed,
  kRose,
  kViolet,
  kYellow,
};

// One mode's tokens, as sRGB.
struct TokenSet {
  std::array<SkColor, kTokenCount> colors{};
  // `--radius` in pixels. The scale shadcn derives from it: sm = radius - 4,
  // md = radius - 2, lg = radius, xl = radius + 4.
  float radius = kDefaultRadius;

  SkColor operator[](Token token) const {
    return colors[static_cast<int>(token)];
  }
};

// A theme pasted from ui.shadcn.com/themes or tweakcn.com: the tokens it sets
// in `:root` (light) and `.dark`, and `--radius`. Whatever it doesn't set
// stays as it is in the base colour.
struct CustomTheme {
  std::array<std::optional<SkColor>, kTokenCount> light;
  std::array<std::optional<SkColor>, kTokenCount> dark;
  std::optional<float> radius;

  friend bool operator==(const CustomTheme&, const CustomTheme&) = default;
};

// Parses the CSS of a theme (`:root { --background: oklch(1 0 0); … }
// .dark { … }`). Understands oklch(), hsl() (with commas or spaces, or
// shadcn's pre-v4 bare `222.2 84% 4.9%`), rgb() and hex colours; ignores
// tokens it doesn't know, `var()` and anything else it can't read. Returns
// nullopt if the text has no token at all.
std::optional<CustomTheme> ParseThemeCss(std::string_view css);

// Zen's accent colours from its primary colour (--zen-primary-color, zen-
// buttons.css): the accent button's background and the accent text and
// borders, as the primary colour with its OKLCH lightness clamped to 0.35-0.6
// (light), 0.55-0.7 (dark; the background) or 0.7-0.85 (dark; the text).
struct ZenAccent {
  SkColor button_background;
  SkColor text;
};
ZenAccent ComputeZenAccent(SkColor primary, bool dark);

// What makes the tokens.
struct Look {
  Style style = Style::kZen;
  Mode mode = Mode::kDark;
  BaseColor base = BaseColor::kBlack;
  Accent accent = Accent::kNone;
  // The pasted theme, if any; applied over the base and accent.
  std::optional<CustomTheme> custom;
  // The Zen style's accent (Zen's --zen-primary-color): the dominant colour of
  // the active Space's theme, or `zen.theme.accent-color`. Without one, Zen's
  // default, the system's accent colour (CSS AccentColor, macOS blue unless
  // the user chose another). Set it with SetZenAccent() (koan_look.h).
  std::optional<SkColor> zen_accent;

  friend bool operator==(const Look&, const Look&) = default;
};

// The tokens of `look` for light or dark. In the shadcn style: the base
// colour's, the accent's primary colour over it, then the pasted theme over
// both. In the Zen style: Zen's own colours (zen-theme.css: its browser
// background, dialog and panel colours, text and borders, and the system
// accent colour for `primary`), which the base, accent and pasted theme leave
// alone. Zen's `input` is its URL bar's colour, an opaque fill, where
// shadcn's is a translucent border.
TokenSet ResolveTokens(const Look& look, bool dark);

// The scale of `--radius`, rounded to whole pixels (never below 0).
int RadiusSm(const TokenSet& tokens);
int RadiusMd(const TokenSet& tokens);
int RadiusLg(const TokenSet& tokens);
int RadiusXl(const TokenSet& tokens);

// The tokens as CSS custom properties (`--background: #0a0a0a; …`, one per
// line) for the WebUI pages, so they draw from the very same values.
std::string TokensToCss(const TokenSet& tokens);

// The names pref values and the settings page use.
std::string_view StyleToString(Style style);
std::optional<Style> StyleFromString(std::string_view name);
std::string_view ModeToString(Mode mode);
std::optional<Mode> ModeFromString(std::string_view name);
std::string_view BaseColorToString(BaseColor base);
std::optional<BaseColor> BaseColorFromString(std::string_view name);
std::string_view AccentToString(Accent accent);
std::optional<Accent> AccentFromString(std::string_view name);

}  // namespace koan::look

#endif  // KOAN_BROWSER_UI_COLOR_KOAN_TOKENS_H_
