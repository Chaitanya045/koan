// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// History per Space profile (#474, .agent/notes/upstream/space-history.md):
// a visit records its tab's profile, the URL bar suggests a profile's pages
// and tabs in its Spaces only, and the Library's queries and removals act on
// one profile or on all.

#include <optional>
#include <set>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_history_profiles.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_space_history.h"
#include "koan/browser/ui/views/koan_space_history_index.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace koan {

namespace {

class KoanSpaceHistoryBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    ui_test_utils::WaitForHistoryToLoad(history());
  }

 protected:
  Profile* profile() { return browser()->GetProfile(); }

  history::HistoryService* history() {
    return HistoryServiceFactory::GetForProfile(
        profile(), ServiceAccessType::EXPLICIT_ACCESS);
  }

  // A new profile (New Profile...), Serialize()d.
  std::string AddProfile(const std::u16string& name) {
    ScopedListPrefUpdate update(profile()->GetPrefs(), prefs::kUserContainers);
    std::optional<containers::UserContainer> container =
        containers::AddUserContainer(update.Get(), name);
    return container ? containers::Serialize(container->id) : std::string();
  }

  // Opens `url` in a new foreground tab as the URL bar does (typed, so the
  // URL bar's in-memory history has it at once).
  content::WebContents* OpenTyped(const GURL& url) {
    NavigateParams params(browser(), url, ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::kNewForegroundTab;
    ui_test_utils::NavigateToURL(&params);
    return params.navigated_or_inserted_contents;
  }

  // The pages of history (every visit) with `app_id` (nullopt: any).
  std::set<GURL> Pages(std::optional<std::string> app_id) {
    history::QueryOptions options;
    options.duplicate_policy = history::QueryOptions::KEEP_ALL_DUPLICATES;
    options.app_id = std::move(app_id);
    base::test::TestFuture<history::QueryResults> results;
    base::CancelableTaskTracker tracker;
    history()->QueryHistory(u"", options, results.GetCallback(), &tracker);
    std::set<GURL> pages;
    for (const history::URLResult& result : results.Take()) {
      pages.insert(result.url());
    }
    return pages;
  }

  AutocompleteController* omnibox() {
    return BrowserWindow::FromBrowser(browser())
        ->GetLocationBar()
        ->GetOmniboxController()
        ->autocomplete_controller();
  }

  // Whether the URL bar, given `text`, suggests a page on `host`.
  bool Suggests(const std::u16string& text, const std::string& host) {
    omnibox()->Start(
        AutocompleteInput(text, metrics::OmniboxEventProto::OTHER,
                          ChromeAutocompleteSchemeClassifier(profile())));
    ui_test_utils::WaitForAutocompleteDone(browser());
    for (const AutocompleteMatch& match : omnibox()->result()) {
      if (match.destination_url.host() == host) {
        return true;
      }
    }
    return false;
  }

  // Suggests(), after the history providers have had the time to see the
  // visit (they hear of it from the same notification as the index).
  bool EventuallySuggests(const std::u16string& text, const std::string& host) {
    for (int attempt = 0; attempt < 50; ++attempt) {
      if (Suggests(text, host)) {
        return true;
      }
      base::RunLoop run_loop;
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(100));
      run_loop.Run();
    }
    return false;
  }

  // A visit to work.example in a Space with the Work profile, and to
  // home.example in the first Space (none). Leaves the Work Space active.
  void VisitInTwoProfiles() {
    spaces_ = GetSpaces(browser());
    ASSERT_TRUE(spaces_);
    ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
    first_ = spaces_->GetSpaces()[0].id;
    work_ = AddProfile(u"Work");
    ASSERT_FALSE(work_.empty());
    home_url_ = embedded_test_server()->GetURL("home.example", "/title1.html");
    work_url_ = embedded_test_server()->GetURL("work.example", "/title2.html");
    ASSERT_TRUE(OpenTyped(home_url_));
    spaces_->CreateSpace(u"Work", u"", {}, work_);
    ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
    work_space_ = *spaces_->GetActiveSpace();
    ASSERT_TRUE(OpenTyped(work_url_));
    // The index has heard of both.
    SpaceHistoryIndex* index = SpaceHistoryIndex::For(profile());
    ASSERT_TRUE(index);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return index->loaded() && index->ShowsPage(work_url_, work_) &&
             index->ShowsPage(home_url_, "");
    }));
  }

  raw_ptr<KoanWorkspaceController> spaces_ = nullptr;
  tab_groups::TabGroupId first_ = tab_groups::TabGroupId::CreateEmpty();
  tab_groups::TabGroupId work_space_ = tab_groups::TabGroupId::CreateEmpty();
  std::string work_;
  GURL home_url_;
  GURL work_url_;
};

}  // namespace

// Each visit records its tab's profile; a Space without one records none,
// which is the default profile's.
IN_PROC_BROWSER_TEST_F(KoanSpaceHistoryBrowserTest, VisitsRecordTheirProfile) {
  ASSERT_NO_FATAL_FAILURE(VisitInTwoProfiles());
  const std::string tag = containers::HistoryAppIdForProfile(work_);
  EXPECT_EQ(std::set<GURL>{work_url_}, Pages(tag));
  const std::set<GURL> untagged = Pages(std::string());
  EXPECT_TRUE(untagged.contains(home_url_));
  EXPECT_FALSE(untagged.contains(work_url_));
  // Every profile's (chrome://history, Clear browsing data).
  const std::set<GURL> all = Pages(std::nullopt);
  EXPECT_TRUE(all.contains(home_url_));
  EXPECT_TRUE(all.contains(work_url_));
}

// The reviewer's check: a page visited in Work is suggested in Work's Space,
// not in the Space without a profile, and the other way round.
IN_PROC_BROWSER_TEST_F(KoanSpaceHistoryBrowserTest,
                       SuggestionsComeFromTheSpacesProfile) {
  ASSERT_NO_FATAL_FAILURE(VisitInTwoProfiles());
  ASSERT_EQ(work_space_, spaces_->GetActiveSpace());
  EXPECT_EQ(work_, HistoryProfileOfWindow(browser()));
  EXPECT_TRUE(EventuallySuggests(u"work.ex", "work.example"));
  EXPECT_FALSE(Suggests(u"home.ex", "home.example"));

  spaces_->SwitchTo(first_);
  EXPECT_EQ("", HistoryProfileOfWindow(browser()));
  EXPECT_TRUE(EventuallySuggests(u"home.ex", "home.example"));
  EXPECT_FALSE(Suggests(u"work.ex", "work.example"));

  // Off, everything shows everywhere, as before #474.
  profile()->GetPrefs()->SetBoolean(prefs::kSpacesSeparateHistory, false);
  EXPECT_TRUE(Suggests(u"work.ex", "work.example"));
  spaces_->SwitchTo(work_space_);
  EXPECT_TRUE(Suggests(u"home.ex", "home.example"));
}

// Switch to Tab offers the tabs of the profile being typed in only.
IN_PROC_BROWSER_TEST_F(KoanSpaceHistoryBrowserTest, SwitchToTabStaysInProfile) {
  ASSERT_NO_FATAL_FAILURE(VisitInTwoProfiles());
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  content::WebContents* work_tab = tab_strip->GetActiveWebContents();
  ASSERT_EQ(containers::SpaceContainerFromString(work_),
            containers::ContainerOf(work_tab));
  content::WebContents* home_tab = nullptr;
  for (int i = 0; i < tab_strip->count(); ++i) {
    if (tab_strip->GetWebContentsAt(i)->GetLastCommittedURL() == home_url_) {
      home_tab = tab_strip->GetWebContentsAt(i);
    }
  }
  ASSERT_TRUE(home_tab);

  EXPECT_FALSE(IsOutsideOmniboxProfile(profile(), work_tab));
  EXPECT_TRUE(IsOutsideOmniboxProfile(profile(), home_tab));
  spaces_->SwitchTo(first_);
  EXPECT_TRUE(IsOutsideOmniboxProfile(profile(), work_tab));
  EXPECT_FALSE(IsOutsideOmniboxProfile(profile(), home_tab));

  // A tab moved to another Space keeps its profile.
  spaces_->MoveTabsToSpace({tabs::TabInterface::GetFromContents(work_tab)},
                           first_);
  EXPECT_TRUE(IsOutsideOmniboxProfile(profile(), work_tab));

  profile()->GetPrefs()->SetBoolean(prefs::kSpacesSeparateHistory, false);
  EXPECT_FALSE(IsOutsideOmniboxProfile(profile(), work_tab));
}

// The Library's profile filter: it queries one profile's visits by their tag,
// and removes one profile's visits to a page, leaving the other's.
IN_PROC_BROWSER_TEST_F(KoanSpaceHistoryBrowserTest, LibraryFilterActsOnOne) {
  ASSERT_NO_FATAL_FAILURE(VisitInTwoProfiles());
  // home.example again, in Work: now in both profiles.
  ASSERT_TRUE(OpenTyped(home_url_));
  const std::string tag = containers::HistoryAppIdForProfile(work_);
  EXPECT_EQ((std::set<GURL>{home_url_, work_url_}), Pages(tag));
  SpaceHistoryIndex* index = SpaceHistoryIndex::For(profile());
  EXPECT_TRUE(
      base::test::RunUntil([&] { return index->ShowsPage(home_url_, work_); }));
  EXPECT_TRUE(index->ShowsPage(home_url_, ""));

  // "Forget This Page" with Work shown.
  base::test::TestFuture<void> expired;
  base::CancelableTaskTracker tracker;
  history()->ExpireHistoryBetween({home_url_}, tag, base::Time(), base::Time(),
                                  /*user_initiated=*/true,
                                  expired.GetCallback(), &tracker);
  ASSERT_TRUE(expired.Wait());
  EXPECT_EQ(std::set<GURL>{work_url_}, Pages(tag));
  EXPECT_TRUE(Pages(std::string()).contains(home_url_));
  // The index reads the deletion again.
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !index->ShowsPage(home_url_, work_); }));
  EXPECT_TRUE(index->ShowsPage(home_url_, ""));
  EXPECT_TRUE(index->ShowsPage(work_url_, work_));
}

}  // namespace koan
