// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_KEYBOARD_STATE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_KEYBOARD_STATE_H_

#include <memory>

namespace views {
class Widget;
}

namespace koan {

// Whether the Control key is held right now, wherever the keyboard's focus is
// (a page gets the key events, not the browser's views), for a browser
// window's Ctrl+Tab walk (KoanShortcutController::CycleToRecentTab()).
class ControlKeyState {
 public:
  // `widget` is the browser window's. On macOS this reads the hardware state
  // (NSEvent.modifierFlags). Elsewhere it follows the Control key's presses
  // and releases in the key events of all of Koan's windows (an aura::Env
  // pre-target handler, which sees them before a page, the URL bar or a
  // bubble does), and counts Control as let go of when `widget` stops being
  // the active window, since the release then goes to another app.
  static std::unique_ptr<ControlKeyState> Create(views::Widget* widget);

  virtual ~ControlKeyState() = default;

  virtual bool IsDown() const = 0;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_KEYBOARD_STATE_H_
