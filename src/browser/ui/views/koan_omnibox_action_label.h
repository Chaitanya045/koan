// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   browser.ftl (urlbar-result-action-switch-tab, as Zen ships it)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

// The text of a suggestion's action button where Koan's differs from
// Chromium's. Pure, so koan_omnibox_unittests can check it without the UI.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_OMNIBOX_ACTION_LABEL_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_OMNIBOX_ACTION_LABEL_H_

#include <optional>
#include <string_view>

#include "components/omnibox/browser/actions/omnibox_action.h"
#include "components/omnibox/browser/actions/omnibox_action_concepts.h"

namespace koan {

// What Zen shows beside an open tab's suggestion (a TAB_SWITCH result's
// action, urlbar-result-action-switch-tab).
inline constexpr char16_t kSwitchToTabLabel[] = u"Switch to Tab";

// The label of `action`'s button in a suggestion row: Zen's "Switch to Tab"
// for an open tab's match (TabSwitchAction, which Chromium labels "Switch to
// this tab"). Nothing for any other action, which keeps Chromium's label.
inline std::optional<std::u16string_view> OmniboxActionLabel(
    const OmniboxAction& action) {
  if (action.ActionId() == OmniboxActionId::TAB_SWITCH) {
    return kSwitchToTabLabel;
  }
  return std::nullopt;
}

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_OMNIBOX_ACTION_LABEL_H_
