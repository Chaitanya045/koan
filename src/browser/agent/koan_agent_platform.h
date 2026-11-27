// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_PLATFORM_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_PLATFORM_H_

#include "build/build_config.h"

// How agent control names the machine it runs on, in the words the user and
// the agents read (settings, the CLI's help and guide, errors): "this Mac" on
// the Mac, as before; "this computer" on Linux (issue #489). And whether the
// browser runs in a Flatpak, which changes what it can reach.
namespace koan::agent {

#if BUILDFLAG(IS_MAC)
inline constexpr char kThisMachine[] = "this Mac";
inline constexpr char kTheMachineThatRunsKoan[] = "the Mac that runs Koan";
#else
inline constexpr char kThisMachine[] = "this computer";
inline constexpr char kTheMachineThatRunsKoan[] = "the computer that runs Koan";
#endif

// Whether this process runs in a Flatpak sandbox (it has /.flatpak-info);
// false off Linux. One stat, the first time.
bool RunsInFlatpak();

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_PLATFORM_H_
