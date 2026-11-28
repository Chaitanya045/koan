// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_EXTENSION_HISTORY_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_EXTENSION_HISTORY_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/values.h"

class Profile;

namespace base {
class CancelableTaskTracker;
}

namespace content {
class BrowserContext;
}

namespace history {
class DeletionInfo;
class HistoryService;
class QueryResults;
class VisitRow;
struct MostVisitedURL;
struct QueryOptions;
}  // namespace history

// Extensions per Space profile (#544, koan_container_extensions.h): what
// chrome.history and chrome.topSites give an extension that is off in a Space
// profile leaves out that profile's visits. A visit's profile is its tag
// (koan_history_profiles.h). Defined in koan_extension_history.cc.
namespace koan {

// HistorySearchFunction::Run(), in place of HistoryService::QueryHistory():
// the same query, without the visits of profiles `extension_id` is off in. A
// page visited there and elsewhere is found by its other visits, with their
// latest time, count and typed count. For an extension that is off nowhere it
// is QueryHistory() itself.
void QueryHistoryForExtension(
    history::HistoryService* history,
    content::BrowserContext* context,
    std::string_view extension_id,
    const std::u16string& text,
    const history::QueryOptions& options,
    base::OnceCallback<void(history::QueryResults)> callback,
    base::CancelableTaskTracker* tracker);

// HistoryGetVisitsFunction::QueryComplete(): erases from `visits` those of
// profiles `extension_id` is off in.
void EraseVisitsOffFor(content::BrowserContext* context,
                       std::string_view extension_id,
                       std::vector<history::VisitRow>& visits);

// HistoryEventRouter::OnHistoryDeletions(): a koan::containers::ScopedEventSkip
// filter (koan_extension_events.h) that leaves out of history.onVisitRemoved
// the pages that only had visits in profiles the extension is off in, by
// what the Space history index (koan_space_history_index.h) knew of them
// before the deletion; with the index not read yet, every page, for an
// extension that is off anywhere. An event left with no page isn't sent.
base::RepeatingCallback<bool(content::BrowserContext* context,
                             std::string_view extension_id,
                             std::optional<base::ListValue>& args_out)>
RemovedPagesFilter(Profile* profile,
                   const history::DeletionInfo& deletion_info);

// TopSitesGetFunction::Run(): `callback`, for TopSites::GetMostVisitedURLs(),
// given only the sites with a visit outside the profiles `extension_id` is off
// in (read from the history database first). For an extension that is off
// nowhere it is `callback`.
base::OnceCallback<void(const std::vector<history::MostVisitedURL>&)>
TopSitesForExtension(
    content::BrowserContext* context,
    std::string_view extension_id,
    base::OnceCallback<void(const std::vector<history::MostVisitedURL>&)>
        callback);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_EXTENSION_HISTORY_H_
