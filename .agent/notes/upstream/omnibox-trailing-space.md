# Typed URL ending in %20 kills DCHECK builds (issue #166)

**Not Koan's code. Upstream Chromium 158.** The crashing match is the
`url-what-you-typed` one: `VerbatimMatchForInput` (`verbatim_match.cc`, called by
`HistoryURLProvider::Start` for the verbatim input; Provider: HistoryURL) sets
`match.contents = display_string`, a `FormatUrl(..., UnescapeRule::SPACES)` result.
(The first fix, #167's c1a5d6c9, sanitized only `HistoryURLProvider::HistoryMatchToACMatch`,
the history-entry match, which also formats this way, and did not fix it: the Mac
reproduced the DCHECK on that head with `data:text/html,a%20`.) `HistoryMatchToACMatch` sets

    match.contents = url_formatter::FormatUrl(url, format_types, UnescapeRule::SPACES, ...)

and, unlike `match.description` (which goes through `AutocompleteMatch::SanitizeString`),
does not sanitize it. For a URL that ends in `%20` (`example.com/a%20`,
`data:text/html,a%20`) the formatted contents end in a decoded space.
`AutocompleteResult::AppendMatches` has `DCHECK_EQ(SanitizeString(match.contents),
match.contents)` (autocomplete_result.cc:335), which fails. Release builds have
no DCHECK, so stock Chrome users never see it; Koan's tested builds are DCHECK
builds. Koan patches none of HistoryURLProvider, `url_formatter` or `AppendMatches`
on develop; the Koan providers (command palette, #97 rows, #58) set no URL contents.

**Fix (this PR):** `patches/components/omnibox/browser/verbatim_match.cc.patch`
(the match that crashed) and `history_url_provider.cc.patch` (the history-entry match)
sanitize the contents, before the term matches are computed from them.

**Guard:** `scripts/e2e/omnibox_trailing_space.mjs` types the repro one key at a time
and greps the log for `:FATAL:` / `Check failed`. The same two greps are the Mac
harness step.

**Other providers with the same shape** (FormatUrl with `UnescapeRule::SPACES` into
`contents`): `history_quick_provider.cc:295`, `open_tab_provider.cc:213`,
`recently_closed_tabs_provider.cc`, `cross_device_tab_provider.cc`,
`zero_suggest_provider.cc`, `most_visited_sites_provider.cc` — each needs that URL
in history / an open tab to trigger. Koan's own `koan_zero_suggest.cc` (#158) takes
open tabs, so it gets the same sanitization there.
