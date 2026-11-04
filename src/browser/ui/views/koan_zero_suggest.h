// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_ZERO_SUGGEST_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_ZERO_SUGGEST_H_

#include "base/memory/scoped_refptr.h"

class AutocompleteProvider;
class AutocompleteProviderClient;

namespace koan {

// Zen's suggestions for an empty URL bar (its Cmd+T panel): the open tabs,
// most recently used first, each switching to its tab, then the most visited
// sites. All local: no request to a search engine. Chromium's desktop
// omnibox has nothing for an empty box on a web page (its open-tab provider
// skips empty input, and the most-visited provider is only for new tab
// pages).
scoped_refptr<AutocompleteProvider> CreateZeroSuggestProvider(
    AutocompleteProviderClient* client);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_ZERO_SUGGEST_H_
