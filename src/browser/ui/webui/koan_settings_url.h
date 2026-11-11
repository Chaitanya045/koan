// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_URL_H_
#define KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_URL_H_

#include <optional>

class GURL;

namespace content {
class BrowserContext;
}

namespace koan::settings {

// chrome://koan-settings, Koan's settings page before its panes moved into
// chrome://settings, still opens them (bookmarks, history, the old e2e
// suites): chrome://koan-settings[/<pane>][?query][#ref] is
// chrome://settings/koan/<pane>, the first pane without one. Nullopt for any
// other URL. Pure, for the unit test.
std::optional<GURL> SettingsURLForLegacyURL(const GURL& url);

// Whether `url` is a chrome://settings page (any section). Pure.
bool IsSettingsURL(const GURL& url);

// The BrowserURLHandler pair for the above (hooked in
// ChromeContentBrowserClient::BrowserURLHandlerCreated). Rewrites only in a
// regular profile: chrome://settings refuses incognito, so the alias doesn't
// open it there. The reverse handler shows the page's real URL in the URL
// bar.
bool RewriteLegacySettingsURL(GURL* url,
                              content::BrowserContext* browser_context);
bool ReverseLegacySettingsURL(GURL* url,
                              content::BrowserContext* browser_context);

}  // namespace koan::settings

#endif  // KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_URL_H_
