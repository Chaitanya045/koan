// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenLibraryHistorySection.mjs (URL and site formatting)
//   ZenLibrarySpacesSection.mjs (the card drag)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#ifndef KOAN_BROWSER_KOAN_LIBRARY_LOGIC_H_
#define KOAN_BROWSER_KOAN_LIBRARY_LOGIC_H_

#include <string>

#include "base/containers/span.h"

class GURL;

// The Library's pure logic, apart from its views.
namespace koan::library {

// "https://www.example.com/a/" -> "example.com/a" (Zen's #formatUrl): no
// http(s) scheme, no "www." and no trailing slash.
std::u16string FormatUrl(const GURL& url);

// The site a page belongs to, as Zen groups History "By Site": its host
// without "www.", or nothing for pages that aren't on the web.
std::u16string SiteOf(const GURL& url);

// Dragging a Space card: the slot (0-based position among the cards) the
// dragged card lands in when its centre is at `center`. `centers` are the
// cards' centres where they sit, including the dragged one's (at
// `drag_index`), which doesn't count. The slot is the number of other cards
// whose centre `center` is past.
int DropSlot(base::span<const int> centers, int drag_index, int center);

// How far card `index` steps aside while the card at `drag_index` is over
// slot `drop_index`: `step` (a card and its gap) towards the vacated place,
// or 0. Cards between the two slots shift; the dragged card doesn't.
int CardShift(int index, int drag_index, int drop_index, int step);

}  // namespace koan::library

#endif  // KOAN_BROWSER_KOAN_LIBRARY_LOGIC_H_
