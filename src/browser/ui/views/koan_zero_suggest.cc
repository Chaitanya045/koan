// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_zero_suggest.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/escape.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/webui_url_constants.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/top_sites.h"
#include "components/omnibox/browser/actions/tab_switch_action.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_match_classification.h"
#include "components/omnibox/browser/autocomplete_provider.h"
#include "components/omnibox/browser/autocomplete_provider_client.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "components/omnibox/browser/omnibox_event_global_tracker.h"
#include "components/omnibox/browser/omnibox_log.h"
#include "components/omnibox/browser/page_classification_functions.h"
#include "components/omnibox/browser/tab_matcher.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_starter_pack_data.h"
#include "components/url_formatter/url_formatter.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/url_constants.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/omnibox/koan_omnibox_hooks.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace koan {

namespace {

// The live Koan zero-suggest providers (one per window's controller): what
// IsLocalZeroSuggestMatch() recognizes a match by. Pointers only compared,
// never used. (Not AutocompleteMatch::additional_info, which Chromium keeps
// for chrome://omnibox-internals.)
std::set<const AutocompleteProvider*>& LiveProviders() {
  static base::NoDestructor<std::set<const AutocompleteProvider*>> providers;
  return *providers;
}

// Tabs search's list before anything is typed: tabs shown in the last 50
// minutes in order (by tens of seconds), older ones after them in tab order.
// Below what OpenTabProvider gives a typed match (up to 10000), never 0.
constexpr int kEmptyTabSearchRelevance = 1300;
constexpr int kEmptyTabSearchSteps = 299;

// How many of each, and in all (Chromium's own limit for zero-prefix
// suggestions is 8).
constexpr size_t kMaxOpenTabs = 5;
constexpr size_t kMaxSuggestions = 8;

// Relevance of the groups: the copied link first (Firefox gives its provider
// priority), then tabs, each group in order.
constexpr int kClipboardRelevance = 1600;
constexpr int kOpenTabRelevance = 1500;
constexpr int kTopSiteRelevance = 1000;

// Firefox's UrlbarProviderClipboard limits.
constexpr size_t kMaxClipboardChars = 2048;
constexpr int kClipboardImpressions = 2;

// The link last offered from the clipboard, and how many more times it may be.
struct ClipboardSuggestion {
  GURL url;
  int impressions_left = 0;
};
ClipboardSuggestion& LastClipboardSuggestion() {
  static base::NoDestructor<ClipboardSuggestion> suggestion;
  return *suggestion;
}

// The host of Koan's own new tab page (koan/common/koan_url_constants.h,
// which comes with that page).
constexpr char kKoanNewTabHost[] = "koan-newtab";

// The most of a page's title a row holds (the row elides it to its width
// anyway; a page can set a title of any length).
constexpr size_t kMaxTitleChars = 1024;

// `text` cut to `max` UTF-16 units, not between the halves of a surrogate pair.
std::u16string CapText(std::u16string text, size_t max) {
  if (text.size() <= max) {
    return text;
  }
  text.resize(max);
  // (A high surrogate: 0xD800-0xDBFF.)
  if ((text.back() & 0xfc00) == 0xd800) {
    text.pop_back();
  }
  return text;
}

// The URL as shown in a row and put in the box: a data: URL (a page can be
// megabytes) as "data:<type>,..." and any other past the display cap cut
// there, as the address bar does. Never used to navigate.
std::string DisplaySpec(const GURL& url) {
  if (url.SchemeIs(url::kDataScheme)) {
    const std::string& spec = url.spec();
    const size_t comma = spec.find(',');
    if (comma == std::string::npos || comma > 256) {
      return "data:...";
    }
    return spec.substr(0, comma + 1) + "...";
  }
  const std::string& spec = url.spec();
  return spec.size() > content::kMaxURLDisplayChars
             ? spec.substr(0, content::kMaxURLDisplayChars)
             : spec;
}

// Whether an open tab's page is worth a row: any page (a web page, a local
// file, a data: page, chrome://settings ...), not a blank one or a new tab
// page (Firefox lists neither as a tab to switch to).
bool IsListableTab(const GURL& url) {
  if (!url.is_valid() || url.IsAboutBlank() ||
      url.SchemeIs(content::kChromeDevToolsScheme)) {
    return false;
  }
  if (url.SchemeIs(content::kChromeUIScheme)) {
    const std::string_view host = url.host();
    return host != chrome::kChromeUINewTabHost &&
           host != chrome::kChromeUINewTabPageHost &&
           host != chrome::kChromeUINewTabPageThirdPartyHost &&
           host != kKoanNewTabHost;
  }
  return true;
}

class KoanZeroSuggestProvider : public AutocompleteProvider {
 public:
  // There is no provider type for Koan: kBuiltin, like Chrome's chrome://
  // page suggestions, runs where the omnibox does (koan_command_palette.cc).
  explicit KoanZeroSuggestProvider(AutocompleteProviderClient* client)
      : AutocompleteProvider(AutocompleteProvider::Type::kBuiltin),
        client_(client) {
    LiveProviders().insert(this);
    // A picked copied link isn't offered again (Firefox's onEngagement).
    url_opened_subscription_ =
        OmniboxEventGlobalTracker::GetInstance()->RegisterCallback(
            base::BindRepeating(&KoanZeroSuggestProvider::OnURLOpened,
                                base::Unretained(this)));
  }
  KoanZeroSuggestProvider(const KoanZeroSuggestProvider&) = delete;
  KoanZeroSuggestProvider& operator=(const KoanZeroSuggestProvider&) = delete;

  // AutocompleteProvider:
  void Start(const AutocompleteInput& input, bool minimal_changes) override;
  void Stop(AutocompleteStopReason stop_reason) override {
    AutocompleteProvider::Stop(stop_reason);
    weak_factory_.InvalidateWeakPtrs();
    pending_ = 0;
  }
  void DeleteMatch(const AutocompleteMatch& match) override;

 private:
  ~KoanZeroSuggestProvider() override { LiveProviders().erase(this); }

  AutocompleteMatch MakeMatch(const GURL& url,
                              const std::u16string& title,
                              int relevance,
                              omnibox::AutocompleteMatchType type);
  void OnTopSites(std::set<GURL> open_urls,
                  const history::MostVisitedURLList& sites);
  void OnClipboardText(std::u16string text);
  void OnURLOpened(OmniboxLog* log);
  // One asynchronous part (the clipboard, the top sites) has finished.
  void FinishPart(bool changed);

  const raw_ptr<AutocompleteProviderClient> client_;
  base::CallbackListSubscription url_opened_subscription_;
  // The asynchronous parts still running for this request.
  int pending_ = 0;
  base::WeakPtrFactory<KoanZeroSuggestProvider> weak_factory_{this};
};

AutocompleteMatch KoanZeroSuggestProvider::MakeMatch(
    const GURL& url,
    const std::u16string& title,
    int relevance,
    omnibox::AutocompleteMatchType type) {
  AutocompleteMatch match(this, relevance, /*deletable=*/false, type);
  match.destination_url = url;
  match.fill_into_edit = base::UTF8ToUTF16(DisplaySpec(url));
  // Zen shows the title, then the site: what the user knows, without the
  // scheme or "www".
  // Sanitized, as Chromium's providers do: a URL ending in "%20" formats to
  // contents with a trailing space, which AppendMatches() DCHECKs (#166).
  // Capped, then sanitized (the cut can end in a space).
  match.contents = AutocompleteMatch::SanitizeString(CapText(
      url.SchemeIs(url::kDataScheme)
          ? base::UTF8ToUTF16(DisplaySpec(url))
          : url_formatter::FormatUrl(
                url,
                AutocompleteMatch::GetFormatTypes(/*preserve_scheme=*/false,
                                                  /*preserve_subdomain=*/false),
                base::UnescapeRule::SPACES, nullptr, nullptr, nullptr),
      content::kMaxURLDisplayChars));
  match.contents_class = {ACMatchClassification(0, ACMatchClassification::URL)};
  // Sanitized like the contents (a title can end in a non-ASCII space), and
  // classified only when there is one: an empty text takes no classification.
  match.description =
      AutocompleteMatch::SanitizeString(CapText(title, kMaxTitleChars));
  if (!match.description.empty()) {
    match.description_class = {
        ACMatchClassification(0, ACMatchClassification::NONE)};
  }
  return match;
}

void KoanZeroSuggestProvider::Start(const AutocompleteInput& input,
                                    bool minimal_changes) {
  matches_.clear();
  done_ = true;
  weak_factory_.InvalidateWeakPtrs();
  pending_ = 0;
  // Only a box that was just opened or clicked (empty, or with the page's
  // address selected: Ctrl+L; Zen lists the same then), not one the user is
  // typing in, and only in the omnibox (not the NTP's realbox, a composebox
  // or Lens).
  if (!UsesLocalZeroSuggestSection(input.current_page_classification()) ||
      !input.IsZeroSuggest() || input.omit_asynchronous_matches()) {
    return;
  }

  // The open tabs, most recently used first. (The tab being left is not one
  // to switch to.)
  std::vector<TabMatcher::TabWrapper> tabs =
      client_->GetTabMatcher().GetOpenTabs(&input);
  std::stable_sort(tabs.begin(), tabs.end(), [](const auto& a, const auto& b) {
    return a.last_shown_time > b.last_shown_time;
  });
  std::set<GURL> open_urls;
  for (const TabMatcher::TabWrapper& tab : tabs) {
    if (matches_.size() >= kMaxOpenTabs) {
      break;
    }
    // (Not a second tab on the page being left either: Navigate()'s switch
    // would find the active tab first and do nothing.)
    if (!IsListableTab(tab.url) ||
        tab.url.EqualsIgnoringRef(input.current_url()) ||
        !open_urls.insert(tab.url).second) {
      continue;
    }
    AutocompleteMatch match =
        MakeMatch(tab.url, tab.title,
                  kOpenTabRelevance - static_cast<int>(matches_.size()),
                  omnibox::AutocompleteMatchType::kOpenTab);
    // The "Switch to Tab" button. (Chromium attaches it, with has_tab_match,
    // only to non-zero-prefix results.)
    match.has_tab_match = true;
    match.actions.push_back(base::MakeRefCounted<TabSwitchAction>(tab.url));
    matches_.push_back(std::move(match));
  }

  // Then the most visited sites, from the history's top sites.
  scoped_refptr<history::TopSites> top_sites = client_->GetTopSites();
  const bool wants_top_sites = top_sites && matches_.size() < kMaxSuggestions;
  // A link on the clipboard goes above them all. (Read on every opening, as
  // Firefox does; the answer may come at once.)
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  pending_ = (wants_top_sites ? 1 : 0) + (clipboard ? 1 : 0);
  done_ = pending_ == 0;
  base::WeakPtr<KoanZeroSuggestProvider> weak = weak_factory_.GetWeakPtr();
  if (wants_top_sites) {
    top_sites->GetMostVisitedURLs(base::BindOnce(
        &KoanZeroSuggestProvider::OnTopSites, weak, std::move(open_urls)));
  }
  if (clipboard && weak) {
    clipboard->ReadText(
        ui::ClipboardBuffer::kCopyPaste, /*data_dst=*/std::nullopt,
        base::BindOnce(&KoanZeroSuggestProvider::OnClipboardText, weak));
  }
}

void KoanZeroSuggestProvider::OnClipboardText(std::u16string text) {
  std::optional<GURL> url = ClipboardUrlForSuggestion(text);
  const bool shows = url && TakeClipboardImpression(*url);
  if (shows) {
    // Firefox's row: the whole link as its title.
    AutocompleteMatch match = MakeMatch(
        *url, base::UTF8ToUTF16(DisplaySpec(*url)), kClipboardRelevance,
        omnibox::AutocompleteMatchType::kNavsuggest);
    match.deletable = true;
    matches_.insert(matches_.begin(), std::move(match));
  }
  FinishPart(shows);
}

void KoanZeroSuggestProvider::FinishPart(bool changed) {
  if (pending_ > 0) {
    --pending_;
  }
  done_ = pending_ == 0;
  NotifyListeners(changed);
}

void KoanZeroSuggestProvider::DeleteMatch(const AutocompleteMatch& match) {
  // Only the copied link is deletable (Firefox's Dismiss).
  ForgetClipboardSuggestion(match.destination_url);
  std::erase_if(matches_, [&match](const AutocompleteMatch& m) {
    return m.deletable && m.destination_url == match.destination_url;
  });
}

void KoanZeroSuggestProvider::OnURLOpened(OmniboxLog* log) {
  const AutocompleteResult& result = *log->result;
  if (log->selection.line >= result.size()) {
    return;
  }
  const AutocompleteMatch& match = result.match_at(log->selection.line);
  if (match.provider == this && match.deletable) {
    ForgetClipboardSuggestion(match.destination_url);
  }
}

void KoanZeroSuggestProvider::OnTopSites(
    std::set<GURL> open_urls,
    const history::MostVisitedURLList& sites) {
  size_t added = 0;
  for (const history::MostVisitedURL& site : sites) {
    if (matches_.size() >= kMaxSuggestions) {
      break;
    }
    if (!site.url.is_valid() || !site.url.SchemeIsHTTPOrHTTPS() ||
        open_urls.contains(site.url)) {
      continue;
    }
    matches_.push_back(MakeMatch(site.url, site.title,
                                 kTopSiteRelevance - static_cast<int>(added++),
                                 omnibox::AutocompleteMatchType::kNavsuggest));
  }
  FinishPart(added > 0);
}

}  // namespace

std::optional<GURL> ClipboardUrlForSuggestion(std::u16string_view text) {
  if (text.empty() || text.size() > kMaxClipboardChars ||
      std::ranges::any_of(
          text, [](char16_t c) { return base::IsUnicodeWhitespace(c); })) {
    return std::nullopt;
  }
  GURL url(text);
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return std::nullopt;
  }
  return url;
}

bool TakeClipboardImpression(const GURL& url) {
  ClipboardSuggestion& last = LastClipboardSuggestion();
  if (last.url != url) {
    last = {url, kClipboardImpressions};
  }
  if (last.impressions_left <= 0) {
    return false;
  }
  --last.impressions_left;
  return true;
}

void ForgetClipboardSuggestion(const GURL& url) {
  ClipboardSuggestion& last = LastClipboardSuggestion();
  if (last.url == url) {
    last.impressions_left = 0;
  }
}

bool UsesLocalZeroSuggestSection(
    metrics::OmniboxEventProto::PageClassification classification) {
  // Every omnibox (also about:blank, the home page and an invalid address),
  // not the NTP's realbox, Omnibox Everywhere, Lens or a composebox.
  return features::IsUrlBarEnabled() &&
         !omnibox::IsWebUISearchbox(classification);
}

bool IsLocalZeroSuggestMatch(const AutocompleteMatch& match) {
  return match.provider && LiveProviders().contains(match.provider);
}

bool IsEmptyTabSearch(const AutocompleteInput& input,
                      const TemplateURLService* service) {
  if (!input.in_keyword_mode() || !service) {
    return false;
  }
  // (Strips the keyword from the copy.)
  AutocompleteInput query = input;
  const TemplateURL* keyword =
      AutocompleteInput::GetSubstitutingTemplateURLForInput(service, &query);
  return keyword &&
         keyword->starter_pack_id() ==
             template_url_starter_pack_data::StarterPackId::kTabs &&
         base::TrimWhitespace(query.text(), base::TRIM_ALL).empty();
}

int EmptyTabSearchRelevance(const AutocompleteInput& input,
                            const GURL& url,
                            base::Time last_shown_time) {
  if (!IsListableTab(url) || url.EqualsIgnoringRef(input.current_url())) {
    return 0;
  }
  const int64_t steps = std::clamp<int64_t>(
      (base::Time::Now() - last_shown_time).InSeconds() / 10, 0,
      kEmptyTabSearchSteps);
  return kEmptyTabSearchRelevance - static_cast<int>(steps);
}

scoped_refptr<AutocompleteProvider> CreateZeroSuggestProvider(
    AutocompleteProviderClient* client) {
  return base::MakeRefCounted<KoanZeroSuggestProvider>(client);
}

}  // namespace koan
