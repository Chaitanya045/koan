// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/updates/koan_update_source.h"

#include <algorithm>
#include <string>

#include "base/command_line.h"
#include "base/logging.h"
#include "koan/browser/updates/buildflags.h"
#include "koan/browser/updates/koan_update_public_key.h"

namespace koan::updates {

bool UpdateTestHooksBuilt() {
  return BUILDFLAG(KOAN_UPDATE_TEST_HOOKS);
}

PublicKey BuiltInPublicKey() {
  PublicKey key;
  std::ranges::copy(kBuiltInPublicKey, key.begin());
  return key;
}

UpdateSource GetUpdateSource(const base::CommandLine& command_line) {
  UpdateSource source{GURL(kManifestUrl), BuiltInPublicKey()};
#if BUILDFLAG(KOAN_UPDATE_TEST_HOOKS)
  if (command_line.HasSwitch(kManifestUrlSwitch)) {
    const GURL url(command_line.GetSwitchValueASCII(kManifestUrlSwitch));
    if (url.is_valid() && url.SchemeIsHTTPOrHTTPS()) {
      source.manifest_url = url;
      source.overridden = true;
    } else {
      LOG(ERROR) << "--" << kManifestUrlSwitch << ": not an http(s) URL";
    }
  }
  if (command_line.HasSwitch(kPublicKeySwitch)) {
    if (const std::optional<PublicKey> key = ParsePublicKey(
            command_line.GetSwitchValueASCII(kPublicKeySwitch))) {
      source.public_key = *key;
      source.overridden = true;
    } else {
      LOG(ERROR) << "--" << kPublicKeySwitch
                 << ": not the base64 of a 32-byte Ed25519 key";
    }
  }
  if (source.overridden) {
    LOG(WARNING) << "Update test hooks: checking " << source.manifest_url
                 << (source.public_key == BuiltInPublicKey()
                         ? " with the built-in key"
                         : " with a test key");
  }
#endif
  return source;
}

}  // namespace koan::updates
