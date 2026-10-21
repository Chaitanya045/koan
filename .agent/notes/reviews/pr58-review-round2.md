# PR #58 review, round 2: URL bar like Zen's (#36, #24)

Reviewed: `a1c234e` (branch `feat/zen-urlbar`, equal to `origin/feat/zen-urlbar`), diffed against `origin/develop` `8b8490b` (#56 on top of #55). Round 1: `pr58-review.md`; the author's reply is the last PR comment.
Nothing was built or run. Everything comes from reading the diff and Chromium 158 (`~/work/koan/chromium/src`, HEAD `67e29b8`; the Mac checkout is the same revision, checked over ssh). I applied the six patches to pristine copies of the upstream files in a scratch directory (`git apply --check`).

**Verdict: safe to merge.** 0 blockers, 0 should-fix, 7 nits. B1 and S1-S6 are fixed as claimed. One gap stays open that no reading can close: nobody has pressed Cmd+T or used File > New Tab on a Mac (`TODO(#51)` in `scripts/e2e/urlbar.mjs:8`). I traced the code path and found no fault, but do that manual check on the MacBook once after merging.

---

## Blockers

None.

## Should fix

None.

## Nits

### N1. REBASE.md (and the PR reply) say the Dock menu runs `IDC_NEW_TAB`; Chromium 158's Dock menu has no New Tab item
- **Where:** `docs/REBASE.md:55` ("the menus' New Tab and the Dock menu still run `IDC_NEW_TAB` through here"); same claim in the author's reply.
- **What is wrong:** `-[AppController applicationDockMenu:]` (`chrome/browser/app_controller_mac.mm:2144-2200`) builds only profile items, New Window, and New Incognito / Isolated Window (`commandFromDock:`). The `IDC_NEW_TAB` case in `executeCommand:withProfile:` (`app_controller_mac.mm:1746-1751`) is reached by File > New Tab (and Cmd+T) while no window is key. It calls `ActivateBrowser()` and then `chrome::ExecuteCommand(browser, IDC_NEW_TAB)`, so the hook does cover it. The code works, but the maintenance doc names an entry point that does not exist.
- **Failure scenario:** on the next Chromium rebase someone audits the "Dock menu" path, finds nothing, and either wastes time or concludes the row is stale and drops the real assumption (`AppController` runs the command through `BrowserCommandController`).
- **Fix:** replace "the Dock menu" with "`AppController`'s File > New Tab when no window is key (`executeCommand:withProfile:`)".

### N2. Holding Ctrl+T on Linux/Windows toggles the bar open and closed
- **Where:** `src/browser/ui/views/koan_shortcuts.cc:131` (`AcceleratorPressed` has no `IsRepeat()` check), `koan_url_bar_controller.cc:195-205` (a second `OpenForNewTab()` with `new_tab_` set calls `Close()`).
- **What is wrong:** Chrome drops auto-repeat for `IDC_NEW_TAB`: `BrowserView::FindCommandIdForAccelerator` (`browser_view.cc:5978`) returns false for a repeated accelerator whose command is not in `kRepeatableCommandIds` (`accelerator_table.cc:340`, which does not list `IDC_NEW_TAB`). In `PreHandleKeyboardEvent` (`browser_view.cc:3287-3293`) that sends the repeat to `focus_manager->ProcessAccelerator()`. That reaches Koan's registered Ctrl+T target. Without Koan, nothing handled it. macOS is not affected: `chrome_command_dispatcher_delegate.mm:101` drops repeats of `IDC_NEW_TAB`.
- **Failure scenario:** hold Ctrl+T for half a second on Linux: the bar opens, closes, opens... about 15 times and ends in either state, with the fade restarting each time.
- **Fix:** in `AcceleratorPressed` (or in `Run` for `kNewTab`) return true without acting when `accelerator.IsRepeat()`.

### N3. With the `koan-url-bar` flag off, the floating bar still gets the new look, the Esc handler and the backdrop
- **Where:** `koan_url_bar_controller.cc:368-370` (the only `IsUrlBarEnabled()` gate in placement), `ApplyCardStyle()` (font, backdrop, shadow), `UpdateKeyHandler()`/`OnKeyEvent` (Esc), `OnFocusChanged` (`bar->Revert()`), none gated.
- **What is wrong:** the flag text says it controls "Zen's URL bar: a behaviour setting, Ctrl+T ..., a copy-URL button", and `DesiredPlacement()` returns "Koan's earlier behaviour: always floating". But the rest of the controller still runs, so the earlier behaviour (plain 2/3-width card, Chrome's multi-step Esc, no shadow backdrop that swallows clicks) cannot be had back by switching the flag off. The pill's new layout (site button, label) is not gated either. The hooks themselves are correctly inert: `OpenForNewTab` returns false first thing, `TakeNewTabDisposition` needs `new_tab_`, and `OnOmniboxChanged` returns when nothing is open.
- **Failure scenario:** a user who disables `koan-url-bar` to get the old floating bar back still gets the shadow backdrop that closes the bar on any click, the larger font and the one-press Esc.
- **Fix:** gate the backdrop, the Esc handler and the larger font on `IsUrlBarEnabled()`, or reword the flag description to say it only controls the new-tab and behaviour settings.

### N4. USAGE.md says the tab strip's "+" opens the URL bar; with horizontal tabs it does not
- **Where:** `docs/USAGE.md:108`; `docs/ZEN_PARITY.md:45` has the caveat.
- **What is wrong:** the horizontal strip's "+" is `TabStrip::NewTabButtonPressed` -> `BrowserTabStripController::CreateNewTab` -> `chrome::NewTab` (`browser_tab_strip_controller.cc:469-471`), which bypasses both hooks. The vertical strip's "+" (the only one that reaches `kActionNewTab`) is hidden by Koan (#50). So the one visible tab-strip "+" does not do what USAGE says. The `kActionNewTab` callback swap is still useful: the action-based app menu uses that action (`action_app_menu_manager.cc:617`).
- **Failure scenario:** a user on horizontal tabs presses "+" per USAGE and gets a new-tab page.
- **Fix:** add "(not with horizontal tabs)" to USAGE.md, as ZEN_PARITY.md already does.

### N5. The pill's copy button is unreachable for keyboard and screen-reader users, and the pill's label is also exposed as a second node
- **Where:** `koan_sidebar_nav_view.cc:339-343` (`copy_button_` is visible only while `hovered_`), `:334` (comment "the label isn't exposed itself"), `:188` (`KoanUrlPill : views::Button` with child buttons and a `Label`).
- **What is wrong:**
  - Focus order is fine: pill, then the copy button when visible, then the site button, in child order, with a focus ring from `Button` (`button.cc:675`) shaped by `InstallRoundRectHighlightPathGenerator`. But an invisible view is not in the focus order or the AX tree, so the copy button can never be tabbed to or read out. (Ctrl+Shift+C and the palette's "Copy URL" cover the function; the site button is always visible, as the author intended.)
  - The comment is wrong for the tree as Views builds it: `ViewAccessibility::GetChildren` (`view_accessibility.cc:1925`) returns the children unless the node is a leaf, and nothing in `ui/views/controls/button` or this file marks the pill leaf or the label ignored. So a screen reader gets the pill (named with the site), then a static-text child with the same text. I did not run a screen reader; this follows the tree code.
- **Failure scenario:** a VoiceOver user lands on the pill and hears the site name twice, and never gets a "Copy URL" button.
- **Fix:** `url_label_->GetViewAccessibility().SetIsIgnored(true)`; optionally keep the copy button in the focus order (invisible-until-focused) or note the shortcut in the pill's description.

### N6. Esc closes the bar for Ctrl/Alt/Cmd+Esc too
- **Where:** `koan_url_bar_controller.cc:651` checks only `!event->IsShiftDown()`.
- **What is wrong:** any Esc with Ctrl, Alt or Cmd held is consumed as "close the bar" and marked handled; only Shift is excluded (Shift+Esc is Chrome's task manager).
- **Failure scenario:** pressing Ctrl+Esc, Alt+Esc or Cmd+Esc while the bar is open closes it, and the key never reaches whatever else is bound to it in the browser.
- **Fix:** require no modifiers: `(event->flags() & ui::EF_ALL_MODIFIERS) == 0`.

### N7. Constants and docs trivia
- `scripts/e2e/palette.mjs:9`: the comment line was extended past 80 columns ("and Ctrl+T opens a tab, not the URL bar). Text is typed ...") and now reads as one run-on sentence.
- The settings row label says "(Ctrl+T, New Tab)" (`koan_settings_rows.cc`) on macOS too, while the pill tooltip in `koan_sidebar_nav_view.cc` already switches to the Command key on Mac.

---

## Checked, no finding

**1. B1 fix (the `IDC_NEW_TAB` hook)**
- **Mac, Cmd+T:** the reserved-command path is real. `prePerformKeyEquivalent` tries the FocusManager at high priority only (`chrome_command_dispatcher_delegate.mm:79-82`), then `CommandForKeyEvent` -> `BrowserNativeWidgetMac::ExecuteCommand(before_first_responder=true)` (`browser_native_widget_mac.mm:619-654`; it runs only if `IsReservedCommandOrKey`, and `IDC_NEW_TAB` is in `kReservedCommandIds`, `accelerator_table.cc:347-352`) -> `chrome::ExecuteCommandWithDisposition` -> `BrowserCommandController::HandleCommandWithDisposition` -> the new hook. Koan's normal-priority accelerator is never reached for a reserved command, so a key press cannot run both.
- **Mac, File > New Tab:** same function through `commandDispatch:`. With no key window it goes through `AppController executeCommand:withProfile:` -> `chrome::ExecuteCommand(browser, IDC_NEW_TAB)` and reaches the hook (but see N1: there is no Dock "New Tab" item).
- **Mac, inactive window at that moment:** `NativeWidgetMac::ShouldActivateOnFocusRequest()` (`native_widget_mac.mm:838-853`) is false for a hidden or minimized window or an inactive app, so `FocusLocation()` focuses the omnibox directly (the "legacy" path) and `HasFocus()` is true. `OpenForNewTab()` does not fall back to a new-tab page in that case.
- **Linux/Windows, one key press, one action:** Ctrl+T is reserved, so `BrowserView::PreHandleKeyboardEvent` (`browser_view.cc:3300-3306`) calls `focus_manager->ProcessAccelerator()`. Koan's normal-priority target is tried first (`AcceleratorManager` pushes new targets to the front, `accelerator_manager.cc` targets_ `push_front`; the comment at `koan_shortcuts.cc:97-98` says so). It returns true after `OpenForNewTab()`, so Chrome's accelerator and the command do not run. If it returns false (fullscreen, pref off, no focus), Chrome's accelerator runs the command, the hook calls `OpenForNewTab()` again with the same false result, and `NewTab` runs once. The only exception is the repeat case in N2.
- **Incognito:** `GetIsNormalType()` is true, the pref is read through `GetOriginalProfile()`, so the bar works like in a normal window. `last_search_` lives in the window's controller for at most 45 s.
- **Empty tab strip:** `OpenForNewTab()` checks `GetTabStripModel()->empty()`, and `ExecuteCommandWithDispositionAndContext` returns before the switch when there is no active tab (`browser_command_controller.cc:658-662`).
- **Popup, DevTools, app/PWA windows:** `BrowserView::GetIsNormalType()` is `GetType() == TYPE_NORMAL` (`browser_view.cc:1556`). `OpenForNewTab()` and the accelerator's `CanHandleAccelerators()` both check it, and `InstallNewTabAction` skips non-normal windows. On Mac app windows `IDC_NEW_TAB` is not reserved (`IsReservedCommandOrKey` returns false for apps), so the path is menu -> command -> hook -> false -> `NewTab`; also no double run.
- **Extension commands on Ctrl+T:** an extension cannot bind it: `CommandService::CanBeBound`-style check returns `!IsChromeAccelerator(...)` (`command_service.cc:575`) and the accelerator table contains Ctrl+T / Cmd+T. Extension commands use the high-priority slot anyway (`accelerator_priority.h`), so on Mac and Linux they would run before Koan's, consistent with the hook.
- **Feature off / pref off:** `OpenForNewTab()` starts with `!features::IsUrlBarEnabled()` and reads `kUrlBarReplaceNewTab`; either way it returns false and `NewTab` runs. (The rest of the controller is not gated: N3.)
- **BUILD.gn:** the second hunk of `patches/chrome/browser/ui/BUILD.gn.patch` lands in the `deps` of `browser_command_controller_impl` (the `source_set` at `BUILD.gn:683-813`, hunk at line 776). `//koan/browser/ui/views` is the header-only target (`public_deps` base, ui/base, ui/gfx/geometry), so there is no cycle; the definitions live in `:impl`, linked through `//chrome/browser:browser` like the other hooks. `koan_url_bar.h` forward-declares `BrowserWindowInterface`, which is what `BrowserCommandController::browser_` already is (`browser_command_controller.h:302`).

**2. S1-S6**
- **S1 (early return while inactive):** the premise holds. Aura: `DesktopWindowTreeHostPlatform::OnActivationChanged` sets `is_active_` before `HandleActivationChanged` (`desktop_window_tree_host_platform.cc:1151-1160`), and `DesktopNativeWidgetAura::IsActive()` also requires the content window to be the active window, so a child bubble taking activation (page info, extension popup, a JS dialog) counts as inactive too and the bar now survives it (`desktop_native_widget_aura.cc:1096-1100, 1587-1601`). Mac: `is_window_key_` is set before `OnWindowKeyStatusChanged`, which calls `OnNativeWidgetActivationChanged(false)` before `StoreFocusedView` (`native_widget_mac.mm:195-215`). I looked for a way to get stuck open and found none: focus is restored with the window (`RestoreFocusedView`), any later focus into a view outside the bar closes it, `Close()` requests page focus (which, when inactive, only stores the view and activates), and teardown calls `EndEditing()`. At worst the bar stays up in a background window until it is activated, which is the intended behaviour.
- **S3 (Esc handler lifetime):** `~KoanUrlBarController` removes itself from `key_target_.view()`, and `ui::EventHandler`'s destructor does not remove it from targets, so that removal is needed and present. The destructor runs from `DestroyFor` -> `ClearProperty` while the BrowserView's children are alive. A `ViewTracker` covers the omnibox dying first. Esc posts `Close()` through a weak pointer, so nothing is destroyed inside the dispatch. `UpdateKeyHandler()` is called from every place `is_open() || new_tab_` changes (open, close, `OpenForNewTab`, `EndNewTabSession`). It coexists with the palette's Tab handler (`koan_command_palette.cc:468-481`, different key). Only N6 (modified Esc).
- **S4 (`KoanUrlPill` as a Button):** left-button on release and Space/Enter come from `ButtonController`; the focus ring and platform focus behaviour come from the `Button` constructor (`button.cc:672-675`); child buttons take their own events and keys; the label has `SetCanProcessEventsWithinSubtree(false)`. Focus order and the AX tree: N5.
- **S5:** the new e2e checks exist for window deactivation, peeked-sidebar floating, multiple toolbars (Esc, app menu New tab, Alt+Enter), and the behaviours via `PREFS`. The Mac key path is the `TODO(#51)` above.
- **S6 (float immediately in compact mode):** the cause is real: `VerticalTabStripRegionView::OnOmniboxPopupVisibilityChanged` takes `kForceCollapse` (`vertical_tab_strip_region_view.cc:1217-1221`). `hides_sidebar()` is `mode_ == kCompact && hide_sidebar_`, so only compact-with-hidden-sidebar floats; "hide toolbar only" keeps the sidebar and the pill. Non-compact modes are unaffected.

**3. Conflicts with #55 and #56**
- **#55:** the palette's `Execute()` posts `RunCommand`, which does `contents->Focus()` first. That closes the bar through `OnFocusChanged`, and `OmniboxEditModel::OpenMatch` has already run `end_edit_session()` (`RevertAll`) before `ExecuteAction` (`omnibox_edit_model.cc:2849-2856`), so `user_input_in_progress_` is false by then and no ">" query is remembered as `last_search_`. The palette no longer references floating state; no leftover uses of `is_url_bar_floating`, `SetUrlBarFloating` or `LayoutFloatingUrlBar` anywhere in `src`, `scripts`, `docs`, `patches`. The palette's "Copy URL" now goes through `CopyCurrentUrl` with Zen's toast.
- **#56:** the port keeps `hides_toolbar()` / `hides_sidebar()` and the variant flags; `is_toolbar_away()` is `!toolbar_revealed_ && !IsUrlBarOpen()`, the same meaning as before; `OnUrlBarOpenChanged()` gives the layer back on open (`toolbar_revealed_ = false`, layer visible) and re-hides it through `ApplyToolbarTransform(false)` on close (#47 keeps the slid-out layer hidden); `SetToolbarRevealed` ignores calls while the bar is open; `OnDidChangeFocus` still reveals the toolbar for other toolbar focus, and runs `CheckToolbarHide()` once the bar has closed; `Deactivate()` ends the session before `mode_` is cleared, and `DestroyFor` runs `compact_mode_->OnBrowserViewDestroying()` while `url_bar_` is alive (members are destroyed in reverse order, `url_bar_` after `compact_mode_` was told). Tabs on the right: `CardBounds` uses `IsSidebarOnRight()` and widens leftwards. `UpdateActive` forwards a behaviour-pref change to `OnModeChanged()`.

**4. Patches**
- All six apply cleanly (`git apply --check`) to pristine copies of `chrome/browser/ui/BUILD.gn`, `browser_command_controller.cc`, `omnibox/BUILD.gn`, `omnibox/chrome_omnibox_client.cc`, `views/location_bar/BUILD.gn` and `location_bar_view.cc` at Chromium `67e29b8`. No other patch touches the same files. Each source patch is an include plus one call (the command hook is a 3-line `if`); the BUILD.gn patches add one dep each. `apply_patches.sh` picks them up by `find`, and `docs/REBASE.md` has a row for each hook (float on typing, new tab from the bar, New Tab command) plus the new-tab-button row, naming the BUILD.gn change.

**5. Other**
- `OnAutocompleteAccept` disposition conversion: `OpenMatch` reverts the box before it for every disposition except `NEW_BACKGROUND_TAB`, so one tab opens, once; `browser_navigator.cc:1038-1047` adds the new tab to `GetFocusedGroup()`, which is Koan's active workspace.
- Copy URL accelerators on `kAccel`: Ctrl+Shift+C is unchanged on Linux; on Mac Cmd+Shift+C is not a reserved command, so it reaches the normal-priority accelerator in `postPerformKeyEquivalent`.

## Not verified (not counted above)
- Anything that needs a Mac: Cmd+T, File > New Tab, and the Cmd+Shift+C copy shortcut on real hardware.
- N7 from round 1 (does the backdrop cover the caption buttons / drag area while floating): the overlay is a later sibling than the frame view, so the backdrop probably does cover them. The author left it open and documented "a click only closes the bar".
- N5 from round 1 (page info bubble from a peeked compact sidebar): left open by the author.
- Screen-reader behaviour of the nested buttons (N5 above is from the tree code only).
