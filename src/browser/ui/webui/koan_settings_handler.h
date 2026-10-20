// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_HANDLER_H_
#define KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_HANDLER_H_

#include <string>

#include "base/memory/raw_ref.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

class PrefService;

namespace koan {

// Backs chrome://koan-settings. Messages from the page:
//   getSettings()            -> everything the page shows (see Settings())
//   setPref(pref, value)     sets a pref that has a row in koan_settings_rows
//   setShortcut(id, key)     key: {key, modifiers}, or null for "not set"
//   resetShortcuts()         back to the defaults
//   setEditing(editing)      suspends the window's shortcuts while a key is
//                            being recorded, so the page sees every key
// and events to it: "pref-changed" (pref, value) and "shortcuts-changed"
// (shortcuts), so the page follows changes made elsewhere (e.g. Ctrl+S).
class KoanSettingsHandler : public content::WebUIMessageHandler,
                            public views::WidgetObserver {
 public:
  explicit KoanSettingsHandler(PrefService& prefs);
  KoanSettingsHandler(const KoanSettingsHandler&) = delete;
  KoanSettingsHandler& operator=(const KoanSettingsHandler&) = delete;
  ~KoanSettingsHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  void HandleGetSettings(const base::ListValue& args);
  void HandleSetPref(const base::ListValue& args);
  void HandleSetShortcut(const base::ListValue& args);
  void HandleResetShortcuts(const base::ListValue& args);
  void HandleSetEditing(const base::ListValue& args);

  // Suspends or resumes the shortcuts of the window showing the page.
  void SuspendShortcuts(bool suspend);

  void OnPrefChanged(const std::string& pref);

  base::DictValue Settings() const;
  base::DictValue Shortcuts() const;

  const raw_ref<PrefService> prefs_;
  PrefChangeRegistrar pref_change_registrar_;
  // The window whose shortcuts are suspended, if any.
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      suspended_widget_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_HANDLER_H_
