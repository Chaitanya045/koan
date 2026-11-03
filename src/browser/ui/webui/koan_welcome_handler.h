// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_WEBUI_KOAN_WELCOME_HANDLER_H_
#define KOAN_BROWSER_UI_WEBUI_KOAN_WELCOME_HANDLER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_ui_message_handler.h"

class ImporterList;
class Profile;

namespace koan {

// chrome://koan-welcome's messages: what Zen's welcome asks of Firefox.
// - getWelcomeData: the browsers to import from (Chromium's ImporterList),
//   the colours, the search engines and the default one.
// - import, setColor, setSearchEngine, setDefaultBrowser: the pages' choices.
// - finish (the Essentials to pin), skip: close the screen (KoanWelcomeHost).
class KoanWelcomeHandler : public content::WebUIMessageHandler {
 public:
  explicit KoanWelcomeHandler(Profile* profile);
  KoanWelcomeHandler(const KoanWelcomeHandler&) = delete;
  KoanWelcomeHandler& operator=(const KoanWelcomeHandler&) = delete;
  ~KoanWelcomeHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  void HandleGetWelcomeData(const base::ListValue& args);
  void OnImportSourcesLoaded(base::Value callback_id);
  void HandleImport(const base::ListValue& args);
  void HandleSetColor(const base::ListValue& args);
  void HandleSetColorScheme(const base::ListValue& args);
  void HandleSetSearchEngine(const base::ListValue& args);
  void HandleSetDefaultBrowser(const base::ListValue& args);
  void HandleFinish(const base::ListValue& args);
  void HandleSkip(const base::ListValue& args);
  void HandleStartWindowDrag(const base::ListValue& args);

  base::ListValue SearchEngines() const;

  const raw_ptr<Profile> profile_;
  std::unique_ptr<ImporterList> importer_list_;

  base::WeakPtrFactory<KoanWelcomeHandler> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_WEBUI_KOAN_WELCOME_HANDLER_H_
