// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_quick_window_rules.h"

#include <algorithm>

namespace koan::quick_window {

namespace {

constexpr float kWidthShare = 0.6f;
constexpr float kHeightShare = 0.75f;
constexpr int kMinWidth = 480;
constexpr int kMinHeight = 360;
constexpr int kMaxWidth = 1000;
constexpr int kMaxHeight = 840;

int Fit(int available, float share, int min, int max) {
  const int wanted = std::clamp(static_cast<int>(available * share), min, max);
  return std::min(wanted, available);
}

}  // namespace

bool OpensInQuickWindow(const LinkFacts& facts) {
  return facts.quick_window_chosen && !facts.off_the_record && facts.web_url &&
         !facts.routed && facts.has_window && !facts.window_is_only_new_tab;
}

gfx::Rect QuickWindowBounds(const gfx::Rect& work_area) {
  const int width = Fit(work_area.width(), kWidthShare, kMinWidth, kMaxWidth);
  const int height =
      Fit(work_area.height(), kHeightShare, kMinHeight, kMaxHeight);
  return gfx::Rect(work_area.x() + (work_area.width() - width) / 2,
                   work_area.y() + (work_area.height() - height) / 2, width,
                   height);
}

std::vector<size_t> MoveMenuOrder(size_t count, std::optional<size_t> active) {
  std::vector<size_t> order;
  order.reserve(count);
  if (active && *active < count) {
    order.push_back(*active);
  }
  for (size_t i = 0; i < count; ++i) {
    if (!active || i != *active) {
      order.push_back(i);
    }
  }
  return order;
}

}  // namespace koan::quick_window
