// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_welcome_handler.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/importer/importer_list.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "koan/browser/ui/webui/koan_welcome_host.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/gfx/color_utils.h"
#include "url/gurl.h"

namespace koan {

namespace {

// The colours page's choices (the native screen's): Koan tints with one
// colour where Zen has a gradient picker.
struct SpaceColor {
  SkColor color;
  const char* name;
};
constexpr auto kSpaceColors = std::to_array<SpaceColor>({
    {SkColorSetRGB(0xd9, 0x6c, 0x4f), "Coral"},
    {SkColorSetRGB(0xe0, 0xa1, 0x3a), "Amber"},
    {SkColorSetRGB(0x6f, 0xa8, 0x55), "Green"},
    {SkColorSetRGB(0x3e, 0x9d, 0x92), "Teal"},
    {SkColorSetRGB(0x4f, 0x86, 0xd9), "Blue"},
    {SkColorSetRGB(0x7a, 0x6c, 0xe0), "Violet"},
    {SkColorSetRGB(0xb4, 0x5f, 0xc9), "Purple"},
    {SkColorSetRGB(0xd9, 0x5f, 0x93), "Pink"},
    {SkColorSetRGB(0x8a, 0x8f, 0x98), "Grey"},
});

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

void KoanWelcomeHandler::RegisterMessages() {
  const auto add = [this](const char* name, auto method) {
    web_ui()->RegisterMessageCallback(
        name, base::BindRepeating(method, base::Unretained(this)));
  };
  add("getWelcomeData", &KoanWelcomeHandler::HandleGetWelcomeData);
  add("import", &KoanWelcomeHandler::HandleImport);
  add("setColor", &KoanWelcomeHandler::HandleSetColor);
  add("setSearchEngine", &KoanWelcomeHandler::HandleSetSearchEngine);
  add("setDefaultBrowser", &KoanWelcomeHandler::HandleSetDefaultBrowser);
  add("finish", &KoanWelcomeHandler::HandleFinish);
  add("skip", &KoanWelcomeHandler::HandleSkip);
}

void KoanWelcomeHandler::HandleGetWelcomeData(const base::ListValue& args) {
  AllowJavascript();
  if (args.empty()) {
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

  base::ListValue colors;
  for (const SpaceColor& color : kSpaceColors) {
    colors.Append(base::DictValue()
                      .Set("name", color.name)
                      .Set("value", color_utils::SkColorToRgbaString(
                                        SkColorSetA(color.color, 0xff))));
  }

  int default_engine = 0;
  if (const TemplateURLService* service =
          TemplateURLServiceFactory::GetForProfile(profile_)) {
    if (const TemplateURL* engine = service->GetDefaultSearchProvider()) {
      default_engine = engine->prepopulate_id();
    }
  }

  ResolveJavascriptCallback(callback_id,
                            base::DictValue()
                                .Set("importSources", std::move(sources))
                                .Set("colors", std::move(colors))
                                .Set("engines", SearchEngines())
                                .Set("defaultEngine", default_engine));
}

base::ListValue KoanWelcomeHandler::SearchEngines() const {
  base::ListValue engines;
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile_);
  if (!service) {
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
  // Chromium's import dialog, where the user picks what to bring along.
  if (const KoanWelcomeHost* host = GetHost(web_ui());
      host && host->callbacks().import) {
    host->callbacks().import.Run();
  }
}

void KoanWelcomeHandler::HandleSetColor(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  const std::string& value = args[0].GetString();
  for (const SpaceColor& color : kSpaceColors) {
    if (color_utils::SkColorToRgbaString(SkColorSetA(color.color, 0xff)) ==
        value) {
      if (ThemeService* theme = ThemeServiceFactory::GetForProfile(profile_)) {
        theme->SetUserColorAndBrowserColorVariant(
            color.color, ui::mojom::BrowserColorVariant::kTonalSpot);
      }
      return;
    }
  }
}

void KoanWelcomeHandler::HandleSetSearchEngine(const base::ListValue& args) {
  if (args.empty() || !args[0].is_int()) {
    return;
  }
  if (TemplateURLService* service =
          TemplateURLServiceFactory::GetForProfile(profile_)) {
    service->SetUserSelectedDefaultSearchProviderByPrepopulateId(
        args[0].GetInt());
  }
}

void KoanWelcomeHandler::HandleSetDefaultBrowser(const base::ListValue& args) {
  base::MakeRefCounted<shell_integration::DefaultBrowserWorker>()
      ->StartSetAsDefault(base::DoNothing());
}

void KoanWelcomeHandler::HandleFinish(const base::ListValue& args) {
  std::vector<GURL> essentials;
  if (!args.empty() && args[0].is_list()) {
    for (const base::Value& url : args[0].GetList()) {
      if (url.is_string()) {
        GURL gurl(url.GetString());
        if (gurl.SchemeIsHTTPOrHTTPS()) {
          essentials.push_back(std::move(gurl));
        }
      }
    }
  }
  if (const KoanWelcomeHost* host = GetHost(web_ui());
      host && host->callbacks().finish) {
    host->callbacks().finish.Run(std::move(essentials));
  }
}

void KoanWelcomeHandler::HandleSkip(const base::ListValue& args) {
  if (const KoanWelcomeHost* host = GetHost(web_ui());
      host && host->callbacks().skip) {
    host->callbacks().skip.Run();
  }
}

}  // namespace koan
