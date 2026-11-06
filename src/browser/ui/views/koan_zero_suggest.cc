// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_zero_suggest.h"

#include <algorithm>
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
#include "components/omnibox/browser/page_classification_functions.h"
#include "components/omnibox/browser/tab_matcher.h"
#include "components/url_formatter/url_formatter.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/url_constants.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/omnibox/koan_omnibox_hooks.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
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

// How many of each, and in all (Chromium's own limit for zero-prefix
// suggestions is 8).
constexpr size_t kMaxOpenTabs = 5;
constexpr size_t kMaxSuggestions = 8;

// Relevance of the two groups; tabs come first, each group in order.
constexpr int kOpenTabRelevance = 1500;
constexpr int kTopSiteRelevance = 1000;

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
           host != "koan-newtab";  // Koan's own new tab page (#96).
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
  }
  KoanZeroSuggestProvider(const KoanZeroSuggestProvider&) = delete;
  KoanZeroSuggestProvider& operator=(const KoanZeroSuggestProvider&) = delete;

  // AutocompleteProvider:
  void Start(const AutocompleteInput& input, bool minimal_changes) override;
  void Stop(AutocompleteStopReason stop_reason) override {
    AutocompleteProvider::Stop(stop_reason);
    weak_factory_.InvalidateWeakPtrs();
  }

 private:
  ~KoanZeroSuggestProvider() override { LiveProviders().erase(this); }

  AutocompleteMatch MakeMatch(const GURL& url,
                              const std::u16string& title,
                              int relevance,
                              omnibox::AutocompleteMatchType type);
  void OnTopSites(std::set<GURL> open_urls,
                  const history::MostVisitedURLList& sites);

  const raw_ptr<AutocompleteProviderClient> client_;
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
  match.contents = AutocompleteMatch::SanitizeString(
      url.SchemeIs(url::kDataScheme)
          ? base::UTF8ToUTF16(DisplaySpec(url))
          : url_formatter::FormatUrl(
                url,
                AutocompleteMatch::GetFormatTypes(/*preserve_scheme=*/false,
                                                  /*preserve_subdomain=*/false),
                base::UnescapeRule::SPACES, nullptr, nullptr, nullptr));
  match.contents_class = {ACMatchClassification(0, ACMatchClassification::URL)};
  // Sanitized like the contents (a title can end in a non-ASCII space), and
  // classified only when there is one: an empty text takes no classification.
  match.description = AutocompleteMatch::SanitizeString(title);
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
  if (!top_sites || matches_.size() >= kMaxSuggestions) {
    return;
  }
  done_ = false;
  top_sites->GetMostVisitedURLs(
      base::BindOnce(&KoanZeroSuggestProvider::OnTopSites,
                     weak_factory_.GetWeakPtr(), std::move(open_urls)));
}

void KoanZeroSuggestProvider::OnTopSites(
    std::set<GURL> open_urls,
    const history::MostVisitedURLList& sites) {
  done_ = true;
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
  NotifyListeners(added > 0);
}

}  // namespace

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

scoped_refptr<AutocompleteProvider> CreateZeroSuggestProvider(
    AutocompleteProviderClient* client) {
  return base::MakeRefCounted<KoanZeroSuggestProvider>(client);
}

}  // namespace koan
