// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_container_extensions.h"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/prefs/testing_pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "koan/browser/containers/koan_history_profiles.h"
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

// The profiles an extension is off in (#544): what history, closed tabs, top
// sites and downloads leave out for it, named as a visit's tag and a closed
// tab's session data name them.
TEST(ContainerExtensionsListTest, ListsTheProfilesAnExtensionIsOffIn) {
  const ContainerId work = Saved();
  const ContainerId bank = Saved();
  base::ListValue list =
      ListWith(work, base::ListValue().Append(kPasswords).Append(kShopping));
  list.Append(base::DictValue()
                  .Set("id", bank.id)
                  .Set(kExtensionsOffKey, base::ListValue().Append(kShopping)));
  // An entry with an id that isn't a saved container's has no list.
  list.Append(base::DictValue()
                  .Set("id", NewContainerId(/*persistent=*/false))
                  .Set(kExtensionsOffKey, base::ListValue().Append(kShopping)));
  EXPECT_EQ(ProfilesExtensionIsOffIn(list, kPasswords),
            std::set<std::string>{Serialize(work)});
  EXPECT_EQ(ProfilesExtensionIsOffIn(list, kShopping),
            std::set<std::string>({Serialize(work), Serialize(bank)}));
  EXPECT_TRUE(ProfilesExtensionIsOffIn(list, "cccccccccccccccccccccccccccccccc")
                  .empty());
  EXPECT_TRUE(ProfilesExtensionIsOffIn(list, "not an id").empty());
  EXPECT_TRUE(ProfilesExtensionIsOffIn(base::ListValue(), kPasswords).empty());
}

// sessions.getRecentlyClosed: a closed tab's profile is the container in its
// session data; a tab of the default partition has none.
TEST(ContainerExtensionsListTest, AClosedTabIsInItsSessionDatasProfile) {
  const ContainerId work = Saved();
  const base::ListValue list =
      ListWith(work, base::ListValue().Append(kPasswords));
  const std::map<std::string, std::string> in_work = {
      {kContainerSessionKey, Serialize(work)}, {"koan.space", "x"}};
  EXPECT_TRUE(IsExtensionOffInSessionData(list, in_work, kPasswords));
  EXPECT_FALSE(IsExtensionOffInSessionData(list, in_work, kShopping));
  EXPECT_FALSE(IsExtensionOffInSessionData(list, {}, kPasswords));
  EXPECT_FALSE(IsExtensionOffInSessionData(
      list, {{kContainerSessionKey, Serialize(Saved())}}, kPasswords));
  EXPECT_FALSE(IsExtensionOffInSessionData(
      list, {{kContainerSessionKey, "not a container"}}, kPasswords));
  // An agent container with the same id isn't the user's.
  EXPECT_FALSE(IsExtensionOffInSessionData(
      list,
      {{kContainerSessionKey,
        Serialize({.kind = ContainerKind::kAgent, .id = work.id})}},
      kPasswords));
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

// What the tab and webNavigation event hooks and the context menu filter take
// once per tab.
TEST_F(ContainerExtensionsTest, ListsWhatIsOffInATab) {
  std::unique_ptr<content::WebContents> work_tab = NewTab(work_);
  std::unique_ptr<content::WebContents> default_tab = NewTab(std::nullopt);
  EXPECT_EQ(ExtensionsOffInContents(work_tab.get()),
            std::set<std::string>({kPasswords}));
  EXPECT_TRUE(ExtensionsOffInContents(default_tab.get()).empty());
  EXPECT_TRUE(ExtensionsOffInContents(nullptr).empty());
}

// webRequest and declarativeNetRequest: a navigation's request has no
// process (WebRequestAPI gives it -1), so its frame decides; any other
// request's process decides. Either way it is decided once, when the request
// is made.
TEST_F(ContainerExtensionsTest, ARequestIsInItsFramesOrProcesssPartition) {
  std::unique_ptr<content::WebContents> work_tab = NewTab(work_);
  std::unique_ptr<content::WebContents> default_tab = NewTab(std::nullopt);
  content::RenderFrameHost* work_frame = work_tab->GetPrimaryMainFrame();
  const content::ChildProcessId no_process;

  // A navigation in the profile's tab: by its frame, with no process.
  const std::optional<content::StoragePartitionConfig> navigation =
      PartitionOfRequest(work_frame->GetFrameTreeNodeId(), no_process);
  ASSERT_TRUE(navigation);
  EXPECT_EQ(*navigation, PartitionConfigFor(&context_, work_));
  EXPECT_TRUE(IsExtensionOffForRequest(&context_, navigation, kPasswords));
  EXPECT_FALSE(IsExtensionOffForRequest(&context_, navigation, kShopping));

  // A subresource: by its process.
  const std::optional<content::StoragePartitionConfig> subresource =
      PartitionOfRequest(content::FrameTreeNodeId(),
                         work_frame->GetProcess()->GetID());
  ASSERT_TRUE(subresource);
  EXPECT_TRUE(IsExtensionOffForRequest(&context_, subresource, kPasswords));

  // The default partition, a frame or process that is gone: no partition, and
  // so nothing is off.
  EXPECT_FALSE(PartitionOfRequest(
      default_tab->GetPrimaryMainFrame()->GetFrameTreeNodeId(), no_process));
  EXPECT_FALSE(PartitionOfRequest(
      content::FrameTreeNodeId(),
      default_tab->GetPrimaryMainFrame()->GetProcess()->GetID()));
  EXPECT_FALSE(PartitionOfRequest(content::FrameTreeNodeId(), no_process));
  EXPECT_FALSE(IsExtensionOffForRequest(&context_, std::nullopt, kPasswords));
}

// A visit's tag and a download's partition name the profile whose list
// applies (#544).
TEST_F(ContainerExtensionsTest, ListsWhatIsOffForAVisitOrAPartition) {
  EXPECT_EQ(ExtensionsOffForVisit(&context_,
                                  HistoryAppIdForProfile(Serialize(work_))),
            std::set<std::string>{kPasswords});
  // The default profile's visits (no tag), another app's tag, a profile with
  // no list.
  EXPECT_TRUE(ExtensionsOffForVisit(&context_, std::nullopt).empty());
  EXPECT_TRUE(ExtensionsOffForVisit(&context_, "com.example.app").empty());
  EXPECT_TRUE(ExtensionsOffForVisit(&context_,
                                    HistoryAppIdForProfile(Serialize(Saved())))
                  .empty());
  EXPECT_EQ(
      ExtensionsOffInPartition(&context_, PartitionConfigFor(&context_, work_)),
      std::set<std::string>{kPasswords});
  EXPECT_TRUE(
      ExtensionsOffInPartition(
          &context_, content::StoragePartitionConfig::CreateDefault(&context_))
          .empty());
  EXPECT_EQ(ProfilesExtensionIsOffIn(&context_, kPasswords),
            std::set<std::string>{Serialize(work_)});
  EXPECT_TRUE(IsExtensionOffInSessionData(
      &context_, {{kContainerSessionKey, Serialize(work_)}}, kPasswords));
}

// GetTabById()'s check: only while an extension's function runs, and for
// that extension.
TEST_F(ContainerExtensionsTest, TheCallingExtensionIsTheOneRunning) {
  std::unique_ptr<content::WebContents> work_tab = NewTab(work_);
  EXPECT_FALSE(IsCallingExtensionOffIn(work_tab.get()));
  {
    ScopedExtensionCall call(kPasswords);
    EXPECT_TRUE(IsCallingExtensionOffIn(work_tab.get()));
    EXPECT_EQ(CallingExtensionId(), kPasswords);
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
  EXPECT_TRUE(CallingExtensionId().empty());
}

}  // namespace
}  // namespace koan::containers
