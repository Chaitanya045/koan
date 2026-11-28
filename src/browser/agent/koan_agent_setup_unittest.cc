// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_setup.h"

#include <set>
#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {
namespace {

std::string Read(const base::FilePath& path) {
  std::string text;
  base::ReadFileToString(path, &text);
  return text;
}

const AgentSetup::Agent* AgentNamed(const AgentSetup& setup,
                                    const std::string& name) {
  for (const AgentSetup::Agent& agent : setup.agents) {
    if (agent.name == name) {
      return &agent;
    }
  }
  return nullptr;
}

TEST(KoanAgentSetupTest, TheEntrySkillOnlyPointsAtKoanSkill) {
  const std::string_view skill = EntrySkill();
  EXPECT_TRUE(IsKoanSkill(skill));
  EXPECT_TRUE(skill.starts_with("---\nname: koan-browser\ndescription: "));
  for (const char* needed : {
#if BUILDFLAG(IS_MAC)
           "~/Applications/Koan.app/Contents/Resources/bin/koan",
           "/Applications/Koan.app/Contents/Resources/bin/koan", "AppleScript",
#else
           "~/.local/share/flatpak/app/dev.koan.Koan/current/active/files/"
           "koan/koan",
           "/var/lib/flatpak/app/dev.koan.Koan/current/active/files/koan/koan",
           "xdotool",
#endif
           "the user's", "Koan", "run `koan skill`", "~/.local/bin/koan",
           "ssh"}) {
    EXPECT_NE(skill.find(needed), std::string_view::npos) << needed;
  }
#if !BUILDFLAG(IS_MAC)
  // Issue #489: no Mac wording off the Mac.
  EXPECT_EQ(skill.find("Mac"), std::string_view::npos);
#endif
  // Small: the guide is `koan skill`'s, not the entry's.
  EXPECT_LT(skill.size(), 2048u);
  EXPECT_FALSE(IsKoanSkill("---\nname: other\n---\nname: koan-browser\n"));
  EXPECT_FALSE(IsKoanSkill("name: koan-browser\n"));
  EXPECT_FALSE(IsKoanSkill("# my notes\n"));
}

// The agents and where their user skills live (their public docs,
// 2026-11-19). One table; a shared directory is written once.
TEST(KoanAgentSetupTest, AgentSkillDirectories) {
  std::set<std::string> names;
  for (const AgentSkillHome& entry : AgentSkillHomes()) {
    names.insert(entry.agent);
    EXPECT_FALSE(std::string_view(entry.skills_dir).starts_with("/"));
    EXPECT_TRUE(std::string_view(entry.skills_dir).ends_with("skills"));
  }
  EXPECT_TRUE(names.contains("Claude Code"));
  EXPECT_TRUE(names.contains("Codex"));
  EXPECT_TRUE(names.contains("Cursor"));
  for (const AgentSkillHome& entry : AgentSkillHomes()) {
    if (std::string_view(entry.agent) == "Claude Code") {
      EXPECT_STREQ(entry.skills_dir, ".claude/skills");
    } else if (std::string_view(entry.agent) == "Codex") {
      EXPECT_STREQ(entry.skills_dir, ".agents/skills");
      EXPECT_STREQ(entry.marker, ".codex");
    } else if (std::string_view(entry.agent) == "Cursor") {
      EXPECT_STREQ(entry.skills_dir, ".cursor/skills");
    }
  }
}

TEST(KoanAgentSetupTest, LocalBinOnThePath) {
  EXPECT_TRUE(MentionsLocalBin("export PATH=\"$HOME/.local/bin:$PATH\"\n"));
  EXPECT_TRUE(MentionsLocalBin("fish_add_path ~/.local/bin"));
  EXPECT_FALSE(MentionsLocalBin("# export PATH=$HOME/.local/bin:$PATH"));
  EXPECT_FALSE(MentionsLocalBin("alias ll='ls -l'\n"));
}

#if BUILDFLAG(IS_LINUX)
// The Flatpak's koan, by the path that follows the installed version.
TEST(KoanAgentSetupTest, TheLinkFollowsTheFlatpaksUpdates) {
  const base::FilePath deployed(
      "/home/u/.local/share/flatpak/app/dev.koan.Koan/x86_64/master/"
      "0123abcd/files/koan/koan");
  const base::FilePath stable(
      "/home/u/.local/share/flatpak/app/dev.koan.Koan/current/active/files/"
      "koan/koan");
  EXPECT_EQ(StableFlatpakPath(deployed), stable);
  EXPECT_EQ(ToolToLink(deployed), stable);
  EXPECT_EQ(StableFlatpakPath(stable), stable);
  EXPECT_EQ(*StableFlatpakPath(base::FilePath(
                "/var/lib/flatpak/app/dev.koan.Koan/aarch64/beta/ff/files")),
            base::FilePath(
                "/var/lib/flatpak/app/dev.koan.Koan/current/active/files"));
  // Not Koan's Flatpak: linked as it is.
  const base::FilePath built("/home/u/koan/out/Release/koan");
  EXPECT_FALSE(StableFlatpakPath(built));
  EXPECT_EQ(ToolToLink(built), built);
  EXPECT_FALSE(StableFlatpakPath(base::FilePath(
      "/var/lib/flatpak/app/org.other.App/x86_64/stable/ab/files/koan")));
  EXPECT_FALSE(StableFlatpakPath(
      base::FilePath("/var/lib/flatpak/app/dev.koan.Koan/x86_64/master")));
}

TEST(KoanAgentSetupTest, TheAppPathOfAFlatpakSandbox) {
  EXPECT_EQ(*FlatpakAppPath("[Application]\nname=dev.koan.Koan\n\n"
                            "[Instance]\ninstance-id=123\napp-path=/var/lib/"
                            "flatpak/app/dev.koan.Koan/x86_64/master/ab/files\n"
                            "branch=master\n"),
            base::FilePath(
                "/var/lib/flatpak/app/dev.koan.Koan/x86_64/master/ab/files"));
  // Only [Instance]'s, only an absolute path without "..".
  EXPECT_FALSE(FlatpakAppPath("[Application]\napp-path=/x/files\n"));
  EXPECT_FALSE(FlatpakAppPath("[Instance]\napp-path=files\n"));
  EXPECT_FALSE(FlatpakAppPath("[Instance]\napp-path=/a/../b\n"));
  EXPECT_FALSE(FlatpakAppPath(""));
}

TEST(KoanAgentSetupTest, ACommandPathIsOneShellWord) {
  EXPECT_EQ(ShellQuoted(base::FilePath("/var/lib/flatpak/app/x/koan")),
            "/var/lib/flatpak/app/x/koan");
  EXPECT_EQ(ShellQuoted(base::FilePath("/home/a b/koan")), "'/home/a b/koan'");
  EXPECT_EQ(ShellQuoted(base::FilePath("/home/it's/koan")),
            "'/home/it'\\''s/koan'");
  EXPECT_EQ(ShellQuoted(base::FilePath("/x/$(rm)/koan")), "'/x/$(rm)/koan'");
}
#endif

// Where the tool is inside an installed Koan: the app on the Mac, a
// deployment of the Flatpak on Linux.
base::FilePath ToolUnder(const base::FilePath& root) {
#if BUILDFLAG(IS_MAC)
  return root.Append("Koan.app/Contents/Resources/bin/koan");
#else
  return root.Append(
      "flatpak/app/dev.koan.Koan/x86_64/master/0123/files/koan/koan");
#endif
}

class KoanAgentSetupFilesTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    home_ = dir_.GetPath().Append("home");
    app_ = ToolUnder(dir_.GetPath());
    ASSERT_TRUE(base::CreateDirectory(home_));
    ASSERT_TRUE(base::CreateDirectory(app_.DirName()));
    ASSERT_TRUE(base::WriteFile(app_, "#!/bin/sh\n"));
  }

  base::FilePath Skill(const char* skills_dir) {
    return home_.Append(skills_dir).Append("koan-browser").Append("SKILL.md");
  }
  base::FilePath Link() { return home_.Append(".local/bin/koan"); }

  base::ScopedTempDir dir_;
  base::FilePath home_;
  base::FilePath app_;
};

TEST_F(KoanAgentSetupFilesTest, NothingIsWrittenUntilAsked) {
  ASSERT_TRUE(base::CreateDirectory(home_.Append(".claude")));
  const AgentSetup setup = ReadAgentSetup(home_, app_);
  EXPECT_TRUE(setup.cli_exists);
  EXPECT_FALSE(setup.linked);
  EXPECT_TRUE(AgentNamed(setup, "Claude Code")->installed);
  EXPECT_FALSE(AgentNamed(setup, "Claude Code")->set_up);
  EXPECT_FALSE(base::PathExists(Skill(".claude/skills")));
  EXPECT_FALSE(base::PathExists(Link()));
}

TEST_F(KoanAgentSetupFilesTest, SetUpLinksTheToolAndWritesEachAgentsSkill) {
  ASSERT_TRUE(base::CreateDirectory(home_.Append(".claude")));
  ASSERT_TRUE(base::CreateDirectory(home_.Append(".codex")));
  AgentSetup setup = SetUpAgents(home_, app_);
  EXPECT_TRUE(setup.problems.empty()) << setup.problems.front();
  EXPECT_TRUE(setup.linked);
  base::FilePath target;
  ASSERT_TRUE(base::ReadSymbolicLink(Link(), &target));
  EXPECT_EQ(target, app_);
  EXPECT_EQ(Read(Skill(".claude/skills")), EntrySkill());
  // Codex's is the shared ~/.agents/skills.
  EXPECT_EQ(Read(Skill(".agents/skills")), EntrySkill());
  EXPECT_TRUE(AgentNamed(setup, "Codex")->set_up);
  // Not installed: nothing written for it.
  EXPECT_FALSE(AgentNamed(setup, "Cursor")->installed);
  EXPECT_FALSE(base::PathExists(home_.Append(".cursor")));
  EXPECT_FALSE(setup.on_path);

  // Again, after an update that moved the app: the link follows it.
  const base::FilePath moved = ToolUnder(dir_.GetPath().Append("Apps"));
  ASSERT_TRUE(base::CreateDirectory(moved.DirName()));
  ASSERT_TRUE(base::WriteFile(moved, "#!/bin/sh\n"));
  ASSERT_TRUE(base::WriteFile(Skill(".claude/skills"),
                              "---\nname: koan-browser\n---\nold\n"));
  setup = SetUpAgents(home_, moved);
  EXPECT_TRUE(setup.problems.empty());
  ASSERT_TRUE(base::ReadSymbolicLink(Link(), &target));
  EXPECT_EQ(target, moved);
  EXPECT_EQ(Read(Skill(".claude/skills")), EntrySkill());

  // Remove: both gone, and the empty skill directories with them.
  setup = RemoveAgentSetup(home_, moved);
  EXPECT_TRUE(setup.problems.empty());
  EXPECT_FALSE(base::IsLink(Link()));
  EXPECT_FALSE(base::PathExists(Skill(".claude/skills").DirName()));
  EXPECT_FALSE(base::PathExists(Skill(".agents/skills").DirName()));
  EXPECT_FALSE(AgentNamed(setup, "Claude Code")->set_up);
  EXPECT_TRUE(base::DirectoryExists(home_.Append(".claude/skills")));
}

TEST_F(KoanAgentSetupFilesTest, WhatIsNotKoansIsLeftAlone) {
  ASSERT_TRUE(base::CreateDirectory(home_.Append(".claude")));
  ASSERT_TRUE(base::CreateDirectory(home_.Append(".cursor")));
  // The user's own ~/.local/bin/koan, and a skill of the same name.
  ASSERT_TRUE(base::CreateDirectory(Link().DirName()));
  ASSERT_TRUE(base::WriteFile(Link(), "mine"));
  ASSERT_TRUE(base::CreateDirectory(Skill(".claude/skills").DirName()));
  ASSERT_TRUE(base::WriteFile(Skill(".claude/skills"), "# my notes\n"));
  // Cursor's, linked to the shared copy (as `npx skills` installs it).
  ASSERT_TRUE(base::CreateDirectory(Skill(".agents/skills").DirName()));
  ASSERT_TRUE(base::WriteFile(Skill(".agents/skills"), EntrySkill()));
  ASSERT_TRUE(base::CreateDirectory(home_.Append(".cursor/skills")));
  ASSERT_TRUE(base::CreateSymbolicLink(Skill(".agents/skills").DirName(),
                                       Skill(".cursor/skills").DirName()));

  AgentSetup setup = SetUpAgents(home_, app_);
  EXPECT_EQ(setup.problems.size(), 2u);
  EXPECT_EQ(Read(Link()), "mine");
  EXPECT_EQ(Read(Skill(".claude/skills")), "# my notes\n");
  EXPECT_FALSE(AgentNamed(setup, "Claude Code")->set_up);
  EXPECT_TRUE(AgentNamed(setup, "Cursor")->set_up);
  EXPECT_TRUE(base::IsLink(Skill(".cursor/skills").DirName()));

  setup = RemoveAgentSetup(home_, app_);
  EXPECT_EQ(Read(Link()), "mine");
  EXPECT_EQ(Read(Skill(".claude/skills")), "# my notes\n");
  // A link someone else made stays.
  EXPECT_TRUE(base::IsLink(Skill(".cursor/skills").DirName()));

  // A link to some other program called koan is not Koan's either.
  ASSERT_TRUE(base::DeleteFile(Link()));
  ASSERT_TRUE(
      base::CreateSymbolicLink(base::FilePath("/opt/other/koan"), Link()));
  setup = SetUpAgents(home_, app_);
  ASSERT_TRUE(base::IsLink(Link()));
  base::FilePath target;
  ASSERT_TRUE(base::ReadSymbolicLink(Link(), &target));
  EXPECT_EQ(target.value(), "/opt/other/koan");
  RemoveAgentSetup(home_, app_);
  EXPECT_TRUE(base::IsLink(Link()));
}

TEST_F(KoanAgentSetupFilesTest, ThePageShowsPathsUnderHomeAsTilde) {
  ASSERT_TRUE(base::CreateDirectory(home_.Append(".claude")));
  const base::DictValue value =
      AgentSetupToValue(SetUpAgents(home_, app_), home_);
  EXPECT_EQ(*value.FindString("link"), "~/.local/bin/koan");
  EXPECT_EQ(*value.FindString("cli"), app_.value());
  EXPECT_EQ(value.FindBool("linked"), true);
  const base::ListValue& agents = *value.FindList("agents");
  ASSERT_FALSE(agents.empty());
  EXPECT_EQ(*agents[0].GetDict().FindString("path"),
            "~/.claude/skills/koan-browser/SKILL.md");
  EXPECT_EQ(agents[0].GetDict().FindBool("setUp"), true);
}

}  // namespace
}  // namespace koan::agent
