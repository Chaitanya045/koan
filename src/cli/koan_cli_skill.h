// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_SKILL_H_
#define KOAN_CLI_KOAN_CLI_SKILL_H_

#include <string>

namespace koan::cli {

// The guide `koan skill` prints (.agent/notes/agent-control.md section 12):
// written from this version's commands, so the installed skill, which only
// says "run koan skill", is never stale.
std::string SkillGuide();

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_CLI_SKILL_H_
