// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_container_extensions.h"

#include <memory>
#include <set>
#include <string>

#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/prefs/testing_pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace koan::containers {
namespace {

// Extension ids: 32 letters a-p.
constexpr char kPasswords[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr char kShopping[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

ContainerId Saved() {
  return {.kind = ContainerKind::kUser,
          .id = NewContainerId(/*persistent=*/true)};
}

// The list as koan.containers.user saves it (AddUserContainer()).
base::ListValue ListWith(const ContainerId& container,
                         base::ListValue extensions_off) {
  return base::ListValue().Append(
      base::DictValue()
          .Set("id", container.id)
          .Set("name", "Work")
          .Set("color", 0)
          .Set(kExtensionsOffKey, std::move(extensions_off)));
}

TEST(ContainerExtensionsListTest, EverythingIsOnByDefault) {
  const ContainerId work = Saved();
  base::ListValue list = base::ListValue().Append(
      base::DictValue().Set("id", work.id).Set("name", "Work"));
  EXPECT_TRUE(ExtensionsOffIn(list, work).empty());
  EXPECT_FALSE(IsExtensionOffIn(list, work, kPasswords));
  EXPECT_TRUE(ExtensionsOffIn(base::ListValue(), work).empty());
}

TEST(ContainerExtensionsListTest, ReadsOnlyItsContainersExtensionIds) {
  const ContainerId work = Saved();
  const ContainerId personal = Saved();
  base::ListValue list =
      ListWith(work, base::ListValue()
                         .Append(kPasswords)
                         .Append("not an id")
                         .Append("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ")
                         .Append(42)
                         .Append(kPasswords));
  EXPECT_EQ(ExtensionsOffIn(list, work), std::set<std::string>{kPasswords});
  EXPECT_TRUE(IsExtensionOffIn(list, work, kPasswords));
  EXPECT_FALSE(IsExtensionOffIn(list, work, kShopping));
  EXPECT_TRUE(ExtensionsOffIn(list, personal).empty());
}

TEST(ContainerExtensionsListTest, OnlySavedUserContainersHaveAList) {
  const ContainerId work = Saved();
  base::ListValue list = ListWith(work, base::ListValue().Append(kPasswords));
  // The same id as an agent container, or a throwaway one, isn't the
  // user's container.
  EXPECT_TRUE(
      ExtensionsOffIn(list, {.kind = ContainerKind::kAgent, .id = work.id})
          .empty());
  const ContainerId throwaway = {.kind = ContainerKind::kUser,
                                 .id = NewContainerId(/*persistent=*/false)};
  base::ListValue throwaway_list =
      ListWith(throwaway, base::ListValue().Append(kPasswords));
  EXPECT_TRUE(ExtensionsOffIn(throwaway_list, throwaway).empty());
  EXPECT_FALSE(SetExtensionsOff(throwaway_list, throwaway, {kShopping}));
}

TEST(ContainerExtensionsListTest, SetsAndClearsTheList) {
  const ContainerId work = Saved();
  const ContainerId personal = Saved();
  base::ListValue list =
      base::ListValue()
          .Append(base::DictValue()
                      .Set("id", work.id)
                      .Set("name", "Work")
                      .Set("color", 3))
          .Append(
              base::DictValue().Set("id", personal.id).Set("name", "Personal"));

  EXPECT_TRUE(SetExtensionsOff(list, work, {kShopping, kPasswords}));
  EXPECT_EQ(ExtensionsOffIn(list, work),
            (std::set<std::string>{kPasswords, kShopping}));
  EXPECT_TRUE(ExtensionsOffIn(list, personal).empty());
  // The entry keeps the rest of the container.
  EXPECT_EQ(*list[0].GetDict().FindString("name"), "Work");
  EXPECT_EQ(list[0].GetDict().FindInt("color"), 3);

  // The same set again changes nothing.
  EXPECT_FALSE(SetExtensionsOff(list, work, {kPasswords, kShopping}));

  // Ids that aren't extension ids are dropped.
  EXPECT_TRUE(SetExtensionsOff(list, work, {kShopping, "../etc"}));
  EXPECT_EQ(ExtensionsOffIn(list, work), std::set<std::string>{kShopping});

  // Back to every extension on: the key goes.
  EXPECT_TRUE(SetExtensionsOff(list, work, {}));
  EXPECT_FALSE(list[0].GetDict().contains(kExtensionsOffKey));

  // A container the list doesn't have.
  EXPECT_FALSE(SetExtensionsOff(list, Saved(), {kPasswords}));
}

class ContainerExtensionsTest : public testing::Test {
 protected:
  ContainerExtensionsTest() {
    prefs_.registry()->RegisterListPref(kUserContainersPref);
    user_prefs::UserPrefs::Set(&context_, &prefs_);
    ScopedListPrefUpdate update(&prefs_, kUserContainersPref);
    update->Append(base::DictValue().Set("id", work_.id).Set("name", "Work"));
    SetExtensionsOff(*update, work_, {kPasswords});
  }

  std::unique_ptr<content::WebContents> NewTab(
      const std::optional<ContainerId>& container) {
    const GURL url("https://bank.test/");
    return content::WebContentsTester::CreateTestWebContents(
        &context_,
        container ? CreateSiteInstance(&context_, *container, url) : nullptr);
  }

  const ContainerId work_ = Saved();
  content::BrowserTaskEnvironment task_environment_;
  content::RenderViewHostTestEnabler render_view_host_enabler_;
  TestingPrefServiceSimple prefs_;
  content::TestBrowserContext context_;
};

TEST_F(ContainerExtensionsTest, AsksTheContainersList) {
  EXPECT_TRUE(IsExtensionOffInPartition(
      &context_, PartitionConfigFor(&context_, work_), kPasswords));
  EXPECT_FALSE(IsExtensionOffInPartition(
      &context_, PartitionConfigFor(&context_, work_), kShopping));
  // The default partition and other containers have no list.
  EXPECT_FALSE(IsExtensionOffInPartition(
      &context_, content::StoragePartitionConfig::CreateDefault(&context_),
      kPasswords));
  EXPECT_FALSE(IsExtensionOffInPartition(
      &context_, PartitionConfigFor(&context_, Saved()), kPasswords));
  EXPECT_FALSE(IsExtensionOffInPartition(
      &context_,
      PartitionConfigFor(&context_,
                         {.kind = ContainerKind::kAgent,
                          .id = NewContainerId(/*persistent=*/false)}),
      kPasswords));

  std::unique_ptr<content::WebContents> work_tab = NewTab(work_);
  std::unique_ptr<content::WebContents> default_tab = NewTab(std::nullopt);
  EXPECT_TRUE(IsExtensionOffInContents(work_tab.get(), kPasswords));
  EXPECT_FALSE(IsExtensionOffInContents(work_tab.get(), kShopping));
  EXPECT_FALSE(IsExtensionOffInContents(default_tab.get(), kPasswords));
  EXPECT_FALSE(IsExtensionOffInContents(nullptr, kPasswords));
  EXPECT_TRUE(IsExtensionOffInProcess(
      work_tab->GetPrimaryMainFrame()->GetProcess(), kPasswords));
  EXPECT_FALSE(IsExtensionOffInProcess(
      default_tab->GetPrimaryMainFrame()->GetProcess(), kPasswords));
  EXPECT_FALSE(IsExtensionOffInProcess(nullptr, kPasswords));
}

// GetTabById()'s check: only while an extension's function runs, and for
// that extension.
TEST_F(ContainerExtensionsTest, TheCallingExtensionIsTheOneRunning) {
  std::unique_ptr<content::WebContents> work_tab = NewTab(work_);
  EXPECT_FALSE(IsCallingExtensionOffIn(work_tab.get()));
  {
    ScopedExtensionCall call(kPasswords);
    EXPECT_TRUE(IsCallingExtensionOffIn(work_tab.get()));
    {
      // A function no extension calls (WebUI).
      ScopedExtensionCall webui("");
      EXPECT_FALSE(IsCallingExtensionOffIn(work_tab.get()));
    }
    EXPECT_TRUE(IsCallingExtensionOffIn(work_tab.get()));
  }
  {
    ScopedExtensionCall call(kShopping);
    EXPECT_FALSE(IsCallingExtensionOffIn(work_tab.get()));
  }
  EXPECT_FALSE(IsCallingExtensionOffIn(work_tab.get()));
}

}  // namespace
}  // namespace koan::containers
