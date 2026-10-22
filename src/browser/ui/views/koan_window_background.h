// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_BACKGROUND_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_BACKGROUND_H_

#include <optional>

class BrowserView;

namespace content {
class WebContents;
}

namespace gfx {
class Canvas;
}

namespace views {
class View;
}

namespace koan {

// Hooks for Zen's Space themes (KoanThemeController).

// From ThemedBackground: paints the window's background (the active Space's
// gradient) behind `view`, a view of `browser_view`'s window, instead of the
// theme's toolbar or frame colour. Returns false where Koan doesn't paint the
// window.
bool PaintWindowBackground(gfx::Canvas* canvas,
                           const views::View* view,
                           const BrowserView* browser_view);

// Whether Koan paints `browser_view`'s window background (then it has no
// solid colour).
bool PaintsWindowBackground(const BrowserView* browser_view);

// From ChromeContentBrowserClient: a Space theme may make a window dark or
// light, but websites follow the appearance setting (Automatic / Light /
// Dark), like Zen's. Whether pages in `web_contents` get a dark colour
// scheme, or nullopt where Chrome decides (also for WebUI pages, which are
// coloured like the window).
std::optional<bool> PrefersDarkWebColorScheme(
    content::WebContents* web_contents,
    bool is_web_ui);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_BACKGROUND_H_
