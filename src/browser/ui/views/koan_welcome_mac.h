// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WELCOME_MAC_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WELCOME_MAC_H_

#include "build/build_config.h"

namespace views {
class Widget;
}

namespace koan {

// Zen shows no window buttons during its welcome: on macOS the traffic
// lights of `window` hide while chrome://koan-welcome fills it, and come back
// after. Elsewhere Koan's own buttons are views the screen covers.
#if BUILDFLAG(IS_MAC)
void SetWindowButtonsHidden(views::Widget* window, bool hidden);
#else
inline void SetWindowButtonsHidden(views::Widget* window, bool hidden) {}
#endif

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WELCOME_MAC_H_
