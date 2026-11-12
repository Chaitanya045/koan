# Mac steps: end-user polish (#349)

A brand-new profile (new user data directory, the welcome shown), default look (shadcn dark), real pointer and keys. Suites: `newtab`, `compact_single`, `urlbar`, `settings`, `welcome`.

| # | Do | Expect |
|---|---|---|
| EMPTY-1 | Finish the welcome (Start browsing) | The page area is a card lighter than the sidebar and the gutter around it (#18181b on #09090b), not one black area. The URL bar does not open by itself (Zen's doesn't after its welcome). |
| EMPTY-2 | Create a Space (the Space bar's +), select it | The same card on the window. Sample a pixel of the card and one of the sidebar (Digital Color Meter): they differ. Light mode: a white card on the #fafafa sidebar. |
| URL-349 | Click the pill, type `wikipedia.org` (a fresh profile, and again after visiting it) | The field shows Zen's magnifier, not a page icon, and **one** `wikipedia.org` row under it, then "wikipedia.org – Brave Search". To be sure it was the field that read as a second row: with `internal_only_uis_enabled` in Local State, chrome://omnibox, "Connect window omnibox", type it again: the result table has one URL row (HistoryURL, url-what-you-typed). If it lists two, note both rows' Provider and Stripped URL columns on #349. |
| TOP-1 | ⌘T on the fresh profile (no page visited yet) | No "Web Store" row (Chromium's prepopulated top site) and no example.com (that came from the e2e harness's startup pages, not Koan). |
| TOP-2 | Visit two sites, ⌘T | Those sites are listed. |
| PILL-1 | On github.com: ⌘S twice (compact mode on, off) | The pill shows GitHub's icon, not the magnifier; hovering and pressing the icon keeps the favicon. Also after a look change (Settings > Look and Feel) and a Space colour change. |
| POPUP-1 | Click the pill, type `koan` | The suggestions start inside the window: no part of the popup or its shadow hangs past the window's left edge (14 pt before). The field sits inside the pill. With Tabs on the right, the same at the right edge. |
| SET-1 | ⌘, in dark mode | The header shows Koan's logo, not Chromium's (also History, Downloads, Extensions). |
| SET-2 | The left menu | "Profile" with the person icon, not "You and Google". Its rows stay (Sync and Google services has the switch that turns sign-in back on, #300 decides the rest). |
| SET-3 | Select menu items, hover others | The selected item is the shadcn sidebar item (`--sidebar-accent`, a grey), not Chromium's light blue; hover the same grey. Zen look: Chromium's colours as before. |
| WEL-1 | `--force-first-run`, new profile: Next to the search step | Brave, Google, DuckDuckGo, Ecosia show their logos in the text colour; Bing and Yahoo their initial. `--search-engine-choice-country=FR`: Qwant and others too. |
| WEL-2 | Next | "You're all set" / "Enjoy browsing." |
