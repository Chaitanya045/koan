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

// If `params` is a Glance gesture -- the user Alt+clicking a link in a tab of
// `browser`, as in Zen -- opens the link in that window's Glance overlay (a
// floating card over the page) and returns true. Called from
// BrowserWebContentsDelegate::OpenURLFromTab().
//
// Chromium normally turns Alt+click into a download inside the renderer.
// With Koan Glance on, blink's NavigationPolicyFromEventModifiers() maps it
// to a popup policy instead, which a link click never produces otherwise; it
// arrives here as a renderer-initiated NEW_POPUP from a trusted gesture.
bool MaybeOpenGlance(BrowserWindowInterface* browser,
                     content::WebContents* source,
                     const content::OpenURLParams& params);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_GLANCE_H_
