// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_window_controls.h"

#include <algorithm>

#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout_params.h"
#include "ui/gfx/geometry/size_conversions.h"

namespace koan {

gfx::Size GetCaptionButtonAreaSize(const BrowserView* browser_view) {
  const BrowserWidget* widget = browser_view->browser_widget();
  const BrowserFrameView* frame = widget ? widget->GetFrameView() : nullptr;
  if (!frame) {
    return gfx::Size();
  }
  // Caption buttons can be on either edge (e.g. GNOME's "close:" layout puts
  // them on the leading one), or both.
  const BrowserLayoutParams params = frame->GetBrowserLayoutParams();
  const gfx::SizeF leading = params.leading_exclusion.content;
  const gfx::SizeF trailing = params.trailing_exclusion.content;
  return gfx::Size(
      static_cast<int>(leading.width() + trailing.width()),
      static_cast<int>(std::max(leading.height(), trailing.height())));
}

gfx::Size GetCaptionButtonAreaSize(const BrowserView* browser_view,
                                   bool leading) {
  const BrowserWidget* widget = browser_view->browser_widget();
  const BrowserFrameView* frame = widget ? widget->GetFrameView() : nullptr;
  if (!frame) {
    return gfx::Size();
  }
  const BrowserLayoutParams params = frame->GetBrowserLayoutParams();
  return gfx::ToCeiledSize(
      (leading ? params.leading_exclusion : params.trailing_exclusion).content);
}

}  // namespace koan
