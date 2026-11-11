// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_UI_H_
#define KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_UI_H_

namespace content {
class WebUI;
class WebUIDataSource;
}  // namespace content

namespace koan::settings {

// Koan's settings are panes of chrome://settings, as Zen's are panes of
// Firefox's about:preferences: one Settings page, one search. Chromium's
// settings app shows them as a SettingsPlugin (koan_settings.js), in the
// sections chrome://settings/koan/<pane> first in its menu; Chromium's search
// asks the plugin too. The routes, menu items and the plugin's view are in
// the hook patches to chrome/browser/resources/settings (route.ts,
// settings_menu.html, settings_main.html, settings.html).
//
// Called at the end of SettingsUI's constructor (settings_ui.cc hook): adds
// Koan's message handlers, the panes' titles ($i18n{koanPane<Id>}) and serves
// the plugin's files (koan_settings.js, koan_settings.css, koan_tokens.css and
// the fonts) from chrome://settings.
void ExtendSettingsUI(content::WebUI& web_ui, content::WebUIDataSource& source);

}  // namespace koan::settings

#endif  // KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_UI_H_
