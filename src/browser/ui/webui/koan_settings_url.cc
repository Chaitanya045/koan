// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_settings_url.h"

#include <string>
#include <string_view>

#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "koan/common/koan_url_constants.h"
#include "url/gurl.h"

namespace koan::settings {

namespace {

// chrome://settings, from Koan's own constant: chrome::kChromeUISettingsHost
// is in //chrome/common:url_constants, which only //chrome/common may depend
// on, and this target links into koan_unittests without //chrome or
// //content. KoanSettingsUrlTest checks it is chrome://settings.
const GURL& SettingsPagesURL() {
  static const base::NoDestructor<GURL> url(kSettingsURL);
  return *url;
}

}  // namespace

bool IsSettingsURL(const GURL& url) {
  return url.SchemeIs(SettingsPagesURL().scheme()) &&
         url.host() == SettingsPagesURL().host();
}

std::optional<GURL> SettingsURLForLegacyURL(const GURL& url) {
  if (!url.SchemeIs(SettingsPagesURL().scheme()) ||
      url.host() != kLegacySettingsHost) {
    return std::nullopt;
  }
  std::string_view pane = url.path();
  while (!pane.empty() && pane.front() == '/') {
    pane.remove_prefix(1);
  }
  while (!pane.empty() && pane.back() == '/') {
    pane.remove_suffix(1);
  }
  const std::string path = pane.empty() ? std::string(SettingsPagesURL().path())
                                        : base::StrCat({"/koan/", pane});
  GURL::Replacements replacements;
  replacements.SetHostStr(SettingsPagesURL().host());
  replacements.SetPathStr(path);
  return url.ReplaceComponents(replacements);
}

}  // namespace koan::settings
