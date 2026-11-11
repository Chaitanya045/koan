// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_OMNIBOX_KOAN_OMNIBOX_HOOKS_H_
#define KOAN_BROWSER_OMNIBOX_KOAN_OMNIBOX_HOOKS_H_

#include <vector>

#include "base/memory/scoped_refptr.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"

class AutocompleteController;
class AutocompleteProvider;
struct AutocompleteMatch;

namespace koan {

// Called at the end of the AutocompleteController constructor, after the
// upstream providers are created. Appends Koan's providers (the command
// palette: Zen's actions in the URL bar) to `providers`. `provider_types` is
// the controller's AutocompleteProvider::Type bitmask.
void AddAutocompleteProviders(
    AutocompleteController* controller,
    int provider_types,
    std::vector<scoped_refptr<AutocompleteProvider>>& providers);

// Whether the zero-prefix suggestions of a box with this page classification
// get a section for Koan's own matches (open tabs and top sites,
// koan_zero_suggest.h): desktop Chromium's sections for an empty box take
// Google's kinds of suggestions only, so those matches would be dropped. Every
// omnibox (also about:blank, the home page and an invalid address), not the
// NTP's realbox, a composebox, Lens or Omnibox Everywhere. Called from
// AutocompleteResult::SortAndCull(), and by the provider itself.
bool UsesLocalZeroSuggestSection(
    metrics::OmniboxEventProto::PageClassification classification);

// Whether `match` is one of that provider's. The section takes only these:
// other zero-prefix navigations (a search engine's, once suggestions are
// turned on) stay dropped, as they were.
bool IsLocalZeroSuggestMatch(const AutocompleteMatch& match);

}  // namespace koan

#endif  // KOAN_BROWSER_OMNIBOX_KOAN_OMNIBOX_HOOKS_H_
