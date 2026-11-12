// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_welcome_handler.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/importer/importer_list.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/regional_capabilities/regional_capabilities_service_factory.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "components/prefs/pref_service.h"
#include "components/regional_capabilities/regional_capabilities_service.h"
#include "components/search_engines/choice_made_location.h"
#include "components/search_engines/search_engine_choice/search_engine_choice_utils.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "koan/browser/importer/koan_import_platform.h"
#include "koan/browser/importer/koan_import_sources.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_welcome_shortcuts.h"
#include "koan/browser/ui/webui/koan_welcome_host.h"
#include "third_party/search_engines_data/resources/definitions/prepopulated_engines.h"

namespace koan {

namespace {

// Zen's list leaves out Wikipedia and eBay (ZenWelcome.mjs).
bool IsExcludedEngine(const std::u16string& name) {
  const std::u16string lower = base::ToLowerASCII(name);
  return lower.find(u"wikipedia") != std::u16string::npos ||
         lower.find(u"ebay") != std::u16string::npos;
}

// Brave Search, Koan's suggested engine (#181): a prepopulated engine of
// Chromium's (prepopulated_engines.json), in most regions' lists already.
const TemplateURLPrepopulateData::PrepopulatedEngine& BraveEngine() {
  return TemplateURLPrepopulateData::brave;
}

// The engine's mark on the search step (welcome_icons/engine-<name>.svg,
// Simple Icons), by Chromium's prepopulated engine. Chromium's own choice
// screen shows logos only in Google Chrome builds (IDR_*_IMAGE from the
// internal search_engines_data resources), and nothing is fetched here before
// the user chooses, so the rest (Bing and Yahoo among them, which Simple Icons
// left out at their owners' request) keep their initial.
std::string_view EngineMark(int prepopulate_id) {
  namespace data = TemplateURLPrepopulateData;
  static constexpr std::pair<const data::PrepopulatedEngine*, std::string_view>
      kMarks[] = {
          {&data::baidu, "engine-baidu"},
          {&data::brave, "engine-brave"},
          {&data::duckduckgo, "engine-duckduckgo"},
          {&data::ecosia, "engine-ecosia"},
          {&data::google, "engine-google"},
          {&data::mojeek, "engine-mojeek"},
          {&data::naver, "engine-naver"},
          {&data::qwant, "engine-qwant"},
          {&data::startpage, "engine-startpage"},
      };
  for (const auto& [engine, mark] : kMarks) {
    if (engine->id == prepopulate_id) {
      return mark;
    }
  }
  return std::string_view();
}

// The search step's row for an engine: its id, name and mark (empty for the
// initial).
base::DictValue EngineRow(int prepopulate_id, std::u16string_view name) {
  return base::DictValue()
      .Set("id", prepopulate_id)
      .Set("name", name)
      .Set("mark", EngineMark(prepopulate_id));
}

const KoanWelcomeHost* GetHost(content::WebUI* web_ui) {
  return KoanWelcomeHost::FromWebContents(web_ui->GetWebContents());
}

}  // namespace

KoanWelcomeHandler::KoanWelcomeHandler(Profile* profile) : profile_(profile) {}

KoanWelcomeHandler::~KoanWelcomeHandler() = default;

void KoanWelcomeHandler::OnJavascriptDisallowed() {
  // The page's renderer is gone or reloading: no answer to its old request.
  weak_factory_.InvalidateWeakPtrs();
  importer_list_.reset();
  engines_loaded_ = {};
}

void KoanWelcomeHandler::RegisterMessages() {
  const auto add = [this](const char* name, auto method) {
    web_ui()->RegisterMessageCallback(
        name, base::BindRepeating(method, base::Unretained(this)));
  };
  add("getWelcomeData", &KoanWelcomeHandler::HandleGetWelcomeData);
  add("setShortcuts", &KoanWelcomeHandler::HandleSetShortcuts);
  add("import", &KoanWelcomeHandler::HandleImport);
  add("setSearchEngine", &KoanWelcomeHandler::HandleSetSearchEngine);
  add("finish", &KoanWelcomeHandler::HandleFinish);
  add("skip", &KoanWelcomeHandler::HandleSkip);
  add("startWindowDrag", &KoanWelcomeHandler::HandleStartWindowDrag);
  add("endWindowDrag", &KoanWelcomeHandler::HandleEndWindowDrag);
}

void KoanWelcomeHandler::HandleGetWelcomeData(const base::ListValue& args) {
  AllowJavascript();
  if (args.empty()) {
    return;
  }
  const KoanWelcomeHost* host = GetHost(web_ui());
  if (!host) {
    // Not the first-run screen: the page shows nothing.
    ResolveJavascriptCallback(args[0], base::Value());
    return;
  }
  if (host->callbacks().page_ready) {
    host->callbacks().page_ready.Run();
  }
  // The browsers to import from first (Chromium's import dialog's list).
  importer_list_ = std::make_unique<ImporterList>();
  importer_list_->DetectSourceProfiles(
      g_browser_process->GetApplicationLocale(),
      /*include_interactive_profiles=*/true,
      base::BindOnce(&KoanWelcomeHandler::OnImportSourcesLoaded,
                     weak_factory_.GetWeakPtr(), args[0].Clone()));
}

void KoanWelcomeHandler::OnImportSourcesLoaded(base::Value callback_id) {
  // On a first run the search engines may still be loading.
  TemplateURLService* service = GetTemplateURLService(/*editable=*/false);
  if (service && !service->loaded()) {
    engines_loaded_ = service->RegisterOnLoadedCallback(
        base::BindOnce(&KoanWelcomeHandler::ResolveWelcomeData,
                       weak_factory_.GetWeakPtr(), std::move(callback_id)));
    service->Load();
    return;
  }
  ResolveWelcomeData(std::move(callback_id));
}

void KoanWelcomeHandler::ResolveWelcomeData(base::Value callback_id) {
  if (!importer_list_) {
    return;
  }
  base::ListValue sources;
  import_sources_.clear();
  for (size_t i = 0; i < importer_list_->count(); ++i) {
    const user_data_importer::SourceProfile& source =
        importer_list_->GetSourceProfileAt(i);
    // One entry per browser profile; the profile under the browser's name,
    // with the browser's app icon when it is installed (Zen's welcome shows
    // each browser's logo).
    const std::string_view bundle_id = importer::BundleIdOf(source);
    sources.Append(base::DictValue()
                       .Set("name", source.importer_name)
                       .Set("profile", source.profile)
                       .Set("icon", bundle_id.empty()
                                        ? std::string()
                                        : importer::AppIconDataUrl(bundle_id)));
    import_sources_.push_back(source);
  }
  importer_list_.reset();

  base::ListValue rows;
  const base::DictValue& overrides =
      profile_->GetPrefs()->GetDict(prefs::kShortcuts);
  for (const shortcuts::WelcomeRow& row : shortcuts::WelcomeRows()) {
    const auto side = [](const shortcuts::WelcomeSide& welcome_side) {
      return base::DictValue()
          .Set("keys", welcome_side.keys)
          .Set("label", welcome_side.label);
    };
    rows.Append(base::DictValue()
                    .Set("id", row.id)
                    .Set("title", row.title)
                    .Set("zen", side(row.zen))
                    .Set("chrome", side(row.chrome))
                    .Set("chosen", shortcuts::HasChromeSide(overrides, row.id)
                                       ? "chrome"
                                       : "zen"));
  }

  base::ListValue engines = SearchEngines();
  // In a choice screen region nothing is chosen for the user (the EEA's
  // rules for a choice screen); elsewhere Brave is, until the user picks
  // another.
  const bool choice_screen = IsChoiceScreenRegion();
  ResolveJavascriptCallback(
      callback_id,
      base::DictValue()
          .Set("isMac", static_cast<bool>(BUILDFLAG(IS_MAC)))
          .Set("shortcutRows", std::move(rows))
          .Set("importSources", std::move(sources))
          .Set("engines", std::move(engines))
          .Set("choiceScreen", choice_screen)
          .Set("defaultEngine",
               choice_screen ? base::Value() : base::Value(BraveEngine().id)));
}

TemplateURLService* KoanWelcomeHandler::GetTemplateURLService(
    bool editable) const {
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile_);
  // (A policy's engine: there is no step, and Chromium CHECKs a user's pick
  // over it when the engines failed to load.)
  if (!service ||
      (editable && !service->CanDefaultSearchProviderBeModifiedByUser())) {
    return nullptr;
  }
  return service;
}

bool KoanWelcomeHandler::IsChoiceScreenRegion() const {
  regional_capabilities::RegionalCapabilitiesService* regional =
      regional_capabilities::RegionalCapabilitiesServiceFactory::GetForProfile(
          profile_);
  return regional && regional->IsInSearchEngineChoiceScreenRegion();
}

base::ListValue KoanWelcomeHandler::SearchEngines() const {
  base::ListValue engines;
  TemplateURLService* service = GetTemplateURLService(/*editable=*/true);
  if (!service) {
    return engines;
  }
  if (IsChoiceScreenRegion()) {
    // Chromium's own choice screen list, in its order, as it is.
    for (const std::unique_ptr<TemplateURL>& engine :
         service->GetChoiceScreenData()->search_engines()) {
      if (engine->prepopulate_id() > 0) {
        engines.Append(
            EngineRow(engine->prepopulate_id(), engine->short_name()));
      }
    }
    return engines;
  }
  // Brave first, whether or not the region lists it (upstream's setter
  // resolves it from the full list).
  engines.Append(EngineRow(BraveEngine().id, BraveEngine().name));
  for (const TemplateURL* engine : service->GetTemplateURLs()) {
    if (engine->prepopulate_id() <= 0 ||
        engine->prepopulate_id() == BraveEngine().id ||
        engine->type() != TemplateURL::NORMAL ||
        IsExcludedEngine(engine->short_name())) {
      continue;
    }
    engines.Append(EngineRow(engine->prepopulate_id(), engine->short_name()));
  }
  return engines;
}

void KoanWelcomeHandler::HandleSetShortcuts(const base::ListValue& args) {
  // [{row id: "zen" | "chrome"}]: each known row's side.
  if (!GetHost(web_ui()) || args.empty() || !args[0].is_dict()) {
    return;
  }
  base::DictValue overrides =
      profile_->GetPrefs()->GetDict(prefs::kShortcuts).Clone();
  for (const shortcuts::WelcomeRow& row : shortcuts::WelcomeRows()) {
    if (const std::string* side = args[0].GetDict().FindString(row.id);
        side && (*side == "zen" || *side == "chrome")) {
      shortcuts::SetWelcomeSide(overrides, row.id, *side == "chrome");
    }
  }
  profile_->GetPrefs()->SetDict(prefs::kShortcuts, std::move(overrides));
}

void KoanWelcomeHandler::HandleImport(const base::ListValue& args) {
  // [whether a browser is picked, its index in the list]: Chromium's import
  // dialog after the screen, where the user picks what to bring along, opens
  // on that browser (as Zen's welcome opens its wizard on one browser).
  if (args.empty() || !args[0].is_bool()) {
    return;
  }
  if (args.size() > 1 && args[1].is_int() && args[1].GetInt() >= 0 &&
      static_cast<size_t>(args[1].GetInt()) < import_sources_.size() &&
      GetHost(web_ui())) {
    importer::SetPreferredSource(import_sources_[args[1].GetInt()]);
  }
  if (const KoanWelcomeHost* host = GetHost(web_ui());
      host && host->callbacks().import) {
    host->callbacks().import.Run(args[0].GetBool());
  }
}

void KoanWelcomeHandler::HandleSetSearchEngine(const base::ListValue& args) {
  // [the engine's prepopulate id], on the step's Next.
  if (!GetHost(web_ui()) || args.empty() || !args[0].is_int()) {
    return;
  }
  TemplateURLService* service = GetTemplateURLService(/*editable=*/true);
  const int id = args[0].GetInt();
  // Only an engine the page was given.
  if (!service ||
      !std::ranges::any_of(SearchEngines(), [id](const base::Value& engine) {
        return engine.GetDict().FindInt("id") == id;
      })) {
    return;
  }
  // In a choice screen region the step is that choice screen (Chromium's
  // list, nothing preselected): recorded as a choice made, so Chromium's
  // dialog never shows; once, and not over an earlier record, which Chromium
  // doesn't expect from a choice screen. Elsewhere a plain user choice.
  const bool record =
      IsChoiceScreenRegion() &&
      !search_engines::GetChoiceCompletionMetadata(*profile_->GetPrefs())
           .has_value();
  // Upstream's setter: the existing engine, or the region's variant of it
  // from the full prepopulated list (Brave where the region lacks it).
  service->SetUserSelectedDefaultSearchProviderByPrepopulateId(
      id, record ? search_engines::ChoiceMadeLocation::kChoiceScreen
                 : search_engines::ChoiceMadeLocation::kOther);
}

void KoanWelcomeHandler::HandleFinish(const base::ListValue& args) {
  if (const KoanWelcomeHost* host = GetHost(web_ui());
      host && host->callbacks().finish) {
    host->callbacks().finish.Run();
  }
}

void KoanWelcomeHandler::HandleSkip(const base::ListValue& args) {
  if (const KoanWelcomeHost* host = GetHost(web_ui());
      host && host->callbacks().skip) {
    host->callbacks().skip.Run();
  }
}

void KoanWelcomeHandler::HandleStartWindowDrag(const base::ListValue& args) {
  if (const KoanWelcomeHost* host = GetHost(web_ui());
      host && host->callbacks().window_drag) {
    host->callbacks().window_drag.Run();
  }
}

void KoanWelcomeHandler::HandleEndWindowDrag(const base::ListValue& args) {
  if (const KoanWelcomeHost* host = GetHost(web_ui());
      host && host->callbacks().window_drag_end) {
    host->callbacks().window_drag_end.Run();
  }
}

}  // namespace koan
