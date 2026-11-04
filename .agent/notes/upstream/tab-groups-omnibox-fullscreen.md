# Upstream-behaviour audit 2: tab groups / Spaces, omnibox / NTP, macOS fullscreen

Date: 2026-11-04. Chromium 158.0.8127.2 (`681cd621e8`), Koan `develop` 77a44141, plus the open PRs that touch these areas (#96, #105, #129, #143, #145, #152, #155). This is a follow-up to `.cache/reviews/vertical-tabs-audit.md` and uses the same method. It was read only: pristine Chromium came from `git show HEAD:` / `git grep HEAD`, and Zen from `.cache/zen-desktop-1.24b`. Nothing was built or run, so every "reachable" verdict comes from reading code. Verdicts marked *(live check)* still need a run on the Mac. Change ids: **G** = tab groups, **O** = omnibox/NTP, **F** = fullscreen. **A** refers to the vertical-tabs audit.

**Summary.** The same pattern shows up in all three areas: Koan uses an upstream feature for its mechanism, but leaves that feature's *side effects and other entry points* alone.

- **Spaces = focus mode.** Koan heals focus exits well. But it never sees what focus mode *does* while it is on:
  - a per-window colour override that also suppresses themes;
  - freezing every tab in the other Spaces;
  - collapsed groups that cannot be expanded.
  
  Several Chrome group shortcuts are also still live (⌘⌃P/W/C/X/Z, Tab > Group Tab).
- **Floating URL bar = upstream's real location bar, moved.** The dropdown is upstream's **WebUI** popup, which is on by default in M158. Koan's #97 row styling only reaches the Views popup, so it is dead code, and all of Google's AI Mode / composebox / "add context" UI shows. Cmd+L keeps the bar in the pill, so the dropdown hangs inside the sidebar (the reported bug). Draft #96 fixes most of this but is not merged.
- **Fullscreen = upstream's immersive mode, bent at two points (#128).** Browser fullscreen holds up. **Content (video) fullscreen is unknown to Koan**: immersive stays enabled, so the content card, compact hot zones, toolbar reveal and URL bar all stay live over the video. #152 covers every close path traced, but it has not been run on the Mac.

None of the fixes needs a new patch file. The exceptions are #96's own (`search.cc`) and an optional flags-UI hide. The rest is:
- feature overrides;
- `src/` code;
- per-window `ActionItem` re-pointing;
- 1-2 lines in existing patches.

Total: about 4-5.5 agent-days and two or three Mac builds.

---

## 0. Cross-cutting (shared with the vertical-tabs audit)

1. **One command-policy layer.** A4 (vertical tabs), G4 (group commands), O2 (`kActionFocusLocation`) and F3 (`kActionToggleFullscreenToolbar`) all re-point per-window `ActionItem` callbacks, as `koan_url_bar_controller.cc` `InstallNewTabAction` already does. Put them in one table in one place (the A-layer policy or a `koan_command_policy.cc`). Force `kUseActionsForBrowserCommands` on in `koan_feature_overrides.cc` once for all of them. For each redirect, keep the legacy-switch backstop next to it: G5 for the tab menu, A1 for the mode.
2. **One Mac menu hunk.** A6, G7 and F3 all hide items *after* upstream's `ValidateUserInterfaceItem` switch in `browser_native_widget_mac.mm.patch`:
   - View > vertical tabs / collapse;
   - Tab > Group Tab / Group Target Tab;
   - View > Always Show Toolbar in Full Screen.
   
   Do them as one hunk with one `koan::HidesMenuCommand(id)` table.
3. **`Mode::kNone` is the common funnel.** Whenever Chromium's toolbar shows (rail, Ctrl+Alt+B layout, compact "hide sidebar only" in fullscreen), all of these come back:
   - upstream's omnibox dropdown, placeholder and page actions (O14);
   - the app menu with "Tab groups" (G4);
   - the fullscreen reveal and the synced "Always show toolbar" pref (F9).
   
   A10 (vertical-tabs) decides what `kNone` means. O14 and F9 should follow that decision, not run ahead of it.
4. **One teardown owner.** F4 is A13. #129 and #152 both add `destroying_`: land #152 first, and fold both into the A-layer policy.
5. **One feature-override block, reviewed per roll:**
   - omnibox WebUI popup / AIM / Lens / Glic / NTP features (O1, O5, O11, O13);
   - tab-group IPH (G11);
   - vertical-tabs IPH (A12);
   - possibly `kPressAndHoldEscToExitBrowserFullscreen` (F10).
   
   Add a `docs/REBASE.md` row for each, because these are the upstream features most likely to change between milestones.
6. **One DCHECK leak detector per area:** A14, G14, O15 and F13. Run them all in the same Mac-loop e2e pass, so that upstream additions fail loudly.

## Highest-risk leaks, all areas (ranked)

| # | Severity | Leak | Who reaches it | Fix |
|---|---|---|---|---|
| 1 | High | **Omnibox dropdown is upstream's WebUI popup.** It is unstyled (the #97 row hooks are dead), and it carries AI Mode chip / composebox / "add context" | Every user, every Cmd+L / Cmd+T / pill click | O1 (feature overrides) |
| 2 | High | **Content (video) fullscreen keeps Koan's chrome.** It gets the asymmetric 8 pt card. In compact, the top edge slides Koan's toolbar over the video, edge hot zones eat the pointer, and ⌘L opens the URL bar over it | Any web page, extensions (`windows.update({state:'fullscreen'})`) | F1 + F2 (`src/` only) |
| 3 | High | **Focus-mode colour override + theme suppression in every Spaces window.** Grey (default) Spaces override the user's colour, Web Store themes never show, and it fights Koan's profile tint | Every user | G1 (1 line, existing `tab_strip_collection_controller.cc.patch`) |
| 4 | High | **Focus-mode freezing freezes all tabs in other Spaces at once** (`kTabGroupsFocusFreezing` param default true). This contradicts `docs/PERFORMANCE.md` 5-MRU | Every user with >1 Space | G2 (1 line, existing `tab_view.cc.patch`) |
| 5 | High | **Cmd+L opens in the pill and the dropdown hangs in the sidebar.** The NTP default-focuses the omnibox, so the bar is already in the pill (the reported bug) | Every Cmd+L, every NTP | O2 + O3 (`src/` + existing patch) |
| 6 | High (process) | **#152 not yet run on the Mac.** develop crashes on every window close (#153) until it lands | Everyone on develop | Land #152 after its checklist; rebase #129 |
| 7 | High | **AI Mode / AIM composebox / `@gemini` / `@aimode` / Lens context menu / NTP AI box** are live with Google DSE | Google-DSE users (the default) | O5 + O6 |
| 8 | High *(live check)* | **Collapsed groups → Spaces that show no tabs**, because the hidden header is the only expander | Copied Chrome profile; `chrome.tabGroups.update({collapsed:true})` | G3 (`src/`) |
| 9 | Medium | **Group shortcuts and the Tab menu run Chrome's group UI.** ⌘⌃P makes an unnamed Space in Chrome colours and opens the editor bubble. ⌘⌃W closes a Space without asking and leaves 2 NTPs. "Group Tab" ungroups | Keyboard / menu bar | G4 + G5 + G7 |
| 10 | Medium | **Spaces exist only in the session file.** Startup "Open the New Tab page" wipes them on relaunch | chrome://settings/onStartup; copied profile | G9 |
| 11 | Medium | **Google NTP** (+ auto-opening "Customize Chrome" panel) on every non-Cmd+T new-tab path | New window, tab-menu "New tab below", `tabs.create({})` | Land #96 split; interim O11 overrides; O12 |
| 12 | Medium | **Pages can swallow ⌘T/⌘W/⌘L/⌘N in Koan's browser fullscreen.** It always counts as "toolbar hidden" | Any page with key handlers, in fullscreen | F6 (existing `browser_command_controller.cc.patch`) |
| 13 | Medium | **Remote trending zero-suggest and search suggestions on** (Zen ships them off; privacy default) | Every user | O7 (pref default) |
| 14 | Medium | **Teardown UI work in `Deactivate()`.** Safe today only because of ordering | Window close | F4 = A13 |
| 15 | Medium | **EEA search-engine choice asked twice.** The welcome records `kOther` | EEA users *(live check)* | O10 |
| 16 | Medium-low | **Pre-existing saved/synced groups** open as sync-connected Spaces with Chrome's "Delete group?" dialog; the saved-groups bar shows on ⌘⇧B | Copied Chrome profile | G8 |
| 17 | Medium | **`kNone`-mode fullscreen** follows Chromium plus the synced `kShowFullscreenToolbar` pref | Rail / Ctrl+Alt+B / compact hide-sidebar-only | F3 + F9 (after A10) |
| 18 | Low-medium | Extensions' `tabs.group` creates Chrome-coloured Spaces. Cmd+Shift+T resurrects a deleted Space. Switch-to-tab inside a Cmd+T session duplicates the tab | Extensions; user | G12, G10, O9 |
| 19 | Low | Cocoa drops toolbar-style changes while a reveal lock is held. Exit affordance / Esc parity. Infobar jump in fullscreen | User, in fullscreen *(live check)* | F8, F5/F10, F7 |
| 20 | Low | `chrome://flags/#tab-groups-focusing` Disabled turns Spaces off; `#omnibox-webui-*` flags re-enable the WebUI popup | Flags only | G13 (deferred), O15 detector |

**Suggested PR packaging**, each independent and each one Mac build:

1. **Omnibox lockdown**: O1-O10, O13, O15. Split from #96, about 1-1.5 days. It fixes #1, #5, #7, #13 and #15 in the table above.
2. **Spaces own focus mode**: G1-G8, G11, G12, G14 (about 1.5 days). Then **Spaces persistence**: G9, G10.
3. **Content fullscreen**: F1, F2, F6, F13, after #152 lands.
4. Fold G4, O2 and F3 into the vertical-tabs policy PR (#155) command table, and fold F4 into its A13.

---

## 1. Tab groups and group focus mode under Spaces


**Summary.** Spaces are upstream tab groups that Koan shows one at a time through upstream's focus mode. Koan never prevents focus mode from being exited. Instead, it heals afterwards: every change schedules `KoanWorkspaceController::Update()`, which re-focuses a Space and pulls loose tabs back into one. That self-healing covers most entry points well; the user sees at most a flicker. The real leaks come from **side effects that run while focus mode is on**, which Koan never notices:
- the **window colour override and theme suppression** that upstream applies to a focused window;
- the **freezing votes** upstream puts on every tab outside the focused group;
- **collapsed groups**, whose tabs stay hidden.

There are also some keyboard paths (⌘⌃P / ⌘⌃W / ⌘⌃C / ⌘⌃X / ⌘⌃Z, the Tab menu's "Group Tab") that run upstream's group UI. And Spaces exist only inside the session file, so any startup without a session restore loses them. All of this can be fixed with `src/`, `ActionItem` re-pointing and two existing hook patches. No new patch file is needed.

---

### 1.1 How Koan maps Space <-> tab group (the model)

- **Space = non-ephemeral `TabGroup` of a normal, non-OTR window.** Spaces apply only when the window has Spaces **and** `features::kTabGroupsFocusing` is on (`koan_workspaces.cc:318-324` `IsEnabled()`). `koan_feature_overrides.cc:17` enables that feature. It is only an override, so `chrome://flags/#tab-groups-focusing` (`about_flags.cc:7357`) can still switch it off, and then Spaces disappear.
- **Active Space = `TabStripModel::GetFocusedGroup()`** (`koan_workspaces.cc:502`). Switching calls `EnterFocusMode` (`:526`). Upstream's `EnterFocusMode` (`tab_strip_model.cc:2217`) is hooked so the Space opens on its last active tab (`koan::PickSpaceTab`, `koan_workspaces.cc:1570`; `tab_strip_model.cc.patch` @2252). `RotateFocusedGroup` (`:2323`, the swipe) is hooked to wrap around (`koan::WrapsSpaces`).
- **Order** = the groups' start index (`GetSpaces()`, `:480`). Ephemeral groups (`kNonGroupFocus`) are excluded.
- **Data lives in the group's visual data and nowhere else** (`ReadSpace` `:636` / `WriteSpace` `:652`):
  - the title is `"<emoji> <name>"`;
  - `TabGroupColorId` is the theme, and `kGrey` means "default theme";
  - `WriteSpace` builds `TabGroupVisualData(title, color)`, which resets `is_collapsed` to false, but only when Koan writes.
  - Spaces therefore persist only through session restore (session group metadata). No Koan pref stores Spaces. The only Space prefs are tint bookkeeping: `koan.workspaces.tint`, `.tint_applied`, `.tint_saved_color`, `.tint_saved_variant` (`koan_prefs.h:104-114`).
- **Invariant "one Space shown, every unpinned tab in a Space"** is enforced *after the fact*. `OnTabStripModelChanged`, `OnTabGroupChanged` and `OnTabGroupFocusChanged` (`:1391/1417/1453`) post `Update()`. `Update()` runs `EnsureOneSpaceShown()` (`:1063`), which:
  1. brings back a closed Space with a new tab under the **same id** (`AddToGroupForRestore`, `:1104`), unless Koan deleted it (`deleting_`) or it was detached to another window (`kDetachedToAnotherTabstrip`, `:1424`);
  2. adds **loose** tabs to the focused Space, else to `last_space_`, else to the first Space, else to a new "Space" (`:1117-1143`);
  3. re-enters focus mode, on the active tab's group or `last_space_`.

  So **every upstream exit from focus mode is undone on the next task**, and the target is the active tab's Space.
- Upstream UI that Koan hides or replaces:
  - the focused group's header and the Unfocus pill (`tab_group_view_vertical_layout.cc.patch`, via `koan::ShowsSpaceHeader`);
  - the "Unfocused tab group" toast (`tab_strip_model.cc.patch` @2301, `koan::ShowsFocusExitedToast`);
  - in the tab context menu, `CommandToggleFocusGroup / AddToExistingGroup / AddToNewGroup / RemoveFromGroup`, which are swapped for Zen's "Move To" (`koan_move_to_space.cc:94-127`).
- **Saving is off**: `koan::SavesTabGroups()` is false with Spaces (`koan_tab_hooks.cc`). It gates the three auto-save points: `saved_tab_group_model_listener.cc.patch` (`OnTabGroupAdded`, pristine `:59-85`), `session_restore.cc.patch` (`ProcessSavedGroup`, pristine `:1311-1335`) and `browser_live_tab_context.cc.patch` (`AddRestoredTab`, pristine `:405-412`). `TabGroupSyncService` still exists; it is required, because `AddRestoredTab` `CHECK`s it.
- **Per window, not per profile.** Each window has its own Spaces. Zen syncs the Space list across windows (`zen.window-sync.enabled` true, `src/zen/sessionstore/ZenWindowSync.sys.mjs`; spaces.md §8). That is a parity gap rather than a group leak, but it shapes the drag and new-window rows below.

Legend for **Koan**: *Owned* means Koan decides. *Redirected* means the entry point runs Koan's code. *Healed* means upstream runs and Koan's `Update()` restores the invariant afterwards. *Hidden* means the UI is not shown but the code path is live. *Hooked* means a patch changes upstream's behaviour. *None* means upstream behaviour leaks through.

### 1.2 Inventory

#### 1.2.1 Focus mode: state, side effects, features

| Upstream item | Where (pristine) | Koan today | Reachable by user / old profile / extension? | Zen 1.24b |
|---|---|---|---|---|
| `kTabGroupsFocusing` (default off) and its flag row | `ui_features.cc:560`; `about_flags.cc:7357-7359` | Enabled by override; `IsEnabled()` requires it | **User:** chrome://flags "Tab Groups Focusing: Disabled" turns Spaces off. Every Space then appears as a plain Chrome group with headers, the Space bar is gone, and `GetFocusedGroup()` returns nullopt everywhere | Workspaces are Zen's own (`ZenSpaceManager.mjs`), not Firefox groups |
| **Focus-mode window colour**: `TabGroupFocusChanged` → `UpdateFocusModeTheme(group)` → `browser_widget->SetUserColorOverride(GetTabGroupDialogColorId(color))`. The same runs on `kVisualsChanged` | `tab_strip_collection_controller.cc:589-606, 625-647`; `root_tab_collection_node.cc:245-253` | **None.** Koan's tint (`UpdateTint`, `:1515`) writes the *profile* user colour. Upstream's per-widget override wins in `BrowserWidget::GetColorProviderKey` (`browser_widget.cc:490-496`, applied after ThemeService's key, so after `koan::AdjustColorProviderKey`) | **Always on, in every window.** A default-theme Space (grey) gets a grey `kAccent` override, so the user's own colour never reaches the window. A coloured Space is tinted twice (Koan seed plus upstream dialog colour) *(live check: how much survives Koan's look mixers)* | Space gradient on the window background only (theme-picker.md) |
| **Focus-mode theme suppression**: with an override, `GetThemeProvider()` returns the *default* provider and `GetCustomTheme()` returns null | `browser_widget.cc:325-334, 352-360` | None | **Always on:** an installed Chrome Web Store theme (images, custom colours) never shows in a Spaces window. Koan's `UpdateTint` skips extension themes, which is moot because the theme is suppressed anyway | Firefox themes keep working |
| **Focus-mode freezing**: `kTabGroupsFocusFreezing` param (default **true**). `TabView::UpdateFocusFreezing` places a `FreezingVote` on every unpinned tab outside the focused group | `ui_features.cc:562, 571-574`; `tab_view.cc:470-490`; `tab_strip_collection_controller.cc:606-623` | **None** | **Always on:** every tab in every other Space has a freeze vote the moment the user switches away. PM freezes it unless a `CannotFreezeReason` applies. This contradicts `docs/PERFORMANCE.md` ("all but the 5 most recently used"): a chat or mail tab one Space away stops at once *(live check: chrome://discards "frozen")* | No freezing by workspace; Firefox only throttles timers |
| Selection rule: a pinned tab is valid inside a focused group | `tab_strip_model_selection_state.cc:53-61` | Used: Essentials (pinned) keep the Space focused | n/a | Essentials shown in every Space |
| `kNonGroupFocus` "Focus tabs" (ephemeral groups), `kTabGroupRibbon` | `ui_features.cc:576-583`; `about_flags.cc:7361-7367`; `tab_menu_model.cc:435-440` | Ephemeral groups are not Spaces (`GetSpaces` filter) | Flags only. With focus-selection on, tab-menu items are removed by Koan anyway (the item needs ungrouped tabs, which Spaces never leave) | n/a |

#### 1.2.2 Entering and leaving focus mode

Every `ExitFocusMode` notifies `OnTabGroupFocusChanged(nullopt)`, and Koan re-focuses on the next task (*Healed*). The rows below list where that is visible or wrong.

| Upstream item | Where | Koan today | Reachable? | Zen |
|---|---|---|---|---|
| Unfocus pill (`TabGroupUnfocusButton` → `chrome::ExitFocusMode(kHeaderUnfocusButton)`) | `tab_group_view.cc:86-90, 617-626`; layout `tab_group_view_vertical_layout.cc:58-76` | **Hidden** for Spaces (patch emplaces it with visible false) | No | n/a |
| Activate a tab outside the group → `SetSelection` exits (`kTabOutsideGroupActivated`) | `tab_strip_model.cc:4300-4311` | **Healed.** Koan focuses that tab's Space; toast suppressed. This is the switch-to-tab path (omnibox, tab search, `tabs.update({active})`, back-to-opener) | Yes; intended | Same (switch-to-tab changes Space) |
| Last tab of the focused group closes (`kLastTabClosed`) | `:5240-5256` | **Healed:** the Space is recreated with a new tab, same id | Yes; Zen parity | Empty Space keeps the empty tab |
| Ungroup the active tab, or all of the group (`MaybeExitFocusModeForUpdate`: `kGroupUngrouped` / `kActiveTabGroupOperation`) | `:6201-6225` | **Healed.** The tab is re-added at the **end** of the Space (`add_to_end=true`) | Mac Tab menu "Group Tab" (toggle), ⌘⌃-keys, `tabs.ungroup`, `tabs.move` out of the range. Visible side effect: the tab jumps to the bottom | n/a |
| Any new group: `AddToNewGroup` exits (`kGroupCreated`) | `:4841` | **Healed**, including Koan's own `CreateSpace` and loose-tab grouping. Upstream unfocused state is visible for one task: all groups with their headers *(live check: flicker)* | Every Space creation | n/a |
| Pin / unpin the active tab (`kUnpinActiveTab`, `kActiveTabGroupOperation`) | `:5300-5312` | **Healed** | Yes | — |
| `CloseAllTabsInGroup` (`kGroupClosed`); when the group holds every tab, adds a fallback tab **outside** the group | `:1289-1313` | **Healed**, but with two new tabs. Koan recreates the Space with a new tab *and* adopts the fallback tab into it | ⌘⌃W (`IDC_CLOSE_TAB_GROUP`) in a window without Essentials *(live check)* | — |
| Group detached / header dragged in (`kActiveTabGroupOperation` @566, `kGroupHeaderDraggedIn` @673) | `:560-578, 673` | **Healed** | Move group to window, `tabGroups.move` | — |
| Saved-group open (`TabGroupSyncDelegateDesktop::HandleOpenTabGroupRequest` exits `kGroupCreated`); `FOCUS_OR_UNFOCUS_GROUP` menu action | `tab_group_sync_delegate_desktop.cc:179-186`; `saved_tab_group_utils.cc:922-953` | **Healed.** The opened group becomes a new, unfocused Space | Only with saved groups (§1.2.5) | — |
| Editor bubble "Focus/Unfocus" | `tab_group_editor_bubble_view.cc:1236-1245` | Editor unreachable for the focused Space (header hidden); reachable for a new group during the unfocused gap (§1.2.4) | Low | — |
| Session restore / tab restore entering focus (`RestoreFocusedTabGroup` from window extra data `kFocusedTabGroupIdKey`; `SetFocusedTabGroup`) | `session_restore.cc:1246-1269, 1389`; `session_service_base.cc:766-777`; `browser.cc:640`; `browser_live_tab_context.cc:224-233, 336-344` | Upstream persists the active Space. Restoring **into an existing window** (crash Restore) ignores the restored window's key, so Koan picks the active tab's Space (PR #145 fixes the extra Space) | Yes, crash restore | `zen.workspaces.active` |
| New tabs: `Navigate` puts any non-pinned new tab into `GetFocusedGroup()` | `browser_navigator.cc:1088-1092` | Relied on (`docs/REBASE.md` row) | Links, NTP, `chrome.tabs.create` (incl. `windowId`), `window.open` as tab. `index` outside the Space with `ADD_FORCE_INDEX` *(live check: clamped into the group?)* | New tab joins the active Space |
| New tabs **not** through `Navigate`: `delegate->AddTabAt` without a group, `chrome::AddTabAt`, restored tabs with no group, tabs from another window | various | **Healed** (loose → active Space) | Yes | Same (orphans go to the active Space) |
| Popups / app windows | `IsEnabled()` needs a normal window | No Spaces there | n/a | n/a |
| Focus swipe controller → `RotateFocusedGroup` | `vertical_tab_strip_focus_swipe_controller.cc`; `tab_strip_model.cc:2323-2371` | **Hooked** (wrap) plus Koan's own swipe, which runs first (`docs/REBASE.md:200`) | — | ZenSpacesSwipe |

#### 1.2.3 Commands, shortcuts, Mac menu bar

All of these are `ActionItem`-mapped (`chrome_action_id.h:76, 92-109, 387`; items built in `browser_actions.cc:1337, 3350-3512`). None is redirected today. Mac keys come from `global_keyboard_shortcuts_mac.mm:157-166`: ⌘⌃C / P / W / X / Z, unconditional.

| Upstream item | Where | Koan today | Reachable? | Zen |
|---|---|---|---|---|
| `IDC_CREATE_NEW_TAB_GROUP` ⌘⌃P → `chrome::NewTab` (bypasses Koan's URL-bar ⌘T hook) + `CommandAddToNewGroupFromMenuItem` → `AddToNewGroup` + `OpenTabGroupEditor` | `browser_commands.cc:1834-1839`; `tab_strip_model.cc:3303-3350` | **None** | **Yes.** Creates an unnamed Space ("Space") with **Chrome's auto colour** (so the profile is tinted) and opens **Chrome's group editor bubble** anchored to a header Koan hides a moment later *(live check: where the bubble lands)* | Create Space = Ctrl+Alt+N form |
| `IDC_CLOSE_TAB_GROUP` ⌘⌃W → `CloseAllTabsInGroup` | `:1841-1848` | **None** (Healed, with two tabs, see above). No confirm | **Yes**: closes every tab of the Space, no "Delete Space?" | Delete Space asks first |
| `IDC_ADD_NEW_TAB_TO_GROUP` ⌘⌃C, `IDC_ADD_NEW_TAB_RECENT_GROUP` | `:1819-1832, 1942-1965` | None | Yes: a new NTP tab in the Space, bypassing Koan's URL-bar new-tab behaviour | ⌘T opens the URL bar |
| `IDC_FOCUS_NEXT/PREV_TAB_GROUP` ⌘⌃X / ⌘⌃Z → `FocusAdjacentTabGroupInFocusMode` (wraps, includes ephemeral groups) | `:607-641, 1850-1917`; controller `:1446-1462` | None, but equivalent to `Cycle()` | Yes; harmless (animation runs off `OnTabGroupFocusChanged`) | Ctrl+Opt+arrows / Cmd+Opt+arrows |
| `IDC_WINDOW_GROUP_TAB` / `IDC_GROUP_TARGET_TAB` (Mac Tab menu "Group Tab", ⌥ alternate) → `CommandToggleGrouped` | `main_menu_builder.mm:549-553`; `browser_commands.cc:1791-1795, 1998-2004`; `tab_strip_model.cc:3006-3034` | None | **Yes.** On a Space tab it *ungroups* (tab jumps to the Space's end). On a whole-Space selection it creates a new group plus the editor bubble | n/a |
| `IDC_GROUP_UNGROUPED_TABS` → `AddToNewGroup` + editor | `:1919-1940`; enabled by `CanGroupAllUngroupedTabs` (`browser_command_controller.cc:2877`) | None | Disabled in practice (Spaces leave no loose tabs) | n/a |
| `IDC_SAVED_TAB_GROUPS_MENU` / `kActionTabGroupsMenu`, `kActionSavedTabGroupsSubmenu` | `browser_command_controller.cc:1983`; `chrome_action_id.h:387, 411` | None | App menu "Tab groups", only in `Mode::kNone` (Chromium toolbar), see vertical-tabs audit A10 | n/a |
| Menu-only actions `kActionToggleTabGroupFocus`, `kActionTabGroupPin`, `kActionTabGroupDelete`, `kActionTabGroupOpenInBrowser/NewWindow` | `chrome_action_id.h:94-98` | None | Saved-group menus only | — |
| `IDC_MOVE_TAB_NEXT/PREVIOUS` at a group boundary changes membership instead of moving | `tab_strip_model.cc:4495-4515` | Healed (tab re-added at the end) | Yes, ⌃⇧PgUp/PgDn at the Space's edge: the tab jumps to the bottom | Zen moves within the Space |

#### 1.2.4 Tab context menu, group header, editor bubble, colours

| Upstream item | Where | Koan today | Reachable? | Zen |
|---|---|---|---|---|
| "Focus/Unfocus group" (+ New badge), "Add tab to group" submenu, "Add tab to new group", "Remove from group" | `tab_menu_model.cc:413-491` | **Redirected:** removed and replaced by "Move To" (`koan_move_to_space.cc:94`). Not added for Essentials | No (via the menu). `ExecuteContextMenuCommand(…, CommandToggleGrouped / AddToNewGroup*)` stays reachable through the commands in §1.2.3 | "Move to Space" submenu |
| "Move to another window" (`ExistingWindowSubMenuModel`) / "Move tab to new window" | `tab_menu_model.cc:493-525`; `browser_commands.cc:437-460 (MaybePreserveFocusedGroupInTarget), 482-…, 1672-1710` | None | Yes. A **whole Space** selected moves as a group: it becomes a Space in the target, gone from the source, and focus is preserved only if it is the target's only group. One tab → the target's active Space, or a **new default "Space"** in a new window | "Move to New Window" moves the tab; the new window has the synced Space list |
| Group header context menu / `TabGroupEditorBubbleView` (name, colour, Ungroup, Close group, Delete, Save/Unsave, Move group to new window, Share) | `tab_group_header_view.cc:528, 769`; `tab_strip_collection_controller.cc:503-519` | **Hidden** for the focused Space (header visible=false, patch) | Through `OpenTabGroupEditor` (`tab_strip_model.cc:2400`, `kEditorOpened` → `root_tab_collection_node.cc:245`): ⌘⌃P, "Group Tab" on a full selection, `GroupAllUngroupedTabs`. **Yes** during the unfocused gap, for a just-created group | n/a |
| Collapse / expand (`ToggleTabGroupCollapsedState`; collapsed groups hide their tabs) | `tab_strip_collection_controller.cc:378`; `tab_group_view.cc:278, 341-361`; `tab_group_view_vertical_layout.cc:146` | **None.** `ReadSpace` ignores `is_collapsed`. Only a Koan write clears it | **Old profile:** Chrome's collapsed groups restore collapsed (session visual data). **Extension:** `tabGroups.update({collapsed:true})` (`tab_groups_api.cc:241-277`). Either way the Space shows **no tabs**, because its header (the only expander) is hidden *(live check)* | Folders collapse; Spaces don't |
| Group colours `TabGroupColorId` (9) = Space theme. Grey = Zen default | `koan_workspaces.cc:101` `kDefaultThemeColor`; `TintSeed` | Owned for Koan-created Spaces | A **Chrome-created** group (old profile, extension, ⌘⌃P) has an auto colour, so it is tinted at once | Gradient themes (#41) |

#### 1.2.5 Saved / synced / shared groups

| Upstream item | Where | Koan today | Reachable? | Zen |
|---|---|---|---|---|
| Auto-save of every new group (`SavedTabGroupModelListener::OnTabGroupAdded` → `AddGroup`) | `saved_tab_group_model_listener.cc:59-85` | **Hooked off** | No | Firefox "Closed Groups" (saved) exist, but groups are disabled by default |
| Session-restore save / connect (`ProcessSavedGroup`) | `session_restore.cc:1311-1335` | **Hooked off** (also stops saved-URL navigation, #59) | No | — |
| Reopen-tab save (`AddRestoredTab` `SaveGroup`) | `browser_live_tab_context.cc:405-412` | **Hooked off** | No | — |
| Saved groups that **already exist** in `TabGroupSyncService` (copied Chrome profile's `Sync Data`, or remote groups if sync works) and their UI: saved-groups bar in the bookmark bar (`bookmark_bar.show_tab_groups`, default **true**, `components/bookmarks/browser/bookmark_utils.cc:467`), app menu "Tab groups", everything menu | `bookmark_bar_controller.cc:88`; `saved_tab_group_utils.cc:900-960` | None | **Old profile:** ⌘⇧B shows the bar, and opening a saved group (`HandleOpenTabGroupRequest`, `tab_group_sync_delegate_desktop.cc:148-200`) adds a new Space **connected to sync**. Its renames then sync; closing its last tab or Koan's Delete Space shows Chrome's **"Delete group?" dialog** (`browser_tab_strip_model_delegate.cc:398-422`, `saved_tab_group_utils.cc:343-390`); a remote or bar "Delete group" closes the Space's tabs. After a restart the connection is dropped (`ProcessSavedGroup` skipped) *(live check: sync needs API keys)* | — |
| Saved-group prefs: `tabgroup.sync_enabled`, `auto_open_synced_tab_groups` (false), `auto_pin_new_tab_groups`, `stop_showing_tab_group_confirmation_on_*`, `saved_tab_groups.*` | `components/saved_tab_groups/public/pref_names.{h,cc}:20-56`; settings allowlist `prefs_util.cc:282, 300` | None | Inert while nothing is saved | — |
| Shared tab groups (data sharing, editor "Share", `IDC_SEND_SHARED_TAB_GROUP_FEEDBACK`) | `browser_command_controller.cc:1075` | None | Needs saved groups plus Google data sharing, so effectively no *(live check)* | — |

#### 1.2.6 Session restore, tab restore, copied profiles

| Upstream item | Where | Koan today | Reachable? | Zen |
|---|---|---|---|---|
| Group metadata restore (`RestoreTabGroupMetadata` → `ChangeTabGroupVisuals` incl. `is_collapsed`) | `session_restore.cc:1271-1309` | Spaces = restored groups; Koan session hooks only carry `koan.*` extra data (`koan_session_data.cc`) | Yes | `zen-sessions.jsonlz4` |
| **Spaces only exist in the session.** Startup "Open the New Tab page / specific pages" (`session_startup_pref.cc.patch` changes only the default) | `session_startup_pref.cc` | None | **User:** chrome://settings/onStartup → every relaunch starts with one default Space; names, icons and themes are gone. **Old profile:** a copied Chrome profile keeps its own startup choice | Spaces are stored independently of tab restore *(live check)* |
| Copied Chrome profile with several groups, collapsed groups, ungrouped tabs | session + `EnsureOneSpaceShown` | Each group becomes a Space (title split into emoji + name, colour = tint). Ungrouped tabs join the focused / first Space. No focus key → active tab's group | **Yes.** Collapsed → empty-looking Spaces (§1.2.4). Coloured groups tint the browser. Many small groups → many Spaces | Zen has an import path but no group mapping |
| Cmd+Shift+T of a tab whose Space is gone → `AddRestoredTab` → `AddToGroupForRestore` with the old id **and old visuals** | `browser_live_tab_context.cc:358-412`; `browser_tabrestore.cc:156-194` | None | **Yes:** reopening a tab from a **deleted** Space brings the Space back ("cannot be undone" in Koan's dialog is untrue). If the Space now lives in another window (moved), the same group id could exist in two windows *(live check)* | Zen restores into the original Space if it exists |
| Reopen a closed window (`BrowserLiveTabContext::Create` + `SetFocusedTabGroup`) | `:336-344, 581-597` | Works (focus key restored) | — | — |
| Crash restore into the existing window | `session_restore.cc:879-885` | PR #145 | Yes | — |

#### 1.2.7 Drag

| Upstream item | Where | Koan today | Reachable? | Zen |
|---|---|---|---|---|
| Drag within the Space past its last tab or into the pinned area | `tab_drag_handler.cc:698, 792` | **Healed** (loose → end of the Space; pinned → Essential; an emptied Space recreated) | Yes | Same |
| Drag to another window | drag controller | Joins the target's Space by drop position, or loose → active Space | Yes | Joins the target window's active Space |
| Tear-off to a new window | drag controller → `CreateNewBrowser` | Single tab → new default "Space" (`EnsureOneSpaceShown`, `:1126-1143`). Whole group only via header drag (header hidden) | Yes; new window gets a fresh Space, not the source's | New window gets the synced Space list |
| Group header drag (and `kGroupHeaderDraggedIn`) | `tab_strip_model.cc:673` | Hidden header, so not draggable; `tabGroups.move` reaches the same code | Extension only | — |

#### 1.2.8 Extensions (`chrome.tabGroups`, `chrome.tabs`)

| API | Where | Effect on Spaces today | Verdict |
|---|---|---|---|
| `tabGroups.update({title,color})` | `tab_groups_api.cc:241-277` | Renames or re-themes the Space (title parsed as emoji + name); tint follows | Fine (an extension "Spaces API") |
| `tabGroups.update({collapsed:true})` | same | Space shows no tabs (header hidden) | **Leak** (G3) |
| `tabGroups.move({windowId})` | `:354-470` | Space moves to the other window; the source window loses it (not recreated) and gets a default Space if it was the last one | Acceptable; document |
| `tabs.group({tabIds})` (new group) / `({groupId})` | `tabs_api.cc:2105` | New group = **new Space** with Chrome's auto colour (tint). Pinned tabs (Essentials) are unpinned into it *(live check)* | Leak-lite: "group by domain" extensions create Spaces (G12) |
| `tabs.ungroup` | `:2252` | Tabs re-added to the focused Space; an emptied Space comes back empty | Healed |
| `tabs.move` (out of the range, `windowId`), `windows.create({tabId})` | — | Healed, or new default Space | OK |
| `tabs.highlight` / `tabs.update({active})` across Spaces | `:1265` | Exits focus mode → Koan switches to that Space; the multi-selection is trimmed to the Space | Intended |
| `tabs.create({windowId,index})` | navigator | Focused group of that window *(live check: index clamp)* | OK |
| `tabGroups.query({collapsed, shared})` | `:175-212` | Lists Spaces as groups | Harmless |

#### 1.2.9 IPH, promos, badges

| Item | Where | Koan today | Reachable? |
|---|---|---|---|
| `kIPHTabGroupsUnfocusingFeature` (toast promo, `MaybeShowFeaturePromo` on **every** focus entry), anchored to `kUnfocusTabGroupButtonElementId` | `tab_group_focus_iph_controller.cc:46-71`; `browser_user_education_service.cc:2203-2215` | None | Anchor hidden, so probably never shown *(live check)* |
| `kIPHTabGroupsFocusingFeature` (snooze, >10 tabs, no focused group, 3 s) on `kTabGroupHeaderElementId` | `:74-96`; `:2191-2201`; `ui_features.cc:564-569` | None | Only in the unfocused gap; checked at fire time, so no |
| `kTabGroupsFocusing` New badge on the tab menu item | `tab_menu_model.cc:428-431` | Item removed | No |
| `kIPHTabGroupsSaveV2IntroFeature` / `…CloseGroupFeature` (app menu) | `browser_user_education_service.cc:1604-1630` | None | Only for saved groups / Chromium toolbar |
| `kTabGroupFocusExited` toast | `tab_strip_model.cc:2301` | **Hooked off** | No |

---

### 1.3 Changes (one Koan layer: `KoanWorkspaceController` owns the group state)

Rule set:
1. A Spaces window is **always** focused, and upstream focus-mode *side effects* are Koan's choice.
2. No upstream group UI (editor, header, group commands) runs in a Spaces window.
3. Every group a window gets is normalised into a Space: expanded, and with the default theme unless Koan or an extension set one.
4. Spaces survive any startup setting.

| # | Change | Hook | New patch? |
|---|---|---|---|
| G1 | **Focus-mode colour:** in Spaces windows, `UpdateFocusModeTheme` sets `koan::SpaceColorOverride(model, group)` instead of the group dialog colour. Recommended value: Koan's `TintSeed` for a coloured Space and **nullopt for the default theme**, so the user's colour and extension themes come back (`browser_widget.cc:325-360`). This is a *per-window* tint, so `UpdateTint`'s profile-wide colour juggling (`kWorkspaceTintApplied/SavedColor/SavedVariant`, `DecidesTint`) can be deleted, and two windows on different Spaces finally show different tints (Zen's per-window gradient). One `koan::` call at `tab_strip_collection_controller.cc:625` | existing `tab_strip_collection_controller.cc.patch` (+ `tabs/common` dep already present) | no |
| G2 | **Focus-mode freezing off:** `TabView::UpdateFocusFreezing` returns after `ReleaseFreezingVote(kFocusedGroup)` when `koan::ShowsSpaceHeader(this)`. Koan's infinite-tabs policy then stays the only freezer, as `PERFORMANCE.md` says. `FeatureOverrides` cannot set the `kTabGroupsFocusFreezing` param (no params API, `feature_overrides.h:27-30`) | existing `tab_view.cc.patch` (1 line in `UpdateFocusFreezing`, `tab_view.cc:470`) | no |
| G3 | **No collapsed Spaces:** in `EnsureOneSpaceShown`, any Space whose `visual_data()->is_collapsed()` is set gets `ChangeTabGroupVisuals(…, collapsed=false)`. This covers old profiles, extensions and any future collapse entry point. Also keep `is_collapsed=false` explicit in `WriteSpace` | `src/` | no |
| G4 | **Re-point group actions per window** (with `kUseActionsForBrowserCommands` forced on, shared with vertical-tabs A4): `kActionCreateNewTabGroup` → `ShowCreateForm()`; `kActionCloseTabGroup` → `DeleteSpace(active)` (asks first, as Zen does); `kActionFocusNext/PrevTabGroup` → `Cycle()`; `kActionAddNewTabToGroup`, `kActionAddNewTabRecentGroup` → Koan's New Tab (URL bar); `kActionWindowGroupTab`, `kActionGroupTargetTab`, `kActionGroupUngroupedTabs` → no-op, `SetVisible(false)`, disabled; `kActionTabGroupsMenu` / `kActionSavedTabGroupsSubmenu` → hidden | `src/` (as `koan_url_bar_controller.cc:274`) | no |
| G5 | **Context-menu commands swallowed:** `ExecuteKoanTabMenuCommand` returns true (no-op) for `CommandToggleGrouped`, `CommandAddToNewGroup`, `CommandAddToNewGroupFromMenuItem`, `CommandRemoveFromGroup`, `CommandToggleFocusGroup` in Spaces windows. This is the backstop for the legacy command switch and direct callers. `IsKoanTabMenuCommandEnabled` already sees them first | existing `tab_strip_model.cc.patch` hook (`ExecuteContextMenuCommand`), `src/` only | no |
| G6 | **Editor bubble never opens for a Space window's group:** handled by G4 and G5. In addition, `KoanWorkspaceController::OnTabGroupChanged(kEditorOpened)` closes any `TabGroupEditorBubbleView` widget for that group (found by anchor) | `src/` | no |
| G7 | **Mac menu bar Tab > "Group Tab" / "Group Target Tab"** hidden after upstream validation | extend `browser_native_widget_mac.mm.patch` (same hunk as vertical-tabs A6) | no (existing) |
| G8 | **Saved-group surfaces off:** `SetDefaultPrefValue(bookmark_bar.show_tab_groups, false)` (`RegisterLocalStatePrefDefaults` is local state, so this one goes in Koan's profile registration hook); G4 hides the app-menu entry. Leave the user's saved data alone. Optionally, `ProcessSavedGroup`'s skip also calls `service->DisconnectLocalTabGroup` so a Space opened from a saved group stops syncing *(live check whether sync is reachable at all)* | `src/` (`koan_prefs.cc` via the `browser_prefs.cc` hook) | no |
| G9 | **Spaces outlive the session:** persist the Space list (id, title, colour, order, active) as window extra data `koan.spaces` (the `koan_session_data.cc` window store; `kWindowRestoreHooks` is empty and waiting) **and** a profile pref `koan.workspaces.list` for windows opened without a restore. On a fresh window with no restored groups, `EnsureOneSpaceShown` recreates the stored Spaces, each with one new tab | `src/` | no |
| G10 | **Tab restore of a deleted Space:** on `TabGroupChange::kCreated` for a group id that Koan deleted this session (`deleting_` history), or that exists in another Koan window, add its tabs to the active Space and drop the visuals. That way Delete Space is final and group ids stay unique per profile *(decide against Zen live first)* | `src/` | no |
| G11 | **IPH off:** `DisableFeature` for `feature_engagement::kIPHTabGroupsFocusingFeature`, `kIPHTabGroupsUnfocusingFeature`, `kIPHTabGroupsSaveV2IntroFeature`, `kIPHTabGroupsSaveV2CloseGroupFeature` | `koan_feature_overrides.cc` | no |
| G12 | **Foreign groups are normalised:** a group created outside Koan (`kCreated`/`kNewGroupCreated` that is not from `NewSpaceGroup`) gets the default theme (grey) unless its colour is set again afterwards. An extension's `tabs.group` then adds a Space but does not recolour the browser. Optionally a toast: "Extension X created a Space" | `src/` | no |
| G13 | **Flag lock:** document that `chrome://flags/#tab-groups-focusing` Disabled disables Spaces (overrides can't beat flags). Hide that row together with `#tab-group-ribbon` and `#focus-selection` when Koan hides other upstream flag rows (needs an `about_flags.cc` or flags-UI patch; defer) | — | deferred (would be new) |
| G14 | **Leak detector (DCHECK):** `DLOG(FATAL)` if, at the end of `Update()`, a Spaces window is unfocused, a Space is collapsed, the widget has a user-colour override Koan didn't ask for, or a `TabView` holds a `kFocusedGroup` freezing vote | `src/` | no |

**Cost.** About 1.5-2.5 agent-days, one Mac build:
- 2 existing patches gain 1-2 lines each (G1, G2), plus 1 existing Mac patch hunk (G7);
- about 300-450 lines in `src/` (G9 is half of that);
- about 80 lines of tint bookkeeping deleted (G1).

**Rebase watch:** add rows for `UpdateFocusModeTheme` and `UpdateFocusFreezing` to `docs/REBASE.md`. `kTabGroupsFocusing` is a launch candidate upstream, so recheck its params and IPH on every roll.

### 1.4 Tests (Mac loop; DCHECK build so G14 fires)

Each test asserts four things:
- the window is focused on a Space;
- every unpinned tab is in a Space;
- no Space is collapsed;
- no `TabGroupEditorBubbleView` widget exists, and `BrowserWidget::user_color_override()` equals G1's value.

1. Seeded profile copied from Chrome with 3 groups (one collapsed, blue/red/grey) plus 4 ungrouped tabs. Relaunch: 3 Spaces, all expanded; ungrouped tabs are in the active one; tint per G1/G12.
2. Same profile with startup "Open the New Tab page". Relaunch: the Spaces (names, icons, themes) are still there (G9).
3. A Chrome Web Store theme installed: the default-theme Space shows it; a blue Space shows Koan's tint only (G1). Two windows on differently coloured Spaces differ.
4. Open a chat page in Space A, switch to B, wait 10 s: chrome://discards shows A's tab not frozen while in the 5-MRU set (G2).
5. ⌘⌃P, ⌘⌃W, ⌘⌃C, ⌘⌃X, ⌘⌃Z, Tab menu "Group Tab" (and the ⌥ alternate): Create form / Delete-Space dialog / Koan new tab / Space cycle / nothing. No editor bubble, no extra NTPs (G4).
6. `--disable-features=UseActionsForBrowserCommands`: repeat 5. G5 swallows; ⌘⌃W still asks (or no-op).
7. Right-click a tab, an Essential, a multi-selection across a split: no group items, "Move To" only.
8. Extension: `tabGroups.update(space,{collapsed:true})` → Space expands on the next task. `tabs.group({tabIds:[a,b]})` → new grey Space. `tabs.ungroup` → tabs stay in the active Space. `tabGroups.move(space,{windowId})` → the source keeps ≥1 Space. `tabs.highlight` across Spaces → switches Space, no toast.
9. Drag a tab past the Space's end, into Essentials, to another window, and tear it off: invariant holds; tear-off window has one Space.
10. Delete Space B (confirm), then Cmd+Shift+T: per G10's decision. Close the last tab of a Space, then Cmd+Shift+T: the tab returns to that Space.
11. Move a whole Space to another window via "Move to another window", reopen a closed tab from it in the first window: no duplicate group id across windows.
12. ⌘⇧B with saved groups in `Sync Data`: no saved-groups bar (G8). Opening one via the everything menu (if still reachable) gives a Space, and closing it shows Koan's dialog only.
13. Crash with 2 Spaces → Restore: exactly the restored Spaces (PR #145), the active one restored.
14. chrome://flags `tab-groups-focusing` Disabled: documented behaviour (Spaces off, no crash).
15. 20 rapid Space switches with a wheel and a swipe, then close the window mid-animation: no crash. The IPH "Unfocus" toast never shows (G11).

### 1.5 Top leaks in this area (ranked)

1. **High: upstream's focus-mode colour override and theme suppression are always on** (`tab_strip_collection_controller.cc:625-647`, `browser_widget.cc:325-360`). Every Spaces window takes the Space's group dialog colour (grey for the default theme), so the user's colour never shows, Chrome Web Store themes are suppressed in every window, and Koan's profile tint fights a per-window override it doesn't know exists. **Fix:** G1, which also makes the tint per window and deletes the bookkeeping.
2. **High: focus-mode freezing freezes every other Space's tabs immediately** (`kTabGroupsFocusFreezing` default true, `tab_view.cc:470-490`). Background work in other Spaces stops, which silently contradicts Koan's documented "5 MRU stay running" policy. Unlike Zen, a Space switch changes page behaviour. **Fix:** G2 (1 line, existing patch).
3. **High *(live check)*: collapsed groups produce Spaces that show no tabs.** A copied Chrome profile or `tabGroups.update({collapsed})` sets the flag, and the only expander (the group header) is hidden. **Fix:** G3 (`src/`).
4. **Medium: unredirected group shortcuts and the Mac Tab menu** (⌘⌃P/W/C/X/Z, "Group Tab"). They create unnamed, Chrome-coloured Spaces with Chrome's editor bubble, close a Space's tabs without asking (leaving two NTPs), and ungroup tabs, which then jump to the Space's end. **Fix:** G4, G5, G7.
5. **Medium: Spaces exist only in the session file.** "Open the New Tab page" at startup (one click in chrome://settings, or inherited from a copied profile) erases every Space on relaunch. **Fix:** G9.
6. **Medium-low: pre-existing saved or synced groups** (copied profile). They open as sync-connected Spaces with Chrome's "Delete group?" dialogs, and a remote or bar deletion closes a Space's tabs. **Fix:** G8.
7. **Low-medium: extensions create Spaces at will** (`tabs.group`), and recolour the browser through Chrome's auto colour. Reopening a tab from a deleted Space resurrects the Space. **Fix:** G12, G10.
8. **Low: `chrome://flags/#tab-groups-focusing` Disabled turns Spaces off entirely** (overrides can't beat flags). **Fix:** G13 (documented and deferred).

### 1.6 As implemented (Spaces lockdown PR, branch `fix/spaces-lockdown`)

Where the PR differs from 1.3, the reason is here; 1.3 stays as the plan.

| # | Status | As built, and why it differs |
|---|---|---|
| G1 | Done | Hook `koan::ApplySpaceColor(model_)` at the top of `TabStripCollectionController::UpdateFocusModeTheme` (pristine `:625`), in the existing patch. `KoanWorkspaceController::GetSpaceColor()`: `TintSeed` for a coloured Space, nullopt for the default theme or with `koan.workspaces.tint` off; between Spaces (upstream's one-task unfocused gap) the Space shown last, so the window doesn't flash the user's colour. Koan also sets it on focus/visuals changes and in every `Update()`: that heals the **horizontal** strip's `BrowserTabStripController::UpdateFocusModeTheme()` (no hook; reachable until vertical-tabs A1 locks the mode) without a new patch. `UpdateTint`, `DecidesTint` and the profile-wide colour writes are gone; `kWorkspaceTintApplied/SavedColor/SavedVariant` stay registered only for a one-time migration (`MigrateWorkspaceTint()`: a profile still showing Koan's tint gets the saved colour back, then the prefs are cleared; remove after 2027-03). Difference from before: the tint's scheme variant is the profile's (tonal spot by default) instead of a forced tonal spot. The e2e tests read the per-window colour through `KoanSpaceBarView`'s `WindowTint` UI DevTools property. |
| G2 | Done | Hook in `TabView::UpdateFocusFreezing` (pristine `:470`): `koan::KeepsOtherSpacesRunning(tab)` releases `kFocusedGroup` and returns. Decided by the **tab's window** (`TabInterface::GetBrowserWindowInterface()`), not `ShowsSpaceHeader(this)`: `OnTabStateChanged()` calls it before the view has a widget, when `ShowsSpaceHeader()` would say no and leave a vote. The horizontal strip's `Tab` votes (`BrowserTabStripController::UpdateTabFocusFreezing`) are not hooked (A1). |
| G3 | Done | `NormalizeSpaces()` at the start of `EnsureOneSpaceShown()`; `WriteSpace()` writes `is_collapsed=false`. Upstream's collapsed-group freezing votes come only from `ToggleTabGroupCollapsedState` (the hidden header), so none to release. |
| G4 | Done | `InstallGroupActions()` per window, `kUseActionsForBrowserCommands` forced on. Also `SetEnabled(false)` on the hidden ones (the command controller may re-enable them; the no-op callback is the guard). *(live check)* whether the legacy app menu's "Tab groups" placeholder honours the hidden `kActionTabGroupsMenu` (only in `Mode::kNone`, A10). `koan_shortcut_table.cc` untouched. |
| G5 | Done | `ExecuteSpacesCommand()` in `koan_tab_menu.cc` also swallows `CommandAddToExistingGroup` (a no-op upstream; Koan's "Move To" submenu reuses that id). Correction to 1.3: `IsKoanTabMenuCommandEnabled` only sees Koan's own id range, not these. |
| G6 | Done | `CloseGroupEditors()` in every `Update()` (which `kEditorOpened` schedules), by `TabGroupEditorBubbleView::kTabGroupEditorBubbleViewId` in the window's element context, rather than by anchor. |
| G7 | Done | One hunk after both switches of `ValidateUserInterfaceItem`: `koan::HidesMenuCommand(browser_view_, tag)` hides and disables `IDC_WINDOW_GROUP_TAB` / `IDC_GROUP_TARGET_TAB` in a Spaces window and shows them in any other (the menu bar is shared, and AppKit keeps a hidden item hidden until told otherwise, `browser_window_command_handler.mm`). A6 and F3 can add their ids to the same table. |
| G8 | Done, differently | `bookmark_bar.show_tab_groups` is registered by `BookmarkModelFactory` **after** Koan's profile prefs hook (`tab_strip_prefs.cc`), so `SetDefaultPrefValue` there would DCHECK. Koan changes the default at runtime instead (`PrefService::SetDefaultPrefValue`, when a Spaces window is added to its widget); a user's explicit choice still wins. The optional `DisconnectLocalTabGroup` is not done (sync needs API keys; *live check*). |
| G9 | Done, narrower | Only the profile pref `koan.workspaces.list` (`[{title, color, active}]`), written from the window the user is in. No `koan.spaces` window extra data: when a session is restored its groups already carry the Spaces, and the case G9 fixes restores no session, so the window store adds nothing. No group ids (a recreated Space gets a new id, so tab restore and G10 never see a duplicate). Recreated in a window that opens **with no Spaces while the profile has no other Spaces window**, not during a session restore and not after a crash (the user may still pick Restore into that window; the stored list is also not overwritten after a crash until the window's Spaces change). That covers startup "New Tab page" / "specific pages", a session with no window, and macOS's new window after the last one closed. A window opened beside another (Cmd+N, tear-off, Move to new window) keeps starting with one Space (1.4 test 9; Zen's window sync is #75). |
| G10 | Done | Small and safe, and it is Zen's behaviour (a tab reopens in its Space if that exists, else in the one shown), so no live decision was needed. Every `kCreated` group that isn't `kInsertedFromAnotherTabstrip` is checked in the next `Update()`: an id Koan deleted this run (`DeletedSpaces()`, filled when a `deleting_` group closes) or one that also exists in another window of the profile has its tabs added to the Space shown last (or kept as a new default Space if the window has no other). Koan's own empty-Space recreation keeps its id and is untouched. |
| G11 | Done | As planned. |
| G12 | Done | "Created outside Koan" = `TabStripModelObserver::OnTabGroupAdded` (fired only by `AddToNewGroup` and ephemeral promotion, never by restore) outside `NewSpaceGroup()`. Normalised in the next `Update()` to the default theme (and "Space" for an empty title) unless the creator changed the visuals first (`kVisualsChanged` not from `WriteSpace()`). No toast. |
| G13 | Deferred | Documented in `docs/ZEN_PARITY.md` (Spaces row): `#tab-groups-focusing` Disabled turns Spaces off; hiding the row needs a flags-UI patch. |
| G14 | Done | `CheckSpaces()` at the end of `Update()`, `#if DCHECK_IS_ON()`, one `DCHECK` listing every leak. Skipped unless the window is at rest: no further update due (this one changed the model), not destroying / closed / delete scheduled, not `closing_all`, no Space being deleted, no tab drag (`BrowserWidget::tab_drag_kind()`), no session restore, at least one Space; popups and Incognito have no `Update()`. The colour is compared after Koan re-applied it (so the horizontal strip doesn't fire); freezing is checked on vertical `TabView`s only. |

**By-hand tests on the Mac (DCHECK build so G14 fires; extends 1.4).** Each step also checks: one Space shown, no loose tab, no collapsed Space, no "Edit group" bubble, and (DCHECK build) no `Spaces:` DCHECK in the log.

1. *G1, one window.* Default Space: the sidebar/toolbar use your own colour (Settings > Appearance colour, or the system accent in Zen's look). Edit Theme > Blue: the window turns blue; chrome://settings/appearance still shows your colour. Back to Default: your colour returns. Settings > Spaces tint off/on: the blue goes and comes back at once.
2. *G1, two windows.* Window A on the blue Space, ⌘N for window B (default Space): A stays blue, B shows your colour; click between them: neither changes.
3. *G1, theme.* Install a Chrome Web Store theme (e.g. "Just Black"): a default-theme Space shows it; a blue Space shows blue, no theme images; back to Default, the theme returns.
4. *G1, upgrade.* On a profile used with the old build while a coloured Space was active: after updating, a default Space shows the user's own colour (not the old Space colour).
5. *G2.* Space A: open a page that counts (e.g. https://www.timeanddate.com/stopwatch/ running, or a chat). Switch to Space B, wait 15 s, open chrome://discards in B: A's tab is not "frozen" (Lifecycle "active"/"passive"); back in A the stopwatch didn't pause.
6. *G3.* Seed: in Google Chrome make two tab groups, collapse one, quit; copy that user data dir and launch Koan with `--user-data-dir=<copy>` ("Continue where you left off"): both groups are Spaces and the collapsed one shows its tabs when you switch to it. Extension (see 13): `chrome.tabGroups.update(id, {collapsed: true})` on the active Space: its tabs reappear within a moment.
7. *G4.* ⌘⌃P: the Create-a-Space form opens (no unnamed Space, no "Edit group" bubble). ⌘⌃W: "Delete Space?" (No keeps it). ⌘⌃X / ⌘⌃Z: next / previous Space, wrapping. ⌘⌃C: the URL bar opens as for ⌘T, and Escape leaves no new tab.
8. *G5.* Launch with `--disable-features=UseActionsForBrowserCommands` and repeat 7: ⌘⌃P adds at most one tab to the current Space (no group, no bubble); Tab > Group Tab (if shown) does nothing.
9. *G6.* With 8's flag, any path that would open Chrome's group editor: no bubble stays open.
10. *G7.* Tab menu: no "Group Tab" (also with ⌥ held: no "Group Target Tab"). In a popup window (window.open with features) the Tab menu is unchanged.
11. *G8.* Seed: copy a Chrome profile that has saved tab groups (the "Sync Data" folder). Turn on Settings > bookmarks bar: no saved-groups chips on it; chrome://settings shows "Show tab groups in bookmarks bar" off unless you turned it on.
12. *G9.* chrome://settings/onStartup > "Open the New Tab page". Have 3 Spaces (one with an emoji, one blue), quit (⌘Q), relaunch: the same 3 Spaces in order, names, emoji and colours, each with a new tab, the one you were in shown. Then "Open a specific page": that page opens in the Space you were in. Close the last window (⌘W on the last tab, app stays in the Dock), click the Dock icon: the new window has the Spaces. With "Continue where you left off": unchanged behaviour (the session's Spaces, no duplicates). After a crash (kill -9) relaunch: one Space; Restore brings the Spaces, no duplicates.
13. *G10.* Space B with 2 tabs; close one tab; Delete Space B (Yes); ⌘⇧T: the tab opens in the Space you're in, B does not come back. Close a Space's last tab (the Space keeps a new tab), ⌘⇧T: the tab returns to that Space.
14. *G11.* 20 Space switches, then wait 10 s: no "Unfocus"/"Focus tab group" promo bubble anywhere.
15. *G12.* Extension: load an unpacked extension with `"permissions": ["tabs", "tabGroups"]` and a background service worker; from its DevTools console: `chrome.tabs.query({currentWindow:true}, t => chrome.tabs.group({tabIds:[t.at(-1).id]}))`: a new Space appears, grey (default theme), the window's colour doesn't change. Then `chrome.tabs.group({tabIds:[...]}, id => chrome.tabGroups.update(id, {color:'red', title:'Ext'}))`: that Space stays red, named Ext.
16. *G13.* chrome://flags/#tab-groups-focusing Disabled, relaunch: Spaces off (groups shown as Chrome groups), no crash; re-enable.
17. *G14 transients.* With the DCHECK build: 20 rapid Space switches with the wheel and a swipe, close the window mid-animation; drag a tab between Spaces and to another window; tear one off; open a popup and an Incognito window; Delete a Space with a page that asks "Leave site?" and Cancel: no `Spaces:` DCHECK.


---

## 2. Omnibox and New Tab Page in Koan: audit


**Reachability baseline.** Koan builds with `disable_fieldtrial_testing_config = true` (`build/koan-mac.gn:27`). An unbranded build also fetches no variations seed unless `--variations-server-url` is passed (`components/variations/service/variations_service.cc:284-300`). So there is **no Finch**: every feature runs at its `BASE_FEATURE` default plus Koan's overrides. chrome://flags and command-line switches can still change them. Koan ships no Google API keys (`koan_branding.h:13`), so Google sign-in, sync and account-gated features (Glic, the NTP's Drive/Calendar modules) are effectively off. Google **search** features that need only cookies still work: AI Mode eligibility, remote suggest, doodles, AI Mode on the NTP.

**Summary.** Koan's floating URL bar is **upstream's real `LocationBarView` and `OmniboxViewViews`**. Koan lifts the browser's top container out of the hidden toolbar and places it over the sidebar pill ("in pill") or centres it as Zen's card ("floating"). Koan owns *where the input sits*, the new-tab session, Esc, the placeholder and page-action hiding. It owns **nothing about the dropdown**. In M158 the dropdown in normal windows is upstream's **WebUI popup** (`kWebUIOmniboxPopup` ENABLED, `omnibox_next_features.cc:49`), a separate widget sized to `location_bar_->BoundsInScreen()`. Three findings follow:

1. Koan's row-styling hooks (`koan_omnibox_rows.*`, 3 patches from #97) style the *Views* popup, and that popup is never created for a normal window. They are dead code.
2. Every Google element of the WebUI popup still shows: AI Mode chip/button, AIM composebox popup, "add context", entity thumbnails, remote trending suggestions.
3. When the input sits in the pill, the dropdown hangs under the pill **inside the sidebar**. Cmd+L opens the input *in the pill* in the default `floating-on-type`. Zen floats it: in Zen only a mouse press keeps it in place. Cmd+L also starts zero-suggest right away (user-initiated focus). That combination is the user's "Cmd+L on the NTP shows the dropdown in the sidebar". The NTP makes it worse: upstream already gives the omnibox default focus there, so the bar is already open in the pill when Cmd+L arrives.

Draft #96 fixes a lot of this but has not been merged: the WebUI popup off, AIM/Lens/Glic off, IPH off, Cmd+L floats, no default NTP focus, remote ZPS off, Koan's empty NTP. Recommendation: split #96. Land the **omnibox lockdown** (O1-O8) first as a small PR. It is almost all feature overrides and `src/`, and it needs no NTP work.

---

### 2.1 How Koan's URL bar works (the model)

- **Reuse, not replace.** `KoanUrlBarController` (`src/browser/ui/views/koan_url_bar_controller.{h,cc}`, one per window, owned by `KoanBrowserViewController`) moves `BrowserView::top_container()`. It does not move the location bar alone. While the bar is open it hides every toolbar sibling of the location bar (`SetPlacement` :496, `HideToolbarSiblings` :592). It then resizes the top container to the card and shifts the toolbar so the location bar fills it (`Layout` :693, `CardBounds` :760), masking the corners on the top container's layer. In the floating look it adds a backdrop (click outside closes, plus Zen's shadow), a fill and a decoration (magnifier and divider). The omnibox, its edit model, providers, popup, keyword mode and page actions are all upstream's.
- **When it opens.** Only through focus. `KoanCompactModeController` forwards focus changes to `OnFocusChanged` (:376). If the focused view is inside the location bar, `SetPlacement(DesiredPlacement())`. `DesiredPlacement` (:468) works like this:
  - `kNone` (the bar stays in Chromium's toolbar) when `!hides_toolbar()`. That is the multiple-toolbars layout / `Mode::kNone` and macOS fullscreen.
  - `kFloating` for `float`, compact with a hidden sidebar, no pill, or once it has already floated.
  - `kFloating` for `floating-on-type` once **user input is in progress** or for a new-tab session.
  - `kInPill` otherwise.

  The open paths are:
  - **Cmd+L** goes through `kActionFocusLocation` → `chrome::FocusLocationBar` (`browser_actions.cc:3600`; legacy `browser_command_controller.cc:1150`). The pill gets it unless typing has started.
  - **Click on the pill / magnifier** goes through `KoanSidebarNavView::FocusOmnibox()` (`koan_sidebar_nav_view.cc:440`) → `FocusLocation(true)`. It also lands in the pill.
  - **Cmd+T, File > New Tab, "+ New Tab" row** go through `kActionNewTab` (callback re-pointed per window, `InstallNewTabAction` :266) and the legacy `IDC_NEW_TAB` hook (`browser_command_controller.cc.patch`) → `OpenForNewTab` (:299). That path floats, with the 45 s text memory and Cmd+T toggling the bar closed.
- **Where the dropdown renders.** It is upstream's popup widget. `LocationBarView::Init` (`location_bar_view.cc:354-403`) builds `OmniboxPopupViewWebUI` for any browser that `CanHostWebUIOmnibox` accepts (`:192-201`: every normal/popup-type window except web apps and DevTools). The presenter sizes the widget from `location_bar_->BoundsInScreen()` plus `content_height_` (`omnibox_popup_presenter_base.cc:361-398`). It re-syncs on `LocationBarView::OnVisibleBoundsChanged` (`location_bar_view.cc:1682-1686`). So the dropdown follows Koan's placement: pill-wide under the pill, card-wide under the card. Koan's own `scripts/e2e/palette.mjs:103-107` reads rows from `chrome://omnibox-popup`, which confirms live that the WebUI popup is what users see.
- **Intercepted entry points (develop):** `kActionNewTab` / `IDC_NEW_TAB` (redirect), `OnAutocompleteAccept` disposition (`chrome_omnibox_client.cc.patch`), placeholder text/colour/visibility (`omnibox_view_views.cc.patch`), page-action hiding while open (`location_bar_view.cc.patch` :1297 and `OnChanged`), Koan's command-palette provider (`autocomplete_controller.cc.patch` → `AddAutocompleteProviders`), and row look (3 Views-popup patches, dead, see P1). Nothing else is intercepted: no feature overrides for omnibox/NTP/AI, no pref defaults.

---

### 2.2 Inventory

Legend for **Koan**: *Owned* means Koan decides. *Redirected* means the entry point runs Koan's code. *Hidden* means the UI is not shown but the code path is live. *Hooked* means a patch changes the numbers but the behaviour is upstream's. *None* means upstream behaviour leaks through. *#96* means the draft handles it but it is not on develop.

#### 2.2.1 Popup implementation and placement

| Upstream item | Where (pristine) | Koan today | Reachable by user / old profile / extension? | Zen 1.24b |
|---|---|---|---|---|
| **P1** `OmniboxPopupViewWebUI` (dropdown-only WebUI, `chrome://omnibox-popup.top-chrome`). `kWebUIOmniboxPopup` **ENABLED** | `omnibox_next_features.cc:49`; chosen `location_bar_view.cc:368-377` | **None.** Koan's row hooks (`koan_omnibox_rows.cc`, patches to `omnibox_result_view.cc` :346, `omnibox_match_cell_view.cc` :564, `omnibox_text_view.cc` :199/215) only reach `OmniboxPopupViewViews`, which is built only when `CanHostWebUIOmnibox` is false (`:399-403`). So the 49 pt rows, radius 8, 14 pt/500 text and `kColorKoanOmniboxRowTitle` are **never applied**. The card shows Chromium's WebUI list in Google's styling. #105's glass blurs only the top-container layer, not this separate widget *(live check)* | **Yes, always**: every Cmd+L, Cmd+T and pill click | Firefox `UrlbarView` inside the same breakout element (one surface). Rows styled by `zen-omnibox.css` |
| **P2** Views popup `OmniboxPopupViewViews` | `location_bar_view.cc:399-403` | **Hooked** (rows) but unused in normal windows | Only if the user disables `#omnibox-webui-popup`-style flags *(flag name: live check)* | n/a |
| **P3** AIM popup (`kWebUIOmniboxAimPopup` ENABLED, `:53`): composebox covering the location bar, `OmniboxPopupState::kAim`; `OmniboxPopupFileSelector` | `location_bar_view.cc:354-366, 405-408`; `omnibox_edit_model.cc:805,851` (`OpenAiMode`) | **None** (#96 disables) | **Yes** when AIM-eligible (Google DSE + server says yes; #151's notes show users saw the AI Mode hint). Opened by the AI Mode button/chip, `@aimode`, or keyboard activation. Covers the location bar, so in the pill it is a narrow composebox **inside the sidebar** *(live check)* | none |
| **P4** `kWebUIOmniboxSimplification` ENABLED: different on-page UI state, "Add context" button below results (`kAddContextButtonVariant` default `kBelowResults`, `kHideClassicContextButton` false) | `omnibox_next_features.cc:57, 84-97`; load-time data `omnibox_popup_ui.cc:248-255` | **None** (#96 disables) | **Yes** with AIM eligibility (context button reads `IsAimPopupEnabled`, `:393`) *(live check: exact gate in TS)* | Zen: no context button (scotchBonnet off) |
| **P5** Full WebUI omnibox / in-BrowserView popup (`kWebUIOmniboxFullPopup` DISABLED, `OmniboxPopupViewFullWebUI`, `OmniboxPopupViewBrowserView`) | `:63`; `location_bar_view.cc:378-387` | None | Only via chrome://flags. It would replace the input row, so Koan's card would host a WebUI input *(would break: the card styling assumes `OmniboxViewViews`)* | n/a |
| **P6** Popup **placement under the pill** (Cmd+L) | `omnibox_view_views.cc:597` (`is_user_initiated` → `StartZeroSuggestRequest`); presenter `:375` | **Owned, but different from Zen.** `floating-on-type` keeps keyboard focus in the pill, and zero-suggest opens the dropdown pill-wide, under the pill, over the sidebar. #96 adds `edit_in_pill_` (only the pill click stays in the pill) | **Yes, every Cmd+L** (reported). On an NTP the bar is already in the pill (P7), so Cmd+L does not even change focus | `UrlbarInputBase-mjs.patch:291`: floats when `float`, or `floating-on-type && !focusedViaMousedown`. Cmd+L floats (`live-results-2026-11-04.md:42`). Only a mouse click on the pill stays in the sidebar (`test_Click_Shoudnt_FLoat_Urlbar`) |
| **P7** Default omnibox focus on NTP (`ShouldFocusLocationBarByDefault`) | `browser_web_contents_delegate.cc:922-950` (`chrome://newtab` host) | **None** (#96 adds `SuppressesDefaultUrlBarFocus`) | **Yes**: every NTP that becomes active (new window, tab-menu "New tab below", `chrome.tabs.create({})`, restore). The bar opens in the pill without user action | Zen never shows an NTP. `open-on-startup` opens the *floating* Cmd+T session on the empty tab |
| **P8** Invisible "fakebox" focus `search::FocusOmnibox(OMNIBOX_FOCUS_INVISIBLE)` from embeddedSearch NTPs | `search/omnibox_utils.cc:70`, `search_tab_helper.cc:303`, `omnibox_view_views.cc:572` | None. Koan's `OnFocusChanged` opens the bar on any focus, so an *invisible* focus lifts a visible bar into the pill | Third-party DSE NTPs with `new_tab_url` and embeddedSearch, extension NTPs *(live check)* | n/a |
| **P9** `Mode::kNone` (Chromium toolbar shown: Ctrl+Alt+B layout, user-collapsed rail, fullscreen) | `DesiredPlacement` :471 | **Intended upstream**: the bar stays in the toolbar with Chromium's dropdown, placeholder ("Search Google or type a URL" / AI hint) and all page actions | Yes. Vertical-tabs audit A10 shrinks it to the real multi-toolbar layout | Zen floats in both layouts (the patch at :291 has no single-toolbar check) |
| **P10** Vertical strip force-collapses while the popup is open | `vertical_tab_strip_region_view.cc:1219-1225` (`kForceCollapse` lock) | **Worked around**: compact with a hidden sidebar always floats (`pr58-review-round2.md` S6) | Hover-peeked sidebar + pill click → floats (deliberate) | n/a |

#### 2.2.2 AI Mode / Lens / Gemini / Glic

| Upstream item | Where | Koan today | Reachable? | Zen 1.24b |
|---|---|---|---|---|
| **A1** AIM master switch `omnibox::kAimEnabled` ENABLED; eligibility = feature + policy pref `omnibox.ai_mode_settings` (0 = allowed) + Google DSE + server response | `aim_eligibility_service_features.cc:11`; `aim_eligibility_service.cc:260, 440-478` | **None** (#96: `DisableFeature(kAimEnabled)`) | **Yes** with Google DSE (default unless the welcome choice changed it) | none (`browser.ml.*` all false, `prefs/firefox/ai.yaml`) |
| **A2** AI Mode page action in the location bar (`kActionAiMode`); pref `omnibox.show_ai_mode_omnibox_button` | `ai_mode_page_action_controller.cc:186`; `omnibox_view_views.cc:616-662` | **Hidden** while Koan's bar is open (`HidesPageActionIcons`); shown in `Mode::kNone` | `Mode::kNone`: **yes**. Single toolbar: no | none |
| **A3** AI Mode placeholder "Press Tab…"/hint | `omnibox_placeholder_util.cc`, `omnibox_view_views.cc:729-735` | **Hooked** (#151): "Search..." while the bar is open; upstream text in `Mode::kNone` | `Mode::kNone` only | `Search...` |
| **A4** Cmd+Enter → `OpenAiMode` (`kShowRhsAimHint`, param of DISABLED `kDynamicAimSubmit`) | `omnibox_view_views.cc:2185-2196`; `omnibox_features.cc:188-191` | None | Off by default; chrome://flags only | Cmd+Enter: Firefox's default (canonize) |
| **A5** Starter pack `@aimode` (AIM-gated), **`@gemini`** (gemini.google.com, not AIM-gated), `@tabs`, `@bookmarks`, `@history`, `@page` (`kStarterPackExpansion` enabled) | `template_url_starter_pack_data.cc:25-110`; `featured_search_provider.cc:71-92` | None | **Yes**: typing `@` lists them (relevance 1455-1460). They are in chrome://settings/searchEngines | Firefox search modes `@bookmarks @tabs @history @actions` (Zen keeps them; `browser.urlbar.shortcuts.workspaces`); no Gemini |
| **A6** Contextual chips / "Ask Google" row in the popup (`kAskGShowChip`), entity/composebox rich images | `omnibox_popup_view_webui.cc:82-90`; `omnibox_popup_ui.cc:203-246` | None | With AIM eligibility *(live check)* | none |
| **A7** Lens overlay `kLensOverlay` ENABLED (Google DSE + RAM); entry points: page context menu "Search with Google Lens" (`IDC_CONTENT_CONTEXT_LENS_OVERLAY`), app menu, image "Search image with Google"; `kLensOverlayOmniboxEntryPoint` DISABLED (no omnibox button) | `lens_features.cc:23, 68`; `lens_overlay_entry_point_controller.cc:89-123` | None (#96 disables both) | **Yes**: page right-click (context-menu audit). Whether the overlay works without API keys *(live check)* | none |
| **A8** Glic / Gemini in Chrome (`kGlic` ENABLED on Mac, `kGlicRollout` ENABLED, `IDC_CONTENT_CONTEXT_GLIC*`, Glic button, hotkey) | `chrome_features.cc:528, 865` | None (#96 disables) | Needs a signed-in, capable account. **Not reachable without API keys** *(live check: the button stays hidden)* | none |
| **A9** Contextual tasks side panel (`contextual_tasks::kContextualTasks`) | components/contextual_tasks | None (#96 disables) | *(live check)* | none |
| **A10** Omnibox Everywhere (`kOmniboxEverywhere` DISABLED; FRE `kOmniboxEverywhereFre` ENABLED but gated by it) | `omnibox_next_features.cc:110-127` | None | No (flag only) | none |

#### 2.2.3 Suggestions content

| Upstream item | Where | Koan today | Reachable? | Zen 1.24b |
|---|---|---|---|---|
| **S1** Zero-suggest on focus (`ZeroSuggestProvider`): NTP (local history + remote "trending"), web pages, SRP | `zero_suggest_provider.cc:~297` (`ResultTypeForInput`); triggers `omnibox_view_views.cc:597, 1515, 1621, 1710` | None (#96: `DisablesRemoteZeroSuggest` hook + its own `koan_zero_suggest.cc` top sites) | **Yes**, every user-initiated focus | Empty input shows open tabs + **top sites** (`browser.urlbar.suggest.topsites` locked true); trending off (`browser.urlbar.trending.featureGate` false) |
| **S2** Remote search suggestions pref `search.suggest_enabled` (default true) | `pref_names.h:383` | None | Yes (default on); chrome://settings/syncSetup "Improve search suggestions" | **Off** (`browser.search.suggest.enabled: false`, `urlbar.yaml`) |
| **S3** Entity suggestions with images, rich answers | WebUI popup | None | Yes with S2 on | none (suggest off) |
| **S4** Rich autocompletion `kRichAutocompletion` ENABLED | `omnibox_features.cc:179` | None | Yes | Firefox autofill (similar). OK |
| **S5** Pedals / actions in suggest ("Clear browsing data" → Chromium's dialog, "Manage passwords", …) | `components/omnibox/browser/actions/` | None. Koan's palette (`>`) covers Zen's actions alongside | Yes (type "clear history") | Zen quick actions (`zen.urlbar.suggestions.quick-actions`) |
| **S6** History clusters / Journeys action chip | `history_clusters` omnibox action | None | *(live check: Journeys is mostly off upstream)* | none |
| **S7** Keyword mode / tab-to-search (site search, `is_keyword_hint`) | `omnibox_view_views.cc:1740-1757` | None. #96 makes Tab on an *empty* input type `>` (Zen's commands mode) | Yes. Keyword placeholder respected (`AdjustOmniboxPlaceholder` :944) | Firefox search mode; Tab on an empty input = commands mode |
| **S8** "Switch to tab" (`kOpenTab` / `kSwitchToTab`), any window, any Space | `chrome_omnibox_client.cc` `OnAutocompleteAccept` | **Owned (indirect)**: upstream leaves group focus mode and Koan switches to that tab's Space (`koan_workspaces.cc:1151-1160`). Inside a **new-tab session** develop forces `kNewForegroundTab`, which turns a switch-to-tab row into a duplicate tab. #96 keeps `kSwitchToTab` | **Yes**: Cmd+T, type an open site, pick "Switch to this tab" *(live check: duplicate)* | Switch-to-tab switches Space (Zen) |
| **S9** Clipboard suggestion (`ClipboardProvider`) | components/omnibox | None | Yes | `browser.urlbar.clipboard.featureGate: true`. OK |
| **S10** Omnibox IPH (`kIPHStarterPack…`, AIM/Lens IPH, "omnibox everywhere" promo) | feature_engagement | None (#96 disables a list) | *(live check)* | no IPH |
| **S11** Popup header / "Most visited" tile row, query tiles | WebUI popup | None | Android-only upstream for tiles *(live check on desktop NTP)* | top sites row |

#### 2.2.4 NTP

| Upstream item | Where | Koan today | Reachable? | Zen 1.24b |
|---|---|---|---|---|
| **N1** What `chrome://newtab` resolves to: `chrome://new-tab-page` (Google DSE) / `chrome://new-tab-page-third-party` / DSE `new_tab_url` | `search/search.cc:170-226` | None on develop (#96 hooks `ForProfile` → `chrome://koan-newtab`) | **Yes**: Google's WebUI NTP for most users | about:newtab, practically never shown (empty tab plus floating bar) |
| **N2** How an NTP appears despite replace-newtab | `chrome::NewTab` callers | **Redirected** for `kActionNewTab` / `IDC_NEW_TAB` only | **Yes**: tab context menu "New tab below/right" (`CommandNewTabToRight`, `tab_menu_model.cc:136,336`), Cmd+N / startup / Dock New Window, `chrome.tabs.create({})`, extension `chrome_url_overrides`, typing `chrome://newtab`, `OpenForNewTab` returning false (fullscreen with Chromium's toolbar, empty strip, pref off) | Cmd+T never opens a tab (`_canReplaceNewTab`); new window = empty tab + floating bar (`open-on-startup`) |
| **N3** Realbox (NTP search box) + **NTP composebox / AI Mode** (`kNtpComposebox` ENABLED), NTP Next AI chips (`kNtpNextFeatures` ENABLED), starter chip | `composebox_fieldtrial.cc:168`; `ntp_features.cc:276-282` | None | **Yes** on Google NTP (AIM-eligible) | none |
| **N4** Customize Chrome side panel: NTP button **and auto-open promo** (`kNtpCustomizeChromeAutoOpen` ENABLED) | `ntp_features.cc:106`; `new_tab_page_feature_promo_helper.cc:210-225` | None | **Yes**: an upstream side panel opens on its own on a fresh profile's NTP. The string says "Customize Chrome" (branding) *(live check)* | Zen theme picker; no NTP panel |
| **N5** Shortcuts / most-visited tiles (`kNtpShortcuts` ENABLED), modules (sign-in gated), doodles (`kNtpAnimatedDoodles`), middle-slot promos | `ntp_features.cc:219, 322` | None | Tiles + doodles: **yes**. Modules: mostly not without sign-in *(live check)* | topsites feed off (`newtab.yaml`) |
| **N6** URL pill on an NTP | `koan_sidebar_nav_view.cc:74` | **Owned** (#151): "Search..." + magnifier | n/a | blank pill on the empty tab |
| **N7** Koan's empty NTP (#96) | `src/browser/koan_new_tab_page.cc`, `koan_new_tab_ui.cc` | **Draft, not merged** | n/a | the transparent empty tab |

#### 2.2.5 Search engine and choice screen

| Upstream item | Where | Koan today | Reachable? | Zen 1.24b |
|---|---|---|---|---|
| **E1** Search engine choice dialog (`SearchEngineChoiceDialogService`, EEA region) | `search_engine_choice_service.cc:576-640`; `search_engine_choice_tab_helper.cc:59-64`; `choice_screen_in_chrome = !(android||fuchsia)` (`buildflags.gni:11`) | **None.** Koan's welcome sets the engine with `SetUserSelectedDefaultSearchProviderByPrepopulateId(id)` with the default `ChoiceMadeLocation::kOther` (`koan_welcome.cc:1121`, `template_url_service.h:428-431`), and **`kOther` does not record the choice** (`template_url_service.cc:1388-1393`) | **Yes for EEA users**: Chromium's choice dialog after Koan's welcome already asked *(live check in an EEA locale)* | Firefox's own (no EEA dialog in Zen) |
| **E2** Default search provider: Google (prepopulated) | `TemplateURLService` | **Owned** at first run (welcome page) | Settings > Search engine stays upstream | DDG/Google selectable |

#### 2.2.6 Location bar decorations (URL pill and toolbar)

| Upstream item | Where | Koan today | Reachable? | Zen 1.24b |
|---|---|---|---|---|
| **D1** Page actions (star, translate, zoom, PWA install, "Open in app" intent picker, cookies, passwords, sharing, AI Mode) + content setting icons | `location_bar_view.cc:1297` (`ShouldHidePageActionIcons`) | **Hidden** while the bar is open (patch); the toolbar is hidden otherwise in single toolbar | `Mode::kNone`: **all visible**. Single toolbar: bubbles that anchor to these icons (password save, translate, zoom on Cmd+±, install) anchor to a hidden view *(live check: where they appear)* | `show-protections-icon` false, `show-pip-button` false; no page actions in the pill |
| **D2** Location icon → page info bubble | `LocationIconView` | **Redirected** in the pill: the site button opens Chromium's page info (`koan_sidebar_nav_view.cc:285, 457`); in the card it is covered by the magnifier decoration | `Mode::kNone`: upstream | Zen site-data panel (`ZenSiteDataPanel`) |
| **D3** Keyword hint chip / selected keyword chip | `LocationBarView` | None (upstream chip inside the card) | Yes | search-mode chip |
| **D4** IPH bubbles anchored to the omnibox (tab-search, AIM, Lens, "Chrome tips") | user education | None | *(live check)*; #96 disables a list | none |

---

### 2.3 Changes

| # | Change | Hook | New patch? |
|---|---|---|---|
| O1 | **Use the Views popup Koan styles.** `DisableFeature` `omnibox::internal::kWebUIOmniboxPopup`, `kWebUIOmniboxAimPopup`, `kWebUIOmniboxSimplification`, `kWebUIOmniboxFullPopup` (as #96 does). Then the 3 row patches do something. Update `scripts/e2e/palette.mjs` `rows()`, which reads `chrome://omnibox-popup`: read the Views rows through the accessibility tree instead. Note upstream's comment that `OmniboxPopupViewViews` will be deprecated (`location_bar_view.cc:395-398`): re-check every roll and list it in `docs/REBASE.md`. Alternative if Views goes away: style the WebUI popup through `chrome://omnibox-popup` CSS (data source override, a bigger patch) | `koan_feature_overrides.cc` | no |
| O2 | **Cmd+L floats, the pill click doesn't** (Zen `!focusedViaMousedown`). Take #96's `edit_in_pill_` alone: `KoanSidebarNavView::FocusOmnibox()` calls `url_bar()->NoteEditInPill()`, `DesiredPlacement` floats for `floating-on-type` unless `edit_in_pill_`, `EndEditing` clears it. Also re-point `kActionFocusLocation` per window (as `InstallNewTabAction` does for `kActionNewTab`). If the omnibox already has focus in the pill (NTP), call `SetPlacement(DesiredPlacement())` after `FocusLocationBar`, because no focus change arrives | `src/` only | no |
| O3 | **No default NTP focus** in single toolbar with replace-newtab (#96 `SuppressesDefaultUrlBarFocus`). Without it, every NTP opens the pill bar by itself (P7) | 1 hunk in `browser_web_contents_delegate.cc` (existing patch file) | no (existing) |
| O4 | **Ignore invisible focus**: in `OnFocusChanged`, skip opening when `edit_model()->focus_state() == OMNIBOX_FOCUS_INVISIBLE` (P8) | `src/` | no |
| O5 | **AI off**: `DisableFeature` `omnibox::kAimEnabled` (covers P3/P4/A2/A3/A5's `@aimode`/A6/N3's AIM), `lens::features::kLensOverlay`, `kLensOverlayOmniboxEntryPoint`, `::features::kGlic`, `kGlicRollout`, `contextual_tasks::kContextualTasks`, `ntp_features::kNtpNextFeatures`, `kNtpComposebox`, `optimization_guide::features::kOptimizationGuideModelExecution` (#96 has most of these) | `koan_feature_overrides.cc` | no |
| O6 | **`@gemini` and `@aimode` gone**: on `TemplateURLService` load, `SetIsActiveTemplateURL(false)` for the starter-pack entries with `StarterPackId::kGemini/kAiMode`. Keep `@tabs/@bookmarks/@history` (Zen has them). Re-run on `OnTemplateURLServiceChanged` so a settings toggle can't revive them, or accept the user re-enabling them | `src/` (profile keyed observer) | no |
| O7 | **Zen's suggestion defaults**: `search.suggest_enabled` default **false** (Zen `browser.search.suggest.enabled: false`), `omnibox.show_ai_mode_omnibox_button` default false, `omnibox.ai_mode_settings` default 1 (belt and braces with O5). These are profile prefs that upstream registers, so Koan needs a `SetDefaultPrefValue` pass **after** `RegisterProfilePrefs` in `browser_prefs.cc` (today Koan's profile prefs register from `tab_strip_prefs.cc.patch`, too early for prefs registered later) | 1 line in the existing `browser_prefs.cc.patch` + `koan_prefs.cc` | no (existing) |
| O8 | **Remote zero-suggest off** (no trending), top sites instead: #96's `zero_suggest_provider.cc` hook + `koan_zero_suggest.cc`. With O7's `search.suggest_enabled=false`, upstream already drops remote ZPS (`BaseSearchProvider::CanSendSuggestRequest` returns false without `SearchSuggestEnabled()`, `base_search_provider.cc:416`), so the **new patch can be skipped** and only Koan's local top-sites provider kept *(live check that ZPS stays local with the pref off)* | `src/` (provider) | yes in #96 → **avoidable** via O7 |
| O9 | **Switch-to-tab in a new-tab session**: keep `kSwitchToTab` for `kOpenTab` matches (#96's `MaybeAdjustOmniboxDisposition(browser, is_open_tab_match, …)`) | existing `chrome_omnibox_client.cc.patch` (signature change) | no |
| O10 | **Search engine choice**: the welcome passes `search_engines::ChoiceMadeLocation::kChoiceScreen` (or calls `SearchEngineChoiceService::RecordChoiceMade`) when the user confirms the engine page, so EEA users aren't asked twice. If welcome is skipped (Esc), leave upstream's dialog: it is the legal one | `src/` (`koan_welcome.cc`) | no |
| O11 | **NTP**: land #96's empty NTP (`search.cc` hook, `chrome://koan-newtab`). Until then: `DisableFeature` `kNtpCustomizeChromeAutoOpen` (no surprise side panel) and IPH | #96: 1 new patch (`search.cc`); interim: overrides | #96 yes |
| O12 | **NTP entry points that bypass replace-newtab** (N2): route tab-menu `CommandNewTabToRight` through `MaybeOpenUrlBarForNewTab` in Koan's tab-menu hook (Zen: the tab menu has "New Tab" → floating), and open the floating bar on a **new window**'s first NTP (Zen `zen.urlbar.open-on-startup`) instead of the pill. Leave `chrome.tabs.create({})` and typed `chrome://newtab` on the NTP (#96 page) | existing `tab_strip_model.cc.patch` (`ExecuteKoanTabMenuCommand`) + `src/` | no |
| O13 | **IPH off** for omnibox/Lens/AIM/Glic/NTP promos (#96's list + `kIPHStarterPack…` if present) | `koan_feature_overrides.cc` | no |
| O14 | **`Mode::kNone` floats too** (Zen floats in both layouts): drop the `!hides_toolbar()` → `kNone` early return for the floating placement only (keep `kNone` in fullscreen until the toolbar reveal is sorted), and apply `HidesPageActionIcons` for the card there. Defer until vertical-tabs audit A10 settles what `kNone` means | `src/` | no |
| O15 | **Leak detector** (DCHECK builds): `CHECK` that `LocationBarView::omnibox_popup_view()` is not an `OmniboxPopupViewWebUI` in a Koan window, and log when `OmniboxPopupState::kAim` is entered. After a roll, upstream re-enabling a popup flavour fails the Mac loop | `src/` | no |

**Cost.** O1-O10 + O13: about 1-1.5 agent-days. All of it is overrides, `src/`, or a line or two in existing patches. No new patch file (O8 avoided via O7). One Mac run. O11/O12 come with #96 (1 new patch). **Rebase burden:** the WebUI/Views popup split is upstream's moving target. O1 depends on `OmniboxPopupViewViews` surviving, and O15 makes its removal loud.

---

### 2.4 Tests (Mac loop; e2e under `scripts/e2e/`)

1. **Popup flavour:** in a normal window, the omnibox popup is `OmniboxPopupViewViews` (O15 check); no `chrome://omnibox-popup` target in `/json`. Rows are 49 pt with radius 8 in the Zen card, and Chromium's defaults in an app/popup window.
2. **Cmd+L** on a web page, on the NTP, on the empty state, with the sidebar visible and in compact mode with the sidebar hidden: the card floats centred at Zen geometry (`cmd-t-cmd-l-session.md` §2.3), the dropdown sits under the card, and **no popup bounds intersect the sidebar**. Repeat Cmd+L while the bar is already in the pill (NTP default focus): it moves to the card.
3. **Pill click**: edits in the pill; typing floats (floating-on-type); `normal` keeps it in the pill and widens it.
4. **NTP open paths** (new window, tab-menu "New tab below", `chrome.tabs.create({})` from a test extension, Cmd+Shift+T of an NTP): the omnibox has no focus and the bar is closed (O3), or the floating bar opens for window start (O12).
5. **AI off:** type `@` → no `@gemini`/`@aimode` rows; `ai` + Tab → no AIM composebox; Cmd+Enter → no AI Mode navigation; no AI Mode page action in `Mode::kNone`; no "Search with Google Lens" in the page context menu; no Glic button. The NTP has no AI Mode button or composebox (or is Koan's page).
6. **Suggestions:** the empty input shows top sites / open tabs, no trending (network log: no `complete/search` request on focus with `search.suggest_enabled` at its default).
7. **Switch to tab across Spaces:** Space A tab `example.com`; in Space B Cmd+T, type `example`, pick "Switch to tab" → Space A is shown on that tab, no duplicate, tab count unchanged.
8. **Search engine choice:** EEA profile (`--search-engine-choice-country=DE`), pick DuckDuckGo in the welcome → no Chromium choice dialog on the first navigation; skip the welcome (Esc) → upstream dialog shows.
9. **Fakebox focus:** a page calling embeddedSearch focus (a third-party DSE NTP fixture) → bar does not open (O4).
10. **Customize Chrome:** fresh profile, open any NTP 5 times → no side panel opens by itself.
11. **Mode::kNone** (Ctrl+Alt+B): Cmd+L shows the card (after O14), or the toolbar dropdown with Koan's placeholder and no AI Mode button (before O14).
12. **Flags regression:** `--enable-features=WebUIOmniboxPopup` → O15 fires in DCHECK builds; release builds still work.

---

### 2.5 Top leaks (ranked)

1. **HIGH: the dropdown is upstream's WebUI popup, unstyled and AI-laden (P1, P3, P4, A6).** It shows on every use of the bar. Koan's row styling from #97 never runs, so the Zen card sits on a Google-styled list. The fix is one block of feature overrides (O1).
2. **HIGH: Cmd+L opens in the pill and the dropdown drops into the sidebar (P6 + P7).** This is the user's report. Upstream gives the NTP default focus, which puts the bar in the pill before the user does anything, and Koan treats keyboard focus like a mouse press. Fix: O2 + O3 (`src/` + an existing patch).
3. **HIGH: AI Mode / AIM composebox / `@gemini` / Lens (A1-A7, N3).** They are live for Google-DSE users without any flag, and Zen ships none of them. Fix: O5 + O6.
4. **MEDIUM: Google's NTP is still what any non-Cmd+T path opens (N1-N5)**, including the auto-opening "Customize Chrome" panel. #96 is a draft. Interim fix: the O11 overrides. Proper fix: land #96 split.
5. **MEDIUM: remote trending suggestions on by default (S1, S2), where Zen ships `browser.search.suggest.enabled=false`.** This is also a privacy default. Fix: O7.
6. **MEDIUM: EEA search-engine choice asked twice (E1)**, because Koan's welcome records `kOther`. Fix: O10 *(live check)*.
7. **LOW-MEDIUM: switch-to-tab inside a Cmd+T session opens a duplicate (S8).** Fix: O9.
8. **LOW: `Mode::kNone` is plain Chromium** (P9, A2, D1): dropdown, placeholder, every page action. It is mostly a layout decision; tracked in O14 and vertical-tabs audit A10.
9. **LOW / live check:** invisible fakebox focus opening the pill (P8), page-action bubbles anchored to the hidden toolbar (D1), Glic without keys (A8).

#### Appendix: key references (pristine 158.0.8127.2)

- Popup choice: `chrome/browser/ui/views/location_bar/location_bar_view.cc:192-201, 354-408, 1297, 1682-1686`; features `chrome/browser/ui/omnibox/omnibox_next_features.cc:49-63, 84-97, 336, 393-415`; presenter `chrome/browser/ui/views/omnibox/omnibox_popup_presenter_base.cc:95-110, 361-398`; `omnibox_popup_view_webui.cc:72-140`; WebUI data `chrome/browser/ui/webui/omnibox_popup/omnibox_popup_ui.cc:158-270`.
- Focus/ZPS: `omnibox_view_views.cc:572-600, 1505-1518, 1700-1712, 1740-1757, 2185-2196`; `omnibox_edit_model.cc:1239-1300, 805, 851`; `browser_web_contents_delegate.cc:922-950`; `search/omnibox_utils.cc:70`; actions `browser_actions.cc:3600`, `browser_command_controller.cc:1150`.
- AI: `components/omnibox/browser/aim_eligibility_service_features.cc:11-35`, `aim_eligibility_service.cc:255-478`, `omnibox_pref_names.h:30,74,78`, `ai_mode_page_action_controller.cc:186`, `components/lens/lens_features.cc:23,68`, `lens_overlay_entry_point_controller.cc:89-123`, `chrome/common/chrome_features.cc:528,865`, `template_url_starter_pack_data.cc`, `featured_search_provider.cc:71-92`.
- NTP: `chrome/browser/search/search.cc:170-226`, `components/search/ntp_features.cc:106,219,276-282,322`, `new_tab_page/composebox/variations/composebox_fieldtrial.cc:168`, `new_tab_page_feature_promo_helper.cc:210-225`.
- Choice screen: `components/search_engines/search_engine_choice/search_engine_choice_service.cc:576-640`, `template_url_service.cc:1388-1420`, `template_url_service.h:428-431`, `search_engine_choice_tab_helper.cc:59-64`.
- Koan: `src/browser/ui/views/koan_url_bar_controller.cc` (`InstallNewTabAction` 266, `OpenForNewTab` 299, `OnFocusChanged` 376, `DesiredPlacement` 468, `SetPlacement` 496, `Layout` 693, `CardBounds` 760, hooks 884-972), `koan_url_bar.h`, `koan_omnibox_rows.{h,cc}`, `koan_sidebar_nav_view.cc:74,431-445`, `koan_welcome.cc:1095-1125`, `koan_workspaces.cc:1148-1160`, `koan_feature_overrides.cc`, `koan_prefs.cc`; patches listed in §2.1; draft `gh pr diff 96` (`koan_feature_overrides.cc`, `koan_url_bar_controller.cc` `edit_in_pill_`, `zero_suggest_provider.cc.patch`, `search.cc.patch`, `browser_web_contents_delegate.cc.patch`).
- Zen 1.24b: `src/browser/components/urlbar/content/UrlbarInputBase-mjs.patch:289-297`, `src/zen/urlbar/ZenUrlbarController.mjs:84-150`, `src/zen/common/modules/ZenVerticalTabsManager.mjs:754-759`, `prefs/zen/zen-urlbar.yaml`, `prefs/firefox/urlbar.yaml` (suggest off, trending off, quicksuggest locked off, topsites locked on), `prefs/firefox/ai.yaml`, `prefs/firefox/newtab.yaml`, `.agent/notes/zen-behaviour/cmd-t-cmd-l-session.md`, `live-results-2026-11-04.md:38-47`, `close-last-tab-and-empty-tab.md`.


---

## 3. Fullscreen and immersive mode on macOS


**Summary.** Koan's normal windows use upstream's immersive fullscreen as is. Koan bends it in two places:

- While Koan hides its toolbar, it forces Chromium's toolbar style to `TOOLBAR_NONE`. This is one hook in `BrowserFrameViewMac::UpdateFullscreenTopUI`.
- It takes the top container back from Chromium's overlay widget into Koan's own overlay. This needs no patch: Koan is an `ImmersiveModeController::Observer` registered after BrowserView.

The design holds for browser fullscreen. The biggest remaining gap is that **Koan has no idea about content (tab/HTML5) fullscreen**:

- Koan's content card, compact hot zones, edge monitor and toolbar reveal all stay live over a fullscreen video.
- The cause: upstream keeps immersive mode *enabled* in content fullscreen, so `ShouldDrawVerticalTabStrip()` stays true and nothing in Koan's predicates changes.

Teardown: #152 fixes the reported crash (#153), and with `destroying_` it covers every close path I could trace. It is still a guard on symptoms, not a rule. Fold it into the single teardown owner proposed in the vertical-tabs audit (A13).

---

### 3.1 How upstream's Mac fullscreen works in 158, and where Koan sits

#### Upstream

**Which controller.** `CreateImmersiveModeController` (`immersive_mode_controller_factory_views.cc:26`) builds `ImmersiveModeControllerMac` whenever `WindowFeatureController::UsesImmersiveFullscreenMode()` is true (`window_feature_controller.cc:63-68`): tabbed windows and PWAs. Popups get `ImmersiveModeControllerStub`.

- There is **no feature flag** for this any more. `kImmersiveFullscreen`, `kImmersiveFullscreenTabs` and `kFullscreenAnimateTabs` do not exist in 158 (`git grep BASE_FEATURE` finds none).
- `separate_tab_strip` is true for tabbed windows. With vertical tabs, though, `SetEnabled` skips the tab overlay (`immersive_mode_controller_mac.mm:104-106`) and `tab_native_widget_id_` stays 0. The bridge then builds the **plain `ImmersiveModeControllerCocoa`**, not the tabbed one (`native_widget_ns_window_bridge.mm:1248-1258`).

**Entering.** The sequence is:

1. AppKit transition start.
2. `BrowserNativeWidgetMac::OnWindowFullscreenTransitionStart` (:357).
3. `BrowserView::FullscreenStateChanging` (:2267, returns early if `browser_widget_->IsClosed()`).
4. `ProcessFullscreen`.
5. `BrowserFrameViewMac::OnFullscreenStateChanged` (:142), which calls `ImmersiveModeController::SetEnabled(IsFullscreen())` and then `UpdateFullscreenTopUI()` (:166-170).

**`SetEnabled(true)`** (`immersive_mode_controller_mac.mm:98-200`) does the following, in order:

- `MoveChildren(browser→overlay)`, skipped in content fullscreen (:159). This moves the find bar, constrained (tab-modal) dialogs, the Glic and omnibox popup widgets, and bubbles anchored in top chrome (`ShouldMoveChild` :430-485).
- `overlay_widget()->Show()`.
- `OnImmersiveModeToolbarRevealChanged(true)`.
- Observers `OnImmersiveFullscreenEntered` (:173). BrowserView is the first observer: it takes `vertical_tabs_enable_state_lock_` and runs `ReparentTopContainerForStartOfImmersive` (`browser_view.cc:6024-6038`, :3800-3826).
- `EnableImmersiveFullscreen` over mojo.
- A focus listener: focus inside the top container takes `focus_lock_`, a reveal lock (:347-358).

**`UpdateFullscreenTopUI`** (`browser_frame_view_mac.mm:250-300`) maps the toolbar style to the Cocoa side:

| Condition | `FullscreenToolbarStyle` | Cocoa style |
|---|---|---|
| Content fullscreen | `TOOLBAR_NONE` | `kNone` |
| `kShowFullscreenToolbar` on | `TOOLBAR_PRESENT` | `kAlways` |
| Otherwise | `TOOLBAR_HIDDEN` | `kAutohide` |

- Every switch *to* `kNone` calls `OnContentFullscreenChanged(true)`, which moves the children back to the browser widget (:276-284, :327-345).
- On the Cocoa side, `kNone` means `SetIgnoreRevealLocks(true)` (`immersive_mode_controller_cocoa.mm:414-418`) and `IsContentFullscreen()==true` (:621). The overlay window is moved **off screen** (`LayoutWindowWithAnchorView` :705-711, `GetOffscreenYOrigin` :625 uses `screen.safeAreaInsets.top`). The thin title-bar controller stays visible while `revealAmount==0` (:645-676).

**Exit and close.** `SetEnabled(false)` notifies `OnImmersiveFullscreenExited` **before** `MoveChildren(overlay→browser)` (:213-222). `BrowserDidClose` → `SetEnabled(false)` (:268), subscribed in `Init` (:93). `TearDownPreBrowserWindowDestruction` resets the controller (`browser_window_features.cc:1272`).

**Notch.** There is no notch-specific code in Chromium, Koan or Zen 1.24b. macOS places the fullscreen window below the safe area. The only safe-area use is the off-screen Y above, plus `menuBarHeight` (cocoa :200).

#### Koan (develop + #128)

**The observer.** `KoanCompactModeController::ImmersiveObserver` (`koan_compact_mode_controller.cc:218-240`) is registered in `OnAddedToWidget` (:273-288), so it runs after BrowserView. On enter and on exit, `OnImmersiveFullscreenChanged` (:1032-1057) does three things:

- ends URL-bar editing;
- collapses the caption strip;
- re-parks the top container in Koan's overlay (`ParkTopContainer` :1014), restores its transform and blocks events.

**Staying active in fullscreen.** The mode stays active because `UseContentCard()` is true on Mac fullscreen (`koan_content_card.cc:51-63`).

**Toolbar style.** `koan::HidesFullscreenTopChrome()` (`koan_browser_view_controller.cc:196`) is fullscreen && `hides_toolbar()`. It drives:

- the `UpdateFullscreenTopUI` hook, which sets `TOOLBAR_NONE` (`patches/.../browser_frame_view_mac.mm.patch`);
- `GetWidgetForAnchoring()` / `FullscreenStateChanged()`, which anchor to the window rather than the overlay (`browser_view.cc.patch`);
- `UpdateFullscreenTopChrome` (:1059), which sets `kWidgetForAnchoringKey`, calls `HideFullscreenTitlebar` (lights hidden, `NSTitlebarContainerView` alpha 0, `koan_traffic_lights_mac.mm:59-70`), and calls `UpdateFullscreenTopUI()` again.

**Compact in fullscreen.**

- Borderless: `ContentCardInsets()` is empty when compact is on (`IsBorderlessFullscreen`, :38).
- A pointer monitor watches a 2 px screen edge (`WantsFullscreenEdges`/`CheckFullscreenEdges` :748-777), because there is no gutter to hover.
- `Deactivate()` in fullscreen hands the top container to Chromium's overlay view instead of the BrowserView (:1174-1206).

**Other #128 pieces.**

- The bookmarks bar is gated by `koan.bookmarks_bar.show` (`bookmark_bar_controller.cc.patch`).
- `kShowFullscreenToolbar` defaults to false (`browser_ui_prefs.cc.patch`).
- View > Always Show Toolbar is disabled while Koan hides its toolbar (`browser_native_widget_mac.mm.patch`, `ValidateUserInterfaceItem`).
- #128 added **no layout patch**. The layout hooks (`browser_view_layout_impl.cc`, `browser_view_tabbed_layout_impl.cc`) are older and have no fullscreen branch.

**Two cases deliberately keep Chromium's reveal (`kAutohide`).**

- `Mode::kNone`: the sidebar is collapsed to the rail or the user picked "Sidebar and Top Toolbar".
- Compact "hide sidebar only".

---

### 3.2 Inventory

Legend for **Koan**, as in the vertical-tabs audit:

- *Owned*: Koan decides.
- *Redirected*: the entry point runs Koan's code.
- *Hidden*: the UI is not shown but the code path is live.
- *Hooked*: a patch changes the numbers, but the behaviour is upstream's.
- *None*: upstream behaviour leaks through.

#### 3.2.1 Machinery, flags, prefs

| Upstream item | Where (file:function/line) | Koan today | Reachable by user / old profile / extension? | Zen 1.24b |
|---|---|---|---|---|
| Controller choice: Mac immersive vs stub; tabbed vs plain Cocoa controller | `immersive_mode_controller_factory_views.cc:26`; `window_feature_controller.cc:63-75`; `native_widget_ns_window_bridge.mm:1245-1259` | None (used as is) | Always for normal windows and PWAs. Popups (no Koan sidebar) use the stub: no immersive, `ShouldHideUIForFullscreen()` true (`browser_window_fullscreen_controller.cc:72-94`) | Firefox's native fullscreen; `inFullscreen` / `sizemode=fullscreen` |
| Feature flags in 158 | `ui_features.cc:248` `kPressAndHoldEscToExitBrowserFullscreen` **on**; `:56` `kKeyboardLockHeldEscExitsTabFullscreen` on; `ui_base_features.cc:436` `kAsyncFullscreenWindowState` off; `remote_cocoa/app_shim/features.cc:14` `kImmersiveFullscreenSpaceSwitchMitigation` on, `:19` `…OverlayWindowDebug` off, `:29` `kMacFullScreenAuxiliaryFileDialog` on, `:34` `kClearFirstResponderInToolbarFullScreenWindow` on; `status_icons_features.cc:9` `kHideStatusIconMacInFullscreen` on; `fullscreen_features.cc:9,11` bubble opaque/origin on; `media_switches.cc:1317` `kFullscreenVideoPictureInPicture` on. **No** `kImmersiveFullscreen*` / `kFullscreenAnimateTabs` | None overridden | Finch or `--disable-features` only. Press-and-hold Esc exits *browser* fullscreen (`fullscreen_controller.cc:503-515`, `RequiresPressAndHoldEscToExit` = `IsFullscreenForBrowser`) | Esc and native fullscreen: unknown (fullscreen-and-notch.md LIVE CHECK 7) *(live check)* |
| `prefs::kShowFullscreenToolbar` ("Always Show Toolbar in Full Screen") | registered `browser_ui_prefs.cc:186-188` **SYNCABLE_PREF** (`chrome_syncable_prefs_database.cc:719`), incognito allowlist `pref_service_incognito_allowlist.cc:202`; read `fullscreen_util_mac.cc:32-41`, `BrowserFrameViewMac` ctor :110 (observer → `UpdateFullscreenTopUI`), `layout_delegate_impl.cc:126-134`, `immersive_mode_controller_mac.mm:308-325`, also `actor_ui_window_controller.cc:361`, `lens/overlay_base_controller.cc:603,945` | **Hooked**: default false (patch). Overridden to `TOOLBAR_NONE` while Koan hides its toolbar | **Yes.** An old Chrome profile, or sync if it is ever enabled, can bring `true`. Then in `Mode::kNone` and compact "hide sidebar only", fullscreen shows Chromium's toolbar permanently (`kAlways`). With Koan's toolbar hidden, the value is ignored but still flips | Unknown for Zen's toolbar layouts. #128's patch comment claims Zen hides the toolbar until the top edge; that was measured only in the single-toolbar default *(live check)* |
| `IDC_TOGGLE_FULLSCREEN_TOOLBAR` / `kActionToggleFullscreenToolbar`, **⇧⌘F** (`accelerators_cocoa.mm:110`), View menu (`main_menu_builder.mm:300`) → `chrome::ToggleAlwaysShowToolbarInFullscreen` (`browser_commands_mac.mm:30-43`) | `browser_actions.cc:3039`; `browser_command_controller.cc:944,2521` (always enabled on Mac) | **Hidden-ish**: the menu item is *disabled* (not hidden) while Koan hides its toolbar, via the `ValidateUserInterfaceItem` hunk. The action callback is untouched | Whether ⇧⌘F reaches the action with the menu item disabled *(live check)*. The result is harmless while Koan's toolbar is hidden, but the pref persists and governs `kNone`-mode fullscreen later | none (Firefox: View > Toolbars) |
| `IDC_FULLSCREEN` / `kActionFullscreen`, ⌃⌘F (`accelerators_cocoa.mm` `AcceleratorForEnterFullscreen`), green button, View > Enter Full Screen (title swap `browser_native_widget_mac.mm:431-436`) | `browser_command_controller.cc:877`, `FullscreenController::ToggleBrowserFullscreenMode` :124 | None. There is no separate "presentation mode" in 158: on Mac browser fullscreen is always immersive, and the toolbar style is the only variant | User | ⌃⌘F, green button |
| Browser-fullscreen exit bubble ("Press and hold Esc…") for controller-initiated fullscreen | `exclusive_access_manager.cc:97-143` (`IsControllerInitiatedFullscreen` → `…BROWSER_FULLSCREEN_EXIT_INSTRUCTION`); bubble parented to `GetWidget()`, y from `top_container_->GetBoundsInScreen()` (`browser_view.cc:718-735`, `exclusive_access_bubble_views.cc:400-430`) | None | Probably on ⌃⌘F / menu entry, not on green-button entry *(live check)*. Position: Koan's top container is parked off-view, so its bounds may push the bubble up or down *(live check)* | No Chromium bubble; Esc behaviour *(live check)* |

#### 3.2.2 Top chrome, reveal, title bar, anchoring

| Upstream item | Where | Koan today | Reachable? | Zen 1.24b |
|---|---|---|---|---|
| Top-container reparent into `overlay_widget()` and back | `browser_view.cc:3800-3826` (Start), `:3828-3851` (End: hides Chromium's overlay view, `DestroyLayer`, re-insert at `top_container_insertion_index_`) | **Owned** while Koan hides its toolbar: re-parked by the observer after BrowserView (observer order is insertion order). `Deactivate()` in fullscreen hands it to `overlay_view()` (:1174-1206) | Every enter and exit. `REBASE.md:193` documents the order dependency | n/a |
| Reveal at the top edge: Cocoa `kAutohide` title-bar accessory (`ImmersiveModeTitlebarViewController`), menu-bar reveal amount, `OnImmersiveModeToolbarRevealChanged` | `immersive_mode_controller_cocoa.mm:366-421, 573-590`; Mac controller :487-518 | **Hooked**: `TOOLBAR_NONE` while Koan hides its toolbar. Koan's own reveal: compact toolbar hot zone / 2 px edge monitor | `kNone`-mode windows (rail, Ctrl+Alt+B layout, compact "hide sidebar only") still get Chromium's reveal *with Koan's top container* (which holds Chromium's toolbar). This is intended per #128 | Default layout: only the menu bar at the top edge |
| Reveal locks (`ImmersiveRevealedLock`): omnibox focus (`omnibox_view_views.cc:540`, `focus_lock_` :347-358), `FocusToolbar` (`browser_view.cc:2427-2436`), `ShowAppMenu` (:3224-3233), location-bar focus reveal (:4475-4484, **skipped for vertical tabs**), download bubble (`download_toolbar_ui_controller.cc:640-645`), tab strip controller (`browser_tab_strip_controller.cc:481`), child NSWindows (`OnChildWindowAdded` :481-498) | as listed; `GetRevealedLock` → mojo `ImmersiveFullscreenRevealLock` | **Neutralised** under `kNone` (`SetIgnoreRevealLocks(true)`). Live otherwise | Under `kNone`, a lock is counted but not applied. Toggling compact or single toolbar while a lock is held: `UpdateToolbarVisibility` returns early when `reveal_lock_count_>0` (:376), so the style change is **dropped** until the next change *(live check: open a menu, toggle compact in fullscreen)* | n/a |
| `MoveChildren` of the find bar, tab-modal dialogs, omnibox popup and top-chrome bubbles into the overlay widget | `immersive_mode_controller_mac.mm:159, 221, 327-345, 396-485` | **Hooked** indirectly: Koan's `kNone` triggers `OnContentFullscreenChanged(true)`, which moves them back to the browser widget. New ones anchor to the window (`GetWidgetForAnchoring` hook) | There is a moment on entry (`SetEnabled` moves them into the overlay, then `UpdateFullscreenTopUI` moves them back). A find bar open across ⌃⌘F may flicker *(live check)* | n/a |
| Dialog/bubble anchoring widget | `BrowserView::GetWidgetForAnchoring` :1592, `FullscreenStateChanged` :2306-2320 (`kWidgetForAnchoringKey`), `tab_modal_dialog_host.cc:46`, `find_bar_host.cc:158` | **Hooked** (2 hunks) plus `UpdateFullscreenTopChrome` | User | n/a |
| Toolbar-anchored bubbles (downloads, page actions, extensions, profile) while the top container is parked | anchors inside `top_container` | None. Not fullscreen-specific (single toolbar parks the container in windowed mode too) | Download bubble in fullscreen: anchored to a slid-out button *(live check)* | Zen anchors to the sidebar's buttons |
| Infobar slide with the menu bar: `GetExtraInfobarOffset() = reveal_amount*(menu_bar_height+overlay_height)` | `immersive_mode_controller_mac.mm:317-325`; `browser_view_tabbed_layout_impl.cc:1209` | None | With Koan's toolbar hidden, an infobar may jump by menu-bar height + top-container height when the menu bar peeks, although nothing is revealed *(live check: whether menu-bar reveal still reports under `kNone`)* | Infobars are notifications in the content area |
| Title-bar strip, thin title-bar controller, traffic lights in fullscreen | cocoa :645-676 (thin controller kept visible under `kNone`), `ImmersiveModeView`; AppKit lights on the menu-bar reveal | **Owned**: `HideFullscreenTitlebar` (transparent, separator none, lights hidden, `NSTitlebarContainerView` alpha 0). `RestoreFullscreenTitlebar` on exit and in `kNone` mode. `KoanTrafficLightsController::OnBrowserViewLayout` re-applies it (:30-55) | User. Not covered: the **exit affordance** (no green button in the top peek); exit only via ⌃⌘F / View menu / Esc-hold | Notes conflict: "traffic lights gone" vs "the green button revealed from the menu bar exits" (fullscreen-and-notch.md §1) *(live check)* |
| Frame insets in fullscreen | `BrowserFrameViewMac::GetBrowserLayoutParams` (zero caption exclusions), `GetTopInset`=0 | Koan: `HasPageSideCaptionButtons`/`UpdateCaptionButtons` return early in fullscreen; `OwnsTitlebar` false | — | Toggle at x 6 in row 1 |
| Notch / safe area | cocoa `GetOffscreenYOrigin` :625-635 only | None needed | OS keeps the 33 pt strip black. With compact borderless, Koan's 2 px top edge is at y 0 *below* the strip, which matches Zen | Same: no notch code, measured 1470×923 at y 33 |

#### 3.2.3 Content (tab / HTML5) fullscreen and exclusive access

| Upstream item | Where | Koan today | Reachable? | Zen 1.24b |
|---|---|---|---|---|
| Tab fullscreen (`requestFullscreen`), `FullscreenController::EnterFullscreenModeForTab` :201, `ExitFullscreenModeForTab` :292, Mac exits instantly (:686-693); "content fullscreen" predicate `fullscreen_utils::IsInContentFullscreen` (`fullscreen_util_mac.cc:17-30`, tab **or extension** fullscreen) | `fullscreen_controller.cc`; immersive stays **enabled** (`OnFullscreenStateChanged` uses `IsFullscreen()`, not the content flag) | **None.** No Koan file mentions content fullscreen (`grep -ri ContentFullscreen src/` is empty) | **Yes: any web page** (a video's fullscreen button) | DOM fullscreen: all chrome collapsed, no margin/radius/shadow, compact rules off (`zen-compact-mode.css:47` `:not([inDOMFullscreen])`) |
| Vertical strip hidden in content fullscreen | `browser_view_layout_delegate_impl.cc:68-74` (`GetTabStripType` → `kNone`) | Inherited (strip not laid out) | — | same |
| `UseContentCard()` / `ContentCardInsets()` / `ActiveContentsRadii()` in content fullscreen | `koan_content_card.cc:51-115`; applied in `multi_contents_view.cc.patch` | **Leak.** `ShouldDrawVerticalTabStrip()` is still true (`ShouldDrawTabStrip` → `kFeatureTabStrip` → `!ShouldHideUIForFullscreen()`, and that is false while immersive is enabled). So the video gets the hidden-toolbar card: TLBR(8, 0, 8, 8) with the leading margin 0 (the sidebar side) plus the 7 pt radius. Compact on → `Insets()` (by luck: borderless) | **Web page**, default single-toolbar profile *(live check: confirm the asymmetric inset)* | Edge to edge |
| Compact hot zones, edge monitor, toolbar reveal in content fullscreen | `WantsFullscreenEdges` :748 (only `mode_` + `IsFullscreen`), `LayoutHotZones` :1592, `OnToolbarHotZoneHovered` | **Leak.** The pointer at the top screen edge slides Koan's toolbar over the video; the left edge takes the sidebar hover lock. `KoanHotZone` views also eat the pointer at the edges of the video | **Web page** + compact mode *(live check)* | Compact disabled in DOM fullscreen |
| URL bar (⌘L / ⌘T replace-newtab) in content fullscreen | `koan_url_bar_controller.cc:301-306` (blocks only `fullscreen && !hides_toolbar`) | **Leak.** The floating URL bar opens over the video in single toolbar | User key. ⌘T switches the tab, which exits tab fullscreen | URL bar `visibility: visible` in DOM fullscreen (issue 5229) |
| Keyboard reservation in fullscreen: with the toolbar "not showing", only `IDC_FULLSCREEN`/`IDC_EXIT` are reserved before the page | `browser_command_controller.cc:523-541`; `IsToolbarShowing()` = `GetTabStripVisible()` = immersive `IsRevealed()` (`browser_view.cc:1378-1391, 3103`) | None. Koan's *browser* fullscreen always counts as "toolbar hidden" | **Web page**: in Koan's native fullscreen a page can `preventDefault` ⌘T, ⌘W, ⌘N, ⌘L before Koan sees them *(live check)*. Chrome only does this in autohide mode; for Koan it is every fullscreen | Firefox reserves ⌘T/⌘W/⌘N in all modes |
| Keyboard Lock, pointer lock, their bubbles; press-and-hold Esc | `exclusive_access_manager.cc:97-143`, `keyboard_lock_controller` | None (correct as upstream) | Web page with keyboard lock | n/a |
| Extension fullscreen: `chrome.windows.update({state:'fullscreen'})` → `SetFullscreenMode(true, url)` | `windows_api.cc:1152-1157` | Same as tab fullscreen (counts as content fullscreen → `TOOLBAR_NONE`) plus the leaks above | **Extension**, no permission needed | n/a |
| `state:'locked-fullscreen'` | `windows_api.cc:109-112, 314-372` (needs `lockWindowFullscreenPrivate`, allowlisted) | None | Not reachable by store extensions | n/a |

#### 3.2.4 Windows, Spaces, restore, PiP, drag

| Upstream item | Where | Koan today | Reachable? | Zen 1.24b |
|---|---|---|---|---|
| Restore into fullscreen | `GetWindowShowState` `browser_view.cc:2934-2943` persists `kFullscreen` (`session_service_commands.cc:263`); `NativeWidgetMac::Show(kFullscreen)` is `NOTIMPLEMENTED()` (`native_widget_mac.mm:784-786`) | None | A restored window probably opens windowed. Koan's `UpdateActiveSoon` path handles either *(live check)* | Firefox restores `sizemode` |
| Multiple fullscreen windows / Spaces; compact pref changed from another window | per-window controllers; space-switch mitigation flag | Handled per window (#128 review: `UpdateActive` → Deactivate/Activate macOS block) | User | per window |
| Tab drag in fullscreen | `tab_drag_controller.cc:464` (`was_source_fullscreen_`), `:755-765` (vertical tabs: dragging all tabs **restores** the window mid-drag) | None | Drag every tab of a fullscreen window: immersive exits during the drag, Koan re-parks while the sidebar may hold hover or peek locks *(live check)* | n/a |
| PiP / document PiP | `BrowserFrameViewMac::OnFullscreenStateChanged` → `PictureInPictureWindowManager::OnAnyBrowserEnteredFullscreen` (`picture_in_picture_window_manager.cc:692`) | Koan's media card `EnterPictureInPicture` only | Fine *(live check: PiP window stays above the fullscreen Space)* | n/a |
| Vertical-tabs enable-state lock and the delayed-switch toast | `browser_view.cc:6027` | See vertical-tabs audit A1 (removes the toggle) | Yes until A1 lands | n/a |
| PWA / popup windows | stub controller for popups, Mac controller for PWAs | No Koan sidebar → `UseContentCard` false → `Mode::kNone` | n/a | n/a |

#### 3.2.5 Teardown: the #128 crash and #152

**What crashed (#153).** The crash path, step by step:

1. `BrowserView::DeleteBrowserWindow()` runs `browser_widget_.reset()` (`browser_view.cc:4293`). libc++ nulls the pointer first.
2. `~Widget` runs `DestroyRootView`, then `~BrowserView`.
3. Koan's hook at the top of `~BrowserView` runs `DestroyFor`, then `OnBrowserViewDestroying` (:348), then `Deactivate()`, then `UpdateCaptionButtons()`.
4. #128 put `browser_view_->IsFullscreen()` at the top of the Mac branch. `BrowserView::IsFullscreen()` dereferences the null `browser_widget_`.

This happened on **every** close with any Koan mode active, not only in fullscreen.

**What #152 changes.**

- `koan::IsWindowFullscreen()` adds a `browser_widget()` check, and all `IsFullscreen()` calls in `src/` go through it: content card, traffic lights, URL bar, window drag, `HidesFullscreenTopChrome`.
- `destroying_` makes `UpdateCaptionButtons()` return, and `Deactivate()` stop before the sidebar expand and the layout.

| Path | Order | Covered by #152? |
|---|---|---|
| Close a window, any mode, windowed | as above | **Yes** (the reported crash) |
| Close while in browser fullscreen | `BrowserDidClose` → `SetEnabled(false)` → Koan re-parks (widget alive) → `TearDownPreBrowserWindowDestruction` resets the immersive controller → `ImmersiveObserver::OnImmersiveModeControllerDestroyed` → `~BrowserView` → `Deactivate` (immersive `From()` is null, so the Mac block is skipped) | **Yes**. #128 review traced it |
| Close during the AppKit transition | `FullscreenStateChanging` returns early when `IsClosed()` (:2269). Koan's `OnImmersiveFullscreenChanged` has **no** such guard, but runs while the widget is alive | Yes, by ordering. Add the same `IsClosed()` guard (F4) |
| Cmd+Q with fullscreen windows | each browser closes through the same path | Yes by trace *(live check: the #152 checklist)* |
| Tab fullscreen while the tab or window closes | Mac exits instantly (`fullscreen_controller.cc:686`); the exit is posted; Koan's `UpdateActiveSoon` uses a `WeakPtr` | Yes *(live check)* |
| `UpdateFullscreenTopChrome` → `frame->UpdateFullscreenTopUI()` → upstream `ImmersiveModeController::From()->OnContentFullscreenChanged` (unguarded) after the immersive reset | Only reachable if called between `TearDownPre…` and `~BrowserView`; `IsWindowFullscreen` is false then, so there is an early return | Yes, via #152's helper |
| Koan's `Deactivate()` before `destroying_` returns | Still reparents the top container, `RemoveChildViewT`, `InsertAfterInFocusList`, `EndEditing` inside `~BrowserView` | Safe today (all children alive), but this is UI work during teardown (#152 review finding 2) |
| `KoanTrafficLightsController` seeing a fake "fullscreen exit" during teardown | :35 | Harmless with `destroying_` |

**Verdict.** #152 plus `destroying_` covers every teardown path I could trace. What is left is structural, not a known crash:

- `Deactivate()` mixes "drop state" and "undo UI".
- #129 and #152 each add a `destroying_` flag. They merge only if one of them drops its hunk.

---

### 3.3 Changes

Rules for Koan's fullscreen:

1. In content fullscreen, Koan is **suspended**: no card, no hot zones, no reveal, no URL bar. This is Zen's `:not([inDOMFullscreen])`.
2. In browser fullscreen, Koan's toolbar mode decides the top edge. Chromium's reveal only appears where Koan shows Chromium's toolbar anyway (`Mode::kNone`).
3. No UI work in teardown.

| # | Change | Hook | New patch? |
|---|---|---|---|
| F1 | Add `koan::IsContentFullscreen(bv)` = `fullscreen_utils::IsInContentFullscreen(bv->browser())`. `UseContentCard()` returns false then: edge to edge, upstream radii. `WantsFullscreenEdges()`, `LayoutHotZones()` and the hot-zone handlers ignore input then (hide the `KoanHotZone`s). `OpenForNewTab`/`Open` refuse then. Do **not** `Deactivate()` (avoid reparenting the top container on every video). Re-evaluate on a `FullscreenObserver` (`FullscreenController::AddObserver`, `OnFullscreenStateChanged`) owned by the compact controller, plus the existing layout re-check | `src/` only (`koan_content_card.cc`, `koan_compact_mode_controller.cc`, `koan_url_bar_controller.cc`) | no |
| F2 | On any fullscreen change, clear Koan's hover state (Zen's `sizemodechange` → `_clearAllHoverStates`): drop `hover_lock_`/`peek_lock_` via the deferred release, `toolbar_revealed_=false`, `edge_sidebar_hovered_=false` in `OnImmersiveFullscreenChanged` and in F1's observer. After a content-fullscreen exit, optional flash behind Koan's equivalent of `zen.view.compact.toolbar-flash-popup` (default false) | `src/` | no |
| F3 | ⇧⌘F / Always Show Toolbar: in the existing `ValidateUserInterfaceItem` hunk, set `new_hidden_state=true` (not just disable) whenever the window has Koan's sidebar (`koan::OwnsTitlebar`-like predicate). Per window, re-point `kActionToggleFullscreenToolbar` to a no-op under Koan: patchless with `kUseActionsForBrowserCommands`; force that feature on (audit A4). Decide the `kNone`-mode style from Zen (F9), not from this pref | existing `browser_native_widget_mac.mm.patch` (+1 line) + `src/` | no |
| F4 | One teardown rule (audit A13). `OnBrowserViewDestroying` sets `destroying_` first, then only drops state: timers, locks (deferred), observers, hot-zone children. No top-container reparent, no `EndEditing` layout, no `UpdateCaptionButtons`. `OnImmersiveFullscreenChanged`/`UpdateFullscreenTopChrome` return early on `destroying_` or `browser_widget()->IsClosed()` (mirrors `browser_view.cc:2269`). Land #152 first, then rebase #129 onto its `destroying_` | `src/` | no |
| F5 | Exit affordance: if Zen shows the window buttons with the menu-bar peek (LIVE CHECK), keep AppKit's lights in `HideFullscreenTitlebar` and fade only the strip background. Else leave as is and document ⌃⌘F | `src/` (`koan_traffic_lights_mac.mm`) | no |
| F6 | Fullscreen keyboard reservation: treat browser fullscreen with `HidesFullscreenTopChrome()` and **not** content fullscreen as "toolbar showing" in `IsReservedCommandOrKey`, so ⌘T/⌘W/⌘N/⌘L stay Koan's as in windowed mode. Content fullscreen keeps upstream's rule (keyboard lock) | existing `browser_command_controller.cc.patch` (+2 lines) | no |
| F7 | Infobar offset: under `HidesFullscreenTopChrome()`, use 0 for `GetExtraInfobarOffset()` in the tabbed layout (if the live check shows the jump) | existing `browser_view_tabbed_layout_impl.cc.patch` (+1 line) | no |
| F8 | Reveal-lock / style race: after any Koan mode change in fullscreen, call `UpdateFullscreenTopChrome()` again when the last reveal lock is released. Or call `ForceToolbarVisibilityUpdate` through `UpdateFullscreenTopUI` on the next mode or pref change (cocoa :376 drops style changes while locks are held). Only if the live check reproduces it | `src/` | no |
| F9 | `kShowFullscreenToolbar` default: measure Zen's "multiple toolbars" and "collapsed" layouts in native fullscreen. If Zen keeps the toolbar visible there, register `true` (revert the default patch) and keep `TOOLBAR_NONE` only for Koan's hidden-toolbar modes. Also make it per-Koan, not synced: a `ClearPref` migration in `src/`, since the pref is `SYNCABLE_PREF` | existing `browser_ui_prefs.cc.patch` (edit or drop) + `src/` | no |
| F10 | Esc: if Zen does not exit native fullscreen on Esc (LIVE CHECK 7), `DisableFeature(kPressAndHoldEscToExitBrowserFullscreen)` in `koan_feature_overrides.cc`. That also removes the "Press and hold Esc" bubble on ⌃⌘F | `src/` | no |
| F11 | Toolbar-anchored bubbles while the top container is parked (download bubble, extensions, page actions): move them to Koan anchors. Not fullscreen-only; track in the toolbar/anchors audit | — | — |
| F12 | `docs/REBASE.md` rows: (a) Cocoa treats `kNone` as content fullscreen (`IsContentFullscreen`, overlay off screen, reveal locks ignored), and Koan relies on `OnContentFullscreenChanged(true)` to move children back; (b) the `GetTabStripType` content-fullscreen rule; (c) `ShouldDrawVerticalTabStrip()` stays true in content fullscreen (F1 depends on it); (d) observer order (row 193 already exists) | docs | no |
| F13 | DCHECK-only checks (like audit A14): in fullscreen with `HidesFullscreenTopChrome()`, `top_container()->parent()` must be Koan's overlay after every immersive notification; `IsContentFullscreen` ⇒ no visible `KoanHotZone`, `ContentCardInsets()` empty | `src/` | no |

**Status.** F1, F2, F6 and the hiding half of F3 (menu item hidden, not just disabled; the ⇧⌘F action and the per-window re-pointing are still open) are in the fullscreen PR (#157). F1 differs from the plan in one way: `UseContentCard()` stays true (so the mode is never deactivated); `ContentCardInsets()` and `ActiveContentsRadii()` check `koan::IsContentFullscreen()` instead. F2 clears the hover state in both `OnImmersiveFullscreenChanged()` and the new `FullscreenController` callback; the sidebar locks are released from a posted task (a closing window never runs it). F4 is #152. F5, F7-F13 are open. **Correction to F6:** only Chromium's *reserved* commands (`kReservedCommandIds`: new / close tab and window, restore, tab switching, quit) are the browser's before the page in a window, so those are what F6 restores in fullscreen; ⌘L and ⌃⌘F are not reserved even in a window (a hands-on test on c157: a page's capturing keydown handler swallows them windowed and fullscreen), as in Firefox. Making them reserved would be a Koan choice, not parity.

Cost: about 1-1.5 agent-days plus one Mac build. No new hook patches: 3 existing patches gain 1-2 lines each. F1 is the one that matters to users.

---

### 3.4 Tests (Mac loop; DCHECK build; real pointer for edge cases)

**Content fullscreen**

1. Single toolbar (default profile): a local `<video>` test page → its fullscreen button. The page covers the whole screen below the notch strip: content bounds = widget bounds, no radius, no 8 pt gap on three sides (F1). Pointer to the top edge and the left edge for 2 s: no Koan toolbar, no sidebar, no `KoanHotZone` visible. Esc: card back, sidebar back.
2. Same with compact on (all three variants: hide sidebar, hide toolbar, hide both): no reveal at either edge. After exit, compact reveals work again and nothing is stuck revealed (F2).
3. ⌘L in video fullscreen: no floating URL bar.
4. Content fullscreen entered **from** browser fullscreen, then Esc: back to browser fullscreen with Koan's layout, `TOOLBAR_NONE`, top container in Koan's overlay.
5. An extension calling `chrome.windows.update(id, {state:'fullscreen'})`: same as 1, then `{state:'normal'}`: windowed Koan layout. `{state:'locked-fullscreen'}` without the permission: an error.

**Browser fullscreen**

6. ⌃⌘F, green button, View > Enter Full Screen, each in single toolbar / compact (3 variants) / `Mode::kNone` (rail; Ctrl+Alt+B). Expected: top-edge peek shows only the menu bar (plus the lights, per F5), except `kNone` modes, which show Chromium's reveal or always-on toolbar per F9. Verify window lists with `CGWindowListCopyWindowInfo` (no stray overlay window on screen).
7. View menu in fullscreen and windowed: no "Always Show Toolbar in Full Screen" (F3). ⇧⌘F: no pref change (read `Preferences`).
8. In fullscreen, a page with `keydown` `preventDefault` on everything: ⌘T, ⌘W, ⌘L, ⌘N still act (F6). With a page keyboard lock (`navigator.keyboard.lock()`) in content fullscreen: upstream behaviour.
9. Find bar (⌘F) open, then ⌃⌘F, then ⌃⌘F again: the find bar stays visible and positioned. Same with a JS `alert()` and a permission bubble open.
10. Infobar visible (e.g. a "restore pages" infobar): menu-bar peek in fullscreen, so the infobar does not jump (F7).
11. Open the sidebar context menu (reveal lock) and toggle compact via ⌘S while it is open, in fullscreen; close the menu. The top edge matches the new mode (F8).
12. ⌃⌘F with the download bubble pending, and a download started in fullscreen: the bubble is visible and anchored on screen (F11 tracking).
13. Two windows, each in its own fullscreen Space; toggle compact in one: both follow, no Chromium toolbar flash in the other.
14. Drag every tab of a fullscreen window (vertical tabs): the window leaves fullscreen mid-drag, no stuck sidebar, no crash.
15. ⌃⌘F, then quit with session restore on, then relaunch: the window state is recorded (windowed or fullscreen) and Koan's layout is correct in either case.

**Teardown (from #152's checklist, plus the transition cases)**

16. Close a second window in each mode (single toolbar, compact ×3, `kNone`) × (windowed, browser fullscreen, video fullscreen).
17. Close the window **during** the ⌃⌘F enter animation and during the exit animation (red button reachable via the menu-bar peek, or ⌘W while the transition runs).
18. ⌘Q with two fullscreen windows on two Spaces, plus one windowed.
19. Video fullscreen, then ⌘W the tab (tab fullscreen exit during close); also close the window from the Dock menu while in video fullscreen.
20. F13's DCHECKs are silent for the whole `fullscreen_*`, `compact_*` and `window_close` suites.

---

### 3.5 Top leaks and crash risks, ranked

1. **High: content fullscreen keeps Koan's chrome (F1).** Any web page triggers it, in the default profile.
   - A fullscreen video gets an asymmetric 8 pt card with rounded corners.
   - In compact mode, the top screen edge slides Koan's toolbar over the video, and the edge hot zones take the pointer.
   - ⌘L opens the URL bar over it.
   - Cause: Koan's predicates key on `IsFullscreen()`/`ShouldDrawVerticalTabStrip()`, and both stay true because upstream keeps immersive enabled in content fullscreen. *(live check to confirm the visuals; the code path is unambiguous.)*
2. **High (process): #152 is unbuilt and unrun on the Mac, and develop crashes on every window close until it lands (#153).** The guard is right. It needs the Mac checklist (tests 16-19), and #129 needs a rebase onto it.
3. **Medium: browser-fullscreen shortcuts can be swallowed by pages (F6).** Koan's fullscreen always counts as "toolbar not showing" (`browser_command_controller.cc:523-541`), so ⌘T, ⌘W and ⌘L go to the page first in every Koan fullscreen, where Chrome does this only in autohide.
4. **Medium: teardown still does UI work in `Deactivate()` (F4).** It is safe today by ordering only (`~Widget` closes the native window before `DestroyRootView`). The next edit that touches `GetWidget()` there becomes a use-after-destroy on every close.
5. **Medium: `kNone`-mode fullscreen is upstream's, driven by a synced pref (F3/F9).** A rail or Ctrl+Alt+B window, or compact "hide sidebar only", shows Chromium's top-edge reveal, or a permanent toolbar if `kShowFullscreenToolbar` arrives `true` from an old profile or sync. Whether that matches Zen's toolbar layouts is unmeasured.
6. **Low-medium: Cocoa drops style changes while a reveal lock is held** (`immersive_mode_controller_cocoa.mm:376`). A Koan mode toggle in fullscreen with a menu or child window open can leave the wrong top-edge behaviour until the next change (F8) *(live check)*.
7. **Low: exit affordance and Esc parity (F5/F10).** No green button in the top peek, and press-and-hold Esc exits with a Chromium bubble. Zen's behaviour on both is unconfirmed.
8. **Low: infobar jump and toolbar-anchored bubbles in fullscreen (F7/F11).** These are cosmetic, and only *(live check)* will show whether they occur.

Related issues:

- **#146** (compact dropped in fullscreen, Chromium toolbar in the peek) was filed on 09d39ad9, before #128. #128 is its fix, but the issue is still open: re-test on develop + #152.
- **#133 / #149** are windowed traffic-light issues (#144 / #128's tabs-on-right fix). Fullscreen is not involved, because `UpdateCaptionButtons` returns early in fullscreen.
