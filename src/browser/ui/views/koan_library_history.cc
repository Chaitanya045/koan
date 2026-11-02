// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenLibraryHistorySection.mjs
//   zen-library.ftl (strings)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/i18n/time_formatting.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_constants.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser_live_tab_context.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/favicon/core/favicon_service.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_service_observer.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/common/pref_names.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_service_observer.h"
#include "components/sessions/core/tab_restore_types.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/common/referrer.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_library.h"
#include "koan/browser/ui/views/koan_library_sections.h"
#include "koan/browser/ui/views/koan_library_view.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/event.h"
#include "ui/gfx/favicon_size.h"
#include "url/gurl.h"

namespace koan {

namespace {

// Zen: PAGE_SIZE, and how far back history goes without a "when" filter.
constexpr int kPageSize = 100;
constexpr int kHistoryDays = 120;
// Most-visited sorts this many recent pages by their visit counts.
constexpr int kMostVisitedPool = 1000;
// History changes arrive in bursts (a page load is several notifications).
constexpr base::TimeDelta kRefreshDelay = base::Milliseconds(300);

// Zen's CLEAR_RANGES.
struct ClearRange {
  const char16_t* label;
  // What the confirmation asks.
  const char16_t* title;
  int hours;  // 0: since midnight; -1: everything.
};
constexpr ClearRange kClearRanges[] = {
    {u"Last hour", u"Clear history from the last hour?", 1},
    {u"Last 12 hours", u"Clear history from the last 12 hours?", 12},
    {u"Today", u"Clear today's history?", 0},
    {u"Last week", u"Clear history from the last week?", 24 * 7},
    {u"Last month", u"Clear history from the last month?", 24 * 30},
    {u"Everything", u"Clear all history?", -1},
};

// "https://www.example.com/a/" -> "example.com/a" (Zen's #formatUrl).
std::u16string FormatUrl(const GURL& url) {
  std::string text = url.spec();
  for (const char* prefix : {"https://", "http://"}) {
    if (base::StartsWith(text, prefix)) {
      text.erase(0, strlen(prefix));
      if (base::StartsWith(text, "www.")) {
        text.erase(0, 4);
      }
      break;
    }
  }
  if (base::EndsWith(text, "/")) {
    text.pop_back();
  }
  return base::UTF8ToUTF16(text);
}

// "Oct 12, 2026, 3:45 PM" (Intl dateStyle medium + timeStyle short).
std::u16string FormatVisit(base::Time time) {
  return base::StrCat({base::TimeFormatShortDate(time), u", ",
                       base::TimeFormatTimeOfDay(time)});
}

// "Today", "Yesterday", "3 days ago" within a week, then the date.
std::u16string FormatDay(base::Time day, base::Time today) {
  const int days_ago = std::lround((today - day) / base::Days(1));
  if (days_ago <= 1) {
    const std::u16string relative = ui::TimeFormat::RelativeDate(day, today);
    if (!relative.empty()) {
      return relative;
    }
  }
  if (days_ago <= 6) {
    return ui::TimeFormat::Simple(ui::TimeFormat::FORMAT_ELAPSED,
                                  ui::TimeFormat::LENGTH_LONG,
                                  base::Days(days_ago));
  }
  return base::TimeFormatShortDate(day);
}

// The site a page belongs to, as Zen groups "By Site": its host without
// "www.", or nothing for pages that aren't on the web.
std::u16string SiteOf(const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return std::u16string();
  }
  std::string host(url.host());
  if (base::StartsWith(host, "www.")) {
    host.erase(0, 4);
  }
  return base::UTF8ToUTF16(host);
}

}  // namespace

// Zen's History section (ZenLibraryHistorySection.mjs): history of the last
// 120 days, searchable, filtered by when it was visited, sorted by date,
// site, visit count or last visit, or the tabs closed recently instead.
class KoanLibraryHistorySection : public KoanLibrarySearchSection,
                                  public history::HistoryServiceObserver,
                                  public sessions::TabRestoreServiceObserver {
  METADATA_HEADER(KoanLibraryHistorySection, KoanLibrarySearchSection)

 public:
  explicit KoanLibraryHistorySection(KoanLibraryController* library)
      : KoanLibrarySearchSection(library,
                                 LibrarySection::kHistory,
                                 u"Search History…",
                                 u"Filter History…"),
        profile_(library->browser_view()->GetProfile()) {
    history_ = HistoryServiceFactory::GetForProfile(
        profile_, ServiceAccessType::EXPLICIT_ACCESS);
    if (history_) {
      history_observation_.Observe(history_);
    }
    tab_restore_ = TabRestoreServiceFactory::GetForProfile(profile_);
    if (tab_restore_) {
      restore_observation_.Observe(tab_restore_);
    }
    SetFilterActive("sort", "date", true);
    SetFilterActive(
        "source",
        profile_->GetPrefs()->GetBoolean(prefs::kLibraryHistoryClosedTabs)
            ? "closed"
            : "history",
        true);
    InitFilters();
  }
  ~KoanLibraryHistorySection() override = default;

  int GetRowCount() const { return static_cast<int>(rows_.size()); }

  // KoanLibrarySection:
  void OnShown() override {
    KoanLibrarySearchSection::OnShown();
    shown_ = true;
    if (dirty_) {
      ResetAndFetch();
    }
  }

  void FillTabMenu(KoanLibraryMenu* menu) override {
    const bool may_delete = IsDeletingHistoryAllowed(profile_);
    KoanLibraryMenu* clear = menu->AddSubMenu(u"Clear history");
    for (const ClearRange& range : kClearRanges) {
      if (range.hours < 0) {
        clear->AddSeparator();
      }
      clear->AddItem(
          range.label,
          base::BindRepeating(&KoanLibraryHistorySection::ConfirmClearHistory,
                              weak_factory_.GetWeakPtr(), range),
          may_delete);
    }
    menu->AddSeparator();
    menu->AddItem(u"Open the History page",
                  base::BindRepeating(&KoanLibraryHistorySection::OpenPage,
                                      weak_factory_.GetWeakPtr(),
                                      GURL("chrome://history/")));
  }

  // KoanLibrarySearchSection:
  std::vector<FilterGroup> GetFilterGroups() const override {
    return {
        WhenFilterGroup(u"When was it visited?"),
        FilterGroup("sort", u"Sort by", /*exclusive=*/true,
                    {{"date", u"By Date"},
                     {"site", u"By Site"},
                     // A closed tab was never counted as visited.
                     {"mostvisited", u"By Most Visited", ShowingClosed()},
                     {"lastvisited", u"By Last Visited"}}),
        FilterGroup("source", u"What to look through", /*exclusive=*/true,
                    {{"history", u"History"}, {"closed", u"Closed tabs"}}),
    };
  }

  void OnSearchChanged() override { ResetAndFetch(); }

  void OnFiltersChanged() override {
    // One source and one sort order are always on.
    if (!ShowingClosed() && !IsFilterActive("source", "history")) {
      SetFilterActive("source", "history", true);
    }
    if (ShowingClosed() && IsFilterActive("sort", "mostvisited")) {
      SetFilterActive("sort", "mostvisited", false);
    }
    if (ActiveSort().empty()) {
      SetFilterActive("sort", "date", true);
    }
    profile_->GetPrefs()->SetBoolean(prefs::kLibraryHistoryClosedTabs,
                                     ShowingClosed());
    ResetAndFetch();
  }

  void OnScrolledToEnd() override { FetchMore(); }

  // history::HistoryServiceObserver:
  void OnURLVisited(history::HistoryService* history_service,
                    const history::VisitedURLInfo& visited_url_info) override {
    OnDataChanged(/*closed_tabs=*/false);
  }
  void OnURLsModified(history::HistoryService* history_service,
                      const history::URLRows& changed_urls) override {
    OnDataChanged(/*closed_tabs=*/false);
  }
  void OnHistoryDeletions(history::HistoryService* history_service,
                          const history::DeletionInfo& deletion_info) override {
    OnDataChanged(/*closed_tabs=*/false);
  }
  void HistoryServiceBeingDeleted(
      history::HistoryService* history_service) override {
    history_observation_.Reset();
    history_ = nullptr;
  }

  // sessions::TabRestoreServiceObserver:
  void TabRestoreServiceChanged(sessions::TabRestoreService* service) override {
    OnDataChanged(/*closed_tabs=*/true);
  }
  void TabRestoreServiceLoaded(sessions::TabRestoreService* service) override {
    OnDataChanged(/*closed_tabs=*/true);
  }
  void TabRestoreServiceDestroyed(
      sessions::TabRestoreService* service) override {
    restore_observation_.Reset();
    tab_restore_ = nullptr;
  }

 private:
  // A row: a page from history, or a closed tab.
  struct Entry {
    GURL url;
    std::u16string title;
    base::Time time;
    int visit_count = 0;
    std::optional<SessionID> closed_id;
  };

  bool ShowingClosed() const { return IsFilterActive("source", "closed"); }

  std::string ActiveSort() const {
    for (const char* sort : {"date", "site", "mostvisited", "lastvisited"}) {
      if (IsFilterActive("sort", sort)) {
        return sort;
      }
    }
    return std::string();
  }

  base::Time Cutoff() const {
    return GetWhenCutoff().value_or(base::Time::Now() -
                                    base::Days(kHistoryDays));
  }

  // Whether rows are pages of history the user may delete (policy
  // AllowDeletingBrowserHistory); closed tabs can always be forgotten.
  bool MayForget(const Entry& entry) const {
    return entry.closed_id || IsDeletingHistoryAllowed(profile_);
  }

  void OnDataChanged(bool closed_tabs) {
    if (closed_tabs != ShowingClosed()) {
      return;
    }
    if (!shown_ || !GetVisible() || !library()->is_open()) {
      dirty_ = true;
      return;
    }
    // Fetched again as far as the list went.
    refresh_timer_.Start(
        FROM_HERE, kRefreshDelay,
        base::BindOnce(&KoanLibraryHistorySection::Fetch,
                       base::Unretained(this),
                       std::max(kPageSize, static_cast<int>(entries_.size()))));
  }

  void ResetAndFetch() { Fetch(kPageSize); }

  // A fresh list, of `count` rows if there are that many.
  void Fetch(int count) {
    dirty_ = false;
    fetching_ = false;
    exhausted_ = false;
    pool_.clear();
    task_tracker_.TryCancelAll();
    if (ShowingClosed()) {
      FetchClosedTabs();
      return;
    }
    if (!history_) {
      Show({});
      return;
    }
    // Most visited sorts the recent pages by their visit counts, and pages
    // through them without asking again.
    Query(base::Time(),
          ActiveSort() == "mostvisited" ? kMostVisitedPool : count, count,
          /*append=*/false);
  }

  // The next page, older than the rows shown.
  void FetchMore() {
    if (exhausted_ || fetching_) {
      return;
    }
    if (ActiveSort() == "mostvisited") {
      const size_t count = std::min(pool_.size(), size_t{kPageSize});
      std::vector<Entry> next(std::make_move_iterator(pool_.begin()),
                              std::make_move_iterator(pool_.begin() + count));
      pool_.erase(pool_.begin(), pool_.begin() + count);
      exhausted_ = pool_.empty();
      Append(std::move(next));
      return;
    }
    Query(oldest_, kPageSize, kPageSize, /*append=*/true);
  }

  // History from Cutoff() to `end` (now if null), newest first.
  void Query(base::Time end, int max_count, int show_count, bool append) {
    history::QueryOptions options;
    options.begin_time = Cutoff();
    options.end_time = end;
    options.max_count = max_count;
    options.policy_for_404_visits = history::VisitQuery404sPolicy::kExclude404s;
    options.duplicate_policy =
        PerDay() ? history::QueryOptions::REMOVE_DUPLICATES_PER_DAY
                 : history::QueryOptions::REMOVE_ALL_DUPLICATES;
    fetching_ = true;
    history_->QueryHistory(
        query(), options,
        base::BindOnce(&KoanLibraryHistorySection::OnHistoryResults,
                       base::Unretained(this), max_count, show_count, append),
        &task_tracker_);
  }

  void OnHistoryResults(int max_count,
                        int show_count,
                        bool append,
                        history::QueryResults results) {
    fetching_ = false;
    std::vector<Entry> entries;
    for (const history::URLResult& result : results) {
      entries.push_back({result.url(), result.title(), result.visit_time(),
                         result.visit_count(), std::nullopt});
    }
    if (!entries.empty()) {
      oldest_ = entries.back().time;
    }
    exhausted_ = results.reached_beginning() ||
                 static_cast<int>(entries.size()) < max_count;
    if (ActiveSort() == "mostvisited") {
      std::ranges::stable_sort(entries, std::greater<>(), &Entry::visit_count);
      pool_.clear();
      if (static_cast<int>(entries.size()) > show_count) {
        pool_.assign(std::make_move_iterator(entries.begin() + show_count),
                     std::make_move_iterator(entries.end()));
        entries.resize(show_count);
      }
      exhausted_ = pool_.empty();
    }
    if (append) {
      // A page of nothing but pages already listed (older visits to them)
      // changes nothing on screen, so no scroll would ask for the next one:
      // ask now.
      if (Append(std::move(entries)) == 0 && !exhausted_) {
        FetchMore();
      }
    } else {
      Show(std::move(entries));
    }
  }

  void FetchClosedTabs() {
    exhausted_ = true;
    if (!tab_restore_) {
      Show({});
      return;
    }
    if (!tab_restore_->IsLoaded()) {
      // TabRestoreServiceLoaded() fetches again. Nothing from the other
      // source meanwhile.
      ResetList();
      ShowEmpty(u"Loading closed tabs…");
      tab_restore_->LoadTabsFromLastSession();
      return;
    }
    const base::Time cutoff = Cutoff();
    const std::u16string needle = base::i18n::ToLower(query());
    std::vector<Entry> entries;
    for (const auto& entry : tab_restore_->entries()) {
      if (entry->type != sessions::tab_restore::Type::TAB ||
          entry->timestamp < cutoff) {
        continue;
      }
      const auto& tab = static_cast<const sessions::tab_restore::Tab&>(*entry);
      if (tab.navigations.empty()) {
        continue;
      }
      const sessions::SerializedNavigationEntry& navigation =
          tab.navigations[tab.normalized_navigation_index()];
      std::u16string title = navigation.title();
      if (title.empty()) {
        title = base::UTF8ToUTF16(navigation.virtual_url().spec());
      }
      if (!needle.empty() &&
          base::i18n::ToLower(
              base::StrCat(
                  {title, u" ",
                   base::UTF8ToUTF16(navigation.virtual_url().spec())}))
                  .find(needle) == std::u16string::npos) {
        continue;
      }
      entries.push_back({navigation.virtual_url(), std::move(title),
                         entry->timestamp, 0, entry->id});
    }
    Show(std::move(entries));
  }

  // Whether a page is listed once a day (By Date) rather than once.
  bool PerDay() const { return ActiveSort() == "date" && query().empty(); }

  void ResetList() {
    favicon_tracker_.TryCancelAll();
    // Forget the rows and groups before they are destroyed; callbacks that
    // still come for them find a newer generation.
    rows_.clear();
    ++generation_;
    flat_group_ = nullptr;
    day_group_ = nullptr;
    site_groups_.clear();
    ClearList();
    entries_.clear();
    shown_keys_.clear();
  }

  // Lists `entries` (newest first) afresh.
  void Show(std::vector<Entry> entries) {
    ResetList();
    if (entries.empty()) {
      ShowEmpty(ShowingClosed() ? u"No closed tabs" : u"No history found");
      InvalidateLayout();
      return;
    }
    Append(std::move(entries));
  }

  // Adds `entries` (older than those shown) to the list, in the groups of
  // the sort order. Pages a page before already showed are skipped. Returns
  // how many rows it added.
  size_t Append(std::vector<Entry> entries) {
    const size_t before = entries_.size();
    const bool per_day = PerDay();
    for (Entry& entry : entries) {
      if (!entry.closed_id &&
          !shown_keys_
               .insert({entry.url,
                        per_day ? entry.time.LocalMidnight() : base::Time()})
               .second) {
        continue;
      }
      AddRow(GroupFor(entry), entry);
      entries_.push_back(std::move(entry));
    }
    InvalidateLayout();
    return entries_.size() - before;
  }

  // The group `entry` goes in: its day (By Date), its site (By Site, sites
  // by name and "Other" last), or one list.
  views::View* GroupFor(const Entry& entry) {
    const std::string sort = ActiveSort();
    if (!query().empty() || sort == "mostvisited" || sort == "lastvisited") {
      if (!flat_group_) {
        flat_group_ = AddGroup(std::u16string());
      }
      return flat_group_;
    }
    if (sort == "site") {
      const std::u16string site = SiteOf(entry.url);
      auto it = site_groups_.find(site);
      if (it != site_groups_.end()) {
        return it->second;
      }
      size_t index = site_groups_.size();
      if (!site.empty()) {
        index = static_cast<size_t>(
            std::ranges::count_if(site_groups_, [&](const auto& other) {
              return !other.first.empty() && other.first < site;
            }));
      }
      views::View* group = AddGroup(site.empty() ? u"Other" : site, index);
      site_groups_[site] = group;
      return group;
    }
    const base::Time day = entry.time.LocalMidnight();
    if (!day_group_ || day != day_) {
      day_ = day;
      day_group_ = AddGroup(FormatDay(day, base::Time::Now().LocalMidnight()));
    }
    return day_group_;
  }

  void AddRow(views::View* group, const Entry& entry) {
    auto row = std::make_unique<KoanLibraryRow>(base::BindRepeating(
        &KoanLibraryHistorySection::OpenEntry, base::Unretained(this), entry));
    row->SetTitle(entry.title.empty() ? base::UTF8ToUTF16(entry.url.spec())
                                      : entry.title);
    // The address, or when it was visited while the row is hovered.
    row->SetSubtitle(FormatUrl(entry.url), FormatVisit(entry.time));
    row->SetIcon(ui::ImageModel::FromVectorIcon(
                     vector_icons::kGlobeIcon, kColorKoanLibraryForeground, 16),
                 16);
    if (MayForget(entry)) {
      row->AddAction(library::CreateIconButton(
          base::BindRepeating(&KoanLibraryHistorySection::ForgetEntry,
                              base::Unretained(this), entry),
          kDeleteIcon, u"Remove from history", library::kRowButtonIconSize,
          library::kRowButtonPadding, library::kPillRadius));
    }
    row->AddAction(library::CreateIconButton(
        base::BindRepeating(&KoanLibraryHistorySection::OpenEntry,
                            base::Unretained(this), entry),
        vector_icons::kUndoIcon, u"Reopen page", library::kRowButtonIconSize,
        library::kRowButtonPadding, library::kPillRadius));
    row->SetMenuCallback(
        base::BindRepeating(&KoanLibraryHistorySection::ShowRowMenu,
                            base::Unretained(this), entry));
    rows_.push_back(group->AddChildView(std::move(row)));
    if (favicon::FaviconService* favicons =
            FaviconServiceFactory::GetForProfile(
                profile_, ServiceAccessType::EXPLICIT_ACCESS)) {
      favicons->GetFaviconImageForPageURL(
          entry.url,
          base::BindOnce(&KoanLibraryHistorySection::OnFavicon,
                         weak_factory_.GetWeakPtr(), generation_,
                         rows_.size() - 1),
          &favicon_tracker_);
    }
  }

  // The row `index` of list `generation`, if that list is still shown.
  KoanLibraryRow* RowAt(int generation, size_t index) const {
    return generation == generation_ && index < rows_.size() ? rows_[index]
                                                             : nullptr;
  }

  void OnFavicon(int generation,
                 size_t index,
                 const favicon_base::FaviconImageResult& result) {
    KoanLibraryRow* row = RowAt(generation, index);
    if (row && !result.image.IsEmpty()) {
      row->SetIcon(ui::ImageModel::FromImage(result.image), gfx::kFaviconSize);
    }
  }

  // A click opens the page in a new tab and closes the library; Ctrl+click
  // (Cmd+click on macOS) or a middle click opens it in the background and
  // keeps the library.
  void OpenEntry(const Entry& entry, const ui::Event& event) {
    const bool background = event.flags() & (ui::EF_PLATFORM_ACCELERATOR |
                                             ui::EF_MIDDLE_MOUSE_BUTTON);
    Open(entry, background);
  }

  void Open(const Entry& entry, bool background) {
    BrowserWindowInterface* browser = library()->browser_view()->browser();
    const WindowOpenDisposition disposition =
        background ? WindowOpenDisposition::kNewBackgroundTab
                   : WindowOpenDisposition::kNewForegroundTab;
    base::OnceClosure open_tab;
    if (entry.closed_id) {
      open_tab = base::BindOnce(
          [](sessions::TabRestoreService* service,
             BrowserWindowInterface* browser, SessionID id,
             WindowOpenDisposition disposition) {
            if (service) {
              service->RestoreEntryById(BrowserLiveTabContext::From(browser),
                                        id, disposition);
            }
          },
          tab_restore_, browser, *entry.closed_id, disposition);
    } else {
      open_tab = base::BindOnce(
          [](BrowserWindowInterface* browser, const GURL& url,
             WindowOpenDisposition disposition) {
            browser->OpenURL(
                content::OpenURLParams(url, content::Referrer(), disposition,
                                       ui::PAGE_TRANSITION_AUTO_BOOKMARK,
                                       /*is_renderer_initiated=*/false),
                /*navigation_handle_callback=*/{});
          },
          browser, entry.url, disposition);
    }
    if (!background) {
      std::move(open_tab).Run();
      library()->Close();
      return;
    }
    library()->KeepOpenWhile(std::move(open_tab));
    if (KoanBrowserViewController* controller =
            KoanBrowserViewController::From(library()->browser_view())) {
      controller->toast()->Show(u"Opened tab in a new tab");
    }
  }

  void OpenPage(const GURL& url) {
    Open({url, std::u16string(), base::Time(), 0, std::nullopt},
         /*background=*/false);
  }

  void ForgetEntry(const Entry& entry) {
    if (!MayForget(entry)) {
      return;
    }
    if (entry.closed_id) {
      if (tab_restore_) {
        tab_restore_->RemoveEntryById(*entry.closed_id);
      }
    } else if (history_) {
      history_->DeleteURLs({entry.url});
    }
    // Drop the row now, like Zen; the services' notifications follow. Not
    // from inside the row's own button, which the rebuild destroys.
    const auto same = [&](const Entry& other) {
      return other.closed_id ? other.closed_id == entry.closed_id
                             : !entry.closed_id && other.url == entry.url;
    };
    std::erase_if(entries_, same);
    std::erase_if(pool_, same);
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<KoanLibraryHistorySection> section) {
                         if (section) {
                           section->Show(std::move(section->entries_));
                         }
                       },
                       weak_factory_.GetWeakPtr()));
  }

  // Firefox's "Forget About This Site": the site's history, cookies, site
  // data and cache.
  void ForgetSite(const Entry& entry) {
    if (!IsDeletingHistoryAllowed(profile_)) {
      return;
    }
    std::string site = net::registry_controlled_domains::GetDomainAndRegistry(
        entry.url,
        net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
    if (site.empty()) {
      site = std::string(entry.url.host());
    }
    if (site.empty()) {
      return;
    }
    ShowLibraryConfirmation(
        library(), u"Forget About This Site",
        base::StrCat({u"This will remove all data related to ",
                      base::UTF8ToUTF16(site),
                      u", including history, cookies, cache and site data. "
                      u"Bookmarks and passwords are kept."}),
        u"Forget",
        base::BindOnce(&KoanLibraryHistorySection::DoForgetSite,
                       weak_factory_.GetWeakPtr(), site));
  }

  void DoForgetSite(const std::string& site) {
    if (history_ && IsDeletingHistoryAllowed(profile_)) {
      history::QueryOptions options;
      options.hostname_suffix = site;
      history_->QueryHistory(
          std::u16string(), options,
          base::BindOnce(
              [](base::WeakPtr<KoanLibraryHistorySection> section,
                 history::QueryResults results) {
                if (!section || !section->history_) {
                  return;
                }
                std::vector<GURL> urls;
                for (const history::URLResult& result : results) {
                  urls.push_back(result.url());
                }
                section->history_->DeleteURLs(urls);
              },
              weak_factory_.GetWeakPtr()),
          &forget_tracker_);
    }
    auto filter = content::BrowsingDataFilterBuilder::Create(
        content::BrowsingDataFilterBuilder::Mode::kDelete);
    filter->AddRegisterableDomain(site);
    profile_->GetBrowsingDataRemover()->RemoveWithFilter(
        base::Time(), base::Time::Max(),
        chrome_browsing_data_remover::DATA_TYPE_SITE_DATA |
            content::BrowsingDataRemover::DATA_TYPE_CACHE,
        content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB |
            content::BrowsingDataRemover::ORIGIN_TYPE_PROTECTED_WEB,
        std::move(filter));
  }

  void ShowRowMenu(const Entry& entry,
                   KoanLibraryRow* row,
                   const gfx::Point& point,
                   ui::mojom::MenuSourceType source_type) {
    auto menu = std::make_unique<KoanLibraryMenu>();
    menu->AddItem(u"Reopen Page",
                  base::BindRepeating(&KoanLibraryHistorySection::Open,
                                      weak_factory_.GetWeakPtr(), entry,
                                      /*background=*/false));
    menu->AddSeparator();
    if (MayForget(entry)) {
      menu->AddItem(u"Forget This Page",
                    base::BindRepeating(&KoanLibraryHistorySection::ForgetEntry,
                                        weak_factory_.GetWeakPtr(), entry));
    }
    if (!entry.closed_id && IsDeletingHistoryAllowed(profile_)) {
      menu->AddItem(u"Forget About This Site",
                    base::BindRepeating(&KoanLibraryHistorySection::ForgetSite,
                                        weak_factory_.GetWeakPtr(), entry),
                    entry.url.has_host());
    }
    row->SetMenuOpen(true);
    const size_t index =
        static_cast<size_t>(std::ranges::find(rows_, row) - rows_.begin());
    RunMenu(std::move(menu), row, gfx::Rect(point, gfx::Size()),
            views::MenuAnchorPosition::kTopLeft, source_type,
            base::BindRepeating(
                [](base::WeakPtr<KoanLibraryHistorySection> section,
                   int generation, size_t row_index) {
                  // The list may have been rebuilt by now.
                  if (KoanLibraryRow* shown =
                          section ? section->RowAt(generation, row_index)
                                  : nullptr) {
                    shown->SetMenuOpen(false);
                  }
                },
                weak_factory_.GetWeakPtr(), generation_, index));
  }

  // Zen's confirmation, naming the range ("Clear history from the last
  // hour?").
  void ConfirmClearHistory(const ClearRange& range) {
    ShowLibraryConfirmation(
        library(), range.title,
        u"The pages you visited in this time frame will be forgotten. This "
        u"cannot be undone.",
        u"Clear history",
        base::BindOnce(&KoanLibraryHistorySection::ClearHistory,
                       weak_factory_.GetWeakPtr(), range.hours));
  }

  void ClearHistory(int hours) {
    if (!IsDeletingHistoryAllowed(profile_)) {
      return;
    }
    const base::Time now = base::Time::Now();
    base::Time begin;
    if (hours == 0) {
      begin = now.LocalMidnight();
    } else if (hours > 0) {
      begin = now - base::Hours(hours);
    }
    profile_->GetBrowsingDataRemover()->Remove(
        begin, base::Time::Max(),
        chrome_browsing_data_remover::DATA_TYPE_HISTORY,
        content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB);
  }

  const raw_ptr<Profile> profile_;
  raw_ptr<history::HistoryService> history_ = nullptr;
  raw_ptr<sessions::TabRestoreService> tab_restore_ = nullptr;
  base::ScopedObservation<history::HistoryService,
                          history::HistoryServiceObserver>
      history_observation_{this};
  base::ScopedObservation<sessions::TabRestoreService,
                          sessions::TabRestoreServiceObserver>
      restore_observation_{this};
  // What the rows show, and (by most visited) what's still to show.
  std::vector<Entry> entries_;
  std::vector<Entry> pool_;
  // Pages listed, with their day when listed once a day.
  std::set<std::pair<GURL, base::Time>> shown_keys_;
  // The oldest visit listed: the next page is older.
  base::Time oldest_;
  // The groups rows are added to.
  raw_ptr<views::View> flat_group_ = nullptr;
  raw_ptr<views::View> day_group_ = nullptr;
  base::Time day_;
  std::map<std::u16string, raw_ptr<views::View>> site_groups_;
  // The rows shown, and which list they are (Show() starts a new one).
  std::vector<raw_ptr<KoanLibraryRow>> rows_;
  int generation_ = 0;
  bool exhausted_ = false;
  bool fetching_ = false;
  // Data is fetched when the section is first shown, and again then if it
  // changed while hidden.
  bool shown_ = false;
  bool dirty_ = true;
  base::OneShotTimer refresh_timer_;
  base::CancelableTaskTracker task_tracker_;
  base::CancelableTaskTracker forget_tracker_;
  base::CancelableTaskTracker favicon_tracker_;
  base::WeakPtrFactory<KoanLibraryHistorySection> weak_factory_{this};
};

BEGIN_METADATA(KoanLibraryHistorySection)
ADD_READONLY_PROPERTY_METADATA(int, RowCount)
END_METADATA

std::unique_ptr<KoanLibrarySection> CreateLibraryHistorySection(
    KoanLibraryController* library) {
  return std::make_unique<KoanLibraryHistorySection>(library);
}

}  // namespace koan
