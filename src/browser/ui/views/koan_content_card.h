// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_CONTENT_CARD_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_CONTENT_CARD_H_

#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rounded_corners_f.h"

class BrowserView;

namespace koan {

// Corner radius of the Zen-style content card.
inline constexpr float kContentCardCornerRadius = 8;

// Gap between the content card and the window edges.
inline constexpr int kContentCardMargin = 8;

// Whether MultiContentsView should draw its single (non-split) contents as a
// floating card: inset by ContentCardInsets() over the themed background,
// with rounded corners and no separators. Split view keeps upstream's
// layout, which already uses cards.
bool UseContentCard(const BrowserView* browser_view);

// Insets of the content card inside MultiContentsView.
gfx::Insets ContentCardInsets();

// Corner radii for the active contents outside split view: the card's radii
// when UseContentCard(), otherwise upstream's `default_radii`. Only the
// contents are rounded; MultiContentsView's background stays square so it
// keeps filling the gutter behind the card's corners.
gfx::RoundedCornersF ActiveContentsRadii(
    const BrowserView* browser_view,
    const gfx::RoundedCornersF& default_radii);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_CONTENT_CARD_H_
