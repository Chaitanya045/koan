// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_NAVIGATION_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_NAVIGATION_H_

#include <cstddef>
#include <optional>
#include <string_view>

// Moving between Spaces the way Zen's settings say (koan_prefs.h
// kWorkspacesWrapAround, kWorkspacesScrollModifierKey; Zen's
// ZenSpaceManager changeWorkspaceShortcut() and its sidebar "wheel"
// listener). Pure, so the unit tests need nothing else.
namespace koan {

// The Space after (or before) the one at `index` of `count`: with
// `wrap_around`, past the last comes the first and the other way; without,
// none at the ends.
inline std::optional<size_t> NextSpaceIndex(size_t index,
                                            size_t count,
                                            bool forward,
                                            bool wrap_around) {
  if (count < 2 || index >= count) {
    return std::nullopt;
  }
  if (forward) {
    if (index + 1 < count) {
      return index + 1;
    }
    return wrap_around ? std::optional<size_t>(0) : std::nullopt;
  }
  if (index > 0) {
    return index - 1;
  }
  return wrap_around ? std::optional<size_t>(count - 1) : std::nullopt;
}

// How many Spaces the switch from `from` to `to` (of `count`) moves, negative
// for backwards: with `wrap_around`, the shorter way round (from the last
// Space to the first is one forward), as Zen slides them.
inline int SpaceSwitchSteps(int from, int to, int count, bool wrap_around) {
  int diff = to - from;
  if (!wrap_around) {
    return diff;
  }
  if (diff > count / 2) {
    diff -= count;
  } else if (diff < -(count / 2)) {
    diff += count;
  }
  return diff;
}

// Whether the modifier named `key` ("ctrl", "alt", "shift" or "meta", Cmd on
// macOS) is down, for a vertical wheel to switch Space. An unknown name
// falls back to Zen's default, "ctrl".
inline bool IsSpaceScrollModifierDown(std::string_view key,
                                      bool ctrl,
                                      bool alt,
                                      bool shift,
                                      bool meta) {
  if (key == "alt") {
    return alt;
  }
  if (key == "shift") {
    return shift;
  }
  if (key == "meta") {
    return meta;
  }
  return ctrl;
}

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_NAVIGATION_H_
