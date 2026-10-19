// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SHORTCUTS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SHORTCUTS_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/base/accelerators/accelerator.h"

class BrowserView;

namespace views {
class FocusManager;
}

namespace koan {

class KoanBrowserViewController;

// Koan's keyboard shortcuts: Zen's defaults (zen-browser/desktop
// src/zen/kbs/ZenKeyboardShortcuts.mjs, shortcuts version 21, Linux), plus a
// few Koan-only ones. Registered on the window's FocusManager after
// BrowserView's own accelerators, so they are tried first; where Zen
// deliberately rebinds a key Chrome uses (Ctrl+S, Ctrl+Shift+C/D/N/P), Koan
// follows Zen. A contextual shortcut (Ctrl+O while Glance is open) passes
// the key on to Chrome otherwise. The full table is in docs/USAGE.md.
class KoanShortcutController : public ui::AcceleratorTarget {
 public:
  enum class Action {
    // Compact mode / layout.
    kToggleCompactMode,
    kCompactModeShowSidebar,
    kToggleSingleToolbar,
    // Workspaces.
    kWorkspaceForward,
    kWorkspaceBackward,
    kNewWorkspace,
    // Split view.
    kSplitGrid,
    kSplitVertical,
    kSplitHorizontal,
    kUnsplit,
    kNewEmptySplit,
    // Tabs and pages.
    kCopyUrl,
    kCopyUrlMarkdown,
    kTogglePinTab,
    kCloseUnpinnedTabs,
    kGlanceExpand,
    kSavePage,
    kLibrary,
    kInspect,
    kNewWindow,
    kNewPrivateWindow,
    kMuteSite,
    kBookmarks,
    kDownloads,
    kReaderMode,
  };

  struct Binding {
    ui::Accelerator accelerator;
    Action action;
  };

  KoanShortcutController(BrowserView* browser_view,
                         KoanBrowserViewController* controller);
  KoanShortcutController(const KoanShortcutController&) = delete;
  KoanShortcutController& operator=(const KoanShortcutController&) = delete;
  ~KoanShortcutController() override;

  // The default bindings, in docs/USAGE.md order.
  static std::vector<Binding> DefaultBindings();

  void OnAddedToWidget();
  void OnBrowserViewDestroying();

  // Runs `action`; returns false if it doesn't apply right now (the key then
  // goes on to Chrome's own handler).
  bool Run(Action action);

  // ui::AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;

 private:
  bool Split(int layout_index);
  bool Unsplit();
  bool CopyUrl(bool markdown);
  bool TogglePinTab();
  bool CloseUnpinnedTabs();
  bool Command(int command_id);

  const raw_ptr<BrowserView> browser_view_;
  const raw_ptr<KoanBrowserViewController> controller_;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;
  std::vector<Binding> bindings_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SHORTCUTS_H_
