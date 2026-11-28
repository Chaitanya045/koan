// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_SETUP_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_SETUP_H_

#include <optional>
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
//
// Linux ("this computer"): a Koan in a Flatpak cannot write to the home
// folder (a permission to write ~/.local/bin or an agent's folder would let a
// compromised browser out of its sandbox), so Settings shows a command to run
// once in a terminal instead, `<the Flatpak's koan> setup`, and the same code
// runs there, on the host. The link points at the Flatpak's path that follows
// its updates (StableFlatpakPath()).
namespace koan::agent {

inline constexpr char kSkillName[] = "koan-browser";
inline constexpr char kFlatpakAppId[] = "dev.koan.Koan";

// Linux: a path inside one deployment of Koan's Flatpak,
// <installation>/app/dev.koan.Koan/<arch>/<branch>/<commit>/files/..., as the
// path that follows the installed version, <installation>/app/dev.koan.Koan/
// current/active/files/... (Flatpak keeps both links current); nullopt for
// any other path.
std::optional<base::FilePath> StableFlatpakPath(const base::FilePath& path);
// The path to link ~/.local/bin/koan to for the tool at `tool`.
base::FilePath ToolToLink(const base::FilePath& tool);
// The app-path of [Instance] in a sandbox's /.flatpak-info: where the app's
// files (/app inside) are on the host.
std::optional<base::FilePath> FlatpakAppPath(std::string_view flatpak_info);
// `path` as one word for a POSIX shell.
std::string ShellQuoted(const base::FilePath& path);

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
