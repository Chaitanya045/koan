// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Split views: Koan's pinned tabs (a split view is pinned as one, in its
// window and, through window sync, in the others) and Zen's one-tab split
// (an empty pane with the URL bar, koan_split_view.h). Pages are chrome://
// ones, which load without a server.

#include <optional>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_context.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/koan_shortcut_table.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_folder_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_shortcuts.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_split_header.h"
#include "koan/browser/ui/views/koan_split_rearrange.h"
#include "koan/browser/ui/views/koan_split_view.h"
#include "koan/browser/ui/views/koan_tab_drag.h"
#include "koan/browser/ui/views/koan_url_bar_controller.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace koan {

namespace {

// The window's tabs pinned in their Space.
int CountPinnedInSpace(BrowserWindowInterface* browser) {
  TabStripModel* tab_strip = browser->GetTabStripModel();
  int count = 0;
  for (int i = 0; i < tab_strip->count(); ++i) {
    count += IsPinnedInSpace(tab_strip->GetTabAtIndex(i)) ? 1 : 0;
  }
  return count;
}

// The window's Koan URL bar.
KoanUrlBarController* UrlBar(BrowserWindowInterface* browser) {
  KoanBrowserViewController* controller = KoanBrowserViewController::From(
      BrowserView::GetBrowserViewForBrowser(browser));
  return controller ? controller->url_bar() : nullptr;
}

// Zen's one-tab split, as every "new split" path asks for it.
void NewEmptySplit(BrowserWindowInterface* browser) {
  chrome::NewSplitTab(browser, split_tabs::SplitTabLayout::kSideBySide,
                      split_tabs::SplitTabCreatedSource::kKeyboardShortcut);
}

std::vector<tabs::TabInterface*> SplitTabs(TabStripModel* tab_strip,
                                           tabs::TabInterface* tab) {
  return tab->IsSplit() ? tab_strip->GetSplitData(*tab->GetSplit())->ListTabs()
                        : std::vector<tabs::TabInterface*>();
}

// Two pages, `first` and `second`, in a side by side split at `ratio`, the
// second selected.
struct TwoPanes {
  raw_ptr<tabs::TabInterface> first = nullptr;
  raw_ptr<tabs::TabInterface> second = nullptr;
};
TwoPanes SplitTwoPages(BrowserWindowInterface* browser, double ratio) {
  TabStripModel* tab_strip = browser->GetTabStripModel();
  TwoPanes panes;
  if (!ui_test_utils::NavigateToURL(browser, GURL("chrome://version/"))) {
    return panes;
  }
  panes.first = tab_strip->GetActiveTab();
  if (!ui_test_utils::NavigateToURLWithDisposition(
          browser, GURL("chrome://chrome-urls/"),
          WindowOpenDisposition::kNewForegroundTab,
          ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP)) {
    return panes;
  }
  panes.second = tab_strip->GetActiveTab();
  tab_strip->AddToNewSplit({tab_strip->GetIndexOfTab(panes.first)},
                           split_tabs::SplitTabVisualData(
                               split_tabs::SplitTabLayout::kSideBySide, ratio),
                           split_tabs::SplitTabCreatedSource::kTabContextMenu);
  if (SplitTabs(tab_strip, panes.second) !=
      std::vector<tabs::TabInterface*>{panes.first, panes.second}) {
    // AddToNewSplit() put the active tab first.
    tab_strip->ReverseTabsInSplit(*panes.second->GetSplit());
  }
  tab_strip->ActivateTabAt(tab_strip->GetIndexOfTab(panes.second));
  return panes;
}

// The window's child widget named `name` (the split header's or the
// rearrange view's).
views::Widget* ChildWidgetNamed(BrowserView* browser_view,
                                std::string_view name) {
  for (views::Widget* widget : views::Widget::GetAllChildWidgets(
           browser_view->GetWidget()->GetNativeView())) {
    if (widget->GetName() == name) {
      return widget;
    }
  }
  return nullptr;
}

// Whether the child widget named `name` is shown with its contents.
bool ChildWidgetShows(BrowserView* browser_view, std::string_view name) {
  views::Widget* widget = ChildWidgetNamed(browser_view, name);
  return widget && widget->IsVisible() && widget->GetContentsView() &&
         widget->GetContentsView()->GetVisible();
}

const split_tabs::SplitTabVisualData& VisualData(TabStripModel* tab_strip,
                                                 tabs::TabInterface* tab) {
  return *tab_strip->GetSplitData(*tab->GetSplit())->visual_data();
}

// The split shortcut (Cmd+Option+V, Ctrl+Alt+V on Linux) in `browser`.
bool RunSplitShortcut(BrowserWindowInterface* browser) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  return browser_view && KoanBrowserViewController::From(browser_view)
                             ->shortcuts()
                             ->Run(shortcuts::Action::kSplitVertical);
}

// A first Space with a page at `url`, active, and a "Work" Space after it
// in the tab strip with chrome://chrome-urls/ in it. Returns the Work tab,
// or null.
tabs::TabInterface* PageThenWorkSpace(BrowserWindowInterface* browser,
                                      const GURL& url) {
  KoanWorkspaceController* spaces = GetSpaces(browser);
  TabStripModel* tab_strip = browser->GetTabStripModel();
  if (!spaces || !WaitForSpaceCount(browser, 1) ||
      !ui_test_utils::NavigateToURL(browser, url)) {
    return nullptr;
  }
  tabs::TabInterface* page = tab_strip->GetActiveTab();
  spaces->CreateSpace(u"Work", u"", {});
  if (!WaitForSpaceCount(browser, 2) ||
      !ui_test_utils::NavigateToURLWithDisposition(
          browser, GURL("chrome://chrome-urls/"),
          WindowOpenDisposition::kNewForegroundTab,
          ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP)) {
    return nullptr;
  }
  tabs::TabInterface* work = tab_strip->GetActiveTab();
  if (work == page || !work->GetGroup() ||
      work->GetGroup() == page->GetGroup()) {
    return nullptr;
  }
  spaces->SwitchTo(*page->GetGroup());
  tab_strip->ActivateTabAt(tab_strip->GetIndexOfTab(page));
  return tab_strip->GetActiveTab() == page ? work : nullptr;
}

}  // namespace

using KoanSplitViewBrowserTest = InProcessBrowserTest;

// Zen: splitting a pinned tab with a normal one pins both. Picking the pinned
// tab as the second tab of the active normal tab's split (upstream's split
// tab picker, or "Add Split View..." on the normal tab) gives both upstream's
// state of the active tab, unpinned; Koan pins the split. Its tabs stay
// synced, so another window has both pinned (a split's tabs used to leave
// window sync, and the other windows closed their copy of the pinned tab).
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       SplitWithAPinnedTabPinsBothEverywhere) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version/")));
  tabs::TabInterface* pinned = tab_strip->GetActiveTab();
  GetPinnedTabs(browser())->PinInSpace(pinned);
  ASSERT_TRUE(IsPinnedInSpace(pinned));
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("chrome://chrome-urls/"),
      WindowOpenDisposition::kNewForegroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  tabs::TabInterface* normal = tab_strip->GetActiveTab();
  ASSERT_FALSE(IsPinnedForUser(normal));

  tab_strip->AddToNewSplit({tab_strip->GetIndexOfTab(pinned)},
                           split_tabs::SplitTabVisualData(),
                           split_tabs::SplitTabCreatedSource::kTabContextMenu);
  ASSERT_TRUE(pinned->IsSplit());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return IsPinnedInSpace(pinned) && IsPinnedInSpace(normal); }));
  EXPECT_EQ(pinned->GetSplit(), normal->GetSplit());

  BrowserWindowInterface* other = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(WaitForSpaceCount(other, 1));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return CountPinnedInSpace(other) == 2; }));
  // Still pinned and split where the split was made.
  EXPECT_TRUE(IsPinnedInSpace(pinned));
  EXPECT_TRUE(IsPinnedInSpace(normal));
  EXPECT_TRUE(pinned->IsSplit());
}

// Zen's createEmptySplit(): the Space's empty tab joins the tab on its
// right and is selected, with the URL bar open over it as for a new tab.
// Never upstream's tab picker page. Another new split from the empty pane
// does nothing (Zen disables cmd_zenNewEmptySplit there).
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       NewSplitTabOpensAnEmptyPaneWithTheUrlBar) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version/")));
  tabs::TabInterface* page = tab_strip->GetActiveTab();

  NewEmptySplit(browser());
  tabs::TabInterface* pane = tab_strip->GetActiveTab();
  ASSERT_NE(pane, page);
  EXPECT_TRUE(IsEmptyTab(pane));
  EXPECT_EQ(SplitTabs(tab_strip, page),
            (std::vector<tabs::TabInterface*>{page, pane}));
  EXPECT_EQ(pane->GetGroup(), page->GetGroup());
  ASSERT_TRUE(UrlBar(browser()));
  EXPECT_TRUE(UrlBar(browser())->is_new_tab());
  for (int i = 0; i < tab_strip->count(); ++i) {
    EXPECT_NE(tab_strip->GetWebContentsAt(i)->GetVisibleURL().spec(),
              chrome::kChromeUISplitViewNewTabPageURL);
  }

  const int count = tab_strip->count();
  NewEmptySplit(browser());
  EXPECT_EQ(tab_strip->count(), count);
  EXPECT_EQ(tab_strip->GetActiveTab(), pane);
  EXPECT_EQ(SplitTabs(tab_strip, page).size(), 2u);
}

// What the URL bar opens from the empty pane (a new tab, as the empty tab
// never navigates) takes the pane's place in the split, keeps its layout, and
// is selected; the pane goes.
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       TabOpenedFromTheEmptyPaneReplacesIt) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version/")));
  tabs::TabInterface* page = tab_strip->GetActiveTab();
  NewEmptySplit(browser());
  ASSERT_TRUE(UrlBar(browser())->is_new_tab());
  const tabs::TabHandle pane = tab_strip->GetActiveTab()->GetHandle();

  ui_test_utils::SendToOmniboxAndSubmit(browser(), "chrome://chrome-urls/");
  ASSERT_TRUE(base::test::RunUntil([&] {
    tabs::TabInterface* active = tab_strip->GetActiveTab();
    return active && active != page && active->IsSplit() &&
           active->GetSplit() == page->GetSplit() &&
           active->GetContents()->GetVisibleURL() ==
               GURL("chrome://chrome-urls/");
  }));
  tabs::TabInterface* opened = tab_strip->GetActiveTab();
  EXPECT_EQ(SplitTabs(tab_strip, page),
            (std::vector<tabs::TabInterface*>{page, opened}));
  EXPECT_EQ(
      tab_strip->GetSplitData(*page->GetSplit())->visual_data()->split_layout(),
      split_tabs::SplitTabLayout::kSideBySide);
  EXPECT_TRUE(base::test::RunUntil([&] { return !pane.Get(); }));
  EXPECT_FALSE(UrlBar(browser())->is_new_tab());
}

// The URL bar closed without opening anything (Esc, a click on the page):
// the empty pane leaves the split, and the tab is selected again.
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       ClosingTheUrlBarTakesTheEmptyPaneAway) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version/")));
  tabs::TabInterface* page = tab_strip->GetActiveTab();
  NewEmptySplit(browser());
  ASSERT_TRUE(UrlBar(browser())->is_new_tab());
  tabs::TabInterface* pane = tab_strip->GetActiveTab();

  UrlBar(browser())->Close();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !page->IsSplit() && tab_strip->GetActiveTab() == page; }));
  EXPECT_FALSE(UrlBar(browser())->is_new_tab());
  // The Space's empty tab stays, hidden, as before.
  EXPECT_FALSE(pane->IsSplit());
  EXPECT_TRUE(IsEmptyTab(pane));
}

// The pane header's unsplit button (Zen's removeTabFromSplit()): the pane's
// tab leaves the split and is selected; with Shift the selection stays.
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       HeaderUnsplitSelectsThePaneUnlessShift) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  TwoPanes panes = SplitTwoPages(browser(), 0.5);
  ASSERT_TRUE(panes.first && panes.second && panes.first->IsSplit());
  ASSERT_EQ(tab_strip->GetActiveTab(), panes.second);

  UnsplitFromHeader(tab_strip, panes.first, /*keep_selection=*/false);
  EXPECT_FALSE(panes.first->IsSplit());
  EXPECT_FALSE(panes.second->IsSplit());
  EXPECT_EQ(tab_strip->GetActiveTab(), panes.first);

  tab_strip->AddToNewSplit({tab_strip->GetIndexOfTab(panes.second)},
                           split_tabs::SplitTabVisualData(),
                           split_tabs::SplitTabCreatedSource::kTabContextMenu);
  ASSERT_TRUE(panes.first->IsSplit());
  UnsplitFromHeader(tab_strip, panes.second, /*keep_selection=*/true);
  EXPECT_FALSE(panes.first->IsSplit());
  EXPECT_EQ(tab_strip->GetActiveTab(), panes.first);
}

// Zen's rearrange drop through upstream's split APIs. The centre swaps the
// panes and each slot keeps its size: this also guards the upstream
// behaviour Koan relies on, that ReverseTabsInSplit() moves the tabs and
// leaves the split's ratio (the first slot's share) alone. An edge stacks or
// sets side by side at half, the dragged pane on that side.
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       HeaderRearrangeSwapsOrMovesThePane) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  TwoPanes panes = SplitTwoPages(browser(), 0.3);
  ASSERT_TRUE(panes.first && panes.second && panes.first->IsSplit());
  tabs::TabInterface* first = panes.first;
  tabs::TabInterface* second = panes.second;

  RearrangeSplitPanes(tab_strip, first, RearrangeSide::kCenter,
                      /*rtl=*/false);
  EXPECT_EQ(SplitTabs(tab_strip, first),
            (std::vector<tabs::TabInterface*>{second, first}));
  EXPECT_EQ(VisualData(tab_strip, first),
            split_tabs::SplitTabVisualData(
                split_tabs::SplitTabLayout::kSideBySide, 0.3));
  // The selection doesn't move.
  EXPECT_EQ(tab_strip->GetActiveTab(), second);

  // `first` (now on the right) dropped on the top of `second`: stacked,
  // `first` on top.
  RearrangeSplitPanes(tab_strip, first, RearrangeSide::kTop, false);
  EXPECT_EQ(SplitTabs(tab_strip, first),
            (std::vector<tabs::TabInterface*>{first, second}));
  EXPECT_EQ(VisualData(tab_strip, first),
            split_tabs::SplitTabVisualData(split_tabs::SplitTabLayout::kStacked,
                                           0.5));

  // `first` dropped on the right of `second`: side by side again, `first`
  // on the right.
  RearrangeSplitPanes(tab_strip, first, RearrangeSide::kRight, false);
  EXPECT_EQ(SplitTabs(tab_strip, first),
            (std::vector<tabs::TabInterface*>{second, first}));
  EXPECT_EQ(VisualData(tab_strip, first),
            split_tabs::SplitTabVisualData(
                split_tabs::SplitTabLayout::kSideBySide, 0.5));
  EXPECT_TRUE(first->IsSplit());
  EXPECT_EQ(first->GetSplit(), second->GetSplit());
}

// Zen's split by drag (koan_tab_drag.h): the dragged tab and the one it was
// dropped on (a row's half, or the page the tab came from) make a split, the
// dragged one on its side and selected; a tab already split gets the limit
// toast instead (Koan's splits take two).
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       DroppingATabToSplitPutsItOnItsSide) {
  TabStripModel* model = browser()->GetTabStripModel();
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), GURL("about:blank"),
        WindowOpenDisposition::kNewForegroundTab,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  }
  const int count = model->count();
  tabs::TabInterface* a = model->GetTabAtIndex(count - 3);
  tabs::TabInterface* b = model->GetTabAtIndex(count - 2);
  tabs::TabInterface* c = model->GetTabAtIndex(count - 1);
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());

  // Dropped on b's right half: b then a, side by side, a selected.
  ASSERT_TRUE(SplitByDrag(browser_view, a, b,
                          split_tabs::SplitTabLayout::kSideBySide,
                          /*dragged_first=*/false));
  ASSERT_TRUE(a->IsSplit());
  EXPECT_EQ(a->GetSplit(), b->GetSplit());
  const split_tabs::SplitTabData* split = model->GetSplitData(*a->GetSplit());
  EXPECT_EQ(b, split->ListTabs().front());
  EXPECT_EQ(split_tabs::SplitTabLayout::kSideBySide,
            split->visual_data()->split_layout());
  EXPECT_EQ(a, model->GetActiveTab());

  // c onto the split: no third pane.
  EXPECT_FALSE(SplitByDrag(browser_view, c, a,
                           split_tabs::SplitTabLayout::kSideBySide, true));
  EXPECT_FALSE(c->IsSplit());

  // Unsplit, then c on the page's top edge over b: c on top, stacked.
  model->RemoveSplit(*a->GetSplit());
  ASSERT_TRUE(SplitByDrag(browser_view, c, b,
                          split_tabs::SplitTabLayout::kStacked,
                          /*dragged_first=*/true));
  split = model->GetSplitData(*c->GetSplit());
  EXPECT_EQ(c, split->ListTabs().front());
  EXPECT_EQ(split_tabs::SplitTabLayout::kStacked,
            split->visual_data()->split_layout());
  EXPECT_EQ(c, model->GetActiveTab());
}

// Zen's drop of a split pane on the sidebar (handleTabDrop()), from the
// pane header's grip: over a tab's row the line shows, and the drop takes
// the pane's tab out of the split to that place, selected.
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       APaneDroppedOnTheSidebarLeavesTheSplitThere) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* model = browser()->GetTabStripModel();
  TwoPanes panes = SplitTwoPages(browser(), 0.5);
  ASSERT_TRUE(panes.first && panes.second && panes.first->IsSplit());
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("chrome://version/"),
      WindowOpenDisposition::kNewBackgroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  tabs::TabInterface* third = model->GetTabAtIndex(model->count() - 1);
  ASSERT_FALSE(third->IsSplit());
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  KoanTabDrag* drag = GetSpaces(browser())->tab_drag();
  ASSERT_TRUE(drag);

  // The top of the third tab's row: the line above it.
  views::View* row = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    browser_view->GetWidget()->LayoutRootViewIfNecessary();
    TabDragContext* context = browser_view->tab_strip_view()->GetDragContext();
    row = context ? context->GetTabForContents(third->GetContents()) : nullptr;
    return row && row->IsDrawn() && !row->bounds().IsEmpty();
  }));
  const gfx::Rect bounds = row->GetBoundsInScreen();
  const gfx::Point point(bounds.CenterPoint().x(), bounds.y() + 4);
  EXPECT_TRUE(drag->UpdatePaneDrag(panes.first, point));
  EXPECT_FALSE(drag->IsDragging());

  drag->DropPane(panes.first, /*keep_selection=*/false);
  EXPECT_FALSE(panes.first->IsSplit());
  EXPECT_FALSE(panes.second->IsSplit());
  EXPECT_EQ(model->GetIndexOfTab(panes.first) + 1, model->GetIndexOfTab(third));
  EXPECT_EQ(panes.first, model->GetActiveTab());

  // Off the sidebar nothing shows, and a drop does nothing.
  model->AddToNewSplit({model->GetIndexOfTab(panes.second)},
                       split_tabs::SplitTabVisualData(),
                       split_tabs::SplitTabCreatedSource::kTabContextMenu);
  ASSERT_TRUE(panes.first->IsSplit());
  const gfx::Point page =
      browser_view->multi_contents_view()->GetBoundsInScreen().CenterPoint();
  EXPECT_FALSE(drag->UpdatePaneDrag(panes.first, page));
  drag->DropPane(panes.first, /*keep_selection=*/false);
  EXPECT_TRUE(panes.first->IsSplit());
}

// The Mac run (cycle 17, split_header): the first header never showed, and
// the first grip drag had no rearrange view. On macOS a control widget is
// shown by Init() (NativeWidgetMacNSWindowHost::InitWindow()), and the
// controller showed the view only when its widget was hidden. Each widget is
// hidden with its view now, and both show from the first time. (Off macOS
// this passes either way: it guards that upstream behaviour.)
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       TheHeaderAndTheRearrangeViewShowTheFirstTime) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TwoPanes panes = SplitTwoPages(browser(), 0.5);
  ASSERT_TRUE(panes.first && panes.second && panes.first->IsSplit());
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  KoanSplitHeaderController* header =
      KoanBrowserViewController::From(browser_view)->split_header();
  ASSERT_TRUE(header);
  EXPECT_FALSE(ChildWidgetNamed(browser_view, "KoanSplitHeader"));

  // The header's own hover (Zen's :hover) shows the first pane's.
  EXPECT_TRUE(base::test::RunUntil([&] {
    header->OnHeaderHovered(0, true);
    return ChildWidgetShows(browser_view, "KoanSplitHeader");
  }));
  // Its grip's drag shows the rearrange view.
  EXPECT_TRUE(base::test::RunUntil([&] {
    header->StartRearrange(0);
    return ChildWidgetShows(browser_view, "KoanSplitRearrange");
  }));
  header->CancelRearrange();
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !ChildWidgetShows(browser_view, "KoanSplitRearrange"); }));
  header->OnHeaderHovered(0, false);
}

// The Mac run (cycle 18, split_header, after an unsplit and a split again):
// the header's log said it showed, yet its view was hidden, so no header
// showed and its grip took no press. On macOS AppKit can order a hidden
// child window back on screen with its parent, and Views then counts it as
// shown (NativeWidgetNSWindowBridge::OnVisibilityChanged()). Such a window is
// hidden again while its header isn't wanted, and a header that is wanted
// shows its view whatever its window says.
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       AHeaderShowsWhateverItsWindowSays) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TwoPanes panes = SplitTwoPages(browser(), 0.5);
  ASSERT_TRUE(panes.first && panes.second && panes.first->IsSplit());
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  KoanSplitHeaderController* header =
      KoanBrowserViewController::From(browser_view)->split_header();
  ASSERT_TRUE(header);

  // Shown once, then gone: its widget is hidden with its view.
  ASSERT_TRUE(base::test::RunUntil([&] {
    header->OnHeaderHovered(0, true);
    return ChildWidgetShows(browser_view, "KoanSplitHeader");
  }));
  header->OnHeaderHovered(0, false);
  views::Widget* widget = ChildWidgetNamed(browser_view, "KoanSplitHeader");
  ASSERT_TRUE(widget);
  if (!base::test::RunUntil([&] { return !widget->IsVisible(); })) {
    GTEST_SKIP() << "the header stays: the pointer rests over its pane";
  }
  ASSERT_FALSE(widget->GetContentsView()->GetVisible());

  // Shown from outside, as AppKit does: hidden again.
  widget->ShowInactive();
  EXPECT_TRUE(base::test::RunUntil([&] { return !widget->IsVisible(); }));

  // Its window shown with its view hidden, and the header wanted: the view
  // shows too.
  widget->ShowInactive();
  header->OnHeaderHovered(0, true);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return ChildWidgetShows(browser_view, "KoanSplitHeader"); }));
  header->OnHeaderHovered(0, false);
}

// Issue #553: the split shortcut on the only tab of a Space, with another
// Space's tab next in the tab strip, never takes that tab (Zen's
// toggleShortcut() only looks at the visible tabs, the Space's). It opens
// Zen's one-tab split in this Space, and the other Space keeps its tab.
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       SplitShortcutNeverTakesATabOfTheNextSpace) {
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  tabs::TabInterface* work =
      PageThenWorkSpace(browser(), GURL("chrome://version/"));
  ASSERT_TRUE(work);
  tabs::TabInterface* page = tab_strip->GetActiveTab();
  const std::optional<tab_groups::TabGroupId> work_space = work->GetGroup();
  ASSERT_GT(tab_strip->GetIndexOfTab(work), tab_strip->GetIndexOfTab(page));

  ASSERT_TRUE(RunSplitShortcut(browser()));
  EXPECT_FALSE(work->IsSplit());
  EXPECT_EQ(work->GetGroup(), work_space);
  ASSERT_TRUE(page->IsSplit());
  const std::vector<tabs::TabInterface*> panes = SplitTabs(tab_strip, page);
  ASSERT_EQ(panes.size(), 2u);
  for (tabs::TabInterface* pane : panes) {
    EXPECT_EQ(pane->GetGroup(), page->GetGroup());
  }
}

// Zen's toggleShortcut(): on the last tab of a Space the shortcut wraps to
// the Space's first tab, not the next Space's.
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       SplitShortcutWrapsToTheSpacesFirstTab) {
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  tabs::TabInterface* work =
      PageThenWorkSpace(browser(), GURL("chrome://version/"));
  ASSERT_TRUE(work);
  tabs::TabInterface* first = tab_strip->GetActiveTab();
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("chrome://chrome-urls/"),
      WindowOpenDisposition::kNewForegroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  tabs::TabInterface* last = tab_strip->GetActiveTab();
  ASSERT_EQ(last->GetGroup(), first->GetGroup());
  ASSERT_GT(tab_strip->GetIndexOfTab(work), tab_strip->GetIndexOfTab(last));

  ASSERT_TRUE(RunSplitShortcut(browser()));
  EXPECT_FALSE(work->IsSplit());
  ASSERT_TRUE(last->IsSplit());
  EXPECT_EQ(last->GetSplit(), first->GetSplit());
}

// Zen's visibleTabs leave out a collapsed folder's tabs: on the last tab of
// a Space the shortcut skips a tab in a folder nested in a collapsed one
// (Koan's folders aren't tab groups, so upstream's IsTabCollapsed() doesn't
// know them) and wraps to the Space's first shown tab.
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       SplitShortcutSkipsATabInACollapsedFolder) {
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  tabs::TabInterface* work =
      PageThenWorkSpace(browser(), GURL("chrome://version/"));
  ASSERT_TRUE(work);
  tabs::TabInterface* home = tab_strip->GetActiveTab();
  KoanFolderController* folders = KoanFolderController::FromBrowser(browser());
  ASSERT_TRUE(folders);
  std::vector<tabs::TabInterface*> pages;
  for (int i = 0; i < 2; ++i) {
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), GURL("chrome://chrome-urls/"),
        WindowOpenDisposition::kNewForegroundTab,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    pages.push_back(tab_strip->GetActiveTab());
  }
  tabs::TabInterface* in_folder = pages[0];
  tabs::TabInterface* last = pages[1];
  const int outer = folders->CreateFolder({}, /*parent=*/0,
                                          folders->GetActiveSpace(), u"Outer",
                                          /*rename=*/false);
  folders->CreateFolder({in_folder}, outer, folders->GetActiveSpace(), u"Inner",
                        /*rename=*/false);
  tab_strip->ActivateTabAt(tab_strip->GetIndexOfTab(last));
  folders->SetCollapsed(outer, true);
  // The folder's tab moved to the top of the Space, ahead of the others.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return tab_strip->GetIndexOfTab(in_folder) < tab_strip->GetIndexOfTab(home);
  }));
  ASSERT_EQ(tab_strip->GetActiveTab(), last);
  ASSERT_TRUE(folders->GetRootMostCollapsed(folders->GetFolderOf(in_folder)));
  ASSERT_GT(tab_strip->GetIndexOfTab(work), tab_strip->GetIndexOfTab(last));

  ASSERT_TRUE(RunSplitShortcut(browser()));
  EXPECT_FALSE(in_folder->IsSplit());
  EXPECT_FALSE(work->IsSplit());
  ASSERT_TRUE(last->IsSplit());
  EXPECT_EQ(last->GetSplit(), home->GetSplit());
  EXPECT_TRUE(folders->GetFolder(outer)->collapsed);
}

}  // namespace koan
