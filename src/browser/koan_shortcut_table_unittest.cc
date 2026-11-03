// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_shortcut_table.h"

#include <set>
#include <string>
#include <vector>

#include "base/values.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace koan::shortcuts {

namespace {

constexpr int kCtrl = ui::EF_CONTROL_DOWN;
constexpr int kAlt = ui::EF_ALT_DOWN;
constexpr int kShift = ui::EF_SHIFT_DOWN;

const Binding* FindBinding(const std::vector<Binding>& bindings,
                           const ui::Accelerator& accelerator) {
  for (const Binding& binding : bindings) {
    if (binding.accelerator == accelerator) {
      return &binding;
    }
  }
  return nullptr;
}

bool HasAction(const std::vector<Binding>& bindings, Action action) {
  for (const Binding& binding : bindings) {
    if (binding.action == action) {
      return true;
    }
  }
  return false;
}

// Every accelerator is bound to at most one action.
void ExpectNoDuplicateAccelerators(const std::vector<Binding>& bindings) {
  for (size_t i = 0; i < bindings.size(); ++i) {
    for (size_t j = i + 1; j < bindings.size(); ++j) {
      EXPECT_FALSE(bindings[i].accelerator == bindings[j].accelerator)
          << "bound twice: key " << bindings[i].accelerator.key_code()
          << " modifiers " << bindings[i].accelerator.modifiers();
    }
  }
}

base::DictValue Override(ui::KeyboardCode key, int modifiers) {
  return ToPrefValue(ui::Accelerator(key, modifiers));
}

}  // namespace

TEST(KoanShortcutTableTest, ActionIdsAreUnique) {
  std::set<std::string> ids;
  for (const Info& info : Actions()) {
    EXPECT_TRUE(ids.insert(info.id).second) << info.id;
  }
}

TEST(KoanShortcutTableTest, EveryActionHasOneInfo) {
  // kOpenSettings is the last of the enum.
  EXPECT_EQ(static_cast<size_t>(Action::kOpenSettings) + 1, Actions().size());
  std::set<Action> actions;
  for (const Info& info : Actions()) {
    EXPECT_TRUE(actions.insert(info.action).second) << info.id;
  }
}

TEST(KoanShortcutTableTest, EveryActionIsInAGroup) {
  std::set<std::string> groups;
  for (const Group& group : Groups()) {
    EXPECT_TRUE(groups.insert(group.id).second) << group.id;
  }
  for (const Info& info : Actions()) {
    EXPECT_TRUE(groups.contains(info.group)) << info.id;
    EXPECT_NE(std::string(), info.label) << info.id;
  }
}

TEST(KoanShortcutTableTest, ActionsFollowGroupOrder) {
  // Actions() is "grouped in Groups() order": the settings page lists them
  // as they come.
  size_t group_index = 0;
  for (const Info& info : Actions()) {
    while (group_index < Groups().size() &&
           std::string(Groups()[group_index].id) != info.group) {
      ++group_index;
    }
    EXPECT_LT(group_index, Groups().size()) << info.id << " is out of order";
    if (group_index == Groups().size()) {
      break;
    }
  }
}

TEST(KoanShortcutTableTest, FindAction) {
  const Info* info = FindAction("new-tab");
  ASSERT_TRUE(info);
  EXPECT_EQ(Action::kNewTab, info->action);
  EXPECT_FALSE(FindAction("no-such-action"));
  EXPECT_FALSE(FindAction(""));
  for (const Info& action : Actions()) {
    EXPECT_EQ(&action, FindAction(action.id));
  }
}

TEST(KoanShortcutTableTest, DefaultBindingsHaveNoDuplicateAccelerators) {
  ExpectNoDuplicateAccelerators(DefaultBindings());
}

TEST(KoanShortcutTableTest, DefaultBindingsAreForKnownActions) {
  for (const Binding& binding : DefaultBindings()) {
    bool found = false;
    for (const Info& info : Actions()) {
      found |= info.action == binding.action;
    }
    EXPECT_TRUE(found) << static_cast<int>(binding.action);
    EXPECT_NE(ui::VKEY_UNKNOWN, binding.accelerator.key_code());
  }
}

TEST(KoanShortcutTableTest, DefaultBindingsLeaveTabCyclingKeysAlone) {
  for (const Binding& binding : DefaultBindings()) {
    for (const CycleKey& cycle_key : CycleKeys()) {
      EXPECT_FALSE(binding.accelerator ==
                   ui::Accelerator(cycle_key.key, cycle_key.modifiers));
    }
  }
}

TEST(KoanShortcutTableTest, ZenDefaults) {
  const std::vector<Binding> bindings = DefaultBindings();
  const auto expect = [&](ui::KeyboardCode key, int modifiers, Action action) {
    const Binding* binding =
        FindBinding(bindings, ui::Accelerator(key, modifiers));
    ASSERT_TRUE(binding) << "no binding for key " << key;
    EXPECT_EQ(action, binding->action);
  };
  expect(ui::VKEY_S, kCtrl, Action::kToggleCompactMode);
  expect(ui::VKEY_S, kCtrl | kAlt | kShift, Action::kSavePage);
  // Zen's "accel": Cmd on macOS, Ctrl elsewhere.
  expect(ui::VKEY_T, ui::EF_PLATFORM_ACCELERATOR, Action::kNewTab);
  expect(ui::VKEY_RIGHT, ui::EF_PLATFORM_ACCELERATOR | kAlt,
         Action::kWorkspaceForward);
  expect(ui::VKEY_LEFT, ui::EF_PLATFORM_ACCELERATOR | kAlt,
         Action::kWorkspaceBackward);
  expect(ui::VKEY_G, ui::EF_PLATFORM_ACCELERATOR | kAlt, Action::kSplitGrid);
  // Zen moves the inspector and Save Page off the browser's keys.
  expect(ui::VKEY_L, kCtrl | kShift, Action::kInspect);
  expect(ui::VKEY_OEM_COMMA, kCtrl, Action::kOpenSettings);
}

TEST(KoanShortcutTableTest, SwitchWorkspaceKeysAreMacOnly) {
  const std::vector<Binding> bindings = DefaultBindings();
#if BUILDFLAG(IS_MAC)
  // Zen on macOS: Ctrl+1..9 and Ctrl+0 switch to Space 1..10.
  for (int number = 1; number <= 10; ++number) {
    const ui::Accelerator accelerator(
        static_cast<ui::KeyboardCode>(ui::VKEY_0 + number % 10), kCtrl);
    const Binding* binding = FindBinding(bindings, accelerator);
    ASSERT_TRUE(binding) << number;
    EXPECT_EQ(number, SwitchWorkspaceNumber(binding->action));
  }
#else
  for (const Binding& binding : bindings) {
    EXPECT_EQ(0, SwitchWorkspaceNumber(binding.action));
  }
#endif
}

TEST(KoanShortcutTableTest, SwitchWorkspaceNumber) {
  EXPECT_EQ(1, SwitchWorkspaceNumber(Action::kSwitchWorkspace1));
  EXPECT_EQ(9, SwitchWorkspaceNumber(Action::kSwitchWorkspace9));
  EXPECT_EQ(10, SwitchWorkspaceNumber(Action::kSwitchWorkspace10));
  EXPECT_EQ(0, SwitchWorkspaceNumber(Action::kNewTab));
  EXPECT_EQ(0, SwitchWorkspaceNumber(Action::kNewWorkspace));
}

TEST(KoanShortcutTableTest, NoOverridesGivesTheDefaults) {
  const std::vector<Binding> defaults = DefaultBindings();
  const std::vector<Binding> bindings = Bindings(base::DictValue());
  ASSERT_EQ(defaults.size(), bindings.size());
  for (size_t i = 0; i < defaults.size(); ++i) {
    EXPECT_TRUE(defaults[i].accelerator == bindings[i].accelerator);
    EXPECT_EQ(defaults[i].action, bindings[i].action);
  }
}

TEST(KoanShortcutTableTest, CustomKeyReplacesTheDefault) {
  const std::vector<Binding> bindings = Bindings(
      base::DictValue().Set("toggle-pin-tab", Override(ui::VKEY_J, kCtrl)));
  const Binding* custom =
      FindBinding(bindings, ui::Accelerator(ui::VKEY_J, kCtrl));
  ASSERT_TRUE(custom);
  EXPECT_EQ(Action::kTogglePinTab, custom->action);
  // The default key (Ctrl+Shift+D) is free.
  EXPECT_FALSE(
      FindBinding(bindings, ui::Accelerator(ui::VKEY_D, kCtrl | kShift)));
  ExpectNoDuplicateAccelerators(bindings);
}

TEST(KoanShortcutTableTest, ClearedShortcutHasNoKey) {
  const std::vector<Binding> bindings = Bindings(
      base::DictValue().Set("copy-url", base::DictValue().Set("key", 0)));
  EXPECT_FALSE(HasAction(bindings, Action::kCopyUrl));
  // The others keep theirs.
  EXPECT_TRUE(HasAction(bindings, Action::kCopyUrlMarkdown));
}

TEST(KoanShortcutTableTest, UsersKeyWinsOverAnotherDefaultWithTheSameKey) {
  // Ctrl+Shift+P is "private window" by default.
  const std::vector<Binding> bindings = Bindings(base::DictValue().Set(
      "new-window", Override(ui::VKEY_P, kCtrl | kShift)));
  const Binding* binding =
      FindBinding(bindings, ui::Accelerator(ui::VKEY_P, kCtrl | kShift));
  ASSERT_TRUE(binding);
  EXPECT_EQ(Action::kNewWindow, binding->action);
  EXPECT_FALSE(HasAction(bindings, Action::kNewPrivateWindow));
  ExpectNoDuplicateAccelerators(bindings);
}

TEST(KoanShortcutTableTest, MalformedOverridesKeepTheDefault) {
  base::DictValue overrides;
  overrides.Set("no-such-action", Override(ui::VKEY_J, kCtrl));
  overrides.Set("copy-url", "not a dictionary");
  // No modifier: a plain key would take it away from typing.
  overrides.Set("new-window", Override(ui::VKEY_J, 0));
  // A tab cycling key.
  overrides.Set("downloads", Override(ui::VKEY_TAB, kCtrl));
  overrides.Set("bookmarks", base::DictValue());
  const std::vector<Binding> bindings = Bindings(overrides);

  const std::vector<Binding> defaults = DefaultBindings();
  ASSERT_EQ(defaults.size(), bindings.size());
  for (size_t i = 0; i < defaults.size(); ++i) {
    EXPECT_TRUE(defaults[i].accelerator == bindings[i].accelerator);
    EXPECT_EQ(defaults[i].action, bindings[i].action);
  }
}

TEST(KoanShortcutTableTest, TwoActionsCannotShareACustomKey) {
  base::DictValue overrides;
  overrides.Set("downloads", Override(ui::VKEY_J, kCtrl));
  overrides.Set("bookmarks", Override(ui::VKEY_J, kCtrl));
  const std::vector<Binding> bindings = Bindings(overrides);
  ExpectNoDuplicateAccelerators(bindings);
  // One of them has it; the other falls back to its default.
  EXPECT_TRUE(HasAction(bindings, Action::kDownloads));
  EXPECT_TRUE(HasAction(bindings, Action::kBookmarks));
}

TEST(KoanShortcutTableTest, ValidCustomAccelerators) {
  EXPECT_TRUE(IsValidCustomAccelerator(ui::Accelerator(ui::VKEY_J, kCtrl)));
  EXPECT_TRUE(IsValidCustomAccelerator(ui::Accelerator(ui::VKEY_J, kAlt)));
  EXPECT_TRUE(IsValidCustomAccelerator(
      ui::Accelerator(ui::VKEY_J, ui::EF_COMMAND_DOWN)));
  EXPECT_TRUE(IsValidCustomAccelerator(ui::Accelerator(ui::VKEY_F5, 0)));
  EXPECT_TRUE(IsValidCustomAccelerator(ui::Accelerator(ui::VKEY_F24, kShift)));
}

TEST(KoanShortcutTableTest, InvalidCustomAccelerators) {
  EXPECT_FALSE(IsValidCustomAccelerator(ui::Accelerator()));
  // A plain key or Shift alone would take a key away from typing.
  EXPECT_FALSE(IsValidCustomAccelerator(ui::Accelerator(ui::VKEY_J, 0)));
  EXPECT_FALSE(IsValidCustomAccelerator(ui::Accelerator(ui::VKEY_J, kShift)));
  // Modifier keys by themselves.
  EXPECT_FALSE(
      IsValidCustomAccelerator(ui::Accelerator(ui::VKEY_SHIFT, kCtrl)));
  EXPECT_FALSE(
      IsValidCustomAccelerator(ui::Accelerator(ui::VKEY_CONTROL, kCtrl)));
}

TEST(KoanShortcutTableTest, TabCyclingKeysAreNotCustomizable) {
  for (const CycleKey& cycle_key : CycleKeys()) {
    EXPECT_FALSE(IsValidCustomAccelerator(
        ui::Accelerator(cycle_key.key, cycle_key.modifiers)));
  }
}

TEST(KoanShortcutTableTest, PrefValueRoundTrip) {
  const ui::Accelerator accelerator(ui::VKEY_K, kCtrl | kAlt);
  EXPECT_TRUE(accelerator == FromPrefValue(ToPrefValue(accelerator)));
}

TEST(KoanShortcutTableTest, InvalidPrefValuesGiveNoKey) {
  EXPECT_EQ(ui::VKEY_UNKNOWN, FromPrefValue(base::DictValue()).key_code());
  EXPECT_EQ(ui::VKEY_UNKNOWN,
            FromPrefValue(base::DictValue().Set("key", -4)).key_code());
  EXPECT_EQ(ui::VKEY_UNKNOWN,
            FromPrefValue(base::DictValue().Set("key", 4000)).key_code());
  // Valid key, but not usable as a shortcut (no modifier).
  EXPECT_EQ(
      ui::VKEY_UNKNOWN,
      FromPrefValue(ToPrefValue(ui::Accelerator(ui::VKEY_J, 0))).key_code());
}

#if !BUILDFLAG(IS_MAC)
// On macOS the text comes from the platform (glyphs), so this is the other
// platforms' Zen format.
TEST(KoanShortcutTableTest, DisplayTextIsZens) {
  EXPECT_EQ(u"Ctrl+S", DisplayText(ui::Accelerator(ui::VKEY_S, kCtrl)));
  EXPECT_EQ(u"Ctrl+Alt+⇧+C",
            DisplayText(ui::Accelerator(ui::VKEY_C, kCtrl | kAlt | kShift)));
  EXPECT_EQ(u"Ctrl+,", DisplayText(ui::Accelerator(ui::VKEY_OEM_COMMA, kCtrl)));
  EXPECT_EQ(u"Alt+→", DisplayText(ui::Accelerator(ui::VKEY_RIGHT, kAlt)));
  EXPECT_EQ(u"F5", DisplayText(ui::Accelerator(ui::VKEY_F5, 0)));
  EXPECT_EQ(u"Ctrl+Num3",
            DisplayText(ui::Accelerator(ui::VKEY_NUMPAD3, kCtrl)));
}
#endif

}  // namespace koan::shortcuts
