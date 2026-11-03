// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   zen-omnibox.css (the suggestion rows' height, radius and font)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_OMNIBOX_ROWS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_OMNIBOX_ROWS_H_

#include "ui/color/color_id.h"
#include "ui/gfx/font_list.h"

namespace gfx {
class RoundedCornersF;
}

namespace views {
class View;
}

namespace koan {

// The styling of the omnibox's suggestion rows in Zen's floating URL bar card
// (zen-omnibox.css .urlbarView-*). Each hook takes the `view` it is called
// for (a row's view, in the omnibox popup of a browser window) and changes
// nothing unless that window's floating URL bar is Zen's card
// (ShowsZenCardRows(), koan_url_bar.h): not in the shadcn look, and not in
// any other omnibox popup. Only the look: what a row says and which rows
// there are belong to the omnibox's providers.

// The height of a one-line suggestion row: Zen's 49pt where Chromium has
// `default_height` (40). Called from
// OmniboxMatchCellView::CalculatePreferredSize().
int OmniboxRowHeight(const views::View* view, int default_height);

// The corners of a hovered or selected row: Zen's radius 8 on all four, where
// Chromium rounds the trailing two into a pill. Called from
// OmniboxResultView::GetPopupCellBackground().
void AdjustOmniboxRowRadii(const views::View* view,
                           gfx::RoundedCornersF& radii);

// The font of a row's text: Zen's 14pt, weight 500 (.urlbarView-title and
// -url). Called from OmniboxTextView::CreateRenderText().
gfx::FontList AdjustOmniboxRowFont(const views::View* view,
                                   const gfx::FontList& font);

// The colour of a row's text: a title that is white at 70% in Zen (the
// omnibox's own text colour for the input stays); urls and dim text keep
// theirs. Called from OmniboxTextView::ApplyStyling()'s colour choice.
ui::ColorId AdjustOmniboxRowTextColorId(const views::View* view,
                                        ui::ColorId id);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_OMNIBOX_ROWS_H_
