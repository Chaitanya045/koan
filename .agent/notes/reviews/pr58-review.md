# PR #58 review: URL bar like Zen's (#36, #24)

Reviewed commit: `e37e644` (PR head, `refs/pull/58/head`), based on develop `fe13f21` (#54).
The worktree `~/.t3/worktrees/koan/feat-zen-urlbar` is in the middle of an interactive rebase onto `43de34b` (#55 merged), so I reviewed the PR head by SHA and did not touch the worktree. Nothing was built or run. Everything below comes from reading the diff, Chromium 158 (`~/work/koan/chromium/src`) and Zen's source (`~/work/koan/.cache/zen-src`). "Uncertain" marks items I could not confirm by reading alone.

Hook patches: all four (`omnibox/BUILD.gn`, `chrome_omnibox_client.cc`, `location_bar/BUILD.gn`, `location_bar_view.cc`) pass `git apply --check` against pristine Chromium HEAD (`67e29b8`). They are minimal: one include and one call each, plus a BUILD.gn dep.

Merge state against develop (#55 merged): textual conflicts only in `docs/REBASE.md`, `docs/USAGE.md`, `koan_features.{h,cc}`, `koan_flag_entries.h` and `src/browser/ui/views/BUILD.gn` (all additive). Nothing semantic with #55 that I found: the palette's `RunCommand` calls `contents->Focus()`, which closes the bar through `OnFocusChanged`, and no code outside the compact controller used `is_url_bar_floating()`. Against #56 (`feat/zen-compact-layouts`): conflicts in `koan_compact_mode_controller.{h,cc}` and `koan_sidebar_nav_view.cc`, besides docs/features. #56 still has `SetUrlBarFloating`/`LayoutFloatingUrlBar` and `is_url_bar_floating()`, and moves `hides_toolbar()` to hide_toolbar_/hide_sidebar_ flags. Whoever lands second must port the URL bar controller's use of `hides_toolbar()`, `sidebar_nav()` (now public here, tracked by `ViewTracker` there) and `OnUrlBarOpenChanged()`/`IsUrlBarOpen()`.

---

## Blockers

### B1. On macOS, ⌘T never reaches Koan's accelerator, so Ctrl+T / ⌘T still opens the new-tab page
- **Where:** `src/browser/ui/views/koan_shortcuts.cc:98-104` (registers every binding at `kNormalPriority`), `src/browser/koan_shortcut_table.cc:133` (`bind(VKEY_T, kAccel, kNewTab)`), `docs/REBASE.md:48` ("`IDC_NEW_TAB` is reserved, so it reaches the FocusManager first").
- **What is wrong:** that reasoning holds for the web-content/renderer path on Linux, not for macOS. In Chromium on macOS, a key equivalent goes through `-[CommandDispatcher performKeyEquivalent:]` (`ui/base/cocoa/command_dispatcher.mm:122`) and `prePerformKeyEquivalent:` (`chrome/browser/ui/cocoa/chrome_command_dispatcher_delegate.mm:66-141`):
  1. The views FocusManager is asked first, but with `require_priority_handler = true` (`ui/views/cocoa/native_widget_mac_ns_window_host.mm:1862-1875`: returns without processing unless `HasPriorityHandler(accelerator)`). Koan registers `kNormalPriority`, so this step skips it.
  2. Then `CommandForKeyEvent` finds `IDC_NEW_TAB` for ⌘T, and `BrowserNativeWidgetMac::ExecuteCommand(..., is_before_first_responder=true)` (`browser_native_widget_mac.mm:619-654`) executes it directly because `IsReservedCommand(IDC_NEW_TAB)` is true. That is `chrome::NewTab`, which opens the NTP.
  3. Koan's normal-priority accelerator would only run in `postPerformKeyEquivalent`, which is never reached for a reserved command.
  The same applies to File > New Tab in the menu bar (`IDC_NEW_TAB` -> `NewTab`), which Koan does not intercept anywhere.
- **Failure scenario:** on a MacBook (the product), ⌘T opens a regular new-tab page and ignores `koan.urlbar.replace_newtab`. The headline feature of #24/#36 only works through Koan's own "+ New Tab" row. The Linux e2e cannot see this: it sends Ctrl+T through the page's CDP key path.
- **Fix:** on macOS register the `kNewTab` accelerator (and a user rebinding of it) with `ui::AcceleratorManager::kHighPriority`. Extension commands also use high priority and the manager DCHECKs a single high-priority handler per accelerator, so check `HasPriorityHandler` first and fall back. For the menu item and other `IDC_NEW_TAB` entry points, either add a tiny hook in `BrowserCommandController::ExecuteCommandWithDisposition` (`case IDC_NEW_TAB`, `browser_command_controller.cc:761`) or document that the menu stays unhooked. The command-level hook is safe where a `chrome::NewTab()` hook is not, because it only fires for user commands (the PR rejected the latter for `CreateNewTabGroup`). Correct the REBASE.md claim.
- Confidence: high from source reading; not run on a Mac.

---

## Should fix

### S1. Switching away from the window (Alt-Tab / ⌘-Tab) closes the bar and discards what was typed; coming back reopens a plain edit of the current URL
- **Where:** `koan_url_bar_controller.cc:267-285` (`OnFocusChanged`, `bar->Revert()` at 283), called from `koan_compact_mode_controller.cc:259`.
- **What is wrong:** on window deactivation views clears the focus (`native_widget_aura.cc:1414` `StoreFocusedView(true)`; `native_widget_mac_ns_window_host.mm:1374`), and `FocusManager` notifies listeners with `focused_now == nullptr` (`focus_manager.cc:427-447`). Koan treats that like "focus left the omnibox": it ends the new-tab session, ends editing and calls `Revert()`. Chrome's omnibox keeps the typed text across window deactivation. On reactivation `RestoreFocusedView()` focuses the omnibox again, and `OnFocusChanged(bar)` opens it as an ordinary edit (`new_tab_` is false).
- **Failure scenario:**
  - (a) Ctrl+T, switch to another app to copy a link, switch back, paste, Enter: the bar is now an in-pill edit of the current page (all selected), so Enter navigates the current tab instead of opening a new one. Ctrl+T's whole point is not to touch the current page.
  - (b) ⌘L, type half a query, ⌘-Tab to a chat, back: the query is gone.
- **Fix:** ignore `focused_now == nullptr` while `!browser_view_->GetWidget()->IsActive()` (keep placement, text and `new_tab_`; the later `RestoreFocusedView` is then a no-op), or only end the session when focus moves to a different view of the same widget. Add an e2e check that deactivating/activating the widget keeps the typed text.
- Confidence: high for the code path; not run.

### S2. Copy-URL shortcuts use Ctrl on macOS, not ⌘⇧C / ⌘⇧⌥C like Zen
- **Where:** `src/browser/koan_shortcut_table.cc:134-135` (`kCtrl | kShift`, `kCtrl | kShift | kAlt`); the PR body says "accel = ⌘" and `docs/USAGE.md` lists Ctrl+Shift+C without a ⌘ form.
- **What is wrong:** Zen's defaults (`ZenKeyboardShortcuts.mjs:1033-1090`) are `accel+shift+C` and `accel+shift+alt+C`, i.e. ⌘⇧C and ⌘⇧⌥C on Mac. `kCtrl` is the Control key on Mac. `kNewTab` and the split-view keys already use `kAccel`; the copy-URL entries were the ones this PR touched and could have fixed (it covers #36's "also for Ctrl+Shift+C").
- **Failure scenario:** a Zen user on a Mac presses ⌘⇧C and gets Chrome's DevTools element picker (`IDC_DEV_TOOLS_INSPECT`) instead of "Copied current URL!". ⌃⇧C copies the URL instead (not a Zen binding).
- **Fix:** bind both to `kAccel`. ⌘⇧C is not a reserved command, so Koan's normal-priority accelerator wins over the menu key equivalent (`postPerformKeyEquivalent` asks the FocusManager before the menu). Keep Ctrl on Linux (`kAccel` is Ctrl there). This is partly covered by the existing `TODO(#51)` in `koan_shortcut_table.h:89`, but the PR text claims otherwise; either fix it or say it is deferred in the PR and USAGE.
- Confidence: high.

### S3. In the multiple-toolbars layout, Esc does not end a new-tab session, and Enter then opens the current page in a new tab
- **Where:** `koan_url_bar_controller.cc:438-442` (the Esc pre-target handler is installed only when the omnibox is lifted, i.e. in `SetPlacement(opening)`); `:194-230` (`OpenForNewTab` sets `new_tab_` in every layout); `:293-303` (`OnOmniboxChanged` does not end the session when the text reverts).
- **What is wrong:** with the toolbar visible (`DesiredPlacement() == kNone`) there is no Esc handler, so Chrome's own multi-step Esc applies (the PR describes it as close suggestions, revert, blur). After the revert step the omnibox still has focus, shows the page URL, and `new_tab_` is still true.
- **Failure scenario:** multiple-toolbars layout. Ctrl+T, type "foo", Esc, Esc (the box now shows the page's URL), Enter. `TakeNewTabDisposition()` is true, so the current page's URL opens in a duplicate foreground tab. Zen closes the bar and creates nothing on Esc.
- **Fix:** also install the Esc handler (or an equivalent `Close()`) while `new_tab_` is set in the kNone layout, or end the session in `OnOmniboxChanged` when `user_input_in_progress` falls to false while `new_tab_` is set. Add the layout to the e2e (run `urlbar` once with `koan.single_toolbar: false`).
- Confidence: medium-high (exact Esc count from the PR text; the missing handler and the stale `new_tab_` are verified).

### S4. The URL pill lost keyboard focus and activation semantics it had as a `LabelButton`; it also activates on any mouse button
- **Where:** `koan_sidebar_nav_view.cc:177-200` (`KoanUrlPill : views::View`, `OnMousePressed`); previously `views::LabelButton`.
- **What is wrong:** a plain `View` with role `kButton` has no focus behaviour, no focus ring and no Enter/Space handling, so keyboard users can no longer Tab to the pill and press it (Ctrl+L still works). `OnMousePressed` runs `FocusOmnibox` for right and middle clicks too, and on press instead of release (a drag that starts on the pill edits the address).
- **Failure scenario:** a keyboard-only or switch-access user tabbing through the sidebar header skips the pill; a right-click on the pill (expecting a context menu) starts editing the address.
- **Fix:** `SetFocusBehavior(...)` (always, or accessible-only on Mac like `Button`), `views::FocusRing::Install`, handle Enter/Space in `OnKeyPressed`, accept only `IsOnlyLeftMouseButton()`, and activate on release; or make the pill a `views::Button` subclass with the existing child buttons. The AX name should include the current site ("Search or enter address" is static), since the label child is not exposed as the name.
- Confidence: high for the removal; the a11y impact assumes default `View` focus behaviour (`NONE`).

### S5. The e2e covers the happy path of Ctrl+T but not the paths where the bugs are
- **Where:** `scripts/e2e/urlbar.mjs` (whole file), `scripts/e2e/prefs/*`.
- **Gaps (each maps to a finding above):**
  - Ctrl+T goes through the page's CDP key path on Linux, so macOS reserved-command handling (B1) is not exercised, and nothing asserts the accelerator is registered at the right priority.
  - No window-deactivation test (S1).
  - Only one layout: the multiple-toolbars new-tab path (S3) is never run (`urlbar` runs with the default single toolbar; the PR runs three behaviours via `PREFS`, but those are manual, not a suite).
  - The `kActionNewTab` callback swap (the PR's claim for the new-tab button and menu item) is never invoked; the test clicks `KoanNewTabRow`, which calls `OpenForNewTab` directly. If `BrowserActions` ever re-creates the item (or `root_action_item()` is not populated at `KoanUrlBarController` construction), this silently stops working. The ordering looks fine today (`browser_view.cc:998` already reads `root_action_item()` in the ctor), but there is no test.
  - Dispositions other than Enter: Alt+Enter (`NEW_FOREGROUND_TAB`, hook does nothing, session ends only by focus move), Alt+Shift+Enter / ⌘+Enter (`NEW_BACKGROUND_TAB`, `OpenMatch` skips `end_edit_session`, bar stays open, `searchbox_utils.cc:501-540`), Shift+Enter (new window), "Switch to tab" matches. I read all of these as correct (see below), but nothing locks it in.
  - "Click outside" dispatches the event on the backdrop view itself (`mouse(find(vs,'KoanUrlBarBackdrop'))`), so it does not check that the backdrop receives real clicks above the page and the sidebar.
- **Fix:** add the cases above (at least: kActionNewTab invocation, multi-toolbar Ctrl+T + Esc, deactivate/activate, Alt+Enter).

### S6. [uncertain] In compact mode an in-pill edit loses its pill when the sidebar peek ends, so the bar jumps to floating mid-typing
- **Where:** `koan_url_bar_controller.cc:244-256` (`OnBrowserViewLayout` -> posted `OnModeChanged`), `:345-349` (`IsPillAvailable`), `:524-530`; the peek lock is released by `KoanCompactModeController::CheckSidebarPeek` as soon as the pointer leaves the sidebar.
- **What is wrong:** nothing keeps the sidebar peeked while the omnibox is edited in the pill. Zen keeps the compact sidebar open while the URL bar has focus.
- **Failure scenario:** compact mode (sidebar hidden), peek the sidebar, click the pill, move the pointer to the page: 400 ms later the pill disappears, `DesiredPlacement()` flips to floating, and the bar jumps to the centre with the typed text (it works, but with a layout flash). Also relevant for #56 which changes this code.
- **Fix:** hold `peek_lock_` (or an equivalent keep-expanded lock) while `placement() == kInPill`, or float immediately when the bar is opened from a peeked sidebar in compact mode.

---

## Nits

- **N1. Stale docs.**
  - `docs/USAGE.md:76`: "Esc (twice when suggestions are showing)" contradicts the PR's one-press Esc.
  - `docs/ZEN_PARITY.md:23`: "Ctrl+T still opens the tab at the end" is no longer true.
  - `docs/ZEN_PARITY.md:43`: "URL bar commands (command palette) | Missing | #40" is stale once #55 is merged (it is, so fix it when resolving the REBASE/USAGE conflicts).
  - `docs/REBASE.md` new rows: no "(+ BUILD.gn)" for the two BUILD.gn patches (other rows do this), and the "URL bar placement" row should mention the crbug.com/390669712 clip workaround (`browser_view_->InvalidateLayout()` in `Layout()`) and the reliance on `OmniboxEditModel::OpenMatch` running `RevertAll()` *before* `OnAutocompleteAccept` (`omnibox_edit_model.cc:~2772-2860`; the hook and the "typed text is not remembered after commit" logic depend on it).
- **N2. Zen parity details not done (none mentioned in ZEN_PARITY except the first).**
  - Zen hides the site-data button until the pill is hovered (`zen-omnibox.css` single-toolbar `.identity-box-button` `opacity: 0` -> 1 on `zen-has-implicit-hover`); Koan shows it all the time.
  - The pill has no hover background (Zen: `--zen-toolbar-element-bg-hover`).
  - Floating card has no 0.5px outline (`rgba(0,0,0,.2)` / `rgb(75,75,75)`) and no font-weight 500 (400 on Linux).
  - Zen's "Copied current URL!" toast carries a Share button on macOS (`ZenCommonUtils.mjs:103-122`, `Services.zen.canShare()`); Koan's does not. Mac is the product, so worth a line in ZEN_PARITY.
  - Zen copies the stripped URI (`gURLBar.zenStrippedURI`), Koan the raw `GetVisibleURL().spec()`.
  - Zen clears the remembered search on a tab switch (`handleUrlbarClose(onSwitch=true)`); Koan keeps it for 45 s when the focus change was caused by switching tabs.
  - Issue #36 lists "dimmed backdrop"; the PR (like Zen's CSS) only draws the shadow. Say so in the PR body (ZEN_PARITY does).
- **N3. `kNewTab` shortcut row has no availability predicate** (`koan_shortcut_table.cc:75`): it shows in Settings > Shortcuts even with `chrome://flags` "Koan URL bar" off or `replace_newtab` off, where it does nothing but fall through. Other Koan-feature actions pass `features::Is...Enabled`.
- **N4. Screenshot housekeeping.** `docs/screenshots/readme-floating-url-bar.png` and `urlbar-floating.png` are byte-identical (blob `fede3ee`); `floating-url-bar.png` (old look) is now unreferenced, and `urlbar-normal.png` / `urlbar-toast.png` are not referenced from the docs. The PR body links the images through the `feat/zen-urlbar` branch, which breaks when the branch is deleted.
- **N5. [uncertain] Page-info bubble in a peeked sidebar.** `KoanSidebarNavView::ShowSitePanel` (`koan_sidebar_nav_view.cc:339`) anchors to the pill's button. In compact mode the sidebar slides away ~400 ms after the pointer leaves it, which happens as soon as the pointer moves into the bubble, leaving the bubble anchored to an off-screen button. Not checked at runtime. Also it does not pass `AddPageInfoClosingCallback` / `SetHighlightedElement` like `LocationBarView::ShowPageInfoDialog` (`location_bar_view.cc:2300-2315`); harmless today.
- **N6. [uncertain] Fade on every placement change.** `SetPlacement` (`koan_url_bar_controller.cc:453`) resets the top container's opacity to 0 and animates it back on pill -> floating too, so the first keystroke makes the whole bar, including the character just typed, flash for 120 ms. Zen animates the breakout, not a fade-from-zero of the text. Consider fading only when `opening` and keeping opacity when switching placement.
- **N7. [uncertain] Backdrop covers the whole overlay** (`:560-562`, `TopContainerOverlayView` is above the frame view's children), so a click on the caption buttons or the window drag area while floating closes the bar instead of reaching them. The PR documents "click only closes the bar"; I could not confirm whether the caption strip (#23/#32) is above or below the overlay in hit testing. Worth one e2e check on the caption hot zone while floating.
- **N8. Constants.** `kFloatingMaxWidth`, `kFloatingMinHeight`, the shadow numbers, `kInPillOpenMaxWidth` (`koan_url_bar_controller.cc:72-92`) and the pill's 26/16/12/5/2 (`koan_sidebar_nav_view.cc`) sit next to the code, while `koan_sidebar_metrics.h` says it holds the sizes "in one place". Radii and colours are correctly shared/IDs (checked: `kColorKoanUrlPill`, `kColorKoanFloatingUrlBarShadow`, `kUrlPillCornerRadius`, `kFloatingUrlBarCornerRadius`; the backdrop uses `kColorToolbar`).
- **N9. Fullscreen.** `OpenForNewTab` returns false in fullscreen (`:197`), so ⌘T in a macOS fullscreen window opens the NTP, although Koan's modes are off there and the standard toolbar could host the same new-tab session as in the multiple-toolbars layout (see S3).
- **N10. Backdrop is deleted inside its own press handler.** `KoanUrlBarBackdrop::OnMousePressed` (`:116-119`) runs `on_press_` -> `Close()` -> focus change -> `SetPlacement(kNone)` -> `RemoveChildViewT(backdrop_)`. Views tolerate this, but the Esc path posts a task and this one does not; posting `Close` here would be consistent.
- **N11.** `OnViewIsDeleting` (`:620-624`) clears sibling bookkeeping but not `top_container_observation_` / `toolbar_observation_`. Same as the code it replaced, and the window teardown path ends the session first (`Deactivate()` -> `EndEditing()`), so not a regression; noting it because the controller now owns the observations.

---

## Checked, no finding

- **OpenMatch / disposition hook:** `OmniboxEditModel::OpenMatch` calls `end_edit_session()` (RevertAll) before `OnAutocompleteAccept` for every disposition except `NEW_BACKGROUND_TAB` (`omnibox_edit_model.cc:2772-2788`, `2865-2885`), so converting CURRENT_TAB -> NEW_FOREGROUND_TAB inside `OnAutocompleteAccept` opens one tab, once; `new_tab_` is consumed by `TakeNewTabDisposition`. Alt+Enter (`NEW_FOREGROUND_TAB`) and Shift+Enter (`NEW_WINDOW`) are left alone and end by focus change; Alt+Shift+Enter / ⌘+Enter (`NEW_BACKGROUND_TAB`) keep the bar open with `new_tab_` set, and the next plain Enter opens a foreground tab. Actions and extension keyword matches never reach `OnAutocompleteAccept`, so they end by focus change (the #55 palette's `contents->Focus()` does that).
- **Esc, blur, click outside, window close:** Esc posts `Close()`; blur and click outside end the session and `Revert()`; teardown goes `OnBrowserViewDestroying` -> `Deactivate` -> `EndEditing`; posted tasks use weak pointers; the swapped `kActionNewTab` callback falls back to `chrome::NewTab` once the controller is gone. Exception: S1, S3.
- **Behaviour variants:** `DesiredPlacement()` matches `normal` / `floating-on-type` / `float` and unknown strings behave as the default. `LocationBarView::OnChanged` hook fires on `user_input_in_progress` transitions only (the member cache makes it edge-triggered).
- **Copy URL:** same clipboard write as `chrome::CopyURL` (`browser_commands.cc:2975-2978`); the replaced Chrome toast is no longer shown (no double toast); Markdown matches Zen's format; pref read via `GetOriginalProfile()` so incognito works; the toast goes through the shared `KoanToast` (`controller->toast()->Show`), no new toast class. (Chrome's `CanCopyUrl` policy check was not applied before either.)
- **Site panel API:** `PageInfoBubbleSpecification::Builder(BubbleAnchor, NativeWindow, WebContents*, GURL)` and `PageInfoBubbleView::CreatePageInfoBubble` match 158; same entry guard as upstream (`IsInitialEntry`).
- **#47:** the controller adds no timers (only `PostTask`); opening makes the layer visible again (`OnUrlBarOpenChanged`) and closing calls `ApplyToolbarTransform(false)` -> `HideAwayToolbarLayer`, so the slid-out toolbar layer stays hidden. **#23/#32:** card geometry is the old one (2/3 width, 750 max) with Zen's 333 vertical centring and radius 15; hot zones are untouched. **#46:** settings rows are in the `kRows` table, `kNewTab` is in the shortcut table as data. **#54:** shared `KoanToast`. **#50:** pill keeps the site name; the favicon was dropped, which matches Zen's single-toolbar pill.
- **Linux-only code outside guards:** none found. Mac values behind `BUILDFLAG(IS_MAC)`: floating font delta, ⌘L tooltip. The only macOS gaps are B1 and S2.
