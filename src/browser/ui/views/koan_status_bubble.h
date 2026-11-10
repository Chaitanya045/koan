// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_STATUS_BUBBLE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_STATUS_BUBBLE_H_

namespace gfx {
class Canvas;
class Size;
}  // namespace gfx

namespace ui {
class ColorProvider;
}

// Zen's styled status panel, the URL shown at the bottom while a link is
// hovered (zen.theme.styled-status-panel, on by default on macOS;
// zen-single-components.css `#statuspanel`): a pill with a hairline border,
// 6px off the page's corner, the text semibold. Read by
// chrome/browser/ui/views/status_bubble_views.cc. The colours are Chromium's
// own kColorStatusBubble* ids, set by Koan's colour mixer.
namespace koan {

// The gap between the page's edges and the pill (`#statuspanel` padding).
inline constexpr int kStatusBubbleMargin = 6;
// The space between the text and the border, beside upstream's 1 px
// kShadowThickness, which is the border (the label's `padding: 4px 8px 5px`).
inline constexpr int kStatusBubbleTextHorizPadding = 8;
// The height beyond the font's: the 1 px border twice and 4 + 5 px padding.
inline constexpr int kStatusBubbleVerticalPadding = 11;
// The label's `border-radius: 16px`, which on a bubble this low is a pill.
inline constexpr int kStatusBubbleCornerRadius = 16;

// The views::style text style of the bubble's label (font-weight 600).
int StatusBubbleTextStyle();

// Paints the bubble, `size` in DIPs, instead of StatusView::OnPaint's
// squared-off shapes: the pill filled with the background colour and its
// border in kColorStatusBubbleShadow. Returns false (Chromium then paints)
// when there is no colour provider yet.
bool PaintStatusBubble(gfx::Canvas* canvas,
                       const gfx::Size& size,
                       const ui::ColorProvider* colors,
                       bool active);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_STATUS_BUBBLE_H_
