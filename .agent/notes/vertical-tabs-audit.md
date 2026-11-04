# Upstream vertical tabs in Koan: audit and recommendation

Date: 2026-11-04. Chromium 158.0.8127.2 (`681cd621e8`), Koan `develop` 77a44141, plus the open PRs that touch this area (#129, #143, #144, #152). Read only: pristine Chromium came from `git show HEAD:` / `git grep HEAD`. Nothing was built or run, so every "reachable" verdict comes from reading the code. Verdicts marked *(live check)* still need a run on the Mac.

**Summary.** Koan's sidebar is upstream's vertical tab strip with Koan's pieces mounted inside it. Koan *uses* upstream's per-window state machine: compact mode hides the sidebar by **collapsing** the strip and riding **expand-on-hover**. But Koan never *owns* that state machine. Only one upstream state is defaulted (`vertical_tabs.enabled`, a registration default), and just one entry point is redirected (the collapse action, in open PR #143). Every other upstream state, command, menu item, setting row, IPH and persisted value still works as it does in Chrome. Recommendation: **option A**, made concrete as one Koan layer (`KoanVerticalTabsPolicy`). That means 1 new hook patch, 2 existing patches extended by a few lines, and the rest in `src/`. It closes every entry point listed below. Do **not** replace upstream's strip (option B) now. B does not even remove the need for A's mode lock (BrowserView builds the horizontal strip either way). It costs about 10x more, and it gives up the tab rows, drag and drop and tab-group machinery that Spaces, Essentials and folders stand on.

---

## 1. How Koan sits on the upstream strip today (the one-paragraph model)

- `vertical_tabs.enabled` defaults to `koan::features::IsSidebarEnabled()` (`patches/chrome/browser/ui/tabs/tab_strip_prefs.cc.patch`). It is **only a default**: any write to the pref is saved to the profile and honoured.
- The sidebar is `VerticalTabStripRegionView` (frame/). Koan mounts these inside it: its rows (`koan_sidebar_tabs.cc`), the Space header (`tab_strip_view_layout.cc` hook), the Space bar and media cards (no patch, `FlexLayoutView` child), and the nav buttons (`koan_sidebar_nav_view.cc`). It hides the bottom container's `NewTabButton` and `FrameSeparator` (by class name). The header's collapse button becomes Koan's "sidebar toggle" (PR #143 re-points `kActionToggleCollapseVertical`, and the patch hides the combo button).
- **Compact mode's "hidden sidebar" is upstream's *collapsed* state.** `KoanCompactModeController::Activate()` calls `RequestCollapse(true)`. The tabbed-layout patch then gives the strip 0 width (`browser_view_tabbed_layout_impl.cc.patch`). The reveal is an `ExpandOnHoverLock(kKeepExpanded)` taken by Koan's hot zone, and `koan::LayoutSidebarReveal` slides the layer. Once the pointer is over the strip, upstream's `UpdateExpandOnHoverState()` keeps it out.
- **A user-collapsed strip (Chromium's icon rail) is treated as Zen's "Collapsed Sidebar" layout.** It turns Koan's single toolbar off (`DesiredSingleToolbar()` → false → `Mode::kNone`), and `Mode::kNone` means **Chromium's own toolbar** is shown. That is the mechanism behind #132.
- Upstream persists the collapse per window (session extra data `vertical_tab_strip_collapsed`) and per profile (`vertical_tabs.collapsed_state`). Compact's collapse is therefore written to disk as if the user had collapsed the strip. Koan keeps a second pref, `koan.compact_mode.sidebar_collapsed_before`, to undo that.

---

## 2. Inventory: upstream state and entry points, Koan's handling, reachability, Zen

Legend for **Koan**: *Owned* means Koan decides. *Redirected* means the entry point runs Koan's code. *Hidden* means the UI is not shown but the code path is live. *Hooked* means a patch changes the numbers but the behaviour is upstream's. *None* means upstream behaviour leaks through.

### 2.1 State and prefs

| Upstream item | Where | Koan today | Reachable by user / old profile? | Zen 1.24b |
|---|---|---|---|---|
| `vertical_tabs.enabled` (`prefs::kVerticalTabsEnabled`), read by `VerticalTabStripStateControllerImpl` ctor and `OnModeChanged()`, gate `ShouldDisplayVerticalTabs()` | `chrome/browser/ui/tabs/tab_strip_prefs.cc:28`, `vertical_tab_strip_state_controller_impl.cc` (ctor, `SetVerticalTabsEnabled`, `OnModeChanged`, `OnLockDestroyed`) | **Default only** (patch). No observer, no lock | **Yes.** Tab menu, View menu, ⋮ > More tools, chrome://settings > Appearance "Tab strip position", an edited `Preferences`, a profile where it was ever set false. Persisted, so Koan stays "Chrome" across restarts (#148) | Firefox's equivalent `sidebar.verticalTabs` is **locked false** (`prefs/firefox/browser.yaml`) and `sidebar.revamp` is locked false: Zen never uses Firefox's vertical tabs and has no horizontal mode |
| `vertical_tabs.collapsed_state`, `vertical_tabs.uncollapsed_width` (profile-wide "last window" state) | `tab_strip_prefs.cc:31-33`; written by `UpdatePrefService()` on every collapse change and window activation; read in `browser_window_features.cc:607-621` for new windows not from session restore | Read and written by upstream. Koan clamps the width in the region ctor (`vertical_tab_strip_region_view.cc.patch`) and works around compact's collapse with `koan.compact_mode.sidebar_collapsed_before` | **Yes.** An old Koan profile (pre-#143 the toggle collapsed to the rail), a crash while compact hid the sidebar, or a profile copied from Chrome with vertical tabs collapsed: the new window opens as Chromium's rail **plus Chromium's toolbar** (`Mode::kNone`) | `zen.view.sidebar-expanded` (Zen's own, default true), `zen.view.sidebar-expanded.max-width` 500 |
| Per-window session extra data `vertical_tab_strip_collapsed` / `vertical_tab_strip_uncollapsed_width` | `VerticalTabStripStateController::kCollapsedKey`; written `UpdateSessionService()`, rebuilt in `session_service_base.cc:784`, restored `session_restore.cc:1370`, `browser_live_tab_context.cc:221/553` (Reopen closed window) | None | **Yes.** Restore after crash or relaunch, and Cmd+Shift+T of a closed window, bring back a collapsed rail saved while compact mode was on | n/a (Zen's state is prefs plus `zen-compact-mode` per window) |
| `vertical_tabs.expand_on_hover` (`kVerticalTabsExpandOnHoverEnabled`, default true) | `tab_strip_prefs.cc:29`; read `IsExpandOnHoverEnabled()` in `UpdateExpandOnHoverState()` | None. **Koan's compact reveal depends on it**: after the hot-zone lock is released (`kSidebarKeepHover`), only upstream's pref-gated hover keeps the strip out | **Yes.** Right-click the sidebar toggle (upstream's collapse-button context menu, see 2.3) and untick "Expand tabs on hover", or use chrome://settings > Appearance. Then the compact reveal closes about 150 ms after the pointer moves from the hot zone onto the sidebar *(live check)* | Zen's reveal is CSS on `zen-has-hover`; there is no such switch |
| `vertical_tabs.enabled_first_time`, `vertical_tabs.everything_menu_last_pressed` | `tab_strip_prefs.cc:30,34` | None | Harmless (metrics, menu badge) | n/a |
| `VerticalTabStripState` limits: default 240, min 126, max 400, collapsed 56, snap (126+56)/2 | `chrome/browser/ui/tabs/vertical_tab_strip_state.h` | **Hooked** in the region view (`koan::SidebarMinWidth/MaxWidth/CollapseSnapWidth`, Zen 163..500). Unhooked readers listed in `docs/REBASE.md` (window min width, `tab_view.cc`, top container, `browser_native_widget_mac.mm` glass cap 400) | Keyboard resize on the focused resize area (±50 px, `OnKeyPressed`) still works and can collapse | 230 default, max 500, 6 pt splitter |
| Collapse state machine `kCollapsed / kCollapsing / kExpanded`; delegate `RequestCollapse`, `IsCollapsing`; `ScopedEnableStateLock` | `vertical_tab_strip_state_controller.h`, region view `RequestCollapse()` → `BrowserAnimationController::Start(kVerticalTabStrip, …)` | **Used** as compact's hidden state (`Activate`, `Deactivate`, `ToggleSingleToolbar`, `OnSidebarToggleAction`, `koan_workspaces.cc:890`) | Driven by Koan, but also by every entry point in 2.2 and 2.3 | Compact mode moves the toolbox off screen (CSS). Collapse is a separate Zen state |
| Expand-on-hover locks (`kKeepExpanded`, `kKeepCurrentState`, force-collapse) and the mouse-velocity heuristic | `vertical_tab_strip_expand_on_hover_lock.*`, region `UpdateExpandOnHoverState`, `CalculateMouseVelocityForExpandOnHover` | **Used** (hover, peek, flash, menu, Space-form locks) | Upstream's own hover on a *user-collapsed* rail stays live | `zen-has-hover`, `zen-user-show`, `has-popup-menu` (compact-mode.md §3-4) |
| Organizer panel in the region (`SetOrganizerPanelView`, segmented control) | `vertical_tab_strip_region_view.cc:612`, `vertical_tab_strip_segmented_control.cc` | Segmented control hidden on macOS (`koan::HidesSidebarTrailingControls`) | `kOrganizerPanel` is **disabled by default**, so not reachable unless Finch or flags turn it on | none |
| IPH: `kIPHVerticalTabsExpandOnHoverFeature` (shown 3 s after any `RequestCollapse` and after 10 min while collapsed, if the hover pref is default), `kIPHVerticalTabstripTutorialFeature`, `kVerticalTabsNewBadge` | `vertical_tab_strip_state_controller_impl.cc:401-414`, `vertical_tab_iph_controller.cc`, `features.cc:35` | None | **Probably yes.** Compact mode *is* a collapse, so `RequestCollapse(true)` starts the 3 s IPH timer every time compact hides the sidebar *(live check: whether user education shows it in a Chromium-branded build)* | n/a |
| Delayed-switch toasts `kTabStripSwitchDelayedVertical/Horizontal` (mode change while an enable lock is held, e.g. immersive fullscreen) | `MaybeShowDelayedToast()` | None | Yes: toggling the mode in fullscreen shows Chromium's toast, then switches on exit | n/a |

### 2.2 Commands, actions, accelerators

`kUseActionsForBrowserCommands` is **enabled by default** in M158 (`ui_features.cc:23`). With it on, `BrowserCommandController` uses `CommandActionUpdater`, and every `IDC_*` mapped in `chrome_action_id.h` runs **the window's ActionItem callback** (`command_action_updater.cc`, `ExecuteAction`). That covers the menu bar, accelerators, `chrome::ExecuteCommand` and `TabStripModel::CommandToggleVertical`. It does not cover the legacy switch in `HandleCommandWithDisposition`. So re-pointing an action's callback per window is a patchless redirect for every command path. It does **not** cover direct pref writes.

| Upstream item | Where | Koan today | Reachable? | Zen |
|---|---|---|---|---|
| `IDC_TOGGLE_VERTICAL_TABS` (34058) / `kActionToggleVerticalTabs` → `chrome::ToggleVerticalTabs()` → `SetVerticalTabsEnabled(!…)` → pref | `browser_actions.cc:1175-1195`, `browser_commands.cc:2601`, `browser_command_controller.cc:740,1850` | **None** | **Yes:** tab menu, Mac View menu, ⋮ > More tools (#148) | None (locked) |
| `IDC_TOGGLE_VERTICAL_TABS_COLLAPSE` (34061) / `kActionToggleCollapseVertical`, accelerator **⇧⌘L** (`accelerators_cocoa.mm:117`, `main_menu_builder.mm:314`, the ActionItem's own accelerator) | `browser_actions.cc:~1215-1243`, `browser_commands.cc:2619` | develop: **None** (collapses to the rail, #132). PR #143: **Redirected** to `ToggleCompactMode()` (or expands a rail first) | develop: yes. With #143: only through the legacy path if `kUseActionsForBrowserCommands` were ever off (Finch or `--disable-features`) | Top-row button = `cmd_toggleCompactModeIgnoreHover`. Zen's own collapse is `cmd_zenToggleSidebar`, **no default key** |
| `IDC_TOGGLE_VERTICAL_TABS_EXPAND_ON_HOVER` (34060) / `kActionToggleVerticalTabsExpandOnHover` | `browser_actions.cc:1197-1207`, collapse-button context menu | None | **Yes** (collapse-button context menu, which is Koan's toggle) | none |
| `IDC_VERTICAL_TABS_SEND_FEEDBACK` (34059) | `browser_command_controller.cc:761` | None | Only from the Windows/Linux system menu; not on Mac | none |
| `IDC_TAB_SEARCH` ⇧⌘A / `kActionTabSearch`. Bubble host anchored to the vertical combo button's end button (`BrowserView::UpdateTabSearchBubbleHost`, `browser_view.cc:4316-4326`) | | PR #143 **hides** the combo button; the shortcut stays | **Yes:** ⇧⌘A opens Chromium's tab search bubble anchored to a hidden view *(live check: placement, crash-free)* | ⇧⌘A = Firefox's "search tabs" (Zen keeps it) |
| Mac `BrowserNativeWidgetMac::ValidateUserInterfaceItem` for both items: `IDC_TOGGLE_VERTICAL_TABS` hidden only in immersive, checkmark = vertical; `…_COLLAPSE` hidden when not vertical, check = collapsed | `browser_native_widget_mac.mm:560-590` | Koan's patch to this file only touches `IDC_TOGGLE_FULLSCREEN_TOOLBAR` | Both items are **visible in View** in a normal window. "Collapse Vertical Tabs" shows a check while compact is on (compact = collapsed) | View menu: Appearance, Toolbars, Sidebar, Collapse Pinned Tabs, …, no vertical/horizontal item |
| Mac Tab menu swap (`AppController onVerticalTabStripModeChanged`, two copies of "New Tab to Right/Below") | `app_controller_mac.mm:1143-1190` | None | Follows the mode; harmless once the mode is locked | n/a |
| Windows/Linux system menu items | `system_menu_model_builder.cc:230-264`, `system_menu_model_delegate.cc`, `BrowserView::ExecuteWindowsCommand` | None | **Not on macOS** (product is Mac-only); listed for completeness | n/a |

### 2.3 Menus, settings rows, UI elements

| Upstream item | Where | Koan today | Reachable? | Zen |
|---|---|---|---|---|
| Tab context menu "Show tabs horizontally / vertically" (`TabStripModel::CommandToggleVertical`, separator before it, New badge) | `tab_menu_model.cc:650-676`, `tab_strip_model.cc:3286` | None. Koan's `AddKoanTabMenuItems` hook runs at the end of `Build()` but leaves it | **Yes**, on tabs and Essentials (#148) | none (context-menus.md §8) |
| App menu ⋮ > More tools > "Show tabs horizontally/vertically" (+ New badge); glow-up menu `action_app_menu_manager.cc:1113-1136` | `app_menu_model.cc:1078-1096` | None | Whenever Chromium's toolbar is visible: `Mode::kNone` (Ctrl+Alt+B "Sidebar and Top Toolbar", rail, fullscreen with the rail) | none |
| chrome://settings > Appearance "Tab strip position" dropdown + "Expand tabs on hover" toggle (settingsPrivate allowlist `prefs_util.cc:288-291`) | `appearance_page.html.ts:164-182` (unconditional row) | None. chrome://koan-settings even links to chrome://settings | **Yes**, one click | Zen hides Firefox's toggles (`icons.css:869` `#toolbar-context-toggle-vertical-tabs {display:none}`), and its `ToolbarContextMenu` patch sets `showSidebarActions = false` |
| NTP / What's New browser command `EnableVerticalTabs` | `browser_command_handler.cc:410` | None | Only *enables*; harmless | n/a |
| Header: collapse button (`kActionToggleCollapseVertical`) **with its own context menu** ("Expand tabs on hover", `VerticalTabStripTopContainer::ShowContextMenuForViewImpl`) | `vertical_tab_strip_top_container.cc:54-55, 385-450` | Button kept as Koan's toggle; moved clear of the traffic lights (`koan::AvoidTrafficLights`). **The context menu is upstream's** (the button's controller wins over Koan's region-level `KoanSidebarContextMenuController`) | **Yes**, right-click the sidebar toggle | right-click = toolbar context menu (Enable compact mode, …) |
| Header: combo button (tab groups + tab search), segmented control | top container | Develop: hidden on macOS only via `HidesSidebarTrailingControls`. PR #143: combo hidden in the ctor and skipped in 3 layout places | With #143: not visible. Upstream still creates it and `UpdateTabSearchBubbleHost` still anchors to it | none |
| Bottom container (new-tab button) | `vertical_tab_strip_bottom_container.cc` | **Hidden** by class name (no patch) | No | Zen's "+ New Tab" row is in the list (Koan's rows) |
| Collapsed rail (56 px, icon-only tabs, vertical combo) | region `CalculateProposedLayout`, top container vertical orientation | Not intentionally offered since #143, but **still the state Koan uses for compact** and the state any leak lands in | Via 2.1 persisted state and 2.2 legacy paths, the drag below with compact disabled, and fullscreen | Zen's own icons-only "Collapsed Sidebar" layout with Zen's top toolbar (`zen.view.sidebar-expanded=false`, `use-single-toolbar=false`, `ZenVerticalTabsManager.toggleExpand()`) |
| Resize area: drag, double-click, keyboard ±50; snap to default within 15 px; snap to collapse below the snap width | region `OnResize`, `OnKeyPressed`, `Layout` | **Hooked:** side (tabs on right), limits, and a drag collapse from expanded calls `koan::OnSidebarDragCollapsed` → compact on + toast (Zen) | With `koan-compact-mode` disabled, or compact already on, a drag below the snap width collapses to the **rail**. Resize area is visible on a rail when expand-on-hover is off | splitter drag closed → compact + "Sidebar hidden!" toast |
| Collapse/expand animation `TabStripAnimations::kVerticalTabStrip` (kCollapse/kExpand/kTabStripWidth/kTabStripHoverWidth), `BrowserAnimationController` | region `OnAnimationProgressed`, `CalculatePreferredSize`; layout `CalculateVerticalTabStripAnimation` | **Hooked** (tabbed-layout patch): single-reflow clip branch forced on Mac, `SetTargetContentBounds(nullopt)` reset (#131/#142), compact zero width, reveal via layer transform | Live and Koan-driven. #131 was upstream's Mac path (continuous reflow) never resetting target bounds | 0.1 s toggle, 0.25 s spring reveal (compact-mode.md §2-3) |
| Focus-mode swipe controller (Space swipe) | `vertical_tab_strip_focus_swipe_controller.cc` | Ordered behind Koan's handlers (no patch) | n/a (Spaces) | ZenSpacesSwipe |
| Immersive/fullscreen: `OnImmersiveFullscreenEntered` takes `GetEnableStateLock()`; mode toggle hidden in immersive; `ReparentTopContainerFor{Start,End}OfImmersive` | `browser_view.cc:6022-6030` | Koan mirrors the reparenting and takes the top container back (#128); `UpdateFullscreenTopUI` patch | Rail in fullscreen → Chromium's toolbar reveal (documented as intended in #128) | Fullscreen keeps Zen's sidebar; compact → borderless (fullscreen-and-notch.md) |
| Side panel interplay: `kSidePanelHorizontalAlignment`; layout clip branch skipped while a side panel animates | tabbed layout | Koan's single-reflow branch excludes `side_panel_is_animating` | Side panel opposite tabs-on-right is untested | Zen has no Chromium side panel |
| Mode switch rebuild: `BrowserView::OnVerticalTabStripModeChanged` → `ResetTabStrip()` / `InitializeTabStrip()` (destroys the `TabStripView`) | `browser_view.cc:1605-1628` | `KoanSidebarTabsController::OnModeChanged` re-adds rows; **the Space header, Essentials hooks etc. are not re-installed** (#148 second half) | Every mode toggle | n/a |
| Extension APIs | `settingsPrivate` (WebUI only); there is no `chrome.tabs`/`windows` vertical-tabs API in 158 (`tabs_apitest.cc` mentions only an Android IPH) | n/a | Extensions can't flip the mode. `chrome.tabGroups.update({collapsed})`, `tabs.group/ungroup` act on Spaces (a separate audit) | n/a |

### 2.4 Teardown and crashes (the "#128 crash")

The window-close crashes came after #128: #130, where `Deactivate()` → `RequestCollapse(false)` runs after `BrowserWindowFeatures::TearDownPreBrowserWindowDestruction()` has reset the animation controller, and #153, where `UpdateCaptionButtons()` → `Widget::IsFullscreen()` runs on a dead widget. Both happen because Koan **mutates upstream's strip state from `~BrowserView`**. PRs #129 (`destroying_`, deferred lock release) and #152 (`koan::IsWindowFullscreen`) patch the symptoms. The structural rule should be that Koan never calls `RequestCollapse`, releases an `ExpandOnHoverLock` synchronously, or reads widget state during teardown. Only one owner of the upstream state can enforce that rule.

---

## 3. Recommendation

### 3.1 Option A: keep upstream's strip; one Koan layer owns every upstream state and entry point

**Design.** Add `src/browser/ui/views/koan_vertical_tabs_policy.{h,cc}`, owned per window by `KoanBrowserViewController`, plus a few free hook functions. It is the only Koan code allowed to call `VerticalTabStripStateController` setters. `KoanCompactModeController`, `koan_workspaces.cc` and `koan_sidebar_tabs.cc` ask it instead (`policy->HideSidebar()`, `ShowSidebar()`, `HoldOpen()`). Its rules:

1. In a normal window with Koan's sidebar, vertical tabs are **always on**: no horizontal mode, as Zen locks `sidebar.verticalTabs`.
2. Upstream's collapsed state is **Koan-internal**: it means "compact mode hides the sidebar" and nothing else. It is never shown as a user-visible icon rail and never persisted.
3. Expand-on-hover is **always on** under Koan.
4. Every upstream command and menu item for these states is removed or redirected to the Koan equivalent.
5. No state changes during teardown.

Concrete changes and the minimal hook for each:

| # | Change | Hook | New patch? |
|---|---|---|---|
| A1 | `ShouldDisplayVerticalTabs()` returns true when `koan::ForcesVerticalTabs()` is true (Koan sidebar feature on; the controller exists only for tabbed windows). `SetVerticalTabsEnabled()` and `OnModeChanged()` return early then (no `NotifyModeWillChange`, no rebuild, no toast). `IsExpandOnHoverEnabled()` returns true then | 4 one-line `koan::` calls in `chrome/browser/ui/tabs/vertical_tab_strip_state_controller_impl.cc` + one dep line in the `impl` target of the already patched `ui/tabs/BUILD.gn` | **yes, 1 file** |
| A2 | Collapse is not persisted: `UpdatePrefService()` / `UpdateSessionService()` write `collapsed=false` under Koan (width still saved). In the ctor, the restored `collapsed` is replaced by `koan::InitialSidebarCollapsed(profile)` (= compact on and hiding the sidebar), so a compact window still starts hidden **without animation**, and an old pref or session value can't produce a rail. This makes `koan.compact_mode.sidebar_collapsed_before`, `collapsed_before_`, `opened_in_compact_mode_` and most of `IsSidebarCollapsedByUser()` dead code: delete them | same file as A1 (3 more call sites) | (A1's) |
| A3 | Migration: on profile load, `ClearPref` for `vertical_tabs.enabled`, `.collapsed_state`, `.expand_on_hover` (cosmetic once A1/A2 exist; keeps chrome://settings truthful) and a `PrefChangeRegistrar` that clears them again if chrome://settings writes them | `src/` only (`koan_prefs.cc` / the policy) | no |
| A4 | Per window, re-point the action callbacks: `kActionToggleVerticalTabs` → no-op + `SetVisible(false)`; `kActionToggleVerticalTabsExpandOnHover` → no-op; `kActionVerticalTabsSendFeedback` → no-op; `kActionToggleCollapseVertical` → Koan toggle (take #143's `InstallSidebarToggleAction` into the policy). Force `kUseActionsForBrowserCommands` on in `koan_feature_overrides.cc`, so a Finch or flag change can't revive the legacy switch (A1 still catches that path for the mode) | `src/` only | no |
| A5 | Tab context menu: remove `CommandToggleVertical` and the separator before it in `AddKoanTabMenuItems` (`koan_move_to_space.cc:109` already does `RemoveItemAt` by command id) | existing hook | no |
| A6 | Mac menu bar: in Koan's `ValidateUserInterfaceItem` hunk, set `IDC_TOGGLE_VERTICAL_TABS` and `IDC_TOGGLE_VERTICAL_TABS_COLLAPSE` hidden **after** upstream's switch (upstream un-hides the first one outside immersive, so removing the NSMenuItem once is not enough). Later: a Zen "View > Sidebar >" submenu from Koan | extend `browser_native_widget_mac.mm.patch` by ~4 lines | no (existing) |
| A7 | ⋮ > More tools item: hidden through A4's `SetVisible(false)` in the glow-up action menu. The legacy `AppMenuModel` item reads only `IsCommandEnabled` → set the action disabled too (greyed). Hiding it fully needs one line in `app_menu_model.cc`. Defer: ⋮ is visible only in `Mode::kNone`, which A10 shrinks | `src/` (+ optional 1-line patch) | optional |
| A8 | Collapse button's context menu: `collapse_button->set_context_menu_controller(koan_sidebar_menu)` when Koan installs the header (the button is found like the other header views) | `src/` | no |
| A9 | Header: keep #143's combo hide, and make `HidesSidebarTrailingControls` unconditional (not Mac-only). Redirect `kActionTabSearch` (⇧⌘A) to the command palette's tab search, so upstream's bubble never anchors to the hidden combo | existing patch + `src/` | no |
| A10 | `Mode::kNone` never shows Chromium's toolbar *because of the strip*: a collapsed strip no longer means "Collapsed Sidebar layout" (A2 makes it impossible), so `DesiredSingleToolbar()` depends only on `koan.single_toolbar`. Zen's Collapsed Sidebar layout becomes a separate, later Koan state (its own pref, mirroring `zen.view.sidebar-expanded`, with a Koan top toolbar). Mark it Missing in `ZEN_PARITY.md` (row 24 now claims "Same") | `src/` | no |
| A11 | Drag/keyboard resize: under Koan, a resize that ends below the snap width **always** goes to `koan::OnSidebarDragCollapsed` and never leaves a rail. With compact off, compact goes on (as now). With compact already on, the strip snaps back to its last width. That needs `OnSidebarDragCollapsed` to return "handled" so the region skips `RequestCollapse`: a 1-line change inside the existing `OnResize` hunk | existing patch | no |
| A12 | IPH and badges off: `DisableFeature` for `kIPHVerticalTabsExpandOnHoverFeature`, `kIPHVerticalTabstripTutorialFeature`, `tabs::kVerticalTabsNewBadge` | `koan_feature_overrides.cc` | no |
| A13 | Teardown rule in one place: the policy sets `destroying_` in `OnBrowserViewDestroying` (first), drops every lock with `koan::ReleaseExpandOnHoverLockSoon`, and refuses `HideSidebar/ShowSidebar`. Fold #129's and #152's guards into it | `src/` | no |
| A14 | A `DCHECK`-only "leak detector": subscribe to `RegisterOnModeWillChange` / `RegisterOnCollapseChanged` and `DLOG(FATAL)` if the mode changes, or a collapse happens that the policy did not request. Every future upstream entry point then fails the Mac loop instead of reaching users | `src/` | no |

**Cost.** About 2-4 agent-days, one Mac build. 1 new hook patch (state controller impl, about 10 lines), 2 existing patches extended a few lines, about 400-600 lines in `src/`, and roughly 300 lines of compact code deleted (A2).

**Rebase burden per Chromium update.** +1 small patch file. The risky dependencies stay as they are: the tabbed-layout patch (animation and clip insets), the region patch (`OnResize`), and the top-container patch. A14 turns silent upstream additions (a new menu item, a new command) into loud failures. Expect 0-1 conflicts per milestone in the new file. Upstream marks this code as moving (TODOs crbug.com/465833741, 465832180, 475222200; `kTabStripUnification`), so recheck `docs/REBASE.md` rows for A1 and A6 on every roll.

**Risk.**
- Low for A1-A9 and A12 (they disable things).
- Medium for A2 and A10. They change how compact starts, and they remove the rail "layout" that `scripts/e2e/collapsed_sidebar.mjs` exercised.
- The residual structural risk stays: compact is still built on upstream's collapse animation and hover locks. Bugs like #131 (upstream's Mac animation path) can recur when upstream changes `CalculateVerticalTabStripAnimation`.

**Bugs eliminated.**
- #148: A1 + A5 + A6 + A7. The Space-header loss disappears too, because the strip is never rebuilt.
- #132: A4 (= #143) + A2 + A10. No startup rail, and no Chromium toolbar from a rail.
- #130 / #153: A13, and the class of bug, because only one place touches upstream state during teardown.
- The hidden items in 2.1 / 2.3: the expand-on-hover switch, the chrome://settings row, ⇧⌘L via the View menu, session-restored rails, and IPH bubbles in compact.
- #131: mitigated only (it stays fixed by #142's hook).
- #133 / #146 / #149 (traffic lights, fullscreen) are Mac title-bar and top-container issues. Neither option fixes them by itself.

### 3.2 Option B: replace upstream's strip with Koan's own sidebar view

The realistic form is a Koan `KoanSidebarRegionView : TabStripRegionView` (15 pure virtuals; 9 upstream files name `VerticalTabStripRegionView` directly: `browser_view.{h,cc}`, `browser_view_layout.h`, `profile_indicator_icon.cc`, `organizer_panel_host.cc`, the expand-on-hover lock, the focus swipe controller). BrowserView would construct it instead of upstream's (`browser_view.cc:993`, a 1-hook patch). It would still host upstream's `TabStripView` (tabs/common) for rows, drag and drop, groups and Essentials. Rewriting those as well ("full B") would mean re-implementing tab drag (`tab_drag_handler`), the collection controller, hover cards, accessibility, link drops (`BrowserRootView`), group focus mode and pinned containers. That is months of work and is not recommended.

Even B-lite needs:
- A1 anyway. `ShouldDisplayVerticalTabs()` still decides whether BrowserView shows the horizontal strip, and every entry point in 2.2/2.3 still flips it. **B does not fix #148 on its own.**
- Its own collapse/compact state and animation (fork of region `OnResize`, `UpdateExpandOnHoverState`, `RequestCollapse`, the velocity heuristic: about 1.2k lines), its own header (top container: about 0.5k), and its own bottom area.
- New hooks in `BrowserViewTabbedLayoutImpl` to size and clip a non-upstream strip (`vertical_tab_strip_collapsed_state` comes from the upstream controller today), and in `BrowserFrameViewMac::OnTabStripStateChanged` / traffic-light centring (they read `IsVerticalTabStripCollapsed()`).
- Re-wiring of everything that finds the region by class name today: Library `GetArea`, workspace switcher, Spaces bar and media mounts, the swipe controller and its handler order, `BrowserView::tab_strip_view()` users such as hover cards, tab search host and immersive.

**Cost.** About 3-5k new lines, 3-5 agent-weeks with several Mac test rounds, plus a 2-3 week period of regressions in Spaces, folders, drag and the Library.

**Rebase burden.** It goes down for behaviour: upstream region and animation changes no longer reach Koan. It goes up for interfaces: `TabStripRegionView` and `TabStripView` APIs are young and changing, so a forked region breaks at compile time when they move, and Koan stops receiving upstream fixes (a11y, RTL, touch, perf).

**Risk.** High: a broad regression surface for a small product team in a 3-week parity push (#114).

**Bugs eliminated.**
- #132, #131, #130/#153 by construction (Koan owns animation and lifetime).
- #148 only together with A1.
- The same nothing on #133/#146/#149.

### 3.3 Verdict

Do **A now**, as one PR, after #143, #129 and #152 land. It folds them in, and A2 makes their workarounds deletable. The user's request to "override Chromium's vertical-tab behaviour to make it Zen-style" *is* A: Zen did the same to Firefox. It locked `sidebar.verticalTabs` and `sidebar.revamp`, hid the menu items, and kept Firefox's tab container.

Reconsider B-lite (region view only, never the tab rows) only if one of these happens:
- the tabbed-layout, region or top-container patches conflict on 2 consecutive milestones;
- upstream reworks `kVerticalTabStrip` animation or `ExpandOnHoverLock`;
- Koan needs a sidebar geometry upstream's region cannot host, for example Zen's 10 pt floating margin and rounded floating compact panel, which today is approximated by a layer transform.

A14's leak detector is what makes A safe to keep.

---

## 4. Tests that prove no upstream vertical-tab state is reachable

Run on the Mac loop (DCHECK build so A14 fires). Each test asserts: `VerticalTabStripStateController::ShouldDisplayVerticalTabs()` stays true; no `RegisterOnModeWillChange` callback runs; `IsCollapsed()` is true **only** while `koan.compact_mode.enabled` hides the sidebar; Chromium's toolbar (`ToolbarView`) is not drawn unless `koan.single_toolbar` is false; the region still contains Koan's Space header and rows. Seeded profiles go in `scripts/e2e/prefs/`.

**Old-profile prefs and session**
1. Profile with `vertical_tabs.enabled=false`: launch → Koan sidebar, no horizontal strip; the pref reads as default afterwards.
2. `vertical_tabs.collapsed_state=true`, `uncollapsed_width=56`, compact off: new window → expanded sidebar at Zen width (clamped 163..500), no rail, no Chromium toolbar.
3. `vertical_tabs.expand_on_hover=false` + compact on: the hot-zone reveal stays out while the pointer is over the sidebar for 3 s (real pointer).
4. Session file with window extra data `vertical_tab_strip_collapsed=true` (from a pre-A build, compact on at quit, then compact pref edited off): relaunch and Restore → expanded, no rail. Same via Cmd+Shift+T on a closed window (`browser_live_tab_context`).
5. Crash while compact hides the sidebar (kill -9) → relaunch → "Restore pages": compact restored hidden without animation; turn compact off → expanded.
6. A profile copied from Google Chrome with vertical tabs collapsed and on the right (`side_panel` alignment) → Koan layout, Koan's tabs-on-right pref is the only side switch.

**Menus**

7. Right-click a tab, an Essential, a pinned tab, a tab in a folder, and a multi-selection: no "Show tabs horizontally/vertically" item or orphan separator (dump the model).
8. Mac menu bar View: no "Show Tabs Vertically" and no "Collapse Vertical Tabs" in a normal window, in fullscreen, after leaving fullscreen, and after switching windows (validation re-runs).
9. ⋮ app menu (Ctrl+Alt+B layout) > More tools: no tab-strip item, or a disabled one until A7's 1-line hook.
10. Right-click the sidebar toggle: Koan's sidebar menu, no "Expand tabs on hover".
11. Right-click the empty sidebar, the Space header, the Space bar: only Koan menus.
12. chrome://settings/appearance: change "Tab strip position" to Top → nothing happens in any window, the dropdown snaps back (A3); toggle "Expand tabs on hover" off → compact reveal still works.

**Shortcuts and commands**

13. ⇧⌘L: toggles compact mode (as ⌘S does), never a rail. Test in a normal window, in fullscreen, with the Library open, and with the URL bar focused.
14. ⇧⌘A: Koan's tab search (palette), no Chromium bubble.
15. `chrome::ExecuteCommand(browser, IDC_TOGGLE_VERTICAL_TABS)`, `IDC_TOGGLE_VERTICAL_TABS_EXPAND_ON_HOVER`, `IDC_VERTICAL_TABS_SEND_FEEDBACK` from a test hook (or the command palette's debug runner): no state change. Repeat with `--disable-features=UseActionsForBrowserCommands` (legacy switch path, caught by A1).
16. `TabStripModel::ExecuteContextMenuCommand(i, CommandToggleVertical)` directly: no mode change.

**Drag and keyboard on the resize area**

17. Drag the edge from expanded to below the snap width → compact on, toast once; never a 56 px rail. Same with tabs on the right.
18. With compact already on and the sidebar revealed: drag the edge below the snap width → snaps back, no rail, compact stays.
19. Focus the resize area (Tab/F6) and press Left repeatedly → same as 17; Right → widens to 500 max.
20. With `--disable-features=KoanCompactMode`: drag below the snap width → snaps back (no rail).

**Upstream internals and teardown**

21. Compact on and off 20 times in 10 s, then close the window mid-animation, with the sidebar revealed or pinned (⌘⌥S), with a menu open, and with the new-Space form open: no crash (#129/#152/#153 paths), and the other window is unchanged.
22. Enter and exit native fullscreen with compact on and off: no mode change toast (`kTabStripSwitchDelayed*`), no rail, no Chromium toolbar at the top edge.
23. Compact hides the sidebar, wait 15 s (> the 3 s and 10 min IPH timers via a test clock): no "expand on hover" IPH bubble.
24. The A14 detector is silent for the whole e2e suite (`compact_*`, `sidebar_*`, `spaces_*`, `fullscreen_*`, `window_close`).

**Extensions**

25. A test extension calling `chrome.settingsPrivate` is impossible (WebUI only). Instead, the extension runs `chrome.windows.create({type:'normal'})` → Koan sidebar; `chrome.windows.create({type:'popup'})` → no sidebar, no crash; `chrome.tabGroups.update(spaceGroup, {collapsed:true})` and `chrome.tabs.ungroup` on a Space's tabs → Koan's Space state stays consistent (scope of a Spaces audit, run here because it uses the same strip).
26. An extension `chrome.commands` binding on ⇧⌘L: Koan's binding wins or the extension's does, consistently (document which); no rail either way.

---

## Appendix: key file:line references (pristine 158.0.8127.2)

- State: `chrome/browser/ui/tabs/vertical_tab_strip_state.h`, `vertical_tab_strip_state_controller{,_impl}.{h,cc}`; controller creation `browser_window/internal/browser_window_features.cc:607-627`.
- Prefs: `chrome/common/pref_names.h:1244-1267`, `tab_strip_prefs.cc:28-35`, settings allowlist `extensions/api/settings_private/prefs_util.cc:288-291`, WebUI `resources/settings/appearance_page/appearance_page.html.ts:164-182`.
- Commands: `chrome_command_ids.h:86-89`, `actions/chrome_action_id.h:280-284`, `browser_actions.cc:1175-1260`, `browser_commands.cc:2601-2628`, `browser_command_controller.cc:740-770,1850-1855`, `actions/command_action_updater.cc`, `ui_features.cc:23,164`.
- Accelerators and menus: `cocoa/accelerators_cocoa.mm:117`, `cocoa/main_menu_builder.mm:312-316`, `views/frame/browser_native_widget_mac.mm:560-590,667-690`, `tabs/tab_menu_model.cc:650-676`, `tabs/tab_strip_model.cc:2849,3286`, `toolbar/app_menu_model.cc:1078-1096`, `app_controller_mac.mm:1143-1190`.
- Views: `views/frame/vertical_tab_strip_region_view.cc` (`OnResize` 741, `RequestCollapse` 818, `UpdateExpandOnHoverState` 988, `GetExpandOnHoverLock`), `views/tabs/vertical/vertical_tab_strip_top_container.cc:42-80,385-450`, `views/frame/browser_view.cc:985-1000,1338-1346,1605-1628,4316-4326,6022-6030`.
- Session: `sessions/session_service_base.cc:784-790`, `sessions/session_restore.cc:1368-1383`, `ui/browser_live_tab_context.cc:221,553`.
- Koan: `src/browser/ui/views/koan_compact_mode_controller.cc` (`Activate` 919, `Deactivate` 1119, `DesiredMode` 779, `IsSidebarCollapsedByUser` 799, `OnSidebarDragCollapsed` 666), `koan_sidebar_tabs.cc:455-500`, `koan_sidebar_animation.h`, `koan_tab_menu.cc:100`, patches `tab_strip_prefs.cc`, `vertical_tab_strip_region_view.cc`, `vertical_tab_strip_top_container.cc`, `layout/browser_view_tabbed_layout_impl.cc`, `browser_native_widget_mac.mm`.
- Zen 1.24b: `prefs/firefox/browser.yaml` (`sidebar.verticalTabs` / `sidebar.revamp` locked false), `src/browser/themes/shared/zen-icons/icons.css:869`, `src/browser/components/customizableui/ToolbarContextMenu-sys-mjs.patch`, `src/zen/common/modules/ZenVerticalTabsManager.mjs:59-63` (`toggleExpand`), `src/zen/common/sys/ZenCustomizableUI.sys.mjs:91-96` (top-row toggle = `cmd_toggleCompactModeIgnoreHover`), `src/browser/components/preferences/zen-settings.js:648-700` (layouts), `.agent/notes/zen-behaviour/compact-mode.md`, `keyboard-shortcuts.md:34` (`cmd_zenToggleSidebar` unbound).
