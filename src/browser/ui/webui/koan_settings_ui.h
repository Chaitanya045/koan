// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_UI_H_
#define KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_UI_H_

#include <stdint.h>

#include "base/values.h"

namespace content {
class WebUI;
class WebUIDataSource;
}  // namespace content

namespace koan::settings {

// Koan's settings are panes of chrome://settings, as Zen's are panes of
// Firefox's about:preferences: one Settings page, one search. Chromium's
// settings app shows them as a SettingsPlugin (koan_settings_page.js), in the
// sections chrome://settings/koan/<pane> first in its menu; Chromium's search
// asks the plugin too. The routes, menu items, the plugin's view and the
// import that loads it are in the hook patches to
// chrome/browser/resources/settings (route.ts, settings_menu.html,
// settings_main.html, settings_main.ts, BUILD.gn).
//
// Called at the end of SettingsUI's constructor (settings_ui.cc hook): adds
// Koan's message handlers, the panes' titles ($i18n{koanPane<Id>}) and serves
// the plugin's files (koan_settings_page.js, koan_settings.css, koan_tokens.css
// and the fonts) from chrome://settings.
void ExtendSettingsUI(content::WebUI& web_ui, content::WebUIDataSource& source);

// Settings > Import, Chromium's import dialog (#363; import_data_handler.cc
// hook): Koan's own items of a source with `services_supported`, in the
// dialog's BrowserProfile ("koanSpaces": Spaces and pinned tabs, the
// checkbox of import_data_dialog.html.ts's hook); and the Koan items the
// dialog's checkboxes ask for (`types`: each checkbox's pref name, whether
// it is checked).
void AddImportDialogItems(uint16_t services_supported,
                          base::DictValue& browser_profile);
uint16_t GetImportDialogItems(const base::DictValue& types);

}  // namespace koan::settings

#endif  // KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_UI_H_
