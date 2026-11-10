// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_EXTENSION_DROP_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_EXTENSION_DROP_H_

#include <algorithm>
#include <cstddef>
#include <vector>

// Dragging a pinned extension's button along the URL pill
// (KoanSidebarExtensionsView): the buttons are a row of equal slots, and the
// dragged one shows in the slot under the pointer. Pure, so the unit tests
// need nothing else.
namespace koan {

// The slot under `x` (from the row's leading edge, already mirrored for RTL)
// in a row of `count` slots `slot_width` wide; clamped to the row, so a
// pointer past either end means the first or the last slot. `count` > 0.
inline size_t ExtensionDropSlot(int x, int slot_width, size_t count) {
  if (x <= 0 || slot_width <= 0) {
    return 0;
  }
  return std::min(static_cast<size_t>(x / slot_width), count - 1);
}

// The buttons' order, as indices into the current order of `count`, with the
// one at `from` moved to `to` (both < `count`).
inline std::vector<size_t> ExtensionDropOrder(size_t count,
                                              size_t from,
                                              size_t to) {
  std::vector<size_t> order;
  order.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    if (i != from) {
      order.push_back(i);
    }
  }
  order.insert(order.begin() + static_cast<std::ptrdiff_t>(to), from);
  return order;
}

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_EXTENSION_DROP_H_
