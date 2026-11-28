// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_TEST_BROWSER_KOAN_BROWSER_TEST_UTILS_H_
#define KOAN_TEST_BROWSER_KOAN_BROWSER_TEST_UTILS_H_

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/scoped_observation.h"
#include "base/test/test_future.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_service_observer.h"
#include "components/history/core/browser/history_types.h"
#include "components/tab_groups/tab_group_id.h"
#include "url/gurl.h"

class BrowserWindowInterface;
class TabStripModel;

namespace koan {

class KoanWorkspaceController;
class KoanPinnedTabController;

// Koan's controllers of `browser`'s window, or null if it has none (a
// window without Koan's UI).
KoanWorkspaceController* GetSpaces(BrowserWindowInterface* browser);
KoanPinnedTabController* GetPinnedTabs(BrowserWindowInterface* browser);

// Waits until the window has `count` Spaces: a window always shows one, which
// Koan creates once the window is set up. Returns false on a timeout.
[[nodiscard]] bool WaitForSpaceCount(BrowserWindowInterface* browser,
                                     size_t count);

// The names of the window's Spaces in sidebar order.
std::vector<std::u16string> SpaceNames(BrowserWindowInterface* browser);

// Writes the window's session to disk now, so the next browser (a test's
// restart) restores it; the session service otherwise saves a little later,
// and a browser closed first would lose the last changes.
void FlushSession(BrowserWindowInterface* browser);

// The indices of the tabs of `space` in the tab strip.
std::vector<int> TabsOfSpace(TabStripModel* tab_strip,
                             const tab_groups::TabGroupId& space);

// What a history deletion notice (history::DeletionInfo) told the observers.
struct HistoryDeletionNotice {
  // IsAllHistory(): observers drop everything they keep.
  bool all_history = false;
  bool has_time_range = false;
  std::optional<std::set<GURL>> restrict_urls;
  // The pages with no visit left.
  std::set<GURL> deleted_pages;
  std::set<history::VisitID> deleted_visits;
};

// Hears the next deletion notice of a history service.
class HistoryDeletionWaiter : public history::HistoryServiceObserver {
 public:
  explicit HistoryDeletionWaiter(history::HistoryService* history);
  ~HistoryDeletionWaiter() override;

  // The notice, once heard: the other observers have heard it too.
  HistoryDeletionNotice Wait();

  // history::HistoryServiceObserver:
  void OnHistoryDeletions(history::HistoryService* history_service,
                          const history::DeletionInfo& deletion_info) override;

 private:
  base::test::TestFuture<HistoryDeletionNotice> notice_;
  base::ScopedObservation<history::HistoryService,
                          history::HistoryServiceObserver>
      observation_{this};
};

}  // namespace koan

#endif  // KOAN_TEST_BROWSER_KOAN_BROWSER_TEST_UTILS_H_
