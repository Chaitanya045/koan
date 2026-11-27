// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_history_matches.h"

#include <string>

#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_provider.h"
#include "koan/browser/omnibox/koan_omnibox_hooks.h"
#include "third_party/metrics_proto/omnibox_input_type.pb.h"
#include "url/gurl.h"

namespace koan {

bool IsSpaceHistoryMatch(const AutocompleteMatch& match) {
  if (!match.provider) {
    return false;
  }
  switch (match.provider->type()) {
    case AutocompleteProvider::Type::kHistoryQuick:
    case AutocompleteProvider::Type::kHistoryUrl:
    case AutocompleteProvider::Type::kHistoryFuzzy:
    case AutocompleteProvider::Type::kShortcuts:
    case AutocompleteProvider::Type::kZeroSuggestLocalHistory:
    case AutocompleteProvider::Type::kMostVisitedSites:
    case AutocompleteProvider::Type::kHistoryClusterProvider:
    case AutocompleteProvider::Type::kHistoryEmbeddings:
      return match.type != omnibox::AutocompleteMatchType::kUrlWhatYouTyped;
    default:
      break;
  }
  if (match.type == omnibox::AutocompleteMatchType::kSearchHistory) {
    return true;
  }
  // The empty URL bar's top sites. Its other rows are open tabs (scoped by
  // the tab matcher) and the copied link, the only deletable one.
  return IsLocalZeroSuggestMatch(match) &&
         match.type == omnibox::AutocompleteMatchType::kNavsuggest &&
         !match.deletable;
}

GURL HistoryPageKey(const GURL& url, const TemplateURLService* service) {
  return AutocompleteMatch::GURLToStrippedGURL(
      url, AutocompleteInput(), service, /*keyword=*/std::u16string(),
      /*keep_search_intent_params=*/false);
}

bool IsTypedAddress(const AutocompleteMatch& match,
                    const AutocompleteInput& input,
                    const TemplateURLService* service) {
  return input.type() == metrics::OmniboxInputType::URL &&
         input.canonicalized_url().is_valid() &&
         HistoryPageKey(match.destination_url, service) ==
             HistoryPageKey(input.canonicalized_url(), service);
}

}  // namespace koan
