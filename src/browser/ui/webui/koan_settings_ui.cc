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
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/fonts/koan_fonts.h"
#include "koan/browser/ui/webui/koan_settings_handler.h"
#include "koan/browser/ui/webui/koan_settings_resources.h"
#include "koan/browser/ui/webui/koan_settings_rows.h"

#if BUILDFLAG(IS_POSIX)
#include "koan/browser/ui/webui/koan_agent_containers_handler.h"
#endif

namespace koan::settings {

namespace {

// The plugin's files, built into the binary by embed_resources.py (no grit
// or .pak changes upstream), served from chrome://settings by these exact
// names: every other path is Chromium's (its files and its routes, such as
// koan/looks).
enum class File { kJs, kCss, kTokens, kSans, kMono };
constexpr auto kFiles = base::MakeFixedFlatMap<std::string_view, File>({
    {"koan_settings.js", File::kJs},
    {"koan_settings.css", File::kCss},
    {"koan_tokens.css", File::kTokens},
    {"koan_geist.woff2", File::kSans},
    {"koan_geist_mono.woff2", File::kMono},
});

// The request's file, without the query: the page asks for the tokens again
// with "?<time>" when the look changes, to get past the cache.
std::string_view FileName(std::string_view request_path) {
  return request_path.substr(0, request_path.find_first_of("?#"));
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
  const look::Look& current = look::CurrentLook();
  if (current.style != look::Style::kShadcn) {
    return std::string();
  }
  const auto block = [](bool dark) {
    return base::StrCat(
        {":root, :host {\n  color-scheme: ", dark ? "dark" : "light", ";\n",
         look::TokensToCss(look::CurrentTokens(dark)),
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
  web_ui.AddMessageHandler(std::make_unique<KoanSettingsHandler>(
      *profile->GetPrefs(), *g_browser_process->local_state()));
#if BUILDFLAG(IS_POSIX)
  web_ui.AddMessageHandler(
      std::make_unique<KoanAgentContainersHandler>(*profile));
#endif
}

}  // namespace koan::settings
