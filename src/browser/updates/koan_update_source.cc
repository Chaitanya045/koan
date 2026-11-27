// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/updates/koan_update_source.h"

#include <algorithm>
#include <string>

#include "base/command_line.h"
#include "base/logging.h"
#include "koan/browser/updates/buildflags.h"
#include "koan/browser/updates/koan_update_install_logic.h"
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
  UpdateSource source;
  source.manifest_url = GURL(kManifestUrl);
  source.public_key = BuiltInPublicKey();
  source.code_requirement = ReleaseCodeRequirement();
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
  if (command_line.HasSwitch(kDownloadUrlSwitch)) {
    const GURL url(command_line.GetSwitchValueASCII(kDownloadUrlSwitch));
    if (url.is_valid() && url.SchemeIsHTTPOrHTTPS()) {
      source.download_url = url;
      source.overridden = true;
    } else {
      LOG(ERROR) << "--" << kDownloadUrlSwitch << ": not an http(s) URL";
    }
  }
  if (command_line.HasSwitch(kCodeRequirementSwitch)) {
    const std::string requirement =
        command_line.GetSwitchValueASCII(kCodeRequirementSwitch);
    if (!requirement.empty() && requirement[0] != '=') {
      source.code_requirement = "=" + requirement;
      source.overridden = true;
    } else {
      LOG(ERROR) << "--" << kCodeRequirementSwitch
                 << ": not a requirement's text";
    }
  }
  if (source.overridden) {
    LOG(WARNING) << "Update test hooks: checking " << source.manifest_url
                 << (source.public_key == BuiltInPublicKey()
                         ? " with the built-in key"
                         : " with a test key")
                 << (source.download_url.is_empty()
                         ? std::string()
                         : ", downloading " + source.download_url.spec())
                 << ", requiring " << source.code_requirement;
  }
#endif
  return source;
}

}  // namespace koan::updates
