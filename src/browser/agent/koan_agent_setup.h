// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_SETUP_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_SETUP_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/values.h"

// Settings > Agent Control > "Set up agents on this Mac"
// (.agent/notes/agent-control.md section 12, design decision 10): how agents
// on the Mac find Koan without being told, as ego lite's installer does with
// its ego-browser skill. One click (never on its own) links ~/.local/bin/koan
// to the `koan` tool inside the running app and writes a small entry skill,
// koan-browser/SKILL.md, into the skills directory of each agent that is
// installed. The skill only says what Koan is and "run `koan skill`"; the
// guide itself comes from the installed `koan`, so it never goes stale. Run
// again after an update (the link follows the app); Remove undoes it. All of
// it blocks on the disk: call it off the UI thread.
namespace koan::agent {

inline constexpr char kSkillName[] = "koan-browser";

// Where an agent keeps its user-level skills, relative to the home
// directory. The agent counts as installed if `marker` exists. Several
// agents may share a directory (~/.agents/skills); it is written once.
struct AgentSkillHome {
  const char* agent;
  const char* marker;
  const char* skills_dir;
};
base::span<const AgentSkillHome> AgentSkillHomes();

// The entry skill's SKILL.md (the repo's skills/koan-browser/SKILL.md is the
// same text, for `npx skills add`; scripts/tests checks they match).
std::string_view EntrySkill();
// Whether `text` is a koan-browser SKILL.md (any version of it): only those
// are replaced or removed, never a file of the user's.
bool IsKoanSkill(std::string_view text);

// Whether a shell startup file puts ~/.local/bin on the PATH, as far as its
// text says (a GUI app's own PATH is not the shell's).
bool MentionsLocalBin(std::string_view shell_rc);

struct AgentSetup {
  struct Agent {
    std::string name;
    base::FilePath skill_file;
    bool installed = false;
    bool set_up = false;
  };
  std::vector<Agent> agents;
  // The `koan` tool in this app, and ~/.local/bin/koan.
  base::FilePath cli;
  base::FilePath link;
  bool cli_exists = false;
  bool linked = false;
  // Whether a shell startup file puts ~/.local/bin on the PATH.
  bool on_path = false;
  // What was left alone or failed, for the page.
  std::vector<std::string> problems;
};

// What is set up now, under `home`, for the tool at `cli`.
AgentSetup ReadAgentSetup(const base::FilePath& home,
                          const base::FilePath& cli);
// Links the tool and writes the skill for each installed agent; then reads.
AgentSetup SetUpAgents(const base::FilePath& home, const base::FilePath& cli);
// Removes what SetUpAgents() wrote (only Koan's skill files and a link to a
// Koan app's tool); then reads.
AgentSetup RemoveAgentSetup(const base::FilePath& home,
                            const base::FilePath& cli);

// {cli, link, cliExists, linked, onPath, agents: [{name, path, installed,
// setUp}], problems: [text]}, with paths under `home` written ~/...
base::DictValue AgentSetupToValue(const AgentSetup& setup,
                                  const base::FilePath& home);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_SETUP_H_
