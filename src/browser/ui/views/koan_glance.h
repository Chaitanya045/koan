// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_GLANCE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_GLANCE_H_

#include "ui/base/window_open_disposition.h"

class BrowserWindowInterface;
class GURL;

namespace ui {
class SimpleMenuModel;
}

namespace content {
struct ContextMenuParams;
class WebContents;
struct OpenURLParams;
}  // namespace content

namespace koan {

// What MaybeOpenGlance() did with a navigation.
enum class GlanceClick {
  // Not a Glance activation click: handle it as usual.
  kNone,
  // Opened in Glance.
  kOpened,
  // An activation click Glance declines (not an http(s) link, not the active
  // tab of a normal window, Glance turned off): handle it with `fallback`,
  // what the modifier means without Glance.
  kDeclined,
};

// If `params` is a Glance gesture -- the user clicking a link with the Glance
// activation modifier (Alt by default, see koan_prefs.h) in a tab of
// `browser`, as in Zen -- opens the link in that window's Glance overlay (a
// floating card over the page). Called from
// BrowserWebContentsDelegate::OpenURLFromTab(), which reopens a declined
// click with `*fallback` as its disposition.
//
// Chromium normally turns such clicks into a download (Alt), a new tab
// (Ctrl, Cmd on macOS) or a new window (Shift) inside the renderer. With
// Koan Glance on, blink's NavigationPolicyFromEventModifiers() maps the
// activation click to a popup policy instead, which a link click never
// produces otherwise; it arrives here as a renderer-initiated NEW_POPUP from
// a trusted gesture.
GlanceClick MaybeOpenGlance(BrowserWindowInterface* browser,
                            content::WebContents* source,
                            const content::OpenURLParams& params,
                            WindowOpenDisposition* fallback);

// Command id of the link context menu's "Open Link in Glance" (Zen's
// zen-open-link-in-glance), in a range chrome_command_ids.h leaves free.
inline constexpr int kGlanceLinkCommandId = 55000;

inline bool IsGlanceLinkContextMenuCommand(int command_id) {
  return command_id == kGlanceLinkCommandId;
}

// Zen's link context menu items, called by the hook at the end of the
// open-link items in RenderViewContextMenu::AppendLinkItems(): upstream's
// "Open link in split view" gets Zen's label "Split Link to New Tab" (while
// the tab isn't split; then upstream opens the link in the other view,
// Chromium splits have two), and "Open Link in Glance" is added for http(s)
// links when Glance is on, in a tab of a normal window or a Glance page.
void AddZenLinkContextMenuItems(content::WebContents* source,
                                const GURL& link_url,
                                ui::SimpleMenuModel* menu);

// Runs "Open Link in Glance" for the menu's `params`: opens the link in the
// Glance card of source's window (replacing the page of an open card), with
// the frame's referrer and origin, growing out of the right-click.
void OpenLinkInGlance(content::WebContents* source,
                      const content::ContextMenuParams& params);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_GLANCE_H_
