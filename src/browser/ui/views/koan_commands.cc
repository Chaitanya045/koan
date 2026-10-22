// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenUBGlobalActions.sys.mjs
//   zen-command-palette.ftl
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_commands.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/containers/fixed_flat_map.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_shortcut_table.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_command_registry.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_shortcuts.h"
#include "koan/browser/ui/views/koan_theme_controller.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/accelerators/accelerator.h"

namespace koan {

namespace {

using Action = KoanShortcutController::Action;
using ColorScheme = ThemeService::BrowserColorScheme;

BrowserWindowInterface* BrowserOf(const KoanBrowserViewController& controller) {
  return controller.browser_view()->browser();
}

bool IsEnabled(const KoanBrowserViewController& controller, int command_id) {
  return chrome::IsCommandEnabled(BrowserOf(controller), command_id);
}

ThemeService* ThemeServiceOf(const KoanBrowserViewController& controller) {
  return ThemeServiceFactory::GetForProfile(
      controller.browser_view()->GetProfile());
}

// The actions' positions in Zen's list (globalActionsTemplate in
// ZenUBGlobalActions.sys.mjs), including those Koan doesn't have yet, so that
// a feature adding one (#41, #42, #43) can take Zen's place.
constexpr auto kZenOrder = base::MakeFixedFlatMap<std::string_view, int>({
    {"toggle-compact-mode", 0},
    {"open-theme-picker", 1},
    {"open-library", 2},
    {"new-split-view", 3},
    {"unsplit-view", 4},
    {"new-space", 5},
    {"new-folder", 6},
    {"collapse-all-folders", 7},
    {"expand-all-folders", 8},
    {"copy-current-url", 9},
    {"settings", 10},
    {"open-private-window", 11},
    {"open-new-window", 12},
    {"pin-tab", 14},
    {"unpin-tab", 15},
    {"next-space", 18},
    {"previous-space", 19},
    {"close-tab", 20},
    {"reopen-closed-tab", 21},
    {"duplicate-tab", 22},
    {"reset-pinned-tab", 23},
    {"reload-tab", 24},
    {"reload-tab-without-cache", 25},
    {"next-tab", 26},
    {"previous-tab", 27},
    {"capture-screenshot", 28},
    {"add-to-essentials", 30},
    {"remove-from-essentials", 31},
    {"find-in-page", 32},
    {"manage-extensions", 33},
    {"switch-to-automatic-appearance", 34},
    {"switch-to-light-mode", 35},
    {"switch-to-dark-mode", 36},
    {"print", 37},
    // Zen lists the per-item actions after these: "Focus on" a Space, then
    // extensions.
    {"focus-space", 100},
});

// Labels are Zen's palette strings (zen-command-palette.ftl), which differ
// from its shortcut names in the shortcut table (zen-preferences.ftl), e.g.
// "Settings" vs "Open Preferences"; the keys do come from the table.
KoanCommand MakeCommand(std::string id,
                        std::u16string label,
                        const gfx::VectorIcon& icon) {
  KoanCommand command;
  // "focus-space:<id>" takes the place of "focus-space".
  const auto order =
      kZenOrder.find(std::string_view(id).substr(0, id.find(':')));
  CHECK(order != kZenOrder.end()) << id;
  command.order = order->second;
  command.id = std::move(id);
  command.label = std::move(label);
  command.icon = &icon;
  return command;
}

// A command that does what one of Koan's (Zen's) shortcuts does.
KoanCommand ShortcutCommand(const KoanBrowserViewController& controller,
                            std::string id,
                            std::u16string label,
                            const gfx::VectorIcon& icon,
                            Action action) {
  KoanCommand command = MakeCommand(std::move(id), std::move(label), icon);
  if (std::optional<ui::Accelerator> accelerator =
          controller.shortcuts()->GetAccelerator(action)) {
    command.shortcut = shortcuts::DisplayText(*accelerator);
  }
  command.run = base::BindRepeating(
      [](Action action, KoanBrowserViewController& controller) {
        controller.shortcuts()->Run(action);
      },
      action);
  return command;
}

// A command that runs one of Chrome's commands (IDC_*).
KoanCommand ChromeCommand(const KoanBrowserViewController& controller,
                          std::string id,
                          std::u16string label,
                          const gfx::VectorIcon& icon,
                          int command_id) {
  KoanCommand command = MakeCommand(std::move(id), std::move(label), icon);
  ui::Accelerator accelerator;
  // Not where Koan has given the key to one of Zen's shortcuts.
  if (controller.browser_view()->GetAccelerator(command_id, &accelerator) &&
      !controller.shortcuts()->IsBound(accelerator)) {
    command.shortcut = shortcuts::DisplayText(accelerator);
  }
  command.run = base::BindRepeating(
      [](int command_id, KoanBrowserViewController& controller) {
        chrome::ExecuteCommand(BrowserOf(controller), command_id);
      },
      command_id);
  return command;
}

// One of Zen's pinned tab actions on the active tab (koan_pinned_tabs.h).
KoanCommand PinnedTabCommand(
    std::string id,
    std::u16string label,
    const gfx::VectorIcon& icon,
    void (KoanPinnedTabController::*action)(tabs::TabInterface*)) {
  KoanCommand command = MakeCommand(std::move(id), std::move(label), icon);
  command.run = base::BindRepeating(
      [](void (KoanPinnedTabController::*action)(tabs::TabInterface*),
         KoanBrowserViewController& controller) {
        if (tabs::TabInterface* tab =
                BrowserOf(controller)->GetTabStripModel()->GetActiveTab()) {
          (controller.pinned_tabs()->*action)(tab);
        }
      },
      action);
  return command;
}

// Zen's "Switch to ... Appearance / Mode": the browser's colour scheme.
KoanCommand AppearanceCommand(std::string id,
                              std::u16string label,
                              const gfx::VectorIcon& icon,
                              ColorScheme scheme) {
  KoanCommand command = MakeCommand(std::move(id), std::move(label), icon);
  command.run = base::BindRepeating(
      [](ColorScheme scheme, KoanBrowserViewController& controller) {
        ThemeServiceOf(controller)->SetBrowserColorScheme(scheme);
      },
      scheme);
  return command;
}

// Zen's availability rules, with "the command is enabled" where Zen checks
// its command element. `owner` owns the registry this source is in.
void AppendGlobalCommands(const KoanBrowserViewController* owner,
                          std::vector<KoanCommand>& commands) {
  const KoanBrowserViewController& controller = *owner;
  const tabs::TabInterface* tab =
      BrowserOf(controller)->GetTabStripModel()->GetActiveTab();
  // Pinned the Zen way: an Essential, or pinned in its workspace.
  const bool pinned = IsPinnedForUser(tab);
  const bool essential = tab && tab->IsPinned();
  const bool pinned_tabs = features::IsPinnedTabsEnabled();
  const auto add_if = [&](bool available, KoanCommand command) {
    if (available) {
      commands.push_back(std::move(command));
    }
  };

  add_if(
      features::IsCompactModeEnabled(),
      ShortcutCommand(controller, "toggle-compact-mode", u"Toggle Compact Mode",
                      kDockToLeftIcon, Action::kToggleCompactMode));
  add_if(IsEnabled(controller, IDC_NEW_SPLIT_TAB),
         ShortcutCommand(controller, "new-split-view", u"New Split View",
                         kSplitSceneIcon, Action::kNewEmptySplit));
  add_if(tab && tab->IsSplit(),
         ShortcutCommand(controller, "unsplit-view", u"Expand current view",
                         kSplitSceneIcon, Action::kUnsplit));
  add_if(tab != nullptr,
         ShortcutCommand(controller, "copy-current-url", u"Copy Current URL",
                         vector_icons::kLinkIcon, Action::kCopyUrl));
  // Zen's preferences: Koan's settings page.
  add_if(true, ShortcutCommand(controller, "settings", u"Settings",
                               kSettingsIcon, Action::kOpenSettings));
  add_if(
      IsEnabled(controller, IDC_NEW_INCOGNITO_WINDOW),
      ShortcutCommand(controller, "open-private-window", u"Open Private Window",
                      kIncognitoIcon, Action::kNewPrivateWindow));
  add_if(IsEnabled(controller, IDC_NEW_WINDOW),
         ShortcutCommand(controller, "open-new-window", u"Open New Window",
                         kNewWindowIcon, Action::kNewWindow));
  add_if(tab && !pinned, ShortcutCommand(controller, "pin-tab", u"Pin Tab",
                                         kKeepIcon, Action::kTogglePinTab));
  add_if(pinned, ShortcutCommand(controller, "unpin-tab", u"Unpin Tab",
                                 kKeepOffIcon, Action::kTogglePinTab));
  add_if(IsEnabled(controller, IDC_CLOSE_TAB),
         ChromeCommand(controller, "close-tab", u"Close Tab",
                       vector_icons::kCloseIcon, IDC_CLOSE_TAB));
  add_if(IsEnabled(controller, IDC_RESTORE_TAB),
         ChromeCommand(controller, "reopen-closed-tab", u"Reopen Closed Tab",
                       kHistoryIcon, IDC_RESTORE_TAB));
  add_if(IsEnabled(controller, IDC_DUPLICATE_TAB),
         ChromeCommand(controller, "duplicate-tab", u"Duplicate Current Tab",
                       vector_icons::kContentCopyIcon, IDC_DUPLICATE_TAB));
  // Like the tab menu's: for any pinned tab (the reset doesn't ignore the
  // fragment, so "away" isn't required).
  const KoanTabState* state =
      pinned_tabs && pinned ? GetTabState(tab) : nullptr;
  add_if(state && state->pinned_url.is_valid(),
         PinnedTabCommand("reset-pinned-tab", u"Reset Pinned Tab",
                          vector_icons::kUndoIcon,
                          &KoanPinnedTabController::ResetToPinnedUrl));
  add_if(IsEnabled(controller, IDC_RELOAD),
         ChromeCommand(controller, "reload-tab", u"Reload Tab",
                       vector_icons::kReloadChromeRefreshOldIcon, IDC_RELOAD));
  add_if(IsEnabled(controller, IDC_RELOAD_BYPASSING_CACHE),
         ChromeCommand(controller, "reload-tab-without-cache",
                       u"Reload Tab Without Cache",
                       vector_icons::kReloadChromeRefreshOldIcon,
                       IDC_RELOAD_BYPASSING_CACHE));
  add_if(IsEnabled(controller, IDC_SELECT_NEXT_TAB),
         ChromeCommand(controller, "next-tab", u"Next Tab",
                       vector_icons::kArrowForwardIcon, IDC_SELECT_NEXT_TAB));
  add_if(IsEnabled(controller, IDC_SELECT_PREVIOUS_TAB),
         ChromeCommand(controller, "previous-tab", u"Previous Tab",
                       vector_icons::kArrowBackIcon, IDC_SELECT_PREVIOUS_TAB));
  add_if(IsEnabled(controller, IDC_SHARING_HUB_SCREENSHOT),
         ChromeCommand(controller, "capture-screenshot", u"Capture Screenshot",
                       kScreenshotRegionIcon, IDC_SHARING_HUB_SCREENSHOT));
  // With Koan's pinned tabs off, its Essentials are just pinned tabs, so
  // these pin and unpin.
  add_if(pinned_tabs ? tab && !essential &&
                           controller.pinned_tabs()->CanAddToEssentials(
                               tab->IsSplit() ? 2 : 1)
                     : tab && !pinned,
         pinned_tabs ? PinnedTabCommand(
                           "add-to-essentials", u"Add to Essentials", kStarIcon,
                           &KoanPinnedTabController::AddToEssentials)
                     : ShortcutCommand(controller, "add-to-essentials",
                                       u"Add to Essentials", kStarIcon,
                                       Action::kTogglePinTab));
  add_if(pinned_tabs ? essential : pinned,
         pinned_tabs ? PinnedTabCommand(
                           "remove-from-essentials", u"Remove from Essentials",
                           vector_icons::kStarFilledIcon,
                           &KoanPinnedTabController::RemoveFromEssentials)
                     : ShortcutCommand(controller, "remove-from-essentials",
                                       u"Remove from Essentials",
                                       vector_icons::kStarFilledIcon,
                                       Action::kTogglePinTab));
  add_if(IsEnabled(controller, IDC_FIND),
         ChromeCommand(controller, "find-in-page", u"Find in Page",
                       kFindInPageIcon, IDC_FIND));
  add_if(
      IsEnabled(controller, IDC_MANAGE_EXTENSIONS),
      ChromeCommand(controller, "manage-extensions", u"Manage Extensions",
                    vector_icons::kChromeExtensionIcon, IDC_MANAGE_EXTENSIONS));
  const ColorScheme scheme =
      ThemeServiceOf(controller)->GetBrowserColorScheme();
  add_if(scheme != ColorScheme::kSystem,
         AppearanceCommand("switch-to-automatic-appearance",
                           u"Switch to Automatic Appearance",
                           kAutomaticAppearanceIcon, ColorScheme::kSystem));
  add_if(scheme != ColorScheme::kLight,
         AppearanceCommand("switch-to-light-mode", u"Switch to Light Mode",
                           kLightModeIcon, ColorScheme::kLight));
  add_if(scheme != ColorScheme::kDark,
         AppearanceCommand("switch-to-dark-mode", u"Switch to Dark Mode",
                           kDarkModeIcon, ColorScheme::kDark));
  add_if(IsEnabled(controller, IDC_PRINT),
         ChromeCommand(controller, "print", u"Print", kPrintIcon, IDC_PRINT));
  // Zen's theme picker, for the active Space.
  if (controller.theme()->IsEnabled()) {
    KoanCommand command = MakeCommand("open-theme-picker", u"Open Theme Picker",
                                      kKoanThemePaintbrushFillIcon);
    command.run = base::BindRepeating([](KoanBrowserViewController& owner) {
      owner.theme()->ShowThemePickerForActiveSpace();
    });
    commands.push_back(std::move(command));
  }
}

void AppendWorkspaceCommands(const KoanBrowserViewController* owner,
                             std::vector<KoanCommand>& commands) {
  const KoanBrowserViewController& controller = *owner;
  KoanWorkspaceController* workspaces = controller.workspaces();
  if (!workspaces->IsEnabled()) {
    return;
  }
  commands.push_back(ShortcutCommand(controller, "new-space", u"New Space",
                                     kAddIcon, Action::kNewWorkspace));
  const std::vector<tab_groups::TabGroupId> ids = workspaces->GetWorkspaces();
  if (ids.size() > 1) {
    commands.push_back(ShortcutCommand(controller, "next-space", u"Next Space",
                                       vector_icons::kArrowForwardIcon,
                                       Action::kWorkspaceForward));
    commands.push_back(ShortcutCommand(
        controller, "previous-space", u"Previous Space",
        vector_icons::kArrowBackIcon, Action::kWorkspaceBackward));
  }
  // Zen: "Focus on" each other Space, matched by the Space's name.
  const std::optional<tab_groups::TabGroupId> active =
      workspaces->GetActiveWorkspace();
  const TabGroupModel* groups = workspaces->tab_strip_model()->group_model();
  int number = 0;
  for (const tab_groups::TabGroupId& id : ids) {
    ++number;
    if (id == active) {
      continue;
    }
    KoanCommand command =
        MakeCommand("focus-space:" + id.ToString(), u"Focus on",
                    vector_icons::kArrowForwardIcon);
    command.target = groups->GetTabGroup(id)->visual_data()->title();
    if (command.target.empty()) {
      // As in the Space switcher.
      command.target = u"Workspace " + base::NumberToString16(number);
    }
    command.run = base::BindRepeating(
        [](tab_groups::TabGroupId id, KoanBrowserViewController& controller) {
          controller.workspaces()->SwitchTo(id);
        },
        id);
    commands.push_back(std::move(command));
  }
}

}  // namespace

void AddGlobalCommands(KoanCommandRegistry& registry,
                       KoanBrowserViewController& controller) {
  // The registry belongs to `controller`.
  registry.AddSource(base::BindRepeating(
      &AppendGlobalCommands,
      base::Unretained(
          static_cast<const KoanBrowserViewController*>(&controller))));
}

void AddWorkspaceCommands(KoanCommandRegistry& registry,
                          KoanBrowserViewController& controller) {
  registry.AddSource(base::BindRepeating(
      &AppendWorkspaceCommands,
      base::Unretained(
          static_cast<const KoanBrowserViewController*>(&controller))));
}

}  // namespace koan
