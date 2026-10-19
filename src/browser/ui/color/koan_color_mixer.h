// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_COLOR_KOAN_COLOR_MIXER_H_
#define KOAN_BROWSER_UI_COLOR_KOAN_COLOR_MIXER_H_

namespace ui {
class ColorProvider;
struct ColorProviderKey;
}  // namespace ui

namespace koan {

// Adds Koan's Zen-style colour overrides. Must run after Chrome's own mixers
// and before custom-theme mixers, so installed themes still win.
void AddKoanColorMixer(ui::ColorProvider* provider,
                       const ui::ColorProviderKey& key);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_COLOR_KOAN_COLOR_MIXER_H_
