# Review: PR #55 "Command palette: Zen's actions in the URL bar (#40)" (Fixes #40)

Reviewed local `HEAD` of `~/.t3/worktrees/koan/feat-zen-command-palette` (bab1ccb, 4 commits on top of
merge-base fe13f21 with `origin/develop`; 32 files, +1536/-3). The local branch has diverged from
`origin/feat/zen-command-palette` (4 local commits vs 3 remote), and the author is rebasing, so I did not
evaluate conflicts with develop. I built and ran nothing.

References used:
- Chromium 158.0.8111.0 at `~/work/koan/chromium/src` (HEAD 67e29b8287, matches `CHROMIUM_VERSION`).
- Zen 1.23.3b. `zen-src` only has `zen-command-palette.ftl` and `ZenUrlbarController.mjs`. The provider logic
  (`ZenUBActionsProvider.sys.mjs`, `ZenUBGlobalActions.sys.mjs`, `ZenUBResultsLearner.sys.mjs`) is in
  `.cache/zen/zen/browser/omni.ja` under `modules/`. I read it with `unzip -p`, extracting nothing. Someone
  should add those three files to `zen-src`, so the next reviewer does not have to dig.

Verdict: one blocker (learned actions beat URL autocompletion for Enter) and six should-fixes. The
provider, hook, threading and fuzzy-matching code is otherwise careful.

## What I verified and found correct

- **Hook patches.** `patches/components/omnibox/browser/{autocomplete_controller.cc,BUILD.gn}.patch` apply
  with `git apply --check` to pristine `git show HEAD:<path>` copies in a scratch dir. They are minimal: one
  include, one call, one dep. The call sits after the upstream providers, so it uses the masked
  `config_.provider_types`. The dep lands in `static_library("browser")`.
- **Upstream APIs.** Every API the PR calls exists in 158 with the signature used:
  - `AutocompleteMatch(provider, relevance, deletable, type)`
  - `OmniboxAction` and `LabelStrings`, `SUPPORT_PEDALS_VECTOR_ICONS`
  - `OmniboxEventGlobalTracker::RegisterCallback`, `OmniboxLog::result` (`raw_ref`), `selection`, `text`
  - `AutocompleteController::autocomplete_provider_client()`
  - `GlobalBrowserCollection::GetLastActiveBrowser`, `BrowserView::GetBrowserViewForBrowser`,
    `BrowserView::GetAccelerator`
  - `TabInterface::IsSplit`/`IsPinned`
  - `ThemeService::{Get,Set}BrowserColorScheme`, `webui::GetBitmapDataUrl`, `kColorOmniboxResultsIcon`
  - All `IDC_*` ids and all vector icons used.
- **Threading and lifetime.**
  - `Start()` is fully synchronous on the UI thread. `done_` stays true, and the default `Stop()` is enough.
  - Every window and classifier controller builds a provider that subscribes to the global tracker. The
    `match.provider != this` filter in `OnURLOpened` makes only the owning provider learn, so there is no
    double counting.
  - The action holds a `WeakPtr<KoanBrowserViewController>`. The `run` callbacks take the controller as an
    argument, and the registry's `Unretained(controller)` is safe because the palette owns the registry.
  - `DestroyFor` calls `OnBrowserViewDestroying()` first, so the pre-target handler is removed while the
    omnibox is alive.
- **Execution path.** `OmniboxEditModel::OpenMatch` (and `searchbox_utils::OpenMatch`) runs a
  `takeover_action` for a kNormal selection and never navigates to `destination_url`. The
  `chrome://koan-command/<id>` URL is only a dedupe key. `ShouldRunProvider` already keeps the provider out
  of Lens, composebox and starter-pack keyword scopes.
- **Zen fidelity.**
  - `FuzzyScore`, thresholds 92/30, max 5, min query length 3 and the protocol regex match Zen's JS.
  - Learner bounds ±5 and the "seen" exclusion for `>`/`>x` match Zen's.
  - Typed-mode sort order (trim to 5 by score, then sort by usage) matches.
  - Every label matches `zen-command-palette.ftl`.
  - The omitted actions are documented.
- **Rows and colours.**
  - Row layout: the WebUI popup renders `secondaryText` (the description) in front of `primaryText` (the
    contents) in the top-chrome popup, so "label - shortcut" works as claimed.
  - Colours and radii: the only colour is `kColorOmniboxResultsIcon`, and there are no radii. The palette
    does not depend on #36's look.
- **macOS.** There are no Linux-only calls. `GetShortcutText()` prints ⌘ glyphs on Mac. Chrome commands use
  `GetDefaultMacAcceleratorForCommandId`. `SUPPORT_PEDALS_VECTOR_ICONS` is guarded. The Mac gap (Koan's own
  keys are still Ctrl-based until #51) is documented in `koan_shortcut_table.h` and ZEN_PARITY.
- **Build.** The build log in `.cache/agent-build-feat-zen-command-palette.log` ends in success. The deps
  in `src/browser/ui/views/BUILD.gn` cover the new includes.

---

## Blockers

### B1. A learned action becomes the default match at relevance 1600, so Enter on a typed site prefix runs the action

- Where: `src/browser/ui/views/koan_command_palette.cc:79-83` (constants), `:267-273`
  (`is_default`, `allowed_to_be_default_match`, relevance). `scripts/e2e/palette.mjs:204-213` enshrines it.
- What is wrong:
  - After the user has run an action once (`usage > 0`), the first sorted candidate gets
    `allowed_to_be_default_match = true` at relevance 1600.
  - `AutocompleteResult::FindTopMatch` picks the highest-relevance default-able match, with no type
    demotion.
  - The competing defaults sit well below 1600. `HistoryURLProvider` inline is 1413 and unvisited
    intranet 1403. History-quick is up to about 1400. Search- and URL-what-you-typed are about 1300 and
    1203.
  - So the action beats URL inline autocompletion and the verbatim navigation.
  - Any query scoring over 92 qualifies, including every 3+ character prefix of a label, because prefix
    scores are 100000+n.
- Failure scenarios:
  1. The user once ran "Print" from the palette. Later they type `pri` to reach primevideo.com, which would
     inline-complete. Enter opens the print dialog instead.
  2. The user often runs "Close Tab". Typing `clo` for a site (cloudflare.com) and pressing Enter closes the
     current tab, with possible loss of unsaved page state.
  3. After one use, `settings` (an intranet host, or a search) no longer navigates or searches. It runs the
     Settings action every time. Likewise `print`, `tab`, `pin`, `dark`.
  - `github.com` and other dotted text are safe: they are not a subsequence of any label.
  - Before the first use nothing is hijacked, because the rows are only boosted, not default.
  - The ordering part is fine: boosted rows sit right after the default match (sorting order 2, above search
    suggestions).
- Zen does something like this, but the lead's requirement is that scoring must not hijack URL navigation.
  There is also no escape. The only way back is to demote the action by ignoring it, which takes several
  uses.
- Fix, either of:
  - Keep learned usage as ordering only. Make an action default only in `>` mode.
  - If Zen's "Enter runs it" behaviour is wanted, allow it only when the typed text equals the full label
    (`score == kExactMatchScore`) and the input is not URL-like. Also lower the relevance to just above
    search-what-you-typed (for example 1301), so that a history/inline URL candidate (1400+) still wins.
  - Adjust the e2e test accordingly: type the full label, and add a negative check (see S6).

---

## Should fix

### S1. `>` with no query (and `>x`) runs the first, most-used action on Enter

- Where: `koan_command_palette.cc:267` (`prefixed ||`). Documented as intentional in the PR body only. It is
  missing from ZEN_PARITY's Differences, and USAGE says "Enter runs the first match".
- Zen (`ZenUBActionsProvider.startQuery`) only makes the first result the heuristic when
  `isPrefixed && ... i === 0 && query.length > 1`. In Zen's mode with nothing typed, Enter does nothing.
- Failure: Tab in an empty URL bar and then Enter (or typing `>` and Enter) runs the first action. The list
  is sorted by usage, so this is the user's most-used action, for example "Close Tab".
- Any text starting with `>` that has a candidate scoring over 30 is also unsearchable. A search like
  `>t...` runs the first action instead.
- Fix: follow Zen. Require `query.size() >= 2` (after the prefix) for default, and add a row to
  ZEN_PARITY if the Koan behaviour differs. Combine with B1's fix.

### S2. Accessible name of "Focus on <Space>" rows omits the Space (and the shortcut)

- Where: `koan_command_palette.cc:139-142` (`LabelStrings(label, label, u"", label)`);
  `koan_commands.cc:247-253` (`target` kept separate from `label`).
- Upstream: for `kPedal`, `GetAccessibilityBaseLabel` returns only
  `takeover_action->GetLabelStrings().accessibility_hint`
  (`components/omnibox/browser/autocomplete_match_type.cc:182-185`). It ignores contents and description.
  Both the WebUI popup (`searchbox_utils.cc`) and the Views path use it.
- Failure: with three Spaces, a screen reader announces "Focus on, 2 of 8" and "Focus on, 3 of 8", and the
  user cannot tell which Space. No row announces its shortcut.
- Fix: build the hint from label, target and shortcut, for example `label + u" " + target`, plus
  `u", " + shortcut` if present. `LabelStrings::accessibility_hint` is the announced field.

### S3. Passed-over actions never move below other results, though ZEN_PARITY says "Same"

- Where: `koan_command_palette.cc:258-259` (usage sort inside the action set only), `:271` (relevance
  depends only on `i`). `docs/ZEN_PARITY.md:90`: "passed-over ones move down (Zen's results learner)".
- Zen: non-prioritised results get `suggestedIndex = getDeprioritizeIndex(id)`, which is `|usage|` for a
  negative usage. A passed-over action sinks up to index 5 in the whole list.
- Here, an action ignored five times (usage -5) still takes a slot right under the default, above all
  search and history rows. It only sorts after other actions.
- Fix: derive the relevance from usage so negative-usage actions drop below search suggestions. For example,
  subtract `100 * -usage` or place them under search-what-you-typed. Alternatively change the doc to
  "Different". The e2e test should cover it (see S6).

### S4. Up to five actions at relevance 1500 crowd out real suggestions

- This is a design judgement, not a crash. I did not measure the UX.
- Where: `koan_command_palette.cc:83` (`kRelevance = 1500`), `:255-257`, `:276` (`shortcut_boosted`).
- Mechanics, all verified in upstream:
  - The result is sorted by relevance and trimmed to `GetMaxMatches()` = 8 on desktop. 1500 beats every
    non-default history, bookmark and search suggestion.
  - Actions are non-search matches, so `CalculateNumMatchesPerUrlCount` (kDynamicMaxAutocomplete, on by
    default) counts them as URL matches. With `url_cutoff` 0, the limit falls from the dynamic maximum to 8.
  - Boosted shortcuts (the user's frequently used history, order 2) sort after our boosted actions, so up to
    five rows can push them down.
- Failure: typing `tab`, `new` or `close` fills the popup with default + five actions + about two other
  rows. The user's actual history or bookmark suggestion may not appear.
- Fix: cap typed (non-`>`) suggestions at 2-3. Or give them a relevance below the top history/bookmark
  matches. Or leave `shortcut_boosted` off for the lower rows. Keep five (or all) for `>` mode.

### S5. Actions are not suppressed in keyword mode or for forced queries (uncertain)

- Where: gating at `koan_command_palette.cc:204-209` checks only zero-suggest, omit_async and `NTP_REALBOX`.
- Upstream `ShouldRunProvider` drops kBuiltin for starter-pack scopes (`@tabs`, `@history`...) but "treats
  all other providers as usual" in custom keyword mode (`autocomplete_controller.cc:1226-1300`).
- Failure: in a custom search-engine keyword scope, typing `print` or `settings` shows action rows, and
  with a learned default (B1) Enter runs the action instead of searching that engine. Typing `?print`
  (forced query) can also show actions.
- I did not verify whether `input.text()` includes the keyword in that mode. If it does, the fuzzy match
  rarely fires; if not, this reproduces.
- Fix: return early when `input.in_keyword_mode()` or
  `input.type() == metrics::OmniboxInputType::FORCED_QUERY`, unless the text is in `>` mode. Add an e2e
  check.

### S6. The e2e test exercises the feature, but guards none of the "does not hijack" or opt-out behaviour

- Where: `scripts/e2e/palette.mjs`. Credit first: it uses the real omnibox and reads the real WebUI popup,
  and it checks position, shortcut, icon, Enter-runs, learner, `>` mode, availability and Focus-on.
- Gaps:
  - No negative navigation test: typing a plain URL or a host (`example.com`, `github.com`) must still
    offer the normal default. Typing an action word before and after using that action must show what Enter
    does (B1).
  - No test for `>` + Enter with an empty query (S1), for the demotion path (S3), for
    `koan.command_palette.suggestions=false` or the `koan-command-palette` flag, or for incognito.
  - The "Two characters" and "Protocol-like" checks assert only that one specific label is absent, so a
    different action row would pass unnoticed. Assert that no row comes from the provider (for example by
    icon prefix or row count).
  - It checks no accessible label for "Focus on" (S2). `a11y_label` is in `m.match` on the popup row.
  - Line 190 hard-codes `Ctrl+S`, which fails once #51 gives Mac bindings or a Linux `accel` change. Read
    the expected text from the settings default instead.
  - Fixed sleeps (700/1000/1200 ms, `press()`/`pageKey()`) are the likely flake source. Poll `popupRows()`
    for the expected condition.
- Fix: add the negative and opt-out checks above, and replace the sleeps with waits on the expected state.

---

## Nits

- **N1. REBASE.md rule and link-time layering (uncertain).**
  - `docs/REBASE.md:8-11` and `:61` say a component can't depend on `//koan`, yet this patch (and the older
    `os_crypt` BUILD.gn patch) makes `components/omnibox/browser` depend on `//koan/browser/omnibox:hooks`.
    The row at `:46` omits the `BUILD.gn` patch, unlike other rows.
  - `koan::AddAutocompleteProviders` is defined in `//koan/browser/ui/views:impl`, so a binary that links
    `components/omnibox/browser` without `//chrome/browser` (for example `components_unittests` or
    omnibox fuzzers) would have an undefined symbol. The Koan build is `chrome` only, so nothing shows it.
  - Fix: update the intro sentence, mention the BUILD.gn patch in the row, and note the link constraint.
- **N2. Labels are string literals, not the shortcut table's (#46).** Keys come from
  `KoanShortcutController::GetAccelerator` (good), but labels are literals in `koan_commands.cc:120-250`.
  Zen's palette labels differ from the table's (for example "Copy Current URL" vs "Copy current URL",
  "Settings" vs "Open Preferences"), so you cannot read them directly. Better is an optional palette label
  on `shortcuts::Info`, or a comment saying why not. The strings are not localisable.
- **N3. Space actions are last.** `koan_command_palette.cc:364-365` adds the workspace source after all
  global ones, but the comment at `koan_commands.cc:120` says "In Zen's order". In Zen, New Space is sixth
  and Next/Previous Space follow Boost. In `>` mode with about eight rows, New Space and Next/Previous Space
  are never visible without typing.
- **N4. Tab in an empty URL bar (uncertain on Mac).**
  - `koan_command_palette.cc:389-403` consumes Tab when the box is empty. A keyboard-only user who Ctrl+Ls
    onto the NTP can no longer Tab onward in one press.
  - Nothing announces the new "Actions" mode to assistive tech (the text just becomes `>`).
  - Zen's `enableCommandsMode` toggles the mode off on a second Tab, and only runs when the floating bar is
    extended. Here a second Tab goes to the default popup behaviour.
  - The e2e test ran only on Linux. I could not confirm how Tab reaches the pre-target handler through
    Cocoa's `keyDown`/`insertTab:` path.
- **N5. Two commands are offered unconditionally.** "Open New Window" and "Settings" use `add_if(true, ...)`
  (`koan_commands.cc:148,154`), while "Private Window" checks `IsEnabled`. `IDC_NEW_WINDOW` is disabled when
  incognito is forced by policy (`browser_command_controller.cc:2117`), so the row is shown and Enter does
  nothing (`Command()` returns false silently). Use `IsEnabled(IDC_NEW_WINDOW)`.
- **N6. The usage pref grows without bound.** Ids like `focus-space:<token>` (`koan_commands.cc:247`) get a
  -1 entry whenever they are shown and passed over, and entries are removed only when they reach 0.
  Deleted or renamed Spaces leave dead keys forever. Prune ids that are not currently available, or limit
  the dict size.
- **N7. Provider type `kBuiltin`.** The provider shares `kBuiltin` with Chrome's built-in provider, so
  metrics (`Omnibox.ProviderTime2.Builtin`, logged provider type) and `GetPreferenceForType` (dedupe,
  `autocomplete_match.cc:134`) lump the two together. There is no dedicated type to use, so this is
  acceptable. Add a comment saying so.
- **N8. Window choice uses "last active browser".** `LastActiveController()` (`:105-111`, `:226-230`) rather
  than the window that owns the omnibox. This is the right window in normal use, but a stolen activation
  between typing and Enter, or an unfocused-window controller, could run an action in the wrong window.
  Iterating controllers and picking the one whose omnibox has focus is sturdier. The profile equality check
  at `:227-230` already stops cross-profile use.
- **N9. Icon colour and provenance.**
  - `koan_command_palette.cc:296-299` bakes `kColorOmniboxResultsIcon` into a PNG at suggest time. A
    selected row's icon is not recoloured with `kColorOmniboxResultsIconSelected`, and a colour/theme change
    shows stale icons until the next keystroke (the cache is keyed by colour, so correct once it re-runs).
    Uncertain on contrast; I could not see the selected state in the screenshots.
  - `light_mode.icon` and `automatic_appearance.icon` are copies of Chromium's ash icons (the former's
    path data matches `ash/.../brightness_up_refresh.icon`) and keep their "The Chromium Authors" headers.
    That is fine for BSD, but `vector_icons.{h,cc}.template` also carry Chromium headers. Note the origin.
- **N10. Actions mode differs visibly from Zen.** The PR's own screenshot shows a trailing `>  -  Google Search`
  row after the actions, and shortcut text such as `Ctrl+Comma` (from `Accelerator::GetShortcutText`; Zen
  shows glyphs like `ALT+⇧+L`). Zen's mode lists actions only, without a row limit
  (`UNLIMITED_MAX_RESULTS`). Consider a custom shortcut formatter (`,`) and dropping search rows for `>`
  input.
- **N11. "Focus on" matching includes the emoji icon.** `command.target` is the whole group title
  (`koan_commands.cc:249`), which starts with the Space's emoji. `FuzzyScore` then misses its prefix and
  exact tiers (a name that equals the query scores as a subsequence). The switcher splits the icon off with
  `SplitIcon`. Match on the name and show the icon separately.
- **N12. "Pin Tab" and "Add to Essentials" are the same action** (both `kTogglePinTab`, same shortcut), and
  so are Unpin / Remove from Essentials. The doc records this. It does mean two rows per state, each with
  its own learner entry. Offering only one per state would reduce noise.
- **N13. Style.**
  - `AppendGlobalCommands(KoanBrowserViewController* controller_ptr, ...)` immediately dereferences the
    pointer; take a reference or bind the pointer deliberately.
  - `AppendGlobalCommands` rebuilds about 30 `KoanCommand`s (callbacks, accelerator lookups) on every
    keystroke of 3+ characters. This is fine today, but skip the work when no label can match.
  - Commands are built before trimming, even when the query can match nothing.
