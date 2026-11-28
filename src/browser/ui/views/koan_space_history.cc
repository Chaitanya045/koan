// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// History per Space profile (#474, .agent/notes/upstream/space-history.md):
// the index of which profiles a page was visited in, and the URL bar's
// filters on it.

#include "koan/browser/ui/views/koan_space_history.h"

#include <memory>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/time/time.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/history/core/browser/history_database.h"
#include "components/history/core/browser/history_db_task.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_provider_client.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_history_profiles.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/omnibox/koan_omnibox_hooks.h"
#include "koan/browser/ui/views/koan_history_matches.h"
#include "koan/browser/ui/views/koan_space_history_index.h"
#include "koan/browser/ui/views/koan_workspaces.h"

namespace koan {

namespace {

const char kSpaceHistoryIndexKey[] = "koan.space_history_index";

// A deletion is read again once the burst it comes in is over.
constexpr base::TimeDelta kRereadDelay = base::Seconds(1);

// `container` (a Space's container, a tab's Serialize()d one) as a history
// profile: itself if it is a saved user container, else the default (empty).
std::string AsHistoryProfile(const std::string& container) {
  return containers::HistoryAppIdForProfile(container).empty() ? std::string()
                                                               : container;
}

// The profile of the URL bar being typed in, when `prefs` are the prefs of
// the last active window's profile; null for any other.
Profile* OmniboxProfileWithPrefs(const PrefService* prefs) {
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
  return browser && browser->GetProfile()->GetPrefs() == prefs
             ? browser->GetProfile()
             : nullptr;
}

}  // namespace

// Reads the profiles of the pages with a tagged visit (or of one page) on
// the history database's sequence, with the database's own queries: the
// visits of each Koan tag (GetAllAppIds(), GetAllVisitsInRange()), then
// every visit of those pages (GetVisitsForURL()), which says whether the page
// was also visited without a profile.
class SpaceHistoryIndex::ReadTask : public history::HistoryDBTask {
 public:
  using Done =
      base::OnceCallback<void(bool all, Pages pages, std::vector<GURL>)>;

  ReadTask(std::optional<history::URLID> url_id, Done done)
      : url_id_(url_id), done_(std::move(done)) {}
  ~ReadTask() override = default;

  // history::HistoryDBTask:
  bool RunOnDBThread(history::HistoryBackend* backend,
                     history::HistoryDatabase* db) override {
    std::set<history::URLID> ids;
    if (url_id_) {
      ids.insert(*url_id_);
    } else {
      for (const std::string& app_id : db->GetAllAppIds().app_ids) {
        if (containers::HistoryProfileFromAppId(app_id).empty()) {
          continue;
        }
        history::VisitVector visits;
        db->GetAllVisitsInRange(base::Time(), base::Time::Max(), app_id,
                                /*max_results=*/0, &visits);
        for (const history::VisitRow& visit : visits) {
          ids.insert(visit.url_id);
        }
      }
    }
    for (history::URLID id : ids) {
      history::URLRow row;
      history::VisitVector visits;
      if (!db->GetURLRow(id, &row)) {
        continue;
      }
      db->GetVisitsForURL(id, &visits);
      containers::PageProfiles page;
      for (const history::VisitRow& visit : visits) {
        const std::string profile =
            containers::HistoryProfileFromAppId(visit.app_id);
        if (profile.empty()) {
          page.in_default = true;
        } else {
          page.profiles.insert(profile);
        }
      }
      if (page.profiles.empty()) {
        untagged_.push_back(row.url());
      } else {
        pages_[row.url()] = std::move(page);
      }
    }
    return true;
  }

  void DoneRunOnMainThread() override {
    std::move(done_).Run(!url_id_, std::move(pages_), std::move(untagged_));
  }

 private:
  const std::optional<history::URLID> url_id_;
  Done done_;
  Pages pages_;
  std::vector<GURL> untagged_;
};

// static
SpaceHistoryIndex* SpaceHistoryIndex::For(Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  Profile* original = profile->GetOriginalProfile();
  if (auto* index = static_cast<SpaceHistoryIndex*>(
          original->GetUserData(kSpaceHistoryIndexKey))) {
    return index;
  }
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      original, ServiceAccessType::IMPLICIT_ACCESS);
  if (!history) {
    return nullptr;
  }
  auto index = std::make_unique<SpaceHistoryIndex>(original, history);
  SpaceHistoryIndex* raw = index.get();
  original->SetUserData(kSpaceHistoryIndexKey, std::move(index));
  return raw;
}

SpaceHistoryIndex::SpaceHistoryIndex(Profile* profile,
                                     history::HistoryService* history)
    : profile_(profile), history_(history) {
  observation_.Observe(history_);
  Read(std::nullopt);
}

SpaceHistoryIndex::~SpaceHistoryIndex() = default;

bool SpaceHistoryIndex::ShowsPage(const GURL& url, std::string_view profile) {
  if (!loaded_) {
    return profile.empty();
  }
  const Pages& keys = Keys();
  const auto it = keys.find(
      HistoryPageKey(url, TemplateURLServiceFactory::GetForProfile(profile_)));
  return containers::PageShowsInProfile(
      it == keys.end() ? nullptr : &it->second, profile);
}

const containers::PageProfiles* SpaceHistoryIndex::PageProfilesOf(
    const GURL& url) const {
  const auto it = pages_.find(url);
  return it == pages_.end() ? nullptr : &it->second;
}

void SpaceHistoryIndex::OnURLVisited(
    history::HistoryService* history_service,
    const history::VisitedURLInfo& visited_url_info) {
  const GURL& url = visited_url_info.url_row.url();
  const std::string profile =
      containers::HistoryProfileFromAppId(visited_url_info.visit_row.app_id);
  auto it = pages_.find(url);
  if (profile.empty()) {
    // A page without a tagged visit needs no entry.
    if (it != pages_.end() && !it->second.in_default) {
      it->second.in_default = true;
      keys_valid_ = false;
    }
    return;
  }
  if (it != pages_.end()) {
    if (it->second.profiles.insert(profile).second) {
      keys_valid_ = false;
    }
    return;
  }
  // A page new to the index: its older visits (before #474, or in a Space
  // without a profile) are read from the database. Until then it is this
  // profile's only.
  pages_[url].profiles.insert(profile);
  keys_valid_ = false;
  Read(visited_url_info.url_row.id());
}

void SpaceHistoryIndex::OnHistoryDeletions(
    history::HistoryService* history_service,
    const history::DeletionInfo& deletion_info) {
  if (deletion_info.IsAllHistory()) {
    pages_.clear();
    keys_valid_ = false;
  }
  // Which visits went isn't said for a time range; read it all again.
  reread_timer_.Start(
      FROM_HERE, kRereadDelay,
      base::BindOnce(&SpaceHistoryIndex::Read, base::Unretained(this),
                     std::optional<history::URLID>()));
}

void SpaceHistoryIndex::HistoryServiceBeingDeleted(
    history::HistoryService* history_service) {
  reread_timer_.Stop();
  tracker_.TryCancelAll();
  observation_.Reset();
  history_ = nullptr;
}

void SpaceHistoryIndex::Read(std::optional<history::URLID> url_id) {
  if (!history_) {
    return;
  }
  if (!url_id) {
    // A whole read replaces the pages read one by one.
    tracker_.TryCancelAll();
  }
  history_->ScheduleDBTask(
      FROM_HERE,
      std::make_unique<ReadTask>(url_id,
                                 base::BindOnce(&SpaceHistoryIndex::OnRead,
                                                weak_factory_.GetWeakPtr())),
      &tracker_);
}

void SpaceHistoryIndex::OnRead(bool all,
                               Pages pages,
                               std::vector<GURL> untagged) {
  if (all) {
    pages_ = std::move(pages);
    loaded_ = true;
  } else {
    for (auto& [url, page] : pages) {
      pages_[url] = std::move(page);
    }
    for (const GURL& url : untagged) {
      pages_.erase(url);
    }
  }
  keys_valid_ = false;
}

const SpaceHistoryIndex::Pages& SpaceHistoryIndex::Keys() {
  const TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile_);
  const bool with_search_engines = service && service->loaded();
  if (keys_valid_ && keys_with_search_engines_ == with_search_engines) {
    return keys_;
  }
  keys_.clear();
  for (const auto& [url, page] : pages_) {
    containers::PageProfiles& key = keys_[HistoryPageKey(url, service)];
    key.in_default |= page.in_default;
    key.profiles.insert(page.profiles.begin(), page.profiles.end());
  }
  keys_valid_ = true;
  keys_with_search_engines_ = with_search_engines;
  return keys_;
}

std::string HistoryProfileOfWindow(BrowserWindowInterface* browser) {
  if (!browser) {
    return std::string();
  }
  if (KoanWorkspaceController* spaces =
          KoanWorkspaceController::FromTabStrip(browser->GetTabStripModel())) {
    if (const std::optional<tab_groups::TabGroupId> space =
            spaces->GetActiveSpace()) {
      return AsHistoryProfile(spaces->GetSpaceContainer(*space));
    }
  }
  const std::optional<containers::ContainerId> container =
      containers::ContainerOf(
          browser->GetTabStripModel()->GetActiveWebContents());
  return container ? AsHistoryProfile(containers::Serialize(*container))
                   : std::string();
}

std::optional<std::string> OmniboxHistoryProfile(Profile* profile) {
  if (!profile ||
      !profile->GetPrefs()->GetBoolean(prefs::kSpacesSeparateHistory)) {
    return std::nullopt;
  }
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
  return browser && browser->GetProfile() == profile
             ? HistoryProfileOfWindow(browser)
             : std::string();
}

bool IsOutsideOmniboxProfile(Profile* profile, content::WebContents* contents) {
  const std::optional<std::string> shown = OmniboxHistoryProfile(profile);
  if (!shown) {
    return false;
  }
  const std::optional<containers::ContainerId> container =
      containers::ContainerOf(contents);
  return *shown != (container
                        ? AsHistoryProfile(containers::Serialize(*container))
                        : std::string());
}

void FilterSpaceHistoryMatches(AutocompleteProviderClient& client,
                               const AutocompleteInput& input,
                               AutocompleteResult& result) {
  Profile* profile = OmniboxProfileWithPrefs(client.GetPrefs());
  const std::optional<std::string> shown = OmniboxHistoryProfile(profile);
  SpaceHistoryIndex* index = shown ? SpaceHistoryIndex::For(profile) : nullptr;
  if (!index) {
    return;
  }
  const TemplateURLService* service = client.GetTemplateURLService();
  const auto hidden = [&](const AutocompleteMatch& match) {
    return IsSpaceHistoryMatch(match) &&
           !index->ShowsPage(match.destination_url, *shown);
  };
  // The address being typed stays, without the title another profile's
  // visit gave it.
  for (AutocompleteMatch& match : result) {
    if (hidden(match) && IsTypedAddress(match, input, service)) {
      match.description.clear();
      match.description_class.clear();
      match.swap_contents_and_description = false;
    }
  }
  result.EraseMatchesWhere([&](const AutocompleteMatch& match) {
    return hidden(match) && !IsTypedAddress(match, input, service);
  });
}

}  // namespace koan
