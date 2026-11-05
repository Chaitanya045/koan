// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WIDGET_COLOR_KEY_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WIDGET_COLOR_KEY_H_

namespace ui {
struct ColorProviderKey;
}

namespace views {

class Widget;

// Defined in ui/views/widget/widget.cc (ui/views/widget/widget.cc.patch):
// `adjuster` runs whenever a child widget takes its parent's colour key
// (Widget::GetColorProviderKey()), before the child's own overrides.
using ChildColorProviderKeyAdjuster = void (*)(const Widget* parent,
                                               const Widget* child,
                                               ui::ColorProviderKey* key);
void SetChildColorProviderKeyAdjuster(ChildColorProviderKeyAdjuster adjuster);

}  // namespace views

namespace koan {

// Installs Koan's adjuster once: a child of a browser window whose Space
// theme sets the window's colour mode keeps the window's scheme instead,
// unless it is the window's own chrome (KoanThemeController).
void InstallChildColorKeyAdjuster();

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WIDGET_COLOR_KEY_H_
