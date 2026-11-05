# Mac steps: a URL ending in %20 in the omnibox (#166, #167)

For the Mac loop and the hands-on tester. DCHECK build, **fresh profile** first (no history), then the same window for
the later rows. Type **one key at a time** and **stop at the end of the text** (the DCHECK fires on the intermediate
result, while the URL ends in `%20`). ⌘T opens the floating bar; ⌘L (on a page) opens it with the page's address.
After **every row**: `grep -E ':FATAL:|Check failed' <koan log>` is empty and Koan is still running (no new `.ips`).
Before the fix every row marked (D) died with `autocomplete_result.cc:335] DCHECK failed:
SanitizeString(match.contents) == match.contents`.

| # | Do | Expect |
|---|---|---|
| 1 | ⌘T, type `example.com/a%20` (D) | The bar stays open, rows listed (the first is `example.com/a`), no crash. Esc. |
| 2 | ⌘T, type `data:text/html,a%20` (D) | Same. Esc. |
| 3 | ⌘T, type `example.com/?q=a%20` (query) (D) | Same. Esc. |
| 4 | ⌘T, type `example.com/a#b%20` (ref) (D) | Same. Esc. |
| 5 | ⌘T, type `view-source:example.com/a%20` (D) | Same. Esc. |
| 6 | ⌘T, type `example.com/a%20b%20`, then Backspace three times | Same at every key. Esc. |
| 7 | ⌘T, type `data:text/html,a b` and `hello world` | Rows listed, no crash (a space inside the text never failed; this is the control). Esc. |
| 8 | Visit it: ⌘T, type `https://example.com/a%20`, Enter (the server's 404 page is fine; it commits, so it goes to history) | The page loads; the address keeps its `%20`. No crash. |
| 9 | On that page press ⌘L (the on-focus verbatim match, from `ZeroSuggestVerbatimMatchProvider`) (D) | The bar opens with the address selected, no crash. Esc. |
| 10 | ⌘T, type `example.com/a` and stop (the page is in history now: `HistoryQuickProvider`'s match) (D) | A row for the visited page, shown without the trailing space; no crash. Esc. |
| 11 | With the page from row 8 open in another tab, ⌘T, type `example.com/a` (the open tab's match, `OpenTabProvider`) (D) | The open tab's row ("Switch to Tab"), no crash. Esc. |
| 12 | Enter on row 1's input, `example.com/a%20` | Opens `example.com/a%20` (the destination keeps `%20`; only the displayed text lost the space). |
