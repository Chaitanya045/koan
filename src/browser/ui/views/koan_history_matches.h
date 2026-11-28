// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_HISTORY_MATCHES_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_HISTORY_MATCHES_H_

class AutocompleteInput;
class GURL;
class TemplateURLService;
struct AutocompleteMatch;

// Which URL bar suggestions come from the user's history, for history per
// Space profile (#474, .agent/notes/upstream/space-history.md). Omnibox types
// only, so koan_omnibox_unittests can test it without the UI.
namespace koan {

// Whether `match` shows a page because it is in the user's history: the
// history providers' matches (history, shortcuts, the empty box's past
// searches and most visited sites), the search provider's past searches, and
// the empty URL bar's top sites (koan_zero_suggest.h). Not the address being
// typed (kUrlWhatYouTyped), bookmarks, open tabs (the tab matcher scopes
// those), search suggestions or Koan's actions.
bool IsSpaceHistoryMatch(const AutocompleteMatch& match);

// The key a page is looked up by: its URL as the omnibox dedupes matches
// (AutocompleteMatch::GURLToStrippedGURL(): no "www.", http for https, a
// search page down to its search terms), so a past search and the visit to
// its results page are one page.
GURL HistoryPageKey(const GURL& url, const TemplateURLService* service);

// Whether `match` goes to the address in the box (HistoryURLProvider makes
// the typed address a history match once it is in history): kept, as the
// user typed it.
bool IsTypedAddress(const AutocompleteMatch& match,
                    const AutocompleteInput& input,
                    const TemplateURLService* service);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_HISTORY_MATCHES_H_
