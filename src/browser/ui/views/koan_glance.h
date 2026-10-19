// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_GLANCE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_GLANCE_H_

class BrowserWindowInterface;

namespace content {
class WebContents;
struct OpenURLParams;
}  // namespace content

namespace koan {

// If `params` is a Glance gesture -- the user Shift+clicking a link in a tab
// of `browser` -- opens the link in that window's Glance overlay (a floating
// card over the page) and returns true. Called from
// BrowserWebContentsDelegate::OpenURLFromTab().
//
// Zen uses Alt+click; in Chromium Alt+click is decided in the renderer as a
// download and never reaches the browser as a navigation, whereas Shift+click
// arrives as a renderer-initiated NEW_WINDOW navigation that can be
// redirected without touching blink or content.
bool MaybeOpenGlance(BrowserWindowInterface* browser,
                     content::WebContents* source,
                     const content::OpenURLParams& params);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_GLANCE_H_
