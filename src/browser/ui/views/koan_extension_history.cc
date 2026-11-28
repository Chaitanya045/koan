// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Extensions per Space profile (#544,
// .agent/notes/upstream/space-extensions.md): chrome.history and
// chrome.topSites without the visits of the profiles an extension is off in.

#include "koan/browser/ui/views/koan_extension_history.h"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/api/history.h"
#include "components/history/core/browser/history_backend.h"
#include "components/history/core/browser/history_database.h"
#include "components/history/core/browser/history_db_task.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "content/public/browser/browser_context.h"
#include "koan/browser/containers/koan_container_extensions.h"
#include "koan/browser/containers/koan_history_profiles.h"
#include "koan/browser/ui/views/koan_space_history_index.h"
#include "ui/base/page_transition_types.h"

namespace koan {

namespace {

const char kExtensionHistoryTasksKey[] = "koan.extension_history_tasks";

// The profiles an extension is off in (ProfilesExtensionIsOffIn()), and
// whether a visit is in one of them.
class OffProfiles {
 public:
  explicit OffProfiles(std::set<std::string> profiles)
      : profiles_(std::move(profiles)) {}

  bool empty() const { return profiles_.empty(); }

  bool Has(const std::optional<std::string>& app_id) const {
    const std::string profile = containers::HistoryProfileFromAppId(app_id);
    return !profile.empty() && profiles_.contains(profile);
  }

  // Whether every profile `page` has visits in is one of them.
  bool HidesPage(const containers::PageProfiles& page) const {
    return !page.in_default && !page.profiles.empty() &&
           std::ranges::all_of(page.profiles, [this](const std::string& p) {
             return profiles_.contains(p);
           });
  }

 private:
  std::set<std::string> profiles_;
};

// A tracker for the top sites' history tasks, kept with the browser profile:
// TopSitesGetFunction has none.
class ExtensionHistoryTasks : public base::SupportsUserData::Data {
 public:
  static base::CancelableTaskTracker* TrackerFor(Profile* profile) {
    auto* tasks = static_cast<ExtensionHistoryTasks*>(
        profile->GetUserData(kExtensionHistoryTasksKey));
    if (!tasks) {
      auto owned = std::make_unique<ExtensionHistoryTasks>();
      tasks = owned.get();
      profile->SetUserData(kExtensionHistoryTasksKey, std::move(owned));
    }
    return &tasks->tracker_;
  }

 private:
  base::CancelableTaskTracker tracker_;
};

// history.search for an extension that is off in some profiles: upstream's
// query once per tag it isn't off in (QueryOptions::app_id: the default
// profile's '' and every other tag in the database), each page with its latest
// visit among them, newest first, as many as asked. A page that also has
// visits in the hidden profiles gets the count, typed count and last visit of
// the others.
class SearchTask : public history::HistoryDBTask {
 public:
  SearchTask(std::u16string text,
             history::QueryOptions options,
             OffProfiles off,
             base::OnceCallback<void(history::QueryResults)> done)
      : text_(std::move(text)),
        options_(std::move(options)),
        off_(std::move(off)),
        done_(std::move(done)) {}
  ~SearchTask() override = default;

  // history::HistoryDBTask:
  bool RunOnDBThread(history::HistoryBackend* backend,
                     history::HistoryDatabase* db) override {
    std::set<std::string> tags = {std::string()};
    for (std::string& app_id : db->GetAllAppIds().app_ids) {
      tags.insert(std::move(app_id));
    }
    std::map<history::URLID, history::URLResult> latest;
    for (const std::string& tag : tags) {
      if (off_.Has(tag)) {
        continue;
      }
      history::QueryOptions options = options_;
      options.app_id = tag;
      const history::QueryResults results =
          backend->QueryHistory(text_, options);
      for (const history::URLResult& result : results) {
        auto [it, inserted] = latest.try_emplace(result.id(), result);
        if (!inserted && result.visit_time() > it->second.visit_time()) {
          it->second = result;
        }
      }
    }
    std::vector<history::URLResult> found;
    found.reserve(latest.size());
    for (auto& [id, result] : latest) {
      found.push_back(std::move(result));
    }
    std::ranges::stable_sort(found, std::ranges::greater(),
                             &history::URLResult::visit_time);
    const size_t max = static_cast<size_t>(options_.EffectiveMaxCount());
    if (found.size() > max) {
      found.resize(max);
    }
    for (history::URLResult& result : found) {
      CountVisibleVisits(db, result);
    }
    results_.SetURLResults(std::move(found));
    return true;
  }

  void DoneRunOnMainThread() override {
    std::move(done_).Run(std::move(results_));
  }

 private:
  // The URL row counts every profile's visits (AddPageVisit()); recount
  // those of a page with hidden ones, with the same rules.
  void CountVisibleVisits(history::HistoryDatabase* db,
                          history::URLResult& result) {
    history::VisitVector visits;
    if (!db->GetVisitsForURL(result.id(), &visits) ||
        std::ranges::none_of(visits, [this](const history::VisitRow& visit) {
          return off_.Has(visit.app_id);
        })) {
      return;
    }
    int visit_count = 0;
    int typed_count = 0;
    base::Time last_visit;
    for (const history::VisitRow& visit : visits) {
      if (off_.Has(visit.app_id)) {
        continue;
      }
      if (!ui::PageTransitionCoreTypeIs(visit.transition,
                                        ui::PAGE_TRANSITION_RELOAD)) {
        ++visit_count;
      }
      if (visit.incremented_omnibox_typed_score) {
        ++typed_count;
      }
      last_visit = std::max(last_visit, visit.visit_time);
    }
    result.set_visit_count(visit_count);
    result.set_typed_count(typed_count);
    result.set_last_visit(last_visit);
  }

  const std::u16string text_;
  const history::QueryOptions options_;
  const OffProfiles off_;
  base::OnceCallback<void(history::QueryResults)> done_;
  history::QueryResults results_;
};

// topSites.get for an extension that is off in some profiles: the sites with a
// visit outside them. A site the database has no row or no visit for (a
// prepopulated page) stays.
class TopSitesTask : public history::HistoryDBTask {
 public:
  using Done = base::OnceCallback<void(const history::MostVisitedURLList&)>;

  TopSitesTask(history::MostVisitedURLList sites, OffProfiles off, Done done)
      : sites_(std::move(sites)),
        off_(std::move(off)),
        done_(std::move(done)) {}
  ~TopSitesTask() override = default;

  // history::HistoryDBTask:
  bool RunOnDBThread(history::HistoryBackend* backend,
                     history::HistoryDatabase* db) override {
    std::erase_if(sites_, [&](const history::MostVisitedURL& site) {
      history::URLRow row;
      history::VisitVector visits;
      return db->GetRowForURL(site.url, &row) &&
             db->GetVisitsForURL(row.id(), &visits) && !visits.empty() &&
             std::ranges::all_of(visits, [this](const history::VisitRow& v) {
               return off_.Has(v.app_id);
             });
    });
    return true;
  }

  void DoneRunOnMainThread() override { std::move(done_).Run(sites_); }

 private:
  history::MostVisitedURLList sites_;
  const OffProfiles off_;
  Done done_;
};

// The ScopedEventSkip filter of history.onVisitRemoved: `pages` are the
// removed URLs, each with its profiles, or nullopt where the index didn't know
// them yet (hidden from an extension that is off anywhere).
bool WillDispatchRemovedPages(
    const std::vector<
        std::pair<std::string, std::optional<containers::PageProfiles>>>& pages,
    content::BrowserContext* context,
    std::string_view extension_id,
    std::optional<base::ListValue>& args_out) {
  const OffProfiles off(
      containers::ProfilesExtensionIsOffIn(context, extension_id));
  if (off.empty()) {
    return true;
  }
  extensions::api::history::OnVisitRemoved::Removed removed;
  removed.all_history = false;
  removed.urls.emplace();
  for (const auto& [url, profiles] : pages) {
    if (profiles && !off.HidesPage(*profiles)) {
      removed.urls->push_back(url);
    }
  }
  if (removed.urls->size() == pages.size()) {
    return true;
  }
  if (removed.urls->empty()) {
    return false;
  }
  args_out = extensions::api::history::OnVisitRemoved::Create(removed);
  return true;
}

}  // namespace

void QueryHistoryForExtension(
    history::HistoryService* history,
    content::BrowserContext* context,
    std::string_view extension_id,
    const std::u16string& text,
    const history::QueryOptions& options,
    base::OnceCallback<void(history::QueryResults)> callback,
    base::CancelableTaskTracker* tracker) {
  OffProfiles off(containers::ProfilesExtensionIsOffIn(context, extension_id));
  if (off.empty()) {
    history->QueryHistory(text, options, std::move(callback), tracker);
    return;
  }
  history->ScheduleDBTask(
      FROM_HERE,
      std::make_unique<SearchTask>(text, options, std::move(off),
                                   std::move(callback)),
      tracker);
}

void EraseVisitsOffFor(content::BrowserContext* context,
                       std::string_view extension_id,
                       std::vector<history::VisitRow>& visits) {
  const OffProfiles off(
      containers::ProfilesExtensionIsOffIn(context, extension_id));
  if (!off.empty()) {
    std::erase_if(visits, [&off](const history::VisitRow& visit) {
      return off.Has(visit.app_id);
    });
  }
}

base::RepeatingCallback<bool(content::BrowserContext* context,
                             std::string_view extension_id,
                             std::optional<base::ListValue>& args_out)>
RemovedPagesFilter(Profile* profile,
                   const history::DeletionInfo& deletion_info) {
  if (deletion_info.IsAllHistory() || deletion_info.deleted_rows().empty()) {
    return {};
  }
  SpaceHistoryIndex* index = SpaceHistoryIndex::For(profile);
  const bool known = index && index->loaded();
  std::vector<std::pair<std::string, std::optional<containers::PageProfiles>>>
      pages;
  bool any_tagged = !known;
  for (const history::URLRow& row : deletion_info.deleted_rows()) {
    std::optional<containers::PageProfiles> profiles;
    if (known) {
      const containers::PageProfiles* page = index->PageProfilesOf(row.url());
      profiles = page ? *page : containers::PageProfiles{.in_default = true};
      any_tagged |= page != nullptr;
    }
    pages.emplace_back(row.url().spec(), std::move(profiles));
  }
  if (!any_tagged) {
    return {};
  }
  return base::BindRepeating(&WillDispatchRemovedPages, std::move(pages));
}

base::OnceCallback<void(const history::MostVisitedURLList&)>
TopSitesForExtension(
    content::BrowserContext* context,
    std::string_view extension_id,
    base::OnceCallback<void(const history::MostVisitedURLList&)> callback) {
  OffProfiles off(containers::ProfilesExtensionIsOffIn(context, extension_id));
  Profile* profile = Profile::FromBrowserContext(context);
  history::HistoryService* history =
      off.empty() ? nullptr
                  : HistoryServiceFactory::GetForProfile(
                        profile, ServiceAccessType::EXPLICIT_ACCESS);
  if (!history) {
    return callback;
  }
  return base::BindOnce(
      [](base::WeakPtr<history::HistoryService> history,
         base::WeakPtr<Profile> profile, OffProfiles off,
         base::OnceCallback<void(const history::MostVisitedURLList&)> callback,
         const history::MostVisitedURLList& sites) {
        if (!history || !profile) {
          // Shutting down: nothing to read with, so nothing to give.
          std::move(callback).Run({});
          return;
        }
        history->ScheduleDBTask(
            FROM_HERE,
            std::make_unique<TopSitesTask>(sites, std::move(off),
                                           std::move(callback)),
            ExtensionHistoryTasks::TrackerFor(profile.get()));
      },
      history->AsWeakPtr(), profile->GetWeakPtr(), std::move(off),
      std::move(callback));
}

}  // namespace koan
