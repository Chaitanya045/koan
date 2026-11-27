// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_HISTORY_INDEX_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_HISTORY_INDEX_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/supports_user_data.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/timer/timer.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_service_observer.h"
#include "components/history/core/browser/history_types.h"
#include "koan/browser/containers/koan_history_profiles.h"
#include "url/gurl.h"

class BrowserWindowInterface;
class Profile;

namespace koan {

// History per Space profile (#474, .agent/notes/upstream/space-history.md):
// which profiles each page has visits in, for the URL bar. Chromium's
// omnibox reads its in-memory URL index, which knows nothing of a visit's
// profile, so Koan filters its history suggestions afterwards
// (koan::FilterSpaceHistoryMatches()) against this index.
//
// It holds only pages with a visit tagged with a profile
// (koan_history_profiles.h): a page without one is the default profile's.
// It is read from the history database once, then kept up to date from the
// history service's notifications; a deletion reads it again.
class SpaceHistoryIndex : public base::SupportsUserData::Data,
                          public history::HistoryServiceObserver {
 public:
  // `profile`'s index (its original profile's), made on first use. Null
  // without a history service.
  static SpaceHistoryIndex* For(Profile* profile);

  SpaceHistoryIndex(Profile* profile, history::HistoryService* history);
  SpaceHistoryIndex(const SpaceHistoryIndex&) = delete;
  SpaceHistoryIndex& operator=(const SpaceHistoryIndex&) = delete;
  ~SpaceHistoryIndex() override;

  // Whether the URL bar in a Space of `profile` (empty: the default one)
  // shows a history suggestion of `url`. Until the index is read, the default
  // profile's URL bar shows every page and another's none.
  bool ShowsPage(const GURL& url, std::string_view profile);

  // Whether the index has been read from the database (for tests).
  bool loaded() const { return loaded_; }

  // history::HistoryServiceObserver:
  void OnURLVisited(history::HistoryService* history_service,
                    const history::VisitedURLInfo& visited_url_info) override;
  void OnHistoryDeletions(history::HistoryService* history_service,
                          const history::DeletionInfo& deletion_info) override;
  void HistoryServiceBeingDeleted(
      history::HistoryService* history_service) override;

 private:
  class ReadTask;
  using Pages = std::map<GURL, containers::PageProfiles>;

  // Reads every page with a tagged visit, or only the page `url_id`.
  void Read(std::optional<history::URLID> url_id);
  // `pages` are the pages read with a tagged visit; `untagged`, pages read
  // that have none (left). `all`: the whole index.
  void OnRead(bool all, Pages pages, std::vector<GURL> untagged);
  // The pages by HistoryPageKey(), merged.
  const Pages& Keys();

  const raw_ptr<Profile> profile_;
  raw_ptr<history::HistoryService> history_;
  base::ScopedObservation<history::HistoryService,
                          history::HistoryServiceObserver>
      observation_{this};
  // Pages with a tagged visit, by URL.
  Pages pages_;
  // `pages_` by HistoryPageKey(), built when asked, and again once the
  // search engines (which the key of a search page needs) have loaded.
  Pages keys_;
  bool keys_valid_ = false;
  bool keys_with_search_engines_ = false;
  bool loaded_ = false;
  // Deletions come in bursts (a range clear is many notifications).
  base::OneShotTimer reread_timer_;
  base::CancelableTaskTracker tracker_;
  base::WeakPtrFactory<SpaceHistoryIndex> weak_factory_{this};
};

// The Space profile whose history `browser`'s URL bar and Library show: its
// active Space's (empty for none), or in a window without Spaces (Incognito,
// a New Blank Window) its active tab's. Only saved user containers count.
std::string HistoryProfileOfWindow(BrowserWindowInterface* browser);

// The profile the URL bar of `profile` shows the history of: that of the last
// active window, when it is one of `profile`'s, else the default one; nullopt
// with Keep history apart for each profile off (everything shows).
std::optional<std::string> OmniboxHistoryProfile(Profile* profile);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_HISTORY_INDEX_H_
