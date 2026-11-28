// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_platform.h"

#if BUILDFLAG(IS_LINUX)
#include <unistd.h>
#endif

namespace koan::agent {

bool RunsInFlatpak() {
#if BUILDFLAG(IS_LINUX)
  // Every Flatpak sandbox has it; a program outside one does not. One stat
  // of a file in /, once: cheap enough for any thread.
  static const bool in_flatpak = access("/.flatpak-info", F_OK) == 0;
  return in_flatpak;
#else
  return false;
#endif
}

}  // namespace koan::agent
