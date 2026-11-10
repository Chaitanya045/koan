// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_container_partition.h"

#include <set>
#include <string>

#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace koan::containers {
namespace {

class ContainerPartitionTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
  content::TestBrowserContext context_;
};

TEST_F(ContainerPartitionTest, NewIdsAreValidAndDistinct) {
  std::set<std::string> ids;
  for (int i = 0; i < 100; ++i) {
    const std::string throwaway = NewContainerId(/*persistent=*/false);
    const std::string saved = NewContainerId(/*persistent=*/true);
    EXPECT_TRUE(IsValidContainerId(throwaway));
    EXPECT_TRUE(IsValidContainerId(saved));
    EXPECT_FALSE(ContainerId{.id = throwaway}.persistent());
    EXPECT_TRUE(ContainerId{.id = saved}.persistent());
    ids.insert(throwaway);
    ids.insert(saved);
  }
  EXPECT_EQ(ids.size(), 200u);
}

TEST_F(ContainerPartitionTest, RejectsInvalidIds) {
  for (const char* id : {"", "t", "x123", "tABC", "t-abc", "t1", "s abc",
                         "tabcdefghijklmnopqrstuvwxyzabcdefghijklmno"}) {
    EXPECT_FALSE(IsValidContainerId(id)) << id;
  }
  EXPECT_TRUE(IsValidContainerId("ta"));
  EXPECT_TRUE(IsValidContainerId("sabc"));
}

TEST_F(ContainerPartitionTest, PartitionRoundTrip) {
  for (ContainerKind kind : {ContainerKind::kAgent, ContainerKind::kUser}) {
    for (bool persistent : {false, true}) {
      const ContainerId container{.kind = kind,
                                  .id = NewContainerId(persistent)};
      const content::StoragePartitionConfig config =
          PartitionConfigFor(&context_, container);
      EXPECT_FALSE(config.is_default());
      EXPECT_EQ(config.in_memory(), !persistent);
      EXPECT_TRUE(config.partition_name().empty());
      // One domain per container: AsyncObliterateStoragePartition() deletes
      // by domain.
      EXPECT_NE(config.partition_domain().find(container.id),
                std::string::npos);
      EXPECT_EQ(ContainerFromPartitionConfig(config), container);
      EXPECT_EQ(IsAgentPartition(config), kind == ContainerKind::kAgent);
    }
  }
}

TEST_F(ContainerPartitionTest, OtherPartitionsAreNotContainers) {
  EXPECT_FALSE(ContainerFromPartitionConfig(
      content::StoragePartitionConfig::CreateDefault(&context_)));
  EXPECT_FALSE(IsAgentPartition(
      content::StoragePartitionConfig::CreateDefault(&context_)));
  // An extension's (its id as the domain), an isolated app's, a name.
  EXPECT_FALSE(
      ContainerFromPartitionConfig(content::StoragePartitionConfig::Create(
          &context_, "abcdefghijklmnopabcdefghijklmnop", "", false)));
  EXPECT_FALSE(
      ContainerFromPartitionConfig(content::StoragePartitionConfig::Create(
          &context_, "koanagentsabc", "name", false)));
  // A throwaway id on disk isn't one Koan made.
  EXPECT_FALSE(ContainerFromPartitionConfig(
      content::StoragePartitionConfig::Create(&context_, "koanagenttabc", "",
                                              /*in_memory=*/false)));
}

TEST_F(ContainerPartitionTest, AgentPrefixFailsClosed) {
  // Not a container Koan can name, but still blocked as an agent's.
  const content::StoragePartitionConfig odd =
      content::StoragePartitionConfig::Create(&context_, "koanagentx", "",
                                              /*in_memory=*/true);
  EXPECT_FALSE(ContainerFromPartitionConfig(odd));
  EXPECT_TRUE(IsAgentPartition(odd));
}

// The check //content/browser's FedCM and client-hints hooks use.
static_assert(IsAgentPartitionDomain("koanagenttabc"));
static_assert(IsAgentPartitionDomain("koanagent"));
static_assert(!IsAgentPartitionDomain("koanusertabc"));
static_assert(!IsAgentPartitionDomain(""));

// The default-partition paths (NoStatePrefetch, the loading predictor, app
// capturing) are refused for both kinds of container, and only for them.
TEST_F(ContainerPartitionTest, ContainerSiteInstances) {
  const GURL url("https://example.com/");
  for (ContainerKind kind : {ContainerKind::kAgent, ContainerKind::kUser}) {
    const ContainerId container{.kind = kind, .id = NewContainerId(false)};
    EXPECT_TRUE(IsContainerSiteInstance(
        CreateSiteInstance(&context_, container, url).get()));
  }
  EXPECT_FALSE(IsContainerSiteInstance(
      content::SiteInstance::CreateForURL(&context_, url).get()));
  EXPECT_FALSE(IsContainerSiteInstance(nullptr));
  EXPECT_FALSE(IsContainerFrame(nullptr));
  EXPECT_FALSE(IsContainerContents(nullptr));
}

TEST_F(ContainerPartitionTest, SessionFormRoundTrip) {
  const ContainerId agent{.kind = ContainerKind::kAgent,
                          .id = NewContainerId(false)};
  const ContainerId user{.kind = ContainerKind::kUser,
                         .id = NewContainerId(true)};
  EXPECT_EQ(Parse(Serialize(agent)), agent);
  EXPECT_EQ(Parse(Serialize(user)), user);
  for (const char* text : {"", "agent:", "agent:x", "agent:tA", "Agent:tab",
                           "guest:tab", "agent:tab ", " agent:tab"}) {
    EXPECT_FALSE(Parse(text)) << text;
  }
}

}  // namespace
}  // namespace koan::containers
