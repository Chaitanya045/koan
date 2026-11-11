// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_agent_access.h"

#include <memory>
#include <string>

#include "components/prefs/testing_pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::containers {
namespace {

using enum AgentCapability;

class AgentAccessTest : public testing::Test {
 protected:
  AgentAccessTest() {
    RegisterAgentAccessPrefs(prefs_.registry());
    user_prefs::UserPrefs::Set(&context_, &prefs_);
  }

  std::unique_ptr<content::WebContents> NewTab() {
    return content::WebContentsTester::CreateTestWebContents(&context_,
                                                             nullptr);
  }

  static ContainerId Task() {
    return {.kind = ContainerKind::kAgent,
            .id = NewContainerId(/*persistent=*/false)};
  }

  content::BrowserTaskEnvironment task_environment_;
  content::RenderViewHostTestEnabler render_view_host_enabler_;
  TestingPrefServiceSimple prefs_;
  content::TestBrowserContext context_;
};

// The user's decision (2026-11-11): agents use the browser as the user does.
TEST_F(AgentAccessTest, EverythingIsOnByDefault) {
  for (AgentCapability capability : kAgentCapabilities) {
    EXPECT_TRUE(AgentMay(&prefs_, capability));
    EXPECT_TRUE(AgentMay(&context_, capability));
  }
}

TEST_F(AgentAccessTest, EachSwitchIsItsOwn) {
  prefs_.SetBoolean(kAgentMayUsePasswords, false);
  EXPECT_FALSE(AgentMay(&prefs_, kPasswords));
  EXPECT_TRUE(AgentMay(&prefs_, kLogins));
  EXPECT_TRUE(AgentMay(&prefs_, kAutofill));
  EXPECT_TRUE(AgentMay(&prefs_, kExtensions));
  EXPECT_TRUE(AgentMay(&prefs_, kHistory));
}

TEST_F(AgentAccessTest, FailsClosed) {
  EXPECT_FALSE(AgentMay(static_cast<const PrefService*>(nullptr), kHistory));
  EXPECT_FALSE(
      AgentMay(static_cast<content::BrowserContext*>(nullptr), kHistory));
  TestingPrefServiceSimple unregistered;
  EXPECT_FALSE(AgentMay(&unregistered, kHistory));
  content::TestBrowserContext no_prefs;
  EXPECT_FALSE(AgentMay(&no_prefs, kHistory));
}

TEST_F(AgentAccessTest, StrictDeniesAll) {
  DenyAllAgentAccess(&prefs_);
  for (AgentCapability capability : kAgentCapabilities) {
    EXPECT_FALSE(AgentMay(&prefs_, capability));
  }
}

// A block applies only to an agent tab, and only while its switch is off.
TEST_F(AgentAccessTest, BlocksOnlyAgentTabsWithTheSwitchOff) {
  std::unique_ptr<content::WebContents> user_tab = NewTab();
  std::unique_ptr<content::WebContents> agent_tab = NewTab();
  SetAgentTab(agent_tab.get(), Task());
  ASSERT_TRUE(IsAgentTab(agent_tab.get()));

  EXPECT_FALSE(BlocksAgent(agent_tab.get(), kPasswords));
  prefs_.SetBoolean(kAgentMayUsePasswords, false);
  EXPECT_TRUE(BlocksAgent(agent_tab.get(), kPasswords));
  EXPECT_FALSE(BlocksAgent(agent_tab.get(), kAutofill));
  EXPECT_FALSE(BlocksAgent(user_tab.get(), kPasswords));
  EXPECT_FALSE(BlocksAgent(nullptr, kPasswords));

  EXPECT_TRUE(BlocksAgentFrame(agent_tab->GetPrimaryMainFrame(), kPasswords));
  EXPECT_FALSE(BlocksAgentFrame(user_tab->GetPrimaryMainFrame(), kPasswords));
  // In the user's partition the processes are the user's: nothing is blocked
  // per process.
  EXPECT_FALSE(BlocksAgentProcess(
      agent_tab->GetPrimaryMainFrame()->GetProcess(), kPasswords));
}

// A user's tab an agent leases is an agent tab for the blocks, but not one of
// the agent's own tabs.
TEST_F(AgentAccessTest, LeasedUserTab) {
  std::unique_ptr<content::WebContents> tab = NewTab();
  SetAgentLeased(tab.get(), true);
  EXPECT_TRUE(IsAgentContents(tab.get()));
  EXPECT_FALSE(IsAgentTab(tab.get()));
  EXPECT_FALSE(IsAgentTabFrame(tab->GetPrimaryMainFrame()));
  prefs_.SetBoolean(kAgentMayUseHistory, false);
  EXPECT_TRUE(BlocksAgent(tab.get(), kHistory));
  SetAgentLeased(tab.get(), false);
  EXPECT_FALSE(BlocksAgent(tab.get(), kHistory));
}

// An agent's own tab in the user's partition is grouped by its task's id
// (its Space, the session, the end of the task), and keeps its first mark.
TEST_F(AgentAccessTest, MarkedTabIsItsTasks) {
  std::unique_ptr<content::WebContents> tab = NewTab();
  EXPECT_EQ(ContainerOf(tab.get()), std::nullopt);
  const ContainerId task = Task();
  SetAgentTab(tab.get(), task);
  EXPECT_EQ(ContainerOf(tab.get()), task);
  SetAgentTab(tab.get(), Task());
  EXPECT_EQ(ContainerOf(tab.get()), task);
  // Still not a container: no partition has the task's id.
  EXPECT_FALSE(IsContainerContents(tab.get()));
  EXPECT_TRUE(IsAgentContents(tab.get()));
}

TEST_F(AgentAccessTest, Describe) {
  const std::u16string all = DescribeAgentAccess(&prefs_, /*strict=*/false);
  EXPECT_NE(all.find(u"They use your logins, saved passwords, addresses and "
                     u"cards, your extensions and history."),
            std::u16string::npos);
  EXPECT_EQ(all.find(u"don't get"), std::u16string::npos);

  prefs_.SetBoolean(kAgentMayUseLogins, false);
  prefs_.SetBoolean(kAgentMayUseExtensions, false);
  const std::u16string some = DescribeAgentAccess(&prefs_, /*strict=*/false);
  EXPECT_NE(some.find(u"container"), std::u16string::npos);
  EXPECT_NE(some.find(u"They don't get your logins and your extensions."),
            std::u16string::npos);

  EXPECT_NE(DescribeAgentAccess(&prefs_, /*strict=*/true).find(u"Strict"),
            std::u16string::npos);
}

}  // namespace
}  // namespace koan::containers
