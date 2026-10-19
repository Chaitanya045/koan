// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_window_controls.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout_params.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/vector_icons/vector_icons.h"

namespace koan {

gfx::Size GetCaptionButtonAreaSize(BrowserView* browser_view) {
  const BrowserFrameView* frame =
      browser_view->browser_widget()
          ? browser_view->browser_widget()->GetFrameView()
          : nullptr;
  if (!frame) {
    return gfx::Size();
  }
  const gfx::SizeF area = frame->GetBrowserLayoutParams().trailing_exclusion.content;
  return gfx::Size(static_cast<int>(area.width()),
                   static_cast<int>(area.height()));
}

std::unique_ptr<views::View> CreateWindowControls(
    BrowserView* browser_view,
    const gfx::Size& button_size) {
  auto controls = std::make_unique<views::BoxLayoutView>();
  controls->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  // The buttons live inside the window, so the widget outlives them.
  views::Widget* widget = browser_view->GetWidget();
  const auto add = [&](const gfx::VectorIcon& icon, std::u16string name,
                       base::RepeatingClosure action) {
    auto button = views::CreateVectorImageButtonWithNativeTheme(
        base::BindRepeating(
            [](base::RepeatingClosure action, const ui::Event&) {
              action.Run();
            },
            std::move(action)),
        icon, /*dip_size=*/16);
    button->SetTooltipText(name);
    button->GetViewAccessibility().SetName(name);
    button->SetPreferredSize(button_size);
    controls->AddChildView(std::move(button));
  };
  add(views::kChromeMinimizeIcon, u"Minimize",
      base::BindRepeating(&views::Widget::Minimize, base::Unretained(widget)));
  add(views::kChromeMaximizeIcon, u"Maximize",
      base::BindRepeating(
          [](views::Widget* w) {
            w->IsMaximized() ? w->Restore() : w->Maximize();
          },
          base::Unretained(widget)));
  add(vector_icons::kCloseIcon, u"Close",
      base::BindRepeating(&views::Widget::Close, base::Unretained(widget)));
  return controls;
}

}  // namespace koan
