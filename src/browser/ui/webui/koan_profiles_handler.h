// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_WEBUI_KOAN_PROFILES_HANDLER_H_
#define KOAN_BROWSER_UI_WEBUI_KOAN_PROFILES_HANDLER_H_

#include <cstdint>
#include <string>

#include "base/containers/flat_map.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"

class Profile;

namespace koan {

// Settings > Tab Management > Profiles (#477, koan_space_profiles.h).
// Messages from the page:
//   koanGetProfiles()                -> Profiles()
//   koanGetProfileSizes()            -> {id: bytes on disk}
//   koanRenameProfile(id, name)      -> bool
//   koanSetProfileColor(id, color)   -> bool
//   koanClearProfileData(id)         -> bool, once cleared
//   koanDeleteProfile(id)            -> bool, once its data is gone
//   koanShowProfileExtensions(id)    -> bool: the Space menu's Extensions...
//                                       for a Space that uses it
// and the event "koan-profiles-changed" (Profiles()) when a profile is made,
// renamed, recoloured, deleted, or given another proxy or extension list
// anywhere (the Create-a-Space form, a Space's menu, another settings tab),
// and when an extension is installed, removed or updated (the names of the
// ones that are off).
class KoanProfilesHandler : public content::WebUIMessageHandler,
                            public extensions::ExtensionRegistryObserver {
 public:
  // `profile`: the regular profile whose profiles are shown.
  explicit KoanProfilesHandler(Profile& profile);
  KoanProfilesHandler(const KoanProfilesHandler&) = delete;
  KoanProfilesHandler& operator=(const KoanProfilesHandler&) = delete;
  ~KoanProfilesHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // extensions::ExtensionRegistryObserver:
  void OnExtensionInstalled(content::BrowserContext* browser_context,
                            const extensions::Extension* extension,
                            bool is_update) override;
  void OnExtensionUninstalled(content::BrowserContext* browser_context,
                              const extensions::Extension* extension,
                              extensions::UninstallReason reason) override;

 private:
  void HandleGet(const base::ListValue& args);
  void HandleGetSizes(const base::ListValue& args);
  void HandleRename(const base::ListValue& args);
  void HandleSetColor(const base::ListValue& args);
  void HandleClearData(const base::ListValue& args);
  void HandleDelete(const base::ListValue& args);
  void HandleShowExtensions(const base::ListValue& args);

  void OnSizes(base::Value callback_id,
               base::flat_map<std::string, int64_t> sizes);
  void Resolve(base::Value callback_id, bool ok);
  void OnProfilesChanged();

  // {profiles: [{id, name, color, spaces: [name], openTabs, essentials,
  //               pinnedTabs, proxy: {server, disconnected} or null,
  //               extensionsOff: [{id, name, icon}]}],
  //  colors: [{color, name, css}]}
  base::DictValue Profiles();

  const raw_ref<Profile> profile_;
  PrefChangeRegistrar pref_change_registrar_;
  base::ScopedObservation<extensions::ExtensionRegistry,
                          extensions::ExtensionRegistryObserver>
      extension_registry_observation_{this};
  base::WeakPtrFactory<KoanProfilesHandler> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_WEBUI_KOAN_PROFILES_HANDLER_H_
