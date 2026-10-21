// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_COMMANDS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_COMMANDS_H_

namespace koan {

class KoanBrowserViewController;
class KoanCommandRegistry;

// Zen's global actions (zen-command-palette.ftl, ZenUBGlobalActions) that
// Koan has: compact mode, split view, tabs, windows, appearance and Chrome's
// own commands, with Zen's labels and availability rules.
void AddGlobalCommands(KoanCommandRegistry& registry,
                       KoanBrowserViewController& controller);

// The Spaces actions: New Space, Next / Previous Space and "Focus on" each
// other Space.
void AddWorkspaceCommands(KoanCommandRegistry& registry,
                          KoanBrowserViewController& controller);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_COMMANDS_H_
