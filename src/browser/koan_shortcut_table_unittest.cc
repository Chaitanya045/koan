// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Unit tests for Koan's shortcut table against Zen 1.24b's keys
// (.agent/notes/zen-behaviour/keyboard-shortcuts.md, from
// src/browser/base/content/zen-keysets.inc.xhtml and a live dump of Zen on
// macOS). Build and run:
//   autoninja -C out/Koan koan/browser:koan_shortcut_unittests
//   out/Koan/koan_shortcut_unittests

#include "koan/browser/koan_shortcut_table.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/utf_ostream_operators.h"
#include "base/values.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace koan::shortcuts {

namespace {

// Zen's "accel", and the Control key that Zen writes as "control" (it is
// Control on macOS and Ctrl elsewhere).
constexpr int kAccel = ui::EF_PLATFORM_ACCELERATOR;
constexpr int kCtrl = ui::EF_CONTROL_DOWN;
constexpr int kAlt = ui::EF_ALT_DOWN;
constexpr int kShift = ui::EF_SHIFT_DOWN;
constexpr int kCmd = ui::EF_COMMAND_DOWN;

using Keys = std::vector<ui::Accelerator>;

Keys KeysOf(Action action) {
  Keys keys;
  for (const Binding& binding : DefaultBindings()) {
    if (binding.action == action) {
      keys.push_back(binding.accelerator);
    }
  }
  return keys;
}

ui::Accelerator Key(ui::KeyboardCode key, int modifiers) {
  return ui::Accelerator(key, modifiers);
}

bool IsBound(const ui::Accelerator& accelerator, Action action) {
  for (const ui::Accelerator& key : KeysOf(action)) {
    if (key == accelerator) {
      return true;
    }
  }
  return false;
}

}  // namespace

// Zen's own keys (zenKeyset) are "accel" on every platform: Cmd on a Mac.
TEST(KoanShortcutTable, ZenKeysFollowAccel) {
  EXPECT_TRUE(IsBound(Key(ui::VKEY_S, kAccel), Action::kToggleCompactMode));
  EXPECT_TRUE(
      IsBound(Key(ui::VKEY_S, kAccel | kAlt), Action::kCompactModeShowSidebar));
  EXPECT_TRUE(
      IsBound(Key(ui::VKEY_RIGHT, kAccel | kAlt), Action::kWorkspaceForward));
  EXPECT_TRUE(
      IsBound(Key(ui::VKEY_LEFT, kAccel | kAlt), Action::kWorkspaceBackward));
  EXPECT_TRUE(
      IsBound(Key(ui::VKEY_K, kAccel | kShift), Action::kCloseUnpinnedTabs));
  EXPECT_TRUE(IsBound(Key(ui::VKEY_G, kAccel | kAlt), Action::kSplitGrid));
  EXPECT_TRUE(IsBound(Key(ui::VKEY_V, kAccel | kAlt), Action::kSplitVertical));
  EXPECT_TRUE(
      IsBound(Key(ui::VKEY_H, kAccel | kAlt), Action::kSplitHorizontal));
  EXPECT_TRUE(IsBound(Key(ui::VKEY_U, kAccel | kAlt), Action::kUnsplit));
  EXPECT_TRUE(
      IsBound(Key(ui::VKEY_8, kAccel | kShift), Action::kNewEmptySplit));
  EXPECT_TRUE(IsBound(Key(ui::VKEY_L, kAlt | kShift), Action::kLibrary));
  EXPECT_TRUE(IsBound(Key(ui::VKEY_O, kAccel), Action::kGlanceExpand));
  EXPECT_TRUE(IsBound(Key(ui::VKEY_D, kAccel | kShift), Action::kTogglePinTab));
  EXPECT_TRUE(
      IsBound(Key(ui::VKEY_N, kAccel | kShift), Action::kNewBlankWindow));
  EXPECT_TRUE(IsBound(Key(ui::VKEY_C, kAccel | kShift), Action::kCopyUrl));
  EXPECT_TRUE(IsBound(Key(ui::VKEY_C, kAccel | kShift | kAlt),
                      Action::kCopyUrlMarkdown));
  // Save Page moves off Accel+S.
  EXPECT_TRUE(
      IsBound(Key(ui::VKEY_S, kAccel | kAlt | kShift), Action::kSavePage));
  // Firefox's private window, bookmarks sidebar and reader mode.
  EXPECT_TRUE(
      IsBound(Key(ui::VKEY_P, kAccel | kShift), Action::kNewPrivateWindow));
  EXPECT_TRUE(IsBound(Key(ui::VKEY_B, kAccel), Action::kBookmarks));
  EXPECT_TRUE(IsBound(Key(ui::VKEY_R, kAccel | kAlt), Action::kReaderMode));
  EXPECT_TRUE(IsBound(Key(ui::VKEY_OEM_COMMA, kAccel), Action::kOpenSettings));
  // Firefox's mute is Control+M, not accel, even on a Mac.
  EXPECT_TRUE(IsBound(Key(ui::VKEY_M, kCtrl), Action::kMuteSite));
}

// The whole macOS table, key for key (the live dump of Zen 1.24b).
#if BUILDFLAG(IS_MAC)
TEST(KoanShortcutTable, MacDefaultsAreZens) {
  EXPECT_EQ(KeysOf(Action::kToggleCompactMode), Keys({Key(ui::VKEY_S, kCmd)}));
  EXPECT_EQ(KeysOf(Action::kCompactModeShowSidebar),
            Keys({Key(ui::VKEY_S, kCmd | kAlt)}));
  EXPECT_EQ(KeysOf(Action::kSavePage),
            Keys({Key(ui::VKEY_S, kCmd | kAlt | kShift)}));
  EXPECT_EQ(KeysOf(Action::kWorkspaceForward),
            Keys({Key(ui::VKEY_RIGHT, kCmd | kAlt)}));
  EXPECT_EQ(KeysOf(Action::kWorkspaceBackward),
            Keys({Key(ui::VKEY_LEFT, kCmd | kAlt)}));
  EXPECT_EQ(KeysOf(Action::kCloseUnpinnedTabs),
            Keys({Key(ui::VKEY_K, kCmd | kShift)}));
  EXPECT_EQ(KeysOf(Action::kLibrary), Keys({Key(ui::VKEY_L, kAlt | kShift)}));
  EXPECT_EQ(KeysOf(Action::kGlanceExpand), Keys({Key(ui::VKEY_O, kCmd)}));
  EXPECT_EQ(KeysOf(Action::kTogglePinTab),
            Keys({Key(ui::VKEY_D, kCmd | kShift)}));
  EXPECT_EQ(KeysOf(Action::kNewBlankWindow),
            Keys({Key(ui::VKEY_N, kCmd | kShift)}));
  EXPECT_EQ(KeysOf(Action::kNewPrivateWindow),
            Keys({Key(ui::VKEY_P, kCmd | kShift)}));
  EXPECT_EQ(KeysOf(Action::kCopyUrl), Keys({Key(ui::VKEY_C, kCmd | kShift)}));
  EXPECT_EQ(KeysOf(Action::kCopyUrlMarkdown),
            Keys({Key(ui::VKEY_C, kCmd | kShift | kAlt)}));
  // Cmd+Shift+* is Shift+8, or the keypad's.
  EXPECT_EQ(KeysOf(Action::kNewEmptySplit),
            Keys({Key(ui::VKEY_8, kCmd | kShift),
                  Key(ui::VKEY_MULTIPLY, kCmd | kShift)}));
  EXPECT_EQ(KeysOf(Action::kSplitGrid), Keys({Key(ui::VKEY_G, kCmd | kAlt)}));
  EXPECT_EQ(KeysOf(Action::kSplitVertical),
            Keys({Key(ui::VKEY_V, kCmd | kAlt)}));
  EXPECT_EQ(KeysOf(Action::kSplitHorizontal),
            Keys({Key(ui::VKEY_H, kCmd | kAlt)}));
  EXPECT_EQ(KeysOf(Action::kUnsplit), Keys({Key(ui::VKEY_U, kCmd | kAlt)}));
  // Firefox's keys as they are on a Mac: key_toggleMute, key_openDownloads,
  // key_inspector (moved from "C" to "L" by Zen) and key_inspectorMac,
  // showAllHistoryKb and key_gotoHistory.
  EXPECT_EQ(KeysOf(Action::kMuteSite),
            Keys({Key(ui::VKEY_M, ui::EF_CONTROL_DOWN)}));
  EXPECT_EQ(KeysOf(Action::kDownloads), Keys({Key(ui::VKEY_J, kCmd)}));
  EXPECT_EQ(KeysOf(Action::kInspect), Keys({Key(ui::VKEY_L, kCmd | kAlt),
                                            Key(ui::VKEY_L, kCmd | kShift)}));
  EXPECT_EQ(KeysOf(Action::kLibraryHistory),
            Keys({Key(ui::VKEY_H, kCmd | kShift), Key(ui::VKEY_Y, kCmd)}));
  EXPECT_EQ(KeysOf(Action::kBookmarks), Keys({Key(ui::VKEY_B, kCmd)}));
  EXPECT_EQ(KeysOf(Action::kReaderMode), Keys({Key(ui::VKEY_R, kCmd | kAlt)}));
  EXPECT_EQ(KeysOf(Action::kOpenSettings),
            Keys({Key(ui::VKEY_OEM_COMMA, kCmd)}));
  // Ctrl+1 ... Ctrl+9, Ctrl+0: Space 1 to 10 (Control, not Cmd).
  for (int number = 1; number <= 10; ++number) {
    const Action action = static_cast<Action>(
        static_cast<int>(Action::kSwitchWorkspace1) + number - 1);
    EXPECT_EQ(KeysOf(action),
              Keys({Key(static_cast<ui::KeyboardCode>(ui::VKEY_0 + number % 10),
                        ui::EF_CONTROL_DOWN)}))
        << number;
  }
  // Koan's own, away from Chrome's Cmd+Option+B and Cmd+Option+N.
  EXPECT_EQ(KeysOf(Action::kToggleSingleToolbar),
            Keys({Key(ui::VKEY_B, ui::EF_CONTROL_DOWN | kAlt)}));
  EXPECT_EQ(KeysOf(Action::kNewWorkspace),
            Keys({Key(ui::VKEY_N, ui::EF_CONTROL_DOWN | kAlt)}));
}
#endif  // BUILDFLAG(IS_MAC)

// No key runs two actions.
TEST(KoanShortcutTable, NoKeyIsBoundToTwoActions) {
  std::set<std::pair<int, int>> seen;
  for (const Binding& binding : DefaultBindings()) {
    const auto id =
        std::make_pair(static_cast<int>(binding.accelerator.key_code()),
                       binding.accelerator.modifiers());
    EXPECT_TRUE(seen.insert(id).second)
        << DisplayText(binding.accelerator, /*macos=*/true);
  }
}

// Every action has a key, except the ones Zen leaves without (Reset Pinned
// Tab) and the Space keys outside macOS.
TEST(KoanShortcutTable, EveryActionHasAKeyExceptZens) {
  for (const Info& info : Actions()) {
    if (info.action == Action::kResetPinnedTab) {
      continue;
    }
#if !BUILDFLAG(IS_MAC)
    if (SwitchWorkspaceNumber(info.action)) {
      continue;
    }
#endif
    EXPECT_FALSE(KeysOf(info.action).empty()) << info.id;
  }
}

// A default key can be set as a custom one, so the settings page can show
// and restore it.
TEST(KoanShortcutTable, DefaultsAreValidCustomKeys) {
  for (const Binding& binding : DefaultBindings()) {
    EXPECT_TRUE(IsValidCustomAccelerator(binding.accelerator))
        << DisplayText(binding.accelerator, /*macos=*/true);
  }
}

TEST(KoanShortcutTable, ReservedAndSystemKeysOnMac) {
  // Chrome's reserved commands.
  EXPECT_TRUE(IsReservedOnMac(Key(ui::VKEY_N, kCmd | kShift)));
  EXPECT_TRUE(IsReservedOnMac(Key(ui::VKEY_T, kCmd)));
  EXPECT_TRUE(IsReservedOnMac(Key(ui::VKEY_W, kCmd)));
  EXPECT_TRUE(IsReservedOnMac(Key(ui::VKEY_RIGHT, kCmd | kAlt)));
  EXPECT_TRUE(IsReservedOnMac(Key(ui::VKEY_LEFT, kCmd | kAlt)));
  EXPECT_TRUE(IsReservedOnMac(Key(ui::VKEY_TAB, kCtrl)));
  EXPECT_FALSE(IsReservedOnMac(Key(ui::VKEY_S, kCmd)));
  EXPECT_FALSE(IsReservedOnMac(Key(ui::VKEY_P, kCmd | kShift)));
  // macOS's.
  EXPECT_TRUE(IsSystemShortcutOnMac(Key(ui::VKEY_Q, kCmd)));
  EXPECT_TRUE(IsSystemShortcutOnMac(Key(ui::VKEY_SPACE, kCmd)));
  EXPECT_TRUE(IsSystemShortcutOnMac(Key(ui::VKEY_TAB, kCmd)));
  EXPECT_TRUE(IsSystemShortcutOnMac(Key(ui::VKEY_LEFT, kCtrl)));
  EXPECT_FALSE(IsSystemShortcutOnMac(Key(ui::VKEY_S, kCmd)));
  // Koan's own Space keys are not system keys (Ctrl+digit, Cmd+Option+arrows).
  EXPECT_FALSE(IsSystemShortcutOnMac(Key(ui::VKEY_1, kCtrl)));
  EXPECT_FALSE(IsSystemShortcutOnMac(Key(ui::VKEY_RIGHT, kCmd | kAlt)));
#if BUILDFLAG(IS_MAC)
  EXPECT_FALSE(IsValidCustomAccelerator(Key(ui::VKEY_Q, kCmd)));
  EXPECT_FALSE(IsValidCustomAccelerator(Key(ui::VKEY_SPACE, kCmd)));
  EXPECT_TRUE(IsValidCustomAccelerator(Key(ui::VKEY_J, kCmd | kShift)));
#endif
}

// Zen's notation: KeyShortcut.toDisplayString() and keyToDisplayString().
TEST(KoanShortcutTable, DisplayTextMatchesZen) {
  EXPECT_EQ(DisplayText(Key(ui::VKEY_C, kCmd | kShift), /*macos=*/true),
            u"⌘ ⇧ C");
  EXPECT_EQ(DisplayText(Key(ui::VKEY_C, kCmd | kShift | kAlt), true),
            u"⌘ ⌥ ⇧ C");
  EXPECT_EQ(DisplayText(Key(ui::VKEY_1, kCtrl), true), u"⌃ 1");
  EXPECT_EQ(DisplayText(Key(ui::VKEY_RIGHT, kCmd | kAlt), true), u"⌘ ⌥ →");
  EXPECT_EQ(DisplayText(Key(ui::VKEY_OEM_COMMA, kCmd), true), u"⌘ ,");
  EXPECT_EQ(DisplayText(Key(ui::VKEY_ESCAPE, kCtrl), true), u"⌃ ⎋");
  EXPECT_EQ(DisplayText(Key(ui::VKEY_RETURN, kCtrl), true), u"⌃ ↩");
  EXPECT_EQ(DisplayText(Key(ui::VKEY_SPACE, kCtrl), true), u"⌃ ␣");
  // Elsewhere: Ctrl+Win+Alt+⇧+Key.
  EXPECT_EQ(DisplayText(Key(ui::VKEY_C, kCtrl | kShift | kAlt), false),
            u"Ctrl+Alt+⇧+C");
  EXPECT_EQ(DisplayText(Key(ui::VKEY_OEM_COMMA, kCtrl), false), u"Ctrl+,");
  EXPECT_EQ(DisplayText(Key(ui::VKEY_ESCAPE, kCtrl), false), u"Ctrl+Esc");
  EXPECT_EQ(DisplayText(Key(ui::VKEY_SPACE, kCtrl), false), u"Ctrl+Space");
}

TEST(KoanShortcutTable, BindingsApplyTheUsersKeys) {
  const ui::Accelerator custom = Key(ui::VKEY_J, kCtrl | kAlt);
  // The user rebinds Mute; its default goes, the other defaults stay.
  base::DictValue overrides;
  overrides.Set("mute", ToPrefValue(custom));
  const std::vector<Binding> bindings = Bindings(overrides);
  int mute_keys = 0;
  for (const Binding& binding : bindings) {
    if (binding.action == Action::kMuteSite) {
      ++mute_keys;
      EXPECT_EQ(binding.accelerator, custom);
    }
  }
  EXPECT_EQ(mute_keys, 1);
  // The user clears Library's key: key 0.
  base::DictValue cleared;
  cleared.Set("library", ToPrefValue(ui::Accelerator()));
  for (const Binding& binding : Bindings(cleared)) {
    EXPECT_NE(binding.action, Action::kLibrary);
  }
}

TEST(KoanShortcutTable, PrefValuesRoundTrip) {
  const ui::Accelerator key = Key(ui::VKEY_K, kAccel | kShift);
  EXPECT_EQ(FromPrefValue(ToPrefValue(key)), key);
  // A key without a modifier is not a shortcut.
  EXPECT_EQ(FromPrefValue(ToPrefValue(Key(ui::VKEY_K, 0))).key_code(),
            ui::VKEY_UNKNOWN);
}

}  // namespace koan::shortcuts
