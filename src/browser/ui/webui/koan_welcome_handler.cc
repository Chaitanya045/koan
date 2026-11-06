// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_welcome_handler.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/scoped_refptr.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/importer/importer_list.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/choice_made_location.h"
#include "components/search_engines/search_engine_choice/search_engine_choice_utils.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "koan/browser/ui/webui/koan_welcome_host.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"
#include "url/gurl.h"

namespace koan {

namespace {

// Zen's search page leaves out Wikipedia and eBay (ZenWelcome.mjs).
bool IsExcludedEngine(const std::u16string& name) {
  const std::u16string lower = base::ToLowerASCII(name);
  return lower.find(u"wikipedia") != std::u16string::npos ||
         lower.find(u"ebay") != std::u16string::npos;
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
}

void KoanWelcomeHandler::RegisterMessages() {
  const auto add = [this](const char* name, auto method) {
    web_ui()->RegisterMessageCallback(
        name, base::BindRepeating(method, base::Unretained(this)));
  };
  add("getWelcomeData", &KoanWelcomeHandler::HandleGetWelcomeData);
  add("import", &KoanWelcomeHandler::HandleImport);
  add("setColor", &KoanWelcomeHandler::HandleSetColor);
  add("setColorScheme", &KoanWelcomeHandler::HandleSetColorScheme);
  add("setSearchEngine", &KoanWelcomeHandler::HandleSetSearchEngine);
  add("setDefaultBrowser", &KoanWelcomeHandler::HandleSetDefaultBrowser);
  add("finish", &KoanWelcomeHandler::HandleFinish);
  add("skip", &KoanWelcomeHandler::HandleSkip);
  add("startWindowDrag", &KoanWelcomeHandler::HandleStartWindowDrag);
  add("endWindowDrag", &KoanWelcomeHandler::HandleEndWindowDrag);
  add("showThemePicker", &KoanWelcomeHandler::HandleShowThemePicker);
  add("closeThemePicker", &KoanWelcomeHandler::HandleCloseThemePicker);
}

void KoanWelcomeHandler::HandleGetWelcomeData(const base::ListValue& args) {
  AllowJavascript();
  if (args.empty()) {
    return;
  }
  if (!GetHost(web_ui())) {
    // Not the first-run screen: the page shows nothing.
    ResolveJavascriptCallback(args[0], base::Value());
    return;
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
  base::ListValue sources;
  for (size_t i = 0; i < importer_list_->count(); ++i) {
    const user_data_importer::SourceProfile& source =
        importer_list_->GetSourceProfileAt(i);
    // Zen lists browsers, each with its profiles; Chromium's list has one
    // entry per browser profile, named "Browser - Profile" for a browser
    // with several.
    base::ListValue profiles;
    profiles.Append(base::DictValue()
                        .Set("id", static_cast<int>(i))
                        .Set("name", source.profile.empty()
                                         ? source.importer_name
                                         : source.profile));
    sources.Append(base::DictValue()
                       .Set("key", static_cast<int>(i))
                       .Set("name", source.importer_name)
                       .Set("image", "")
                       .Set("profiles", std::move(profiles)));
  }

  // The colours page: a Space's colours (Koan's Spaces take one of the tab
  // group colours, until #41's gradients), in the window's shades of them.
  base::ListValue colors;
  const ui::ColorProvider& provider =
      web_ui()->GetWebContents()->GetColorProvider();
  for (const auto& [id, label] : tab_groups::GetTabGroupColorLabelMap()) {
    colors.Append(
        base::DictValue()
            .Set("id", static_cast<int>(id))
            .Set("name", label)
            .Set("value", color_utils::SkColorToRgbaString(provider.GetColor(
                              GetTabGroupTabStripColorId(id, true)))));
  }

  int default_engine = 0;
  if (const TemplateURLService* service =
          TemplateURLServiceFactory::GetForProfile(profile_)) {
    if (const TemplateURL* engine = service->GetDefaultSearchProvider()) {
      default_engine = engine->prepopulate_id();
    }
  }
  // A policy that sets the default browser leaves the user nothing to pick
  // (settings hides it too); Zen has no such policy.
  const bool can_set_default_browser =
      !g_browser_process->local_state()->IsManagedPreference(
          prefs::kDefaultBrowserSettingEnabled) &&
      shell_integration::CanSetAsDefaultBrowser();

  base::Value theme_picker;
  if (const KoanWelcomeHost* host = GetHost(web_ui());
      host && host->callbacks().theme_picker_size) {
    if (const std::optional<gfx::Size> size =
            host->callbacks().theme_picker_size.Run()) {
      theme_picker = base::Value(base::DictValue()
                                     .Set("width", size->width())
                                     .Set("height", size->height()));
    }
  }

  // Zen's theme maths starts from the system's accent (AccentColor).
  base::Value system_accent;
  if (const KoanWelcomeHost* host = GetHost(web_ui());
      host && host->callbacks().system_accent) {
    if (const std::optional<SkColor> accent =
            host->callbacks().system_accent.Run()) {
      system_accent =
          base::Value(base::ListValue()
                          .Append(static_cast<int>(SkColorGetR(*accent)))
                          .Append(static_cast<int>(SkColorGetG(*accent)))
                          .Append(static_cast<int>(SkColorGetB(*accent))));
    }
  }

  ResolveJavascriptCallback(
      callback_id, base::DictValue()
                       .Set("systemAccent", std::move(system_accent))
                       .Set("isMac", static_cast<bool>(BUILDFLAG(IS_MAC)))
                       .Set("importSources", std::move(sources))
                       .Set("colors", std::move(colors))
                       .Set("themePicker", std::move(theme_picker))
                       .Set("engines", SearchEngines())
                       .Set("defaultEngine", default_engine)
                       .Set("canSetDefaultBrowser", can_set_default_browser));
}

base::ListValue KoanWelcomeHandler::SearchEngines() const {
  base::ListValue engines;
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile_);
  // None when a policy sets the engine: the page is skipped.
  if (!service || !service->CanDefaultSearchProviderBeModifiedByUser()) {
    return engines;
  }
  for (const TemplateURL* engine : service->GetTemplateURLs()) {
    if (engine->prepopulate_id() <= 0 ||
        IsExcludedEngine(engine->short_name())) {
      continue;
    }
    // The engine's logo where the build bundles it (Google Chrome builds
    // only: enable_builtin_search_provider_assets); else the page draws its
    // initial on a neutral tile. Never fetched from the network.
    const std::string image = engine->GetBuiltinImageResourceId();
    engines.Append(
        base::DictValue()
            .Set("id", engine->prepopulate_id())
            .Set("name", engine->short_name())
            .Set("icon", image == "IDR_DEFAULT_FAVICON"
                             ? std::string()
                             : base::StrCat({"chrome://theme/", image})));
  }
  return engines;
}

void KoanWelcomeHandler::HandleImport(const base::ListValue& args) {
  // [whether a browser is picked]: Chromium's import dialog after the screen,
  // where the user picks what to bring along.
  if (args.empty() || !args[0].is_bool()) {
    return;
  }
  if (const KoanWelcomeHost* host = GetHost(web_ui());
      host && host->callbacks().import) {
    host->callbacks().import.Run(args[0].GetBool());
  }
}

void KoanWelcomeHandler::HandleSetColor(const base::ListValue& args) {
  if (args.empty() || !args[0].is_int()) {
    return;
  }
  if (const KoanWelcomeHost* host = GetHost(web_ui());
      host && host->callbacks().space_color) {
    host->callbacks().space_color.Run(args[0].GetInt());
  }
}

void KoanWelcomeHandler::HandleSetColorScheme(const base::ListValue& args) {
  // The picker's auto / light / dark buttons.
  if (!GetHost(web_ui()) || args.empty() || !args[0].is_string()) {
    return;
  }
  const std::string& scheme = args[0].GetString();
  ThemeService* theme = ThemeServiceFactory::GetForProfile(profile_);
  if (!theme) {
    return;
  }
  theme->SetBrowserColorScheme(
      scheme == "light"  ? ThemeService::BrowserColorScheme::kLight
      : scheme == "dark" ? ThemeService::BrowserColorScheme::kDark
                         : ThemeService::BrowserColorScheme::kSystem);
}

void KoanWelcomeHandler::HandleSetSearchEngine(const base::ListValue& args) {
  // [engine's prepopulate id, whether the page is done (Next)].
  if (!GetHost(web_ui()) || args.size() < 2 || !args[0].is_int() ||
      !args[1].is_bool()) {
    return;
  }
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile_);
  // (With a policy's engine there is no page; Chromium CHECKs a user's pick
  // over it when the engines failed to load.) Only an engine the page was
  // given: the setter CHECKs others.
  const int id = args[0].GetInt();
  if (!service || !service->CanDefaultSearchProviderBeModifiedByUser() ||
      !std::ranges::any_of(SearchEngines(), [id](const base::Value& engine) {
        return engine.GetDict().FindInt("id") == id;
      })) {
    return;
  }
  // Zen sets it as it is picked. Next records the choice as made, as
  // Chromium's own choice screen does (where the region has one, it then
  // never shows): once, and not over an earlier record, which Chromium
  // doesn't expect from a choice screen.
  const bool record =
      args[1].GetBool() &&
      !search_engines::GetChoiceCompletionMetadata(*profile_->GetPrefs())
           .has_value();
  service->SetUserSelectedDefaultSearchProviderByPrepopulateId(
      id, record ? search_engines::ChoiceMadeLocation::kChoiceScreen
                 : search_engines::ChoiceMadeLocation::kOther);
}

void KoanWelcomeHandler::HandleSetDefaultBrowser(const base::ListValue& args) {
  if (!GetHost(web_ui()) ||
      g_browser_process->local_state()->IsManagedPreference(
          prefs::kDefaultBrowserSettingEnabled)) {
    return;
  }
  base::MakeRefCounted<shell_integration::DefaultBrowserWorker>()
      ->StartSetAsDefault(base::DoNothing());
}

void KoanWelcomeHandler::HandleFinish(const base::ListValue& args) {
  // Only the page's own Essentials, each once.
  std::vector<GURL> essentials;
  if (!args.empty() && args[0].is_list()) {
    for (const base::Value& url : args[0].GetList()) {
      if (!url.is_string()) {
        continue;
      }
      GURL gurl(url.GetString());
      if (FindWelcomeEssential(gurl) &&
          std::ranges::find(essentials, gurl) == essentials.end()) {
        essentials.push_back(std::move(gurl));
      }
    }
  }
  if (const KoanWelcomeHost* host = GetHost(web_ui());
      host && host->callbacks().finish) {
    host->callbacks().finish.Run(std::move(essentials));
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

void KoanWelcomeHandler::HandleShowThemePicker(const base::ListValue& args) {
  // The anchor's rect in the page: x, y, width, height.
  if (args.size() < 4 || !std::ranges::all_of(args, [](const base::Value& v) {
        return v.is_int() || v.is_double();
      })) {
    return;
  }
  const gfx::Rect anchor(base::ClampRound(args[0].GetDouble()),
                         base::ClampRound(args[1].GetDouble()),
                         base::ClampRound(args[2].GetDouble()),
                         base::ClampRound(args[3].GetDouble()));
  if (const KoanWelcomeHost* host = GetHost(web_ui());
      host && host->callbacks().show_theme_picker) {
    host->callbacks().show_theme_picker.Run(anchor);
  }
}

void KoanWelcomeHandler::HandleCloseThemePicker(const base::ListValue& args) {
  if (const KoanWelcomeHost* host = GetHost(web_ui());
      host && host->callbacks().close_theme_picker) {
    host->callbacks().close_theme_picker.Run();
  }
}

void KoanWelcomeHandler::HandleSkip(const base::ListValue& args) {
  if (const KoanWelcomeHost* host = GetHost(web_ui());
      host && host->callbacks().skip) {
    host->callbacks().skip.Run();
  }
}

}  // namespace koan
