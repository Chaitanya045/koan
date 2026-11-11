// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_settings_url.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace koan::settings {
namespace {

// The old page's URLs open Koan's panes in chrome://settings.
TEST(KoanSettingsUrlTest, LegacyURLsOpenThePanes) {
  const struct {
    const char* legacy;
    const char* settings;
  } kCases[] = {
      {"chrome://koan-settings", "chrome://settings/koan/looks"},
      {"chrome://koan-settings/", "chrome://settings/koan/looks"},
      {"chrome://koan-settings/shortcuts", "chrome://settings/koan/shortcuts"},
      {"chrome://koan-settings/agents/", "chrome://settings/koan/agents"},
      {"chrome://koan-settings/tabs?x=1#y",
       "chrome://settings/koan/tabs?x=1#y"},
  };
  for (const auto& c : kCases) {
    EXPECT_EQ(SettingsURLForLegacyURL(GURL(c.legacy)), GURL(c.settings))
        << c.legacy;
  }
}

// Every other URL is left alone, chrome://settings included (its sections
// and Koan's are served as they are).
TEST(KoanSettingsUrlTest, OtherURLsAreNotRewritten) {
  for (const char* url :
       {"chrome://settings", "chrome://settings/koan/looks",
        "chrome://settings/appearance", "chrome://koan-welcome/",
        "https://koan-settings/", "chrome-untrusted://koan-settings/"}) {
    EXPECT_EQ(SettingsURLForLegacyURL(GURL(url)), std::nullopt) << url;
  }
}

}  // namespace
}  // namespace koan::settings
