// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_WELCOME_SHORTCUTS_H_
#define KOAN_BROWSER_KOAN_WELCOME_SHORTCUTS_H_

#include <string_view>

#include "base/containers/span.h"
#include "base/values.h"

// The welcome's shortcut step (onboarding v2, #181): the keys where Koan's
// default, Zen's shortcuts, gives a key another job than Chrome does, each
// with its two sides. Picking Chrome's side writes the user's own keys
// (prefs::kShortcuts, as the settings page does); Zen's side removes them,
// back to the defaults. macOS only: elsewhere there are no rows (and no
// step).
namespace koan::shortcuts {

struct WelcomeSide {
  // The key as macOS shows it, and what it does.
  const char16_t* keys;
  const char16_t* label;
};

struct WelcomeRow {
  const char* id;
  const char16_t* title;
  WelcomeSide zen;
  WelcomeSide chrome;
};

base::span<const WelcomeRow> WelcomeRows();

// Whether `overrides` (prefs::kShortcuts) has the Chrome side of `row`.
bool HasChromeSide(const base::DictValue& overrides, std::string_view row);

// Sets `row` in `overrides` to Chrome's side (its keys written) or Zen's (its
// keys removed, the defaults again). An unknown row does nothing.
void SetWelcomeSide(base::DictValue& overrides,
                    std::string_view row,
                    bool chrome);

}  // namespace koan::shortcuts

#endif  // KOAN_BROWSER_KOAN_WELCOME_SHORTCUTS_H_
