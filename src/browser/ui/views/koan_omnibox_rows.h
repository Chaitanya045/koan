// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_OMNIBOX_ROWS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_OMNIBOX_ROWS_H_

#include "ui/color/color_id.h"
#include "ui/gfx/font_list.h"

namespace gfx {
class RoundedCornersF;
}

namespace koan {

// The styling of the omnibox's suggestion rows in Zen's look (zen-omnibox.css
// .urlbarView-*; nothing changes in the shadcn look). Only the look: what a
// row says and which rows there are belong to the omnibox's providers.

// The height of a one-line suggestion row: Zen's 49pt where Chromium has
// `default_height` (40). Called from
// OmniboxMatchCellView::CalculatePreferredSize().
int OmniboxRowHeight(int default_height);

// The corners of a hovered or selected row: Zen's radius 8 on all four, where
// Chromium rounds the trailing two into a pill. Called from
// OmniboxResultView::GetPopupCellBackground().
void AdjustOmniboxRowRadii(gfx::RoundedCornersF& radii);

// The font of a row's text: Zen's 14pt, weight 500 (.urlbarView-title and
// -url). Called from OmniboxTextView::CreateRenderText().
gfx::FontList AdjustOmniboxRowFont(const gfx::FontList& font);

// The colour of a row's text: a title that is white at 70% in Zen (the
// omnibox's own text colour for the input stays); urls and dim text keep
// theirs. Called from OmniboxTextView::ApplyStyling()'s colour choice.
ui::ColorId AdjustOmniboxRowTextColorId(ui::ColorId id);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_OMNIBOX_ROWS_H_
