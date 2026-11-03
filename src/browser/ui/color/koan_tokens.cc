// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// The palettes (koan_shadcn_palettes.inc) are shadcn/ui's, MIT, Copyright (c)
// 2023 shadcn: https://github.com/shadcn-ui/ui

#include "koan/browser/ui/color/koan_tokens.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"

namespace koan::look {

namespace {

#include "koan/browser/ui/color/koan_shadcn_palettes.inc"

constexpr std::array<std::string_view, kTokenCount> kTokenNames = {
    "background",
    "foreground",
    "card",
    "card-foreground",
    "popover",
    "popover-foreground",
    "primary",
    "primary-foreground",
    "secondary",
    "secondary-foreground",
    "muted",
    "muted-foreground",
    "accent",
    "accent-foreground",
    "destructive",
    "border",
    "input",
    "ring",
    "sidebar",
    "sidebar-foreground",
    "sidebar-primary",
    "sidebar-primary-foreground",
    "sidebar-accent",
    "sidebar-accent-foreground",
    "sidebar-border",
    "sidebar-ring",
};

std::span<const OklchColor, kTokenCount> BasePalette(BaseColor base,
                                                     bool dark) {
  switch (base) {
    case BaseColor::kBlack:
      return dark ? kBlackDark : kBlackLight;
    case BaseColor::kNeutral:
      return dark ? kNeutralDark : kNeutralLight;
    case BaseColor::kZinc:
      return dark ? kZincDark : kZincLight;
    case BaseColor::kSlate:
      return dark ? kSlateDark : kSlateLight;
    case BaseColor::kStone:
      return dark ? kStoneDark : kStoneLight;
    case BaseColor::kGray:
      return dark ? kGrayDark : kGrayLight;
  }
  return dark ? kBlackDark : kBlackLight;
}

std::span<const AccentOverride> AccentOverrides(Accent accent, bool dark) {
  switch (accent) {
    case Accent::kNone:
      return {};
    case Accent::kBlue:
      return dark ? std::span<const AccentOverride>(kBlueDark)
                  : std::span<const AccentOverride>(kBlueLight);
    case Accent::kGreen:
      return dark ? std::span<const AccentOverride>(kGreenDark)
                  : std::span<const AccentOverride>(kGreenLight);
    case Accent::kOrange:
      return dark ? std::span<const AccentOverride>(kOrangeDark)
                  : std::span<const AccentOverride>(kOrangeLight);
    case Accent::kRed:
      return dark ? std::span<const AccentOverride>(kRedDark)
                  : std::span<const AccentOverride>(kRedLight);
    case Accent::kRose:
      return dark ? std::span<const AccentOverride>(kRoseDark)
                  : std::span<const AccentOverride>(kRoseLight);
    case Accent::kViolet:
      return dark ? std::span<const AccentOverride>(kVioletDark)
                  : std::span<const AccentOverride>(kVioletLight);
    case Accent::kYellow:
      return dark ? std::span<const AccentOverride>(kYellowDark)
                  : std::span<const AccentOverride>(kYellowLight);
  }
  return {};
}

// --- Parsing CSS ---

std::optional<Token> TokenFromCssName(std::string_view name) {
  for (int i = 0; i < kTokenCount; ++i) {
    if (kTokenNames[i] == name) {
      return static_cast<Token>(i);
    }
  }
  return std::nullopt;
}

// A number, optionally with a percent sign or a unit.
struct Number {
  double value = 0;
  bool percent = false;
};

std::optional<Number> ParseNumber(std::string_view text) {
  text = base::TrimWhitespaceASCII(text, base::TRIM_ALL);
  if (text == "none") {
    return Number{};
  }
  Number number;
  if (text.ends_with('%')) {
    number.percent = true;
    text.remove_suffix(1);
  } else if (text.ends_with("deg")) {
    text.remove_suffix(3);
  }
  if (!base::StringToDouble(text, &number.value) ||
      !std::isfinite(number.value)) {
    return std::nullopt;
  }
  return number;
}

// The arguments of a CSS colour function or a bare `H S% L%` list: split at
// spaces, commas and the slash that introduces the alpha.
struct Arguments {
  std::vector<std::string> values;
  std::optional<std::string> alpha;
};

Arguments SplitArguments(std::string_view text) {
  Arguments result;
  std::string_view colours = text;
  const size_t slash = text.find('/');
  if (slash != std::string_view::npos) {
    colours = text.substr(0, slash);
    result.alpha = std::string(
        base::TrimWhitespaceASCII(text.substr(slash + 1), base::TRIM_ALL));
  }
  for (std::string_view part :
       base::SplitStringPiece(colours, " ,\t\n", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    result.values.emplace_back(part);
  }
  return result;
}

std::optional<float> ParseAlpha(const Arguments& arguments) {
  if (!arguments.alpha) {
    return 1.0f;
  }
  const std::optional<Number> number = ParseNumber(*arguments.alpha);
  if (!number) {
    return std::nullopt;
  }
  return static_cast<float>(std::clamp(
      number->percent ? number->value / 100 : number->value, 0.0, 1.0));
}

// hsl(222.2 84% 4.9% / 50%), hsl(222, 84%, 4.9%) and shadcn's bare
// `222.2 84% 4.9%`.
std::optional<SkColor> ParseHsl(std::string_view text) {
  Arguments arguments = SplitArguments(text);
  // The legacy hsla(): the alpha is a fourth value.
  if (arguments.values.size() == 4 && !arguments.alpha) {
    arguments.alpha = arguments.values[3];
    arguments.values.pop_back();
  }
  if (arguments.values.size() != 3) {
    return std::nullopt;
  }
  const auto h = ParseNumber(arguments.values[0]);
  const auto s = ParseNumber(arguments.values[1]);
  const auto l = ParseNumber(arguments.values[2]);
  const auto a = ParseAlpha(arguments);
  if (!h || !s || !l || !a) {
    return std::nullopt;
  }
  // Saturation and lightness are percentages with or without the sign (CSS
  // Color 4 allows both, and shadcn's own themes sometimes drop it).
  return HslToSkColor(static_cast<float>(h->value),
                      static_cast<float>(s->value / 100),
                      static_cast<float>(l->value / 100), *a);
}

std::optional<SkColor> ParseOklch(std::string_view text) {
  const Arguments arguments = SplitArguments(text);
  if (arguments.values.size() != 3) {
    return std::nullopt;
  }
  const auto l = ParseNumber(arguments.values[0]);
  const auto c = ParseNumber(arguments.values[1]);
  const auto h = ParseNumber(arguments.values[2]);
  const auto a = ParseAlpha(arguments);
  if (!l || !c || !h || !a) {
    return std::nullopt;
  }
  return OklchToSkColor(
      {static_cast<float>(l->percent ? l->value / 100 : l->value),
       // CSS: 100% chroma is 0.4.
       static_cast<float>(c->percent ? c->value / 100 * 0.4 : c->value),
       static_cast<float>(h->value), *a});
}

std::optional<SkColor> ParseRgb(std::string_view text) {
  Arguments arguments = SplitArguments(text);
  if (arguments.values.size() == 4 && !arguments.alpha) {
    arguments.alpha = arguments.values[3];
    arguments.values.pop_back();
  }
  if (arguments.values.size() != 3) {
    return std::nullopt;
  }
  std::array<uint8_t, 3> channels;
  for (int i = 0; i < 3; ++i) {
    const auto n = ParseNumber(arguments.values[i]);
    if (!n) {
      return std::nullopt;
    }
    const double v = n->percent ? n->value / 100 * 255 : n->value;
    channels[i] = static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 255.0)));
  }
  const auto a = ParseAlpha(arguments);
  if (!a) {
    return std::nullopt;
  }
  return SkColorSetARGB(static_cast<uint8_t>(std::lround(*a * 255)),
                        channels[0], channels[1], channels[2]);
}

std::optional<SkColor> ParseHex(std::string_view text) {
  text.remove_prefix(1);
  // #rgb and #rgba repeat each digit.
  std::string expanded;
  if (text.size() == 3 || text.size() == 4) {
    for (char c : text) {
      expanded += c;
      expanded += c;
    }
    text = expanded;
  }
  std::vector<uint8_t> bytes;
  if ((text.size() != 6 && text.size() != 8) ||
      !base::HexStringToBytes(text, &bytes)) {
    return std::nullopt;
  }
  return SkColorSetARGB(bytes.size() == 4 ? bytes[3] : 255, bytes[0], bytes[1],
                        bytes[2]);
}

// The part of `value` inside the parentheses of `function(…)`.
std::optional<std::string_view> FunctionArguments(std::string_view value,
                                                  std::string_view function) {
  if (!base::StartsWith(value, function,
                        base::CompareCase::INSENSITIVE_ASCII)) {
    return std::nullopt;
  }
  value.remove_prefix(function.size());
  value = base::TrimWhitespaceASCII(value, base::TRIM_LEADING);
  if (value.size() < 2 || value.front() != '(' || value.back() != ')') {
    return std::nullopt;
  }
  return value.substr(1, value.size() - 2);
}

std::optional<SkColor> ParseColor(std::string_view value) {
  value = base::TrimWhitespaceASCII(value, base::TRIM_ALL);
  if (value.empty()) {
    return std::nullopt;
  }
  if (value.front() == '#') {
    return ParseHex(value);
  }
  if (auto args = FunctionArguments(value, "oklch")) {
    return ParseOklch(*args);
  }
  if (auto args = FunctionArguments(value, "hsla")) {
    return ParseHsl(*args);
  }
  if (auto args = FunctionArguments(value, "hsl")) {
    return ParseHsl(*args);
  }
  if (auto args = FunctionArguments(value, "rgba")) {
    return ParseRgb(*args);
  }
  if (auto args = FunctionArguments(value, "rgb")) {
    return ParseRgb(*args);
  }
  // shadcn before v4: `222.2 84% 4.9%`.
  if (value.find('%') != std::string_view::npos &&
      value.find('(') == std::string_view::npos) {
    return ParseHsl(value);
  }
  return std::nullopt;
}

// `0.5rem`, `8px` or a bare number of pixels.
std::optional<float> ParseRadius(std::string_view value) {
  value = base::TrimWhitespaceASCII(value, base::TRIM_ALL);
  double scale = 1;
  if (value.ends_with("rem")) {
    value.remove_suffix(3);
    scale = 16;
  } else if (value.ends_with("px")) {
    value.remove_suffix(2);
  }
  double number = 0;
  if (!base::StringToDouble(value, &number) || !std::isfinite(number) ||
      number < 0) {
    return std::nullopt;
  }
  return static_cast<float>(std::min(number * scale, 64.0));
}

std::string RemoveComments(std::string_view css) {
  std::string result;
  result.reserve(css.size());
  for (size_t i = 0; i < css.size(); ++i) {
    if (css.compare(i, 2, "/*") == 0) {
      const size_t end = css.find("*/", i + 2);
      if (end == std::string_view::npos) {
        break;
      }
      i = end + 1;
      continue;
    }
    result += css[i];
  }
  return result;
}

}  // namespace

std::string_view TokenCssName(Token token) {
  return kTokenNames[static_cast<int>(token)];
}

std::optional<CustomTheme> ParseThemeCss(std::string_view css) {
  // Anything longer is not a theme.
  constexpr size_t kMaxLength = 64 * 1024;
  if (css.size() > kMaxLength) {
    return std::nullopt;
  }
  const std::string text = RemoveComments(css);

  CustomTheme theme;
  bool found = false;
  // The selectors of the blocks we are in; a declaration belongs to a dark
  // block if any of them mentions `.dark`.
  std::vector<bool> dark_blocks;
  std::string current;
  auto in_dark = [&] {
    return std::ranges::any_of(dark_blocks, [](bool dark) { return dark; });
  };
  auto declaration = [&](std::string_view decl) {
    const size_t colon = decl.find(':');
    if (colon == std::string_view::npos || dark_blocks.empty()) {
      return;
    }
    std::string_view name =
        base::TrimWhitespaceASCII(decl.substr(0, colon), base::TRIM_ALL);
    std::string_view value =
        base::TrimWhitespaceASCII(decl.substr(colon + 1), base::TRIM_ALL);
    if (!name.starts_with("--")) {
      return;
    }
    name.remove_prefix(2);
    if (const size_t important = value.find("!important");
        important != std::string_view::npos) {
      value =
          base::TrimWhitespaceASCII(value.substr(0, important), base::TRIM_ALL);
    }
    if (name == "radius") {
      if (auto radius = ParseRadius(value)) {
        theme.radius = *radius;
        found = true;
      }
      return;
    }
    const std::optional<Token> token = TokenFromCssName(name);
    if (!token) {
      return;
    }
    if (std::optional<SkColor> color = ParseColor(value)) {
      (in_dark() ? theme.dark : theme.light)[static_cast<int>(*token)] = color;
      found = true;
    }
  };

  for (char c : text) {
    if (c == '{') {
      dark_blocks.push_back(current.find(".dark") != std::string::npos);
      current.clear();
    } else if (c == '}') {
      declaration(current);
      if (!dark_blocks.empty()) {
        dark_blocks.pop_back();
      }
      current.clear();
    } else if (c == ';') {
      declaration(current);
      current.clear();
    } else {
      current += c;
    }
  }
  if (!found) {
    return std::nullopt;
  }
  return theme;
}

namespace {

// Zen's colours (Zen 1.24b's zen-theme.css, with its default accent colour:
// macOS's blue) as tokens.
TokenSet ZenTokens(bool dark, std::optional<SkColor> zen_accent) {
  // Zen's text (Firefox's), its browser background (--zen-main-browser-
  // background), the content area (the branding colour), dialogs
  // (--zen-dialog-background), panels (--arrowpanel-background), the URL bar
  // (--zen-urlbar-background) and a hover (--zen-toolbar-element-bg-hover) on
  // the browser background.
  const SkColor foreground =
      dark ? SkColorSetRGB(0xfb, 0xfb, 0xfe) : SkColorSetRGB(0x15, 0x14, 0x1a);
  const SkColor muted_foreground =
      dark ? SkColorSetRGB(0xae, 0xae, 0xb2) : SkColorSetRGB(0x5b, 0x5b, 0x66);
  const SkColor browser =
      dark ? SkColorSetRGB(0x1b, 0x1b, 0x1b) : SkColorSetRGB(0xeb, 0xeb, 0xeb);
  const SkColor content =
      dark ? SkColorSetRGB(0x10, 0x10, 0x10) : SkColorSetRGB(0xfa, 0xfb, 0xff);
  const SkColor dialog =
      dark ? SkColorSetRGB(0x1c, 0x1c, 0x1c) : SkColorSetRGB(0xfa, 0xfb, 0xff);
  const SkColor panel =
      dark ? SkColorSetRGB(0x1f, 0x1f, 0x1f) : SkColorSetRGB(0xf4, 0xf4, 0xf4);
  const SkColor urlbar =
      dark ? SkColorSetRGB(0x18, 0x18, 0x18) : SkColorSetRGB(0xf4, 0xf4, 0xf4);
  const SkColor hover =
      dark ? SkColorSetRGB(0x32, 0x32, 0x32) : SkColorSetRGB(0xd8, 0xd8, 0xd8);
  const SkColor muted =
      dark ? SkColorSetRGB(0x2b, 0x2b, 0x2b) : SkColorSetRGB(0xe0, 0xe0, 0xe2);
  // --zen-colors-border-contrast.
  const SkColor border =
      SkColorSetA(dark ? SK_ColorWHITE : SK_ColorBLACK, 0x1c);
  const SkColor accent =
      dark ? SkColorSetRGB(10, 132, 255) : SkColorSetRGB(0, 122, 255);
  const SkColor on_accent = SkColorSetRGB(0xfb, 0xfb, 0xfe);
  const SkColor destructive =
      dark ? SkColorSetRGB(0xff, 0x6b, 0x6b) : SkColorSetRGB(0xd7, 0x00, 0x22);
  TokenSet tokens;
  auto set = [&](Token token, SkColor color) {
    tokens.colors[static_cast<int>(token)] = color;
  };
  set(Token::kBackground, content);
  set(Token::kForeground, foreground);
  set(Token::kCard, dialog);
  set(Token::kCardForeground, foreground);
  set(Token::kPopover, panel);
  set(Token::kPopoverForeground, foreground);
  set(Token::kPrimary, accent);
  set(Token::kPrimaryForeground, on_accent);
  set(Token::kSecondary, hover);
  set(Token::kSecondaryForeground, foreground);
  set(Token::kMuted, muted);
  set(Token::kMutedForeground, muted_foreground);
  set(Token::kAccent, hover);
  set(Token::kAccentForeground, foreground);
  set(Token::kDestructive, destructive);
  set(Token::kBorder, border);
  set(Token::kInput, urlbar);
  set(Token::kRing, accent);
  set(Token::kSidebar, browser);
  set(Token::kSidebarForeground, foreground);
  set(Token::kSidebarPrimary, accent);
  set(Token::kSidebarPrimaryForeground, on_accent);
  set(Token::kSidebarAccent, hover);
  set(Token::kSidebarAccentForeground, foreground);
  set(Token::kSidebarBorder, border);
  set(Token::kSidebarRing, accent);
  // --zen-border-radius.
  tokens.radius = 7;
  return tokens;
}

}  // namespace

TokenSet ResolveTokens(const Look& look, bool dark) {
  if (look.style == Style::kZen) {
    return ZenTokens(dark, look.zen_accent);
  }
  TokenSet tokens;
  const std::span<const OklchColor, kTokenCount> palette =
      BasePalette(look.base, dark);
  for (int i = 0; i < kTokenCount; ++i) {
    tokens.colors[i] = OklchToSkColor(palette[i]);
  }
  for (const AccentOverride& override_ : AccentOverrides(look.accent, dark)) {
    tokens.colors[static_cast<int>(override_.token)] =
        OklchToSkColor(override_.color);
  }
  if (look.custom) {
    const auto& overrides = dark ? look.custom->dark : look.custom->light;
    for (int i = 0; i < kTokenCount; ++i) {
      if (overrides[i]) {
        tokens.colors[i] = *overrides[i];
      }
    }
    if (look.custom->radius) {
      tokens.radius = *look.custom->radius;
    }
  }
  return tokens;
}

namespace {
int RoundedRadius(float radius) {
  return std::max(0, static_cast<int>(std::lround(radius)));
}
}  // namespace

int RadiusSm(const TokenSet& tokens) {
  return RoundedRadius(tokens.radius - 4);
}
int RadiusMd(const TokenSet& tokens) {
  return RoundedRadius(tokens.radius - 2);
}
int RadiusLg(const TokenSet& tokens) {
  return RoundedRadius(tokens.radius);
}
int RadiusXl(const TokenSet& tokens) {
  return RoundedRadius(tokens.radius + 4);
}

std::string TokensToCss(const TokenSet& tokens) {
  std::string css;
  for (int i = 0; i < kTokenCount; ++i) {
    const SkColor color = tokens.colors[i];
    const std::string value =
        SkColorGetA(color) == 0xff
            ? base::StringPrintf("#%02x%02x%02x", SkColorGetR(color),
                                 SkColorGetG(color), SkColorGetB(color))
            : base::StringPrintf("rgb(%d %d %d / %.3g)", SkColorGetR(color),
                                 SkColorGetG(color), SkColorGetB(color),
                                 SkColorGetA(color) / 255.0);
    css += base::StringPrintf(
        "--%s: %s;\n", std::string(kTokenNames[i]).c_str(), value.c_str());
  }
  css += base::StringPrintf("--radius: %gpx;\n", tokens.radius);
  return css;
}

namespace {

template <typename T, size_t N>
std::string_view ToName(const std::pair<T, std::string_view> (&names)[N],
                        T value) {
  for (const auto& [v, name] : names) {
    if (v == value) {
      return name;
    }
  }
  return names[0].second;
}

template <typename T, size_t N>
std::optional<T> FromName(const std::pair<T, std::string_view> (&names)[N],
                          std::string_view name) {
  for (const auto& [v, n] : names) {
    if (n == name) {
      return v;
    }
  }
  return std::nullopt;
}

constexpr std::pair<Style, std::string_view> kStyleNames[] = {
    {Style::kShadcn, "shadcn"},
    {Style::kZen, "zen"},
};
constexpr std::pair<Mode, std::string_view> kModeNames[] = {
    {Mode::kAuto, "auto"},
    {Mode::kLight, "light"},
    {Mode::kDark, "dark"},
};
constexpr std::pair<BaseColor, std::string_view> kBaseNames[] = {
    {BaseColor::kBlack, "black"}, {BaseColor::kNeutral, "neutral"},
    {BaseColor::kZinc, "zinc"},   {BaseColor::kSlate, "slate"},
    {BaseColor::kStone, "stone"}, {BaseColor::kGray, "gray"},
};
constexpr std::pair<Accent, std::string_view> kAccentNames[] = {
    {Accent::kNone, "none"},     {Accent::kBlue, "blue"},
    {Accent::kGreen, "green"},   {Accent::kOrange, "orange"},
    {Accent::kRed, "red"},       {Accent::kRose, "rose"},
    {Accent::kViolet, "violet"}, {Accent::kYellow, "yellow"},
};

}  // namespace

std::string_view StyleToString(Style style) {
  return ToName(kStyleNames, style);
}
std::optional<Style> StyleFromString(std::string_view name) {
  return FromName(kStyleNames, name);
}
std::string_view ModeToString(Mode mode) {
  return ToName(kModeNames, mode);
}
std::optional<Mode> ModeFromString(std::string_view name) {
  return FromName(kModeNames, name);
}
std::string_view BaseColorToString(BaseColor base) {
  return ToName(kBaseNames, base);
}
std::optional<BaseColor> BaseColorFromString(std::string_view name) {
  return FromName(kBaseNames, name);
}
std::string_view AccentToString(Accent accent) {
  return ToName(kAccentNames, accent);
}
std::optional<Accent> AccentFromString(std::string_view name) {
  return FromName(kAccentNames, name);
}

}  // namespace koan::look
