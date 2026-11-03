// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/color/koan_color_mixer.h"

#include <initializer_list>

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/color/koan_look.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_transform.h"
#include "ui/gfx/color_utils.h"

namespace koan {

namespace {

using look::Token;

// The tokens of one mode, with the few operations the mapping needs. A
// colour here is a token, or a token laid over another (shadcn writes those
// as `bg-accent/50`), so every surface traces back to a design token.
class Tokens {
 public:
  explicit Tokens(bool dark)
      : tokens_(look::CurrentTokens(dark)), dark_(dark) {}

  bool dark() const { return dark_; }
  SkColor operator[](Token token) const { return tokens_[token]; }

  // `token` at `opacity` (0..1), left translucent.
  SkColor Alpha(Token token, float opacity) const {
    return SkColorSetA(tokens_[token], ToAlpha(opacity));
  }
  // `foreground` at `opacity` over `background`, opaque.
  SkColor Over(Token foreground, Token background, float opacity) const {
    return Over(tokens_[foreground], tokens_[background], opacity);
  }
  static SkColor Over(SkColor foreground, SkColor background, float opacity) {
    const float alpha = opacity * SkColorGetA(foreground) / 255.0f;
    return color_utils::AlphaBlend(SkColorSetA(foreground, SK_AlphaOPAQUE),
                                   SkColorSetA(background, SK_AlphaOPAQUE),
                                   alpha);
  }
  // A token that is translucent by design (`border` is white at 10% in dark
  // mode) laid over `background`.
  SkColor Opaque(Token token, Token background) const {
    return Over(tokens_[token], tokens_[background], 1.0f);
  }
  // Text on a destructive fill: shadcn's `text-white`, which in light mode is
  // `primary-foreground` and in dark mode `foreground`.
  SkColor OnDestructive() const {
    return tokens_[dark_ ? Token::kForeground : Token::kPrimaryForeground];
  }
  // shadcn's `bg-input/30` in dark mode and a plain `background` in light
  // mode, the fill of inputs.
  SkColor InputFill(Token background) const {
    // Zen's URL bar colour is a token of its own.
    if (look::CurrentLook().style == look::Style::kZen) {
      return tokens_[Token::kInput];
    }
    return dark_ ? Over(Token::kInput, background, 0.3f)
                 : tokens_[Token::kBackground];
  }

 private:
  static SkAlpha ToAlpha(float opacity) {
    return static_cast<SkAlpha>(opacity * SK_AlphaOPAQUE + 0.5f);
  }

  // A copy: it is small, and the look may change while providers are built.
  const look::TokenSet tokens_;
  bool dark_;
};

}  // namespace

// Chromium's system and core colours from shadcn's tokens, before anything is
// derived from them. See koan_color_mixer.h.
void AddKoanCoreColorMixer(ui::ColorProvider* provider,
                           const ui::ColorProviderKey& key) {
  const Tokens t(key.color_mode == ui::ColorProviderKey::ColorMode::kDark);
  ui::ColorMixer& mixer = provider->AddMixer();

  // --- Core colours (core_default_color_mixer.cc) ---
  // Menus, bubbles and dialogs are popovers.
  mixer[ui::kColorPrimaryBackground] = {t[Token::kPopover]};
  mixer[ui::kColorPrimaryForeground] = {t[Token::kPopoverForeground]};
  mixer[ui::kColorSecondaryForeground] = {t[Token::kMutedForeground]};
  mixer[ui::kColorAccent] = {t[Token::kPrimary]};
  mixer[ui::kColorAccentWithGuaranteedContrastAtopPrimaryBackground] = {
      t[Token::kPrimary]};
  mixer[ui::kColorAlertHighSeverity] = {t[Token::kDestructive]};
  mixer[ui::kColorDisabledForeground] = {
      t.Over(Token::kMutedForeground, Token::kPopover, 0.6f)};
  mixer[ui::kColorEndpointForeground] = {t[Token::kForeground]};
  mixer[ui::kColorEndpointBackground] = {t[Token::kBackground]};
  mixer[ui::kColorItemHighlight] = {t[Token::kRing]};
  mixer[ui::kColorItemSelectionBackground] = {t[Token::kAccent]};
  mixer[ui::kColorMenuSelectionBackground] = {t[Token::kAccent]};
  mixer[ui::kColorMidground] = {t.Opaque(Token::kBorder, Token::kPopover)};
  mixer[ui::kColorSubtleAccent] = {t[Token::kSecondary]};
  mixer[ui::kColorSubtleEmphasisBackground] = {t[Token::kMuted]};
  mixer[ui::kColorTextSelectionBackground] = {t[Token::kPrimary]};
  mixer[ui::kColorTextSelectionForeground] = {t[Token::kPrimaryForeground]};

  // --- Sys tokens (sys_color_mixer.cc) ---
  // Primary: shadcn's `primary`, which is near-white in dark mode.
  mixer[ui::kColorSysPrimary] = {t[Token::kPrimary]};
  mixer[ui::kColorSysOnPrimary] = {t[Token::kPrimaryForeground]};
  mixer[ui::kColorSysPrimaryContainer] = {t[Token::kSecondary]};
  mixer[ui::kColorSysOnPrimaryContainer] = {t[Token::kSecondaryForeground]};
  // shadcn has one neutral accent: secondary and tertiary follow it.
  mixer[ui::kColorSysSecondary] = {t[Token::kMutedForeground]};
  mixer[ui::kColorSysOnSecondary] = {t[Token::kBackground]};
  mixer[ui::kColorSysSecondaryContainer] = {t[Token::kSecondary]};
  mixer[ui::kColorSysOnSecondaryContainer] = {t[Token::kSecondaryForeground]};
  mixer[ui::kColorSysTertiary] = {t[Token::kMutedForeground]};
  mixer[ui::kColorSysOnTertiary] = {t[Token::kBackground]};
  mixer[ui::kColorSysTertiaryContainer] = {t[Token::kSecondary]};
  mixer[ui::kColorSysOnTertiaryContainer] = {t[Token::kSecondaryForeground]};
  mixer[ui::kColorSysError] = {t[Token::kDestructive]};
  mixer[ui::kColorSysOnError] = {t.OnDestructive()};
  mixer[ui::kColorSysErrorContainer] = {
      t.Over(Token::kDestructive, Token::kPopover, 0.2f)};
  mixer[ui::kColorSysOnErrorContainer] = {t[Token::kDestructive]};

  mixer[ui::kColorSysOnSurface] = {t[Token::kForeground]};
  mixer[ui::kColorSysOnSurfaceVariant] = {t[Token::kMutedForeground]};
  mixer[ui::kColorSysOutline] = {t.Opaque(Token::kInput, Token::kBackground)};
  mixer[ui::kColorSysSurfaceVariant] = {t[Token::kMuted]};

  mixer[ui::kColorSysInversePrimary] = {t[Token::kPrimaryForeground]};
  mixer[ui::kColorSysInverseOnSurface] = {t[Token::kBackground]};
  mixer[ui::kColorSysInverseSurface] = {t[Token::kForeground]};
  mixer[ui::kColorSysInverseSurfacePrimary] = {t[Token::kForeground]};

  // Surfaces: the page is `background`, a raised surface `card`, and the
  // steps in between lighten towards `secondary` (shadcn's hover surface).
  mixer[ui::kColorSysSurface] = {t[Token::kBackground]};
  mixer[ui::kColorSysSurface1] = {t[Token::kCard]};
  mixer[ui::kColorSysSurface2] = {
      t.Over(Token::kSecondary, Token::kCard, 0.5f)};
  mixer[ui::kColorSysSurface3] = {t[Token::kSecondary]};
  mixer[ui::kColorSysSurface4] = {
      t.Over(Token::kForeground, Token::kSecondary, 0.04f)};
  mixer[ui::kColorSysSurface5] = {
      t.Over(Token::kForeground, Token::kSecondary, 0.08f)};
  mixer[ui::kColorSysSurfaceContainerLowest] = {t[Token::kBackground]};
  mixer[ui::kColorSysSurfaceContainerLow] = {t[Token::kBackground]};
  mixer[ui::kColorSysSurfaceContainer] = {t[Token::kCard]};
  mixer[ui::kColorSysSurfaceContainerHigh] = {t[Token::kSecondary]};
  mixer[ui::kColorSysSurfaceContainerHighest] = {
      t.Over(Token::kForeground, Token::kSecondary, 0.06f)};

  // Text and icons. Toolbar icons use "secondary" on-surface: a step below
  // `foreground`.
  mixer[ui::kColorSysOnSurfaceSecondary] = {
      t.Over(Token::kForeground, Token::kMutedForeground, 0.65f)};
  mixer[ui::kColorSysOnSurfaceSubtle] = {t[Token::kMutedForeground]};
  mixer[ui::kColorSysOnSurfacePrimary] = {t[Token::kForeground]};
  mixer[ui::kColorSysOnSurfacePrimaryInactive] = {t[Token::kMutedForeground]};

  mixer[ui::kColorSysTonalContainer] = {t[Token::kSecondary]};
  mixer[ui::kColorSysOnTonalContainer] = {t[Token::kSecondaryForeground]};
  mixer[ui::kColorSysBaseTonalContainer] = {t[Token::kMuted]};
  mixer[ui::kColorSysOnBaseTonalContainer] = {t[Token::kForeground]};
  mixer[ui::kColorSysTonalOutline] = {
      t.Opaque(Token::kBorder, Token::kBackground)};
  mixer[ui::kColorSysNeutralOutline] = {
      t.Opaque(Token::kBorder, Token::kBackground)};
  mixer[ui::kColorSysNeutralContainer] = {t[Token::kMuted]};
  mixer[ui::kColorSysDivider] = {
      t.Opaque(Token::kSidebarBorder, Token::kSidebar)};

  // Chrome's surfaces. The frame, toolbar, tab strip and sidebar are one
  // surface, shadcn's `sidebar`; the page floats on it as a `background`
  // card, like the Sidebar component's "inset" variant.
  mixer[ui::kColorSysBase] = {t[Token::kSidebar]};
  mixer[ui::kColorSysBaseContainer] = {t[Token::kCard]};
  mixer[ui::kColorSysBaseContainerElevated] = {t[Token::kPopover]};
  mixer[ui::kColorSysBaseContainerOnSurface] = {t[Token::kCard]};
  mixer[ui::kColorSysSurfaceSection] = {t[Token::kCard]};
  mixer[ui::kColorSysHeader] = {t[Token::kSidebar]};
  mixer[ui::kColorSysHeaderInactive] = {t[Token::kSidebar]};
  mixer[ui::kColorSysHeaderContainer] = {t[Token::kSidebarAccent]};
  mixer[ui::kColorSysHeaderContainerInactive] = {t[Token::kSidebar]};
  mixer[ui::kColorSysOnHeaderDivider] = {
      t.Opaque(Token::kSidebarBorder, Token::kSidebar)};
  mixer[ui::kColorSysOnHeaderDividerInactive] = {
      t.Opaque(Token::kSidebarBorder, Token::kSidebar)};
  mixer[ui::kColorSysOnHeaderPrimary] = {t[Token::kSidebarForeground]};
  mixer[ui::kColorSysOnHeaderPrimaryInactive] = {t[Token::kMutedForeground]};
  // The URL pill and omnibox: shadcn's Input.
  mixer[ui::kColorSysOmniboxContainer] = {t.InputFill(Token::kSidebar)};

  // States are overlays: shadcn hovers with `accent`, a few percent of
  // `foreground`.
  const float hover = t.dark() ? 0.08f : 0.05f;
  mixer[ui::kColorSysStateHoverOnProminent] = {
      t.Alpha(Token::kPrimaryForeground, 0.1f)};
  mixer[ui::kColorSysStateHoverOnSubtle] = {t.Alpha(Token::kForeground, hover)};
  mixer[ui::kColorSysStateRippleNeutralOnProminent] = {
      t.Alpha(Token::kPrimaryForeground, 0.16f)};
  mixer[ui::kColorSysStateRippleNeutralOnSubtle] = {
      t.Alpha(Token::kForeground, 0.12f)};
  mixer[ui::kColorSysStateRipplePrimary] = {t.Alpha(Token::kPrimary, 0.3f)};
  mixer[ui::kColorSysStateFocusRing] = {t[Token::kRing]};
  mixer[ui::kColorSysStateFocusRingInverse] = {t[Token::kRing]};
  mixer[ui::kColorSysStateTextHighlight] = {t[Token::kPrimary]};
  mixer[ui::kColorSysStateOnTextHighlight] = {t[Token::kPrimaryForeground]};
  mixer[ui::kColorSysStateFocusHighlight] = {
      t.Alpha(Token::kForeground, hover)};
  // shadcn disables with `opacity-50`.
  mixer[ui::kColorSysStateDisabled] = {t.Alpha(Token::kForeground, 0.5f)};
  mixer[ui::kColorSysStateDisabledContainer] = {
      t.Alpha(Token::kForeground, 0.1f)};
  mixer[ui::kColorSysStateHoverDimBlendProtection] = {
      t.Alpha(Token::kForeground, 0.1f)};
  mixer[ui::kColorSysStateHoverBrightBlendProtection] = {
      t.Alpha(Token::kForeground, 0.12f)};
  mixer[ui::kColorSysStateInactiveRing] = {t.Alpha(Token::kRing, 0.55f)};
  mixer[ui::kColorSysStateOnHeaderHover] = {t[Token::kSidebarAccentForeground]};
  mixer[ui::kColorSysStateHeaderHover] = {t[Token::kSidebarAccent]};
  mixer[ui::kColorSysStateHeaderHoverInactive] = {
      t.Alpha(Token::kForeground, hover)};
  mixer[ui::kColorSysStateHeaderSelect] = {
      t.Alpha(Token::kSidebarAccent, 0.6f)};
  mixer[ui::kColorSysStateHover] = {t.Alpha(Token::kForeground, hover)};
  mixer[ui::kColorSysStateFocus] = {t.Alpha(Token::kForeground, 0.12f)};
  mixer[ui::kColorSysStatePressed] = {t.Alpha(Token::kForeground, 0.12f)};
}

// The leaves macOS's native mixers (which run after the early mixer) take
// from the system, and the shadcn-specific shapes of a few Chromium
// surfaces.
void AddShadcnChromiumColors(ui::ColorMixer& mixer,
                             const ui::ColorProviderKey& key) {
  const Tokens t(key.color_mode == ui::ColorProviderKey::ColorMode::kDark);

  // Selection and focus: shadcn's (`selection:bg-primary`), not the system
  // accent colour.
  mixer[ui::kColorTextSelectionBackground] = {t[Token::kPrimary]};
  mixer[ui::kColorTextSelectionForeground] = {t[Token::kPrimaryForeground]};
  // The Material mixer derives these from its own tonal selection colour.
  for (ui::ColorId id : std::initializer_list<ui::ColorId>{
           ui::kColorTextfieldSelectionBackground,
           ui::kColorLabelSelectionBackground,
           kColorOmniboxSelectionBackground}) {
    mixer[id] = {t[Token::kPrimary]};
  }
  for (ui::ColorId id : std::initializer_list<ui::ColorId>{
           ui::kColorTextfieldSelectionForeground,
           ui::kColorLabelSelectionForeground,
           kColorOmniboxSelectionForeground}) {
    mixer[id] = {t[Token::kPrimaryForeground]};
  }
  mixer[ui::kColorSysStateFocusRing] = {t[Token::kRing]};
  mixer[ui::kColorSysStateTextHighlight] = {t[Token::kPrimary]};
  mixer[ui::kColorSysStateOnTextHighlight] = {t[Token::kPrimaryForeground]};

  // Dialogs are `bg-background` with a border (menus and dropdowns, which
  // the early mixer already made popovers, are `bg-popover`).
  mixer[ui::kColorDialogBackground] = {t[Token::kBackground]};

  // Menus: a popover with a `border` outline.
  mixer[ui::kColorMenuBorder] = {t.Opaque(Token::kBorder, Token::kPopover)};
  mixer[ui::kColorMenuItemForeground] = {t[Token::kPopoverForeground]};
  mixer[ui::kColorMenuItemForegroundDisabled] = {
      t.Over(Token::kPopoverForeground, Token::kPopover, 0.5f)};
  mixer[ui::kColorMenuSeparator] = {t.Opaque(Token::kBorder, Token::kPopover)};
  mixer[ui::kColorBubbleBorder] = {t.Opaque(Token::kBorder, Token::kPopover)};

  // Tooltips are shadcn's: `bg-primary text-primary-foreground` (Zen's are
  // panels).
  const bool zen = look::CurrentLook().style == look::Style::kZen;
  mixer[ui::kColorTooltipBackground] = {
      t[zen ? Token::kPopover : Token::kPrimary]};
  mixer[ui::kColorTooltipForeground] = {
      t[zen ? Token::kPopoverForeground : Token::kPrimaryForeground]};

  // The omnibox suggestions and the find bar float: popovers (not the
  // sidebar's colour, which differs from `popover` in light mode), with
  // shadcn's `accent` for the hovered or selected row.
  mixer[kColorOmniboxResultsBackground] = {t[Token::kPopover]};
  mixer[kColorOmniboxResultsBackgroundHovered] = {t[Token::kAccent]};
  mixer[kColorOmniboxResultsBackgroundSelected] = {t[Token::kAccent]};
  // A suggestion reads "title — url": the url is the muted colour, not the
  // accent Chromium makes links.
  mixer[kColorOmniboxResultsUrl] = {t[Token::kMutedForeground]};
  mixer[kColorOmniboxResultsUrlSelected] = {t[Token::kMutedForeground]};
  mixer[kColorOmniboxResultsTextSelected] = {t[Token::kForeground]};
  mixer[kColorFindBarBackground] = {t[Token::kPopover]};
  // The new tab page is the page's own surface.
  mixer[kColorNewTabPageBackground] = {t[Token::kBackground]};
  // The URL bar's card: Koan's floating-card colour, hovered or not.
  mixer[kColorLocationBarBackground] = {kColorKoanFloatingUrlBarBackground};
  mixer[kColorLocationBarBackgroundHovered] = {
      kColorKoanFloatingUrlBarBackground};
  // The URL pill and omnibox have Input's `border-input`.
  mixer[kColorLocationBarBorder] = {t.Opaque(Token::kInput, Token::kSidebar)};

  // Focus borders use the ring.
  mixer[ui::kColorFocusableBorderFocused] = {t[Token::kRing]};
}

// Koan's own views (koan_color_id.h) from the tokens.
void AddShadcnKoanColors(ui::ColorMixer& mixer,
                         const ui::ColorProviderKey& key) {
  const Tokens t(key.color_mode == ui::ColorProviderKey::ColorMode::kDark);

  // The sidebar is shadcn's Sidebar: tab rows are `sidebar-accent` items.
  mixer[kColorKoanSidebarTabSelected] = {t[Token::kSidebarAccent]};
  mixer[kColorKoanSidebarTabHovered] = {
      t.Over(Token::kSidebarAccent, Token::kSidebar, 0.6f)};
  mixer[kColorKoanSidebarRowHovered] = {t[Token::kSidebarAccent]};
  mixer[kColorKoanSidebarRowPressed] = {
      t.Over(Token::kSidebarForeground, Token::kSidebarAccent, 0.1f)};
  mixer[kColorKoanSidebarNewTabForeground] = {t[Token::kSidebarForeground]};
  mixer[kColorKoanSidebarSeparator] = {t[Token::kSidebarBorder]};
  mixer[kColorKoanSidebarClearForeground] = {t[Token::kMutedForeground]};
  mixer[kColorKoanSidebarClearForegroundHovered] = {
      t[Token::kSidebarForeground]};

  mixer[kColorKoanPinnedTabSlash] = {t[Token::kMutedForeground]};
  mixer[kColorKoanPinnedTabResetHovered] = {t[Token::kSidebarAccent]};
  mixer[kColorKoanPinnedTabResetPressed] = {
      t.Over(Token::kSidebarForeground, Token::kSidebarAccent, 0.1f)};
  mixer[kColorKoanPinnedTabSublabel] = {t[Token::kMutedForeground]};

  // Essentials tiles: cards on the sidebar.
  mixer[kColorKoanEssentialsTile] = {t[Token::kSidebarAccent]};
  mixer[kColorKoanEssentialsTileHovered] = {
      t.Over(Token::kSidebarForeground, Token::kSidebarAccent, 0.08f)};
  mixer[kColorKoanEssentialsTileSelected] = {
      t.Over(Token::kSidebarForeground, Token::kSidebarAccent, 0.16f)};

  // The URL pill is an Input; the floating bar a popover with shadcn's
  // `shadow-lg`.
  mixer[kColorKoanUrlPill] = {t.InputFill(Token::kSidebar)};
  mixer[kColorKoanFloatingUrlBarBackground] = {t[Token::kPopover]};
  mixer[kColorKoanFloatingUrlBarGlass] = {t.Alpha(Token::kPopover, 0.9f)};
  mixer[kColorKoanFloatingUrlBarBorder] = {
      t.Opaque(Token::kBorder, Token::kPopover)};
  mixer[kColorKoanFloatingUrlBarShadow] = ui::SetAlpha(
      ui::kColorShadowBase, static_cast<SkAlpha>(t.dark() ? 0x80 : 0x40));

  // Toasts are Sonner's: a popover with a border.
  mixer[kColorKoanToastBackgroundTop] = {t[Token::kPopover]};
  mixer[kColorKoanToastBackgroundBottom] = {t[Token::kPopover]};
  mixer[kColorKoanToastForeground] = {t[Token::kPopoverForeground]};
  mixer[kColorKoanToastKeyBackground] = {t[Token::kMuted]};
  mixer[kColorKoanToastButton] = {t[Token::kSecondary]};
  mixer[kColorKoanToastButtonIcon] = {t[Token::kSecondaryForeground]};

  // Glance: shadcn's dialog overlay is `bg-black/50`.
  mixer[kColorKoanGlanceBackdrop] =
      ui::SetAlpha(ui::kColorShadowBase, static_cast<SkAlpha>(0x80));
  mixer[kColorKoanGlanceCardShadow] = {
      ui::kColorShadowValueAmbientShadowElevationFour};
  mixer[kColorKoanGlanceButton] = {t[Token::kSecondary]};
  mixer[kColorKoanGlanceButtonIcon] = {t[Token::kSecondaryForeground]};
  mixer[kColorKoanGlanceButtonConfirm] = {t[Token::kDestructive]};
  mixer[kColorKoanGlanceButtonConfirmIcon] = {t.OnDestructive()};

  // Spaces.
  mixer[kColorKoanSpaceForeground] = {t[Token::kSidebarForeground]};
  mixer[kColorKoanSpaceHeaderHovered] = {t[Token::kSidebarAccent]};
  mixer[kColorKoanSpaceButtonHovered] = {t[Token::kSidebarAccent]};
  mixer[kColorKoanSpaceDot] = {t[Token::kMutedForeground]};
  mixer[kColorKoanSpaceFormField] = {t.InputFill(Token::kSidebar)};
  mixer[kColorKoanSpaceFormFieldHovered] = {t[Token::kSidebarAccent]};
  mixer[kColorKoanSpaceFormDescription] = {t[Token::kMutedForeground]};
  mixer[kColorKoanSpaceIconBorder] = {t.Opaque(Token::kInput, Token::kSidebar)};
  mixer[kColorKoanEmojiHovered] = {t[Token::kAccent]};

  // The Library is a Sheet: `background`, with `accent` hovers.
  mixer[kColorKoanLibraryBackground] = {t[Token::kBackground]};
  mixer[kColorKoanLibrarySideHighlight] = {t[Token::kSidebar]};
  mixer[kColorKoanLibraryForeground] = {t[Token::kForeground]};
  mixer[kColorKoanLibraryForegroundSecondary] = {t[Token::kMutedForeground]};
  mixer[kColorKoanLibraryForegroundDim] = {
      t.Over(Token::kMutedForeground, Token::kBackground, 0.7f)};
  mixer[kColorKoanLibraryPill] = {t[Token::kSecondary]};
  mixer[kColorKoanLibraryPillHovered] = {t[Token::kAccent]};
  mixer[kColorKoanLibraryChipActive] = {
      t.Over(Token::kForeground, Token::kAccent, 0.12f)};
  mixer[kColorKoanLibraryRowHovered] = {t[Token::kAccent]};
  mixer[kColorKoanLibraryTabHovered] = {
      t.Over(Token::kAccent, Token::kBackground, 0.5f)};
  mixer[kColorKoanLibraryTabSelected] = {t[Token::kAccent]};
  mixer[kColorKoanLibraryIcon] = {t[Token::kPrimary]};
  mixer[kColorKoanLibraryIconFill] = {
      t.Over(Token::kPrimary, Token::kBackground, 0.15f)};
  mixer[kColorKoanLibraryProgressTrack] = {t[Token::kSecondary]};
  mixer[kColorKoanLibrarySpaceCard] = {t[Token::kCard]};
  mixer[kColorKoanLibrarySpaceCardBorder] = {
      t.Opaque(Token::kBorder, Token::kCard)};
  mixer[kColorKoanLibrarySpaceIconEmpty] = {t[Token::kMuted]};
  mixer[kColorKoanLibraryAddSpace] = {t[Token::kMuted]};
  mixer[kColorKoanLibraryAddSpaceHovered] = {t[Token::kAccent]};
  mixer[kColorKoanLibraryAddSpaceForeground] = {t[Token::kMutedForeground]};

  // Media cards.
  mixer[kColorKoanMediaCard] = {t[Token::kCard]};
  mixer[kColorKoanMediaCardShadow] = ui::SetAlpha(
      ui::kColorShadowBase, static_cast<SkAlpha>(t.dark() ? 0x66 : 0x1a));
  mixer[kColorKoanMediaCardOutline] = {t.Opaque(Token::kBorder, Token::kCard)};
  mixer[kColorKoanMediaProgressTrack] = {t[Token::kSecondary]};
  mixer[kColorKoanMediaProgress] = {t[Token::kPrimary]};

  // The download animation.
  mixer[kColorKoanDownloadArcRing] = {
      t.Opaque(Token::kBorder, Token::kPopover)};
  mixer[kColorKoanDownloadArcCircle] = {t[Token::kPrimary]};
  mixer[kColorKoanDownloadArcIcon] = {t[Token::kPrimaryForeground]};
  mixer[kColorKoanDownloadArcShadow] = ui::SetAlpha(
      ui::kColorShadowBase, static_cast<SkAlpha>(t.dark() ? 0x80 : 0x40));
  mixer[kColorKoanDownloadTile] = {t[Token::kPrimary]};
  mixer[kColorKoanDownloadTileIcon] = {t[Token::kPrimaryForeground]};
  mixer[kColorKoanDownloadTileShadow] = ui::SetAlpha(
      ui::kColorShadowBase, static_cast<SkAlpha>(t.dark() ? 0x66 : 0x1a));

  // The welcome screen: shadcn pages on `background`.
  mixer[kColorKoanWelcomeBackdropTop] = {t[Token::kSidebar]};
  mixer[kColorKoanWelcomeBackdropBottom] = {t[Token::kBackground]};
  mixer[kColorKoanWelcomeTitle] = {t[Token::kForeground]};
  mixer[kColorKoanWelcomeStartButton] = {t[Token::kPrimary]};
  mixer[kColorKoanWelcomeStartButtonHovered] = {
      t.Over(Token::kPrimary, Token::kBackground, 0.9f)};
  mixer[kColorKoanWelcomeStartButtonIcon] = {t[Token::kPrimaryForeground]};
  mixer[kColorKoanWelcomeSidebar] = {t[Token::kSidebar]};
  mixer[kColorKoanWelcomeContent] = {t[Token::kBackground]};
  mixer[kColorKoanWelcomeSurface] = {t[Token::kCard]};
  mixer[kColorKoanWelcomeTile] = {t[Token::kMuted]};
  mixer[kColorKoanWelcomeMockWindow] = {t[Token::kCard]};
  mixer[kColorKoanWelcomeBorder] = {t[Token::kBorder]};
  mixer[kColorKoanWelcomeShadow] = ui::SetAlpha(
      ui::kColorShadowBase, static_cast<SkAlpha>(t.dark() ? 0x40 : 0x0d));
  mixer[kColorKoanWelcomeText] = {t[Token::kForeground]};
  mixer[kColorKoanWelcomeTextSecondary] = {t[Token::kMutedForeground]};
  mixer[kColorKoanWelcomeRadio] = {t[Token::kMutedForeground]};
  mixer[kColorKoanWelcomeAccent] = {t[Token::kPrimary]};
  mixer[kColorKoanWelcomeAccentHovered] = {
      t.Over(Token::kPrimary, Token::kBackground, 0.9f)};
  mixer[kColorKoanWelcomeOnAccent] = {t[Token::kPrimaryForeground]};
}

}  // namespace koan
