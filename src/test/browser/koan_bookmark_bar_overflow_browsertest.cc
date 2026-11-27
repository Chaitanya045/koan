// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// The bookmarks bar's overflow (») menu while bookmarks are removed from the
// bar (#546). The » menu lists the bar's children from the first that
// doesn't fit; when a shown bookmark goes, its button goes and the bar moves
// the menu's start index down one (BookmarkBarView::RemoveBookmarkButton() ->
// UpdateFirstHiddenNodeIndex() -> BookmarkBarMenuDelegate::
// SetMenuStartIndex()), after the bookmark has left the model. Upstream's
// SetMenuStartIndex() then read the folder as it was before the removal: with
// the » menu empty (its last bookmark deleted from its own context menu, the
// menu stays open) it read one past the end and CHECKed
// (bookmark_parent_folder_children.cc, "index < size()"), and otherwise it
// listed the menu's first bookmark twice. Upstream's bug, guarded by the
// bookmark_bar_menu_delegate.cc patch.

#include <memory>
#include <string>
#include <vector>

#include "base/memory/ptr_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service_factory.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/bookmarks/bookmark_parent_folder.h"
#include "chrome/browser/bookmarks/bookmark_test_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/bookmarks/bookmark_stats.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bar_menu_delegate.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/common/bookmark_metrics.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/koan_prefs.h"
#include "ui/base/mojom/menu_source_type.mojom-shared.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/controls/menu/menu_delegate.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/test/views_test_utils.h"
#include "url/gurl.h"

namespace koan {

namespace {

using bookmarks::BookmarkNode;

class KoanBookmarkBarOverflowBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    WaitForBookmarkMergedSurfaceServiceToLoad(
        BookmarkMergedSurfaceServiceFactory::GetForProfile(
            browser()->GetProfile()));
  }

  bookmarks::BookmarkModel* model() {
    return BookmarkModelFactory::GetForBrowserContext(browser()->GetProfile());
  }
  const BookmarkNode* bar_node() { return model()->bookmark_bar_node(); }
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  // Adds a bookmark titled `title` at the end of the bookmarks bar.
  void AddToBar(const std::string& title) {
    model()->AddURL(bar_node(), bar_node()->children().size(),
                    base::UTF8ToUTF16(title),
                    GURL("https://example.com/?" + title));
  }
  // Removes the bar's bookmark at `index`, as the bookmark manager, sync or
  // an extension (chrome.bookmarks.removeTree) does.
  void RemoveFromBar(size_t index) {
    model()->Remove(bar_node()->children()[index].get(),
                    bookmarks::metrics::BookmarkEditSource::kOther, FROM_HERE);
  }

  static std::vector<std::u16string> Titles(views::MenuItemView* menu) {
    std::vector<std::u16string> titles;
    if (menu->HasSubmenu()) {
      for (views::MenuItemView* item : menu->GetSubmenu()->GetMenuItems()) {
        titles.push_back(item->title());
      }
    }
    return titles;
  }
};

// The » menu's start index moves down after a shown bookmark was removed:
// nothing is read past the folder's end, and nothing already in the menu is
// added again.
IN_PROC_BROWSER_TEST_F(KoanBookmarkBarOverflowBrowserTest,
                       StartIndexDropsAfterShownBookmarkRemoved) {
  for (const char* title : {"a", "b", "c", "d"}) {
    AddToBar(title);
  }
  const BookmarkParentFolder bar = BookmarkParentFolder::BookmarkBarFolder();
  views::MenuDelegate real_delegate;

  {
    // The » menu after its last bookmark went: every bookmark is on the bar.
    auto delegate = std::make_unique<BookmarkBarMenuDelegate>(
        browser(), nullptr, &real_delegate, BookmarkLaunchLocation::kNone);
    delegate->SetActiveMenu(bar, 4);
    views::MenuItemView* menu = delegate->menu();
    EXPECT_TRUE(Titles(menu).empty());

    // "a" is removed; its button goes and the start index drops to 3, the
    // folder's new size.
    RemoveFromBar(0);
    delegate->SetMenuStartIndex(bar, 3);
    EXPECT_TRUE(Titles(menu).empty());

    delegate.reset();
    // The menu never showed: a MenuRunner takes it and deletes it.
    views::MenuRunner runner(base::WrapUnique(menu), 0);
  }

  {
    // Now b, c, d; the » menu holds c and d.
    auto delegate = std::make_unique<BookmarkBarMenuDelegate>(
        browser(), nullptr, &real_delegate, BookmarkLaunchLocation::kNone);
    delegate->SetActiveMenu(bar, 1);
    views::MenuItemView* menu = delegate->menu();
    EXPECT_EQ(Titles(menu), (std::vector<std::u16string>{u"c", u"d"}));

    // "b" (shown) is removed: c, d move down to 0 and 1, and the start index
    // to 0. c is in the menu already.
    RemoveFromBar(0);
    delegate->SetMenuStartIndex(bar, 0);
    EXPECT_EQ(Titles(menu), (std::vector<std::u16string>{u"c", u"d"}));

    delegate.reset();
    views::MenuRunner runner(base::WrapUnique(menu), 0);
  }
}

// The steps of #546 on the real bar: the bar overflows by one bookmark, the »
// menu is shown and that bookmark is deleted from the menu's context menu
// (the menu stays open, empty), then the bar's bookmarks are removed one by
// one. The first removal used to crash Koan.
IN_PROC_BROWSER_TEST_F(KoanBookmarkBarOverflowBrowserTest,
                       RemoveBarBookmarksAfterOverflowMenuShown) {
  browser()->GetProfile()->GetPrefs()->SetBoolean(prefs::kBookmarksBarShow,
                                                  true);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return browser_view()->IsBookmarkBarVisible(); }));
  BookmarkBarView* bar = browser_view()->GetBookmarkBarView();
  ASSERT_TRUE(bar);

  // Bookmarks until one doesn't fit: the » button shows.
  for (int i = 0; i < 60 && !bar->overflow_button()->GetVisible(); ++i) {
    AddToBar(base::StringPrintf("Bookmark %02d title", i));
    views::test::RunScheduledLayout(browser_view());
  }
  ASSERT_TRUE(bar->overflow_button()->GetVisible());

  // The » menu lists the one that didn't fit.
  bar->overflow_button()->Activate(nullptr);
  views::MenuItemView* menu = bar->GetMenu();
  ASSERT_TRUE(menu);
  ASSERT_EQ(Titles(menu).size(), 1u);

  // Right-click > Delete on it, in the menu.
  views::MenuItemView* item = menu->GetSubmenu()->GetMenuItemAt(0);
  ASSERT_TRUE(menu->GetDelegate()->ShowContextMenu(
      item, item->GetCommand(), gfx::Point(),
      ui::mojom::MenuSourceType::kMouse));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return bar->GetContextMenu() != nullptr; }));
  const size_t shown = bar_node()->children().size() - 1;
  bar->GetContextMenu()->GetDelegate()->ExecuteCommand(IDC_BOOKMARK_BAR_REMOVE,
                                                       0);
  ASSERT_EQ(bar_node()->children().size(), shown);
  views::test::RunScheduledLayout(browser_view());
  // The » menu is still open (its controller is alive), now empty.
  ASSERT_EQ(bar->GetMenu(), menu);
  EXPECT_FALSE(bar->overflow_button()->GetVisible());

  // Remove the shown bookmarks, the first each time.
  while (!bar_node()->children().empty()) {
    RemoveFromBar(0);
    views::test::RunScheduledLayout(browser_view());
  }
  EXPECT_FALSE(bar->overflow_button()->GetVisible());
  EXPECT_FALSE(bar->GetMenu());
}

}  // namespace

}  // namespace koan
