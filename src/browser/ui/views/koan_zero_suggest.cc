// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_zero_suggest.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/escape.h"
#include "base/strings/utf_string_conversions.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/top_sites.h"
#include "components/omnibox/browser/actions/tab_switch_action.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_match_classification.h"
#include "components/omnibox/browser/autocomplete_provider.h"
#include "components/omnibox/browser/autocomplete_provider_client.h"
#include "components/omnibox/browser/tab_matcher.h"
#include "components/url_formatter/url_formatter.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/omnibox/koan_omnibox_hooks.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "url/gurl.h"

namespace koan {

namespace {

// How many of each, and in all (Chromium's own limit for zero-prefix
// suggestions is 8).
constexpr size_t kMaxOpenTabs = 5;
constexpr size_t kMaxSuggestions = 8;

// Relevance of the two groups; tabs come first, each group in order.
constexpr int kOpenTabRelevance = 1500;
constexpr int kTopSiteRelevance = 1000;

// The additional-info key that marks a match as this provider's.
constexpr char kMatchMark[] = "koan zero suggest";

class KoanZeroSuggestProvider : public AutocompleteProvider {
 public:
  // There is no provider type for Koan: kBuiltin, like Chrome's chrome://
  // page suggestions, runs where the omnibox does (koan_command_palette.cc).
  explicit KoanZeroSuggestProvider(AutocompleteProviderClient* client)
      : AutocompleteProvider(AutocompleteProvider::Type::kBuiltin),
        client_(client) {}
  KoanZeroSuggestProvider(const KoanZeroSuggestProvider&) = delete;
  KoanZeroSuggestProvider& operator=(const KoanZeroSuggestProvider&) = delete;

  // AutocompleteProvider:
  void Start(const AutocompleteInput& input, bool minimal_changes) override;
  void Stop(AutocompleteStopReason stop_reason) override {
    AutocompleteProvider::Stop(stop_reason);
    weak_factory_.InvalidateWeakPtrs();
  }

 private:
  ~KoanZeroSuggestProvider() override = default;

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
  match.fill_into_edit = base::UTF8ToUTF16(url.spec());
  // Zen shows the title, then the site: what the user knows, without the
  // scheme or "www".
  // Sanitized, as Chromium's providers do: a URL ending in "%20" formats to
  // contents with a trailing space, which AppendMatches() DCHECKs (#166).
  match.contents = AutocompleteMatch::SanitizeString(url_formatter::FormatUrl(
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
  // SortAndCull()'s Koan section takes only these (IsLocalZeroSuggestMatch()).
  match.RecordAdditionalInfo(kMatchMark, "1");
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
    if (!tab.url.is_valid() || !tab.url.SchemeIsHTTPOrHTTPS() ||
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

bool IsLocalZeroSuggestMatch(const AutocompleteMatch& match) {
  return match.GetAdditionalInfoForDebugging(kMatchMark) == "1";
}

scoped_refptr<AutocompleteProvider> CreateZeroSuggestProvider(
    AutocompleteProviderClient* client) {
  return base::MakeRefCounted<KoanZeroSuggestProvider>(client);
}

}  // namespace koan
