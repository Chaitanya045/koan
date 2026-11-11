// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_BAR_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_BAR_H_

#include <memory>
#include <optional>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "koan/browser/ui/views/koan_space_drag_logic.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/views/view.h"

namespace views {
class Button;
}

namespace koan {

class KoanSpaceButton;
class KoanSpaceStripView;

// Zen's sidebar bottom bar: an optional leading button (the Library's) at
// the start, "+" with Zen's Create menu at the end, and between them the
// Space switcher, one narrow button per Space, centred: the current Space
// shows its icon (or a dot for none) in colour, the others a small grey dot
// until hovered. Like Zen, the Space buttons are hidden while there is only
// one Space. In a sidebar too narrow for a row (icons only), they stack
// above "+" at full size.
//
// The switcher is a strip of its own that clips its buttons. When they don't
// fit at their narrowest, it scrolls sideways like Zen's (overflow-x: auto,
// no scrollbar, no fade): the wheel or touchpad over it scrolls it, and the
// current Space and the hovered one scroll into view, smoothly.
class KoanSpaceBarView : public views::View, public gfx::AnimationDelegate {
  METADATA_HEADER(KoanSpaceBarView, views::View)

 public:
  explicit KoanSpaceBarView(KoanWorkspaceController* controller);
  KoanSpaceBarView(const KoanSpaceBarView&) = delete;
  KoanSpaceBarView& operator=(const KoanSpaceBarView&) = delete;
  ~KoanSpaceBarView() override;

  // A button before the Space buttons (the Library's), or none.
  void SetLeadingButton(std::unique_ptr<views::View> button);

  // Shows `spaces`, reusing the buttons of Spaces it already shows.
  void Update(const std::vector<KoanWorkspaceController::SpaceInfo>& spaces);

  // Whether the Space buttons are in a row, shrunk to dots but for the
  // current or hovered one (Zen's icons-overflow), rather than a column.
  bool compact() const { return compact_; }
  // Whether the buttons stack (a sidebar too narrow for a row).
  bool IsNarrow(int width) const;
  // Whether a Space button is hovered, pressed or focused.
  bool IsAnyButtonHovered() const;
  // The window's user colour override (its Space's tint) as an ARGB value,
  // or 0 for none: a UI DevTools property, which the e2e tests read.
  int GetWindowTint() const;
  // A Space button's state changed: the others may change how they look.
  void OnButtonStateChanged();

  // The Space whose button is at `point` (in this view's coordinates), if a
  // button shows there.
  std::optional<tab_groups::TabGroupId> SpaceAt(const gfx::Point& point) const;

  // Zen's reorder by dragging (ZenSpaceIcons.mjs): a press on a Space
  // button, then the drag (once it has moved kSpaceReorderThreshold along
  // the bar) puts it before or after the button under the pointer, with a
  // haptic tick per step; the release saves the order. Points are in this
  // view's coordinates.
  void StartReorder(KoanSpaceButton* button, const gfx::Point& point);
  void UpdateReorder(KoanSpaceButton* button, const gfx::Point& point);
  void EndReorder(KoanSpaceButton* button);
  void CancelReorder();
  // A reorder is under way (Zen's reorder-mode): the other buttons fade.
  bool IsReordering() const { return reordering_; }
  bool IsReordered(const KoanSpaceButton* button) const {
    return reordering_ && reorder_button_ == button;
  }

  // A wheel or touchpad scroll over the bar (`target` is the view under the
  // pointer, `x` and `y` the event's offsets): scrolls the Space switcher if
  // the pointer is over it and its buttons overflow. A mouse wheel
  // (`precise` false) scrolls it only vertically: a horizontal one switches
  // Space, as in Zen. Returns whether it scrolled (or would have, at an
  // end).
  bool ScrollStrip(const views::View* target, float x, float y, bool precise);
  // Scrolls `button` into view (Zen's scrollIntoView with inline: nearest).
  void ScrollButtonIntoView(const KoanSpaceButton* button);
  // Paints the emoji of the switcher's buttons, under them (see OnPaint()).
  void PaintStripIcons(gfx::Canvas* canvas) const;

  // For tests: the switcher's bounds and a Space button's, in this view's
  // coordinates (the button's as laid out, which the switcher clips), "+"'s,
  // and whether the switcher clips what it holds.
  gfx::Rect GetStripBoundsForTesting() const;
  std::optional<gfx::Rect> GetSpaceButtonBoundsForTesting(
      const tab_groups::TabGroupId& space) const;
  gfx::Rect GetCreateButtonBoundsForTesting() const;
  bool StripClipsForTesting() const;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;

 private:
  // A point in this view's coordinates, in the switcher's.
  gfx::Point ToStrip(const gfx::Point& point) const;
  // Sets the scroll offset (clamped), now or smoothly.
  void ScrollTo(int scroll, bool animate);
  // Lays out the row: the buttons in the switcher, scrolled.
  void LayoutRow(const gfx::Rect& area);

  const raw_ptr<KoanWorkspaceController> controller_;
  std::vector<raw_ptr<KoanSpaceButton>> buttons_;
  raw_ptr<views::Button> create_button_ = nullptr;
  raw_ptr<views::View> leading_ = nullptr;
  // The Space switcher, which holds `buttons_`.
  raw_ptr<KoanSpaceStripView> strip_ = nullptr;
  bool compact_ = true;
  // How far the switcher is scrolled (from its start, in LTR terms), and how
  // far it can be; a smooth scroll's way, the row's last layout, and whether
  // the next layout scrolls the current Space into view (it changed, or the bar
  // is new).
  int scroll_ = 0;
  int max_scroll_ = 0;
  int scroll_from_ = 0;
  int scroll_to_ = 0;
  SpaceStripLayout strip_layout_;
  gfx::LinearAnimation scroll_animation_{this};
  std::optional<tab_groups::TabGroupId> active_space_;
  bool scroll_to_active_ = true;
  // The button pressed for a reorder, where along the bar, and whether it
  // has moved far enough to be one.
  raw_ptr<KoanSpaceButton> reorder_button_ = nullptr;
  int reorder_start_ = 0;
  bool reordering_ = false;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_BAR_H_
