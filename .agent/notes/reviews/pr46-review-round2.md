# Review round 2: PR #46, chrome://koan-settings (issue #39)

Branch `feat/zen-settings` at `3d79a35`, after `git fetch`; the local branch matches `origin/feat/zen-settings`. Diffed against `origin/develop` (15 commits, 48 files).
Upstream is Chromium 158 HEAD at `/home/chaitanya/work/koan/chromium/src`, read with `git show HEAD:`. Nothing in the worktree or the shared tree was modified, built or run.

## Checked and OK

- **Patches apply.** The 6 changed or new patches were checked with `git apply --check` against pristine `HEAD:` copies in a scratch directory (since deleted). All 6 apply:
  - `tabs/BUILD.gn`
  - `tabs/back_to_opener/BUILD.gn`
  - `back_to_opener_controller.cc`
  - `tab_strip_model.cc`
  - `webui/BUILD.gn`
  - `chrome_web_ui_configs.cc`

  The `tabs/BUILD.gn` dep goes into `tab_strip_impl`, the target that compiles `tab_strip_model.cc`. `//koan/browser:tab_hooks` depends only on `//base`, prefs, user_prefs and content, so it creates no cycle. All the new hooks are listed in `docs/REBASE.md`.
- **S1 (second settings tab): fixed.** `ShowSingletonTabOverwritingNTP(profile->GetOriginalProfile(), url, IGNORE_AND_NAVIGATE)` exists with that signature (singleton_tabs.h:28). It reuses the tab whatever its path. The e2e test (`settings.mjs:225-232`) now asserts one settings tab, and that it is in front.
- **S2 (incognito): fixed.** Every Koan reader, writer and observer uses `GetOriginalProfile()->GetPrefs()`:
  - compact mode / layout
  - shortcuts
  - workspace tint (whose `UpdateTint` still returns early in OTR)
  - the settings handler

  The only read through an OTR `PrefService` is `koan_tab_hooks.cc`. That is safe: the overlay has no Koan values, so reads fall through to the persistent store. Ctrl+, from a private window opens the page in a regular window. The e2e test covers both directions.
- **S3 (default/custom collision): fixed** for the case reported. `Bindings()` skips a default whose key is already bound, and skips malformed entries. `scripts/e2e/prefs/settings.json` seeds Ctrl+Alt+R and the test checks the result. A new path to the same DCHECK is described in SF1.
- **S4: fixed.** The three unowned behaviours are implemented, and ZEN_PARITY now says Partial. One parity row is wrong (SF3).
- **S5: fixed.** The test now covers:
  - the live tint, checked through `browser.theme.user_color2` and `tint_applied`;
  - Backspace and Reset, checked by what the keys then do;
  - `setPref` refusals;
  - the browser-side conflict refusal, by calling `setShortcut` directly;
  - Tab, and the "Unsaved shortcut!" note.
- **`DetermineNewSelectedIndex` hook.**
  - **Pref off:** the function returns `nullopt` before looking at any tab, so upstream runs exactly as before.
  - **Pref on:**
    - The candidates exclude the block and anything that fails `is_eligible`, which covers collapsed tabs and the focus-mode restriction. `GetTabIndexAfterClosing`'s CHECK therefore holds.
    - The hook never turns a found candidate into `nullopt`, so `CHECK(next_selected_index.has_value())` in `RemoveTabFromIndexImpl` is safe.
  - **Split tabs:** `RemoveTabFromIndexImpl` overwrites the result for split tabs (tab_strip_model.cc:5019-5024), so splits are unaffected.
  - **Focus-mode workspaces and Essentials:** the focused pass picks among the workspace's tabs and the Essentials. When the last tab of the focused group closes and there are no Essentials, the second pass picks the most recently used tab of any group. Upstream would pick the neighbour; both then leave focus mode the same way.
  - **Closing several tabs at once:** `CloseTabs` updates the selection model per removal and only notifies at the end. A chain of most-recently-used picks always ends on the most recently used surviving tab, with no intermediate tab shown.
- **Back-to-opener hook.**
  - It sits after the `GetContents()` null check, so it can't dereference null.
  - Turning the setting off makes `CanGoBack`, `ShouldEnableBackButton` and `GoBack` (browser_commands.cc:1031-1051, 1127) all ignore the opener.
  - The `kCloseOnBackWithNoHistory` observer calls `BrowserCommandController::TabStateChanged()`. The controller is created unconditionally in `BrowserWindowFeatures` and outlives the BrowserView, and the registrar is cleared in `OnBrowserViewDestroying`.
- **Ctrl+Tab takeover, reserved keys.**
  - All four keys map to reserved commands: `IDC_CYCLE_TO_NEXT_TAB` / `IDC_CYCLE_TO_PREV_TAB` for Ctrl+Tab and Ctrl+Shift+Tab, `IDC_SELECT_NEXT_TAB` / `IDC_SELECT_PREVIOUS_TAB` for Ctrl+PgDn/PgUp (accelerator_table.cc:80-84, 347-359).
  - `PreHandleKeyboardEvent` sends reserved keys to `FocusManager::ProcessAccelerator`. There Koan's normal-priority target is tried before BrowserView's, and a `false` return falls through to BrowserView (`TryProcess`, accelerator_manager.cc:170-181).
  - With both settings off, a normal window behaves exactly as before. With one on, web pages in normal windows lose nothing they didn't already lose, since upstream reserves these keys too. Fullscreen and keyboard lock are unchanged, because the keys aren't reserved there and the page sees them first, as upstream.
- **e2e seeding in run.sh.**
  - `KOAN_ROOT` is the worktree.
  - The file is copied into a fresh `mktemp` profile before launch, and only when `prefs/<test>.json` exists.
  - `koan.shortcuts` is not a tracked or protected pref, so Chrome keeps the seeded value.
  - No other test is affected.

---

## Blockers

None.

---

## Should fix

### SF1. Binding Ctrl+Tab, Ctrl+Shift+Tab or Ctrl+PgDn/PgUp to a Koan action registers the same target twice (DCHECK crash in dev/e2e builds)
- **Where:**
  - `src/browser/ui/views/koan_shortcuts.cc:107-118` (`RegisterAccelerators`) and `:141-148`
  - `src/browser/koan_shortcut_table.cc:187-207` (`IsValidCustomAccelerator`)
  - `src/browser/ui/webui/resources/koan_settings.js:204-208` (`isValidKey`)
- **What's wrong:**
  - `RegisterAccelerators()` registers every entry of `bindings_` and then every `kCycleKeys` entry, all with target `this`.
  - Nothing stops a custom binding from using one of the cycle keys: VKEY_TAB(9) or VKEY_NEXT/PRIOR with Ctrl passes both the JS and the C++ validity checks.
  - The browser-side conflict check only compares against `Bindings()`, which doesn't include the cycle keys.
  - Registering the same target twice for one accelerator hits `DCHECK(!Contains(target)) << "Registering the same target multiple times"` (ui/base/accelerators/accelerator_manager.cc:126). `out/Koan/args.gn` has `dcheck_always_on = true`.
- **Failure scenario:**
  1. Open chrome://koan-settings > Keyboard Shortcuts and focus "Toggle Mute". Shortcut handling is suspended, so Ctrl+Tab reaches the page.
  2. Press Ctrl+Tab. The editor shows `Ctrl+Tab` with "Replaces Chromium's shortcut", which is allowed.
  3. Press Esc. `setShortcut` succeeds and the `koan.shortcuts` observer runs `OnShortcutsChanged()` → `RegisterAccelerators()` in every window. The DCHECK fires and the browser crashes.
  4. On every later start, `OnAddedToWidget()` → `RegisterAccelerators()` crashes again until Preferences is hand-edited.

  In a build without DCHECKs, the custom action wins and the Ctrl+Tab settings silently stop working.
- **Fix:**
  - Either refuse the four cycle keys as custom keys, both in `IsValidCustomAccelerator` and in the page's `isValidKey`, with a note;
  - or skip registering (and handling) a cycle key that `bindings_` already contains.

  Add an e2e step that tries Ctrl+Tab in the editor.

### SF2. With "Close tab … on Back" off, the Back button's menu still offers "Close and go back to …", and it does nothing
- **Where:** `patches/chrome/browser/ui/tabs/back_to_opener/back_to_opener_controller.cc.patch` (the hook is only in `CanGoBackToOpener()`), together with `koan_feature_overrides.cc:17-20` (`tabs::kBackToOpener` enabled for everyone).
- **What's wrong:**
  - The feature creates a `BackToOpenerController` for every tab (tab_features.cc:1041).
  - `BackForwardMenuModel` decides whether to show the opener item with `BackToOpenerController::HasValidOpener()`, not `CanGoBackToOpener()`. See back_forward_menu_model.cc:348 (`GetSectionForIndex`), :427-429 (`GetSectionItemCount`) and :437-440 (the "Show Full History" condition).
  - The label comes from `GetFormattedOpenerTitle()`, which is also gated only on `HasValidOpener()`.
  - Activating the item calls `GoBackToOpener()`, which checks `CanGoBackToOpener()` and returns early when the setting is off.
- **Failure scenario:**
  1. In Tab Management, switch "Close tab and switch to its owner tab…" off.
  2. Click a `target=_blank` link; a child tab opens.
  3. Right-click (or long-press) Back in the child. The menu shows `Close and go back to "<opener title>"` and "Show Full History", even though the tab has no history.
  4. Clicking the item does nothing.

  Before this PR the feature was off, so the item never appeared.
- **Fix:** Gate the relationship rather than one consumer. For example, put the same `koan::ShouldCloseTabOnBack()` check in `BackToOpenerController::HasValidOpener() const`, and change `CanGoBackToOpener()` to call it instead of reading `has_valid_opener_` directly. That is still a one-line hook, and it covers the menu, the label and Back. When the pref changes, also call `NotifyUIStateChanged()` or rely on the existing `TabStateChanged()`.

### SF3. The Ctrl+Tab takeover overrides Chromium's own "Ctrl+Tab in recently used order" setting, and ZEN_PARITY says that setting doesn't exist
- **Where:**
  - `src/browser/ui/views/koan_shortcuts.cc:251-281` (`CycleTabs`)
  - `docs/ZEN_PARITY.md:84-85`
  - `docs/USAGE.md:124`
- **What's wrong:**
  - Chromium 158 has `features::kCtrlTabMru`, which is **enabled by default** (ui_features.cc:34), and the pref `browser.ctrl_tab_mru` (default false). The toggle is shown in chrome://settings > Appearance (`appearance_page.html.ts:228-230`, `showCtrlTabMru`).
  - With that toggle on, `IDC_CYCLE_TO_NEXT_TAB` / `IDC_CYCLE_TO_PREV_TAB` run `CycleToMruTab()` (browser_command_controller.cc:787-815).
  - Koan's `CycleTabs()` runs first whenever either Koan Ctrl+Tab setting is on. It never checks `chrome::IsCtrlTabMruEnabled()`, so it silently replaces MRU order with tab order.
  - Zen warns about exactly this combination (`zenTabsCycleByAttributeWarning` for `ignore-essential-tabs` plus `browser.ctrlTab.sortByRecentlyUsed`, zen-settings.js:734-741).
  - The parity rows are wrong in two places:
    - "Chromium cycles in tab order" is false.
    - "Ctrl+Tab in Koan always stays in the active workspace" is also false with MRU on, because `GetGlobalMruTab()` (browser_commands.cc:548-582) ignores group focus and spans all windows of the profile.
- **Failure scenario:**
  1. In chrome://settings > Appearance, turn on "Cycle through the most recently used tabs with Ctrl+Tab".
  2. In chrome://koan-settings, turn on "Ctrl+Tab cycles within Essential or Workspace tabs only".
  3. Ctrl+Tab now cycles in strip order, and the Chromium setting has no effect. Nothing on either page explains why.
- **Fix:** Pick one:
  - (a) For Ctrl+Tab and Ctrl+Shift+Tab, return `false` from `CycleTabs` when `chrome::IsCtrlTabMruEnabled(browser)` is true, so Chromium's MRU wins. This is Zen's precedence, since its warning only tells the user.
  - (b) Show a warning on the Koan row when the Chromium pref is on, as Zen does.

  Either way, correct the two ZEN_PARITY rows: MRU cycling exists upstream, and with it on, Ctrl+Tab can leave the workspace.

### SF4. With a Koan Ctrl+Tab setting on, popup windows swallow Ctrl+Tab and Ctrl+PgDn/PgUp instead of passing them to the page
- **Where:** `src/browser/ui/views/koan_shortcuts.cc:251-281` (`CycleTabs` doesn't check whether the command is enabled). The controller is created for every `BrowserView` (`koan_browser_view_hooks.cc:11-13`), including popups.
- **What's wrong:**
  - For `TYPE_POPUP`, `IsReservedCommandOrKey` still reserves the tab-cycling commands, since only APP/APP_POPUP are exempt. So `PreHandleKeyboardEvent` calls `ProcessAccelerator` before the page sees the key.
  - Upstream, `BrowserView::AcceleratorPressed` → `ExecuteCommandWithDispositionAndContext` returns `false` because tab commands are disabled without a tab strip (`supports_tabs`, browser_command_controller.cc:2015-2022, :649). The key is then reported NOT_HANDLED and reaches the page.
  - Koan's `CycleTabs` instead loops over a one-tab strip, finds nothing, and returns `true`, so the key is consumed.
  - Koan's other actions go through `Command()`, which does check `chrome::IsCommandEnabled`. `CycleTabs` doesn't.
- **Failure scenario:**
  1. Turn on "Ignore Pending tabs when cycling with Ctrl+Tab".
  2. Open a site's popup window (`window.open(url, '', 'popup')`), for example a web spreadsheet or editor that uses Ctrl+PgDn to change sheets or Ctrl+Tab to switch documents.
  3. The keys never reach the page, while stock Chromium delivers them.
- **Fix:** At the top of `CycleTabs`, return `false` unless `chrome::IsCommandEnabled(browser, forward ? IDC_SELECT_NEXT_TAB : IDC_SELECT_PREVIOUS_TAB)` is true, or unless the browser is `TYPE_NORMAL`.

---

## Nits

### N1. Most recently used on close can pick a tab the user never looked at
- **Where:** `src/browser/koan_tab_hooks.cc:26-43`
- **What's wrong:** It ranks by `WebContents::GetLastActiveTimeTicks()`. That value is set to "now" when the WebContents is **created** (web_contents_impl.cc:1411) and is otherwise updated only when the tab becomes visible (:5376-5378). A background tab opened later than the user's last real tab switch therefore outranks the tab the user was just reading.
- **Scenario:**
  1. Read tab A. Ctrl+click three links, opening B1-B3 in the background.
  2. Click an unrelated tab Z and close it.
  3. If the opener preferences don't decide, Koan selects B3, which was never viewed, instead of A.

  Upstream's own Ctrl+Tab MRU seeds never-focused tabs the same way (tab_lifecycle_unit.cc:108-117), so this is a parity and UX question, not a crash. The e2e test only uses Ctrl+T tabs, which are shown when they open, so it doesn't catch it.
- **Possible fix:** Prefer tabs that have been active before, for example by tracking activation in a small `TabStripModelObserver`. At least note the limitation.

### N2. Formatting
- **Where:** `src/browser/koan_shortcut_table.cc:11-12`
- **What's wrong:** Chromium's clang-format (`buildtools/linux64-format/clang-format`) wants the blank line between `koan_features.h` and the `ui/events` includes removed. All other changed `.cc`/`.h` files are clean.

### N3. The e2e test doesn't cover the new hook's group cases
- **Where:** `scripts/e2e/tab_management.mjs`
- **What's missing:** Most recently used on close is only tested with ungrouped tabs. These cases are not exercised:
  - inside a focused workspace (Essentials eligible, other workspaces not);
  - closing the last tab of a workspace;
  - collapsed groups.

  "Ignore pending tabs" is untested; this is acknowledged in the PR.
- **Fix:** Add a workspace case using the helpers `workspaces.mjs` already has.
