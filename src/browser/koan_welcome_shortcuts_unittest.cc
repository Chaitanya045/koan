// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_welcome_shortcuts.h"

#include <vector>

#include "base/values.h"
#include "build/build_config.h"
#include "koan/browser/koan_shortcut_table.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace koan::shortcuts {

#if BUILDFLAG(IS_MAC)

namespace {

// The actions bound to `accelerator` with these overrides.
std::vector<Action> ActionsOn(const base::DictValue& overrides,
                              const ui::Accelerator& accelerator) {
  std::vector<Action> actions;
  for (const Binding& binding : Bindings(overrides)) {
    if (binding.accelerator == accelerator) {
      actions.push_back(binding.action);
    }
  }
  return actions;
}

}  // namespace

constexpr int kCmd = ui::EF_COMMAND_DOWN;

TEST(KoanWelcomeShortcutsTest, EveryRowStartsOnZensSide) {
  const base::DictValue none;
  for (const WelcomeRow& row : WelcomeRows()) {
    EXPECT_FALSE(HasChromeSide(none, row.id)) << row.id;
  }
}

TEST(KoanWelcomeShortcutsTest, ChromesSideMovesTheKeysAndZensSideRestores) {
  base::DictValue overrides;
  for (const WelcomeRow& row : WelcomeRows()) {
    SetWelcomeSide(overrides, row.id, /*chrome=*/true);
    EXPECT_TRUE(HasChromeSide(overrides, row.id)) << row.id;
  }
  // Cmd+S saves the page; compact mode is on Ctrl+Cmd+S.
  EXPECT_EQ(ActionsOn(overrides, ui::Accelerator(ui::VKEY_S, kCmd)),
            std::vector<Action>{Action::kSavePage});
  EXPECT_EQ(ActionsOn(overrides,
                      ui::Accelerator(ui::VKEY_S, kCmd | ui::EF_CONTROL_DOWN)),
            std::vector<Action>{Action::kToggleCompactMode});
  // Chrome's keys for Inspect, Downloads and the private window.
  EXPECT_EQ(
      ActionsOn(overrides, ui::Accelerator(ui::VKEY_C, kCmd | ui::EF_ALT_DOWN)),
      std::vector<Action>{Action::kInspect});
  EXPECT_EQ(ActionsOn(overrides,
                      ui::Accelerator(ui::VKEY_J, kCmd | ui::EF_SHIFT_DOWN)),
            std::vector<Action>{Action::kDownloads});
  EXPECT_EQ(ActionsOn(overrides,
                      ui::Accelerator(ui::VKEY_N, kCmd | ui::EF_SHIFT_DOWN)),
            std::vector<Action>{Action::kNewPrivateWindow});
  // Nothing of Koan's on Chrome's Cmd+Option+arrows and Cmd+Shift+D.
  EXPECT_TRUE(ActionsOn(overrides,
                        ui::Accelerator(ui::VKEY_RIGHT, kCmd | ui::EF_ALT_DOWN))
                  .empty());
  EXPECT_TRUE(ActionsOn(overrides,
                        ui::Accelerator(ui::VKEY_D, kCmd | ui::EF_SHIFT_DOWN))
                  .empty());

  // Zen's side again: no user keys left, the defaults.
  for (const WelcomeRow& row : WelcomeRows()) {
    SetWelcomeSide(overrides, row.id, /*chrome=*/false);
    EXPECT_FALSE(HasChromeSide(overrides, row.id)) << row.id;
  }
  EXPECT_TRUE(overrides.empty());
}

TEST(KoanWelcomeShortcutsTest, OneRowLeavesTheOthers) {
  base::DictValue overrides;
  SetWelcomeSide(overrides, "downloads", /*chrome=*/true);
  EXPECT_TRUE(HasChromeSide(overrides, "downloads"));
  EXPECT_FALSE(HasChromeSide(overrides, "save-page"));
  EXPECT_EQ(ActionsOn(overrides, ui::Accelerator(ui::VKEY_S, kCmd)),
            std::vector<Action>{Action::kToggleCompactMode});
}

#endif  // BUILDFLAG(IS_MAC)

TEST(KoanWelcomeShortcutsTest, AnUnknownRowDoesNothing) {
  base::DictValue overrides;
  SetWelcomeSide(overrides, "no-such-row", /*chrome=*/true);
  EXPECT_TRUE(overrides.empty());
  EXPECT_FALSE(HasChromeSide(overrides, "no-such-row"));
}

#if !BUILDFLAG(IS_MAC)
TEST(KoanWelcomeShortcutsTest, NoRowsOffMac) {
  EXPECT_TRUE(WelcomeRows().empty());
}
#endif

}  // namespace koan::shortcuts
