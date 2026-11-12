// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_settings_ui.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/containers/fixed_flat_map.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "koan/browser/importer/koan_import_sources.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/color/koan_tokens.h"
#include "koan/browser/ui/fonts/koan_fonts.h"
#include "koan/browser/ui/webui/koan_settings_handler.h"
#include "koan/browser/ui/webui/koan_settings_resources.h"
#include "koan/browser/ui/webui/koan_settings_rows.h"
#include "third_party/skia/include/core/SkColor.h"

#if BUILDFLAG(IS_POSIX)
#include "koan/browser/ui/webui/koan_agent_containers_handler.h"
#endif

namespace koan::settings {

namespace {

// The plugin's files, built into the binary by embed_resources.py (no grit
// or .pak changes upstream), served from chrome://settings by these exact
// names: every other path is Chromium's (its files and its routes, such as
// koan/looks).
enum class File { kJs, kCss, kTokens, kChromium, kSans, kMono };
constexpr auto kFiles = base::MakeFixedFlatMap<std::string_view, File>({
    {"koan_settings_page.js", File::kJs},
    {"koan_settings.css", File::kCss},
    {"koan_tokens.css", File::kTokens},
    {"koan_chromium.css", File::kChromium},
    {"koan_geist.woff2", File::kSans},
    {"koan_geist_mono.woff2", File::kMono},
});

// The request's file, without the query: the page asks for the tokens again
// with "?<time>" when the look changes, to get past the cache.
std::string_view FileName(std::string_view request_path) {
  return request_path.substr(0, request_path.find_first_of("?#"));
}

// What shadcn's components change in dark mode (their `dark:` classes), for
// koan_chromium.css: fields and outline buttons over a tint of --input, and
// the Switch's thumb.
std::string DarkVariants(bool dark) {
  if (dark) {
    return "--koan-field-background: color-mix(in srgb, var(--input) 30%, "
           "transparent);\n"
           "--koan-field-hover: color-mix(in srgb, var(--input) 50%, "
           "transparent);\n"
           "--koan-outline-border: var(--input);\n"
           "--koan-outline-background: var(--koan-field-background);\n"
           "--koan-outline-hover: var(--koan-field-hover);\n"
           "--koan-switch-track: color-mix(in srgb, var(--input) 80%, "
           "transparent);\n"
           "--koan-switch-thumb: var(--foreground);\n"
           "--koan-switch-thumb-checked: var(--primary-foreground);\n";
  }
  return "--koan-field-background: transparent;\n"
         "--koan-field-hover: var(--accent);\n"
         "--koan-outline-border: var(--border);\n"
         "--koan-outline-background: var(--background);\n"
         "--koan-outline-hover: var(--accent);\n"
         "--koan-switch-track: var(--input);\n"
         "--koan-switch-thumb: var(--background);\n"
         "--koan-switch-thumb-checked: var(--background);\n";
}

// Lucide's chevron-down in the muted text colour at half opacity (shadcn's
// SelectTrigger), as an image Chromium's drop-downs can draw: a background
// image can't take its colour from CSS.
std::string Chevron(const look::TokenSet& tokens) {
  const SkColor color = tokens[look::Token::kMutedForeground];
  return base::StringPrintf(
      "--koan-chevron: url(\"data:image/svg+xml,<svg "
      "xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
      "stroke='%%23%02x%02x%02x' stroke-opacity='0.5' stroke-width='2' "
      "stroke-linecap='round' stroke-linejoin='round'><path d='m6 9 6 6 "
      "6-6'/></svg>\");\n",
      SkColorGetR(color), SkColorGetG(color), SkColorGetB(color));
}

// The look's stylesheet draws Koan's panes from the same tokens as the native
// UI (koan::look::TokensToCss), so the two never differ. In the Zen style it
// is empty: the panes keep their own Zen values and Chromium's sections their
// own look. It is rebuilt on every request; the panes ask again when a look
// pref changes.
//
// It is loaded twice: in the document (the fonts, and the tokens over
// Chromium's own variables, so its sections take the look too) and in the
// panes' shadow root (where `:host` overrides the panes' Zen defaults).
std::string LookStylesheet() {
  if (!look::IsShadcnStyle()) {
    return std::string();
  }
  const look::Look& current = look::CurrentLook();
  const auto block = [](bool dark) {
    const look::TokenSet& tokens = look::CurrentTokens(dark);
    return base::StrCat(
        {":root, :host {\n  color-scheme: ", dark ? "dark" : "light", ";\n",
         look::TokensToCss(tokens), DarkVariants(dark), Chevron(tokens),
         // Hover surface and the field border the page derives from tokens.
         "--secondary-hover: color-mix(in srgb, var(--secondary), "
         "var(--foreground) 8%);\n"
         "--field-border: var(--input);\n"
         "--r-item: calc(var(--radius) - 2px);\n"
         "--r-field: calc(var(--radius) - 2px);\n"
         "--r-button: calc(var(--radius) - 2px);\n"
         "--r-card: calc(var(--radius) + 4px);\n"
         // shadcn's Select: a bordered, transparent field.
         "--select-background: transparent;\n"
         "--select-border: var(--input);\n"
         "--select-font-weight: 400;\n}\n"});
  };
  std::string css = base::StrCat(
      {"@font-face { font-family: 'Geist'; src: url(/koan_geist.woff2) "
       "format('woff2'); font-weight: 100 900; font-display: swap; }\n"
       "@font-face { font-family: 'Geist Mono'; src: "
       "url(/koan_geist_mono.woff2) "
       "format('woff2'); font-display: swap; }\n"});
  switch (current.mode) {
    case look::Mode::kDark:
      css += block(true);
      break;
    case look::Mode::kLight:
      css += block(false);
      break;
    case look::Mode::kAuto:
      css += block(false);
      css += "@media (prefers-color-scheme: dark) {\n" + block(true) + "}\n";
      break;
  }
  css +=
      "body, :host { font-family: 'Geist', Inter, system-ui, sans-serif; "
      "font-size: 14px; }\n"
      "kbd, code, .shortcut-input { font-family: 'Geist Mono', ui-monospace, "
      "monospace; }\n"
      // Chromium's sections: its variables (cr_shared_vars.css) from the
      // tokens. Colours it takes straight from chrome://theme/colors.css
      // come from Koan's colour mixers already.
      ":root:root {\n"
      "  --cr-primary-text-color: var(--foreground);\n"
      "  --cr-secondary-text-color: var(--muted-foreground);\n"
      "  --cr-card-background-color: var(--card);\n"
      "  --cr-card-border-radius: var(--r-card);\n"
      "  --cr-separator-color: var(--border);\n"
      "  --cr-scrollable-border-color: var(--border);\n"
      "  --cr-link-color: var(--primary);\n"
      "  --cr-checked-color: var(--primary);\n"
      "  --cr-focus-outline-color: var(--ring);\n"
      "  --cr-hover-background-color: var(--accent);\n"
      "  --cr-menu-background-color: var(--popover);\n"
      // shadcn's popover shadow (shadow-md).
      "  --cr-menu-shadow: 0 4px 6px -1px rgb(0 0 0 / 0.1), "
      "0 2px 4px -2px rgb(0 0 0 / 0.1);\n"
      // Cards: a 1px border (as a ring, which every card of the page draws
      // with) and no elevation (#301).
      "  --cr-card-shadow: 0 0 0 1px var(--border);\n"
      "  --cr-title-text-color: var(--foreground);\n"
      "  --cr-form-field-label-color: var(--muted-foreground);\n"
      "  --cr-active-background-color: var(--accent);\n"
      "  --cr-hover-on-subtle-background-color: var(--accent);\n"
      "  --iron-icon-fill-color: var(--muted-foreground);\n"
      "  --settings-error-color: var(--destructive);\n"
      // The left menu's selected and hovered item: shadcn's sidebar menu
      // item (settings_menu.css.patch), not Chromium's light blue.
      "  --koan-nav-item-selected-background: var(--sidebar-accent);\n"
      "  --koan-nav-item-selected-color: var(--sidebar-accent-foreground);\n"
      "  --koan-nav-item-hover-background: var(--sidebar-accent);\n"
      "  background: var(--background);\n}\n";
  return css;
}

void ServeFile(const std::string& request_path,
               content::WebUIDataSource::GotDataCallback callback) {
  std::string data;
  switch (kFiles.at(FileName(request_path))) {
    case File::kJs:
      data = kKoanSettingsJs;
      break;
    case File::kCss:
      data = kKoanSettingsCss;
      break;
    case File::kTokens:
      data = LookStylesheet();
      break;
    case File::kChromium:
      // Chromium's sections keep their own look in the Zen style.
      if (look::IsShadcnStyle()) {
        data = kKoanChromiumCss;
      }
      break;
    case File::kSans:
      data = std::string(fonts::SansWoff2());
      break;
    case File::kMono:
      data = std::string(fonts::MonoWoff2());
      break;
  }
  std::move(callback).Run(
      base::MakeRefCounted<base::RefCountedString>(std::move(data)));
}

}  // namespace

void ExtendSettingsUI(content::WebUI& web_ui,
                      content::WebUIDataSource& source) {
  source.SetRequestFilter(base::BindRepeating([](const std::string& path) {
                            return kFiles.contains(FileName(path));
                          }),
                          base::BindRepeating(&ServeFile));
  // The menu items' labels (settings_menu.html: $i18n{koanPaneLooks} ...).
  for (const Pane& pane : Panes()) {
    std::string id = pane.id;
    id[0] = base::ToUpperASCII(id[0]);
    source.AddString(base::StrCat({"koanPane", id}), pane.title);
  }
  // Koan's settings are the regular profile's (chrome://settings never opens
  // in an incognito one).
  Profile* profile = Profile::FromWebUI(&web_ui)->GetOriginalProfile();
  // Koan has no Google account (browser sign-in is off by default,
  // koan_prefs.cc): the People section and its menu item are the profile's,
  // not "You and Google" (IDS_SETTINGS_PEOPLE, which this replaces: the
  // localized strings are added before this hook runs). Zen has no browser
  // account rows either (#300): "Allow Chrome sign-in" goes (it is the only
  // row `signinAvailable` shows), and the subpage that held it is "Google
  // services", since sync can't be set up. That subpage stays: it has
  // "Autocomplete searches and URLs" and "Make searches and browsing better".
  if (!profile->GetPrefs()->GetBoolean(::prefs::kSigninAllowed)) {
    source.AddString("peoplePageTitle", "Profile");
    source.AddBoolean("signinAvailable", false);
    source.AddString("syncAndNonPersonalizedServices", "Google services");
    source.AddString("syncPageTitle", "Google services");
  }
  // Zen turns AI controls off and deletes its AI rows (Z28): no AI section,
  // menu item or route, and no link to it from the Privacy Guide. Its
  // features are off already (koan_feature_overrides.cc).
  source.AddBoolean("showAiPage", false);
  // Appearance (page_visibility.ts patch): no Theme row, since Koan's look is
  // its Look and Feel and Chrome themes and Customize Chrome are what Zen
  // hides (its browser theme group); and no Show home button row while the
  // URL bar replaces the new tab page, the rule for Zen's Home pane
  // (zen-preferences.css).
  source.AddBoolean("koanShowHomeButtonRow", !profile->GetPrefs()->GetBoolean(
                                                 prefs::kUrlBarReplaceNewTab));
  // The import dialog's Koan checkbox (Zen's migration-zen-spaces-option-
  // label).
  source.AddString("koanImportSpaces", "Spaces and pinned tabs");
  web_ui.AddMessageHandler(std::make_unique<KoanSettingsHandler>(
      *profile->GetPrefs(), *g_browser_process->local_state()));
#if BUILDFLAG(IS_POSIX)
  web_ui.AddMessageHandler(
      std::make_unique<KoanAgentContainersHandler>(*profile));
#endif
}

void AddImportDialogItems(uint16_t services_supported,
                          base::DictValue& browser_profile) {
  browser_profile.Set("koanSpaces",
                      (services_supported & importer::kSpacesImportItem) != 0);
}

uint16_t GetImportDialogItems(const base::DictValue& types) {
  return types.FindBool(prefs::kImportDialogSpaces).value_or(false)
             ? importer::kSpacesImportItem
             : 0;
}

}  // namespace koan::settings
