# Typed URL ending in %20 kills DCHECK builds (issue #166)

**Not Koan's code: an upstream Chromium 158 bug.** Several omnibox providers set
`match.contents = url_formatter::FormatUrl(url, ..., UnescapeRule::SPACES, ...)`
without sanitizing it (they do sanitize `match.description`). `%20` unescapes to a
space, so a URL ending in `%20` (`example.com/a%20`, `data:text/html,a%20`, a query
`?q=a%20`, a ref `#b%20`, `view-source:` forms) gives contents with a trailing space.
`AutocompleteResult::AppendMatches` DCHECKs `SanitizeString(contents) == contents`
(`autocomplete_result.cc:335`), and `SanitizeString` trims whitespace. Release builds
have no DCHECK, so stock Chrome users don't see it; Koan's tested builds are DCHECK
builds. A space inside the text survives the trim (`data:text/html,a b` never failed).
Koan patches none of these providers or `AppendMatches` outside this fix.

**Crash seen (#166):** the url-what-you-typed match from `VerbatimMatchForInput`
(`verbatim_match.cc`, called by `HistoryURLProvider::Start`; `Provider: HistoryURL`).

**Patched (one `SanitizeString(...)` each, listed in `docs/REBASE.md`, drop each when
upstream sanitizes there; check on every roll):**

| File | Match | Reached by |
|---|---|---|
| `verbatim_match.cc` `VerbatimMatchForInput` | url-what-you-typed | typing the URL (crashed) |
| `history_url_provider.cc` `HistoryMatchToACMatch` | a history entry | typing a prefix of a visited URL |
| `history_quick_provider.cc:295` | a history entry (quick) | the same, after one visit |
| `zero_suggest_verbatim_match_provider.cc:169, :219` | the page's own address | Cmd+L on `http(s)://.../a%20` |
| `open_tab_provider.cc:213` | an open tab | typing a prefix, a tab open on the URL (`@tabs`) |
| `recently_closed_tabs_provider.cc:152, :300` | a closed tab | zero-prefix / `@tabs` |

**Same shape, not patched** (not reachable in Koan today): `cross_device_tab_provider.cc:89`
(needs sync, off), `zero_suggest_provider.cc:457` (Google's remote zero-prefix, off:
`search.suggest_enabled` false), `most_visited_sites_provider.cc:113` (the NTP's tiles; the
desktop omnibox doesn't show them). Revisit if any of those turns on.

**Upstream:** worth a crbug report: sanitize `contents` where it is built, as
`keyword_provider.cc`, `clipboard_provider.cc` and `enterprise_search_aggregator_provider.cc`
already do. The fix is the same one line per site. Koan's own `koan_zero_suggest.cc`
(#158) sanitizes its contents too.

**Test:** `scripts/mac/steps/omnibox_trailing_space.md` (the Mac rows: typed, query,
ref, `view-source:`, visit then Cmd+L, retype after a visit, open tab) and
`scripts/e2e/omnibox_trailing_space.mjs` (typing, from `run.sh`, which exports `KOAN_LOG`).
