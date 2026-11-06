// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_welcome_host.h"

#include <utility>

namespace koan {

const WelcomeEssential* FindWelcomeEssential(const GURL& url) {
  for (const WelcomeEssential& essential : kWelcomeEssentials) {
    if (GURL(essential.url) == url) {
      return &essential;
    }
  }
  return nullptr;
}

KoanWelcomeHost::Callbacks::Callbacks() = default;
KoanWelcomeHost::Callbacks::Callbacks(Callbacks&&) = default;
KoanWelcomeHost::Callbacks& KoanWelcomeHost::Callbacks::operator=(Callbacks&&) =
    default;
KoanWelcomeHost::Callbacks::~Callbacks() = default;

KoanWelcomeHost::KoanWelcomeHost(content::WebContents* contents,
                                 Callbacks callbacks)
    : content::WebContentsUserData<KoanWelcomeHost>(*contents),
      callbacks_(std::move(callbacks)) {}

KoanWelcomeHost::~KoanWelcomeHost() = default;

WEB_CONTENTS_USER_DATA_KEY_IMPL(KoanWelcomeHost);

}  // namespace koan
