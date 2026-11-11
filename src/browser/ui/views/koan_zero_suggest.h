// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_ZERO_SUGGEST_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_ZERO_SUGGEST_H_

#include <optional>
#include <string_view>

#include "base/memory/scoped_refptr.h"

class GURL;

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

// The empty URL bar's "visit copied link" row (Zen turns Firefox's
// browser.urlbar.clipboard.featureGate on; UrlbarProviderClipboard): the
// clipboard's text when it is an http(s) URL of at most 2048 characters with
// no whitespace in it, else nothing.
std::optional<GURL> ClipboardUrlForSuggestion(std::u16string_view text);

// Whether the row for `url` may show once more, counting this time: twice per
// copied link (Firefox's CLIPBOARD_IMPRESSION_LIMIT), and never again once
// picked or removed (ForgetClipboardSuggestion()). For the whole browser, like
// Firefox's provider.
bool TakeClipboardImpression(const GURL& url);
void ForgetClipboardSuggestion(const GURL& url);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_ZERO_SUGGEST_H_
