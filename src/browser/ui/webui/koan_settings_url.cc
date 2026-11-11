// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_settings_url.h"

#include <string>
#include <string_view>

#include "base/strings/strcat.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/common/url_constants.h"
#include "koan/common/koan_url_constants.h"
#include "url/gurl.h"

namespace koan::settings {

std::optional<GURL> SettingsURLForLegacyURL(const GURL& url) {
  if (!url.SchemeIs(content::kChromeUIScheme) ||
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
  const GURL first_pane(kSettingsURL);
  const std::string path = pane.empty() ? std::string(first_pane.path())
                                        : base::StrCat({"/koan/", pane});
  GURL::Replacements replacements;
  replacements.SetHostStr(chrome::kChromeUISettingsHost);
  replacements.SetPathStr(path);
  return url.ReplaceComponents(replacements);
}

}  // namespace koan::settings
