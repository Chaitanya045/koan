// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_system_accent.h"

#include "ui/native_theme/os_settings_provider.h"

namespace koan {

std::optional<SkColor> GetSystemAccentColor() {
  return ui::OsSettingsProvider::Get().AccentColor();
}

}  // namespace koan
