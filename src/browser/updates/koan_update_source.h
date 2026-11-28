// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UPDATES_KOAN_UPDATE_SOURCE_H_
#define KOAN_BROWSER_UPDATES_KOAN_UPDATE_SOURCE_H_

#include "koan/browser/updates/koan_update_logic.h"
#include "url/gurl.h"

namespace base {
class CommandLine;
}

// Where updates come from and the key they must be signed with.
//
// A build people install trusts only the key built in from
// koan_update_public_key.txt and only kManifestUrl. For development and
// tests, a build made with the GN arg koan_update_test_hooks (BUILD.gn; off by
// default, and never on in what release.yml or the nightlies build) also
// takes two switches:
//
//   --koan-update-manifest-url=<http(s) URL>  fetch this manifest instead
//   --koan-update-public-key=<base64>         trust this key instead
//
// Without that arg the switches aren't compiled in. Either way the release
// URL checks (IsReleaseDownloadUrl(), IsReleasesPageUrl()) stay as they are.
namespace koan::updates {

inline constexpr char kManifestUrlSwitch[] = "koan-update-manifest-url";
inline constexpr char kPublicKeySwitch[] = "koan-update-public-key";

struct UpdateSource {
  GURL manifest_url;
  PublicKey public_key = {};
  // Whether a switch changed either (test hook builds only).
  bool overridden = false;
};

// Whether this build was made with koan_update_test_hooks.
bool UpdateTestHooksBuilt();

// The key built in from koan_update_public_key.txt.
PublicKey BuiltInPublicKey();

// kManifestUrl and BuiltInPublicKey(), unless test hooks are built in and
// `command_line` overrides them. A switch with a bad value is ignored
// (logged), so the real source stays.
UpdateSource GetUpdateSource(const base::CommandLine& command_line);

}  // namespace koan::updates

#endif  // KOAN_BROWSER_UPDATES_KOAN_UPDATE_SOURCE_H_
