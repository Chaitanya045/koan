// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_TAB_AUTO_CLOSER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_TAB_AUTO_CLOSER_H_

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_service_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "koan/browser/koan_tab_auto_close.h"
#include "url/gurl.h"

class BrowserView;
class BrowserWindowInterface;
class Profile;

namespace content {
class WebContents;
}

namespace tabs {
class TabInterface;
}

namespace koan {

// "Close tabs I haven't used for" (Settings > Tab Management > Performance;
// Arc's auto-archive, #482) for one window: while the setting isn't Never, a
// timer checks the window's tabs every few minutes, and once soon after the
// window opens (so a tab that aged while Koan was closed goes after the
// session is restored), and closes those koan::auto_close::WhyKept() lets
// go. They are kept in prefs::kAutoClosedTabs for the Library's closed tabs,
// not in Chromium's recently closed list (25 entries, and Cmd+Shift+T's).
// Before the first close ever, the profile's front window shows a notice
// instead, and nothing closes until the next check.
//
// Deleting history (all of it, a time range or pages) forgets the closed
// tabs it covers: every profile's (#474), except for the Library's removals
// from one profile (DeleteProfileHistory()).
//
// It also keeps the time each visible tab was last on screen with the
// session (koan::auto_close::kLastSeenSessionKey): WebContents only knows
// when a tab was shown. And for a tab put in the window without being shown,
// when it was opened (koan::auto_close::kOpenedUnseenSessionKey, until it is
// shown): Chromium keeps no last-active time for a tab never shown, so after
// a restart its age would start again (#577).
class KoanTabAutoCloser : public TabStripModelObserver,
                          public history::HistoryServiceObserver {
 public:
  explicit KoanTabAutoCloser(BrowserView* browser_view);
  KoanTabAutoCloser(const KoanTabAutoCloser&) = delete;
  KoanTabAutoCloser& operator=(const KoanTabAutoCloser&) = delete;
  ~KoanTabAutoCloser() override;

  // Stops the timer and lets go of the window's tabs.
  void OnBrowserViewDestroying();

  // Runs a check now (tests).
  void CheckNowForTesting() { Check(); }

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // history::HistoryServiceObserver: deleting history forgets the closed
  // tabs it covers, as it does Chromium's recently closed ones.
  void OnHistoryDeletions(history::HistoryService* history_service,
                          const history::DeletionInfo& deletion_info) override;
  void HistoryServiceBeingDeleted(
      history::HistoryService* history_service) override;

 private:
  // The setting (or the test switch) changed: restarts the timer.
  void OnSettingChanged();
  void Check();
  // The tabs on screen now: the active tab and the tabs selected with it
  // (its split view).
  std::vector<tabs::TabInterface*> VisibleTabs() const;
  // Notes that `tab` is on screen now.
  void Stamp(tabs::TabInterface* tab);
  // Notes when `contents`, in this window and not shown, was opened, unless
  // it was ever on screen (it has a time either way).
  void MarkUnseen(base::WeakPtr<content::WebContents> contents);
  // `tab` is on screen: it no longer counts from when it was opened.
  void ClearUnseen(tabs::TabInterface* tab);
  auto_close::TabFacts FactsFor(
      tabs::TabInterface* tab,
      const std::vector<tabs::TabInterface*>& active) const;
  // Records `tab` for the Library and closes it.
  void Close(tabs::TabInterface* tab);
  // Whether tabs may close at `now`: only once the one-time notice has been
  // up a while. Shows it in the profile's front window (and returns false)
  // if it wasn't shown yet.
  bool MayClose(size_t count, base::Time now);
  Profile* profile() const;

  raw_ptr<BrowserView> browser_view_;
  PrefChangeRegistrar prefs_;
  std::optional<base::TimeDelta> limit_;
  base::OneShotTimer first_check_;
  base::RepeatingTimer timer_;
  base::ScopedObservation<history::HistoryService,
                          history::HistoryServiceObserver>
      history_observation_{this};
  base::WeakPtrFactory<KoanTabAutoCloser> weak_factory_{this};
};

// The Library's closed tabs (koan_library_history.cc): the tabs `profile`
// closed automatically, newest first.
std::vector<auto_close::ClosedTab> GetAutoClosedTabs(Profile* profile);

// Reopens the closed tab `id` in `browser`, in its Space if the window has
// it (else the active one), with its Koan session data (its container,
// #318), in front unless `background`, and drops it from the list. False if
// there is no such tab.
bool ReopenAutoClosedTab(BrowserWindowInterface* browser,
                         const std::string& id,
                         bool background);

// Drops the closed tab `id` from `profile`'s list ("Forget This Page").
void ForgetAutoClosedTab(Profile* profile, const std::string& id);

// The Library's removals while it shows one Space profile (#474): deletes
// that profile's visits (`history_profile`, named as in
// koan_history_profiles.h) to `urls`, or to every page if empty, from `begin`
// (all of them if null) on, even once the Library has closed. Chromium's
// notice names the visits and the pages gone, never a time range (#582), and
// doesn't say which profile it was, so the closed tabs it covers are
// forgotten here, only those of `history_profile` (by the container in their
// session data), and the windows' closers let the notice go by.
void DeleteProfileHistory(Profile* profile,
                          history::HistoryService* history,
                          std::string_view history_profile,
                          const std::set<GURL>& urls,
                          base::Time begin);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_TAB_AUTO_CLOSER_H_
