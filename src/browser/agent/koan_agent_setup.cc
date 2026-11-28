// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_setup.h"

#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"

namespace koan::agent {

namespace {

// From each agent's public documentation (2026-11-19): Claude Code reads
// ~/.claude/skills; Codex reads ~/.agents/skills (its older ~/.codex/skills
// is not written, so that a Codex that reads both does not list the skill
// twice); Cursor ~/.cursor/skills; ~/.agents/skills is also the shared
// directory other agents (Amp, Cline, Pi, ...) and `npx skills -g` use.
constexpr AgentSkillHome kAgentSkillHomes[] = {
    {"Claude Code", ".claude", ".claude/skills"},
    {"Codex", ".codex", ".agents/skills"},
    {"Cursor", ".cursor", ".cursor/skills"},
    {"Other agents (~/.agents)", ".agents", ".agents/skills"},
};

#if BUILDFLAG(IS_MAC)
constexpr char kEntrySkill[] = R"(---
name: koan-browser
description: Use Koan, the user's own web browser on their Mac, through the `koan` command. Open tabs of your own that use the user's logins, read and act on pages, take over a tab the user gives you. Use when a task needs a real, signed-in browser, or the user mentions Koan or their browser.
---

# Koan, the user's browser

Koan is the user's web browser. Agents use it with the `koan` command,
never with AppleScript or a remote debugging port.

Before any browser task, run `koan skill` and follow the guide it prints.
It comes from the installed Koan, so it is always current. Its last line is
`[koan:skill] end of guide v<N>`: if that is missing, the output was cut,
so read it again. A `[koan:notice]` line means this `koan` and the running
Koan disagree on versions: tell the user. `koan help <command>` gives a
command's exact options.

The loop that takes the fewest turns: `koan snapshot <tab> --interactive`,
then one `koan act <tab> <step> <step> ... --until <condition> --diff` for
a whole page of a form, then read what it printed (not just "ok") before
the next batch.

If `koan` is not found, use its full path:
`~/.local/bin/koan`, `~/Applications/Koan.app/Contents/Resources/bin/koan`
or `/Applications/Koan.app/Contents/Resources/bin/koan`. On another machine,
run it on the user's Mac over ssh, by that full path, and reuse one ssh
connection for every command (`ssh -o ControlMaster=auto -o
ControlPath=/tmp/koan-ssh-%C -o ControlPersist=10m <mac> ...`) or send them
all through one `ssh <mac> <koan path> shell`.
)";

#else
// Linux: the same guide for a computer, where Koan is a Flatpak.
constexpr char kEntrySkillLinux[] = R"(---
name: koan-browser
description: Use Koan, the user's own web browser on their computer, through the `koan` command. Open tabs of your own that use the user's logins, read and act on pages, take over a tab the user gives you. Use when a task needs a real, signed-in browser, or the user mentions Koan or their browser.
---

# Koan, the user's browser

Koan is the user's web browser. Agents use it with the `koan` command,
never with input tools (xdotool, ydotool) or a remote debugging port.

Before any browser task, run `koan skill` and follow the guide it prints.
It comes from the installed Koan, so it is always current. Its last line is
`[koan:skill] end of guide v<N>`: if that is missing, the output was cut,
so read it again. A `[koan:notice]` line means this `koan` and the running
Koan disagree on versions: tell the user. `koan help <command>` gives a
command's exact options.

The loop that takes the fewest turns: `koan snapshot <tab> --interactive`,
then one `koan act <tab> <step> <step> ... --until <condition> --diff` for
a whole page of a form, then read what it printed (not just "ok") before
the next batch.

If `koan` is not found, use its full path: `~/.local/bin/koan`, or the one
in Koan's Flatpak,
`~/.local/share/flatpak/app/dev.koan.Koan/current/active/files/koan/koan`
or `/var/lib/flatpak/app/dev.koan.Koan/current/active/files/koan/koan`.
Run it on the host, not inside another sandbox. On another machine, run it
on the user's computer over ssh, by that full path, and reuse one ssh
connection for every command (`ssh -o ControlMaster=auto -o
ControlPath=/tmp/koan-ssh-%C -o ControlPersist=10m <host> ...`) or send
them all through one `ssh <host> <koan path> shell`.
)";
#endif

#if BUILDFLAG(IS_MAC)
constexpr char kToolInApp[] = "Contents/Resources/bin/koan";
#else
// /app/koan/koan in Koan's Flatpak, seen from the host.
constexpr char kToolInApp[] = "files/koan/koan";
#endif

base::FilePath SkillFile(const base::FilePath& home,
                         const AgentSkillHome& entry) {
  return home.Append(entry.skills_dir).Append(kSkillName).Append("SKILL.md");
}

// The skill file there is Koan's: a real file (a link is someone else's,
// e.g. `npx skills` links agents' directories to ~/.agents/skills) with
// Koan's skill in it.
bool IsOwnSkillFile(const base::FilePath& file) {
  std::string text;
  return !base::IsLink(file) && !base::IsLink(file.DirName()) &&
         base::ReadFileToStringWithMaxSize(file, &text, 64 * 1024) &&
         IsKoanSkill(text);
}

bool IsKoanSkillFile(const base::FilePath& file) {
  std::string text;
  return base::ReadFileToStringWithMaxSize(file, &text, 64 * 1024) &&
         IsKoanSkill(text);
}

// A link to some Koan app's tool (this one, or an older copy elsewhere).
bool IsKoanToolLink(const base::FilePath& link) {
  base::FilePath target;
  return base::ReadSymbolicLink(link, &target) &&
         target.value().ends_with(std::string("/") + kToolInApp);
}

std::string Display(const base::FilePath& path, const base::FilePath& home) {
  base::FilePath relative;
  if (home.AppendRelativePath(path, &relative)) {
    return "~/" + relative.value();
  }
  return path.value();
}

}  // namespace

base::span<const AgentSkillHome> AgentSkillHomes() {
  return kAgentSkillHomes;
}

std::string_view EntrySkill() {
#if BUILDFLAG(IS_MAC)
  return kEntrySkill;
#else
  return kEntrySkillLinux;
#endif
}

std::optional<base::FilePath> StableFlatpakPath(const base::FilePath& path) {
  const std::vector<std::string> parts = path.GetComponents();
  for (size_t i = 0; i + 5 < parts.size(); ++i) {
    // <installation>/app/dev.koan.Koan/<arch>/<branch>/<commit>/files/...
    if (parts[i] != "app" || parts[i + 1] != kFlatpakAppId ||
        parts[i + 5] != "files") {
      continue;
    }
    // <arch>/<branch>/<commit> becomes current/active.
    base::FilePath stable(parts[0]);
    for (size_t j = 1; j < parts.size(); ++j) {
      if (j == i + 2) {
        stable = stable.Append("current").Append("active");
      } else if (j != i + 3 && j != i + 4) {
        stable = stable.Append(parts[j]);
      }
    }
    return stable;
  }
  return std::nullopt;
}

base::FilePath ToolToLink(const base::FilePath& tool) {
  return StableFlatpakPath(tool).value_or(tool);
}

std::optional<base::FilePath> FlatpakAppPath(std::string_view flatpak_info) {
  bool in_instance = false;
  for (std::string_view line :
       base::SplitStringPiece(flatpak_info, "\n", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (line.starts_with("[")) {
      in_instance = line == "[Instance]";
      continue;
    }
    constexpr std::string_view kKey = "app-path=";
    if (in_instance && line.starts_with(kKey)) {
      const base::FilePath path(line.substr(kKey.size()));
      if (path.IsAbsolute() && !path.ReferencesParent()) {
        return path;
      }
      return std::nullopt;
    }
  }
  return std::nullopt;
}

std::string ShellQuoted(const base::FilePath& path) {
  const std::string& text = path.value();
  if (!text.empty() &&
      text.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUV"
                             "WXYZ0123456789/._-+") == std::string::npos) {
    return text;
  }
  std::string quoted = "'";
  for (char c : text) {
    quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
  }
  return quoted + "'";
}

bool IsKoanSkill(std::string_view text) {
  if (!text.starts_with("---\n")) {
    return false;
  }
  const size_t end = text.find("\n---", 3);
  return end != std::string_view::npos &&
         text.substr(0, end + 1).find("\nname: koan-browser\n") !=
             std::string_view::npos;
}

bool MentionsLocalBin(std::string_view shell_rc) {
  for (std::string_view line : base::SplitStringPiece(
           shell_rc, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (!line.starts_with("#") && line.find(".local/bin") != line.npos &&
        (line.find("PATH") != line.npos || line.find("path") != line.npos)) {
      return true;
    }
  }
  return false;
}

AgentSetup ReadAgentSetup(const base::FilePath& home,
                          const base::FilePath& cli) {
  AgentSetup setup;
  setup.cli = cli;
  setup.link = home.Append(".local").Append("bin").Append("koan");
  setup.cli_exists = base::PathExists(cli);
  base::FilePath target;
  setup.linked = base::ReadSymbolicLink(setup.link, &target) && target == cli;
  for (const char* rc : {".zshrc", ".zprofile", ".zshenv", ".bash_profile",
                         ".bashrc", ".profile", ".config/fish/config.fish"}) {
    std::string text;
    if (base::ReadFileToStringWithMaxSize(home.Append(rc), &text,
                                          1024 * 1024) &&
        MentionsLocalBin(text)) {
      setup.on_path = true;
      break;
    }
  }
  for (const AgentSkillHome& entry : AgentSkillHomes()) {
    AgentSetup::Agent agent;
    agent.name = entry.agent;
    agent.skill_file = SkillFile(home, entry);
    agent.installed = base::DirectoryExists(home.Append(entry.marker));
    // Through links too: a skill `npx skills` installed counts.
    agent.set_up = IsKoanSkillFile(agent.skill_file);
    setup.agents.push_back(std::move(agent));
  }
  return setup;
}

AgentSetup SetUpAgents(const base::FilePath& home, const base::FilePath& cli) {
  std::vector<std::string> problems;
  // The link: made, or moved to this app (after an update or a move); never
  // over a file or a link that is not Koan's.
  const base::FilePath link =
      home.Append(".local").Append("bin").Append("koan");
  base::FilePath target;
  const bool is_link = base::ReadSymbolicLink(link, &target);
  if (!base::PathExists(cli)) {
    problems.push_back("This build of Koan has no koan tool to link.");
  } else if (is_link && target == cli) {
    // Already there.
  } else if ((is_link && !IsKoanToolLink(link)) ||
             (!is_link && base::PathExists(link))) {
    problems.push_back(Display(link, home) +
                       " is another program; Koan left it alone.");
  } else {
    const base::FilePath fresh = link.AddExtension("koan-new");
    base::DeleteFile(fresh);
    if (!base::CreateDirectory(link.DirName()) ||
        !base::CreateSymbolicLink(cli, fresh) || !base::Move(fresh, link)) {
      base::DeleteFile(fresh);
      problems.push_back("Could not link " + Display(link, home) + ".");
    }
  }
  // The skill, once per directory, for each agent that is installed.
  std::set<base::FilePath> written;
  for (const AgentSkillHome& entry : AgentSkillHomes()) {
    const base::FilePath file = SkillFile(home, entry);
    if (!base::DirectoryExists(home.Append(entry.marker)) ||
        !written.insert(file).second) {
      continue;
    }
    if (base::PathExists(file) && !IsOwnSkillFile(file)) {
      if (!IsKoanSkillFile(file)) {
        problems.push_back(Display(file, home) +
                           " is not Koan's; Koan left it alone.");
      }
      // Else Koan's skill, installed another way (a link): left as it is.
      continue;
    }
    if (base::IsLink(file.DirName()) ||
        !base::CreateDirectory(file.DirName()) ||
        !base::ImportantFileWriter::WriteFileAtomically(file, EntrySkill())) {
      problems.push_back("Could not write " + Display(file, home) + ".");
    }
  }
  AgentSetup setup = ReadAgentSetup(home, cli);
  setup.problems = std::move(problems);
  return setup;
}

AgentSetup RemoveAgentSetup(const base::FilePath& home,
                            const base::FilePath& cli) {
  std::vector<std::string> problems;
  const base::FilePath link =
      home.Append(".local").Append("bin").Append("koan");
  base::FilePath target;
  if (base::ReadSymbolicLink(link, &target) &&
      (target == cli || IsKoanToolLink(link)) && !base::DeleteFile(link)) {
    problems.push_back("Could not remove " + Display(link, home) + ".");
  }
  std::set<base::FilePath> removed;
  for (const AgentSkillHome& entry : AgentSkillHomes()) {
    const base::FilePath file = SkillFile(home, entry);
    if (!removed.insert(file).second || !IsOwnSkillFile(file)) {
      continue;
    }
    if (!base::DeleteFile(file)) {
      problems.push_back("Could not remove " + Display(file, home) + ".");
      continue;
    }
    // The directory too, if nothing else is in it.
    if (base::IsDirectoryEmpty(file.DirName())) {
      base::DeleteFile(file.DirName());
    }
  }
  AgentSetup setup = ReadAgentSetup(home, cli);
  setup.problems = std::move(problems);
  return setup;
}

base::DictValue AgentSetupToValue(const AgentSetup& setup,
                                  const base::FilePath& home) {
  base::ListValue agents;
  for (const AgentSetup::Agent& agent : setup.agents) {
    agents.Append(base::DictValue()
                      .Set("name", agent.name)
                      .Set("path", Display(agent.skill_file, home))
                      .Set("installed", agent.installed)
                      .Set("setUp", agent.set_up));
  }
  base::ListValue problems;
  for (const std::string& problem : setup.problems) {
    problems.Append(problem);
  }
  return base::DictValue()
      .Set("cli", setup.cli.value())
      .Set("link", Display(setup.link, home))
      .Set("cliExists", setup.cli_exists)
      .Set("linked", setup.linked)
      .Set("onPath", setup.on_path)
      .Set("agents", std::move(agents))
      .Set("problems", std::move(problems));
}

}  // namespace koan::agent
