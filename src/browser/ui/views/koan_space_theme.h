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

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_THEME_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_THEME_H_

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/values.h"
#include "build/build_config.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/point_f.h"

// A Space's theme (Zen's gradient themes, ZenGradientGenerator.mjs) and the
// colour maths behind it. Everything here mirrors Zen's formulas and
// constants so that the same theme looks the same in Koan and Zen; the
// comments name the Zen function each part corresponds to.
namespace koan::theme {

// An sRGB colour with 0-255 channels, like Zen's [r, g, b] arrays.
using Rgb = std::array<int, 3>;

// How a dot's position maps to a colour (Zen's data-type).
enum class DotType {
  // Hue from the angle, lightness from the distance (dots placed by hand).
  kNone,
  // Hue from the angle, saturation from the distance, fixed lightness
  // (presets and dots derived from them).
  kExplicitLightness,
  // Greys only: lightness from the distance.
  kExplicitBlackWhite,
};

// One colour of a theme (an entry of Zen's gradientColors).
struct ThemeColor {
  ThemeColor();
  ThemeColor(const ThemeColor&);
  ThemeColor& operator=(const ThemeColor&);
  ~ThemeColor();

  bool operator==(const ThemeColor&) const;

  Rgb c = {0, 0, 0};
  // A custom colour (the custom colour list) is a CSS hex colour, possibly
  // with alpha, used as is.
  bool is_custom = false;
  SkColor custom = SK_ColorTRANSPARENT;
  std::string algorithm;
  bool is_primary = false;
  double lightness = 50;
  // Where the dot sits in the picker's colour field.
  std::optional<gfx::PointF> position;
  DotType type = DotType::kNone;
};

// A Space's theme (Zen's getTheme()). No colours is the default theme.
struct SpaceTheme {
  SpaceTheme();
  SpaceTheme(const SpaceTheme&);
  SpaceTheme& operator=(const SpaceTheme&);
  ~SpaceTheme();

  bool operator==(const SpaceTheme&) const;

  bool is_default() const { return colors.empty(); }

  base::DictValue ToDict() const;
  static SpaceTheme FromDict(const base::DictValue& dict);
  // A stored theme of Zen's family (see GetTokens()); the default theme for
  // another family's.
  static SpaceTheme FromStored(const base::DictValue& stored);
  // The same from a stored theme as JSON (a Space's theme field), the
  // default theme for empty or malformed JSON.
  static SpaceTheme FromStoredJson(std::string_view json);

  // The primary dot's colour (the one the picker's big dot shows), for UI
  // that stands for the theme with one colour; nullopt for the default
  // theme.
  std::optional<SkColor> PrimaryColor() const;

  std::vector<ThemeColor> colors;
  double opacity = 0.5;
  double texture = 0;
};

// Zen's dark theme styles (Settings > Look and Feel). They only change dark
// windows.
enum class DarkStyle {
  kDefault = 0,
  // Night (AMOLED): black instead of dark grey.
  kNight = 1,
  // Colorful: dark surfaces take some of the accent colour.
  kColorful = 2,
};

// The window state the colours depend on.
struct Environment {
  // Zen's isDarkMode: the window's colour scheme (Automatic / Light / Dark).
  bool dark = false;
  DarkStyle dark_style = DarkStyle::kDefault;
  // The system accent colour (Zen's AccentColor).
  Rgb system_accent = {53, 132, 228};
  // The window's own colours, from Koan's colour IDs for the window's scheme
  // (koan_color_mixer.cc; Zen's light values here): the window behind a theme
  // (--zen-themed-toolbar-bg-transparent), the background without a theme,
  // and the surface theme colours are blended into
  // (getToolbarModifiedBaseRaw).
  Rgb window = {0xe2, 0xe2, 0xe2};
  Rgb default_background = {0xe9, 0xe9, 0xe9};
  Rgb tint_base = {240, 240, 244};
  // Zen's canBeTransparent: the window is translucent, the blurred desktop
  // behind it (Koan's glass, koan_glass_state.h). The theme is then drawn
  // with Zen's maths for a transparent window.
  bool transparent = false;
  // Zen's platform branches for a transparent window: macOS lays a white
  // overlay over each colour (blendWithWhiteOverlay()), and its default
  // light theme is fully transparent (Linux: white at 40%).
  bool macos = BUILDFLAG(IS_MAC);
  // zen.theme.dark-mode-bias (0.3): how much a transparent window leans
  // towards dark text.
  double dark_mode_bias = 0.3;
  // The opacity slider's minimum (Zen's lazy.MIN_OPACITY), which the white
  // overlay uses.
  double min_opacity = BUILDFLAG(IS_MAC) ? 0.3 : 0.25;
};

// Colour conversions, as Zen does them (rgbToHsl returns the hue in
// degrees, possibly negative, and saturation and lightness in [0, 1]).
Rgb HslToRgb(double h, double s, double l);
std::array<double, 3> RgbToHsl(const Rgb& rgb);
// Mixes `percentage`% of `a` with the rest of `b` (Zen's blendColors).
Rgb BlendColors(const Rgb& a, const Rgb& b, double percentage);
double Luminance(const Rgb& rgb);
double ContrastRatio(const Rgb& a, const Rgb& b);
SkColor ToSkColor(const Rgb& rgb, double alpha = 1);
Rgb FromSkColor(SkColor color);
// "#rgb", "#rrggbb" or "#rrggbbaa".
std::optional<SkColor> ParseHexColor(std::string_view hex);
// "#rrggbb", or "#rrggbbaa" for a translucent colour.
std::string HexColor(SkColor color);

// Zen's getToolbarModifiedBaseRaw(): the surface colours are blended into.
Rgb ToolbarBase(const Environment& env, const SpaceTheme& theme);
// Zen's getNativeAccentColor().
Rgb NativeAccentColor(const Environment& env);
// Zen's getMostDominantColor(): the primary colour, or (for a custom
// primary colour) the native accent. nullopt for the default theme.
std::optional<Rgb> DominantColor(const Environment& env,
                                 const SpaceTheme& theme);
// Zen's shouldBeDarkMode(): whether light text reads better on `accent`.
bool ShouldBeDarkMode(const Environment& env,
                      const SpaceTheme& theme,
                      const Rgb& accent);
// Zen's getAccentColorForUI().
Rgb AccentColorForUI(const Environment& env, const Rgb& accent, bool dark_mode);
// How a theme dresses a window (Zen's getGradientForWorkspace()).
struct Appearance {
  // Whether the window's surfaces are dark. For the default theme this is
  // the window's colour scheme; otherwise it follows the theme.
  bool dark = false;
  // The theme sets it (not the window's scheme).
  bool explicit_mode = false;
  Rgb primary = {0, 0, 0};
};
Appearance GetAppearance(const Environment& env, const SpaceTheme& theme);

// Koan, not Zen (#291): the contrast of the light mode's text (black at 90%,
// as Zen's shouldBeDarkMode() composites it) on `color` painted at `opacity`
// on a transparent window (Zen's white wash, at that alpha) over a dark blur
// (`env.window`: the window's colour in its dark scheme, which is what the
// blur shows over a dark desktop).
double LightTextContrastOverBlur(const Environment& env,
                                 const Rgb& color,
                                 double opacity);
// The contrast the light mode's text keeps over the blur: WCAG's AA for
// normal text.
inline constexpr double kLightTextMinContrast = 4.5;
// Koan, not Zen (#291): the least opacity a theme's colours are painted at
// on a transparent window in a dark system scheme (`env.dark`) when the
// theme gives the window light mode. Zen decides the mode on the theme's
// colour alone (the accent is the background on a transparent window) but
// paints the colour at the slider's opacity over the blur, which is dark
// there: at a low opacity the sidebar is mid-grey, and its dark text and
// icons fade into it. Each colour is painted at least as opaque as it takes
// for LightTextContrastOverBlur() to reach kLightTextMinContrast (at most
// Zen's maximum, 0.8). 0 when no floor applies: an opaque window, a light
// system, the default theme, a dark mode, custom colours (painted as
// given).
double LightThemeOpacityFloor(const Environment& env, const SpaceTheme& theme);

// A CSS background layer: a solid colour (one stop), or a
// linear-gradient(<angle>) / radial-gradient(circle at <x>% <y>%) with
// colour stops at fractions of the gradient line (which may lie outside
// [0, 1], as in CSS).
struct GradientLayer {
  enum class Kind { kSolid, kLinear, kRadial };
  struct Stop {
    bool operator==(const Stop&) const = default;

    SkColor4f color;
    float position;
  };

  GradientLayer();
  GradientLayer(const GradientLayer&);
  GradientLayer& operator=(const GradientLayer&);
  ~GradientLayer();

  bool operator==(const GradientLayer&) const;

  Kind kind = Kind::kSolid;
  // kLinear: CSS angle in degrees (0 points up, clockwise).
  float angle = 0;
  // kRadial: centre in fractions of the box.
  gfx::PointF center;
  std::vector<Stop> stops;
};

// A window background: the window's own colour (`base`, Zen's
// --zen-themed-toolbar-bg-transparent) below the theme's `layers` (Zen's
// --zen-main-browser-background), which are listed top first like CSS
// backgrounds. Switching Spaces cross-fades the layers over the base.
struct Background {
  Background();
  Background(const Background&);
  Background& operator=(const Background&);
  ~Background();

  bool operator==(const Background&) const;

  // Below the layers. Transparent on a translucent window (Zen's
  // --zen-themed-toolbar-bg-transparent: transparent).
  SkColor base = SK_ColorBLACK;
  std::vector<GradientLayer> layers;
  // Opacity of the grain texture (the theme's texture).
  float grain = 0;
  // On a translucent window, the opaque paint faded in over the layers at
  // `cover` (0..1), for opacity above Zen's range: at 1 the blur is fully
  // covered (Koan's "no glass" end of the slider).
  float cover = 0;
  SkColor cover_base = SK_ColorTRANSPARENT;
  std::vector<GradientLayer> cover_layers;
  // The alpha the theme's colours are painted at instead of the slider's
  // lower opacity (LightThemeOpacityFloor()); 0 when they take the slider's.
  float opacity_floor = 0;
};

// Zen's getGradient() for the browser background. Koan paints it across the
// whole window, the top toolbar included (Zen paints the toolbar's own,
// which only differs for two-colour themes).
Background GetBackground(const Environment& env, const SpaceTheme& theme);
// A CSS-like description of a background, for tests.
std::string DescribeBackground(const Background& background);

// The colour tokens a theme family can set, named like shadcn/ui's (#48).
// Koan's colour mixer maps them to colour IDs as families need them; today
// the primary colour (the window's accent) is the one Zen's family sets.
enum class Token {
  kBackground,
  kForeground,
  kCard,
  kPopover,
  kPrimary,
  kMuted,
  kAccent,
  kBorder,
  kInput,
  kRing,
  kSidebar,
  kSidebarForeground,
  kSidebarPrimary,
  kSidebarAccent,
  kSidebarBorder,
};

// What a theme gives a window: its background, its light/dark mode and its
// colour tokens. Everything Koan paints or colours for a theme comes from
// these values: the background through KoanThemeController, the mode and the
// primary colour through the window's colour key into Chrome's and Koan's
// colour IDs (koan_color_mixer.cc). Every theme family produces them
// (GetTokens()).
struct WindowTokens {
  WindowTokens();
  WindowTokens(const WindowTokens&);
  WindowTokens& operator=(const WindowTokens&);
  ~WindowTokens();

  bool operator==(const WindowTokens&) const;

  // Behind the sidebar, the toolbar and the page.
  Background background;
  // Set where the theme decides the mode (else the window's scheme does).
  std::optional<bool> dark;
  // The tokens the theme sets; the others keep the window's colours.
  base::flat_map<Token, SkColor> colors;
};

// Theme families: how a stored theme (a dict with its family's name and
// version) becomes a window's tokens. Zen's gradient themes are the first
// family; a theme stored without a family is one of Zen's.
inline constexpr char kZenGradientFamily[] = "zen-gradient";
inline constexpr int kThemeVersion = 1;

// The tokens of a stored theme; an unknown family gives the default theme.
WindowTokens GetTokens(const Environment& env, const base::DictValue& stored);

// The tokens of a Zen gradient theme (Zen's getGradientForWorkspace()).
WindowTokens GetZenTokens(const Environment& env, const SpaceTheme& theme);

// The colour harmonies (Zen's colorHarmonies), in Zen's order.
struct Harmony {
  const char* type;
  std::vector<double> angles;
};
const std::vector<Harmony>& Harmonies();

// The preset swatches under the colour field, five pages like Zen's.
struct Preset {
  double lightness;  // < 0: none (black and white presets).
  const char* algo;
  int num_dots;
  gfx::PointF position;
  DotType type;
  // The swatch's colours: one, or three for the gradient swatches.
  std::vector<SkColor> swatch;
};
const std::vector<std::vector<Preset>>& PresetPages();

// The picker's dots and the theme they make: Zen's nsZenThemePicker state
// (dots, useAlgo, #currentLightness, currentOpacity, currentTexture) and the
// operations on it, independent of any view. Positions are in the colour
// field's coordinates.
class PickerModel {
 public:
  static constexpr int kMaxDots = 3;
  // The opacity slider's range (Zen's min/max on the range input,
  // theme-picker.inc: 0.30 on macOS).
#if BUILDFLAG(IS_MAC)
  static constexpr double kMinOpacity = 0.3;
#else
  static constexpr double kMinOpacity = 0.25;
#endif
  static constexpr double kMaxOpacity = 0.8;
  // On a translucent window the slider goes on to 1, which covers the blur
  // (above kMaxOpacity, Zen's maths are kept and the opaque paint fades in).
  static constexpr double kGlassMaxOpacity = 1;

  struct Dot {
    int id = 0;
    gfx::PointF position;
    DotType type = DotType::kNone;
    double lightness = 50;
    Rgb color = {0, 0, 0};
    // Zen's "primary" class.
    bool primary = false;
    // Creation order, which is the order of Zen's dot elements: the first
    // one is drawn big and is the only one that takes the mouse.
    int serial = 0;
    // Where the dot appeared; it animates from there to `position`.
    gfx::PointF spawn_position;
  };

  // The big dot's radius, including its border.
  static constexpr float kPrimaryDotRadius = 25;

  // `field_size` is the colour field's (square) size.
  explicit PickerModel(float field_size);
  PickerModel(const PickerModel&) = delete;
  PickerModel& operator=(const PickerModel&) = delete;
  ~PickerModel();

  // Loads a theme (Zen's onWorkspaceChange + recalculateDots).
  void Load(const SpaceTheme& theme);
  // The theme the dots make (Zen's updateCurrentWorkspace).
  SpaceTheme ToTheme() const;

  // Click in the field (Zen's onThemePickerClick): the first click makes a
  // dot, later ones move the primary dot. Returns whether anything changed.
  bool Click(gfx::PointF point);
  // Drags the primary dot (Zen's onDotMouseMove).
  void DragTo(gfx::PointF point);
  // The colour action buttons.
  void AddDot();
  void RemoveDot();
  void ToggleAlgorithm();
  // Right-click on the primary dot removes it (Zen's onDotMouseUp).
  void RemovePrimaryDot();
  // A preset swatch (Zen's initPredefinedColors click handler).
  void ApplyPreset(const Preset& preset);

  // The custom colour list (newest first).
  void AddCustomColor(SkColor color);
  void RemoveCustomColor(size_t index);
  const std::vector<SkColor>& custom_colors() const { return custom_colors_; }

  void set_opacity(double opacity) { opacity_ = opacity; }
  double opacity() const { return opacity_; }
  // Whether the window is translucent: the slider's range then ends at
  // kGlassMaxOpacity, and it works for the default theme too.
  void set_glass(bool glass) { glass_ = glass; }
  bool glass() const { return glass_; }
  double max_opacity() const { return glass_ ? kGlassMaxOpacity : kMaxOpacity; }
  // Snaps to Zen's 16 grain steps (Zen's onTextureMouseMove); returns
  // whether the value changed.
  bool SetTextureFromAngle(double degrees);
  void set_texture(double texture) { texture_ = texture; }
  double texture() const { return texture_; }

  const std::vector<Dot>& dots() const { return dots_; }
  const std::string& algorithm() const { return use_algo_; }
  // The big dot (null without dots).
  const Dot* DraggableDot() const;
  // Whether `point` is on the big dot.
  bool HitsPrimaryDot(gfx::PointF point) const;
  float field_size() const { return field_size_; }

  // Zen's getColorFromPosition(). Updates the current lightness for dots
  // placed by hand, like Zen.
  Rgb ColorFromPosition(gfx::PointF position, DotType type);
  // Zen's calculateInitialPosition().
  static gfx::PointF InitialPosition(const Rgb& rgb);

 private:
  enum class Action { kAdd, kRemove, kUpdate };
  // Zen's calculateCompliments() and handleColorPositions(). nullopt where
  // Zen throws (no harmony fits), which abandons the action.
  std::optional<std::vector<Dot>> CalculateCompliments(
      std::vector<Dot> dots,
      Action action,
      const std::string& use_harmony);
  void HandleColorPositions(std::vector<Dot> positions);
  void SpawnDot(gfx::PointF position, DotType type, bool primary);
  // Clamps a point to the colour circle.
  gfx::PointF ClampToCircle(gfx::PointF point) const;
  void RenumberDots();
  Dot* MutableDraggableDot();

  const float field_size_;
  std::vector<Dot> dots_;
  std::vector<SkColor> custom_colors_;
  std::string use_algo_;
  double current_lightness_ = 50;
  double opacity_ = 0.5;
  double texture_ = 0;
  bool glass_ = false;
  int next_serial_ = 0;
};

// The opacity slider's wave (Zen's #interpolateWavePath): a straight line
// at `progress` 0, a full sine wave at 1, as a list of SVG path commands
// in the wave's viewBox coordinates ("0 -7.605 455 70").
struct WaveSegment {
  // 'M' or 'C' (or 'L' for the straight line).
  char type;
  std::array<float, 6> points;
};
std::vector<WaveSegment> InterpolateWavePath(double progress);
// The wave's horizontal extent in viewBox units and its centre line.
inline constexpr float kWaveStartX = 51.373f;
inline constexpr float kWaveEndX = 367.037f;
inline constexpr float kWaveCenterY = 27.395f;

}  // namespace koan::theme

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_THEME_H_
