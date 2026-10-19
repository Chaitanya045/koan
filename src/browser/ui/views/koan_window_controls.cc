// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_window_controls.h"

#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout_params.h"

namespace koan {

gfx::Size GetCaptionButtonAreaSize(const BrowserView* browser_view) {
  const BrowserWidget* widget = browser_view->browser_widget();
  const BrowserFrameView* frame = widget ? widget->GetFrameView() : nullptr;
  if (!frame) {
    return gfx::Size();
  }
  const gfx::SizeF area =
      frame->GetBrowserLayoutParams().trailing_exclusion.content;
  return gfx::Size(static_cast<int>(area.width()),
                   static_cast<int>(area.height()));
}

}  // namespace koan
