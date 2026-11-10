// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_KEYBOARD_STATE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_KEYBOARD_STATE_H_

namespace koan {

// Whether the Control key is held right now, wherever the keyboard's focus is
// (a page gets the key events, not the browser's views). macOS only; false
// elsewhere, where callers behave as if it had just been let go.
bool IsControlKeyDown();

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_KEYBOARD_STATE_H_
