# Settings audit

Does every row of the one Settings page (`chrome://settings`: Koan's panes,
then Chromium's sections) do what it says in Koan? For each row: the pref and
every reader, whether a change applies at once to every window, new windows
and after a restart, whether the control shows the real value, its dependent
rows, and whether its label matches the behaviour and Zen 1.24b's
(`zen-preferences.ftl`, `prefs/*.yaml`). A row that silently did nothing was
fixed, hidden or made to say so.

## Visual rows

The e2e suite `scripts/e2e/settings_effects_visual.mjs` (in the Mac suite,
`scripts/mac/suite/e2e.list`) changes each row through the settings page's
own control, checks the effect in the window (UI DevTools) or in a page
(CDP), and puts the row back. "Skip" means the effect can't be seen from
UI DevTools or CDP on macOS. Native fullscreen for the layout rows is
#311's (`fullscreen_multiple`, `fullscreen_single`); this suite checks the
bookmarks bar there.

All profile prefs here are read from the original profile, so private
windows follow the same settings (`settings.mjs` checks compact mode in a
private window). A pref is read again whenever a window is created, so new
windows and restarts follow it. The suite checks that for tabs on the right
in a new window.

| Row | Pref | Readers | Live? | What was wrong | Fixed in | Test |
|---|---|---|---|---|---|---|
| Look | `koan.look.style` (local state) | `koan_look_controller.cc` (`LookController`, a pref observer that re-themes every profile's windows); the `look::IsShadcnStyle()` callers | Yes, every window; the font only after a restart (the description says so) | The group's description said Zen's look was the default. It is shadcn's (#296) | This PR (text) | `Look:` |
| Mode | `koan.look.mode` (local state) | `AdjustColorProviderKey()` (shadcn only, not in private windows) | Yes | Did nothing in the Zen look, but stayed enabled there | This PR: greyed out in the Zen look (`Row::look`) | `Mode:`, `...greys out the shadcn colour rows` |
| Base colour | `koan.look.base` (local state) | `LookController::Reload()` → shadcn tokens | Yes | Same as Mode | This PR | `Base colour:` |
| Accent colour | `koan.look.accent` (local state) | as Base colour | Yes | Same as Mode | This PR | `Accent colour:` |
| Paste a theme | `koan.look.custom_css` (local state) | `look::ParseThemeCss()` (the handler refuses CSS without a token) | Yes | Same as Mode | This PR | `Theme:` |
| Browser Layout | `koan.single_toolbar` | `KoanCompactModeController` (observer → `UpdateActiveSoon()`), `InitialSidebarCollapsed()`, `KoanLoadingIndicator` (on layout) | Yes; the cards follow Ctrl+Alt+B | Windowed: nothing. macOS fullscreen: the top toolbar never showed with Sidebar and Top Toolbar | Fullscreen: #311 | `Layout:` (windowed) |
| Enable compact mode | `koan.compact_mode.enabled` | compact controller, `koan_content_card.cc` (borderless fullscreen), `InitialSidebarCollapsed()` | Yes; the checkbox follows ⌘S | Nothing | | `Compact mode:` |
| What compact mode hides | `koan.compact_mode.hide` | compact controller (`PrefHidesSidebar/Toolbar()`), `InitialSidebarCollapsed()` | Yes | Windowed: nothing (with Only Sidebar every choice hides the sidebar, as the note says). macOS fullscreen: as Browser Layout | Fullscreen: #311 | `What compact mode hides:` (all three, and Only Sidebar) |
| Flash popup | `koan.compact_mode.toolbar_flash_popup` | compact controller, on each tab selection | Yes | Nothing. (Zen also skips the flash while a Glance animates; Koan doesn't: minor) | | `Flash popup:` |
| Background tab toast | `koan.compact_mode.show_background_tab_toast` | compact controller, on each inserted tab | Yes (Zen reads it once at startup) | Nothing | | `Background tab toast:` |
| Show the bookmarks bar | `koan.bookmarks_bar.show` | `bookmark_bar_controller.cc.patch` (observer; original profile); `KoanCompactModeController::docked_bookmarks_bar_height()` (Only Sidebar); `BrowserView::IsBookmarkBarVisible()` (fullscreen hook) | Yes, every window | **With Only Sidebar (the default layout) the bar never showed**: it is in the top container, which that layout kept out of the window. Zen shows it in a 34 px top bar above the page then (`vertical-tabs-topbar.inc.css`, `zen-has-bookmarks`). In macOS fullscreen upstream hid it until AppKit revealed the toolbar. ⇧⌘B did nothing (its View menu item was hidden, and its command switched Chromium's ignored pref) | This PR: with Only Sidebar (and compact mode with it) the top container stays, shifted up so only the bar shows, docked above the page beside the sidebar, the card below it; shown in macOS fullscreen in both layouts (`browser_view.cc.patch`); ⇧⌘B and View > Always Show Bookmarks Bar switch Koan's setting, the menu's check mark follows it (command policy, `browser_native_widget_mac.mm.patch`). The new tab page behaves as every page. Zen differences: in compact mode with Only Sidebar Zen hides its top bar until the top edge; Koan keeps the bar docked. While the URL bar floats (the top container is its card) the bar is out of sight | `Bookmarks bar:` (both layouts, compact, macOS fullscreen; ⇧⌘B through the menu bar) |
| Show New Tab Button on Tab List | `koan.tabs.show_newtab_vertical` | `KoanSidebarTabsController::UpdateNewTabRow()` (observer) | Yes | Nothing | | `Show New Tab Button:` |
| Move the new tab button to the top | `koan.view.show_newtab_button_top` | sidebar tabs (the row, `OpenNewTab()`), `koan_workspaces.cc` (new tab index) | Yes | Nothing. It stays enabled with the button hidden, as in Zen | | `Move the new tab button to the top:` |
| Tabs on the right | `koan.tabs.on_right` | compact controller `OnTabsOnRightChanged()` (re-activates, lays out again) | Yes, new windows too | Nothing | | `Tabs on the right:` (also a new window) |
| Translucent window | `glass_frame.enabled` (local state, Chromium's) | `GlassFrameService` (observer; `glass_frame_service.cc.patch`: every window) | Yes | Nothing found | | Skip: the blur is an NSVisualEffectView, not a view |
| Allow dragging the window from web pages | `koan.view.drag_window_from_content` | `KoanWindowDragController`, on each press | Yes | Nothing | | `Window dragging:` |
| Haptic feedback | `koan.haptic_feedback` (local state) | `koan_haptics_mac.mm`, on each tick | Yes | Nothing | | Skip: nothing to observe |
| Dark theme style | `koan.theme.dark_style` | `KoanThemeController` (observer) | Yes | **Did nothing in the shadcn look (the default)**: Space themes, and so their dark styles, are the Zen look's (`KoanThemeController::IsEnabled()`), but the row stayed enabled | This PR: greyed out in the shadcn look, with a note | `...the Zen-only Dark theme style is greyed out`; the colours: skip (need a Space theme, set from a native menu on macOS) |
| Enable Glance | `koan.glance.enabled` | `koan_renderer_switches.cc` (when a page's renderer process starts), `IsGlanceOn()` (each click), `koan_pinned_glance.cc` | Turning it off: at once. Turning it on: pages opened after the change | The row didn't say that turning it on needs new pages. **Mac run:** not even the next page had it: content's spare renderer process, started before the change, kept the old switches | Note (#312); the spare is dropped on a change (`koan::DropSpareRenderers()`, fix/settings-visual-e2e) | `Glance:` |
| Trigger method | `koan.glance.activation_method` | renderer switch (process start), `FallbackDisposition()` | Pages opened after the change (the note says so) | **Mac run:** the next page kept the old trigger (the spare renderer, as above) | fix/settings-visual-e2e | `Trigger method:` |
| Open links from Essentials and pinned tabs to other sites in Glance | `koan.glance.open_essential_external_links` | `OpenNewTabInGlance()`, on each link | Yes | Nothing | | `Pinned tabs' links to other sites:` |
| Open "Search for" in Glance | `koan.glance.enable_contextmenu_search` | `OnContextMenuCommand()` | Yes | Nothing found | | Skip: the context menu is a native NSMenu on macOS |
| URL bar: Behavior | `koan.urlbar.behavior` | `KoanUrlBarController::Behavior()` (each open), compact controller (observer) | Yes | Nothing | | `URL bar Behavior:` (all three) |
| Open the URL bar instead of a new tab page | `koan.urlbar.replace_newtab` | `OpenForNewTab()`, `OpenOnStartup()`, `SuppressesDefaultUrlBarFocus()` | Yes | Nothing | | `Replace new tab:` |
| Show the copy URL button | `koan.urlbar.show_copy_url` | `KoanSidebarNavView::UpdateUrl()` | **Was not live**: the button changed only at the next navigation or tab switch | This PR: the view observes the pref | `Copy URL button:` |
| Glass URL bar | `koan.look.acrylic_urlbar` (local state) | `KoanUrlBarController::UpdateGlassCard()` (observer) | Yes | Only the floating card is glass (not the URL bar in the top toolbar, nor in the pill); the row didn't say so | This PR: note | `Glass URL bar:` (reads `KoanUrlBarCardFill`'s new `Glass` property) |
| New tab page | `koan.newtab.page` | `search.cc.patch` → `GetNewTabPageURL()`, each new tab page | New tabs | Nothing | | `New tab page:` |
| Show the active Space's theme | `koan.workspaces.tint` | `KoanThemeController` and `KoanWorkspaceController::OnTintPrefChanged()` (observers) | Yes | **Did nothing in the shadcn look**, as Dark theme style | This PR: greyed out in the shadcn look, with a note | `...where the Space theme row is greyed out` |

### Not covered here

- The window's own colours for the look rows: `look.mjs` reads frame dumps,
  which only Linux's headless Ozone makes. (`look.mjs` still expects Zen's
  look as the default, and isn't in the Mac suite.)

## Behaviour and Chromium rows

Audited 2026-11-12 (fix/settings-audit-behaviour). "e2e" is
`scripts/e2e/settings_effects_behaviour.mjs` unless named otherwise; every
row there is changed through the page's own control. "Mac only" steps need
the macOS suite's test_helper extension; "real input" steps need the Mac
bench's keypress tools (`KOAN_INPUT=real`).

### Koan's rows

| Row (pane > group) | Pref (where) | Readers | Live / new windows / restart | Was broken? | Fix | Checked by |
|---|---|---|---|---|---|---|
| Check for updates (Look and Feel > Updates) | `koan.updates.check_enabled` (local state) | `koan_updates.cc` `Updater::CanCheck`, observed (`ScheduleCheck`) | live; browser-wide | Yes: an "Update Available!" found before the check was turned off still showed at the next start | the notice waits while the check is off (`ShowPending`) | e2e round trip; effect needs GitHub (skipped), `koan_update_logic_unittest` |
| Show a notification after Koan was updated | `koan.updates.show_update_notification` (local state) | `Updater::Start` (once per launch, as Zen's `checkForZenUpdates`) | next launch of a new build, which is when it can show | No | - | e2e round trip; effect needs a new build (skipped) |
| Close tab and switch to its owner tab ... when going back with no history (Tab Management > Workspaces) | `koan.tabs.close_on_back_with_no_history` | `koan::ShouldCloseTabOnBack` (back_to_opener patch, read each time); `KoanShortcutController` refreshes Back's enabled state on change, per window | live in every window; Zen's label and default (on) | No | - | e2e: Back enabled/disabled live, Back closes the child and shows its opener (Mac too: the sidebar's Back button); `tab_management.mjs` (Linux) |
| Ignore Pending tabs when cycling with Ctrl+Tab | `koan.tabs.ctrl_tab_skip_pending` | `KoanShortcutController::CycleTabs` (read per key) | live | **Yes, on macOS:** a real Ctrl+Tab / Ctrl+Page Down is one of Chrome's reserved commands there, which `prePerformKeyEquivalent` runs before normal-priority accelerators, so Koan never saw the key and both Ctrl+Tab rows (and Koan's recently-used walk) did nothing with a real keyboard. CDP keys take the views path, where Koan comes first, so the e2e passed | the cycling keys are registered high priority on macOS (`koan_shortcuts.cc`), guarded by `KoanShortcutTable.TabCyclingKeysAreReservedOnMac` | e2e (Mac only): skips the unloaded tab; real Ctrl+Page Down / Ctrl+Tab (real input) |
| Ctrl+Tab cycles within Essential or Workspace tabs only | `koan.tabs.ctrl_tab_within_kind` | `CycleTabs` | live | Yes, on macOS (as above) | as above | e2e (Mac only): stays among the Essentials, wraps within the Space; real keys; `tab_management.mjs` |
| Ctrl+Tab note ("Ctrl+Tab will cycle by recently used order ...") | shown while the row's pref and Chromium's `browser.ctrl_tab_mru` are on | `KoanSettingsHandler` observes both | live | No (the note's pref was named by string: now `::prefs::kCtrlTabMru`, so a rename breaks the build) | symbol | e2e: Chromium's row on → Ctrl+Tab goes to the tab shown before, and the note shows; off → it goes |
| When closing a tab, switch to the most recently used tab | `koan.tabs.select_recently_used_on_close` | `koan::PickRecentlyUsedTab` (tab_strip_model patch; `KoanPinnedTabController::SelectAnotherTab`) | live | No | - | e2e (Mac: shown by sidebar clicks, closed through test_helper): on → the tab shown before, off → the neighbour; `koan_tab_management_browsertest` (also the extension API's close) |
| Go from the last Space to the first (and back) | `koan.workspaces.wrap_around_navigation` | `KoanWorkspaceController::WrapsAround` (shortcuts, wheel, swipe, Space drag) | live | No | - | e2e: Next/Previous Space wrap on, stop off |
| Natural scrolling | `koan.workspaces.natural_scroll` | `SwipeAmount`, `OnSpaceWheel` | live | No | - | e2e (not on macOS: needs native wheel events) |
| Key to hold to switch Spaces with the mouse wheel | `koan.workspaces.scroll_modifier_key` | `IsSpaceScrollModifierDown` | live | No | - | e2e round trip (each option); effect: `koan_space_navigation_unittest` |
| Restore pinned tabs to their originally pinned URL on startup (Pinned Tabs) | `koan.pinned_tabs.restore_to_pinned_url` | `koan_pinned_tabs.cc` (session restore of each pinned tab) | next start, as it says; with On startup other than "Continue", pinned tabs come back at their pinned URL anyway (`ZEN_PARITY.md`) | No | - | e2e (Mac only): two restarts, on and off |
| Open other sites from a pinned tab's URL bar in a new tab | `koan.tabs.open_pinned_in_new_tab` | `koan_url_bar_controller.cc` omnibox disposition (read per navigation) | live | No | - | e2e: another site from an Essential's URL bar opens a new tab; off, the Essential goes there |
| Close Tab Shortcut Behavior (all six options) | `koan.pinned_tabs.close_shortcut_behavior` | `KoanPinnedTabController::HandleCloseShortcut` / `HandleCloseRequest`, `CloseButtonUnloads` (read per press) | live | No | - | e2e: each option on a new Essential (Ctrl+W; on macOS Cmd+W is the main menu's: real input); `pinned.mjs` (Linux) |
| Freeze background tabs (Performance) | `performance_tuning.tab_freezing.enabled` (local state, Chromium's) | `UserPerformanceTuningManager` → `FreezingPolicy::SetFreezingEnabledByUser` (observed) | live | No | - | e2e round trip; effect: `power.mjs` (freezing takes minutes) |
| Agent ready (Agent Control) | `koan.agent_control.enabled` (local state) | `AgentControlService::Update` (observed: starts / stops the gateway, ends tasks) | live | No | - | e2e: the gateway's `AgentControl/agent.json` comes and goes |
| Strict | `koan.agent_control.strict` (profile) | `koan_agent_containers.cc`, approval dialog (per task) | next task, as the group says | Partly: an indented sub-option of Agent ready that stayed enabled while Agent ready was off (a row could only depend on a profile pref) | `depends_on` takes a local-state pref; Strict greys out with Agent ready | e2e: greyed out and back |
| Your logins / Saved passwords / Addresses and cards / Your extensions / History / Limit a taken-over tab to its site | `koan.agent_control.use.*`, `koan.agent_control.limit_take_over_to_site` (profile) | `koan_agent_access.cc` (next task or page, as the group says) | as described | No | - | e2e round trip; effect: `agent_cli.mjs` (needs the koan CLI) |
| Awake agent tabs at a time / Fewer in Low Power Mode / Approval requests per minute / Failed connection attempts | `koan.agent_control.max_leased_tabs`, `low_power_halves`, `max_prompts_per_minute`, `max_auth_failures` (local state) | `koan_agent_lease.cc` (observed), `AgentControlService` limits (observed) | new connections and new awake tabs, as the group says | No | - | e2e: every option of each menu; effect needs an agent |
| Keyboard Shortcuts (rebind, conflicts, clear, reset) | `koan.shortcuts` | `KoanShortcutController::OnShortcutsChanged` (every window re-registers; macOS menu bar keys refreshed) | live | No | - | `settings.mjs` (each by what the key then does) |

### Chromium's rows that Koan's layout or behaviour overrides

| Chromium row (section) | Pref | What Koan does with it | Was broken? | Fix | Checked by |
|---|---|---|---|---|---|
| Show bookmarks bar / Bookmarks bar visibility (Appearance) | `bookmark_bar.show_on_all_tabs`, `bookmark_bar.visibility_state` | ignored: the bar shows only with Koan's own row (Look and Feel > Bookmarks bar), as Zen has none (`bookmark_bar_controller.cc` patch) | **Yes**: the toggle did nothing | hidden (`appearance_page.html.ts` patch); Koan's row is found by the search | e2e: not shown |
| Show tab search button (Appearance; either variant) | `tab_search.pinned_to_tabstrip` | the sidebar's tab search / tab groups button is hidden for good (`vertical_tab_strip_top_container.cc` patch); Koan has no horizontal strip | **Yes**: did nothing | hidden | e2e: not shown |
| Automatically pin new tab groups (Appearance) | `auto_pin_new_tab_groups` | Spaces are tab groups that are never saved (`koan::SavesTabGroups()`), so there is never a new saved group to pin | **Yes**: did nothing | hidden | e2e: not shown |
| Show tab groups in bookmarks bar (Appearance) | `bookmark_bar.show_tab_groups` | default off in Koan; shows Chrome's saved groups of an old profile on the bar (when Koan's bar is on) | No | - (kept: it still does what it says) | - |
| Tab hover cards: previews / memory usage (Appearance) | `browser.hovercard.image_previews_enabled`, `browser.hovercard.memory_usage_enabled` | the sidebar shows no hover cards (Zen: `browser.tabs.hoverPreview.enabled`=false; `koan::ShowsTabHoverCards()`) | **Yes**: did nothing | hidden | e2e: not shown |
| Tab strip position, Expand tabs on hover (Appearance) | `vertical_tabs.enabled`, `vertical_tabs.expand_on_hover` | always the sidebar | No (hidden before, #154) | - | e2e: not shown |
| Mode: Light / Dark / Device (Appearance), and Koan's macOS View > Appearance menu | `browser.theme.color_scheme2` | in the shadcn style (the default) the look's mode (Look and Feel > Mode, `koan.look.mode`, default Dark) set every window's colour mode over it | **Yes**: in the default look both did nothing, and the row showed Device while the windows were dark | `KoanColorSchemeSync` keeps them in step both ways while the shadcn style is on, from the first change of either (one setting, two places); the Zen style uses Chromium's alone, as before. Nothing is written at start, because the scheme is also web pages' `prefers-color-scheme` and the look's default (Dark) would turn sites dark on a Mac in light mode: until the user changes a mode, Chromium's row shows Device while the shadcn windows are dark (accepted, maintainer 2026-11-12) | e2e: Koan's Light → Chromium's Light; Chromium's Dark → Koan's Dark |
| Theme / Reset to default, Show home button (Appearance) | `extensions.theme.id`, `browser.show_home_button` | issue #300 (Z28) hides them | - | #300 (not duplicated here) | - |
| Cycle through the most recently used tabs with Ctrl+Tab (Appearance) | `browser.ctrl_tab_mru` | Koan walks its own recently used list (the Space's tabs and the Essentials) while it is on; Koan's Ctrl+Tab rows note it | No (the macOS key path is the Ctrl+Tab fix above) | - | e2e |
| Side panel position (Appearance) | `side_panel.is_right_aligned` | upstream lays the side panel out on either side, next to Koan's sidebar | No | - | Mac step (below) |
| Font size, Customize fonts, Page zoom, Warn before quitting (Appearance) | `webkit.webprefs.*`, zoom, `browser.confirm_to_quit` | untouched | No | - | - |
| Allow dragging a tab into split view (Appearance) | `browser.split_view_drag_and_drop_enabled` | upstream's drop target in the page (`multi_contents_view.cc`), which Koan's sidebar drags use | No | - | Mac step (below) |
| On startup (Continue where you left off / New Tab page / pages) | `session.restore_on_startup` | Koan defaults to Continue (Zen's `browser.startup.page`=3); with the others, Spaces (`koan.workspaces.list`) and pinned tabs still come back | No | - | `spaces_relaunch.mjs` (New Tab page start) |
| Search engine, Manage search engines | `default_search_provider*` | Koan's URL bar uses Chromium's `TemplateURLService`; the welcome page only picks the default | No | - | - |
| Memory Saver, Energy Saver, tab discard exceptions (Performance) | `performance_tuning.*` | untouched; pinned tabs and Essentials are exempt from discarding and freezing, and so are agent tabs | No | - | `power.mjs` |

Mac steps for the rows no script can drive: put the side panel on the left
and on the right with tabs on the left and on the right (it opens beside the
sidebar, on the side it says); drag a tab from the sidebar onto the page with
"Allow dragging..." on and off (a split only while on).
