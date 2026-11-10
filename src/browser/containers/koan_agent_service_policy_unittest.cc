// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_agent_service_policy.h"

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/keyed_service/core/dependency_graph.h"
#include "components/keyed_service/core/keyed_service_base_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::containers {
namespace {

// The upgrade guard: every per-profile service Chromium builds is decided for
// agent containers. A new upstream service fails here until it gets a row in
// koan_agent_service_policy.cc.
TEST(AgentServicePolicyTest, EveryProfileServiceIsDecided) {
  ChromeBrowserMainExtraPartsProfiles::
      EnsureBrowserContextKeyedServiceFactoriesBuilt();
  std::vector<raw_ptr<DependencyNode, VectorExperimental>> nodes;
  ASSERT_TRUE(BrowserContextDependencyManager::GetInstance()
                  ->GetDependencyGraphForTesting()
                  .GetConstructionOrder(&nodes));
  ASSERT_FALSE(nodes.empty());
  std::vector<std::string> undecided;
  for (DependencyNode* node : nodes) {
    const char* name = static_cast<KeyedServiceBaseFactory*>(node)->name();
    if (!FindAgentServiceRule(name)) {
      undecided.push_back(name);
    }
  }
  EXPECT_TRUE(undecided.empty())
      << "Per-profile services with no row in koan_agent_service_policy.cc "
         "(decide each for agent containers, agent-control.md 7.2): "
      << testing::PrintToString(undecided);
}

TEST(AgentServicePolicyTest, RulesAreSortedAndUnique) {
  base::span<const ServiceRule> rules = AgentServiceRules();
  for (size_t i = 1; i < rules.size(); ++i) {
    EXPECT_LT(rules[i - 1].name, rules[i].name);
  }
  for (const ServiceRule& rule : rules) {
    EXPECT_FALSE(rule.reason.empty()) << rule.name;
  }
}

}  // namespace
}  // namespace koan::containers
