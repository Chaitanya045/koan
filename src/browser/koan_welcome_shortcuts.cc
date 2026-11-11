// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_welcome_shortcuts.h"

#include <algorithm>
#include <array>

#include "build/build_config.h"
#include "koan/browser/koan_shortcut_table.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace koan::shortcuts {

namespace {

// One user key Chrome's side writes: `key` for the action `id`, or none
// (VKEY_UNKNOWN: the action's keys cleared, so Chrome's own run again).
struct Override {
  const char* id;
  ui::KeyboardCode key;
  int modifiers;
};

struct RowOverrides {
  const char* row;
  std::array<Override, 2> overrides;
  size_t count;
};

#if BUILDFLAG(IS_MAC)
constexpr int kCmd = ui::EF_COMMAND_DOWN;
constexpr int kCtrl = ui::EF_CONTROL_DOWN;
constexpr int kAlt = ui::EF_ALT_DOWN;
constexpr int kShift = ui::EF_SHIFT_DOWN;

// Chrome's keys on macOS for what Zen moved (Chrome's menus and
// accelerators_cocoa.mm), and the Zen keys that take Chrome's.
constexpr WelcomeRow kRows[] = {
    {"save-page", u"⌘S", {u"⌘S", u"Compact mode"}, {u"⌘S", u"Save page"}},
    {"inspector", u"Inspect", {u"⌥⌘L", u"Inspect"}, {u"⌥⌘C", u"Inspect"}},
    {"downloads", u"Downloads", {u"⌘J", u"Downloads"}, {u"⇧⌘J", u"Downloads"}},
    {"private-window",
     u"Private window",
     {u"⇧⌘P", u"Private window"},
     {u"⇧⌘N", u"Private window"}},
    {"option-arrows",
     u"⌥⌘← →",
     {u"⌥⌘← →", u"Switch Space"},
     {u"⌥⌘← →", u"Switch tab"}},
    {"pin-tab", u"⇧⌘D", {u"⇧⌘D", u"Pin tab"}, {u"⇧⌘D", u"Bookmark all tabs"}},
};

constexpr RowOverrides kOverrides[] = {
    // Save Page goes back to Cmd+S; compact mode moves to Ctrl+Cmd+S.
    {"save-page",
     {{{"save-page", ui::VKEY_S, kCmd},
       {"compact-mode-toggle", ui::VKEY_S, kCtrl | kCmd}}},
     2},
    // Inspect Elements is Chrome's Cmd+Option+C (Cmd+Option+I, DevTools, is
    // Chrome's either way).
    {"inspector", {{{"inspector", ui::VKEY_C, kAlt | kCmd}}}, 1},
    {"downloads", {{{"downloads", ui::VKEY_J, kShift | kCmd}}}, 1},
    // Chrome's private window key; Zen's new window on it goes.
    {"private-window", {{{"private-browsing", ui::VKEY_N, kShift | kCmd}}}, 1},
    // Without Koan's keys, Chrome's reserved Cmd+Option+arrows switch tabs.
    {"option-arrows",
     {{{"workspace-forward", ui::VKEY_UNKNOWN, 0},
       {"workspace-backward", ui::VKEY_UNKNOWN, 0}}},
     2},
    // Without pinning on it, Chrome's Bookmark All Tabs.
    {"pin-tab", {{{"toggle-pin-tab", ui::VKEY_UNKNOWN, 0}}}, 1},
};
#endif

base::span<const Override> OverridesOf(std::string_view row) {
#if BUILDFLAG(IS_MAC)
  for (const RowOverrides& entry : kOverrides) {
    if (row == entry.row) {
      return base::span(entry.overrides).first(entry.count);
    }
  }
#endif
  return {};
}

base::DictValue PrefValue(const Override& entry) {
  return ToPrefValue(ui::Accelerator(entry.key, entry.modifiers));
}

}  // namespace

base::span<const WelcomeRow> WelcomeRows() {
#if BUILDFLAG(IS_MAC)
  return kRows;
#else
  return {};
#endif
}

bool HasChromeSide(const base::DictValue& overrides, std::string_view row) {
  const base::span<const Override> entries = OverridesOf(row);
  return !entries.empty() &&
         std::ranges::all_of(entries, [&](const Override& entry) {
           const base::DictValue* value = overrides.FindDict(entry.id);
           return value && *value == PrefValue(entry);
         });
}

void SetWelcomeSide(base::DictValue& overrides,
                    std::string_view row,
                    bool chrome) {
  for (const Override& entry : OverridesOf(row)) {
    if (chrome) {
      overrides.Set(entry.id, PrefValue(entry));
    } else {
      overrides.Remove(entry.id);
    }
  }
}

}  // namespace koan::shortcuts
