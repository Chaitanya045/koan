// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenWindowDragChild.sys.mjs
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_zen_window_drag.h"

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"

namespace koan {

namespace {

// A function of (x, y): ZenWindowDragChild's #isEventOverDraggableContent,
// inverted, for Blink.
constexpr char kScript[] = R"((x, y) => {
  if (document.fullscreenElement || document.pointerLockElement) {
    return false;
  }
  const root = document.scrollingElement || document.documentElement;
  if (root && root.scrollHeight > innerHeight && x > innerWidth - 16) {
    return false;
  }
  const element = document.elementFromPoint(x, y);
  if (!element) {
    return true;
  }
  const caret = document.caretRangeFromPoint(x, y);
  const text = caret && caret.startContainer;
  if (text && text.nodeType === Node.TEXT_NODE && text.parentElement &&
      getComputedStyle(text.parentElement).userSelect !== 'none') {
    const range = document.createRange();
    range.selectNodeContents(text);
    for (const r of range.getClientRects()) {
      if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
        return false;
      }
    }
  }
  const interactive = 'a[href], area, button, input, select, textarea, ' +
      'label, summary, details, video, audio, canvas, iframe, frame, ' +
      'embed, object, img, [draggable="true"]';
  const roles = new Set(['button', 'checkbox', 'combobox', 'grid', 'link',
      'listbox', 'menu', 'menubar', 'menuitem', 'option', 'radio',
      'scrollbar', 'searchbox', 'slider', 'spinbutton', 'switch', 'tab',
      'textbox', 'toolbar', 'tree', 'treegrid']);
  for (let node = element; node;
       node = node.parentElement || node.getRootNode().host) {
    if (node === document.body || node === document.documentElement) {
      continue;
    }
    if (node.matches(interactive) || node.isContentEditable ||
        node.tabIndex >= 0) {
      return false;
    }
    const role = node.getAttribute('role');
    if (role && roles.has(role.toLowerCase())) {
      return false;
    }
  }
  const cursor = getComputedStyle(element).cursor;
  return cursor === 'auto' || cursor === 'default';
})";

}  // namespace

std::u16string IsEmptySpaceScript(double x, double y) {
  return base::UTF8ToUTF16(
      base::StrCat({"(", kScript, ")(", base::NumberToString(x), ", ",
                    base::NumberToString(y), ")"}));
}

}  // namespace koan
