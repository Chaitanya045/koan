// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_OMNIBOX_KOAN_OMNIBOX_HOOKS_H_
#define KOAN_BROWSER_OMNIBOX_KOAN_OMNIBOX_HOOKS_H_

#include <vector>

#include "base/memory/scoped_refptr.h"

class AutocompleteController;
class AutocompleteProvider;

namespace koan {

// Called at the end of the AutocompleteController constructor, after the
// upstream providers are created. Appends Koan's providers (the command
// palette: Zen's actions in the URL bar) to `providers`. `provider_types` is
// the controller's AutocompleteProvider::Type bitmask.
void AddAutocompleteProviders(
    AutocompleteController* controller,
    int provider_types,
    std::vector<scoped_refptr<AutocompleteProvider>>& providers);

// Whether the zero-prefix suggestions get a section for Koan's own matches
// (open tabs and top sites, koan_zero_suggest.h): desktop Chromium's sections
// for an empty box take Google's kinds of suggestions only, so those matches
// would be dropped. Called from AutocompleteResult::SortAndCull().
bool UsesLocalZeroSuggestSection();

}  // namespace koan

#endif  // KOAN_BROWSER_OMNIBOX_KOAN_OMNIBOX_HOOKS_H_
