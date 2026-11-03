// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SYSTEM_ACCENT_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SYSTEM_ACCENT_H_

#include <optional>

#include "third_party/skia/include/core/SkColor.h"

namespace koan {

// The system's accent colour (CSS AccentColor, which Zen's theme maths
// reads): System Settings > Appearance > Accent colour on macOS, the GTK or
// Windows accent elsewhere. nullopt where the system has none.
std::optional<SkColor> GetSystemAccentColor();

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SYSTEM_ACCENT_H_
