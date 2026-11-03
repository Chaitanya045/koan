# shadcn look: plan (issue #48, 2026-11-03)

The maintainer ran Koan on a Mac for the first time and found it "typical
Chrome": Chromium's grey frame, a title bar strip, stock colours. Cause: the
only colours Koan changed were its own views and a few chrome ids (toolbar,
separators), in Zen's values. Everything Chromium draws (menus, bubbles,
dialogs, omnibox popup, find bar, infobars, WebUI pages) came from Chromium's
Material baseline, and the window followed the system's light/dark mode.

## How colours reach the screen

`ui::AddColorMixers` builds, in order: Ref palette, Sys tokens (`kColorSys*`),
core defaults (`kColorPrimaryBackground`, …), native core, `ui_color_mixer`
(menus, bubbles, buttons, textfields), Material UI, native UI, CSS system
colours; then Chrome's mixers (`AddChromeColorMixers`: toolbar, tabs,
omnibox, side panel, find bar …), then `koan::AddKoanColorMixer`.

A recipe in a mixer reads other colours **as they were at that mixer**
(`ColorMixer::GetResultColor` evaluates against `input_mixer_getter_`). So a
mixer that runs last cannot restyle what earlier recipes already derived: it
would have to list every derived colour. Hence two seams:

1. **Early mixer** (`AddKoanCoreColorMixer`): runs after the native core mixer
   and before `ui_color_mixer`. It sets the Sys tokens and the ~20 core
   colours from shadcn's tokens. Menus, bubbles, dialogs, buttons, textfields,
   tooltips, the omnibox, tab strip, toolbar, find bar, side panel, infobars,
   download bubble and the WebUI pages (`chrome://theme/colors.css` is
   generated from these ids) then derive from shadcn without being listed.
   One hook patch: `ui/color/color_mixers.{h,cc}` (a setter for a function
   pointer, called after `AddNativeCoreColorMixer`).
2. **Late mixer** (existing `AddKoanColorMixer`): the Koan colour ids
   (`kColorKoan*`) per style, plus the few Chromium leaves that macOS's
   native UI mixer overrides afterwards (menu item colours, text selection,
   focus ring) and a handful of shadcn-specific choices (frame = `sidebar`,
   page card = `background`).

## Tokens

`//koan/browser/ui/color`:
- `koan_oklch`: OKLCH → sRGB (Ottosson matrices, gamut reduction by chroma).
- `koan_shadcn_palettes.inc`: shadcn v4 base colours (Neutral, Zinc, Slate,
  Stone, Gray) and accent themes (Blue, Green, Orange, Red, Rose, Violet,
  Yellow), light and dark, **generated** from shadcn's registry by
  `scripts/gen_shadcn_palettes.py` (OKLCH values copied verbatim).
- `koan_tokens`: `Token` (the 26 colour tokens), `TokenSet` (+ `--radius` and
  shadcn's scale sm/md/lg/xl), `ParseThemeCss` (paste a theme from
  ui.shadcn.com/themes or tweakcn.com: oklch/hsl/rgb/hex, `:root` / `.dark`),
  `ResolveTokens(Look, dark)`, `TokensToCss` (the WebUI pages draw from the
  same values).
- `koan_look`: the current `Look` (style, mode, base, accent, pasted theme),
  process-wide, set from local-state prefs `koan.look.*`.

## Dark by default, everywhere

`ThemeService::GetColorProviderKey` gets one hook that sets
`key.color_mode` from the look's mode (default **Dark**). The colour mode of
the key is what Chromium uses for the NSWindow appearance on macOS
(`NativeWidgetNSWindowBridge::SetColorMode`), so the title bar, native
scrollbars and Cocoa menus are dark too, and what every Views bubble, menu
and WebUI page (NTP, settings, history, downloads) follows. Websites keep
their own colour scheme (the Appearance setting), as in Zen.

## Shape

`--radius` (10px by default) feeds Koan's radius helpers
(`look::RadiusSm/Md/Lg/Xl`): the page card, tab rows, Essentials, URL bar,
buttons, menus, dialogs (through `GetLayoutConstant` and
`ChromeLayoutProvider::GetCornerRadiusMetric`).

## Zen style

The existing Zen values stay in `AddZenColors`; the style switch only picks
which function runs. Switching rebuilds the colour providers
(`NativeTheme::NotifyOnNativeThemeUpdated`) so every window follows live.

## Seam for #41 / PR #62 (theme picker)

PR #62 owns per-Space gradients and sets the window's mode and accent through
the widget's colour key. This PR does not touch that code. The seams:
- `koan::look::CurrentLook().style`: #62's gradient hook should paint only in
  the Zen style (the shadcn style is flat).
- `koan::look::ResolveTokens(look, dark)`: per-Space shadcn themes (Space →
  `Look`) can feed the same function; the mixer reads the process-wide look
  today, and a per-window look would come through the widget's colour key.

## Verification

No build is possible on Linux until #89 (Chromium 158.0.8127.2) lands; code is
written against the checkout's headers. Mac CI (#70) or the maintainer's Mac
produces the screenshots.
