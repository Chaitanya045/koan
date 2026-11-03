// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_COLOR_KOAN_COLOR_MIXER_H_
#define KOAN_BROWSER_UI_COLOR_KOAN_COLOR_MIXER_H_

#include "third_party/skia/include/core/SkColor.h"

namespace ui {
class ColorMixer;
class ColorProvider;
struct ColorProviderKey;
}  // namespace ui

namespace koan {

// Adds Koan's colours for the current look (look::CurrentLook()): shadcn's
// tokens, or Zen's values. Must run after Chrome's own mixers and before
// custom-theme mixers, so installed themes still win.
void AddKoanColorMixer(ui::ColorProvider* provider,
                       const ui::ColorProviderKey& key);

// The floating URL bar's card colour in the current look (Zen's
// rgb(24, 24, 24) / white; shadcn's popover), an opaque colour, not an id:
// the colours that follow it (the location bar, the suggestions) are values,
// so none of them can loop back through Chromium's recipes.
SkColor FloatingUrlBarCardColor(const ui::ColorProviderKey& key);

// Zen's sidebar colours that derive from the window's primary colour (the
// Space theme's, from its colour key, or the default theme's from the system
// accent): the toolbar and tab text (also `kColorToolbarText`), the selected
// and hovered tab rows and the pressed row. Computed values, not references.
void AddZenTabColors(ui::ColorMixer& mixer, const ui::ColorProviderKey& key);

// Zen's suggestion-row colours that depend on the accent and the card
// (AddZenColors can't read the tokens): the selected row, the title text.
void AddZenSuggestionColors(ui::ColorMixer& mixer,
                            const ui::ColorProviderKey& key);

// Sets Chromium's system (`kColorSys*`) and core colours from the look's
// tokens (shadcn's, or Zen's in the Zen look: ResolveTokens()). It must run
// after the native core mixer and before ui_color_mixer.cc, so that every
// colour derived from them (menus, bubbles, dialogs, buttons, textfields, the
// omnibox, tab strip, toolbar, WebUI pages …) follows without being listed.
// Called through ui::SetCoreColorMixerOverride() (patches/ui/color).
void AddKoanCoreColorMixer(ui::ColorProvider* provider,
                           const ui::ColorProviderKey& key);

// Parts of AddKoanColorMixer that follow the look's tokens
// (koan_shadcn_color_mixer.cc). AddShadcnChromiumColors runs in both looks:
// the Chromium leaves that macOS's native mixers set after the early mixer
// (menus, tooltips, the omnibox popup and location bar, the find bar …).
// AddShadcnKoanColors is the shadcn look's Koan colour ids; the Zen look's are
// AddZenColors' (koan_color_mixer.cc).
void AddShadcnChromiumColors(ui::ColorMixer& mixer,
                             const ui::ColorProviderKey& key);
void AddShadcnKoanColors(ui::ColorMixer& mixer,
                         const ui::ColorProviderKey& key);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_COLOR_KOAN_COLOR_MIXER_H_
