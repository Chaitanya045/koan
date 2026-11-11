// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_essentials.h"

#include <algorithm>

#include "base/logging.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/tabs/common/pinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_animating_layout_manager.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/shared/rounded_scroll_bar.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_vertical_tabs.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/class_property.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace koan {

// The Essentials grid's shown height while it eases (GetEssentialsHeight()),
// kept on the strip's pinned scroll view.
struct EssentialsHeight {
  // Where the ease started and where it goes.
  int from = 0;
  int to = 0;
  // What the layout was last given; -1 before the first layout.
  int shown = -1;
  // The shared animation's offsets when the ease started, and at the last
  // layout: a new start (or a restart from 0) begins a new ease from `shown`.
  double starting_offset = 0.0;
  double last_offset = 1.0;
  // The scroll bar's thumb is hidden while the grid is shorter than its
  // tiles: upstream's thumb shows whenever its bounds change, and the grid is
  // not scrolled, only revealed.
  bool thumb_hidden = false;
};

}  // namespace koan

DEFINE_UI_CLASS_PROPERTY_TYPE(koan::EssentialsHeight*)

namespace koan {

namespace {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(EssentialsHeight, kEssentialsHeightKey)

// Zen's Essentials grid (.zen-essentials-container) fits at most four tiles
// across (each at least 23.7% of the row).
constexpr int kMaxEssentialsColumns = 4;

}  // namespace

int ClampEssentialsColumns(int columns) {
  if (!features::IsSidebarEnabled()) {
    return columns;
  }
  return std::min(columns, kMaxEssentialsColumns);
}

int GetEssentialsHeight(const TabStripView* tab_strip_view, int target_height) {
  if (!features::IsSidebarEnabled() || !tab_strip_view) {
    return target_height;
  }
  views::ScrollView* scroll_view = tab_strip_view->pinned_tabs_scroll_view();
  PinnedTabContainerView* grid = tab_strip_view->GetPinnedTabsContainer();
  if (!scroll_view || !grid ||
      !grid->GetProperty(kHasAnimatingLayoutManagerKey)) {
    return target_height;
  }
  // The strip's one shared tab animation, which every container of the strip
  // (the grid, the Space's group, the unpinned tabs) follows.
  const TabCollectionAnimatingLayoutManager::AnimationCoordinator& coordinator =
      static_cast<const TabCollectionAnimatingLayoutManager*>(
          grid->GetLayoutManager())
          ->animation_coordinator();
  EssentialsHeight* height = scroll_view->GetProperty(kEssentialsHeightKey);
  if (!height) {
    height = scroll_view->SetProperty(kEssentialsHeightKey,
                                      std::make_unique<EssentialsHeight>());
  }
  auto* scroll_bar = views::AsViewClass<tabs::RoundedScrollBar>(
      scroll_view->vertical_scroll_bar());
  const double offset = coordinator.current_offset();
  if (offset >= 1.0 || height->shown < 0 ||
      !gfx::Animation::ShouldRenderRichAnimation()) {
    if (height->thumb_hidden && scroll_bar) {
      scroll_bar->SetIsAnimatingSize(false);
    }
    *height = {.from = target_height,
               .to = target_height,
               .shown = target_height,
               .starting_offset = coordinator.starting_offset(),
               .last_offset = 1.0};
    return target_height;
  }
  // A new target, or the animation (re)started: ease from where it is now,
  // as upstream's containers start again from their current layout
  // (SetStartingLayoutToCurrent()).
  if (target_height != height->to ||
      coordinator.starting_offset() != height->starting_offset ||
      offset < height->last_offset) {
    height->from = height->shown;
    height->to = target_height;
    height->starting_offset = coordinator.starting_offset();
  }
  height->last_offset = offset;
  if (height->from != height->to && !height->thumb_hidden && scroll_bar) {
    height->thumb_hidden = true;
    scroll_bar->SetIsAnimatingSize(true);
  }
  height->shown = gfx::Tween::IntValueBetween(coordinator.GetCurrentProgress(),
                                              height->from, height->to);
  LOG(INFO) << "KOANDBG ess-height target=" << target_height
            << " shown=" << height->shown << " from=" << height->from
            << " offset=" << offset
            << " progress=" << coordinator.GetCurrentProgress()
            << " grid_animating="
            << TabCollectionAnimatingLayoutManager::IsViewAnimating(grid)
            << " grid_w=" << grid->width();
  return height->shown;
}

int GetEmptyEssentialsWidth(const views::SizeBounds& size_bounds) {
  if (!features::IsSidebarEnabled()) {
    return 0;
  }
  LOG(INFO) << "KOANDBG ess-empty-width " << size_bounds.width().value_or(-1);
  return size_bounds.width().value_or(0);
}

views::SizeBounds GetEssentialsLayoutBounds(
    const views::SizeBounds& size_bounds,
    const views::View* grid) {
  if (!features::IsSidebarEnabled() || !size_bounds.width().is_bounded() ||
      size_bounds.width().value() > 0) {
    return size_bounds;
  }
  const views::ScrollView* scroll_view =
      views::ScrollView::GetScrollViewForContents(
          const_cast<views::View*>(grid));
  const int width = scroll_view ? scroll_view->GetContentsBounds().width() : 0;
  if (width <= 0) {
    return size_bounds;
  }
  return views::SizeBounds(width, size_bounds.height());
}

int GetEssentialsMinHeight(int min_height, int preferred_height) {
  if (!features::IsSidebarEnabled()) {
    return min_height;
  }
  return std::min(min_height, preferred_height);
}

tabs::VerticalTabStripCollapseState GetEssentialsCollapseState(
    tabs::VerticalTabStripCollapseState state) {
  return OwnsSidebar() ? tabs::VerticalTabStripCollapseState::kExpanded : state;
}

bool IsEssentialsTile(bool pinned) {
  return pinned && features::IsSidebarEnabled();
}

void PaintEssentialsTile(gfx::Canvas* canvas,
                         const views::View& tab,
                         bool selected,
                         double hover,
                         float corner_radius) {
  const ui::ColorProvider* provider = tab.GetColorProvider();
  const SkColor idle = provider->GetColor(kColorKoanEssentialsTile);
  const SkColor hovered = provider->GetColor(kColorKoanEssentialsTileHovered);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(selected ? provider->GetColor(kColorKoanEssentialsTileSelected)
                          : color_utils::AlphaBlend(hovered, idle,
                                                    static_cast<float>(hover)));
  canvas->DrawRoundRect(gfx::RectF(tab.GetLocalBounds()), corner_radius, flags);
}

}  // namespace koan
