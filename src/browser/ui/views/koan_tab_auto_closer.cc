// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_tab_auto_closer.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/uuid.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser_tabrestore.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/singleton_tabs.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h"
#include "chrome/common/webui_url_constants.h"
#include "components/download/public/common/download_item.h"
#include "components/history/core/browser/history_backend.h"
#include "components/history/core/browser/history_database.h"
#include "components/history/core/browser/history_db_task.h"
#include "components/history/core/browser/history_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "components/sessions/core/serialized_user_agent_override.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_history_profiles.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_switches.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_folder_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_session_data.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_tab_unload.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/page_transition_types.h"

namespace koan {

namespace {

// How long the one-time notice stays: it has a button to read and press.
constexpr base::TimeDelta kNoticeTimeout = base::Seconds(10);

// A stamp is written to the session at most this often per tab.
constexpr base::TimeDelta kStampGranularity = base::Minutes(1);

// Whether a download `contents` started is still running.
bool IsDownloading(content::WebContents* contents) {
  content::DownloadManager* downloads =
      contents->GetBrowserContext()->GetDownloadManager();
  if (!downloads) {
    return false;
  }
  content::DownloadManager::DownloadVector items;
  downloads->GetAllDownloads(&items);
  return std::ranges::any_of(items, [contents](const auto& item) {
    return item->GetState() == download::DownloadItem::IN_PROGRESS &&
           content::DownloadItemUtils::GetOriginalWebContents(item) == contents;
  });
}

// `tab` and the other tabs of its split view.
std::vector<tabs::TabInterface*> WithSplitPartners(TabStripModel* model,
                                                   tabs::TabInterface* tab) {
  std::vector<tabs::TabInterface*> tabs = {tab};
  if (!tab->GetSplit()) {
    return tabs;
  }
  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* other = model->GetTabAtIndex(i);
    if (other != tab && other->GetSplit() == tab->GetSplit()) {
      tabs.push_back(other);
    }
  }
  return tabs;
}

std::string TestOverride() {
  return base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
      switches::kAutoCloseTabsAfter);
}

// The deletions DeleteProfileHistory() asked for whose notice may still be
// on its way, kept with the profile: every window's closer lets those go by.
class ProfileHistoryDeletions : public base::SupportsUserData::Data {
 public:
  static ProfileHistoryDeletions* Get(Profile* profile) {
    return static_cast<ProfileHistoryDeletions*>(
        profile->GetOriginalProfile()->GetUserData(kUserDataKey));
  }

  static ProfileHistoryDeletions* GetOrCreate(Profile* profile) {
    if (!Get(profile)) {
      profile->GetOriginalProfile()->SetUserData(
          kUserDataKey, std::make_unique<ProfileHistoryDeletions>());
    }
    return Get(profile);
  }

  // A deletion on its way, until Remove(id).
  int Add() {
    const int id = next_id_++;
    pending_.push_back({id, std::nullopt});
    return id;
  }

  // Deletion `id` is removing `visit` (with others): its notice names it.
  void SetVisit(int id, history::VisitID visit) {
    for (Deletion& deletion : pending_) {
      if (deletion.id == id) {
        deletion.visit = visit;
      }
    }
  }

  void Remove(int id) {
    std::erase_if(pending_,
                  [id](const Deletion& deletion) { return deletion.id == id; });
  }

  // Whether `info` is the notice of one of them: a visit is removed once.
  bool Covers(const history::DeletionInfo& info) const {
    return std::ranges::any_of(pending_, [&](const Deletion& deletion) {
      return deletion.visit &&
             info.deleted_visit_ids().contains(*deletion.visit);
    });
  }

  // For the deletions' history tasks: they outlive the Library that asked.
  base::CancelableTaskTracker* tracker() { return &tracker_; }

  base::WeakPtr<ProfileHistoryDeletions> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  static constexpr char kUserDataKey[] = "koan.profile_history_deletions";

  struct Deletion {
    int id;
    std::optional<history::VisitID> visit;
  };

  int next_id_ = 0;
  std::vector<Deletion> pending_;
  base::CancelableTaskTracker tracker_;
  base::WeakPtrFactory<ProfileHistoryDeletions> weak_factory_{this};
};

// Removes one profile's visits (those tagged `app_id`) from `begin` on, to
// `urls` (every page if empty), on the history database's sequence, with
// HistoryBackend::RemoveVisits() (as sync removes visits). Its notice names
// the visits and the pages gone (those with no visit left in any profile),
// with no time range. ExpireHistoryBetween() can't say that (#582): its notice
// carries the range it was given, and from the beginning that is "all
// history" (DeletionInfo::IsAllHistory()) to every observer, which DeletionInfo
// rejects with pages (a DCHECK) and which visited links, the URL bar's index,
// top sites and the rest take as everything gone.
class RemoveProfileVisitsTask : public history::HistoryDBTask {
 public:
  RemoveProfileVisitsTask(std::string app_id,
                          std::set<GURL> urls,
                          base::Time begin,
                          base::WeakPtr<ProfileHistoryDeletions> deletions,
                          int id)
      : app_id_(std::move(app_id)),
        urls_(std::move(urls)),
        begin_(begin),
        origin_(base::SequencedTaskRunner::GetCurrentDefault()),
        deletions_(std::move(deletions)),
        id_(id) {}

  // Destroyed on the sequence that scheduled it, after the notice.
  ~RemoveProfileVisitsTask() override {
    origin_->PostTask(
        FROM_HERE,
        base::BindOnce(&ProfileHistoryDeletions::Remove, deletions_, id_));
  }

  // history::HistoryDBTask:
  bool RunOnDBThread(history::HistoryBackend* backend,
                     history::HistoryDatabase* db) override {
    history::VisitVector visits;
    db->GetAllVisitsInRange(begin_, base::Time::Max(), app_id_,
                            /*max_results=*/0, &visits);
    if (!urls_.empty()) {
      std::set<history::URLID> url_ids;
      for (const GURL& url : urls_) {
        if (const history::URLID url_id = db->GetRowForURL(url, nullptr)) {
          url_ids.insert(url_id);
        }
      }
      std::erase_if(visits, [&](const history::VisitRow& visit) {
        return !url_ids.contains(visit.url_id);
      });
    }
    if (visits.empty()) {
      return true;
    }
    // The notice goes to the same sequence, after this.
    origin_->PostTask(FROM_HERE,
                      base::BindOnce(&ProfileHistoryDeletions::SetVisit,
                                     deletions_, id_, visits.front().visit_id));
    backend->RemoveVisits(visits, history::DeletionInfo::Reason::kOther);
    return true;
  }

  void DoneRunOnMainThread() override {}

 private:
  const std::string app_id_;
  const std::set<GURL> urls_;
  const base::Time begin_;
  const scoped_refptr<base::SequencedTaskRunner> origin_;
  const base::WeakPtr<ProfileHistoryDeletions> deletions_;
  const int id_;
};

// The profile (koan_history_profiles.h) whose closed tab has the Koan session
// data `data`: the one its container names, as the Library's filter reads it.
std::string HistoryProfileOf(const std::map<std::string, std::string>& data) {
  const auto container = data.find(containers::kContainerSessionKey);
  return container == data.end()
             ? std::string()
             : containers::HistoryProfileFromAppId(
                   containers::HistoryAppIdForProfile(container->second));
}

void OpenTabSettings(Profile* profile) {
  ShowSingletonTabOverwritingNTP(
      profile->GetOriginalProfile(),
      GURL(std::string(chrome::kChromeUISettingsURL) + "koan/tabs"),
      NavigateParams::IGNORE_AND_NAVIGATE);
}

}  // namespace

KoanTabAutoCloser::KoanTabAutoCloser(BrowserView* browser_view)
    : browser_view_(browser_view) {
  BrowserWindowInterface* browser = browser_view->browser();
  // Normal windows of a regular profile: a private window's tabs couldn't
  // be found again once closed.
  if (browser->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
      profile()->IsOffTheRecord()) {
    browser_view_ = nullptr;
    return;
  }
  browser->GetTabStripModel()->AddObserver(this);
  prefs_.Init(profile()->GetPrefs());
  prefs_.Add(prefs::kAutoCloseTabsAfterHours,
             base::BindRepeating(&KoanTabAutoCloser::OnSettingChanged,
                                 base::Unretained(this)));
  if (history::HistoryService* history = HistoryServiceFactory::GetForProfile(
          profile(), ServiceAccessType::EXPLICIT_ACCESS)) {
    history_observation_.Observe(history);
  }
  // The tabs already in the window (OnTabStripModelChanged() marks the ones
  // inserted from now on).
  TabStripModel* model = browser->GetTabStripModel();
  for (int i = 0; i < model->count(); ++i) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanTabAutoCloser::MarkUnseen,
                                  weak_factory_.GetWeakPtr(),
                                  model->GetWebContentsAt(i)->GetWeakPtr()));
  }
  OnSettingChanged();
}

KoanTabAutoCloser::~KoanTabAutoCloser() = default;

void KoanTabAutoCloser::OnBrowserViewDestroying() {
  first_check_.Stop();
  timer_.Stop();
  prefs_.RemoveAll();
  history_observation_.Reset();
  if (browser_view_) {
    browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
  }
  browser_view_ = nullptr;
}

Profile* KoanTabAutoCloser::profile() const {
  return browser_view_->GetProfile();
}

void KoanTabAutoCloser::OnSettingChanged() {
  limit_ = auto_close::IdleLimit(
      profile()->GetPrefs()->GetInteger(prefs::kAutoCloseTabsAfterHours),
      TestOverride());
  first_check_.Stop();
  timer_.Stop();
  if (!limit_) {
    return;
  }
  // Soon after the window opens (or the setting is turned on), then
  // regularly. The tabs on screen are stamped meanwhile by tab switches.
  first_check_.Start(FROM_HERE, auto_close::FirstCheckDelay(*limit_), this,
                     &KoanTabAutoCloser::Check);
  timer_.Start(FROM_HERE, auto_close::CheckInterval(*limit_), this,
               &KoanTabAutoCloser::Check);
}

void KoanTabAutoCloser::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (!browser_view_) {
    return;
  }
  // A tab put in the window in the background (opened, moved here or
  // restored): when it was opened, once it is in its window (and once Koan
  // has put it in its Space). Not if it is shown by then.
  if (change.type() == TabStripModelChange::kInserted) {
    for (const TabStripModelChange::ContentsWithIndex& inserted :
         change.GetInsert()->contents) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&KoanTabAutoCloser::MarkUnseen,
                                    weak_factory_.GetWeakPtr(),
                                    inserted.contents->GetWeakPtr()));
    }
  }
  if (!selection.active_tab_changed()) {
    return;
  }
  // The tab shown now (and its split view) counts from when it is left.
  if (selection.new_contents) {
    if (tabs::TabInterface* shown =
            tabs::TabInterface::MaybeGetFromContents(selection.new_contents)) {
      for (tabs::TabInterface* tab :
           WithSplitPartners(tab_strip_model, shown)) {
        ClearUnseen(tab);
      }
    }
  }
  // The tab left was on screen until now (and its split view with it).
  if (selection.old_contents &&
      tab_strip_model->GetIndexOfWebContents(selection.old_contents) !=
          TabStripModel::kNoTab) {
    if (tabs::TabInterface* old =
            tabs::TabInterface::MaybeGetFromContents(selection.old_contents)) {
      for (tabs::TabInterface* tab : WithSplitPartners(tab_strip_model, old)) {
        Stamp(tab);
      }
    }
  }
}

void KoanTabAutoCloser::OnHistoryDeletions(
    history::HistoryService* history_service,
    const history::DeletionInfo& deletion_info) {
  // Expiry isn't the user's deletion; the list has its own.
  if (!browser_view_ || deletion_info.is_from_expiration()) {
    return;
  }
  // One profile's history, from the Library: DeleteProfileHistory() has
  // forgotten that profile's closed tabs, and this notice doesn't say which
  // profile it was.
  if (const ProfileHistoryDeletions* library =
          ProfileHistoryDeletions::Get(profile());
      library && library->Covers(deletion_info)) {
    return;
  }
  PrefService* prefs = profile()->GetPrefs();
  const base::ListValue& list = prefs->GetList(prefs::kAutoClosedTabs);
  if (list.empty()) {
    return;
  }
  std::optional<std::pair<base::Time, base::Time>> range;
  const history::DeletionTimeRange& time_range = deletion_info.time_range();
  if (time_range.IsValid() && !deletion_info.restrict_urls()) {
    // No end is up to now (ExpireHistoryBetween()'s).
    range.emplace(time_range.begin(), time_range.end().is_null()
                                          ? base::Time::Max()
                                          : time_range.end());
  }
  std::set<GURL> urls;
  if (!deletion_info.IsAllHistory()) {
    for (const history::URLRow& row : deletion_info.deleted_rows()) {
      urls.insert(row.url());
    }
  }
  base::ListValue kept = auto_close::ForgetDeletedHistory(
      list, deletion_info.IsAllHistory(), range, urls);
  if (kept.size() != list.size()) {
    prefs->SetList(prefs::kAutoClosedTabs, std::move(kept));
  }
}

void KoanTabAutoCloser::HistoryServiceBeingDeleted(
    history::HistoryService* history_service) {
  history_observation_.Reset();
}

std::vector<tabs::TabInterface*> KoanTabAutoCloser::VisibleTabs() const {
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  std::vector<tabs::TabInterface*> tabs;
  for (int i = 0; i < model->count(); ++i) {
    // The active tab and the tabs selected with it: its split view, or a
    // multi-selection (kept too, to be safe).
    if (model->IsTabSelected(i)) {
      tabs.push_back(model->GetTabAtIndex(i));
    }
  }
  return tabs;
}

void KoanTabAutoCloser::Stamp(tabs::TabInterface* tab) {
  content::WebContents* contents = tab->GetContents();
  if (!contents) {
    return;
  }
  ClearUnseen(tab);
  const base::Time now = base::Time::Now();
  const base::Time stored = auto_close::ParseTime(
      GetTabSessionData(contents, auto_close::kLastSeenSessionKey));
  if (!stored.is_null() && (now - stored).magnitude() < kStampGranularity) {
    return;
  }
  SetTabSessionData(contents, auto_close::kLastSeenSessionKey,
                    auto_close::SerializeTime(now));
}

void KoanTabAutoCloser::MarkUnseen(
    base::WeakPtr<content::WebContents> contents) {
  if (!browser_view_ || !contents) {
    return;
  }
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  const int index = model->GetIndexOfWebContents(contents.get());
  if (index == TabStripModel::kNoTab || model->IsTabSelected(index) ||
      !GetTabSessionData(contents.get(), auto_close::kLastSeenSessionKey)
           .empty() ||
      !GetTabSessionData(contents.get(), auto_close::kOpenedUnseenSessionKey)
           .empty()) {
    return;
  }
  // A tab never shown has the time its WebContents was made (after a
  // restart, the restore: a tab from before #577, which counts from then on);
  // one shown before a restart, its saved last-active time, which is what
  // it counted from anyway.
  SetTabSessionData(contents.get(), auto_close::kOpenedUnseenSessionKey,
                    auto_close::SerializeTime(std::min(
                        contents->GetLastActiveTime(), base::Time::Now())));
}

void KoanTabAutoCloser::ClearUnseen(tabs::TabInterface* tab) {
  content::WebContents* contents = tab->GetContents();
  if (contents &&
      !GetTabSessionData(contents, auto_close::kOpenedUnseenSessionKey)
           .empty()) {
    SetTabSessionData(contents, auto_close::kOpenedUnseenSessionKey,
                      std::string());
  }
}

auto_close::TabFacts KoanTabAutoCloser::FactsFor(
    tabs::TabInterface* tab,
    const std::vector<tabs::TabInterface*>& active) const {
  content::WebContents* contents = tab->GetContents();
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  KoanFolderController* folders = controller ? controller->folders() : nullptr;
  const bool unloaded = IsTabUnloaded(contents);
  return auto_close::TabFacts{
      .essential = tab->IsPinned(),
      .pinned = IsPinnedInSpace(tab),
      .in_folder =
          folders && folders->IsEnabled() && folders->GetFolderOf(tab) != 0,
      .active = std::ranges::contains(active, tab),
      .empty_tab = IsEmptyTab(tab),
      .agent = containers::IsAgentContents(contents),
      // An unloaded tab plays nothing, holds no form and has no handlers.
      .in_use = !unloaded && (IsTabInUse(contents) || IsDownloading(contents) ||
                              contents->NeedToFireBeforeUnload()),
      .last_used = auto_close::LastUsed(
          contents->GetLastActiveTime(),
          auto_close::ParseTime(
              GetTabSessionData(contents, auto_close::kLastSeenSessionKey)),
          auto_close::ParseTime(GetTabSessionData(
              contents, auto_close::kOpenedUnseenSessionKey))),
  };
}

void KoanTabAutoCloser::Check() {
  if (!browser_view_ || !limit_) {
    return;
  }
  // Not while the session comes back (the next check sees its tabs), nor in
  // the middle of a tab drag.
  if (SessionRestore::IsRestoring(profile()) || TabDragController::IsActive()) {
    return;
  }
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  std::vector<tabs::TabInterface*> active = VisibleTabs();
  for (tabs::TabInterface* tab : active) {
    Stamp(tab);
  }
  // The tab each other Space shows when switched to stays too. The Space on
  // screen shows the active tab, kept above; its "last active tab" can be a
  // background tab opened after it and never shown, which would never close.
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  if (KoanWorkspaceController* spaces =
          controller ? controller->workspaces() : nullptr;
      spaces && spaces->IsEnabled()) {
    for (const KoanWorkspaceController::SpaceInfo& space :
         spaces->GetSpaces()) {
      if (space.active) {
        continue;
      }
      if (tabs::TabInterface* shown = spaces->GetLastActiveTab(space.id)) {
        std::ranges::copy(WithSplitPartners(model, shown),
                          std::back_inserter(active));
      }
    }
  }

  std::map<tabs::TabInterface*, auto_close::TabFacts> facts;
  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (tab->GetContents()) {
      facts.emplace(tab, FactsFor(tab, active));
    }
  }
  const base::Time now = base::Time::Now();
  std::vector<tabs::TabHandle> to_close;
  for (const auto& [tab, tab_facts] : facts) {
    std::vector<auto_close::TabFacts> partners;
    for (tabs::TabInterface* partner : WithSplitPartners(model, tab)) {
      if (auto it = facts.find(partner); partner != tab && it != facts.end()) {
        partners.push_back(it->second);
      }
    }
    if (auto_close::WhyKept(tab_facts, partners, now, *limit_) ==
        auto_close::Keep::kNone) {
      to_close.push_back(tab->GetHandle());
    }
  }
  // A Space is a tab group, which goes with its last tab: one whose tabs
  // would all close keeps its most recently used (its last active tab
  // normally stays anyway).
  std::map<tab_groups::TabGroupId, int> open_in_space;
  for (int i = 0; i < model->count(); ++i) {
    if (std::optional<tab_groups::TabGroupId> group =
            model->GetTabAtIndex(i)->GetGroup()) {
      ++open_in_space[*group];
    }
  }
  for (const tabs::TabHandle& handle : to_close) {
    if (std::optional<tab_groups::TabGroupId> group =
            handle.Get()->GetGroup()) {
      --open_in_space[*group];
    }
  }
  for (const auto& [group, open] : open_in_space) {
    if (open > 0) {
      continue;
    }
    auto newest = std::ranges::max_element(
        to_close, {}, [&](const tabs::TabHandle& handle) {
          return handle.Get()->GetGroup() == group
                     ? facts.at(handle.Get()).last_used
                     : base::Time();
        });
    to_close.erase(newest);
  }
  if (to_close.empty() || !MayClose(to_close.size(), now)) {
    return;
  }
  for (const tabs::TabHandle& handle : to_close) {
    if (tabs::TabInterface* tab = handle.Get();
        tab && model->GetIndexOfTab(tab) != TabStripModel::kNoTab) {
      Close(tab);
    }
  }
}

void KoanTabAutoCloser::Close(tabs::TabInterface* tab) {
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  content::WebContents* contents = tab->GetContents();
  auto_close::ClosedTab record;
  record.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  record.url = CurrentTabUrl(contents);
  record.title = contents->GetTitle();
  record.closed = base::Time::Now();
  if (std::optional<tab_groups::TabGroupId> group = tab->GetGroup()) {
    record.space = group->ToString();
  }
  record.data = GetTabSessionData(contents);
  record.data.erase(auto_close::kLastSeenSessionKey);
  record.data.erase(auto_close::kOpenedUnseenSessionKey);
  if (record.url.is_valid()) {
    PrefService* prefs = profile()->GetPrefs();
    prefs->SetList(
        prefs::kAutoClosedTabs,
        auto_close::AddClosedTab(prefs->GetList(prefs::kAutoClosedTabs), record,
                                 record.closed));
  }
  // Not into Chromium's recently closed tabs: Cmd+Shift+T stays the user's.
  model->CloseWebContentsAt(model->GetIndexOfTab(tab),
                            TabCloseTypes::CLOSE_NONE);
}

bool KoanTabAutoCloser::MayClose(size_t count, base::Time now) {
  PrefService* prefs = profile()->GetPrefs();
  if (base::Time shown = prefs->GetTime(prefs::kAutoCloseNoticeTime);
      !shown.is_null()) {
    // Shown, by this window or another: nothing closes until it has been in
    // front of the user a while. A time ahead of the clock starts again.
    if (shown > now) {
      prefs->SetTime(prefs::kAutoCloseNoticeTime, now);
    }
    return auto_close::NoticeLongEnoughAgo(shown, now, *limit_);
  }
  // In the profile's front window, where it is seen.
  BrowserView* front = browser_view_;
  Profile* own_profile = profile();
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* browser) {
        if (browser->GetProfile() != own_profile ||
            browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
          return true;
        }
        if (BrowserView* view =
                BrowserView::GetBrowserViewForBrowser(browser)) {
          front = view;
        }
        return false;
      });
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(front);
  if (!controller || !controller->toast()) {
    return false;
  }
  controller->toast()->ShowWithAction(
      count == 1 ? u"A tab you haven't used for a while will close soon. "
                   u"Closed tabs are in the Library's History."
                 : u"Tabs you haven't used for a while will close soon. "
                   u"Closed tabs are in the Library's History.",
      kKoanSettingsIcon, u"Settings",
      base::BindOnce(&OpenTabSettings, own_profile), kNoticeTimeout);
  prefs->SetTime(prefs::kAutoCloseNoticeTime, now);
  return false;
}

std::vector<auto_close::ClosedTab> GetAutoClosedTabs(Profile* profile) {
  return auto_close::ReadClosedTabs(
      profile->GetPrefs()->GetList(prefs::kAutoClosedTabs), base::Time::Now());
}

bool ReopenAutoClosedTab(BrowserWindowInterface* browser,
                         const std::string& id,
                         bool background) {
  Profile* profile = browser->GetProfile();
  std::vector<auto_close::ClosedTab> tabs = GetAutoClosedTabs(profile);
  auto it = std::ranges::find(tabs, id, &auto_close::ClosedTab::id);
  if (it == tabs.end()) {
    return false;
  }
  const auto_close::ClosedTab tab = std::move(*it);
  ForgetAutoClosedTab(profile, id);

  TabStripModel* model = browser->GetTabStripModel();
  TabGroupModel* groups = model->group_model();
  // Its own Space if this window has it, else the one the user is in.
  std::optional<tab_groups::TabGroupId> group;
  if (std::optional<base::Token> token = base::Token::FromString(tab.space);
      token && groups &&
      groups->ContainsTabGroup(tab_groups::TabGroupId::FromRawToken(*token))) {
    group = tab_groups::TabGroupId::FromRawToken(*token);
  } else {
    group = model->GetFocusedGroup();
  }
  int index = model->count();
  if (group && groups && groups->ContainsTabGroup(*group)) {
    index = static_cast<int>(groups->GetTabGroup(*group)->ListTabs().end());
  } else {
    group.reset();
  }

  sessions::SerializedNavigationEntry navigation;
  navigation.set_index(0);
  navigation.set_virtual_url(tab.url);
  navigation.set_original_request_url(tab.url);
  navigation.set_title(tab.title);
  navigation.set_transition_type(ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  navigation.set_timestamp(tab.closed);
  const sessions::SerializedNavigationEntry navigations[] = {navigation};
  // As Recently Closed reopens a tab: Koan's data (its container, #318)
  // comes back through the restore hooks (koan_session_data.h).
  return chrome::AddRestoredTab(
             browser, navigations, index, /*selected_navigation=*/0,
             /*extension_app_id=*/std::string(), group,
             /*select=*/!background, /*pin=*/false, base::TimeTicks(),
             base::Time(), /*storage_namespace=*/nullptr,
             sessions::SerializedUserAgentOverride(), tab.data,
             /*from_session_restore=*/false,
             /*is_active_browser=*/std::nullopt) != nullptr;
}

void ForgetAutoClosedTab(Profile* profile, const std::string& id) {
  PrefService* prefs = profile->GetPrefs();
  prefs->SetList(
      prefs::kAutoClosedTabs,
      auto_close::RemoveClosedTab(prefs->GetList(prefs::kAutoClosedTabs), id));
}

void DeleteProfileHistory(Profile* profile,
                          history::HistoryService* history,
                          std::string_view history_profile,
                          const std::set<GURL>& urls,
                          base::Time begin) {
  PrefService* prefs = profile->GetPrefs();
  const base::ListValue& list = prefs->GetList(prefs::kAutoClosedTabs);
  const size_t count = list.size();
  std::optional<std::pair<base::Time, base::Time>> range;
  if (urls.empty() && !begin.is_null()) {
    range.emplace(begin, base::Time::Max());
  }
  base::ListValue kept = auto_close::ForgetDeletedHistory(
      list, /*all=*/urls.empty() && begin.is_null(), range, urls,
      [&](const auto_close::ClosedTab& tab) {
        return HistoryProfileOf(tab.data) == history_profile;
      });
  if (kept.size() != count) {
    prefs->SetList(prefs::kAutoClosedTabs, std::move(kept));
  }
  if (!history) {
    return;
  }
  ProfileHistoryDeletions* deletions =
      ProfileHistoryDeletions::GetOrCreate(profile);
  history->ScheduleDBTask(
      FROM_HERE,
      std::make_unique<RemoveProfileVisitsTask>(
          containers::HistoryAppIdForProfile(history_profile), urls, begin,
          deletions->GetWeakPtr(), deletions->Add()),
      deletions->tracker());
}

}  // namespace koan
