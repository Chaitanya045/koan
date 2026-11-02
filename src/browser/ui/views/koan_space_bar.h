// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_BAR_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_BAR_H_

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

// Zen's sidebar bottom bar: one button per Space (its emoji, or a dot),
// the active one in colour and the others greyed out, and "+" with Zen's
// Create menu. Like Zen, the Space buttons are hidden while there is only
// one Space, and inactive ones shrink to dots when they don't fit. In a
// sidebar too narrow for a row (icons only), they stack above "+".
class KoanSpaceBarView : public views::View {
  METADATA_HEADER(KoanSpaceBarView, views::View)

 public:
  explicit KoanSpaceBarView(KoanWorkspaceController* controller);
  KoanSpaceBarView(const KoanSpaceBarView&) = delete;
  KoanSpaceBarView& operator=(const KoanSpaceBarView&) = delete;
  ~KoanSpaceBarView() override;

  // Shows `spaces`, reusing the buttons of Spaces it already shows.
  void Update(const std::vector<KoanWorkspaceController::SpaceInfo>& spaces);

  // Whether the Space buttons don't fit at full size.
  bool overflows() const { return overflows_; }
  // Whether the buttons stack (a sidebar too narrow for a row).
  bool IsNarrow(int width) const;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;

 private:
  const raw_ptr<KoanWorkspaceController> controller_;
  std::vector<raw_ptr<KoanSpaceButton>> buttons_;
  raw_ptr<views::Button> create_button_ = nullptr;
  bool overflows_ = false;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_BAR_H_
