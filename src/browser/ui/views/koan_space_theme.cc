// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenGradientGenerator.mjs
//   browser.xhtml (preset swatches)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_space_theme.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "ui/gfx/geometry/vector2d_f.h"

namespace koan::theme {

namespace {

constexpr char kExplicitLightness[] = "explicit-lightness";
constexpr char kExplicitBlackWhite[] = "explicit-black-white";

constexpr Rgb kBlack = {0, 0, 0};
constexpr Rgb kWhite = {255, 255, 255};
// How much accent the Colorful dark style mixes into dark surfaces.
constexpr double kColorfulAccentPercent = 15;
// Zen's gradient angle (getGradient's `rotation`).
constexpr float kRotation = -30;

// JavaScript's Math.round (halves round up, also for negative numbers).
double JsRound(double value) {
  return std::floor(value + 0.5);
}

double Degrees(double radians) {
  return radians * 180 / std::numbers::pi;
}

double Radians(double degrees) {
  return degrees * std::numbers::pi / 180;
}

const char* DotTypeName(DotType type) {
  switch (type) {
    case DotType::kNone:
      return "";
    case DotType::kExplicitLightness:
      return kExplicitLightness;
    case DotType::kExplicitBlackWhite:
      return kExplicitBlackWhite;
  }
  return "";
}

DotType DotTypeFromName(const std::string& name) {
  if (name == kExplicitLightness) {
    return DotType::kExplicitLightness;
  }
  if (name == kExplicitBlackWhite) {
    return DotType::kExplicitBlackWhite;
  }
  return DotType::kNone;
}

double HueToRgb(double p, double q, double t) {
  if (t < 0) {
    t += 1;
  }
  if (t > 1) {
    t -= 1;
  }
  if (t < 1.0 / 6) {
    return p + (q - p) * 6 * t;
  }
  if (t < 1.0 / 2) {
    return q;
  }
  if (t < 2.0 / 3) {
    return p + (q - p) * (2.0 / 3 - t) * 6;
  }
  return p;
}

// The colour a theme colour paints (Zen's #getSingleRGBColor). Koan's window
// is opaque, so the colour is blended into the base by the theme's opacity,
// fully opaque, as Zen does for an opaque window.
SkColor4f SingleColor(const Environment& env,
                      const SpaceTheme& theme,
                      const ThemeColor& color) {
  if (color.is_custom) {
    return SkColor4f::FromColor(color.custom);
  }
  return SkColor4f::FromColor(ToSkColor(
      BlendColors(color.c, ToolbarBase(env, theme), theme.opacity * 100)));
}

GradientLayer Solid(SkColor4f color) {
  GradientLayer layer;
  layer.stops = {{color, 0}};
  return layer;
}

GradientLayer Linear(float angle, std::vector<GradientLayer::Stop> stops) {
  GradientLayer layer;
  layer.kind = GradientLayer::Kind::kLinear;
  layer.angle = angle;
  layer.stops = std::move(stops);
  return layer;
}

GradientLayer Radial(gfx::PointF center,
                     std::vector<GradientLayer::Stop> stops) {
  GradientLayer layer;
  layer.kind = GradientLayer::Kind::kRadial;
  layer.center = center;
  layer.stops = std::move(stops);
  return layer;
}

constexpr SkColor4f kTransparent = {0, 0, 0, 0};

std::string DescribeColor(const SkColor4f& color) {
  if (color.fA == 0) {
    return "transparent";
  }
  const SkColor c = color.toSkColor();
  return base::StringPrintf(
      "rgba(%d, %d, %d, %s)", SkColorGetR(c), SkColorGetG(c), SkColorGetB(c),
      base::NumberToString(JsRound(color.fA * 1000) / 1000).c_str());
}

std::string DescribePercent(double fraction) {
  return base::StrCat(
      {base::NumberToString(JsRound(fraction * 1000) / 10), "%"});
}

// Zen's sine path for the opacity slider.
constexpr char kSinePath[] =
    "M 51.373 27.395 C 60.14 -8.503 68.906 -8.503 77.671 27.395 C 86.438 "
    "63.293 95.205 63.293 103.971 27.395 C 112.738 -8.503 121.504 -8.503 "
    "130.271 27.395 C 139.037 63.293 147.803 63.293 156.57 27.395 C 165.335 "
    "-8.503 174.101 -8.503 182.868 27.395 C 191.634 63.293 200.4 63.293 "
    "209.167 27.395 C 217.933 -8.503 226.7 -8.503 235.467 27.395 C 244.233 "
    "63.293 252.999 63.293 261.765 27.395 C 270.531 -8.503 279.297 -8.503 "
    "288.064 27.395 C 296.83 63.293 305.596 63.293 314.363 27.395 C 323.13 "
    "-8.503 331.896 -8.503 340.662 27.395 M 314.438 27.395 C 323.204 -8.503 "
    "331.97 -8.503 340.737 27.395 C 349.503 63.293 358.27 63.293 367.037 "
    "27.395";

// Zen's parseSinePath().
std::vector<WaveSegment> ParseSinePath() {
  std::vector<WaveSegment> segments;
  char command = 0;
  std::vector<float> numbers;
  auto flush = [&] {
    if (command == 'M' && numbers.size() >= 2) {
      segments.push_back({'M', {numbers[0], numbers[1]}});
    } else if (command == 'C') {
      for (size_t i = 0; i + 6 <= numbers.size(); i += 6) {
        segments.push_back({'C',
                            {numbers[i], numbers[i + 1], numbers[i + 2],
                             numbers[i + 3], numbers[i + 4], numbers[i + 5]}});
      }
    }
    numbers.clear();
  };
  for (std::string_view token : base::SplitStringPiece(
           kSinePath, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (token == "M" || token == "C") {
      flush();
      command = token[0];
      continue;
    }
    double value;
    if (base::StringToDouble(token, &value)) {
      numbers.push_back(value);
    }
  }
  flush();
  return segments;
}

}  // namespace

ThemeColor::ThemeColor() = default;
ThemeColor::ThemeColor(const ThemeColor&) = default;
ThemeColor& ThemeColor::operator=(const ThemeColor&) = default;
ThemeColor::~ThemeColor() = default;
bool ThemeColor::operator==(const ThemeColor&) const = default;

SpaceTheme::SpaceTheme() = default;
SpaceTheme::SpaceTheme(const SpaceTheme&) = default;
SpaceTheme& SpaceTheme::operator=(const SpaceTheme&) = default;
SpaceTheme::~SpaceTheme() = default;
bool SpaceTheme::operator==(const SpaceTheme&) const = default;

base::DictValue SpaceTheme::ToDict() const {
  base::ListValue list;
  for (const ThemeColor& color : colors) {
    base::DictValue entry;
    if (color.is_custom) {
      entry.Set("c", HexColor(color.custom));
    } else {
      entry.Set("c", base::ListValue()
                         .Append(color.c[0])
                         .Append(color.c[1])
                         .Append(color.c[2]));
    }
    entry.Set("isCustom", color.is_custom);
    entry.Set("algorithm", color.algorithm);
    entry.Set("isPrimary", color.is_primary);
    entry.Set("lightness", color.lightness);
    if (color.position) {
      entry.Set("position", base::DictValue()
                                .Set("x", color.position->x())
                                .Set("y", color.position->y()));
    }
    if (color.type != DotType::kNone) {
      entry.Set("type", DotTypeName(color.type));
    }
    list.Append(std::move(entry));
  }
  return base::DictValue()
      .Set("family", kZenGradientFamily)
      .Set("version", kThemeVersion)
      .Set("gradientColors", std::move(list))
      .Set("opacity", opacity)
      .Set("texture", texture);
}

// static
SpaceTheme SpaceTheme::FromDict(const base::DictValue& dict) {
  SpaceTheme theme;
  // A pref can hold anything: keep the values in their ranges.
  const auto number = [](std::optional<double> value, double fallback,
                         double min, double max) {
    return value && std::isfinite(*value) ? std::clamp(*value, min, max)
                                          : fallback;
  };
  theme.opacity = number(dict.FindDouble("opacity"), 0.5, 0, 1);
  theme.texture = number(dict.FindDouble("texture"), 0, 0, 1);
  const base::ListValue* list = dict.FindList("gradientColors");
  if (!list) {
    return theme;
  }
  for (const base::Value& value : *list) {
    const base::DictValue* entry = value.GetIfDict();
    if (!entry) {
      continue;
    }
    ThemeColor color;
    color.is_custom = entry->FindBool("isCustom").value_or(false);
    if (color.is_custom) {
      const std::string* hex = entry->FindString("c");
      std::optional<SkColor> parsed = hex ? ParseHexColor(*hex) : std::nullopt;
      if (!parsed) {
        continue;
      }
      color.custom = *parsed;
    } else {
      const base::ListValue* c = entry->FindList("c");
      if (!c || c->size() != 3) {
        continue;
      }
      for (size_t i = 0; i < 3; ++i) {
        color.c[i] =
            static_cast<int>(JsRound(number((*c)[i].GetIfDouble(), 0, 0, 255)));
      }
    }
    if (const std::string* algorithm = entry->FindString("algorithm")) {
      color.algorithm = *algorithm;
    }
    color.is_primary = entry->FindBool("isPrimary").value_or(false);
    color.lightness = number(entry->FindDouble("lightness"), 50, 0, 100);
    if (const base::DictValue* position = entry->FindDict("position")) {
      // Within reach of the colour field (Zen's is 358px).
      constexpr double kMaxPosition = 1000;
      color.position =
          gfx::PointF(number(position->FindDouble("x"), 0, 0, kMaxPosition),
                      number(position->FindDouble("y"), 0, 0, kMaxPosition));
    }
    if (const std::string* type = entry->FindString("type")) {
      color.type = DotTypeFromName(*type);
    }
    theme.colors.push_back(std::move(color));
  }
  return theme;
}

std::string HexColor(SkColor color) {
  std::string hex = base::StringPrintf("#%02x%02x%02x", SkColorGetR(color),
                                       SkColorGetG(color), SkColorGetB(color));
  if (SkColorGetA(color) != 0xff) {
    hex += base::StringPrintf("%02x", SkColorGetA(color));
  }
  return hex;
}

std::optional<SkColor> ParseHexColor(std::string_view hex) {
  if (!hex.starts_with('#')) {
    return std::nullopt;
  }
  hex.remove_prefix(1);
  std::vector<uint8_t> bytes;
  if (hex.size() == 3) {
    std::string expanded;
    for (char c : hex) {
      expanded += {c, c};
    }
    if (!base::HexStringToBytes(expanded, &bytes)) {
      return std::nullopt;
    }
  } else if ((hex.size() != 6 && hex.size() != 8) ||
             !base::HexStringToBytes(hex, &bytes)) {
    return std::nullopt;
  }
  return SkColorSetARGB(bytes.size() == 4 ? bytes[3] : 0xff, bytes[0], bytes[1],
                        bytes[2]);
}

Rgb HslToRgb(double h, double s, double l) {
  double r, g, b;
  if (s == 0) {
    r = g = b = l;
  } else {
    const double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    const double p = 2 * l - q;
    r = HueToRgb(p, q, h + 1.0 / 3);
    g = HueToRgb(p, q, h);
    b = HueToRgb(p, q, h - 1.0 / 3);
  }
  return {static_cast<int>(JsRound(r * 255)),
          static_cast<int>(JsRound(g * 255)),
          static_cast<int>(JsRound(b * 255))};
}

std::array<double, 3> RgbToHsl(const Rgb& rgb) {
  const double r = rgb[0] / 255.0;
  const double g = rgb[1] / 255.0;
  const double b = rgb[2] / 255.0;
  const double max = std::max({r, g, b});
  const double min = std::min({r, g, b});
  const double d = max - min;
  double h = 0;
  if (d == 0) {
    h = 0;
  } else if (max == r) {
    h = std::fmod((g - b) / d, 6);
  } else if (max == g) {
    h = (b - r) / d + 2;
  } else {
    h = (r - g) / d + 4;
  }
  const double l = (min + max) / 2;
  const double s = d == 0 ? 0 : d / (1 - std::abs(2 * l - 1));
  return {h * 60, s, l};
}

Rgb BlendColors(const Rgb& a, const Rgb& b, double percentage) {
  const double p = percentage / 100;
  Rgb result;
  for (size_t i = 0; i < 3; ++i) {
    result[i] = static_cast<int>(JsRound(a[i] * p + b[i] * (1 - p)));
  }
  return result;
}

double Luminance(const Rgb& rgb) {
  std::array<double, 3> channels;
  for (size_t i = 0; i < 3; ++i) {
    const double v = rgb[i] / 255.0;
    channels[i] = v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
  }
  return channels[0] * 0.2126 + channels[1] * 0.7152 + channels[2] * 0.0722;
}

double ContrastRatio(const Rgb& a, const Rgb& b) {
  const double la = Luminance(a);
  const double lb = Luminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

SkColor ToSkColor(const Rgb& rgb, double alpha) {
  return SkColorSetARGB(static_cast<U8CPU>(JsRound(alpha * 255)),
                        std::clamp(rgb[0], 0, 255), std::clamp(rgb[1], 0, 255),
                        std::clamp(rgb[2], 0, 255));
}

Rgb FromSkColor(SkColor color) {
  return {static_cast<int>(SkColorGetR(color)),
          static_cast<int>(SkColorGetG(color)),
          static_cast<int>(SkColorGetB(color))};
}

namespace {

// The colour the Colorful dark style tints dark surfaces with: the theme's
// primary colour where it has one, else the system's accent.
Rgb ColorfulTint(const Environment& env, const SpaceTheme& theme) {
  for (const ThemeColor& color : theme.colors) {
    if (color.is_primary && !color.is_custom) {
      return color.c;
    }
  }
  return NativeAccentColor(env);
}

// A dark surface `color` in the window's dark theme style: black for Night,
// tinted for Colorful (Koan's own styles; Zen 1.23 only names them).
Rgb DarkStyled(const Environment& env, const SpaceTheme& theme, Rgb color) {
  if (!env.dark) {
    return color;
  }
  switch (env.dark_style) {
    case DarkStyle::kDefault:
      return color;
    case DarkStyle::kNight:
      return kBlack;
    case DarkStyle::kColorful:
      return BlendColors(ColorfulTint(env, theme), color,
                         kColorfulAccentPercent);
  }
  return color;
}

}  // namespace

Rgb ToolbarBase(const Environment& env, const SpaceTheme& theme) {
  return DarkStyled(env, theme, env.tint_base);
}

Rgb NativeAccentColor(const Environment& env) {
  const Rgb rgb =
      BlendColors(env.system_accent, env.tint_base, env.dark ? 80 : 50);
  if (env.dark) {
    // Zen uses a darker accent on dark windows.
    return BlendColors(rgb, kBlack, 40);
  }
  return rgb;
}

std::optional<Rgb> DominantColor(const Environment& env,
                                 const SpaceTheme& theme) {
  if (theme.colors.empty()) {
    return std::nullopt;
  }
  // Zen's getPrimaryColor(): the primary colour, else the middle one.
  const auto primary = std::ranges::find_if(
      theme.colors, [](const ThemeColor& c) { return c.is_primary; });
  const ThemeColor& color = primary != theme.colors.end()
                                ? *primary
                                : theme.colors[theme.colors.size() / 2];
  if (color.is_custom) {
    return NativeAccentColor(env);
  }
  return color.c;
}

bool ShouldBeDarkMode(const Environment& env,
                      const SpaceTheme& theme,
                      const Rgb& accent) {
  // The window isn't transparent: the accent is seen through the base.
  const Rgb bg =
      BlendColors(ToolbarBase(env, theme), accent, (1 - theme.opacity) * 100);
  constexpr double kTextOpacity = 0.9;
  // Zen's getToolbarColor() text, composited over the background.
  const Rgb dark_text = BlendColors(bg, kWhite, (1 - kTextOpacity) * 100);
  const Rgb light_text = BlendColors(bg, kBlack, (1 - kTextOpacity) * 100);
  return ContrastRatio(bg, dark_text) > ContrastRatio(bg, light_text);
}

Rgb AccentColorForUI(const Environment& env,
                     const Rgb& accent,
                     bool dark_mode) {
  if (dark_mode) {
    return accent;
  }
  // Greys stay grey (Zen's gh-15142).
  if (accent[0] == accent[1] && accent[1] == accent[2]) {
    return accent;
  }
  const auto [h, s, l] = RgbToHsl(accent);
  const double saturation = std::min(1.0, s + 0.3);
  const double target_lightness = env.dark ? 0.62 : 0.42;
  const double lightness = l * 0.4 + target_lightness * 0.6;
  return HslToRgb(h / 360, saturation, lightness);
}

Appearance GetAppearance(const Environment& env, const SpaceTheme& theme) {
  Appearance appearance;
  std::optional<Rgb> dominant = DominantColor(env, theme);
  const bool is_default = !dominant;
  if (is_default) {
    dominant = NativeAccentColor(env);
  }
  appearance.dark = env.dark;
  if (!is_default) {
    appearance.dark = ShouldBeDarkMode(env, theme, *dominant);
    appearance.explicit_mode = true;
  }
  appearance.primary = AccentColorForUI(env, *dominant, appearance.dark);
  return appearance;
}

GradientLayer::GradientLayer() = default;
GradientLayer::GradientLayer(const GradientLayer&) = default;
GradientLayer& GradientLayer::operator=(const GradientLayer&) = default;
GradientLayer::~GradientLayer() = default;
bool GradientLayer::operator==(const GradientLayer&) const = default;

Background::Background() = default;
Background::Background(const Background&) = default;
Background& Background::operator=(const Background&) = default;
Background::~Background() = default;
bool Background::operator==(const Background&) const = default;

Background GetBackground(const Environment& env, const SpaceTheme& theme) {
  Background background;
  background.grain = theme.texture;
  // Below the theme: the window's own background.
  background.base = ToSkColor(DarkStyled(env, theme, env.window));

  const std::vector<ThemeColor>& colors = theme.colors;
  if (colors.empty()) {
    background.layers.push_back(Solid(SkColor4f::FromColor(
        ToSkColor(DarkStyled(env, theme, env.default_background)))));
    return background;
  }

  auto single = [&](size_t i) { return SingleColor(env, theme, colors[i]); };
  if (colors.size() == 1) {
    background.layers.push_back(Solid(single(0)));
    return background;
  }
  if (std::ranges::any_of(colors,
                          [](const ThemeColor& c) { return c.is_custom; })) {
    // Custom colours: one linear gradient through all colours, evenly.
    std::vector<GradientLayer::Stop> stops;
    for (size_t i = 0; i < colors.size(); ++i) {
      stops.push_back({single(i), static_cast<float>(i) / (colors.size() - 1)});
    }
    background.layers.push_back(Linear(kRotation, std::move(stops)));
    return background;
  }
  if (colors.size() == 2) {
    background.layers.push_back(
        Linear(kRotation + 180, {{single(0), 0.3f}, {kTransparent, 1.2f}}));
    background.layers.push_back(
        Linear(kRotation, {{single(1), 0.3f}, {kTransparent, 1.2f}}));
    return background;
  }
  // Three colours (Zen's maximum).
  background.layers.push_back(
      Linear(-5, {{single(2), 0.1f}, {kTransparent, 0.8f}}));
  background.layers.push_back(
      Radial(gfx::PointF(0.95f, 0), {{single(1), 0}, {kTransparent, 0.75f}}));
  background.layers.push_back(
      Radial(gfx::PointF(0, 0), {{single(0), 0.1f}, {kTransparent, 0.7f}}));
  return background;
}

WindowTokens::WindowTokens() = default;
WindowTokens::WindowTokens(const WindowTokens&) = default;
WindowTokens& WindowTokens::operator=(const WindowTokens&) = default;
WindowTokens::~WindowTokens() = default;
bool WindowTokens::operator==(const WindowTokens&) const = default;

WindowTokens GetZenTokens(const Environment& env, const SpaceTheme& theme) {
  WindowTokens tokens;
  tokens.background = GetBackground(env, theme);
  // Zen's zen-should-be-dark-mode and --zen-primary-color.
  const Appearance appearance = GetAppearance(env, theme);
  if (appearance.explicit_mode) {
    tokens.dark = appearance.dark;
    tokens.colors[Token::kPrimary] = ToSkColor(appearance.primary);
  }
  return tokens;
}

// static
SpaceTheme SpaceTheme::FromStored(const base::DictValue& stored) {
  const std::string* family = stored.FindString("family");
  // A theme written by a newer Koan (a later version of the family) shows
  // as the default theme rather than being misread.
  const int version = stored.FindInt("version").value_or(kThemeVersion);
  return (!family || *family == kZenGradientFamily) && version <= kThemeVersion
             ? FromDict(stored)
             : SpaceTheme();
}

WindowTokens GetTokens(const Environment& env, const base::DictValue& stored) {
  // Zen's gradient themes are the only family so far; a theme of a family
  // this version doesn't know shows as the default theme.
  return GetZenTokens(env, SpaceTheme::FromStored(stored));
}

std::string DescribeBackground(const Background& background) {
  std::vector<std::string> layers;
  for (const GradientLayer& layer : background.layers) {
    if (layer.kind == GradientLayer::Kind::kSolid) {
      layers.push_back(DescribeColor(layer.stops[0].color));
      continue;
    }
    std::vector<std::string> parts;
    if (layer.kind == GradientLayer::Kind::kLinear) {
      parts.push_back(base::StrCat(
          {base::NumberToString(static_cast<int>(layer.angle)), "deg"}));
    } else {
      parts.push_back(
          base::StrCat({"circle at ", DescribePercent(layer.center.x()), " ",
                        DescribePercent(layer.center.y())}));
    }
    for (const GradientLayer::Stop& stop : layer.stops) {
      parts.push_back(base::StrCat(
          {DescribeColor(stop.color), " ", DescribePercent(stop.position)}));
    }
    layers.push_back(base::StrCat({layer.kind == GradientLayer::Kind::kLinear
                                       ? "linear-gradient("
                                       : "radial-gradient(",
                                   base::JoinString(parts, ", "), ")"}));
  }
  return base::JoinString(layers, ", ");
}

const std::vector<Harmony>& Harmonies() {
  static const base::NoDestructor<std::vector<Harmony>> harmonies(
      std::vector<Harmony>{
          {"complementary", {180}},
          {"singleAnalogous", {310}},
          {"splitComplementary", {150, 210}},
          {"analogous", {50, 310}},
          {"triadic", {120, 240}},
          {"floating", {}},
      });
  return *harmonies;
}

const std::vector<std::vector<Preset>>& PresetPages() {
  constexpr DotType kLight = DotType::kExplicitLightness;
  constexpr DotType kBw = DotType::kExplicitBlackWhite;
  auto rgb = [](int r, int g, int b) { return SkColorSetRGB(r, g, b); };
  auto hex = [](uint32_t value) {
    return SkColorSetRGB(value >> 16, (value >> 8) & 0xff, value & 0xff);
  };
  // Zen's swatches (browser.xhtml, PanelUI-zen-gradient-generator-color-
  // pages). Positions are parsed with parseInt, so fractions are dropped.
  static const base::NoDestructor<std::vector<std::vector<Preset>>> pages(
      std::vector<std::vector<Preset>>{
          {
              {90, "float", 1, {240, 240}, kLight, {hex(0xf4efdf)}},
              {80, "float", 1, {233, 157}, kLight, {hex(0xf0b8cd)}},
              {80, "float", 1, {236, 111}, kLight, {hex(0xe9c3e3)}},
              {70, "float", 1, {234, 173}, kLight, {hex(0xda7682)}},
              {70, "float", 1, {220, 187}, kLight, {hex(0xeb8570)}},
              {60, "float", 1, {225, 237}, kLight, {hex(0xdcce7f)}},
              {60, "float", 1, {147, 195}, kLight, {hex(0x5becad)}},
              {50, "float", 1, {81, 84}, kLight, {hex(0x919bb5)}},
          },
          {
              {90,
               "analogous",
               3,
               {240, 240},
               kLight,
               {rgb(245, 237, 214), rgb(221, 243, 216), rgb(243, 216, 225)}},
              {85,
               "analogous",
               3,
               {233, 157},
               kLight,
               {rgb(243, 190, 222), rgb(247, 222, 186), rgb(223, 195, 238)}},
              {80,
               "analogous",
               3,
               {236, 111},
               kLight,
               {rgb(229, 179, 228), rgb(236, 172, 178), rgb(197, 185, 223)}},
              {70,
               "analogous",
               3,
               {234, 173},
               kLight,
               {rgb(235, 122, 159), rgb(239, 239, 118), rgb(210, 133, 224)}},
              {70,
               "analogous",
               3,
               {220, 187},
               kLight,
               {rgb(242, 115, 123), rgb(175, 242, 115), rgb(230, 125, 232)}},
              {60,
               "analogous",
               3,
               {225, 237},
               kLight,
               {rgb(221, 205, 85), rgb(97, 212, 94), rgb(215, 91, 124)}},
              {60,
               "analogous",
               3,
               {147, 195},
               kLight,
               {rgb(75, 231, 210), rgb(84, 175, 222), rgb(62, 244, 112)}},
              {55,
               "analogous",
               3,
               {81, 84},
               kLight,
               {rgb(122, 132, 158), rgb(137, 117, 164), rgb(116, 162, 164)}},
          },
          {
              {10, "float", 1, {171, 72}, kLight, {rgb(93, 86, 106)}},
              {40, "float", 1, {265, 79}, kLight, {hex(0x997096)}},
              {35, "float", 1, {301, 176}, kLight, {hex(0x956066)}},
              {30, "float", 1, {237, 210}, kLight, {hex(0x9c6645)}},
              {30, "float", 1, {91, 228}, kLight, {hex(0x517b6c)}},
              {25, "float", 1, {67, 159}, kLight, {hex(0x576e75)}},
              {20, "float", 1, {314, 235}, kLight, {rgb(131, 109, 95)}},
              {20, "float", 1, {118, 215}, kLight, {hex(0x447464)}},
          },
          {
              {10,
               "analogous",
               3,
               {171, 72},
               kLight,
               {rgb(23, 17, 34), rgb(37, 14, 35), rgb(18, 22, 33)}},
              {40,
               "analogous",
               3,
               {265, 79},
               kLight,
               {rgb(128, 76, 124), rgb(141, 63, 66), rgb(97, 88, 116)}},
              {35,
               "analogous",
               3,
               {301, 176},
               kLight,
               {rgb(122, 56, 64), rgb(126, 121, 52), rgb(111, 68, 110)}},
              {30,
               "analogous",
               3,
               {237, 210},
               kLight,
               {rgb(131, 65, 22), rgb(64, 128, 25), rgb(122, 31, 91)}},
              {30,
               "analogous",
               3,
               {91, 228},
               kLight,
               {rgb(45, 108, 85), rgb(52, 85, 101), rgb(52, 118, 35)}},
              {25,
               "analogous",
               3,
               {67, 159},
               kLight,
               {rgb(45, 74, 83), rgb(46, 50, 81), rgb(38, 90, 65)}},
              {20,
               "analogous",
               3,
               {314, 235},
               kLight,
               {rgb(64, 47, 38), rgb(55, 64, 38), rgb(59, 43, 52)}},
              {20,
               "analogous",
               3,
               {118, 215},
               kLight,
               {rgb(22, 80, 61), rgb(26, 60, 76), rgb(27, 87, 15)}},
          },
          {
              {-1, "float", 1, {340, 180}, kBw, {rgb(224, 224, 224)}},
              {-1, "float", 1, {337, 180}, kBw, {rgb(224, 224, 224)}},
              {-1, "float", 1, {315, 180}, kBw, {rgb(192, 192, 192)}},
              {-1, "float", 1, {292, 180}, kBw, {rgb(160, 160, 160)}},
              {-1, "float", 1, {270, 180}, kBw, {rgb(128, 128, 128)}},
              {-1, "float", 1, {247, 180}, kBw, {rgb(96, 96, 96)}},
              {-1, "float", 1, {225, 180}, kBw, {rgb(64, 64, 64)}},
              {-1, "float", 1, {202, 180}, kBw, {rgb(32, 32, 32)}},
              {-1, "float", 1, {180, 180}, kBw, {rgb(0, 0, 0)}},
          },
      });
  return *pages;
}

PickerModel::PickerModel(float field_size) : field_size_(field_size) {}

PickerModel::~PickerModel() = default;

void PickerModel::Load(const SpaceTheme& theme) {
  dots_.clear();
  custom_colors_.clear();
  opacity_ = theme.opacity;
  texture_ = theme.texture;
  use_algo_ = theme.colors.empty() ? "" : theme.colors[0].algorithm;
  current_lightness_ = theme.colors.empty() ? 50 : theme.colors[0].lightness;
  for (const ThemeColor& color : theme.colors) {
    if (color.is_custom) {
      custom_colors_.push_back(color.custom);
      continue;
    }
    Dot dot;
    dot.id = static_cast<int>(dots_.size());
    dot.position = color.position.value_or(InitialPosition(color.c));
    dot.type = color.type;
    dot.lightness = color.lightness;
    dot.color = color.c;
    dot.primary = color.is_primary || dots_.empty();
    dot.serial = next_serial_++;
    dot.spawn_position = dot.position;
    dots_.push_back(dot);
  }
}

SpaceTheme PickerModel::ToTheme() const {
  SpaceTheme theme;
  theme.opacity = opacity_;
  theme.texture = texture_;
  std::vector<const Dot*> ordered;
  for (const Dot& dot : dots_) {
    ordered.push_back(&dot);
  }
  std::ranges::sort(ordered, {}, &Dot::serial);
  for (const Dot* dot : ordered) {
    ThemeColor color;
    color.c = dot->color;
    color.algorithm = use_algo_;
    color.is_primary = dot->primary;
    color.lightness = current_lightness_;
    color.position = dot->position;
    color.type = dot->type;
    theme.colors.push_back(std::move(color));
  }
  for (SkColor custom : custom_colors_) {
    ThemeColor color;
    color.is_custom = true;
    color.custom = custom;
    color.algorithm = use_algo_;
    color.lightness = current_lightness_;
    theme.colors.push_back(std::move(color));
  }
  // Zen's fixTheme(): there is always a primary colour.
  if (!theme.colors.empty() &&
      std::ranges::none_of(theme.colors,
                           [](const ThemeColor& c) { return c.is_primary; })) {
    theme.colors[0].is_primary = true;
  }
  return theme;
}

gfx::PointF PickerModel::ClampToCircle(gfx::PointF point) const {
  const float radius = field_size_ / 2;
  const gfx::PointF center(radius, radius);
  const gfx::Vector2dF offset = point - center;
  if (offset.Length() <= radius) {
    return point;
  }
  const double angle = std::atan2(offset.y(), offset.x());
  return gfx::PointF(center.x() + std::cos(angle) * radius,
                     center.y() + std::sin(angle) * radius);
}

const PickerModel::Dot* PickerModel::DraggableDot() const {
  // Only the first dot in the field (Zen's :first-of-type) takes the mouse.
  const auto it = std::ranges::min_element(dots_, {}, &Dot::serial);
  return it == dots_.end() ? nullptr : &*it;
}

PickerModel::Dot* PickerModel::MutableDraggableDot() {
  const auto it = std::ranges::min_element(dots_, {}, &Dot::serial);
  return it == dots_.end() ? nullptr : &*it;
}

bool PickerModel::HitsPrimaryDot(gfx::PointF point) const {
  const Dot* dot = DraggableDot();
  return dot && (point - dot->position).Length() <= kPrimaryDotRadius;
}

bool PickerModel::Click(gfx::PointF point) {
  const gfx::PointF position = ClampToCircle(point);
  if (dots_.empty()) {
    SpawnDot(position, DotType::kNone, /*primary=*/true);
    return true;
  }
  auto primary = std::ranges::find(dots_, 0, &Dot::id);
  if (primary == dots_.end()) {
    return false;
  }
  primary->position = position;
  if (std::optional<std::vector<Dot>> positions =
          CalculateCompliments(dots_, Action::kUpdate, use_algo_)) {
    HandleColorPositions(std::move(*positions));
  }
  return true;
}

void PickerModel::DragTo(gfx::PointF point) {
  Dot* draggable = MutableDraggableDot();
  if (!draggable) {
    return;
  }
  draggable->position = ClampToCircle(point);
  if (std::optional<std::vector<Dot>> positions =
          CalculateCompliments(dots_, Action::kUpdate, use_algo_)) {
    HandleColorPositions(std::move(*positions));
  }
}

void PickerModel::AddDot() {
  if (dots_.size() >= kMaxDots) {
    return;
  }
  if (std::optional<std::vector<Dot>> positions =
          CalculateCompliments(dots_, Action::kAdd, use_algo_)) {
    HandleColorPositions(std::move(*positions));
  }
}

void PickerModel::RemoveDot() {
  std::ranges::stable_sort(dots_, {}, &Dot::id);
  if (dots_.empty()) {
    return;
  }
  dots_.pop_back();
  RenumberDots();
  if (std::optional<std::vector<Dot>> positions =
          CalculateCompliments(dots_, Action::kRemove, "")) {
    HandleColorPositions(std::move(*positions));
  }
}

void PickerModel::RemovePrimaryDot() {
  const Dot* draggable = DraggableDot();
  if (!draggable) {
    return;
  }
  std::erase_if(dots_, [&](const Dot& d) { return &d == draggable; });
  std::ranges::stable_sort(dots_, {}, &Dot::id);
  RenumberDots();
  if (std::optional<std::vector<Dot>> positions =
          CalculateCompliments(dots_, Action::kRemove, "")) {
    HandleColorPositions(std::move(*positions));
  }
}

void PickerModel::RenumberDots() {
  for (size_t i = 0; i < dots_.size(); ++i) {
    dots_[i].id = static_cast<int>(i);
    dots_[i].primary = i == 0;
  }
}

void PickerModel::ToggleAlgorithm() {
  std::vector<const Harmony*> applicable;
  for (const Harmony& harmony : Harmonies()) {
    if (harmony.angles.size() + 1 == dots_.size()) {
      applicable.push_back(&harmony);
    }
  }
  if (applicable.empty()) {
    return;
  }
  const auto current = std::ranges::find_if(
      applicable, [&](const Harmony* h) { return h->type == use_algo_; });
  const size_t next =
      current == applicable.end()
          ? 0
          : (current - applicable.begin() + 1) % applicable.size();
  use_algo_ = applicable[next]->type;
  if (std::optional<std::vector<Dot>> positions =
          CalculateCompliments(dots_, Action::kUpdate, use_algo_)) {
    HandleColorPositions(std::move(*positions));
  }
}

void PickerModel::ApplyPreset(const Preset& preset) {
  if (static_cast<size_t>(preset.num_dots) < dots_.size()) {
    dots_.resize(preset.num_dots);
  }
  std::vector<Dot> dots;
  Dot primary;
  primary.id = 0;
  primary.position = preset.position;
  primary.type = preset.type;
  dots.push_back(primary);
  for (int i = 1; i < preset.num_dots; ++i) {
    Dot dot;
    dot.id = i;
    dot.type = preset.type;
    dots.push_back(dot);
  }
  use_algo_ = preset.algo;
  if (preset.lightness >= 0) {
    current_lightness_ = preset.lightness;
  }
  if (std::optional<std::vector<Dot>> positions =
          CalculateCompliments(std::move(dots), Action::kUpdate, use_algo_)) {
    HandleColorPositions(std::move(*positions));
  }
}

void PickerModel::AddCustomColor(SkColor color) {
  custom_colors_.insert(custom_colors_.begin(), color);
}

void PickerModel::RemoveCustomColor(size_t index) {
  if (index < custom_colors_.size()) {
    custom_colors_.erase(custom_colors_.begin() + index);
  }
}

bool PickerModel::SetTextureFromAngle(double degrees) {
  const double previous = texture_;
  double texture = degrees + 90;
  if (texture < 0) {
    texture += 360;
  }
  texture /= 360;
  // Zen snaps to the closest of 16 steps.
  texture = JsRound(texture * 16) / 16;
  if (texture == 1) {
    texture = 0;
  }
  texture_ = texture;
  return previous != texture_;
}

Rgb PickerModel::ColorFromPosition(gfx::PointF position, DotType type) {
  // Zen's constants: the field plus 30px padding on each side, and half a
  // dot's size.
  constexpr double kPadding = 30;
  constexpr double kDotHalfSize = 29;
  const double x = position.x() + kDotHalfSize;
  const double y = position.y() + kDotHalfSize;
  const double width = field_size_ + kPadding * 2;
  const double height = field_size_ + kPadding * 2;
  const double center_x = width / 2;
  const double center_y = height / 2;
  const double radius = (width - kPadding) / 2;
  const double distance =
      std::sqrt(std::pow(x - center_x, 2) + std::pow(y - center_y, 2)) - 0.2;
  double angle = Degrees(std::atan2(y - center_y, x - center_x));
  if (angle < 0) {
    angle += 360;
  }
  const double normalized_distance = 1 - std::min(distance / radius, 1.0);
  const double hue = angle;
  double saturation = normalized_distance * 100;
  if (type != DotType::kExplicitLightness) {
    saturation = 90 + (1 - normalized_distance) * 10;
    // Further out is lighter.
    current_lightness_ = JsRound((1 - normalized_distance) * 100);
  }
  double lightness = current_lightness_;
  if (type == DotType::kExplicitBlackWhite) {
    saturation = 0;
    lightness = JsRound((1 - normalized_distance) * 100);
  }
  Rgb rgb = HslToRgb(hue / 360, saturation / 100, lightness / 100);
  for (int& channel : rgb) {
    channel = std::clamp(channel, 0, 255);
  }
  return rgb;
}

// static
gfx::PointF PickerModel::InitialPosition(const Rgb& rgb) {
  // Zen hard codes the field's size here (380px).
  constexpr double kSize = 380;
  const double center = kSize / 2;
  const double radius = kSize / 2;
  const auto [hue, saturation, lightness] = RgbToHsl(rgb);
  const double angle = Radians(hue);
  // Zen divides the [0, 1] saturation by 100 again, so colours without a
  // position start near the centre.
  const double normalized_saturation = saturation / 100;
  return gfx::PointF(center + radius * normalized_saturation * std::cos(angle),
                     center + radius * normalized_saturation * std::sin(angle));
}

std::optional<std::vector<PickerModel::Dot>> PickerModel::CalculateCompliments(
    std::vector<Dot> dots,
    Action action,
    const std::string& use_harmony) {
  if (dots.empty()) {
    return std::vector<Dot>();
  }
  const std::vector<Harmony>& harmonies = Harmonies();
  auto find = [&](auto predicate) -> const Harmony* {
    const auto it = std::ranges::find_if(harmonies, predicate);
    return it == harmonies.end() ? nullptr : &*it;
  };
  auto with_angles = [&](size_t count) {
    return find([count](const Harmony& h) { return h.angles.size() == count; });
  };

  // Zen's getColorHarmonyType().
  const int num_dots =
      static_cast<int>(dots.size()) + (action == Action::kAdd      ? 1
                                       : action == Action::kRemove ? -1
                                                                   : 0);
  const Harmony* harmony = nullptr;
  const Harmony* selected =
      use_harmony.empty()
          ? nullptr
          : find([&](const Harmony& h) { return h.type == use_harmony; });
  if (selected) {
    switch (action) {
      case Action::kRemove:
        harmony = dots_.empty()
                      ? with_angles(0)
                      : (selected->angles.empty()
                             ? nullptr
                             : with_angles(selected->angles.size() - 1));
        break;
      case Action::kAdd:
        harmony = with_angles(selected->angles.size() + 1);
        break;
      case Action::kUpdate:
        harmony = selected;
        break;
    }
  } else if (action == Action::kRemove) {
    harmony = num_dots >= 0 ? with_angles(num_dots) : nullptr;
    if (harmony && std::string_view(harmony->type) == "analogous" &&
        num_dots == 1) {
      harmony = find([](const Harmony& h) {
        return std::string_view(h.type) == "singleAnalogous";
      });
    }
  } else {
    harmony = num_dots >= 1 ? with_angles(num_dots - 1) : nullptr;
  }
  // Zen throws here (no harmony fits), which aborts the action.
  if (!harmony) {
    return std::nullopt;
  }
  use_algo_ = harmony->type;
  if (harmony->angles.empty()) {
    return dots;
  }

  const auto primary = std::ranges::find(dots, 0, &Dot::id);
  if (primary == dots.end()) {
    return std::vector<Dot>();
  }
  const gfx::PointF center(field_size_ / 2, field_size_ / 2);
  const gfx::Vector2dF offset = primary->position - center;
  const double base_angle =
      std::fmod(Degrees(std::atan2(offset.y(), offset.x())) + 360, 360);
  const double distance = std::min(static_cast<double>(offset.Length()),
                                   static_cast<double>(field_size_ / 2));

  std::vector<Dot> updated = dots;
  if (!dots_.empty()) {
    Dot first;
    first.id = 0;
    first.position = primary->position;
    first.type = primary->type;
    updated = {first};
  }
  for (size_t i = 0; i < harmony->angles.size(); ++i) {
    const double angle =
        Radians(std::fmod(base_angle + harmony->angles[i], 360));
    Dot dot;
    dot.id = static_cast<int>(i + 1);
    dot.position = gfx::PointF(center.x() + distance * std::cos(angle),
                               center.y() + distance * std::sin(angle));
    dot.type = primary->type;
    updated.push_back(dot);
  }
  return updated;
}

void PickerModel::HandleColorPositions(std::vector<Dot> positions) {
  std::ranges::stable_sort(positions, {}, &Dot::id);
  for (const Dot& position : positions) {
    const auto existing = std::ranges::find(dots_, position.id, &Dot::id);
    if (existing != dots_.end()) {
      existing->type = position.type;
      existing->position = position.position;
      existing->color = ColorFromPosition(position.position, position.type);
      existing->lightness = current_lightness_;
    } else {
      SpawnDot(position.position, position.type, /*primary=*/false);
    }
  }
}

void PickerModel::SpawnDot(gfx::PointF position, DotType type, bool primary) {
  int id = static_cast<int>(dots_.size());
  if (primary) {
    id = 0;
    const auto existing = std::ranges::find(dots_, 0, &Dot::id);
    if (existing != dots_.end()) {
      existing->id = static_cast<int>(dots_.size());
      existing->primary = false;
    }
  }
  Dot dot;
  dot.id = id;
  dot.position = position;
  dot.type = type;
  dot.color = ColorFromPosition(position, type);
  dot.lightness = current_lightness_;
  dot.primary = primary;
  dot.serial = next_serial_++;
  dot.spawn_position = position;
  dots_.push_back(dot);
}

std::vector<WaveSegment> InterpolateWavePath(double progress) {
  static const base::NoDestructor<std::vector<WaveSegment>> sine(
      ParseSinePath());
  if (progress <= 0.001) {
    return {{'M', {kWaveStartX, kWaveCenterY}},
            {'L', {kWaveEndX, kWaveCenterY}}};
  }
  if (progress >= 0.999) {
    return *sine;
  }
  // Zen interpolates towards 27.3, not the line's 27.395.
  constexpr float kReferenceY = 27.3f;
  const float t = progress;
  auto lerp = [&](float y) { return kReferenceY + (y - kReferenceY) * t; };
  std::vector<WaveSegment> path;
  for (const WaveSegment& segment : *sine) {
    WaveSegment interpolated = segment;
    if (segment.type == 'M') {
      interpolated.points[1] = lerp(segment.points[1]);
    } else {
      interpolated.points[1] = lerp(segment.points[1]);
      interpolated.points[3] = lerp(segment.points[3]);
      interpolated.points[5] = lerp(segment.points[5]);
    }
    path.push_back(interpolated);
  }
  return path;
}

}  // namespace koan::theme
