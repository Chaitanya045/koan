// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
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
  int hours;  // 0: since midnight; -1: everything.
};
constexpr ClearRange kClearRanges[] = {
    {u"Last hour", 1},      {u"Last 12 hours", 12},   {u"Today", 0},
    {u"Last week", 24 * 7}, {u"Last month", 24 * 30}, {u"Everything", -1},
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

  int GetRowCount() const { return row_count_; }

  // KoanLibrarySection:
  void OnShown() override {
    KoanLibrarySearchSection::OnShown();
    shown_ = true;
    if (dirty_) {
      ResetAndFetch();
    }
  }

  void FillTabMenu(KoanLibraryMenu* menu) override {
    const bool may_delete =
        profile_->GetPrefs()->GetBoolean(::prefs::kAllowDeletingBrowserHistory);
    KoanLibraryMenu* clear = menu->AddSubMenu(u"Clear history");
    for (const ClearRange& range : kClearRanges) {
      if (range.hours < 0) {
        clear->AddSeparator();
      }
      clear->AddItem(
          range.label,
          base::BindRepeating(&KoanLibraryHistorySection::ConfirmClearHistory,
                              weak_factory_.GetWeakPtr(), range.hours),
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

  void OnScrolledToEnd() override {
    if (exhausted_ || fetching_) {
      return;
    }
    limit_ += kPageSize;
    Fetch();
  }

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
    return base::Time::Now() - base::Days(GetWhenDays().value_or(kHistoryDays));
  }

  void OnDataChanged(bool closed_tabs) {
    if (closed_tabs != ShowingClosed()) {
      return;
    }
    if (!shown_ || !GetVisible() || !library()->is_open()) {
      dirty_ = true;
      return;
    }
    refresh_timer_.Start(FROM_HERE, kRefreshDelay,
                         base::BindOnce(&KoanLibraryHistorySection::Fetch,
                                        base::Unretained(this)));
  }

  void ResetAndFetch() {
    limit_ = kPageSize;
    exhausted_ = false;
    Fetch();
  }

  void Fetch() {
    dirty_ = false;
    task_tracker_.TryCancelAll();
    if (ShowingClosed()) {
      FetchClosedTabs();
      return;
    }
    if (!history_) {
      Show({});
      return;
    }
    history::QueryOptions options;
    options.begin_time = Cutoff();
    options.policy_for_404_visits = history::VisitQuery404sPolicy::kExclude404s;
    const std::string sort = ActiveSort();
    if (sort == "mostvisited" && query().empty()) {
      options.max_count = kMostVisitedPool;
    } else {
      options.max_count = limit_;
    }
    options.duplicate_policy =
        sort == "date" && query().empty()
            ? history::QueryOptions::REMOVE_DUPLICATES_PER_DAY
            : history::QueryOptions::REMOVE_ALL_DUPLICATES;
    fetching_ = true;
    history_->QueryHistory(
        query(), options,
        base::BindOnce(&KoanLibraryHistorySection::OnHistoryResults,
                       base::Unretained(this)),
        &task_tracker_);
  }

  void OnHistoryResults(history::QueryResults results) {
    fetching_ = false;
    std::vector<Entry> entries;
    for (const history::URLResult& result : results) {
      entries.push_back({result.url(), result.title(), result.visit_time(),
                         result.visit_count(), std::nullopt});
    }
    if (ActiveSort() == "mostvisited" && query().empty()) {
      std::ranges::stable_sort(entries, std::greater<>(), &Entry::visit_count);
      exhausted_ = static_cast<int>(entries.size()) <= limit_;
      if (!exhausted_) {
        entries.resize(limit_);
      }
    } else {
      exhausted_ = static_cast<int>(entries.size()) < limit_;
    }
    Show(std::move(entries));
  }

  void FetchClosedTabs() {
    exhausted_ = true;
    if (!tab_restore_) {
      Show({});
      return;
    }
    if (!tab_restore_->IsLoaded()) {
      // TabRestoreServiceLoaded() fetches again.
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

  // Lists `entries` (newest first) the way the sort order groups them.
  void Show(std::vector<Entry> entries) {
    favicon_tracker_.TryCancelAll();
    ClearList();
    row_count_ = 0;
    entries_ = std::move(entries);
    if (entries_.empty()) {
      ShowEmpty(ShowingClosed() ? u"No closed tabs" : u"No history found");
      InvalidateLayout();
      return;
    }
    const std::string sort = ActiveSort();
    if (!query().empty() || sort == "mostvisited" || sort == "lastvisited") {
      if (sort == "lastvisited") {
        std::ranges::stable_sort(entries_, std::greater<>(), &Entry::time);
      }
      views::View* group = AddGroup(std::u16string());
      for (const Entry& entry : entries_) {
        AddRow(group, entry);
      }
    } else if (sort == "site") {
      // By name, with what isn't on a site last ("Other").
      std::map<std::u16string, std::vector<const Entry*>> sites;
      for (const Entry& entry : entries_) {
        sites[SiteOf(entry.url)].push_back(&entry);
      }
      const auto add_site = [&](const std::u16string& site) {
        auto it = sites.find(site);
        if (it == sites.end()) {
          return;
        }
        views::View* group = AddGroup(site.empty() ? u"Other" : site);
        for (const Entry* entry : it->second) {
          AddRow(group, *entry);
        }
      };
      for (const auto& [site, unused] : sites) {
        if (!site.empty()) {
          add_site(site);
        }
      }
      add_site(std::u16string());
    } else {
      const base::Time today = base::Time::Now().LocalMidnight();
      base::Time day;
      views::View* group = nullptr;
      for (const Entry& entry : entries_) {
        const base::Time entry_day = entry.time.LocalMidnight();
        if (!group || entry_day != day) {
          day = entry_day;
          group = AddGroup(FormatDay(day, today));
        }
        AddRow(group, entry);
      }
    }
    InvalidateLayout();
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
    row->AddAction(library::CreateIconButton(
        base::BindRepeating(&KoanLibraryHistorySection::ForgetEntry,
                            base::Unretained(this), entry),
        kDeleteIcon, u"Remove from history", library::kRowButtonIconSize,
        library::kRowButtonPadding, library::kPillRadius));
    row->AddAction(library::CreateIconButton(
        base::BindRepeating(&KoanLibraryHistorySection::OpenEntry,
                            base::Unretained(this), entry),
        vector_icons::kUndoIcon, u"Reopen page", library::kRowButtonIconSize,
        library::kRowButtonPadding, library::kPillRadius));
    row->SetMenuCallback(
        base::BindRepeating(&KoanLibraryHistorySection::ShowRowMenu,
                            base::Unretained(this), entry));
    KoanLibraryRow* added = group->AddChildView(std::move(row));
    ++row_count_;
    if (favicon::FaviconService* favicons =
            FaviconServiceFactory::GetForProfile(
                profile_, ServiceAccessType::EXPLICIT_ACCESS)) {
      // Cancelled with `favicon_tracker_` before rows are rebuilt.
      favicons->GetFaviconImageForPageURL(
          entry.url,
          base::BindOnce(
              [](KoanLibraryRow* row,
                 const favicon_base::FaviconImageResult& result) {
                if (!result.image.IsEmpty()) {
                  row->SetIcon(ui::ImageModel::FromImage(result.image), 16);
                }
              },
              base::Unretained(added)),
          &favicon_tracker_);
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
        background ? WindowOpenDisposition::NEW_BACKGROUND_TAB
                   : WindowOpenDisposition::NEW_FOREGROUND_TAB;
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
    if (entry.closed_id) {
      if (tab_restore_) {
        tab_restore_->RemoveEntryById(*entry.closed_id);
      }
    } else if (history_) {
      history_->DeleteURLs({entry.url});
    }
    // Drop the row now, like Zen; the services' notifications follow. Not
    // from inside the row's own button, which the rebuild destroys.
    std::erase_if(entries_, [&](const Entry& other) {
      return other.closed_id ? other.closed_id == entry.closed_id
                             : !entry.closed_id && other.url == entry.url;
    });
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
        library()->browser_view(), u"Forget About This Site",
        base::StrCat({u"This will remove all data related to ",
                      base::UTF8ToUTF16(site),
                      u", including history, cookies, cache and site data. "
                      u"Bookmarks and passwords are kept."}),
        u"Forget",
        base::BindOnce(&KoanLibraryHistorySection::DoForgetSite,
                       weak_factory_.GetWeakPtr(), site));
  }

  void DoForgetSite(const std::string& site) {
    if (history_) {
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
    menu->AddItem(u"Forget This Page",
                  base::BindRepeating(&KoanLibraryHistorySection::ForgetEntry,
                                      weak_factory_.GetWeakPtr(), entry));
    if (!entry.closed_id) {
      menu->AddItem(u"Forget About This Site",
                    base::BindRepeating(&KoanLibraryHistorySection::ForgetSite,
                                        weak_factory_.GetWeakPtr(), entry),
                    entry.url.has_host());
    }
    row->SetMenuOpen(true);
    RunMenu(std::move(menu), row, gfx::Rect(point, gfx::Size()),
            views::MenuAnchorPosition::kTopLeft, source_type,
            base::BindRepeating(
                [](base::WeakPtr<KoanLibraryHistorySection> section,
                   KoanLibraryRow* row) {
                  // The row may have been rebuilt by now.
                  if (section && section->list()->Contains(row)) {
                    row->SetMenuOpen(false);
                  }
                },
                weak_factory_.GetWeakPtr(), base::Unretained(row)));
  }

  void ConfirmClearHistory(int hours) {
    ShowLibraryConfirmation(
        library()->browser_view(), u"Clear history?",
        u"The pages you visited in this time frame will be forgotten. This "
        u"cannot be undone.",
        u"Clear history",
        base::BindOnce(&KoanLibraryHistorySection::ClearHistory,
                       weak_factory_.GetWeakPtr(), hours));
  }

  void ClearHistory(int hours) {
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
  std::vector<Entry> entries_;
  int limit_ = kPageSize;
  int row_count_ = 0;
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
