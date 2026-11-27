// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <string_view>

#include "components/os_crypt/async/browser/freedesktop_secret_key_provider.h"
#include "koan/common/koan_branding.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace os_crypt_async {

// Upstream's header befriends this class (and nothing defines it): Koan uses
// it to read the provider's private names. If upstream renames or moves
// them, this file stops compiling.
class FreedesktopSecretKeyProviderCompatTest {
 public:
  static std::string_view KeyName() {
    return FreedesktopSecretKeyProvider::kKeyName;
  }
  static std::string_view AppName() {
    return FreedesktopSecretKeyProvider::kAppName;
  }
  static std::string_view KWalletFolder() {
    return FreedesktopSecretKeyProvider::kKWalletFolder;
  }
};

}  // namespace os_crypt_async

namespace koan {
namespace {

using Provider = os_crypt_async::FreedesktopSecretKeyProviderCompatTest;

// The Secret Service item's label and "application" attribute and the
// KWallet folder and entry are Koan's, not Chromium's (#503).
TEST(KoanLinuxSafeStorageTest, FreedesktopProviderUsesKoansNames) {
  EXPECT_EQ(Provider::KeyName(), kLinuxSecretKeyName);
  EXPECT_EQ(Provider::AppName(), kLinuxSecretAppName);
  EXPECT_EQ(Provider::KWalletFolder(), kLinuxKWalletFolder);

  EXPECT_EQ(Provider::KeyName(), "Koan Safe Storage");
  EXPECT_EQ(Provider::AppName(), "koan");
  EXPECT_EQ(Provider::KWalletFolder(), "Koan Keys");
}

}  // namespace
}  // namespace koan
