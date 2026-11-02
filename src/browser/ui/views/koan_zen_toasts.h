// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   zen-general.ftl
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

// Zen's toasts for a tab opened in the background and a renamed workspace.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_TOASTS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_TOASTS_H_

namespace koan {

// zen-background-tab-opened-toast.
inline constexpr char16_t kBackgroundTabOpenedToast[] =
    u"New background tab opened!";

// zen-workspace-renamed-toast.
inline constexpr char16_t kWorkspaceRenamedToast[] =
    u"Workspace has been successfully renamed!";

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_TOASTS_H_
