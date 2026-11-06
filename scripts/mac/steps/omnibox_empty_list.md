# Mac steps: the empty URL bar's list (#158)

DCHECK build. The provider is `koan_zero_suggest.cc`; the controller asks again on `OnOmniboxChanged()`.

| # | Do | Expect |
|---|---|---|
| 1 | Page with 3 other tabs open: ⌘T | The other tabs (Switch to Tab), then top sites. |
| 2 | Type a character, Backspace (and: select all, Delete) | The list is back (one request, not a loop). |
| 3 | **Private window with one tab**: ⌘T, wait 5 s, watch the log (`-v=1` not needed) and the CPU | The bar opens empty, no rows. No stream of zero-suggest requests in the log; CPU idle. Esc. |
| 4 | Fresh profile (no top sites), one tab: ⌘T, wait 5 s | Same as 3. |
| 5 | Right after launch, before top sites have loaded: ⌘T | The list appears when they have (the retry), or nothing; no loop. |
| 6 | A `data:` tab with a megabyte of text in the URL, another tab active: ⌘T | A row for it showing `data:text/html;...` (cut), Enter switches to it. The row and the box never hold the whole URL. |
| 7 | `>` (command palette) typed, then deleted to empty | The list is back; with the palette's `>` mode on (keyword selected) no list request is made while it is. |

## No AI Mode (#178, O5)

| # | Do | Expect |
|---|---|---|
| A1 | ⌘T, look at the overlay (empty, then typing `a`) | No AI Mode chip, button, "⌘ + return for AI Mode" hint, tooltip or row anywhere in the card or the page-action area. |
| A2 | In ⌘T and ⌘L type `@` | Only @history, @bookmarks, @tabs (no @aimode, no @gemini). |
| A3 | ⇧⌘A (Search Tabs) | The bubble lists tabs only: no AI Mode / Ask AI / Gemini entry, button or footer. (If anything shows, send a screenshot: it may be a surface the static audit didn't find.) |
| A4 | Settings > Search engine / the address bar section | No AI Mode toggle or @aimode shortcut offered. |
