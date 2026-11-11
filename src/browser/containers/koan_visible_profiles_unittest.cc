// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_visible_profiles.h"

#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::containers {
namespace {

// The launch picker's count: an agent's hidden profile (ephemeral, omitted,
// as CreateMultiProfileAsync(is_hidden) makes it) doesn't count, so the user's
// single profile still opens straight into a window.
TEST(VisibleProfilesTest, HiddenProfilesDontCount) {
  content::BrowserTaskEnvironment task_environment;
  TestingProfileManager manager(TestingBrowserProcess::GetGlobal());
  ASSERT_TRUE(manager.SetUp());
  manager.CreateTestingProfile("user");
  TestingProfile* agent = manager.CreateTestingProfile("agent");
  ProfileAttributesStorage& storage = *manager.profile_attributes_storage();
  EXPECT_EQ(CountVisibleProfiles(storage), 2u);

  ProfileAttributesEntry* entry =
      storage.GetProfileAttributesWithPath(agent->GetPath());
  ASSERT_TRUE(entry);
  entry->SetIsEphemeral(true);
  entry->SetIsOmitted(true);
  EXPECT_EQ(CountVisibleProfiles(storage), 1u);
  EXPECT_EQ(storage.GetNumberOfProfiles(), 2u);
}

}  // namespace
}  // namespace koan::containers
