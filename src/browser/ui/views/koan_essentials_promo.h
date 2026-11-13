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

// Zen's drop feedback during Koan's tab drag (koan_tab_drag.h), a child of
// the TabStripView laid out at `slot()` by PlaceSpaceHeader(), taking no
// events:
// - kAcross: #zen-drag-indicator between rows, a 2px line with a hollow ring
//   at its start (DropLineBounds(), koan_space_drag_logic.h);
// - kUpright: the same line upright between the Essentials tiles;
// - kBackground: #zen-dragover-background, the bar behind a row a drop goes
//   into (a folder, the Space header), the sidebar's width;
// - kSplitTab: <zen-split-fake-tab>, half a row a held tab would split.
// The lines are on a layer above the tiles and rows; the bar paints in the
// strip, below them (the strip's first child).
class KoanTabDropIndicatorView : public views::View {
  METADATA_HEADER(KoanTabDropIndicatorView, views::View)

 public:
  enum class Style { kAcross, kUpright, kBackground, kSplitTab };

  // Zen's --zen-drag-indicator-height.
  static constexpr int kThickness = 2;

  explicit KoanTabDropIndicatorView(bool background = false);
  KoanTabDropIndicatorView(const KoanTabDropIndicatorView&) = delete;
  KoanTabDropIndicatorView& operator=(const KoanTabDropIndicatorView&) = delete;
  ~KoanTabDropIndicatorView() override;

  // Where it shows, in the TabStripView, and how (a line view takes kAcross,
  // kUpright or kSplitTab); empty hides it.
  void SetSlot(const gfx::Rect& slot, Style style);
  const gfx::Rect& slot() const { return slot_; }
  Style style() const { return style_; }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  gfx::Rect slot_;
  Style style_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_PROMO_H_
