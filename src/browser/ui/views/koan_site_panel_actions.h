// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SITE_PANEL_ACTIONS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SITE_PANEL_ACTIONS_H_

class BrowserView;
class GURL;

namespace content {
class WebContents;
}

namespace views {
class BubbleDialogDelegateView;
}

// Zen's site panel actions (issue #78; Zen's #zen-site-data-header in
// zen-panels/site-data.inc, ZenSiteDataPanel.sys.mjs).
namespace koan {

// Adds Zen's header row to the site panel `bubble` (Chromium's page info
// bubble, opened from the URL pill's site icon) for the page in `contents`:
// Share This Page, Take a Screenshot, Reader Mode and Bookmark This Page,
// over Chromium's own commands. Zen's Boost button is not there (Koan has no
// Boosts). Does nothing unless `bubble` is a PageInfoBubbleView (not for
// chrome:// pages' internal bubble).
void AddSitePanelActions(views::BubbleDialogDelegateView* bubble,
                         BrowserView* browser_view,
                         content::WebContents* contents);

// Whether pages can be shared through the system's share sheet (macOS, as
// Zen's Services.zen.canShare()).
bool CanSharePage();

// Shares `url` (without its tracking parameters: callers pass
// StripTrackingParameters()) through macOS's share sheet, shown at the
// pointer: the button just pressed. Chromium's own picker, the one
// navigator.share() uses (RenderWidgetHostView::ShowSharePicker()).
void SharePage(content::WebContents* contents, const GURL& url);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SITE_PANEL_ACTIONS_H_
