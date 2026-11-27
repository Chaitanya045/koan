// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Zen's bookmarks per Space (koan_bookmark_spaces.h): the bookmarks bar and
// the URL bar's bookmark suggestions follow the active Space, a deleted
// Space's bookmarks show everywhere, and the model (the bookmark manager,
// chrome.bookmarks) keeps every bookmark.

#include <algorithm>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/browser/titled_url_match.h"
#include "components/bookmarks/test/bookmark_test_helpers.h"
#include "components/prefs/pref_service.h"
#include "components/query_parser/query_parser.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/omnibox/koan_omnibox_hooks.h"
#include "koan/browser/ui/views/koan_bookmark_spaces.h"
#include "koan/browser/ui/views/koan_bookmark_spaces_hooks.h"
#include "koan/browser/ui/views/koan_bookmark_spaces_rule.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_window_sync_client.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "ui/base/base_window.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace koan {

namespace {

using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;

class KoanBookmarkSpacesBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
    model_ =
        BookmarkModelFactory::GetForBrowserContext(browser()->GetProfile());
    bookmarks::test::WaitForBookmarkModelToLoad(model_);
    // Zen's bookmarks toolbar (off by default in Koan, as in Zen).
    browser()->GetProfile()->GetPrefs()->SetBoolean(prefs::kBookmarksBarShow,
                                                    true);
  }

  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  KoanWindowSyncClient* sync_client() {
    return KoanBrowserViewController::From(browser_view())->window_sync();
  }

  // Creates a Space (it becomes the active one) and waits for its sync id.
  std::string CreateSpace(const std::u16string& name) {
    KoanWorkspaceController* spaces = GetSpaces(browser());
    const size_t count = spaces->GetSpaces().size();
    spaces->CreateSpace(name, u"", {});
    EXPECT_TRUE(WaitForSpaceCount(browser(), count + 1));
    return WaitForSyncId(*spaces->GetActiveSpace());
  }
  std::string WaitForSyncId(const tab_groups::TabGroupId& space) {
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return !sync_client()->GetSpaceSyncId(space).empty(); }));
    return sync_client()->GetSpaceSyncId(space);
  }

  const BookmarkNode* AddToBar(const std::u16string& title,
                               const std::string& url) {
    return model_->AddURL(model_->bookmark_bar_node(),
                          model_->bookmark_bar_node()->children().size(), title,
                          GURL(url));
  }

  // The bookmarks bar's button titled `title`, laid out.
  views::LabelButton* BarButton(const std::u16string& title) {
    BookmarkBarView* bar = browser_view()->bookmark_bar();
    if (!bar) {
      return nullptr;
    }
    views::test::RunScheduledLayout(bar);
    for (views::View* child : bar->children()) {
      auto* button = views::AsViewClass<views::LabelButton>(child);
      if (button && button->GetText() == title) {
        return button;
      }
    }
    return nullptr;
  }
  // Whether the bar shows the button: it takes room and can take focus.
  bool BarShows(const std::u16string& title) {
    views::LabelButton* button = BarButton(title);
    return button && button->GetVisible() && button->width() > 0 &&
           button->GetFocusBehavior() != views::View::FocusBehavior::NEVER;
  }
  bool BarCollapsed(const std::u16string& title) {
    views::LabelButton* button = BarButton(title);
    return button && button->width() == 0 &&
           button->GetFocusBehavior() == views::View::FocusBehavior::NEVER;
  }

  // What the URL bar's BookmarkProvider keeps of the bookmarks matching
  // `text`: their titles.
  std::vector<std::u16string> Suggested(const std::u16string& text) {
    std::vector<std::u16string> titles;
    for (const bookmarks::TitledUrlMatch& match : model_->GetBookmarksMatching(
             text, 50, query_parser::MatchingAlgorithm::DEFAULT)) {
      if (!HidesBookmarkSuggestion(model_, match.node)) {
        titles.push_back(match.node->GetTitledUrlNodeTitle());
      }
    }
    std::ranges::sort(titles);
    return titles;
  }

  raw_ptr<BookmarkModel> model_ = nullptr;
};

}  // namespace

// Zen: the toolbar shows the active Space's bookmarks and those of no Space;
// switching Space brings the others back.
IN_PROC_BROWSER_TEST_F(KoanBookmarkSpacesBrowserTest, BarFollowsTheSpace) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  const tab_groups::TabGroupId first = *spaces->GetActiveSpace();
  const std::string first_id = WaitForSyncId(first);
  const std::string work_id = CreateSpace(u"Work");
  const tab_groups::TabGroupId work = *spaces->GetActiveSpace();

  AddToBar(u"Everywhere", "https://everywhere.test/");
  const BookmarkNode* in_work = AddToBar(u"Work only", "https://work.test/");
  const BookmarkNode* in_first = AddToBar(u"First only", "https://one.test/");
  KoanBookmarkSpaces::SetSpaces(model_, in_work, {work_id});
  KoanBookmarkSpaces::SetSpaces(model_, in_first, {first_id});

  ASSERT_TRUE(base::test::RunUntil([&] { return BarShows(u"Everywhere"); }));
  EXPECT_TRUE(BarShows(u"Work only"));
  EXPECT_TRUE(BarCollapsed(u"First only"));
  EXPECT_FALSE(HidesBookmark(browser(), in_work));
  EXPECT_TRUE(HidesBookmark(browser(), in_first));

  spaces->SwitchTo(first);
  ASSERT_TRUE(base::test::RunUntil([&] { return BarShows(u"First only"); }));
  EXPECT_TRUE(BarCollapsed(u"Work only"));
  EXPECT_TRUE(BarShows(u"Everywhere"));

  // A folder of another Space hides what is in it too (the menus never reach
  // it; the suggestions leave it out).
  const BookmarkNode* folder =
      model_->AddFolder(model_->bookmark_bar_node(), 0, u"Work folder");
  const BookmarkNode* inside =
      model_->AddURL(folder, 0, u"Inside", GURL("https://inside.test/"));
  KoanBookmarkSpaces::SetSpaces(model_, folder, {work_id});
  EXPECT_TRUE(HidesBookmark(browser(), folder));
  EXPECT_TRUE(HidesBookmark(browser(), inside));
  spaces->SwitchTo(work);
  EXPECT_FALSE(HidesBookmark(browser(), inside));
}

// The URL bar's bookmark suggestions (BookmarkProvider) follow the Space too.
IN_PROC_BROWSER_TEST_F(KoanBookmarkSpacesBrowserTest,
                       SuggestionsFollowTheSpace) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  const tab_groups::TabGroupId first = *spaces->GetActiveSpace();
  WaitForSyncId(first);
  const std::string work_id = CreateSpace(u"Work");
  // The suggestions follow the active window, the one being typed in.
  browser()->GetWindow()->Activate();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser() ==
           browser();
  }));

  AddToBar(u"koan docs", "https://docs.test/");
  KoanBookmarkSpaces::SetSpaces(
      model_, AddToBar(u"koan tracker", "https://tracker.test/"), {work_id});

  EXPECT_EQ(Suggested(u"koan"),
            (std::vector<std::u16string>{u"koan docs", u"koan tracker"}));
  spaces->SwitchTo(first);
  EXPECT_EQ(Suggested(u"koan"), (std::vector<std::u16string>{u"koan docs"}));
}

// Zen: a bookmark left with no Space shows everywhere. Deleting a Space takes
// it off its bookmarks; renaming one changes nothing (they name its id).
IN_PROC_BROWSER_TEST_F(KoanBookmarkSpacesBrowserTest,
                       DeletedSpaceShowsEverywhereRenameKeepsIt) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  const tab_groups::TabGroupId first = *spaces->GetActiveSpace();
  const std::string first_id = WaitForSyncId(first);
  const std::string work_id = CreateSpace(u"Work");
  const tab_groups::TabGroupId work = *spaces->GetActiveSpace();
  const BookmarkNode* both = AddToBar(u"Both", "https://both.test/");
  const BookmarkNode* in_work = AddToBar(u"Work only", "https://work.test/");
  KoanBookmarkSpaces::SetSpaces(model_, both, {first_id, work_id});
  KoanBookmarkSpaces::SetSpaces(model_, in_work, {work_id});
  spaces->SwitchTo(first);
  EXPECT_TRUE(HidesBookmark(browser(), in_work));

  spaces->RenameSpace(work, u"Office");
  EXPECT_TRUE(HidesBookmark(browser(), in_work));
  EXPECT_EQ(KoanBookmarkSpaces::GetSpaces(in_work),
            (std::vector<std::string>{work_id}));

  // What Delete Space does once confirmed (OnDeleteConfirmed()).
  KoanBookmarkSpaces::OnSpaceDeleted(browser_view(), work);
  spaces->DeleteSpaceForSync(work);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  EXPECT_TRUE(KoanBookmarkSpaces::GetSpaces(in_work).empty());
  std::string value;
  EXPECT_FALSE(in_work->GetMetaInfo(bookmark_spaces::kMetaInfoKey, &value));
  EXPECT_EQ(KoanBookmarkSpaces::GetSpaces(both),
            (std::vector<std::string>{first_id}));
  EXPECT_FALSE(HidesBookmark(browser(), in_work));
}

// The setting off: every bookmark shows, and the Spaces are kept for later.
// The model (the bookmark manager, chrome.bookmarks) has every bookmark
// either way.
IN_PROC_BROWSER_TEST_F(KoanBookmarkSpacesBrowserTest,
                       SettingOffShowsEverything) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  const tab_groups::TabGroupId first = *spaces->GetActiveSpace();
  WaitForSyncId(first);
  const std::string work_id = CreateSpace(u"Work");
  const BookmarkNode* in_work = AddToBar(u"Work only", "https://work.test/");
  KoanBookmarkSpaces::SetSpaces(model_, in_work, {work_id});
  spaces->SwitchTo(first);
  ASSERT_TRUE(base::test::RunUntil([&] { return BarCollapsed(u"Work only"); }));
  EXPECT_EQ(model_->bookmark_bar_node()->children().size(), 1u);

  browser()->GetProfile()->GetPrefs()->SetBoolean(prefs::kBookmarksPerSpace,
                                                  false);
  EXPECT_FALSE(HidesBookmark(browser(), in_work));
  EXPECT_TRUE(base::test::RunUntil([&] { return BarShows(u"Work only"); }));
  EXPECT_EQ(KoanBookmarkSpaces::GetSpaces(in_work),
            (std::vector<std::string>{work_id}));
}

}  // namespace koan
