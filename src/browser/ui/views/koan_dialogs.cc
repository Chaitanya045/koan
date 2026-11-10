// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   commonDialog-css.patch, zen-panels/dialog.css (the prompts' metrics)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_dialogs.h"

#include <algorithm>

#include "koan/browser/ui/color/koan_look.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/message_box_view.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace koan {

namespace {

// `padding: 30px 26px` around the dialog and `gap: 6px` between its rows.
constexpr int kVerticalPadding = 30;
constexpr int kHorizontalPadding = 26;
constexpr int kRowGap = 6;
// The buttons' `margin: 4px 6px 0 6px`.
constexpr int kButtonMarginTop = 4;
constexpr int kButtonMarginSide = 6;
// `min-width: 32em` at macOS's message-box font (13px).
constexpr int kWidth = 32 * 13;
// `.dialogBox { border-radius: 18px }`.
constexpr int kCornerRadius = 18;

}  // namespace

void StyleJavaScriptDialog(views::DialogDelegate& dialog,
                           views::MessageBoxView& message_box) {
  // MessageBoxView pads its text with the dialog's own side insets; the
  // margins make up the rest to Zen's 26px.
  const int message_inset = views::LayoutProvider::Get()
                                ->GetInsetsMetric(views::INSETS_DIALOG)
                                .left();
  const int side = std::max(0, kHorizontalPadding - message_inset);
  dialog.set_frame_margins({
      .contents = gfx::Insets::TLBR(kRowGap, side, 0, side),
      .title = gfx::Insets::TLBR(kVerticalPadding, kHorizontalPadding, 0,
                                 kHorizontalPadding),
  });
  // The message box's width includes its own insets.
  dialog.set_fixed_width(kWidth);
  message_box.SetMessageWidth(kWidth - 2 * side);
  if (!look::IsShadcnStyle()) {
    dialog.set_corner_radius(kCornerRadius);
  }
}

void StyleJavaScriptDialogButtons(views::DialogDelegate& dialog) {
  views::Widget* widget = dialog.GetWidget();
  if (!widget) {
    return;
  }
  dialog.SetButtonRowInsets(gfx::Insets::TLBR(
      kRowGap + kButtonMarginTop, kHorizontalPadding + kButtonMarginSide,
      kVerticalPadding, kHorizontalPadding + kButtonMarginSide));
  // The web-modal host's bounds for the new preferred size, centred as
  // before (constrained_window::ConfigureDesiredBoundsDelegate).
  const gfx::Rect bounds = dialog.GetDesiredWidgetBounds();
  if (!bounds.IsEmpty()) {
    widget->SetBounds(bounds);
  }
}

}  // namespace koan
