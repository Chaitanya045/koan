// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_registry.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "koan/browser/agent/koan_agent_policy.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {

using AddResult = TargetRegistry::AddResult;

TEST(KoanAgentRegistryTest, AClientSeesOnlyItsOwnTabs) {
  TargetRegistry registry;
  EXPECT_EQ(registry.AddAgentTab("T1", "alice", "task"), AddResult::kOk);
  EXPECT_EQ(registry.AddAgentTab("T2", "bob", "task"), AddResult::kOk);
  EXPECT_EQ(registry.GrantUserTab("U1", "alice", TabState::kReadable),
            AddResult::kOk);
  EXPECT_EQ(registry.TargetsFor("alice"),
            (std::vector<std::string>{"T1", "U1"}));
  EXPECT_EQ(registry.TargetsFor("bob"), (std::vector<std::string>{"T2"}));
  EXPECT_TRUE(registry.TargetsFor("carol").empty());

  EXPECT_TRUE(registry.MaySend("alice", "T1", "Input.dispatchMouseEvent"));
  EXPECT_FALSE(registry.MaySend("alice", "T2", "Page.captureScreenshot"));
  EXPECT_FALSE(registry.MaySend("alice", "T9", "Page.captureScreenshot"));
  EXPECT_FALSE(registry.MaySend("", "T1", "Page.captureScreenshot"));
}

TEST(KoanAgentRegistryTest, TheBudgets) {
  TargetRegistry registry;
  for (size_t i = 0; i < TargetRegistry::kMaxTabsPerTask; ++i) {
    ASSERT_EQ(
        registry.AddAgentTab("a" + base::NumberToString(i), "alice", "one"),
        AddResult::kOk);
  }
  EXPECT_EQ(registry.CanAddAgentTab("alice", "one"), AddResult::kTaskFull);
  EXPECT_EQ(registry.AddAgentTab("a-extra", "alice", "one"),
            AddResult::kTaskFull);
  // The default task (empty name) is a task like any other.
  for (size_t i = TargetRegistry::kMaxTabsPerTask;
       i < TargetRegistry::kMaxTabsPerClient; ++i) {
    ASSERT_EQ(registry.AddAgentTab("a" + base::NumberToString(i), "alice", ""),
              AddResult::kOk);
  }
  EXPECT_EQ(registry.CanAddAgentTab("alice", "three"), AddResult::kClientFull);
  for (size_t i = TargetRegistry::kMaxTabsPerClient;
       i < TargetRegistry::kMaxTabsTotal; ++i) {
    ASSERT_EQ(registry.AddAgentTab("b" + base::NumberToString(i), "bob", "x"),
              AddResult::kOk);
  }
  EXPECT_EQ(registry.CanAddAgentTab("carol", "x"), AddResult::kBrowserFull);
  // User tabs are not the agent's to count.
  EXPECT_EQ(registry.GrantUserTab("U1", "carol", TabState::kDriven),
            AddResult::kOk);
  EXPECT_EQ(registry.AddAgentTab("a0", "bob", "y"), AddResult::kExists);
  EXPECT_EQ(registry.AddAgentTab("", "bob", "y"), AddResult::kInvalid);
  EXPECT_EQ(registry.CanAddAgentTab("", "y"), AddResult::kInvalid);
}

TEST(KoanAgentRegistryTest, OneClientPerUserTab) {
  TargetRegistry registry;
  ASSERT_EQ(registry.AddAgentTab("T1", "alice", ""), AddResult::kOk);
  EXPECT_EQ(registry.GrantUserTab("T1", "alice", TabState::kReadable),
            AddResult::kExists);
  EXPECT_EQ(registry.GrantUserTab("U1", "alice", TabState::kReadable),
            AddResult::kOk);
  EXPECT_EQ(registry.GrantUserTab("U1", "bob", TabState::kDriven),
            AddResult::kExists);
  // From readable to driven, once the user approved it.
  EXPECT_EQ(registry.GrantUserTab("U1", "alice", TabState::kDriven),
            AddResult::kOk);
  EXPECT_EQ(registry.Find("U1")->state, TabState::kDriven);
  EXPECT_EQ(registry.GrantUserTab("U2", "alice", TabState::kPaused),
            AddResult::kInvalid);
}

// Design 9.2: a paused tab still answers reads, and refuses everything that
// could change the page; a readable tab is the same.
TEST(KoanAgentRegistryTest, PausedAndReadableTabsOnlyRead) {
  TargetRegistry registry;
  ASSERT_EQ(registry.AddAgentTab("T1", "alice", ""), AddResult::kOk);
  ASSERT_EQ(registry.GrantUserTab("U1", "alice", TabState::kReadable),
            AddResult::kOk);
  EXPECT_TRUE(registry.Pause("T1"));
  EXPECT_FALSE(registry.Pause("nope"));
  for (const char* target : {"T1", "U1"}) {
    EXPECT_TRUE(registry.MaySend("alice", target, "Page.captureScreenshot"));
    EXPECT_TRUE(
        registry.MaySend("alice", target, "Accessibility.getFullAXTree"));
    for (const char* mutating :
         {"Input.dispatchKeyEvent", "Input.insertText", "Page.navigate",
          "Page.reload", "Runtime.evaluate", "Runtime.callFunctionOn",
          "Emulation.setDeviceMetricsOverride", "DOM.focus",
          "Page.setDocumentContent", "Unknown.method"}) {
      EXPECT_FALSE(registry.MaySend("alice", target, mutating))
          << target << " " << mutating;
    }
  }
  // Pausing a readable tab keeps it readable; only a paused tab resumes.
  EXPECT_TRUE(registry.Pause("U1"));
  EXPECT_EQ(registry.Find("U1")->state, TabState::kReadable);
  EXPECT_FALSE(registry.Resume("U1"));
  EXPECT_TRUE(registry.Resume("T1"));
  EXPECT_TRUE(registry.MaySend("alice", "T1", "Input.dispatchKeyEvent"));
}

TEST(KoanAgentRegistryTest, RemovingAClientOrATask) {
  TargetRegistry registry;
  registry.AddAgentTab("T1", "alice", "one");
  registry.AddAgentTab("T2", "alice", "two");
  registry.GrantUserTab("U1", "alice", TabState::kDriven);
  registry.AddAgentTab("T3", "bob", "one");
  EXPECT_EQ(registry.RemoveTask("alice", "one"),
            (std::vector<std::string>{"T1"}));
  EXPECT_EQ(registry.RemoveClient("alice"),
            (std::vector<std::string>{"T2", "U1"}));
  EXPECT_EQ(registry.TargetsFor("bob"), (std::vector<std::string>{"T3"}));
  EXPECT_TRUE(registry.Remove("T3"));
  EXPECT_FALSE(registry.Remove("T3"));
  EXPECT_EQ(registry.size(), 0u);
}

// Every read-only method is one the policy forwards at all, so the read-only
// list never makes a denied method reachable.
TEST(KoanAgentRegistryTest, ReadOnlyMethodsAreAllowedByThePolicy) {
  EXPECT_TRUE(std::ranges::is_sorted(ReadOnlyMethods()));
  for (std::string_view method : ReadOnlyMethods()) {
    EXPECT_EQ(Classify(method).verdict, Verdict::kAllow) << method;
  }
  EXPECT_FALSE(IsReadOnlyMethod("Page.navigate"));
  EXPECT_FALSE(IsReadOnlyMethod(""));
}

}  // namespace koan::agent
