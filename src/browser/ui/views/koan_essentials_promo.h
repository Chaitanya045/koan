// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_PROMO_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_PROMO_H_

#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"

namespace koan {

// Zen's <zen-essentials-promo> (ZenEssentialsPromo.mjs,
// zen-essentials-promo.css): with no Essentials, the drop target that takes
// their place at the top of the sidebar while a tab is dragged there, "Add
// to Essentials" / "Keep your favorite tabs just a click away" under a
// heart. Lit while the tab is over it. Laid out above the Space header by
// PlaceSpaceHeader() (koan_spaces.h).
class KoanEssentialsPromoView : public views::View {
  METADATA_HEADER(KoanEssentialsPromoView, views::View)

 public:
  KoanEssentialsPromoView();
  KoanEssentialsPromoView(const KoanEssentialsPromoView&) = delete;
  KoanEssentialsPromoView& operator=(const KoanEssentialsPromoView&) = delete;
  ~KoanEssentialsPromoView() override;

  // Zen's [dragover="true"]: the dragged tab is over it.
  void SetDragOver(bool drag_over);
  bool GetDragOver() const { return drag_over_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  bool drag_over_ = false;
};

// Zen's #zen-drag-indicator (vertical-tabs.css): the line that shows where
// a dragged tab drops, here between the Essentials tiles (upright) or between
// a Space's rows when an Essential is dragged out to them. Upstream's drag
// moves the dragged rows themselves within their own section; this marks
// the drops Koan makes at the end of the drag (koan_space_drag.h). A child
// of the TabStripView, laid out at `slot()` by PlaceSpaceHeader(), on a layer
// above the tiles, and takes no events.
class KoanTabDropIndicatorView : public views::View {
  METADATA_HEADER(KoanTabDropIndicatorView, views::View)

 public:
  // Zen's --zen-drag-indicator-height.
  static constexpr int kThickness = 2;

  KoanTabDropIndicatorView();
  KoanTabDropIndicatorView(const KoanTabDropIndicatorView&) = delete;
  KoanTabDropIndicatorView& operator=(const KoanTabDropIndicatorView&) = delete;
  ~KoanTabDropIndicatorView() override;

  // Where it shows, in the TabStripView; empty hides it.
  void SetSlot(const gfx::Rect& slot);
  const gfx::Rect& slot() const { return slot_; }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  gfx::Rect slot_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_PROMO_H_
