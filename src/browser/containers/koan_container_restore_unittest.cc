// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <map>
#include <string>
#include <vector>

#include "content/public/browser/security_principal.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::containers {
namespace {

class ContainerRestoreTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
  content::TestBrowserContext context_;
};

// Only a saved user container's tab is restored into its container (#111);
// every other record gets no SiteInstance, and the restore hook blanks it.
TEST_F(ContainerRestoreTest, RestoredTabs) {
  const ContainerId saved_user{.kind = ContainerKind::kUser,
                               .id = NewContainerId(true)};
  scoped_refptr<content::SiteInstance> site = SiteInstanceForRestoredTab(
      &context_, {{kContainerSessionKey, Serialize(saved_user)}});
  ASSERT_TRUE(site);
  EXPECT_EQ(ContainerFromPartitionConfig(
                site->GetSecurityPrincipal().GetStoragePartitionConfig()),
            saved_user);

  const ContainerId agent{.kind = ContainerKind::kAgent,
                          .id = NewContainerId(true)};
  const ContainerId throwaway_user{.kind = ContainerKind::kUser,
                                   .id = NewContainerId(false)};
  for (const std::map<std::string, std::string>& extra_data :
       std::vector<std::map<std::string, std::string>>{
           {},
           {{kContainerSessionKey, Serialize(agent)}},
           {{kContainerSessionKey, Serialize(throwaway_user)}},
           {{kContainerSessionKey, "user:"}},
           {{"koan.other", Serialize(saved_user)}},
       }) {
    EXPECT_FALSE(SiteInstanceForRestoredTab(&context_, extra_data));
  }
}

}  // namespace
}  // namespace koan::containers
