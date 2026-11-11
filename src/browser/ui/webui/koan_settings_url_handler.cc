// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <utility>

#include "content/public/browser/browser_context.h"
#include "koan/browser/ui/webui/koan_settings_url.h"
#include "url/gurl.h"

namespace koan::settings {

bool RewriteLegacySettingsURL(GURL* url,
                              content::BrowserContext* browser_context) {
  if (browser_context->IsOffTheRecord()) {
    return false;
  }
  std::optional<GURL> settings_url = SettingsURLForLegacyURL(*url);
  if (!settings_url) {
    return false;
  }
  *url = std::move(*settings_url);
  return true;
}

bool ReverseLegacySettingsURL(GURL* url,
                              content::BrowserContext* browser_context) {
  // Nothing to rewrite back: returning true makes the URL bar show the page
  // that opened, chrome://settings/koan/<pane>.
  return IsSettingsURL(*url);
}

}  // namespace koan::settings
