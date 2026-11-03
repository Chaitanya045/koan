// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_LOOK_HOOKS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_LOOK_HOOKS_H_

#include <optional>

class Profile;

namespace ui {
struct ColorProviderKey;
}

namespace koan {

// Called at the end of ThemeService::GetColorProviderKey(), before any browser
// window needs colours. The first call loads the user's look from local state
// and plugs Koan's early colour mixer (AddKoanCoreColorMixer) into
// ui::AddColorMixers(). In the shadcn
// style the key's colour mode follows the look's mode (dark by default), not
// the system's, so every Views surface, WebUI page and, on macOS, the
// NSWindow appearance (title bar, scrollbars, Cocoa menus) is dark.
// Incognito and enterprise-isolated profiles keep their own.
void AdjustColorProviderKey(const Profile* profile, ui::ColorProviderKey& key);

// Called at the top of ChromeLayoutProvider::GetCornerRadiusMetric(): in the
// shadcn style, the corner radius of what `token` (a views::ShapeContextToken
// or ChromeShapeContextTokens) rounds, from shadcn's radius scale; nullopt
// for the Zen style and for tokens the look doesn't shape.
std::optional<int> GetLookCornerRadius(int token);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_LOOK_HOOKS_H_
