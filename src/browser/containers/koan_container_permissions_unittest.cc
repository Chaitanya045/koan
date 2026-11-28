// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// The permission hook (B1, patches/components/permissions/permission_util.cc
// .patch) and the upstream check around it: an agent container gets no
// permission, a Space's profile asks for notifications like a default tab
// (#478), and anything else is upstream's.

#include <memory>

#include "components/content_settings/core/common/content_settings_types.h"
#include "components/permissions/permission_util.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/mock_render_process_host.h"
#include "content/public/test/test_browser_context.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace koan::containers {
namespace {

class ContainerPermissionsTest : public testing::Test {
 protected:
  std::unique_ptr<content::MockRenderProcessHost> ProcessIn(
      const content::StoragePartitionConfig& config) {
    return std::make_unique<content::MockRenderProcessHost>(
        &context_, config, /*is_for_guests_only=*/false);
  }
  std::unique_ptr<content::MockRenderProcessHost> ProcessIn(
      ContainerKind kind) {
    return ProcessIn(PartitionConfigFor(
        &context_, ContainerId{.kind = kind, .id = NewContainerId(true)}));
  }

  bool Blocked(ContentSettingsType permission,
               content::RenderProcessHost* process) {
    return permissions::PermissionUtil::IsPermissionBlockedInPartition(
        permission, GURL("https://mail.example.com/"), process);
  }

  content::BrowserTaskEnvironment task_environment_;
  content::TestBrowserContext context_;
};

TEST_F(ContainerPermissionsTest, KoanDecidesForItsContainers) {
  const auto user = ProcessIn(ContainerKind::kUser);
  const auto agent = ProcessIn(ContainerKind::kAgent);
  const auto plain =
      ProcessIn(content::StoragePartitionConfig::CreateDefault(&context_));

  EXPECT_EQ(PermissionBlockedInContainer(user.get(), /*notifications=*/true),
            false);
  EXPECT_EQ(PermissionBlockedInContainer(user.get(), /*notifications=*/false),
            std::nullopt);
  EXPECT_EQ(PermissionBlockedInContainer(agent.get(), true), true);
  EXPECT_EQ(PermissionBlockedInContainer(agent.get(), false), true);
  EXPECT_EQ(PermissionBlockedInContainer(plain.get(), true), std::nullopt);
  EXPECT_EQ(PermissionBlockedInContainer(nullptr, true), std::nullopt);

  // A domain that only looks like a user container's isn't one.
  const auto odd = ProcessIn(content::StoragePartitionConfig::Create(
      &context_, "koanuserx", "", /*in_memory=*/true));
  EXPECT_EQ(PermissionBlockedInContainer(odd.get(), true), std::nullopt);
}

// Upstream asks IsPermissionBlockedInPartition() before every request and
// status (permission_manager.cc), and it refuses notifications outside the
// site's home partition. With the hook, a Space's profile is asked; an
// agent's container gets nothing; another partition stays refused.
TEST_F(ContainerPermissionsTest, UpstreamAsksASpacesProfileForNotifications) {
  const auto user = ProcessIn(ContainerKind::kUser);
  const auto agent = ProcessIn(ContainerKind::kAgent);
  const auto plain =
      ProcessIn(content::StoragePartitionConfig::CreateDefault(&context_));
  const auto other = ProcessIn(content::StoragePartitionConfig::Create(
      &context_, "someapp", "", /*in_memory=*/true));

  EXPECT_FALSE(Blocked(ContentSettingsType::NOTIFICATIONS, user.get()));
  EXPECT_FALSE(Blocked(ContentSettingsType::GEOLOCATION, user.get()));
  EXPECT_TRUE(Blocked(ContentSettingsType::NOTIFICATIONS, agent.get()));
  EXPECT_TRUE(Blocked(ContentSettingsType::GEOLOCATION, agent.get()));
  EXPECT_FALSE(Blocked(ContentSettingsType::NOTIFICATIONS, plain.get()));
  EXPECT_TRUE(Blocked(ContentSettingsType::NOTIFICATIONS, other.get()));
}

// The check //content/browser's push and service-worker notification hooks
// use: any domain with the prefix, valid or not.
static_assert(IsUserPartitionDomain("koanusersabc"));
static_assert(IsUserPartitionDomain("koanuserx"));
static_assert(!IsUserPartitionDomain("koanagentsabc"));
static_assert(!IsUserPartitionDomain(""));

// The push hook's (#596): both kinds, and nothing else (the default
// partition's empty domain, an extension's id).
static_assert(IsContainerPartitionDomain("koanusersabc"));
static_assert(IsContainerPartitionDomain("koanagenttabc"));
static_assert(!IsContainerPartitionDomain(""));
static_assert(!IsContainerPartitionDomain("abcdefghijklmnopabcdefghijklmnop"));

}  // namespace
}  // namespace koan::containers
