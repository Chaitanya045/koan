// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_BAR_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_BAR_H_

#include <memory>
#include <optional>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class Button;
}

namespace koan {

class KoanSpaceButton;

// Zen's sidebar bottom bar: an optional leading button (the Library's) at
// the start, "+" with Zen's Create menu at the end, and between them the
// Space switcher, one narrow button per Space, centred: the current Space
// shows its icon (or a dot for none) in colour, the others a small grey dot
// until hovered. Like Zen, the Space buttons are hidden while there is only
// one Space. In a sidebar too narrow for a row (icons only), they stack
// above "+" at full size.
class KoanSpaceBarView : public views::View {
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

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;

 private:
  const raw_ptr<KoanWorkspaceController> controller_;
  std::vector<raw_ptr<KoanSpaceButton>> buttons_;
  raw_ptr<views::Button> create_button_ = nullptr;
  raw_ptr<views::View> leading_ = nullptr;
  bool compact_ = true;
  // The button pressed for a reorder, where along the bar, and whether it
  // has moved far enough to be one.
  raw_ptr<KoanSpaceButton> reorder_button_ = nullptr;
  int reorder_start_ = 0;
  bool reordering_ = false;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_BAR_H_
