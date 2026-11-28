// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_WEBUI_KOAN_WELCOME_HANDLER_H_
#define KOAN_BROWSER_UI_WEBUI_KOAN_WELCOME_HANDLER_H_

#include <memory>
#include <optional>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "content/public/browser/web_ui_message_handler.h"

class ImporterList;
class Profile;
class TemplateURLService;

namespace koan {

// chrome://koan-welcome's messages (onboarding v2, #181):
// - getWelcomeData: the shortcut rows with the user's side of each, the
//   browsers to import from (Chromium's ImporterList, with Koan's: Zen, Arc,
//   Chrome...; koan_import_sources.h) and their icons, the search engines
//   (Brave first and chosen; in a choice screen region Chromium's list).
//   Answered once both are ready, or at kWelcomeDataTimeout with what is
//   (#493: a detection or an engine load that never ends never holds the
//   page); what comes later is sent as "import-sources-changed" or
//   "engines-changed".
// - setShortcuts, import, setSearchEngine: a step's choice, on its Next.
// - finish, skip: close the screen (KoanWelcomeHost).
// - startWindowDrag, endWindowDrag: the window around the page.
// Only for the first-run screen (a page with a KoanWelcomeHost): the page
// typed into a tab gets no data and changes nothing.
class KoanWelcomeHandler : public content::WebUIMessageHandler {
 public:
  // How long the page waits for its data before it gets what is ready.
  static constexpr base::TimeDelta kWelcomeDataTimeout = base::Seconds(3);

  explicit KoanWelcomeHandler(Profile* profile);
  KoanWelcomeHandler(const KoanWelcomeHandler&) = delete;
  KoanWelcomeHandler& operator=(const KoanWelcomeHandler&) = delete;
  ~KoanWelcomeHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptDisallowed() override;

 private:
  void HandleGetWelcomeData(const base::ListValue& args);
  void OnImportSourcesLoaded();
  void OnEnginesLoaded();
  // Answers the page once the browsers and the engines are ready.
  void MaybeResolveWelcomeData();
  // Answers the page with what is ready.
  void ResolveWelcomeData();
  // The browsers found (none before the detection ends), with their icons;
  // they become the ones the page's import pick indexes.
  base::ListValue ImportSourceRows();
  // The search step's engines (none until they load), whether it is a
  // choice screen, and the engine chosen for the user.
  base::DictValue EngineData() const;
  bool EnginesReady() const;
  void HandleSetShortcuts(const base::ListValue& args);
  void HandleImport(const base::ListValue& args);
  void HandleSetSearchEngine(const base::ListValue& args);
  void HandleFinish(const base::ListValue& args);
  void HandleSkip(const base::ListValue& args);
  void HandleStartWindowDrag(const base::ListValue& args);
  void HandleEndWindowDrag(const base::ListValue& args);

  // Whether the profile is in a search engine choice screen region (the
  // EEA): the step is then a choice screen.
  bool IsChoiceScreenRegion() const;
  // The engines the page offers. In a choice screen region Chromium's choice
  // screen list, in its order; elsewhere Brave Search first, then the
  // region's prepopulated engines but Wikipedia and eBay (as Zen's list).
  // None when a policy sets the engine.
  base::ListValue SearchEngines() const;
  // The service, or null; with `editable`, null too when a policy sets the
  // engine.
  TemplateURLService* GetTemplateURLService(bool editable) const;

  const raw_ptr<Profile> profile_;
  std::unique_ptr<ImporterList> importer_list_;
  // The browsers the page was given, in its order (its import pick is an
  // index).
  std::vector<user_data_importer::SourceProfile> import_sources_;
  bool import_sources_loaded_ = false;
  // Waits for the search engines to load (first run: maybe not yet).
  base::CallbackListSubscription engines_loaded_;
  // The page's getWelcomeData request, until it is answered.
  std::optional<base::Value> callback_id_;
  base::OneShotTimer data_timeout_;

  base::WeakPtrFactory<KoanWelcomeHandler> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_WEBUI_KOAN_WELCOME_HANDLER_H_
