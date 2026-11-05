# Upstream-behaviour audit 3: split view, Glance, Library, media, pinned/Essentials, folders

Date: 2026-11-05. Chromium 158.0.8127.2 (`681cd621e8`), Koan `develop` `dcf9bdaa`, plus the open PRs #60 (folders, head 2dd65ded), #150 (pinned section, head b78d5b75), and for cross-reference #90 (Mac menus) and #159 (Spaces lockdown). This was a read-only pass, using the same method as `vertical-tabs.md` (**A** ids) and `tab-groups-omnibox-fullscreen.md` (**G** = groups, **O** = omnibox, **F** = fullscreen). Pristine Chromium came from `git show HEAD:` / `git grep HEAD`, Zen from `.cache/zen-desktop-1.24b`. Nothing was built or run, so every verdict marked *(live check)* still needs a run on the Mac.

**Id scheme in this file.**

| Area | Change ids | Mac test ids |
|---|---|---|
| 1 Split view | SV1… | S1… |
| 2 Glance | GL1… | GT1… (not "G": G is audit 2's tab-group ids) |
| 3 Library / history / downloads / bookmarks | LB1… | L1… |
| 4 Media / PiP | MD1… | M1… |
| 5 Pinned / Essentials | PN1… | P1… |
| 6 Folders | FD1… | FO1… (not "F": F is audit 2's fullscreen ids) |
| 7 What Zen suppresses in Firefox | Z54… (Z1–Z53 are in `zen-firefox-overrides.md`) | inline |

## Summary

The pattern is the same as in audits 1 and 2: Koan reuses upstream's mechanism (split tabs, the tab collections, pinned state, the download/media/PiP stacks) but leaves that feature's *other entry points and side effects* live.

- **Split view = upstream split tabs, relabelled.** Every "new empty split" entry point, Koan's own included, ends in `chrome::NewSplitTab`, which loads Chromium's tab-search split NTP in the second pane (SV1). The panes keep Chromium's mini toolbar, radius and outline (SV9/SV10). A merged bug in the Cmd+Opt+V/H/G shortcut can pull the next Space's first tab into the current Space (SV2), and a split from an Essential makes the partner an Essential (SV3). Session restore of 2-pane splits is upstream's and fine (reuse).
- **Glance has no upstream rival.** Chromium 158 has no desktop link preview / peek (it was removed), so nothing competes with Option-click. Glance is not a tab, though: browser shortcuts act on the parent (Cmd+W closes the parent, GL2), it has no tab helpers (prompts, autofill, file pickers and DOM fullscreen fail, GL3, needs a decision), and the controller holds a raw pointer that dangles on a WebContents replace (GL4, crash class).
- **Library (#64) catches the keys, not the commands.** The Mac History / Window menus, ⌥⌘L, the app menu and "See all downloads" still open `chrome://history` / `chrome://downloads` (LB1). Chromium's partial download bubble auto-opens after every finished download, anchored to a hidden button (LB3/LB5), and dangerous downloads are handled only by Chromium's UI (LB4). No crash was found in #64/#66.
- **Media (#66).** Auto-PiP fires on Space switches (a Space switch is a tab switch; Zen ships it off: MD2). Ctrl+M runs `IDC_WINDOW_MUTE_SITE`, a permanent per-site block (MD3). Chromium's media toolbar button can show beside Koan's card (MD1). Media in a frozen background Space may stop responding (MD4 / G2, live check).
- **Pinned / Essentials (#59, #150).** A transient ungroup (Mac Tab menu, ⌃⇧PgUp/PgDn, extensions) makes Koan's repair clear a Space-pinned tab's pinned state and URL (PN2). "Remove from Essentials" on the active Essential briefly exits focus mode and drops the tab at the bottom (PN1). The `pinned_tabs` startup pref restores bare Essentials and loses Space-pinned tabs (PN4, with G9). Extensions pin = Essential (PN3).
- **Folders (#60).** M158 has **no nested groups** (a group inside a group crashes), so folders must be Koan metadata, as #60 does. But #60 cannot merge (its two layout patches target files renamed by #89; 33 commits behind: FD1), it unpins folder tabs where Zen pins them (FD2), and it fights #150 in the same upstream layout loop with a second normaliser (FD3/PN6).

## Highest-risk items, all areas (ranked)

| # | Severity | Item | Where it lives | Fix |
|---|---|---|---|---|
| 1 | Blocker | #60 targets files that no longer exist in 8127.2 | PR #60 | FD1 (port) |
| 2 | High (crash class) | Glance keeps a raw parent pointer across a WebContents replace | merged `koan_glance_controller.cc` | GL4 |
| 3 | High | Cmd+W in a Glance closes the parent; Cmd+R/F/L/[ act on the parent | merged Glance | GL2 |
| 4 | High (bug) | Split shortcut pulls a tab across Spaces | merged `koan_shortcuts.cc:419-423` | SV2 |
| 5 | High (looks like Chromium) | New split opens Chromium's tab-search split NTP; mini toolbar on every pane | merged split + upstream | SV1, SV9, SV10 |
| 6 | High | History/Downloads commands open Chromium WebUI; download bubble auto-opens | merged #64 | LB1, LB3+LB4, LB5 |
| 7 | High | Auto-PiP on Space switch; Ctrl+M = permanent site mute | merged #66 / defaults | MD2, MD3 |
| 8 | High (parity) | Folder tabs unpinned, folders below the separator | PR #60 vs #150 | FD2 |
| 9 | High (structural) | Two row injectors + two normalisers in one upstream layout loop | PR #150 + PR #60 | FD3 / PN6 |
| 10 | Medium-high | Space-pinned state silently cleared after a transient ungroup (folders too) | merged #59, PR #60 | PN2, FD4 |
| 11 | Medium-high | Split from an Essential creates a second Essential | merged split/Glance | SV3 |
| 12 | Medium | Non-"continue" startup loses Space-pinned tabs, folders, Koan Essential state | upstream `pinned_tabs` pref | PN4 + FD6 + G9 |
| 13 | Medium | Glance has no tab helpers | merged Glance | GL3 (decision) |
| 14 | Medium | Pinned/Essential cross-domain links don't open in Glance (Zen does) | missing | Z54 |

## What must change

**Merged code (develop):** SV1 (redirect `chrome::NewSplitTab` in the existing `browser_commands.cc.patch`), SV2, SV3, SV9/SV10, GL2, GL4, a GL3 decision, LB1 (redirect the commands in `browser_command_controller.cc.patch`), LB3+LB4+LB5, LB6, LB2, MD1 (one-line patch in `MediaToolbarButtonView::Show()`), MD2 (auto-PiP default Block), MD3, MD4 (with G2), PN1, PN2, PN3, PN4 (with G9).

**PR #150:** take one shared "pinned section" rule and one layout hook that #60 can also use (FD3/PN6); don't move rows during a temporary focus exit (PN6).

**PR #60:** port onto develop after #57/#89/#150 (FD1); keep folder tabs pinned and folders in the pinned section, like Zen (FD2); share #150's layout hook and ordering pass (FD3); survive transient ungroups (FD4); persist outside the session file (FD6); Zen input parity (FD7); redo the Xvfb drag test as Mac checks (FD9).

**Cross-cutting with audits 1 and 2:** SV1, LB1, MD3, GL2 and the shortcut fixes belong in the single command-policy table (audit 2 §0.1). The Mac menu items (Tab > New Split View, Pin Tab tick, History/Window > Downloads, View > Always Show Bookmarks Bar, View > Cast) belong in the one `browser_native_widget_mac.mm.patch` hunk (audit 2 §0.2).

---

## 1. Split view vs upstream split tabs

Date: 2026-11-05. Chromium 158.0.8127.2 (`681cd621e8`), Koan `develop` `dcf9bdaa`. This was a read-only pass: pristine Chromium came from `git show HEAD:` / `git grep HEAD`, and nothing was built or run. Every verdict marked *(live check)* still needs a run on the Mac. Unless a path says otherwise, Koan paths are under `src/browser/ui/views/`.

**Summary.** Koan's split view is upstream's split tabs with three changes:
- new labels in the tab and page context menus;
- a "Split Out Tab" item;
- Zen's keyboard shortcuts mapped onto `AddToNewSplit` / `UpdateSplitLayout` / `RemoveSplit`, plus Zen's limit toast for a third panel.

Everything else is upstream and unstyled, and it looks like Chromium:
- the panes and their outline;
- the per-pane mini toolbar;
- the drop target and its nudge;
- the sidebar's split row;
- the "Arrange split view" / "Swap with active split" submenus;
- the toolbar split button;
- the IPH;
- the Mac Tab-menu item and **Cmd+Opt+N**.

The biggest single leak is in **`chrome::NewSplitTab`**. Every "new split" entry point ends there, Koan's own Cmd+Shift+\*, palette and Space create menu included. It opens Chromium's tab-search **"split view new tab page"** (`chrome://tab-search.top-chrome/split_new_tab_page.html`) in the second pane. Zen instead opens an empty pane plus the floating URL bar. One redirect in the existing `browser_commands.cc.patch` fixes every one of those entry points.

Two real bugs sit in merged Koan code:
- The Cmd+Opt+V/H/G shortcut pairs the active tab with `active+1` without checking the group, so it can **pull a tab out of the next Space**.
- Glance -> Split and the shortcut both inherit upstream's "the new tab takes the pivot's pinned state", so splitting from an **Essential turns the other page into an Essential**.

Session restore of splits is upstream's, and it works for Koan's 2-pane model as-is (**reuse**).

### 1.1 Model and what Koan maps onto it

- **No feature flag gates split view in M158.** No `kSideBySide` remains; split tabs are launched code. The related switches are:
  - `tabs::kSplitViewHorizontal` (**enabled**): the stacked layout. Params `split_view_horizontal_direct_access` and `..._direct_tab_access` both default false (`chrome/browser/ui/tabs/features.cc:19-29, 45-54`). Flag row `split-view-horizontal`: `about_flags.cc:13610-13614`.
  - `features::kSplitViewTabDraggingUpdates` (enabled). Param `show_drop_target_for_tab_delay` = **1000 ms** (`ui_features.cc:263-268`).
  - `features::kSplitViewDragAndDropVelocity` (enabled), with min/max delay of 1000 ms and distance 20 (`ui_features.cc:270-291`).
  - `features::kWebUISplitTabsButton` (**disabled**) (`chrome_features.cc:2060`; `ui_features.cc:449-453`).
  - Blink `WindowOpenSplitTab`: a `window.open(..., "split")` request. The base feature is enabled, but Blink's status is `test`, so it is not web-exposed by default (`runtime_enabled_features.json5:8003-8006`). The browser half is in `browser_web_contents_delegate.cc:192-225, 706-719` and `browser_navigator.cc:1099-1140`.
- **Data.**
  - `split_tabs::SplitTabVisualData` holds a `SplitTabLayout` (`kSideBySide` | `kStacked`) and `split_ratio` (default 0.5) (`components/split_tabs/split_tab_visual_data.h`).
  - `SplitTabData` (`components/tabs/public/split_tab_data.h:20-43`) lives in a `SplitTabCollection` inside the tab collection tree, so inside a pinned collection or a group, which means inside a Space.
  - **Exactly 2 tabs.**
    - `AddToNewSplit` does `CHECK(indices.size() == 1u || indices.size() == 2u)`, and with one index it pivots on the **active** tab (`tab_strip_model.cc:1984-2024`).
    - `RestoreSplit` does `CHECK_EQ(indices.size(), 2u)` (`:2026-2041`).
    - `AddToSplitImpl` does `CHECK(!tab->IsSplit())` and moves the tabs to the **pivot's group and pinned state** (`:4631-4685`, `MoveTabsAndSetPropertiesImpl` at `:4645-4647`).
    - The vertical row lays out only when it has 2 children (`split_tab_view.cc:313-327`).
- **TabStripModel API.**
  - `UpdateSplitLayout` `:1893`, `UpdateSplitRatio` `:1928`, `UpdateTabInSplit` (kReplace / kSwap) `:1945`, `ReverseTabsInSplit` `:1974`.
  - `AddToNewSplit` `:1984`, `RestoreSplit` `:2026`, `RemoveSplit` `:2173`, `MoveSplitTo` `:1145`.
  - `DetachSplitTabForInsertion` / `InsertDetachedSplitTabAt` `:627`.
  - `ExecuteAddToNewSplitCommand` `:3422-3455`: it removes the active tab from the indices, and with nothing left it asks the delegate for `chrome::NewSplitTab`.
  - Observers get `OnSplitTabChanged` from `NotifySplitTab{Created,VisualsChanged,ContentsUpdated,Removed,Detached,Attached}` (`:2554-2640`).
- **What Koan maps.**
  - Zen grid / vsep / hsep -> `kSideBySide` / `kSideBySide` / `kStacked` (`koan_shortcuts.cc:214-219`).
  - Unsplit -> `RemoveSplit`. "Split Out Tab" -> `RemoveSplit` (`koan_split_view.cc:44-48`), because a 2-split cannot lose one member and keep going.
  - A third panel -> Zen's toast "Can't add more panels to the split view!" (`koan_split_view.cc:50-55`; used in `koan_shortcuts.cc:396-418` and `koan_glance_controller.cc:806-813`).
- **Koan saves no split state of its own.** `koan_session_data.h` has no split key; upstream's session commands carry it (§1.2.5).

### 1.2 Inventory

#### 1.2.1 Flags, prefs, IPH

| Upstream item | Where (pristine) | Koan today | Reachable? | Zen 1.24b |
|---|---|---|---|---|
| Split view itself (no flag) | `tab_strip_model.cc:1984`; `multi_contents_view.cc` | Used | Always | Split view on (2-4 panes) |
| `kSplitViewHorizontal` (stacked layout, enabled) + flag row | `tabs/features.cc:19`; `about_flags.cc:13610` | Used for Cmd+Opt+H (`kStacked`) | Flag "Stacked Split Views: Disabled" removes the menu's "Toggle orientation" (`split_tab_menu_model.cc:159-165`) but not Koan's Cmd+Opt+H *(live check: does `MultiContentsView` still render `kStacked`?)* | hsep always available |
| `kSplitViewTabDraggingUpdates` `show_drop_target_for_tab_delay` = 1000 ms | `ui_features.cc:263-268` | None | Every tab drag to the page edge | Preview appears **at once** when entering the outer quarter (split-view.md §3.2) |
| `kSplitViewDragAndDropVelocity` (min/max delay 1000 ms, distance 20) | `ui_features.cc:270-291` | None | Same | No velocity gate |
| `prefs::kSplitViewDragAndDropEnabled` (`browser.split_view_drag_and_drop_enabled`, default **true**, synced); Settings > Appearance toggle | `pref_names.h:643`; `browser_ui_prefs.cc:129`; `appearance_page.html.ts:341-346`; settingsPrivate allowlist `prefs_util.cc:316` | None (Koan's settings don't show it) | chrome://settings/appearance; a synced value from a Chrome profile turns drag-to-split off | `zen.splitView.enable-tab-drop` = true |
| `kSplitViewDragAndDropNudgeShownCount/UsedCount` (local state) | `browser_ui_prefs.cc:108-109`; `multi_contents_view_drop_target_controller.cc:91-101, 192-199, 258-261` | None | The first link/tab drags show Chromium's **nudge** drop target | No nudge |
| `prefs::kPinSplitTabButton` (`browser.pin_split_tab_button`, default false) | `pref_names.h:631`; `browser_ui_prefs.cc:141`; `split_tabs_button.cc:73-76, 203` | None | Pin from the toolbar-button context menu, or a copied profile | No toolbar button |
| IPH `IPH_SideBySidePinnableFeature`, `IPH_SideBySideTabSwitchFeature`, `IPH_SplitViewHorizontalIndirectAccessFeature` (all **enabled**) | `feature_constants.cc:278-286`; `split_view_iph_controller.cc:40-95`; `tab_strip_model.cc:2011-2021` | None | After the first split, after N tab switches, after a side-by-side split *(live check: anchor and look with Koan's toolbar hidden)* | No promos |
| `kWebUISplitTabsButton` (disabled) | `chrome_features.cc:2060` | Off (default) | Flags only | n/a |

#### 1.2.2 Commands, menus, accelerators, Mac menu

| Upstream item | Where | Koan today | Reachable? | Zen 1.24b |
|---|---|---|---|---|
| **`chrome::NewSplitTab`**: adds `chrome://tab-search.top-chrome/split_new_tab_page.html` next to the active tab (the regular NTP in OTR, or with a single tab), then `AddToNewSplit({active})` and activates it | `browser_commands.cc:1797-1817`; URL `webui_url_constants.h:323-324` | **None.** Koan's "New Empty Split" (Cmd+Shift+\*, `koan_shortcuts.cc:222-223`), the palette's "New Split View" (`koan_commands.cc:223-225`) and the Space create menu's "New Split" (`koan_workspaces.cc:972`) all run `IDC_NEW_SPLIT_TAB`. The shortcut's "nothing to pair" fallback calls it directly (`koan_shortcuts.cc:424-427`) | **Every** empty-split path shows Chromium's tab-search "choose a tab" page in the right pane | Empty pane plus the **floating URL bar**, bound to the split; Esc dissolves it (split-view.md §3.1) |
| `IDC_NEW_SPLIT_TAB` / `kActionSplitTab`: enabled in normal windows; executes `NewSplitTab` when not already split | `browser_command_controller.cc:887-891, 2874-2876`; `chrome_action_id.h:389`; `browser_actions.cc:2646-2652` | Used (above) | Command | — |
| Mac: **Cmd+Opt+N** = `IDC_NEW_SPLIT_TAB`; Tab menu "Add Tab to New Split View"; `ExecuteCommand` special-cases it and calls `chrome::NewSplitTab` **directly**, bypassing the command controller | `accelerators_cocoa.mm:119`; `main_menu_builder.mm:565-566`; `browser_native_widget_mac.mm:698-710` | None (no hiding hunk for it) | Menu bar, Cmd+Opt+N | Not a Zen key or menu item |
| Mac clashes with Koan's Zen keys: **Cmd+Opt+H** = app menu "Hide Others"; **Cmd+Opt+U** = View Source | `main_menu_builder.mm:84-87`; `accelerators_cocoa.mm:57` | Koan binds Cmd+Opt+H (horizontal) and Cmd+Opt+U (unsplit) (`koan_shortcut_table.cc:155-158`) | *(live check: which wins on a real key press: NSMenu key equivalent or Koan's accelerator?)* | Zen binds the same keys; split-view.md's live note says Cmd+Opt+U did not fire from synthesized events |
| Tab menu, context tab not split, active not split: `CommandAddToSplit`, "New split view with current tab" / "Add tab to new split view"; enabled for 1-2 tabs | `tab_menu_model.cc:359-398`; enabled `:397` | **Relabelled** "Add Split View..." / "Join N Tabs" (`koan_split_view.cc:24-34`) | Yes. On the **active** tab it runs `NewSplitTab` (split NTP). On another tab it pairs that tab with the active one. "Join 3/4 Tabs" shows but is **greyed** | "Add Split View..." on one tab = **new empty split** with that tab (URL bar). Join 2-4 works; a 5th gives the toast |
| Tab menu, context tab not split, active split: `CommandSwapWithActiveSplit` submenu "Swap with active split" (`SplitTabSwapMenuModel`) | `tab_menu_model.cc:345-358` | None | Right-click any other tab while a split is active | Not in Zen (Zen would **add** the tab, up to 4) |
| Tab menu, split member: `CommandArrangeSplit` submenu "Arrange split view" (`SplitTabMenuModel`: Separate views, Close left/right view, Toggle orientation, Reverse views) | `tab_menu_model.cc:400-411`; `split_tab_menu_model.cc:115-176` | Kept; "Split Out Tab" inserted before it (`koan_split_view.cc:35-39`) | Every split-member right-click | Only **Split Out Tab** |
| Tab menu "Mute sites" split submenu (`SplitTabMuteMenuModel`) | `tab_menu_model.cc:555-560` | None | Right-click a split member | Zen's plain mute toggle |
| `TabStripModel::IsContextMenuCommandEnabled` returns true for all three split commands | `tab_strip_model.cc:2748-2751, 3086-3108` | Koan's own 3001 is checked first (`koan_tab_menu.h`) | — | — |
| Page context menu `IDC_CONTENT_CONTEXT_OPENLINKSPLITVIEW` "Open link in split view". On a split tab: "Open link in left/right (top/bottom) view", which **navigates the other pane** | `render_view_context_menu.cc:1010-1040, 2050-2085, 6150-6203`; id `chrome_command_ids.h:321` | Relabelled "Split Link to New Tab" while not split (`koan_glance_controller.cc:552-561`); when split, upstream's label and replace behaviour stay | Any link right-click | New tab + split, **new tab selected** (upstream also activates it, `:6199-6203`). When the group is full the item is **disabled**, not a replace |
| Cmd+Opt+click a link -> `kNavigationPolicySplitView` -> `kNewSplitView`. Already split: navigates the other pane. Cmd+Opt+click on a bookmark / omnibox row -> `kNewSplitView` too | `navigation_policy.cc:59-62, 154-158`; `browser_web_contents_delegate.cc:582-600`; `browser_navigator.cc:1099-1140`; `window_open_disposition_utils.cc:20-29` | None (Koan's Glance hook needs exactly one modifier) | Any page | No such gesture; Cmd+Opt+click is a background tab in Firefox *(live check in Zen)* |
| New-tab-button context menu "New split view" | `new_tab_button_menu_model.cc:48, 71-78, 141-145` | None | *(live check: is the vertical strip's new-tab button / its menu reachable in Koan?)* | — |
| WebUI / NTP promo `browser_command_handler` -> `NewSplitTab` | `browser_command_handler.cc:403` | None | NTP promos only (Koan's NTP work, #96) | — |

#### 1.2.3 Rendering (content area, sidebar, toolbar)

| Upstream item | Where | Koan today | Reachable? | Zen 1.24b |
|---|---|---|---|---|
| Split panes: `split_view_insets_` 8, panes padded `kSplitViewContentPadding` 4 plus `ContentsContainerOutline` thickness; radius `kSplitViewContentCornerRadius` **6**; active-pane outline / highlight | `multi_contents_view.h:67-70`; `multi_contents_view.cc:71-74, 520-527, 800-815`; `contents_container_view.cc:57, 196-228` | **Unchanged.** Koan's content-card patch is `!IsInSplitView()`-gated (`multi_contents_view.cc.patch` @507-531; `koan_content_card.cc:40`; `koan_browser_view_controller.cc:168`) | Every split | 8 pt gap, 7 pt radius, **2 px accent-derived outline** on the active pane only, no inner padding (split-view.md §2.1) |
| **Mini toolbar** on the inactive pane (favicon, domain, alert icon, close "x"), notched into the outline; the context menu is `SplitTabMenuModel(kMiniToolbar)` | `multi_contents_view_mini_toolbar.cc:85-170`; `contents_container_view.cc:220-224`; `split_tab_menu_model.cc:130-143` | None | Every split, always visible on the inactive pane | **No** mini toolbar. Instead a hover-only header centred on each pane's top edge with **grip + x** (§2.1) |
| Resize area (`MultiContentsResizeArea`), snap within 15 px, min size clamp; ratio -> `UpdateSplitRatio` | `multi_contents_view.cc:56, 87-88, 754-795`; `multi_contents_view_delegate.cc:64-72` | Reused | Yes | Invisible splitter in the gap; min **7 %**; no snap *(live check: does the 15 px snap read as a jump?)* |
| Drop target (`MultiContentsDropTargetView`): tab drag (`HandleTabDrop`) and **link drag** (`HandleLinkDrop`), START/END/BOTTOM sides, nudge state | `multi_contents_view.cc:94-98, 622-660`; `multi_contents_view_delegate.cc:91-175`; `..._drop_target_controller.cc:33, 192-199`; tab target found via the vertical region first (`browser_view.cc:2506-2520`) | Reused, unstyled | Tab drag to the edge: Zen parity minus look and timing. **Link drag** (from a page or another app) to the edge: Chromium-only | Tab only, outer **quarter** of any side (left/right/top/bottom), 0.1 s preview with a fake pane (§3.2). No link drop |
| Vertical split row `SplitTabView`: two `TabView`s side by side with a **2 px** gap (`kSplitViewGap`), stacked when collapsed; painted background only when pinned | `split_tab_view.h:35`; `split_tab_view.cc:101-122, 313-360, 382-388` | Unstyled (no Koan hook in `split_tab_view.cc`) | Every split in the sidebar | One rounded container, members `flex:1`, **1 x 16 separator** (hidden when active), group gets the selected background, 28 px member height, stacked 34 px with a 2 px outline when collapsed (§2.2) *(live check: side by side)* |
| Toolbar `SplitTabsToolbarButton`: visible when pinned **or the active tab is split**; menu `SplitTabMenuModel(kToolbarButton)` incl. "**Send feedback**" | `split_tabs_button.cc:57-80, 189, 203`; `split_tab_menu_model.cc:176-186` | None | Multiple-toolbar mode, while a split is active *(live check: visible in Koan's toolbar?)* | No such button |
| Drop target / tab highlight on hover (`SplitTabHighlightController`) | `split_tab_highlight_controller.cc` | None | Hovering a split row *(live check)* | Hover = toolbar bg on the container |

#### 1.2.4 Koan's own split code against upstream

| Koan path | What it does | Upstream rule it hits | Risk |
|---|---|---|---|
| `KoanShortcutController::Split` (`koan_shortcuts.cc:383-431`) | With nothing selected, pairs active with **`active + 1`** if not split and of the same pinned state | `AddToSplitImpl` moves the tab to the **pivot's group** (`tab_strip_model.cc:4645-4647`) | **Bug.** On the last tab of a Space, `active+1` is the first (hidden) tab of the **next Space**, and it is silently moved into this Space. Zen uses `gBrowser.visibleTabs` and wraps to the first visible tab (`ZenViewSplitter.mjs:2067-2084`). With no partner Koan falls back to `NewSplitTab` (split NTP) where Zen does nothing (< 2 tabs) |
| Same, pinned pair | `IsTabPinned(active+1) == IsTabPinned(active)` lets two Essentials (Koan: `IsPinned()` = Essential, `koan_commands.cc:208`) form a split | Split of pinned tabs lives in the pinned collection (`split_tab_view.cc:101-122` paints a pinned background) | *(live check)*: what Koan's Essentials grid does with a split. Zen: Essentials are **duplicated**, never moved (§1) |
| `KoanGlanceController::OpenInSplit` (`koan_glance_controller.cc:801-825`) | Inserts the Glance page after the source unactivated, then `AddToNewSplit({index})` | Pivot = the active tab (source); the new tab takes its **pinned state** (`browser_navigator.cc:1140-1150` says so for link splits) | From an **Essential**, the Glance page becomes an Essential. **Active stays on the source** (left), while Zen selects the **glance** (right) (glance.md §6) |
| Tab-menu relabels (`koan_split_view.cc:20-40`) | Labels only | Behaviour stays upstream's (`ExecuteAddToNewSplitCommand`) | "Add Split View..." on the active tab opens the split NTP. "Join 3 Tabs" is shown but disabled (Zen: works; Koan should toast) |
| "Split Link to New Tab" relabel when not split (`koan_glance_controller.cc:557-561`) | Label only | Split tab: upstream navigates the other pane (`render_view_context_menu.cc:6157-6182`) | A **destructive replace** of the other pane's page; Zen disables the item for a full group |
| `IsSplitOutTabCommand` 3001, Glance 55000 | Command ids | 55000 is free in `chrome_command_ids.h` (54019 is the highest 54xxx) | OK |

#### 1.2.5 Session restore, tab restore, extensions

| Upstream item | Where | Koan today | Reachable? | Zen 1.24b |
|---|---|---|---|---|
| Session commands `kCommandSetSplitTab` 36 / `kCommandSetSplitTabData` 37; `SessionService::SetSplitTab/SetSplitTabData` ignore a closing window | `session_service_commands.cc:111-112, 352-360, 481-503`; `session_service.cc:265-297` | **Reused** (Koan stores nothing) | Every restart | Split data saved per group (tabs, grid type, sizes) |
| Restore: collect tabs by `split_id`, `RestoreSplit` only if **exactly 2**, then `UpdateSplitLayout` + `UpdateSplitRatio` from the saved visual data | `session_restore.cc:1093-1138, 1203-1211, 1226-1243` | Reused | Yes; layout and ratio come back | Same, after Spaces restore |
| Tab restore (Cmd+Shift+T of a closed split / window): `CreateHistoricalSplit`, `BrowserLiveTabContext::ReconstructSplit` | `browser_live_tab_context.cc:244-266, 492-510` | Reused (Koan's extra data rides along via `GetExtraDataForTab`) | Yes | — |
| Close the active split member: `CloseTab` closes just that tab when the selection is the split | `browser_commands.cc:1465-1476` (after Koan's pinned hook) | Reused | Cmd+W | Same (group dissolves) |
| Extensions: `tabs.Tab.splitViewId`, `tabs.query({splitViewId})`, `SPLIT_VIEW_ID_NONE`; `tabs.move` keeps splits together | `tabs.json:55, 156, 446, 561-565, 868, 1212`; `extension_tab_util.cc:390-394, 842`; `tabs_api.cc:200-212, 755-765` | Reused | Read-only exposure; no extension API creates a split | No equivalent |
| Saved tab groups | — | Off with Spaces (`koan::SavesTabGroups()`; tab-groups audit §1.1) | — | — |

### 1.3 Changes (verdicts)

- **SV1, redirect: `chrome::NewSplitTab` -> Koan's empty split.** One hook at the top of `NewSplitTab` in the existing `browser_commands.cc.patch` (`browser_commands.cc:1797`). It catches every caller:
  - command controller, Mac menu and Cmd+Opt+N (`browser_native_widget_mac.mm:707`);
  - the tab menu's "split with itself" via `BrowserTabStripModelDelegate::NewSplitTab` (`browser_tab_strip_model_delegate.cc:382-391`);
  - the toolbar button, the new-tab-button menu and the WebUI handlers.

  Koan's version follows split-view.md §3.1: the current tab plus a blank partner, then `url_bar()->OpenForNewTab()` bound to the split. Esc or a tab switch dissolves it. Zen's empty pane is transparent, so the partner should be a Koan blank page, never `split_new_tab_page.html`. Then `koan_shortcuts.cc:424-427` calls the same Koan function. **Must-change.**
- **SV2, fix: the shortcut partner rule.** In `KoanShortcutController::Split`, pick the next tab **in the same Space**: same group, non-hidden, wrapping to the Space's first tab. With fewer than 2 tabs, do nothing (Zen does nothing; today Koan opens the split NTP). **Must-change** (it moves tabs between Spaces today).
- **SV3, override: the Essentials and pinned mix.** Before any `AddToNewSplit` whose pivot or partner is an Essential (shortcut, Glance -> Split, "Split Link to New Tab", tab drop on the page), follow Zen §1: duplicate the Essential and split the copy, or *(cheaper)* refuse with a toast. At minimum, `OpenInSplit` must not hand Essential status to the Glance page. *(live check: Koan's Essentials grid with a pinned split)*
- **SV4, override: Glance -> Split selection.** After `AddToNewSplit` in `OpenInSplit`, `ActivateTabAt` the new tab, as upstream's own link split does (`render_view_context_menu.cc:6199-6203`). With a right-hand sidebar, `ReverseTabsInSplit`.
- **SV5, lock/hide: Chromium-only tab-menu items.** In `AddZenSplitTabMenuItems`:
  - remove `CommandArrangeSplit` (keep "Split Out Tab");
  - remove the split `SplitTabMuteMenuModel` row *(or keep, low)*;
  - replace `CommandSwapWithActiveSplit` with Zen's "Add Split View..." running `ShowSplitLimitToast`;
  - enable "Join N Tabs" for N >= 3 and route it to the toast. Today it is greyed, and upstream's `CHECK(indices.size() <= 2)` means it must never reach `AddToNewSplit`.
  - "Add Split View..." on the active tab then goes through SV1.
- **SV6, override: "Split Link to New Tab" on a split tab.** Zen's full group means the item is disabled. In `AddZenLinkContextMenuItems`, when `tab->IsSplit()`, set the item disabled with Zen's label, or remove it. Never "Open link in left view" (that replaces the other pane's page).
- **SV7, override: Cmd+Opt+click.** Blink maps it to `kNavigationPolicySplitView` (`navigation_policy.cc:59-62`), and when the source is split the browser replaces the other pane (`browser_web_contents_delegate.cc:586-600`). Make it a background tab, as in Firefox, with one line in the existing `navigation_policy.cc.patch`. The browser-side bookmark/omnibox gesture (`window_open_disposition_utils.cc:27-29`) is a `ui/base` change; leave it unless a live check shows Zen differs. *(live check in Zen first.)*
- **SV8, lock: Mac menu and accelerators.** Add `IDC_NEW_SPLIT_TAB` to the cross-cutting `koan::HidesMenuCommand` table (Tab > "Add Tab to New Split View"). Drop or redirect Cmd+Opt+N, which is not a Zen key. Live-check the Cmd+Opt+H (Hide Others) and Cmd+Opt+U (View Source) clashes. If NSMenu wins, Koan needs those key equivalents removed in `main_menu_builder.mm` or an earlier hook (`browser_native_widget_mac.mm.patch`). *(live check)*
- **SV9, restyle (Koan look): panes.** In `UpdateContentsBorderAndOverlay` / `SetBackgroundRadii` (existing `multi_contents_view.cc.patch`), when split:
  - insets 8 / radius 7 to match the content card;
  - no inner padding;
  - an outline that is 2 px, accent-derived and only on the active pane.
- **SV10, lock + replace: mini toolbar.** Hide `MultiContentsViewMiniToolbar` (`contents_container_view.cc:220-224`; a hook in `MiniToolbar::UpdateState`, or `SetVisible(false)` from the same patch). Then build Zen's hover header (grip + x; x = `RemoveSplit` + select that tab, Shift+x = keep the selection) as a Koan child view. The grip and rearrange part is mostly moot at 2 panes; swap = `ReverseTabsInSplit`.
- **SV11, override: drag timings and the nudge.** In `koan_feature_overrides.cc`, set `show_drop_target_for_tab_delay` to 0 and drop the velocity gate (`kSplitViewDragAndDropVelocity` disabled). Restyle `MultiContentsDropTargetView` as Zen's fake pane. Disable **link** drops in `MultiContentsViewDropTargetController::OnWebContentsDragUpdate` (Zen has none), and so the nudge. *(live check: link drop path)*
- **SV12, lock: prefs.** `kSplitViewDragAndDropEnabled` should be a Koan setting row, if Zen's `enable-tab-drop` is surfaced, or forced true. Hide the toolbar `SplitTabsToolbarButton` in Koan's toolbar and force `kPinSplitTabButton` false. That also kills the toolbar menu's "Send feedback".
- **SV13, lock: IPH.** Disable `kIPHSideBySidePinnableFeature`, `kIPHSideBySideTabSwitchFeature` and `kIPHSplitViewHorizontalIndirectAccessFeature` in the shared feature-override block (cross-cutting item 5).
- **SV14, restyle: the sidebar split row.** Zen's container, separator and active-member colours go in `SplitTabView::OnPaint` / layout, through the existing `tabs/common` patch family (new small hook in `split_tab_view.cc`).
- **SV15, reuse:** session and tab restore of splits, `CloseTab` on a split, the extension `splitViewId` exposure, `UpdateSplitRatio` / `UpdateSplitLayout`, the stacked layout, and the 2-pane limit plus toast (a documented Zen divergence: 3-4 panes and grid are out of scope at 2 panes).
- **SV16, detector:** in debug builds, a DCHECK in `KoanBrowserViewController` that no `MultiContentsViewMiniToolbar`, `SplitTabsToolbarButton` or split-NTP URL is visible or committed in a Koan window. Run it in the Mac e2e pass (cross-cutting item 6).

### 1.4 Mac tests

- **S1.** Cmd+Shift+\*, the palette's "New Split View", and the Space create menu's "New Split": an empty right pane plus the floating URL bar, never the "tab search" list page. Enter a URL: the pane loads and is selected. Repeat and press Esc: back to one pane, original selected (SV1).
- **S2.** Menu bar Tab > (item should be gone), Cmd+Opt+N (nothing, or S1's behaviour) (SV1, SV8).
- **S3.** Space A with tabs A1 and A2 (A2 active, last). Space B has B1. Cmd+Opt+V: A1+A2 split (wrap), B1 still in Space B (SV2). One-tab Space + Cmd+Opt+V: nothing.
- **S4.** Essential E active, Cmd+Opt+V; Alt-click a link on E and press Glance's Split: no new Essential appears, and E stays where it was (SV3).
- **S5.** Glance -> Split: parent left, glance right **and selected** (SV4).
- **S6.** Right-click a split member: "Split Out Tab", no "Arrange split view". Right-click another tab while a split is active: no "Swap with active split"; "Add Split View..." gives the limit toast. Select 3 tabs and pick "Join 3 Tabs": the toast, no crash (SV5).
- **S7.** In a split, right-click a link: no "Open link in left view"; Zen's item is disabled. Cmd+Opt+click a link in a split: the other pane's page is **not** replaced (SV6, SV7).
- **S8.** Cmd+Opt+H and Cmd+Opt+U with a split active: split horizontal / unsplit, not Hide Others / View Source *(live check)* (SV8).
- **S9.** Panes: 8 pt gap, radius 7, 2 px accent outline only on the active pane, **no** favicon/domain mini toolbar on the inactive pane; hover the pane top: grip + x header (SV9, SV10).
- **S10.** Drag a sidebar tab to the page's right quarter: the preview appears within ~100 ms (not 1 s), with no "nudge" box. Drag a link from a page to the edge: no drop target (SV11).
- **S11.** Multiple-toolbar mode with a split: no split button in the toolbar; no IPH bubble after the first split, nor after 5+ tab switches (SV12, SV13).
- **S12.** Resize the splitter to the extremes: it clamps; check the 15 px snap against Zen's 7 % minimum.
- **S13.** Split side by side at 70/30, quit, relaunch: same split, layout and ratio. Close the window, then Cmd+Shift+T: split back (SV15).
- **S14.** Sidebar split row: separator, active background; collapsed sidebar stacks the members (SV14).
- **S15.** `chrome://settings/appearance` "split view drag and drop" off (or synced off): Koan's drag-to-split follows the Koan setting, not the hidden upstream one (SV12).

### 1.5 Top leaks (split)

| # | Severity | Leak | Who reaches it | Fix |
|---|---|---|---|---|
| 1 | High | Every "new empty split" opens **Chromium's tab-search split NTP** | Cmd+Shift+\*, palette, Space menu, tab menu on the active tab, Cmd+Opt+N, Mac Tab menu | SV1 |
| 2 | High (bug) | Cmd+Opt+V/H/G **moves the next Space's first tab** into the current Space | Shortcut on the last tab of a Space | SV2 |
| 3 | High | **Mini toolbar** (favicon + domain + x) and Chromium outline and padding on every split | Every split | SV9 + SV10 |
| 4 | Medium-high | Splits from an **Essential** make the partner an Essential (Glance -> Split, shortcut, link split) | Essentials users | SV3 |
| 5 | Medium | Chromium-only tab-menu submenus ("Arrange split view" with Close left/right / Reverse / Toggle; "Swap with active split") | Every split right-click | SV5 |
| 6 | Medium | "Open link in other view" and Cmd+Opt+click **replace** the other pane's page | Link right-click in a split; Cmd+Opt+click | SV6 + SV7 |
| 7 | Medium | Drag-to-split waits 1 s, shows the Chromium nudge, accepts **links** | Any drag to the page edge | SV11 |
| 8 | Medium *(live check)* | Cmd+Opt+H / Cmd+Opt+U may run Hide Others / View Source instead of split | Mac keyboard | SV8 |
| 9 | Low-medium | IPH bubbles, toolbar split button (+ "Send feedback") in multiple-toolbar mode | After first split | SV12 + SV13 |
| 10 | Low | Sidebar split row is Chromium's (2 px gap, no container) | Every split | SV14 |

---

## 2. Glance vs upstream link preview

**Summary.** **Upstream has no desktop link preview or peek in M158.**
- `kLinkPreview`, `PreviewManager` and the preview window are deleted. `git grep LinkPreview HEAD` hits only the comments in `render_view_context_menu_browsertest.cc:507-510`.
- `preview_tab` / `PreviewTab` exists only on Android (`chrome/android/.../previewtab`).
- There is no `Peek` feature in `chrome_features.cc` / `ui_features.cc`.

So Glance collides with no upstream preview UI. What it does collide with is **upstream's modifier-click semantics**:
- Alt+click = download in Blink;
- Option-click on bookmarks / omnibox rows = save to disk in the browser;
- Cmd+Opt+click = split.

It also collides with **the fact that M158 tab helpers are TabFeatures**. Every tab helper now lives in `TabFeatures` (`tab_features.h:5-22`, created in `TabModel`, `tab_model.cc:68`), and Glance's page is a bare `WebContents::Create` (`koan_glance_controller.cc:657-659`). So inside a Glance there is no permission manager, no autofill or password manager, no find, no fullscreen handling, and no file chooser. Browser shortcuts typed in the card act on the **tab underneath**: Cmd+W closes the parent, Cmd+F/R/L/[ go to the parent. In Zen a Glance is a real tab, so those keys act on the Glance (glance.md §7: Cmd+W closes the glance).

The interception itself is sound:
- the Blink policy hook drops synthesized clicks;
- the browser checks `kNewPopup` + renderer-initiated + trusted gesture;
- the fallback is never `kNewPopup`, so no recursion;
- teardown is ordered and posted.

The remaining risks are a raw `source_` across a WebContents **replace** and the one-off app-modal JS dialogs. Tab hover cards are a separate upstream preview that Zen doesn't have.

### 2.1 How Glance sits on upstream (the model)

1. **Renderer.** `NavigationPolicyFromEventModifiers` (`navigation_policy.cc:51-71`) is hooked first. A primary-button event with **exactly one** modifier equal to the configured one returns `kNavigationPolicyNewPopup` (`navigation_policy.cc.patch` @53-64; `koan_features.cc:70-93`, which reads the `kGlanceActivationMethod` switch once per renderer).
   - `NavigationPolicyFromEvent` turns a synthesized NewPopup into the current tab (patch @157-172, modelled on upstream's download and split rules, `:140-158`).
   - `NavigationPolicyForCreateWindow` lets script `window.open` during an Alt+click keep the app policy (patch @182-205).
   - Without the hook, Alt alone on the Mac is **`kNavigationPolicyDownload`** (`:70`).
2. **Browser.** `BrowserWebContentsDelegate::OpenURLFromTab` calls `koan::MaybeOpenGlance` right after the DevTools early return, **before** upstream's `kNewSplitView` handling and the popup blocker (`browser_web_contents_delegate.cc.patch` @542-561; pristine `:575-600`).
   - `IsActivationClick` (`koan_glance_controller.cc:224-229`) requires `kNewPopup`, renderer-initiated, a user gesture and a trusted event.
   - It is declined for non-http(s), a non-active source, or a window without Koan's controller. The declined fallback (`:202-218`) is the current tab, a new window (shift) or a background tab (meta); never `kNewPopup`.
3. **Context menu.** "Open Link in Glance" (id 55000) runs a browser-initiated `Open` (`:567-584`; `render_view_context_menu.cc.patch` @2121-2127, @3507-3517, @3901-3915).
4. **Card.**
   - A `TYPE_CONTROL` child widget over `MultiContentsView` (content-card insets) hosts a bare `WebContents` whose delegate is the controller (`:635-684`, `:858-870`).
   - `OpenURLFromTab` / `AddNewContents` from the page are forwarded to the source tab's delegate (`:893-948`). Gesture-less popups are dropped, because `PopupBlockerTabHelper` is a tab feature (`tab_features.h:132`).
   - `HandleKeyboardEvent` takes Esc and forwards everything else to `BrowserView` (`:957-967`).
   - JS dialogs use `AppModalDialogManager` (`:983-987`).
   - Any active-tab change or removal of the source tears it down (`:989-1013`).
5. **Promotion.**
   - `ExpandToTab` (`:783-799`) and `OpenInSplit` (`:801-825`) `TakeContents()` (which drops the delegate and user data) and `InsertWebContentsAt`.
   - `TabModel` then creates the TabFeatures, so the page gains its helpers only at this point.
   - Cmd+O = expand, only while open (`koan_shortcut_table.cc:170-171`; `koan_shortcuts.cc:243-251`).

### 2.2 Inventory

| Upstream item | Where (pristine) | Koan today | Reachable? | Zen 1.24b |
|---|---|---|---|---|
| Desktop link preview (`kLinkPreview`, preview window, Alt+click / long-press trigger) | Deleted; only test comments at `render_view_context_menu_browsertest.cc:507-510` | n/a | No | n/a |
| Blink Alt+click -> **Download** (`kNavigationPolicyDownload`); synthesized-event guard | `navigation_policy.cc:70, 140-144` | **Overridden** before it for the Glance modifier (patch @57-64) | With method `alt`, Alt+click never downloads. A declined Alt+click opens **in place** (`FallbackDisposition`) | Firefox `browser.altClickSave` = false, so Alt+click is a normal open; in Zen it is Glance. Parity |
| Blink **Cmd+Opt+click** -> `kNavigationPolicySplitView` | `navigation_policy.cc:59-62, 154-158` | Untouched (two modifiers) | Yes; see §1 SV7 | Not a Zen gesture *(live check)* |
| Blink keyboard path: `NavigationPolicyFromEventModifiers(0, ...)` for key events | `navigation_policy.cc:82-85` | The hook applies (button 0) | **Alt+Enter** on a focused link opens Glance; Alt+Enter / Alt+click on a **form submit** opens the POST result in Glance (form submission also uses `NavigationPolicyFromEvent`; `LoadURLParams(params)` carries the post data) *(live check)* | Only a primary-button **link** click (glance.md §2) |
| `UserRequestedNavigationPolicyForCreateWindow` (used for `window.open` split requests) | `navigation_policy.cc:198-204`; `create_window.cc:376` | Not patched: NewPopup counts as "user requested" while `NavigationPolicyForCreateWindow` treats it as app policy | Only with the `WindowOpenSplitTab` web feature on (test status) | n/a |
| Browser-side modifier dispositions: Option-click = **`kSaveToDisk`**, Cmd+Opt = split (bookmarks, omnibox rows, history, tab search) | `window_open_disposition_utils.cc:20-39` | None | Option-click a bookmark **downloads** it | Option-click a bookmark = **Glance from the bookmark** (glance.md §2) |
| `IDC_CONTENT_CONTEXT_OPENLINK*` items in the **Glance page's** menu: `GetBrowser()` is null for a non-tab WebContents, so `IsNormalBrowser()` is false and there is no split item; new tab / window go through the controller to the source | `render_view_context_menu.cc:2050-2085, 6150-6153` (`CHECK(browser)` only in `OpenLinkInSplitView`) | "Open Link in Glance" added when `GlanceControllerFor(source)` | *(live check: every item in a Glance-page context menu, e.g. Lens, Reading mode, Translate, which assume a tab)* | Real tab: full menu |
| **TabFeatures-only helpers** (`PermissionRequestManager`, `ChromeAutofillClient`, `ChromePasswordManagerClient`, `FindTabHelper`, `PopupBlockerTabHelper`, `extensions::TabHelper`, `PageSpecificContentSettings`, ...). Non-tab WebContents must attach them explicitly (allowlist in `chrome/browser/ui/tabs/PRESUBMIT.py:80-125`; pattern in `contextual_tasks_side_panel_coordinator.cc:171-205`) | `tab_features.h:5-22`; `tab_model.cc:68`; `tab_features.cc:421` | **None attached** (`koan_glance_controller.cc:657-659`) | In a Glance: permission prompts *(live check: auto-deny or no prompt)*, password autofill, `<input type=file>` (`RunFileChooser` not overridden, so cancelled), DOM fullscreen (`EnterFullscreenModeForTab` not overridden, so denied), extension content-script messaging by tab id | Real tab: all work. DOM fullscreen **expands** the glance |
| Browser shortcuts from the card: `HandleKeyboardEvent` -> `BrowserView` | `koan_glance_controller.cc:957-967` | Forwarded | **Cmd+W closes the parent tab** (then teardown); Cmd+F finds in the parent; Cmd+R reloads the parent; Cmd+L / Cmd+[ / zoom / DevTools act on the parent | Cmd+W closes **the glance** (glance.md §7); other keys act on the glance tab |
| JS dialogs | `AppModalDialogManager` (`:983-987`) | App-modal (window-level Chromium dialog) | Any `alert()` in a Glance | Tab-modal (in-page) |
| Popups from the Glance page | `:925-948` | Forwarded to the source's delegate; gesture-less blocked **without** Chromium's "pop-up blocked" UI | Yes | Normal Firefox popup blocking |
| **Tab hover cards** (`TabHoverCardController`) on vertical `TabView` hover / focus; images feature **off on Mac** (`kTabHoverCardImages`), `prefs::kHoverCardImagesEnabled` true, `kHoverCardMemoryUsageEnabled` true; settings toggles in the allowlist | `tab_view.cc:708, 882, 1029-1070`; `split_tab_view.cc:187-191`; `ui_features.cc:303-309`; `browser_ui_prefs.cc:75-77`; `prefs_util.cc:355-357`; Koan only migrates the memory pref (`tab_strip_prefs.cc.patch` @31) | None | *(live check: does Koan's sidebar show Chromium hover cards with title / domain / memory?)* | No hover cards; the native title tooltip only |
| Glance in the sidebar | — | Not shown | — | The glance appears **as a child of its parent tab row** (glance.md §1; zen-mac-spec §7) |
| Session | — | Glance is not a tab, so it is never saved | Quit with a Glance open: gone | Glance tabs are removed on quit. Parity |

### 2.3 Crash and teardown review (merged code)

- **`source_` is a `raw_ptr<WebContents>`** and is only cleared on `kRemoved` (`:996-1003`). A **`kReplaced`** change (discard, `ReplaceWebContentsAt`, a prerender or NTP swap) keeps the active index, so `active_tab_changed()` may be false, and `source_` dangles. Later `source_->Focus()` (`CloseNow`), `source_->GetDelegate()` (`OpenURLFromTab`/`AddNewContents`) or `GetIndexOfWebContents(source_)` (`ExpandToTab`) would then touch a freed object. Under BRP that is a crash, not silent UAF. Fix: handle `kReplaced` in `OnTabStripModelChanged` (tear down, or rebind). *(live check: can the active tab be replaced while Glance is open?)*
- **Inactive split pane as source.** `MaybeOpenGlance` declines when `GetActiveWebContents() != source` (`:541-547`), so an Alt+click in the inactive pane opens **in place** if the mousedown didn't activate the pane first *(live check)*.
- **Promotion while a dialog is up.** `TakeContents` -> `ReleaseContents` cancels app-modal dialogs. Expand is blocked while beforeunload runs (`:785-787`). OK.
- **Window close.** `OnBrowserViewDestroying` removes the observer, closes the widget and resets `contents_` synchronously (`:834-848`). It is safe only because nothing in the Glance WebContents' delegate callbacks is on the stack at window close. That is the same "one teardown owner" note as A13 in the vertical-tabs audit.
- **Popup forwarding** passes `source_` as the opener-side source to `AddNewContents`. A `window.open(..., "split")` from a Glance page (with the web feature on) would split relative to the parent tab. That is acceptable.

### 2.4 Changes (verdicts)

- **GL1, reuse:** the Blink activation hook, the synthesized-event guard, the browser-side shape check and the fallback. No upstream preview feature exists to lock. Record "link preview deleted upstream" in `docs/REBASE.md`, so a future roll that resurrects one (Alt-hover / long-press) gets audited.
- **GL2, override: keys inside Glance.** Handle these in `KoanGlanceController::HandleKeyboardEvent` before forwarding:
  - Cmd+W -> `Close()`;
  - Cmd+F -> Glance-local find, or swallow;
  - Cmd+R -> reload `contents_`;
  - Cmd+L -> Expand, then focus;
  - Cmd+[ / ] -> history of `contents_`;
  - zoom -> `contents_`.

  On the Mac, Cmd+W also arrives through the NSMenu (`IDC_CLOSE_TAB`), so the existing `HandleCloseTabCommand` hook (`browser_commands.cc.patch` @17-20) must also ask "is Glance open?" and close the Glance instead. **Must-change** (Cmd+W today closes the page the user was reading).
- **GL3, override: give the Glance page the minimum non-tab helpers.** As `contextual_tasks_side_panel_coordinator.cc:171-205` does, attach:
  - `PermissionRequestManager`, `PermissionRecoverySuccessRateTracker` and `PageSpecificContentSettings`;
  - `ChromePasswordManagerClient` and `ChromeAutofillClient` (for parity);
  - `extensions::TabHelper` (the contextual-tasks comment notes UI that dereferences it unchecked).

  Also implement `RunFileChooser` (`FileSelectHelper`) and `EnterFullscreenModeForTab` (Zen: expand, then fullscreen). Alternative: make the Glance page a real tab, hidden in the strip, the way Zen does. That costs more, but it makes all of GL2 and GL3 automatic. Pick one before the next Glance PR. *(live check: geolocation and notification prompts in a Glance)*
- **GL4, fix: `kReplaced`.** Tear down, or rebind `source_` (§2.3). **Must-change** (crash class).
- **GL5, override: Glance only for link clicks.** In the browser check, require `params.post_data == nullptr` (no form POST into Glance). In Blink, require a mouse event, not a key event, if Zen parity on Alt+Enter matters *(live check Zen)*.
- **GL6, redirect: Option-click on bookmarks** (bookmark bar, Library). Zen opens a Glance; Chromium **saves to disk** (`window_open_disposition_utils.cc:36-37`). Hook the bookmark open path (`bookmark_utils` / `OpenAllIfAllowed` callers) to call `KoanGlanceController::Open`. Lower priority; it is also the place to stop the download.
- **GL7, override: JS dialogs.** Use `TabModalDialogManager::CreateForWebContents` on the Glance page (it is allowlisted for non-tab WebContents, PRESUBMIT `:80-84`), so dialogs look like the tab's.
- **GL8, lock: hover cards.** If the live check shows them in the sidebar, suppress `TabView::UpdateHoverCard` for Koan's sidebar (existing `tab_view.cc.patch`) and hide the two settings rows. Zen has none. *(live check)*
- **GL9, parity backlog (not a leak):**
  - Glance as a child row of the parent tab;
  - the split button disabled when the split is full (today: toast plus expand, `:806-813`);
  - an opening animation from the link rect (Koan uses the press point).
- **GL10, consistency:** add `kNavigationPolicyNewPopup` to `UserRequestedNavigationPolicyForCreateWindow`'s exclusions in the same Blink patch, so the two functions agree (`navigation_policy.cc:198-204`).

### 2.5 Mac tests

- **GT1.** Option-click a link: Glance opens. Option-click a `mailto:` link, and Option-click in an extension popup: opens normally, **no download** (GL1).
- **GT2.** Set the activation method to `ctrl`, relaunch: Option-click is a normal click; Ctrl-click opens a Glance (renderer switch).
- **GT3.** Cmd+Opt+click a link: no Glance, and per SV7 no split replacement.
- **GT4.** In a Glance: Cmd+W closes **only the Glance**, through both the key and the File menu. Cmd+R reloads the Glance; Cmd+F does not open find on the parent (GL2).
- **GT5.** In a Glance: request geolocation or notifications, pick a file in `<input type=file>`, request DOM fullscreen (video): a prompt, a picker, and expand + fullscreen (GL3) *(live check today: expected silent deny / no-op)*.
- **GT6.** In a Glance, a saved-password site: autofill offers (GL3).
- **GT7.** `alert()` in a Glance: a tab-style dialog over the card, not a window sheet (GL7).
- **GT8.** Glance open, then the parent tab is discarded or replaced (e.g. `chrome://discards` "urgent discard" on the active tab, if allowed): no crash, Glance closes (GL4).
- **GT9.** Option-click a form's submit button, and Option+Enter on a focused link: compare with Zen (GL5).
- **GT10.** Option-click a bookmark-bar item: Glance in Zen. Koan today downloads it (GL6).
- **GT11.** Hover a sidebar tab for 1 s: no Chromium hover card (GL8).
- **GT12.** Glance over a split (Option-click in the active pane): the card covers both panes. Split button: toast "Can't add more panels..." and the page opens as a tab next to the split. Option-click in the **inactive** pane: Glance, not in-place navigation (§2.3).
- **GT13.** Close the window with a Glance open, and quit with a Glance open: no crash, nothing restored.
- **GT14.** Right-click inside a Glance page and run every item (new tab, new window, copy link, Lens, Translate, Inspect): no crash, and new tabs open next to the parent.

### 2.6 Top leaks (Glance)

| # | Severity | Leak | Who reaches it | Fix |
|---|---|---|---|---|
| 1 | High | **Cmd+W in a Glance closes the parent tab**; Cmd+R/F/L/[ act on the parent | Every Glance user | GL2 |
| 2 | High *(live check)* | Glance page has **no tab helpers**: permissions, password/autofill, file picker, DOM fullscreen silently fail | Any non-trivial page opened in Glance | GL3 |
| 3 | Medium (crash class) | Raw `source_` survives a **WebContents replace** of the parent | Discard or replace of the active tab while Glance is open | GL4 |
| 4 | Medium | Option-click on a **bookmark downloads** it (Zen: Glance) | Bookmark bar, Library | GL6 |
| 5 | Medium *(live check)* | Chromium **tab hover cards** in the sidebar | Every tab hover | GL8 |
| 6 | Low-medium | App-modal (window) JS dialogs from Glance | `alert()` / beforeunload in Glance | GL7 |
| 7 | Low | Form submit / Alt+Enter open Glance (Zen: links only) | Option-click a submit button | GL5 |
| 8 | Low | No link preview upstream (none to lock); watch on rolls | — | GL1 (REBASE.md row) |

**Must-change across both sections:** SV1 (split NTP redirect), SV2 (cross-Space partner bug), SV10 (mini toolbar), GL2 (Cmd+W and friends in Glance), GL4 (`kReplaced` teardown), and a decision on GL3 (helpers vs. real hidden tab). SV3 should go in alongside, so no Essentials are created by accident.

---

## 3. Library, History, Downloads, Bookmarks

Date: 2026-11-05. Chromium 158.0.8127.2 (pristine via `git show HEAD:` / `git grep HEAD`), Koan `develop` dcf9bdaa, which includes #64 (`52a54c9e`, Library) and #66 (`8cfb3de0`, media controller, toasts, download animation). Zen 1.24b from `.cache/zen-desktop-1.24b`, plus `.agent/notes/zen-behaviour/library.md` and `keyboard-shortcuts.md`. Read only: nothing was built or run. Verdicts marked *(live check)* still need a Mac run. Change ids **LB**, tests **L**.

**Summary.** Koan's Library (#64) is a faithful port of Zen's slide-over panel, but it is reached **only through Koan's own keys and the sidebar button**. Zen redirects the *commands*: `Browser:ShowAllHistory`, `key_gotoHistory` and `Tools:Downloads` all open the Library (`src/zen/common/zen-sets.js:21-25`, `ZEN_LIBRARY_TARGETS`). Koan intercepts only the keystrokes, at high priority (`koan_shortcuts.cc:96-108`). Every *command* path still opens Chromium's WebUI tab:
- Mac menu History > Show Full History;
- Window > Downloads;
- ⌥⌘L;
- the app menu;
- the download bubble's "See all".

This leaves the user with **two history UIs and two (sometimes three) download UIs**. The most visible download leak is that upstream's **partial download bubble auto-opens when a download finishes**. It anchors to a downloads button in a toolbar that Koan hides, and in immersive fullscreen it takes a reveal lock. Zen ships `browser.download.alwaysOpenPanel=false`. Bookmarks are a smaller problem, because Zen's Library has no bookmarks section either. But Koan's Ctrl+B opens Chromium's WebUI bookmarks side panel on the **right** (upstream default), and the Mac View/Bookmarks menu items toggle Chromium prefs that Koan's bookmarks-bar patch ignores. Journeys (history clusters) is on, with no switch short of policy, but nothing in Koan's UI leads to it once LB1 lands.

### 3.1 How Koan sits on upstream today (the model)

- **Library = Koan widget, upstream data.** `KoanLibraryController` (`koan_library.h`, `koan_library_controller.cc`) owns a `TYPE_CONTROL` child widget over the sidebar and content area (`CreateWidget`, `:346-358`). The sections read upstream services directly:
  - History: `HistoryService::QueryHistory`, `TabRestoreService`, `BrowsingDataRemover`, with `ScopedObservation` and `CancelableTaskTracker` (`koan_library_history.cc:836-867`).
  - Downloads: `AllDownloadItemNotifier` on the profile's `DownloadManager`, rows keyed by GUID (`koan_library_downloads.cc:515-560`).
  - Spaces: the tab groups.
  
  It is available only in normal, non-OTR windows (`IsAvailable`, `koan_library_controller.cc:173-178`). The last section is stored in `koan.library.last_section` (`koan_prefs.cc:42`).
- **Entry points Koan owns:**
  - ⌥⇧L, and on Mac ⌘Y and ⇧⌘H → History, ⇧⌘J → Downloads (`koan_shortcut_table.cc:172-201`). The Mac keys are registered at high priority so they beat Chrome's menu key equivalents (`koan_shortcuts.cc:96-108`). TODO(#51) says this is not yet checked on a Mac.
  - the sidebar Library button;
  - the command palette "open-library" (`koan_commands.cc:220-222`).
  
  In a private window the keys fall through to Chrome's menu (`koan_shortcuts.cc:261-293`).
- **Download animation** (`KoanDownloadAnimation`) observes `DownloadManager::OnDownloadCreated` and flies an arc to the Library button. The *only* upstream change is `download_toolbar_ui_controller.cc.patch` @504: `ShowPendingDownloadStartedAnimation` returns early when `koan::ShowsDownloadAnimation()` (Koan pref on and rich animation allowed, `koan_browser_view_hooks.cc:28-34`). Nothing else in the download bubble is touched.
- **Bookmarks bar:** `bookmark_bar_controller.cc.patch` makes `koan.bookmarks_bar.show` (default false, `koan_prefs.cc:47`) the only switch. Upstream's `kShowBookmarkBar` / `kBookmarkBarVisibilityState` are ignored: `should_show_bar = true` once Koan's pref is on.
- **Ctrl+B** → `IDC_SHOW_BOOKMARK_SIDE_PANEL` (`koan_shortcuts.cc:281-282`), which is upstream's WebUI bookmarks side panel.
- **Ctrl+M** → `IDC_WINDOW_MUTE_SITE` (`koan_shortcuts.cc:279-280`); see M-section MD3.
- Koan has **no hook** in `browser_command_controller.cc` / `browser_commands.cc` / `chrome_pages.cc` for history, downloads or bookmarks. The only hooks there are fullscreen, new tab and close tab.

### 3.2 Inventory

#### 3.2.1 Commands, accelerators, menus (history / downloads / bookmarks)

| Upstream item | Where (pristine) | Koan today | Reachable? | Zen 1.24b |
|---|---|---|---|---|
| `IDC_SHOW_HISTORY` → `chrome::ShowHistory` → singleton `chrome://history` tab (OTR: incognito disclaimer dialog) | `browser_command_controller.cc:1316-1318`; `chrome_pages.cc:334-359`; action `kActionShowHistory` `chrome_action_id.h:157` | **Key only:** ⌘Y is taken by Koan at high priority *(live check, #51)*. The command itself is not redirected | **Yes:** Mac History > "Show Full History" (`main_menu_builder.mm:401-405`, menu click → command, not a key), the app menu History submenu (`app_menu_model.cc:2225-2237`, `Mode::kNone` only), Recent-tabs "Show full history", Library's own "Open the History page" (`koan_library_history.cc:222-225`, intended) | The `Browser:ShowAllHistory` **command** opens the Library History (`zen-sets.js:22`); ⌘Y |
| ⌘Y / ⇧⌘H key equivalents (`IDC_SHOW_HISTORY`, `IDC_HOME`) | `accelerators_cocoa.mm:56, 99` | Taken by Koan (high priority) | Only when Koan's accelerator misses, e.g. focus in a native sheet *(live check)* | ⌘Y History, ⇧⌘H `key_gotoHistory` (both → Library) |
| `IDC_SHOW_DOWNLOADS` → `chrome::ShowDownloads`: hides the bubble, opens `chrome://downloads` | `browser_command_controller.cc:1327-1329`; `chrome_pages.cc:366-380`; `kActionShowDownloadsPage` `chrome_action_id.h:159` | **Key only** (⇧⌘J) | **Yes:** Mac Window > "Downloads" (`main_menu_builder.mm:486-488`), **⌥⌘L** (`global_keyboard_shortcuts_mac.mm:147`, not taken by Koan), app menu (`app_menu_model.cc:2251-2255`), the bubble's "See all downloads", Library's dangerous-download fallback (`koan_library_downloads.cc:926-937`) | `Tools:Downloads` command → Library Downloads (`zen-sets.js:24`); **⌘J** on Mac, ⇧⌘J is the Browser Console (`keyboard-shortcuts.md:167`) |
| `IDC_SHOW_BOOKMARK_MANAGER` ⌥⌘B → `chrome://bookmarks` | `accelerators_cocoa.mm:52-53`; `main_menu_builder.mm:429-430`; `chrome_pages.cc:323-326` | None | Yes: Bookmarks menu, ⌥⌘B | ⇧⌘O "Manage Bookmarks" (Places organizer window) |
| `IDC_SHOW_BOOKMARK_BAR` ⇧⌘B, View > "Always Show Bookmarks Bar" | `accelerators_cocoa.mm:50-51`; `main_menu_builder.mm:297-298` | **Ignored, not redirected.** It flips Chromium's `kShowBookmarkBar`, which the patch disregards | **Yes:** ⇧⌘B and the menu tick toggle while nothing changes on screen; the tick lies | ⇧⌘B toggles the bookmarks toolbar |
| `IDC_BOOKMARK_THIS_TAB` ⌘D / `IDC_BOOKMARK_ALL_TABS`: star bubble anchored to the location bar | `accelerators_cocoa.mm:54`; `main_menu_builder.mm:431-438` | None | Yes. The bubble anchors to the star in a location bar that is hidden unless the URL bar is open *(live check: where the bubble lands)* | ⌘D "Bookmark Tab…" panel |
| `IDC_SHOW_BOOKMARK_SIDE_PANEL` → `SidePanelUI::Show(kBookmarks)` | `browser_command_controller.cc:1305-1308`; enabled `:2108`; app menu `bookmark_sub_menu_model.cc:85-88` | **Used by Ctrl+B** (`koan_shortcuts.cc:281`) | Yes; opens a WebUI panel in Chromium style on the **right** (`side_panel_prefs.cc:40-41`, default `!IsRTL()`) | ⌘B Firefox bookmarks sidebar (Koan binds **Ctrl**+B: a Mac mismatch, `keyboard-shortcuts.md:167`) |
| `IDC_SHOW_HISTORY_CLUSTERS_SIDE_PANEL` (Journeys side panel) | `browser_command_controller.cc:1319-1322`, enabled `:1939-1941` | None | App menu > History > "Grouped history" only (`Mode::kNone`) *(live check)*; no key | none |
| `IDC_READING_LIST_MENU_SHOW_UI` / `IDC_READING_LIST_MENU_ADD_TAB` | `:1568-1575`, enabled `:2114` | None | App menu "Bookmarks and lists"; **tab context menu "Add tab to reading list"** (`tab_menu_model.cc:626-632`); page context menu "Add link to reading list" (`render_view_context_menu.cc:2238-2245`, menu-simplification only) | No reading list |
| Mac **History menu** contents (`HistoryMenuBridge`: Recently Closed, Recently Visited) | `main_menu_builder.mm:369-410` | None | Yes. Items open pages, and "Recently Closed" restores tabs or windows (fine) | Zen History menu: Show All History, Clear Recent History…, Restore Previous Session, Recently Closed Tabs |
| Mac **Bookmarks menu** (`BookmarkMenuBridge` fills every bookmark) | `main_menu_builder.mm:412-445` | None | Yes; fine | Bookmarks menu exists |

#### 3.2.2 Side panel registry (M158)

| Upstream item | Where | Koan today | Reachable? | Zen |
|---|---|---|---|---|
| `SidePanelCoordinator` per window + `SidePanelRegistry` | `browser_window_features.cc:888-895` | None | Yes, for each panel below | Firefox sidebar (legacy, `sidebar.revamp=false` locked) |
| **Bookmarks** side panel, always registered | `browser_window_features.cc:897-899` | Ctrl+B | Yes | ⌘B sidebar |
| **History (by date)** side panel `kByDateHistoryInSidePanel` (**disabled** by default) | `ui_features.cc:379`; `history_side_panel_coordinator.cc:61-63`; `browser_window_features.cc:910-918` | None | No (flag only) | — |
| **History clusters / Journeys** side panel (registered when the by-date panel is off and `IsSidePanelSupported`) | `browser_window_features.cc:914-918`; `history_clusters_side_panel_utils.cc:13-20` | None | Through the command above *(live check)* | — |
| **Reading list** side panel, always | `browser_window_features.cc:928-930` | None | Tab context menu → adds silently. Panel only via the app menu | — |
| Tabs from other devices (`kTabsFromOtherDevicesSidePanel`, disabled), Comments | `ui_features.cc:381`; `browser_window_features.cc:904-923` | — | No | — |
| Saved side-panel state: **no per-window open panel in the session** (no side-panel code in `chrome/browser/sessions`). Prefs: `side_panel.is_right_aligned` (default right), `side_panel.alignment_overrides`, `side_panel.id_to_width`, `side_panel.companion_pinned_to_toolbar`; pinned toolbar actions `toolbar.pinned_actions` | `pref_names.h:1206-1217`; `side_panel_prefs.cc:34-57`; `pinned_toolbar_actions_model.cc:37` | None | Alignment and widths are user state. The panel opens on the **right** even with Koan's sidebar on the left. With "tabs on the right" it shares the sidebar's edge *(live check: overlap with Koan's sidebar or the Library)* | Firefox sidebar on the start side |

#### 3.2.3 Download bubble (the shelf is gone in M158)

`chrome/browser/ui/views/download/` has no shelf view any more, only `bubble/`, `download_in_progress_dialog_view.*` and `download_started_animation_views.*`. The bubble is the only Chromium download UI.

| Upstream item | Where | Koan today | Reachable? | Zen |
|---|---|---|---|---|
| Toolbar downloads button = ephemeral pinned action `kActionShowDownloads` (shown while downloads are recent) | `download_toolbar_ui_controller.cc:146-161`; `browser_actions.cc:2053` | None | Lives in the toolbar. **Hidden** in single toolbar / compact-hide-toolbar. Shows on toolbar reveal, in `Mode::kNone` and with multiple toolbars, next to Koan's Library badge (two download indicators) | `browser.download.autohideButton=false`, but the button lives in Zen's toolbar, and in single-toolbar Zen it moves to the sidebar |
| Download-started animation (`DownloadBubbleStartedAnimationViews`) | `:176-189, 500-523` | **Hooked off** when Koan's animation is on (patch @504) | With `koan.download_animation` off, upstream's animation flies to the **hidden** button's bounds *(live check)* | Zen arc, 1000 ms |
| **Partial bubble auto-opens when a download finishes** (`OnUpdatedItem(is_done, may_show_details)` → `ShowDetails` → `ShowBubble(kPartial)`, with a 5 s auto-close) | `download_display_controller.cc:151-190`; `download_bubble_ui_controller.cc:178-209, 251`; pref `download_bubble.partial_view_enabled` default **true** (`download_prefs.cc:368`; `pref_names.h:66-67`) | **None** | **Yes, every finished download in the last-active window.** The anchor is `GetBubbleAnchorAsync(kActionShowDownloads)` in a hidden toolbar (`:614-622`): either no bubble or one at the toolbar's parked spot *(live check)*. Dangerous downloads also arrive here | `browser.download.alwaysOpenPanel=false`; Zen shows its Library-button download stack instead (library.md §6.1) |
| **Immersive reveal lock** while the bubble shows | `download_toolbar_ui_controller.cc:638-646` | None | In Mac fullscreen it reveals Koan's hidden top chrome when a download finishes *(live check against #157)* | — |
| Fullscreen "download" exclusive-access notice (`ShouldShowExclusiveAccessBubble`) | `:240-278`; `download_display_controller.cc:137-144` | None | In content fullscreen: Chromium's grey "download started" notice. Acceptable | Firefox shows nothing |
| Download in-progress exit warning `DownloadInProgressDialogView` | `browser_view.cc:3200-3209`; `unload_controller.cc:1004-1042` | None | Yes, on quit or last window close with active downloads | Firefox warns on quit with downloads: **reuse** *(live check look)* |
| `chrome://downloads` (WebUI) | `chrome_pages.cc:366-380` | Library falls back to it for dangerous downloads (`koan_library_downloads.cc:926-937`) | Yes (see 3.2.1) | Places "Library" window (legacy) |
| Mac Dock progress, badge count, `NSProgress` per download, Finder progress | `download_status_updater_mac.mm:82-90` and on | None | Yes; native | Firefox has Dock progress: **reuse** |
| `downloads.setUiOptions({enabled:false})` (extension) → `ShouldShowDownloadBubble` false | `download_bubble_prefs.cc:15-26` | Ignored by Koan's Library and animation | Extension | Firefox has no equivalent |

#### 3.2.4 History clusters (Journeys), history embeddings

| Upstream item | Where | Koan today | Reachable? | Zen |
|---|---|---|---|---|
| Journeys enabled on every desktop build (`is_journeys_enabled_no_locale_check = !ANDROID && !IOS`); no `BASE_FEATURE` kill switch left | `components/history_clusters/core/config.h:30-31`; `history_clusters_service.cc:107-125, 154-161` | None | Clustering runs in the background on history. **Visible: chrome://history "By group" tab** (`history_clusters_util.cc:31-43`) | none |
| `history_clusters.visible` (default true). It hides the UI **only when set by policy** (`… \|\| !journeys_is_managed`) | `history_clusters_prefs.cc:27`; `history_clusters_util.cc:36-42` | None | A user pref cannot hide it | — |
| Omnibox Journeys chip / row: `HistoryClustersAction` is created only by `HistoryClusterProvider` (`kOmniboxHistoryClusterProvider` **disabled**) | `features.cc:21-23`; `history_cluster_provider.cc:61`; `autocomplete_controller.cc:568-577` | None | No (flag only) | — |
| `kJourneysImages` (enabled) | `features.cc:16-19` | None | Needs Google metadata; no API keys, so inert *(live check)* | — |
| History embeddings / AI history search `kHistoryEmbeddings` (**disabled**) | `history_embeddings_features.cc:26` | None | No | — |

#### 3.2.5 Koan's merged Library (#64): duplicates, conflicts, teardown

| Item | Where (Koan) | Finding | Risk |
|---|---|---|---|
| Two history UIs | Library History vs `chrome://history` (3.2.1) | Menu and command paths open Chromium's page; Zen sends them to the Library | Looks-like-Chromium. **High** |
| Two or three download UIs | Library Downloads + Koan arc vs the partial bubble + toolbar button + `chrome://downloads` | Finished downloads pop Chromium's bubble (where it can anchor); the toolbar button shows beside Koan's badge on reveal or in `kNone` | Duplicate UI. **High** |
| Dangerous / insecure downloads | `ShowWarning`, `koan_library_downloads.cc:926-937` | Uses the bubble's security subpage **only if the toolbar is drawn**, otherwise opens `chrome://downloads`. In the default single toolbar it always opens the Chromium page. If LB3 turns the partial view off, nothing prompts at all until the user opens the Library | Safety + looks-like-Chromium. **Medium** |
| Bookmarks | Zen's Library has **no bookmarks section** (rail: Routing, Media, Downloads, Boosts, Spaces, History; library.md §1) | No gap in the Library. Bookmarks UI = upstream side panel + `chrome://bookmarks` + bar | OK; style only |
| Library "Open the History page" | `koan_library_history.cc:222-225` | Matches Zen's "Open legacy library" item (`ZenLibrary.mjs:341-348`) | Intended |
| Teardown | `KoanLibraryController::OnBrowserViewDestroying` (`:287-293`): removes the tab-strip observer, Esc accelerator, animations, widget. History: `ScopedObservation` + `CancelableTaskTracker`, timers as members, so `Unretained` callbacks are safe. Downloads: `OnManagerGoingDown` resets the notifier, rows keep GUIDs only | No dangling pointer found. `RemoveObserver` runs even when the observer was never added (unavailable window); harmless (`ObserverList` no-op) | Low |
| Keep-alive after close | `KeepAlive()` → weak-ptr runner (`:400-405`) | Safe after destroy | Low |
| Library vs Chromium side panel geometry | `GetArea()` unions `contents_container` + sidebar (`:423-437`) | An open bookmarks side panel is not part of `contents_container`: the Library slides beside or over it depending on the side *(live check)* | Low |

### 3.3 Changes

Rule set: (1) every history and downloads *command* opens the Library in a window that has one, as Zen's `ZEN_LIBRARY_TARGETS` does; (2) no Chromium download surface appears unbidden; (3) bookmark toggles drive Koan's pref; (4) Journeys and reading list have no entry points.

| # | Change | Verdict | Hook | New patch? |
|---|---|---|---|---|
| LB1 | **Redirect `IDC_SHOW_HISTORY` and `IDC_SHOW_DOWNLOADS`** to `KoanLibraryController::Toggle(kHistory / kDownloads)` (open, not toggle, for menu clicks) when `IsAvailable()`. Otherwise fall through (private window keeps Chromium). This covers the Mac menu, ⌥⌘L, the app menu, the bubble's "See all", extensions' `chrome.downloads.showDefaultFolder`-style callers of `ShowDownloads`, and every `chrome::ShowHistory` caller. Put it next to `MaybeOpenUrlBarForNewTab` in the existing `ExecuteCommandWithDisposition` hunk. Also re-point `kActionShowHistory` / `kActionShowDownloadsPage` per window (shared command-policy table, audit 2 §0.1). Then the high-priority key registration (`koan_shortcuts.cc:96-108`) becomes a belt-and-braces measure | **redirect** | existing `browser_command_controller.cc.patch` + `src/` | no |
| LB2 | **Mac downloads key = ⌘J** (Zen, `keyboard-shortcuts.md:167`), with **⇧⌘J left to Chrome's command**, which LB1 then redirects to the Library anyway. **Bookmarks sidebar = ⌘B**, not Ctrl+B (`koan_shortcut_table.cc:197-201`) | **override** (shortcut table) | `src/` | no |
| LB3 | **Partial download bubble off by default:** `SetDefaultPrefValue(download_bubble.partial_view_enabled, false)`, as Zen's `alwaysOpenPanel=false` does. This also stops the reveal lock firing on completion. In its place, on a *dangerous/insecure* download Koan opens the Library on Downloads with the row highlighted (Zen shows the blocked state in the Library row) | **override** + small `src/` | `koan_prefs.cc` (profile default hook) | no |
| LB4 | **Dangerous-download action in the Library:** replace the `chrome://downloads` fallback with Koan's own Keep / Delete confirm. Use `DownloadItem::ValidateDangerousDownload` / `ValidateInsecureDownload` behind `DownloadUIModel`'s rules, or open the bubble's security subpage anchored to the Library row. Only `kNone` keeps the toolbar path | **reuse** (model) / **override** (UI) | `src/` | no |
| LB5 | **Toolbar downloads button stays hidden in Koan modes:** in `DownloadToolbarUIController::Show()` return early when `koan::IsToolbarHidden()` or a Library exists (Zen moves the button into the sidebar, where the Library button already has the ring). One line next to the existing hook | **lock** | existing `download_toolbar_ui_controller.cc.patch` | no |
| LB6 | **Bookmark-bar toggles drive Koan's pref:** `IDC_SHOW_BOOKMARK_BAR` (⇧⌘B, View menu) toggles `koan.bookmarks_bar.show`. The Mac View item's check state reads Koan's pref, in the same `ValidateUserInterfaceItem` hunk as F3 | **redirect** | existing `browser_command_controller.cc.patch` + `browser_native_widget_mac.mm.patch` | no |
| LB7 | **Bookmarks side panel follows Koan's sidebar side:** write `side_panel.alignment_overrides["kBookmarks"]` so the panel opens on the sidebar's side (Firefox's sidebar is on the start side) *(confirm against Zen live: ⌘B)* | **override** | `src/` | no |
| LB8 | **Reading list off:** remove `CommandAddToReadLater` from the tab menu (Koan already filters tab-menu items in `IsKoanTabMenuCommandEnabled` / its menu hook) and `IDC_CONTENT_CONTEXT_ADD_LINK_TO_READING_LIST`. The side panel stays registered but unreachable | **lock** | existing tab-menu hook / `render_view_context_menu.cc.patch` | no |
| LB9 | **Journeys:** no user-level switch exists. Accept the chrome://history "By group" tab; LB1 removes every menu path to chrome://history except Koan's explicit "Open the History page". Optional: hide the tab via a managed pref from Koan's policy defaults (heavy; defer) | **reuse** (accept) | — | — |
| LB10 | **`IDC_SHOW_BOOKMARK_MANAGER` / ⌥⌘B:** keep `chrome://bookmarks` (Zen opens Places). Add Zen's ⇧⌘O as a second key | **reuse** + key | `src/` | no |
| LB11 | **Leak detector (DCHECK):** in a window with a Library, `DLOG(FATAL)` if a `DownloadBubble` widget (`kBubbleName`) opens without a user click on the toolbar button, or if a singleton `chrome://history` / `chrome://downloads` tab is created without Koan's "Open the History page" | **detector** | `src/` | no |

**Cost.** About 1-1.5 agent-days, one Mac build:
- 2-3 lines each in three existing patches (LB1, LB5, LB6);
- about 150-250 lines in `src/`, of which LB4 is the bulk.

**Rebase watch:** `ShowHistory` / `ShowDownloads` callers, `download_bubble.partial_view_enabled`, `kByDateHistoryInSidePanel` (it would replace the Journeys panel), and Journeys `Config`.

### 3.4 Tests (Mac loop)

1. **L1.** Mac menu History > Show Full History; Window > Downloads; ⌥⌘L; ⌘Y; ⇧⌘H; ⌘J: each opens the Library on the right section. No `chrome://history` / `chrome://downloads` tab appears. The same items in a private window open Chromium's pages, or the incognito disclaimer (LB1, LB2).
2. **L2.** With focus in the omnibox, in the Library's own search field, and on a page: ⌘Y toggles the Library and the menu item does not also run (#51 TODO).
3. **L3.** Download a file from a page: Koan's arc plays and no Chromium arc. When it finishes, **no bubble** appears, in a normal window and in macOS fullscreen. In fullscreen the top chrome does not reveal (LB3).
4. **L4.** Download an `.exe` flagged as dangerous (or a mixed-content file): the Library opens on Downloads with Keep / Delete. No `chrome://downloads` tab (LB3, LB4).
5. **L5.** Compact mode with toolbar reveal and `Mode::kNone` (Ctrl+Alt+B): no downloads toolbar button while downloads are recent (LB5).
6. **L6.** ⇧⌘B and View > Always Show Bookmarks Bar: the bar shows and hides, and the check mark matches (LB6).
7. **L7.** ⌘B: the bookmarks side panel opens on the sidebar's side, in both sidebar positions. Esc / ⌘B closes it. The Library opened over it is laid out correctly (LB7).
8. **L8.** Tab context menu and link context menu: no "reading list" items (LB8).
9. **L9.** Quit with a download running: Chromium's "downloads in progress" dialog. Record its look against Zen's quit warning (reuse).
10. **L10.** Dock: progress on the Dock icon during a download, and a bounce of the Downloads stack when it finishes.
11. **L11.** Close a window while the Library is open mid-slide, with History results pending and a "Clear history?" confirm open: no crash. Reopen the Library 31 s after closing it (widget freed): it works.
12. **L12.** Delete a history entry from `chrome://history` while the Library History is open: the row goes (observer). Remove a download in `chrome://downloads`: the row goes.

### 3.5 Top leaks in this area (ranked)

1. **High: history and downloads commands open Chromium's WebUI pages.** This covers the Mac History and Window menus, ⌥⌘L, the app menu and "See all downloads". Zen redirects the commands, not just the keys. **Fix:** LB1.
2. **High *(live check)*: the partial download bubble auto-opens on every finished download.** It anchors to a hidden toolbar button. In fullscreen it takes an immersive reveal lock, so Koan's hidden top chrome can slide in. Zen opens nothing. **Fix:** LB3 (pref default), LB5.
3. **Medium: dangerous downloads are handled only by Chromium's UI.** In the default single toolbar that means the `chrome://downloads` page. With LB3 alone there would be no prompt at all. **Fix:** LB4, together with LB3.
4. **Medium: bookmark toggles are dead.** ⇧⌘B and View > Always Show Bookmarks Bar flip a pref Koan ignores, so the menu tick lies. **Fix:** LB6.
5. **Medium: Mac key mismatches.** Downloads is ⇧⌘J (Chrome's key) where Zen uses ⌘J, and ⇧⌘J is the Browser Console in Zen. Bookmarks is Ctrl+B where Zen uses ⌘B. **Fix:** LB2.
6. **Low-medium: two download indicators** (the toolbar button beside the Library badge) on toolbar reveal and in `kNone`. **Fix:** LB5.
7. **Low: Chromium's bookmarks side panel opens on the right whatever the sidebar side.** The reading list is still offered in menus. Journeys shows on chrome://history. **Fix:** LB7, LB8, LB9 (accept).

---

## 4. Media controls

Same sources and method as §3. Change ids **MD**, tests **M**.

**Summary.** Koan's media card stack (#66, `KoanMediaController`) is a good port of `ZenMediaController`. It observes `content::MediaSession` over mojo, per tab, and builds no global state of its own. Upstream's media stack still runs in full beside it:
- **Global media controls.** The `MediaToolbarButtonView` is always built on Mac (`kWebUIMediaButton` is off), and its controller shows the button whenever any media session is active. In Koan it sits in the hidden toolbar, and it appears next to Koan's card whenever the toolbar shows.
- **Auto picture-in-picture on tab switch**, with an "Ask" content setting and Chromium's in-PiP permission overlay. **A Space switch is a tab switch**, so leaving a Space with a Meet call or a page that registered `enterpictureinpicture` pops a PiP window. Zen ships `media.videocontrols.picture-in-picture.enable-when-switching-tabs.enabled=false`.
- **Media keys / Now Playing.** These are native and Zen-equivalent: reuse.

Two Koan-specific findings:
- **Ctrl+M mutes the *site* persistently** (`IDC_WINDOW_MUTE_SITE` writes a `SOUND=BLOCK` content-setting exception). Zen's Ctrl+M mutes the *tab*.
- **Group focus-mode freezing votes ignore Koan's "played audio" protection** (`freezing_policy.cc.patch` removes `kPlayedAudio` for `kVoting`). A *paused* player in another Space can therefore be frozen, and Koan's card and the media keys would then control a frozen page *(live check)*.

### 4.1 How Koan sits on upstream today (the model)

- `KoanMediaController`, one per window (`koan_media_controller.h`, `koan_browser_view_controller.cc:71`). `TabWatcher` (a `WebContentsObserver`) watches `OnAudioStateChanged`, `DidUpdateAudioMutingState` and `MediaPictureInPictureChanged` (`koan_media_controller.cc:867-886`). A card is created only if the tab's `MediaSession` is not inactive (`OnAudibleChanged`). `KoanMediaCard` binds a `MediaSessionObserver` receiver (`:116-125`). Its controls are:
  - play / pause: `Suspend/Resume(kUI)`;
  - previous / next track and `SeekTo`;
  - PiP: `MediaSession::EnterPictureInPicture()`, shown only when the session lists `kEnterPictureInPicture` (`:176-178, 510`);
  - mute: `SetTabAudioMuted(…, kAudioIndicator)`, which is **tab** mute (`:179-182`);
  - close: pause, then dismiss (`:185`).
  
  Cards hide while their tab is active or in PiP (video *or* document PiP: `media_session_impl.cc:1179-1185`).
- It is installed into the vertical tab strip's flex column above the Space bar (`InstallView`). With the strip collapsed below `kMediaMinSidebarWidth`, nothing is shown.
- It is enabled by `features::IsMediaControlsEnabled()` + `koan.media_controls.enabled` (default true, `koan_prefs.cc:44`), matching Zen's `zen.mediacontrols.enabled`.
- It changes **no** upstream media code. The only related upstream changes are the freezing-policy patch (`kPlayedAudio`, 1 h protection after audio; but removed for `FreezingType::kVoting`, the type focus-mode votes use) and the feature overrides (`kInfiniteTabsFreezing` on, `koan_feature_overrides.cc`).
- **A `KoanBrowserViewController` is built for every `BrowserView`**, including document-PiP windows and popups (`koan_browser_view_hooks.cc:14-16`). The media controller and download animation run there too.

### 4.2 Inventory

#### 4.2.1 Global media controls, Cast

| Upstream item | Where | Koan today | Reachable? | Zen 1.24b |
|---|---|---|---|---|
| `MediaToolbarButtonView` built on Win/Mac/Linux unless `kWebUIMediaButton` (disabled) | `toolbar_view.cc:420-427, 557-561`; `ui_features.cc:404` | None | Built in every window | none (Zen's only media UI is the sidebar card) |
| Button visibility = any active media item or open dialog (`ShowToolbarButton` / `Hide` / `Disable`) | `media_toolbar_button_controller.cc:41-70` | None | **Yes, whenever the toolbar shows** (compact reveal, `Mode::kNone`, multiple toolbars) and media plays: a **second media control next to Koan's card** *(live check: is it laid out inside Koan's single-toolbar pill?)* | — |
| `MediaDialogView` (GMC bubble): per-session items, Cast device picker, "Save video frame" (`kGlobalMediaControlsSaveVideoFrame` on), seamless transfer (off), auto-dismiss (on) | `media_switches.cc:641-649`; `chrome/browser/ui/views/global_media_controls/media_dialog_view.cc` | None | Through the button above | — |
| Media Router / Cast (`kMediaRouter` **enabled**; `kDelayMediaSinkDiscovery` enabled) | `media_router_feature.cc:47, 53` | None | **Yes:** Mac **View > "Cast…"** (`main_menu_builder.mm:344-345`), page context menu "Cast…" (`render_view_context_menu.cc:2493, 2928`), app menu (`app_menu_model.cc:859`). First use triggers macOS **Local Network** permission *(live check)* | No casting in Zen/Firefox |

#### 4.2.2 Media session, audio focus, media keys, Now Playing

| Upstream item | Where | Koan today | Reachable? | Zen |
|---|---|---|---|---|
| `kMediaSessionService` (on, non-Android); audio-focus enforcement **off** on Mac (several sessions can play at once) | `services/media_session/public/cpp/features.cc:15-32` | Relied on (`MediaSession` mojo API) | Yes | Firefox: also several at once |
| `kHardwareMediaKeyHandling` (on for Mac) → `MediaKeysListenerManagerImpl` + `ActiveMediaSessionController` (follows the focused session) + `SystemMediaControls` (macOS `MPNowPlayingInfoCenter` / `MPRemoteCommandCenter`) | `media_switches.cc:673-686`; `media_keys_listener_manager_impl.cc:49-55`; `components/system_media_controls/mac/now_playing_info_center_delegate*.mm` | None (independent of Koan's card) | Yes: F7-F9 keys, AirPods, Control Center "Now Playing" show the active session with its artwork | Firefox: `media.hardwaremediakeys.enabled`, Now Playing: **reuse** |
| Tab audio indicator, click to mute the **tab** (`TabMutedReason::kAudioIndicator`) | `tab_muted_utils` | Koan's card mute uses the same (`:179-182`) | Yes | Zen tab mute button |
| **Mute site** (`IDC_WINDOW_MUTE_SITE` → `CommandToggleSiteMuted` → `SetSitesMuted` writes a `ContentSettingsType::SOUND` BLOCK exception for the origin) | `browser_command_controller.cc:1435-1437`; `browser_commands.cc:1974-1978`; `tab_strip_model.cc:5656-5700`; tab menu "Mute site" `tab_menu_model.cc:563-584` | **Koan's Ctrl+M sends it** (`koan_shortcuts.cc:279-280`, table label "Toggle Mute") | **Yes:** Ctrl+M mutes every tab of that site **forever** (it survives relaunch; undo only through Ctrl+M on that site or site settings). The tab context menu also says "Mute site" | Ctrl+M = `key_toggleMute`, **tab** mute; tab menu "Mute Tab" |
| Freezing vs media: upstream protects `kAudible` / `kRecentlyAudible`. Koan adds `kPlayedAudio` (1 h), **but removes it, and `kPinnedTab`, for `FreezingType::kVoting`** | pristine `freezing_policy.cc:366-378`; Koan patch hunk @374 | The group focus-mode freezing vote (`kTabGroupsFocusFreezing`, audit 2 G2, not landed) is `kVoting` | **Yes *(live check)*:** pause music in Space A, switch to B, wait past the recently-audible window. A's tab is frozen. Koan's card ▶, Now Playing ▶ and the media keys then act on a frozen renderer | No workspace freezing |

#### 4.2.3 Picture-in-picture

| Upstream item | Where | Koan today | Reachable? | Zen |
|---|---|---|---|---|
| Video PiP window (`VideoOverlayWindowViews`): back-to-tab, play/pause, next/previous track, hang-up/mic/camera (media-session actions) | `chrome/browser/ui/views/overlay/` | Koan card's PiP button → `MediaSession::EnterPictureInPicture` | Yes. "Back to tab" activates the opener, which may be in another Space → Koan heals focus (switches Space) | Firefox PiP window with its own controls: **reuse** |
| Card PiP availability = `IsPictureInPictureAvailable()` or the site's `enterpictureinpicture` handler | `media_session_impl.cc:1946-1978` | `Supports(kEnterPictureInPicture)` | Yes | Zen: `can-pip` only when exactly **one** eligible video (`ZenMediaController.sys.mjs:311-323`); Chromium's rule differs (any PiP-able player) *(live check)* |
| **Document PiP** API (`DocumentPictureInPictureAPI` stable) → a `BrowserView` of type picture-in-picture | `runtime_enabled_features.json5:2649-2653`; `media_switches.cc:542-560` | **A full `KoanBrowserViewController` is created for it** (media controller, download animation, compact mode, …) | Yes (Meet, Spotify mini-player…). Koan's media controller has no strip there, so it builds no view but keeps cards. The download animation can play inside the PiP window *(live check: nothing Koan draws there)* | Firefox: no document PiP |
| **Auto-PiP on tab switch** (`AutoPictureInPictureTabHelper`, every tab: `tab_features.cc:1338-1340`). It fires on `OnTabActivatedChanged(false)` for a site that registered `enterpictureinpicture` (or uses camera/mic) and is playing / audible / https | `auto_picture_in_picture_tab_helper.cc:99-103, 460-491, 716-793` | None | **Yes: a Koan Space switch, a Glance open, Ctrl+Tab and any other tab switch** pop a PiP window for Meet / Teams calls and opted-in players. The first time, Chromium's **auto-PiP permission overlay** (Allow this time / Allow on every visit / Don't allow) appears inside it | **Off:** `media.videocontrols.picture-in-picture.enable-when-switching-tabs.enabled=false` (`prefs/firefox/pip.yaml`) |
| `AUTO_PICTURE_IN_PICTURE` content setting, default **ASK** (OTR treats ASK as block) | `content_settings_registry.cc:643-652` | None | Site settings | — |
| Browser-initiated auto-PiP for any video (`BrowserInitiatedAutomaticPictureInPicture`, runtime flag "experimental"); `kAutoPictureInPictureOnWindowOccluded` (off) | `runtime_enabled_features.json5:1022-1024`; `media_switches.cc:387-388, 415` | — | No (flags). There is no `kAutoPictureInPictureForVideoPlayback` in M158; this is its equivalent | — |
| PiP keyboard shortcut | none in Chromium | none | — | ⇧⌥⌘] `View:PictureInPicture` (`keyboard-shortcuts.md`) |

#### 4.2.4 Koan's merged media controller (#66): duplicates, conflicts, teardown

| Item | Where (Koan) | Finding | Risk |
|---|---|---|---|
| Duplicate UI | Card stack vs `MediaToolbarButtonView` | Both show when the toolbar shows | Looks-like-Chromium. **Medium** |
| PiP entry points | Card PiP button; Chromium auto-PiP; page / video-controls PiP; GMC dialog PiP | Auto-PiP is the conflict: it fires on Space switches, and the card hides while in PiP | **High** (MD2) |
| Observer lifetime | `OnBrowserViewDestroying` (`:915-926`): removes the tab-strip observer, stops the timer, removes the view, then `cards_.clear()` (mojo receivers close) and `watchers_.clear()`. Posted tasks use a weak pointer. `CheckStillPlaying` compares `contents` without dereferencing it. `kReplaced` (discard) and `kRemoved` (close, detach) erase the watcher and the card | Clean. Button callbacks use `Unretained(card)`, but `RemoveCard` destroys the card *after* `UpdateView()` rebuilds the views (`:1092-1103`) | Low |
| Tabs detached mid-play | `kRemoved` on detach, then the other window's `Watch()` starts the card again if audible | Paused media that is moved to another window loses its card until it plays again | Low (Zen is the same per window) |
| Glance contents | Not in the tab strip, so never watched | Media playing in Glance gets no card | Low; *(check Zen)* |
| Doc-PiP / popup `BrowserView`s | Controller built for every window type | Extra observers, no view. Not a crash path found by reading | Low |

### 4.3 Changes

Rule set: (1) Koan's card is the only media chrome in a Koan window; (2) PiP opens only on a user action, as in Zen; (3) Koan's mute key mutes tabs; (4) media in another Space keeps working.

| # | Change | Verdict | Hook | New patch? |
|---|---|---|---|---|
| MD1 | **Hide the GMC toolbar button in Koan windows:** in `MediaToolbarButtonView::Show()`, return when `koan::ShowsMediaControls(browser_view_)`, that is, Koan's card is on (feature + pref). With the card off, it keeps Chromium's behaviour. Alternative with no new patch: hook `ToolbarView::Init` to skip creating `media_button` (needs a `toolbar_view.cc` patch, so also new). Keep the dialog code alive for the Cast flows | **lock** | 1-line hook in `media_toolbar_button_view.cc` | **yes** (tiny) |
| MD2 | **Auto-PiP off by default (Zen):** `SetDefaultContentSetting(AUTO_PICTURE_IN_PICTURE, BLOCK)` via the profile default pref (`profile.default_content_setting_values.auto_picture_in_picture` = 2) in Koan's prefs hook. The user can allow it per site in settings. This covers Space switches, Glance and Ctrl+Tab | **override** | `koan_prefs.cc` | no |
| MD3 | **Ctrl+M = tab mute:** `Action::kMuteSite` → `SetTabAudioMuted(active, !muted, TabMutedReason::kAudioIndicator)`, as the card does. Rename the action to `kMuteTab` and keep the pref key "mute". Change the tab context menu "Mute site" to "Mute tab" (`CommandToggleTabMuted`-style item through Koan's tab-menu hook) *(check whether Zen's menu shows site mute anywhere: no)* | **override** | `src/` (+ existing tab-menu hook) | no |
| MD4 | **Media survives other Spaces:** keep `kPlayedAudio` for `kVoting` too, so a paused player stays unfrozen for 1 h. Or land audit 2 **G2** (no focus-mode freezing votes at all), which also fixes this. Prefer G2, and delete the `kVoting` removal of `kPlayedAudio` only if G2 slips | **override** | existing `freezing_policy.cc.patch` (1 line) or `tab_view.cc.patch` (G2) | no |
| MD5 | **Cast off:** `DisableFeature(media_router::kMediaRouter)` in `koan_feature_overrides.cc`. This removes View > Cast…, the context-menu Cast, the GMC Cast footers and Local Network discovery. Zen has no casting *(confirm the user wants Cast gone; Presentation API sites lose it)* | **lock** | `koan_feature_overrides.cc` | no |
| MD6 | **PiP button rule like Zen:** show the card's PiP only when the tab has exactly one PiP-eligible video, or the site handles `enterpictureinpicture`. Without a per-video count from content, keep Chromium's `IsPictureInPictureAvailable` and record the difference | **reuse** (accept) | — | — |
| MD7 | **No Koan controllers in PiP / popup windows:** `OnBrowserViewCreated` builds the media controller and download animation only for `GetIsNormalType()` windows (the Library already gates) | **lock** | `src/` | no |
| MD8 | **⇧⌥⌘] toggles PiP** for the active tab (`MediaSession::EnterPictureInPicture` / `ExitPictureInPicture`), Zen's `View:PictureInPicture` | **override** (new key) | `src/` shortcut table | no |
| MD9 | **Leak detector (DCHECK):** `DLOG(FATAL)` if the media toolbar button `IsDrawn()` while Koan's card is enabled. Also if a PiP window opens within 1 s of a Koan Space switch with no user PiP action | **detector** | `src/` | no |

**Cost.** About 0.5-1 agent-day:
- one tiny new patch (MD1);
- 1 line in an existing patch, or G2 (MD4);
- the rest is prefs, overrides and `src/`.

**Rebase watch:** `MediaToolbarButtonView::Show`, `AutoPictureInPictureTabHelper` triggers, the freezing `CannotFreezeReasonsForType`, and `kWebUIMediaButton` (it would move the button into the WebUI toolbar and bypass MD1).

### 4.4 Tests (Mac loop)

1. **M1.** Play YouTube in tab A, switch to tab B: Koan's card appears after about 0.5 s. Compact mode: reveal the toolbar, then Ctrl+Alt+B (`kNone`): **no** media toolbar button (MD1).
2. **M2.** Join a Meet test call (or a page registering `navigator.mediaSession.setActionHandler('enterpictureinpicture')`) in Space A, then switch to Space B with Ctrl+2, the swipe and the Space bar: **no PiP window**, and the call keeps running. Allow auto-PiP for the site in settings: the PiP window opens, with no Chromium permission overlay a second time (MD2).
3. **M3.** Card PiP button on a single-video page: the PiP window opens and the card hides. "Back to tab" in the PiP window: the opener tab's Space shows and the card stays hidden while that tab is active.
4. **M4.** Ctrl+M on a YouTube tab: that tab mutes, a second YouTube tab keeps playing, relaunch, and no site exception exists in `chrome://settings/content/sound` (MD3).
5. **M5.** Play music in Space A, pause it, switch to Space B, wait 2 min. `chrome://discards`: A's tab is **not frozen**. The card's ▶, the F8 key and Control Center ▶ all resume it (MD4).
6. **M6.** F7 / F8 / F9 and AirPods: control the last-playing session, with Now Playing artwork and title. With two sessions playing, record which one wins (reuse).
7. **M7.** View menu and page context menu: no "Cast…" (MD5, if adopted).
8. **M8.** A document-PiP mini-player (Spotify web / a demo page): no Koan sidebar, card or download tile drawn in the PiP window. Close it: no crash (MD7).
9. **M9.** Close a window while a card is fading in, while a seek drag is active, and 0.4 s after a tab switch (timer pending): no crash. Discard a playing background tab (`chrome://discards`): its card goes and no crash.
10. **M10.** ⇧⌥⌘] enters and leaves PiP on the active video (MD8).

### 4.5 Top leaks in this area (ranked)

1. **High: auto-PiP fires on every Space switch** (and on Glance and Ctrl+Tab) for call pages and opted-in players. The first time it also shows Chromium's permission overlay. Zen ships this off. **Fix:** MD2 (pref default).
2. **High: Ctrl+M mutes the whole site, permanently.** Upstream's site-mute writes a SOUND block exception; Zen's Ctrl+M mutes the tab. **Fix:** MD3.
3. **Medium *(live check)*: a paused player in another Space can be frozen** by the focus-mode `kVoting` vote. Koan's patch drops its own `kPlayedAudio` protection for that type. The card, media keys and Now Playing then do nothing. **Fix:** MD4 / G2.
4. **Medium: Chromium's media toolbar button appears beside Koan's card** whenever the toolbar shows. **Fix:** MD1.
5. **Low-medium: Cast entry points** (View > Cast…, context menu) and Local Network discovery, which Zen doesn't have. **Fix:** MD5 (needs a decision).
6. **Low: Koan controllers run in document-PiP and popup windows.** No PiP key. The card's PiP rule differs from Zen's. **Fix:** MD7, MD8, MD6 (accept).

---

## 5. Pinned tabs and Essentials

Date: 2026-11-05. Chromium 158.0.8127.2 (`681cd621e8`), Koan `develop` dcf9bdaa (merged #59 = 5ef046ef), open PR #150 (`fix/pinned-section-137`). Read only: pristine Chromium from `git show HEAD:` / `git grep HEAD`, Zen from `.cache/zen-desktop-1.24b`. Nothing was built or run. Verdicts marked *(live check)* need a Mac run. Ids: changes **PN**, tests **P**. **G** = the tab-groups audit (`tab-groups-omnibox-fullscreen.md` §1), **A** = the vertical-tabs audit.

**Summary.** Koan maps Zen's two pinned kinds onto Chromium like this:
- **Essentials are Chromium's pinned tabs.** They live in `PinnedTabCollection`, outside every group. Upstream focus mode treats a pinned tab as valid in any focused group, so Essentials show in every Space for free.
- **Zen's per-Space pinned tabs are *not* pinned for Chromium.** They are ordinary tabs at the top of the Space's group, flagged `KoanTabState::pinned_in_space`, and kept at the top after the fact by `KoanPinnedTabController::Normalize()`.

This model is sound, and it is the only one upstream allows: a pinned tab can't be in a group. But it has three structural leaks:
1. Every upstream path that pins or unpins goes through `SetTabPinnedImpl`. That function always **ungroups**, and on the active tab it **exits focus mode**. Koan heals afterwards (G's `EnsureOneSpaceShown`), but the heal puts the tab at the **end** of the Space and can make Koan **drop the tab's Space-pinned state**.
2. Every Chromium-facing surface sees only Essentials as "pinned": extensions, the `pinned_tabs` startup pref, the Mac Tab menu checkmark, focus freezing. Space-pinned tabs are invisible or lost there.
3. Upstream's own pin entry points create **Essentials** (`tabs.update({pinned})`, `tabs.create({pinned})`, `--app` pinned launches). Zen's do the opposite: Firefox's `pinTab` makes a Space-pinned tab.

The tab-menu Pin, the Mac Tab menu "Pin Tab", ⌘W and the close button are all redirected correctly today. #150 is a reasonable layout fix, but it puts a foreign view inside upstream's `TabGroupView`, and #60 does the same in the same loop. One hook should own that loop (FD3).

### 5.1 The model (Chromium M158 tab collections, and where Koan sits)

- **Collections and their invariants** (`components/tabs`):

  | Collection | Children | Where |
  |---|---|---|
  | `TabStripCollection` | exactly one `PinnedTabCollection` (index 0), then one `UnpinnedTabCollection` (index 1); no tabs of its own | `impl/tab_strip_collection.cc:59-69` |
  | `PinnedTabCollection` | tabs and `SPLIT` only | `impl/pinned_tab_collection.cc:10-11` |
  | `UnpinnedTabCollection` | tabs, `GROUP`, `SPLIT` | `impl/unpinned_tab_collection.cc:17-18` |
  | `TabGroupTabCollection` | tabs and `SPLIT` only | `impl/tab_group_tab_collection.cc:20-22` |
  | `SplitTabCollection` | tabs only | `impl/split_tab_collection.cc:19` |

  - `AddCollection` `CHECK`s the child type (`public/tab_collection.h:247`). The type enum is `TABSTRIP, PINNED, UNPINNED, GROUP, SPLIT` (`tab_collection.h:153-161`).
  - **Placement: pinned beats group.** `GetInsertionDetails(index, pinned, group)` goes to the pinned collection whenever `pinned` is true, whatever the group (`tab_strip_collection.cc:740-775`).
  - `ConstrainInsertionIndex` and `ConstrainMoveIndex` clamp pinned tabs to `[0, IndexOfFirstNonPinnedTab]` (`tab_strip_model.cc:3829-3838`). So **all pinned tabs come before all unpinned tabs, and a pinned tab is never in a group.**
- **Pin and unpin always ungroup.**
  - `SetTabPinnedImpl` moves the tab to `IndexOfFirstNonPinnedTab()` (pin) or `IndexOfFirstNonPinnedTab()-1` (unpin) with `group = nullopt` (`tab_strip_model.cc:5574-5584`). An unpinned Essential therefore lands **loose** (in no group), first after the Essentials.
  - `SetSplitPinnedImpl` keeps a split together (`:5586-5608`), but only `SetTabsPinned` (`:5525`) uses it. The public `SetTabPinned(index)` (`:1433-1442`) moves one tab and takes the split apart, which Koan's `SetEssential` repairs.
  - Going the other way, `AddToExistingGroup` **unpins**: `MoveTabsAndSetPropertiesImpl(..., group, /*pinned=*/false)` (`:4895-4945`, `:4949`).
- **Focus mode and pinning.**
  - `IsTabValidInFocusedGroup` returns `group == focused || tab->IsPinned()` (`tab_strip_model_selection_state.cc:53-61`).
  - `MoveTabsWithNotifications` → `MaybeUpdateFocusModeForMovedTab` (`tab_strip_model.cc:5291-5313`) **exits focus mode with `kUnpinActiveTab`** when the *active* tab is unpinned (it is then loose), or with `kActiveTabGroupOperation`.
  - The view filter hides every unpinned child that is not in the focused group, **loose tabs included** (`unpinned_tab_container_view.cc:151-166`). The pinned container is never filtered.
- **Koan today (#59):**
  - Essentials = `IsPinned()`, capped at 12 (`koan_pinned_tabs.h` `kMaxEssentials`).
  - Space-pinned = `KoanTabState::pinned_in_space`, saved as per-tab session extra data `koan.pinned_tab`.
  - `koan::IsPinnedForUser()` (`koan_pinned.h`) is the "either kind" check, used by upstream's Close Others / Close Below (`tab_strip_model.cc.patch` @3948), back-to-opener, and Clear.
  - The page's pinned live state (`PageLiveStateDecorator::SetIsPinnedTab`) is set for both kinds, so Koan's `CannotFreezeReason::kPinnedTab` patch exempts them.
  - Drop rules, the cap and "pinned run at the top of the Space" are enforced by `Normalize()`. It runs after moves, waits for `TabDragController::IsActive()` and `SessionRestore::IsRestoring()`, and `changing_pin_` suppresses it for Koan's own moves.

### 5.2 Inventory

#### 5.2.1 State, collections, layout

| Upstream item | Where (pristine) | Koan today | Reachable? | Zen 1.24b |
|---|---|---|---|---|
| Pinned / unpinned / group collections and the "pinned never grouped, pinned first" invariant | §5.1 | **Reused**: Essentials = pinned collection; Space-pinned tabs = the first tabs of the group | Always | Essentials `zen-essential` + `pinned`; Space-pinned tabs = `pinned` with `zen-workspace-id` (`essentials-and-pinned.md` §1). Both are Firefox-pinned |
| `PinnedTabContainerView` and its vertical layout (grid; one column while collapsed; **uniform tile width, doubled for every tile if any child is a split**) | `pinned_tab_container_view_vertical_layout.cc:60-110` (split doubling `:70-77`) | **Hooked**: `koan::ClampEssentialsColumns` (≤4 per row, `pinned_tab_container_view_vertical_layout.cc.patch`); tiles painted by `PaintEssentialsTile` | One split Essential makes **all** tiles double width, so 2 per row *(live check against Zen)* | `repeat(auto-fit, minmax(max(23.7%, min), 1fr))` with the count hacks (`essentials-and-pinned.md` §2) |
| Pinned container height cap (≤ half the strip unless the unpinned part is short) | `tab_strip_view_layout.cc:25-44` | None | With 12 Essentials in a short window the grid scrolls inside its own `pinned_tabs_scroll_view_` (`tab_strip_view.cc:180`) *(live check)* | Grid outside the scrollbox; it never scrolls, max 12 |
| Space-pinned order ("pinned run" at the start of the group) | Not upstream | **Owned** by Koan (`MoveIntoPinnedTabs`, `MoveAfterPinnedTabs`, `Normalize`) | Healed after any upstream move | Zen keeps a separate pinned container per Space |
| Focus freezing: `TabView::UpdateFocusFreezing` votes on `!tab->IsPinned()` tabs outside the focused group | `tab_view.cc:470-490` (`:485`) | Space-pinned tabs of other Spaces **get a `kFocusedGroup` freezing vote** (Chromium thinks they are unpinned). Koan's `kPinnedTab` CannotFreezeReason probably wins over the vote *(live check: chrome://discards)* | Every Space switch (G2 not merged) | Pinned tabs are never frozen by Space |
| Separator between the pinned grid and the unpinned scroll view | `tab_strip_view.cc` (`GetTabsSeparator`) | develop: Koan's rows (separator + Clear, New Tab) sit after it, **above** the Space-pinned tabs. #150 moves them into the Space's `TabGroupView` after the pinned run | — | Header → pinned (and folders) → separator/Clear → "+ New Tab" → tabs |

#### 5.2.2 Commands, menus, accelerators, Mac menu bar

| Upstream item | Where | Koan today | Reachable? | Zen |
|---|---|---|---|---|
| Tab menu "Pin / Unpin" `CommandTogglePinned` (label from `WillContextMenuPin`, which counts Essentials only) | `tab_menu_model.cc:539-551`; enable `tab_strip_model.cc:2731`; execute `:2968-3000` (pin + `OnRemovingAllTabsFromGroups` saved-group delete confirm) | **Redirected**: `ExecutePinnedTabMenuCommand` → `TogglePin` (Essential → Remove from Essentials, Space-pinned → unpin, else → pin to Space). Relabelled with `IsPinnedForUser`; hidden on Essentials; Add/Remove Essentials, Change Label/Icon, Unload, Edit Pinned URL, Reset inserted (`koan_pinned_tabs.cc` `AddPinnedTabMenuItems`) | Redirect covers the views menu and every `ExecuteContextMenuCommand` caller | Same items (`context-menus.md`) |
| `IDC_WINDOW_PIN_TAB` (35013) / `kActionWindowPinTab` → `chrome::PinTab` → `ExecuteContextMenuCommand(active, CommandTogglePinned)`; `IDC_PIN_TARGET_TAB` (35051, ⌥ alternate, keyboard-focused tab) → `PinKeyboardFocusedTab` | `chrome_command_ids.h:137,165`; `chrome_action_id.h:75,108`; `browser_actions.cc:3328-3340`; `browser_commands.cc:1785-1789, 1989-1996`; `browser_command_controller.cc:1438-1439, 1500-1501` | **Redirected** through the same `ExecuteKoanTabMenuCommand` hook | Mac menu bar **Tab > Pin Tab** and ⌥ "Pin Target Tab" (`main_menu_builder.mm:543-547`) | **Zen's menu bar has no Tab menu** (zen-mac-spec §11) |
| Mac validation: "Pin Tab" toggle state = `!WillContextMenuPin(active)` | `browser_native_widget_mac.mm:603-607` (enable `:468, 484`) | None | **Wrong check**: a Space-pinned tab shows *unchecked*, an Essential shows *checked*; choosing it on an Essential removes it from Essentials | n/a (no Tab menu) |
| ⌘⇧D (Zen `cmd_zenTogglePinTab`) | `koan_shortcuts.cc:455-465` | **Owned**: `TogglePin` | — | Same |
| Close paths: `chrome::CloseTab` (⌘W, File > Close Tab) incl. the `kClosePinnedTab` "press again" toast; close button / middle click `TabStripCollectionController::CloseTab` | `browser_commands.cc:1457-1530` (toast `:1507-1525`); `toast_id.h:39` | **Redirected** (`browser_commands.cc.patch` hook before the split/toast logic; `tab_strip_collection_controller.cc.patch`), Zen's six close behaviours | The upstream toast is unreachable while pinned tabs are on (every Chromium-pinned tab is `IsPinnedForUser`) | Same |
| Close Others / Close Below skip pinned | `tab_strip_model.cc` `GetTabsClosedByCommand` | **Hooked** (`IsPinnedForUser`) | — | Same |
| Saved-group confirm on pin (`OnRemovingAllTabsFromGroups`) | `tab_strip_model.cc:2976-2998` | Bypassed by the redirect; Spaces aren't saved groups (G) | No | n/a |
| "Add to group" submenu unpins first (`ExistingTabGroupSubMenuModel`) | `existing_tab_group_sub_menu_model.cc:303-309` | Item removed for Spaces (G) | No | n/a |

#### 5.2.3 Saved state: session, tab restore, startup pref, reset

| Upstream item | Where | Koan today | Reachable? | Zen |
|---|---|---|---|---|
| `kCommandSetPinnedState` (id 12) in the session; `SetPinnedState`, rebuild in `BuildCommandsForTab` | `session_service_commands.cc:82, 1223-1228`; `session_service_base.cc:394-401, 675` | **Reused** for Essentials. Space-pinned = `pinned=false` + group + Koan's `koan.pinned_tab` extra data (`session_service_base.cc.patch`, `browser_tabrestore.cc.patch` → `OnRestoringPinnedTab`) | — | `zen-sessions` / sessionstore with `pinned` |
| Restore placement: pinned tabs are pushed behind the last existing pinned tab, the index is ignored; group ids are **relabelled** per restore | `session_restore.cc:1139-1143, 1181-1203` | Essentials restored by upstream; Space-pinned re-placed by `Normalize` (`changed_group` while restoring) | Crash "Restore" into an open window: Essentials are appended to the existing ones and can pass 12 → `Normalize` demotes the extras to plain Space tabs, pinned URL cleared *(live check)* | Essentials shared across windows, so no duplicates |
| Tab restore (⌘⇧T) carries `pinned` + extra data | `browser_live_tab_context.cc:398, 458` (+ Koan `GetExtraDataForTab` hook) | **Reused** | — | Same |
| **`pinned_tabs` startup pref** (`prefs::kPinnedTabs`, tracked pref `ENFORCE_ON_LOAD`). `PinnedTabService` writes it on quit/last-window close; `PinnedTabCodec` encodes only `IsPinned()` tabs and only their **last committed URL**. `GetPinnedTabsForState` returns them unless the startup pref restores the last session or another tabbed window exists | `pref_names.h:858`; `chrome_pref_service_factory.cc:139`; `pinned_tab_service.cc:33-108`; `pinned_tab_codec.cc:38-53, 68-71, 74-90, 109-`; `startup_tab_provider.cc:124-131, 242-250`; `startup_browser_creator_impl.cc:660`; factory `chrome_browser_main_extra_parts_profiles.cc:1295` | Default startup is LAST (`session_startup_pref.cc.patch`), so it is dormant. Documented in ZEN_PARITY row 123 for Essentials only | **User:** chrome://settings/onStartup "Open the New Tab page" / "specific pages", or a copied Chrome profile. Then on relaunch: Essentials come back as Chromium pins at their *current* URL (pinned URL, label and icon lost, because `InitPinnedState` takes the current URL), **Space-pinned tabs are gone**, and so are Spaces (G9) and folders | When not resuming the session Zen restores **only the pinned tabs**, i.e. Essentials + Space-pinned + folders with their state (`ZenSessionManager.sys.mjs:321-343, 373-377, 430-433`) |
| Profile reset unpins every tab | `profile_resetter.cc:410-428` | None | chrome://settings/reset: all Essentials become plain tabs in the active Space (pinned URLs cleared by `Normalize`); Space-pinned flags survive | n/a |
| Saved tab groups "pinned" (= pinned to the bookmark-bar saved-groups row: `SavedTabGroup::is_pinned()` / `SetPinned`, pref `auto_pin_new_tab_groups` default true, `kActionTabGroupPin`, "Pin group" in saved-group menus) | `saved_tab_group.h:107,165`; `pref_names.cc:36`, `pref_names.h:42`; `tab_group_dynamic_menu.cc:281-283`; `saved_tab_group_tabs_menu_model.cc:151` | Inert: Spaces are never saved (G) | Only with pre-existing saved groups (G8) | n/a |

**Naming collision (flag it in code and docs).** In M158, "pinned" means at least five unrelated things. Koan code should say "Essential" / "Space-pinned" and never "pinned group":
- tab pinned (Essentials);
- **saved-group pinned to the bookmarks bar** (`is_pinned`, `auto_pin_new_tab_groups`), which is *not* a Space-pinned group;
- Glic "tab pinned to a conversation" (`tab_menu_model.cc:267`, `tab_strip_model.cc:3237`);
- pinned toolbar actions;
- the web-app "pinned home tab" (`tab_menu_model.cc:156`).

#### 5.2.4 Extensions and other programmatic pins

| API / caller | Where | Effect today | Verdict |
|---|---|---|---|
| `tabs.update({pinned:true/false})` → `TabListBridge::PinTab/UnpinTab` → `SetTabPinned` | `tabs_api.cc:1461-1481`; `tab_list_bridge.cc:302-314` | `true` makes an **Essential** (in Zen it makes a Space-pinned tab). Past 12, `Normalize` demotes it to a plain Space tab, so the pin is silently undone. `false` on an Essential: loose, then healed to the **end** of the Space. A split tab is split apart | **Leak** (PN3) |
| `tabs.create({pinned:true})` | `tabs_api.cc:863, 1119` | New Essential (cap as above) | Leak (PN3) |
| `tabs.query({pinned})`, `Tab.pinned` | `tabs_api.cc:737`; `extension_tab_util.cc:380` | Space-pinned tabs read as `pinned:false`. Tab suspenders and "close unpinned" extensions close or discard them | Leak-lite (PN3) |
| `tabs.group({tabIds:[essential]})`, `tabs.move` into a group | `AddToExistingGroup` unpins | The Essential becomes a Space tab; Koan's `was_essential` path clears its pinned URL | Acceptable; document |
| `tabGroups.move({index})` into the pinned range | `tab_groups_api.cc:51-72` | Rejected upstream | Fine |
| `tabs.highlight` / split create need matching pinned state | `tabs_api.cc:2364-2385` | An Essential can't be split with a Space tab through the API | Fine |
| Web app "open in pinned tab" launch, AI overlay tools | `application_launch.cc:319`; `ai_overlay_dialog/tools/tools_workspace.cc:738,1228` | New Essential | Rare; covered by PN3's hook if it is placed in `TabStripModel` *(live check: whether the AI overlay is reachable)* |

#### 5.2.5 Drag

| Upstream item | Where | Koan today | Reachable? | Zen |
|---|---|---|---|---|
| A drag carries only tabs whose pinned state matches the source tab | `tab_drag_handler.cc:220-233` | Used | Dragging an Essential plus Space tabs drags the Essential only | Zen drags the multiselection |
| Drag from the grid into the list (unpin) and from the list onto the grid (pin) | drag controller → `MoveTabTo` with pinned | **Healed** by `Normalize` (Zen's drop rules) | Yes | Same rules |
| Drop onto a **full** grid (12) | — | Accepted, then `RemoveFromEssentials` → **a Space-pinned tab dropped there comes back as a plain tab** (pinned URL lost) | Yes | Refused (`canEssentialBeAdded`), tab unchanged (ZEN_PARITY row 130 covers the Essential case only) |
| Drop at the pinned/normal boundary gives one model index for both sides | — | #150 caveat: keeps its kind | Yes | By section |

#### 5.2.6 Interplay with Spaces (groups + focus mode)

| Path | What upstream does | Koan result | Risk |
|---|---|---|---|
| **Remove from Essentials / Pin-in-Space on the *active* Essential** (`SetEssential(false)`) | `SetTabPinned(false)` → loose → `ExitFocusMode(kUnpinActiveTab)` *synchronously* → `GetFocusedGroup()` is now nullopt, so `SetEssential`'s `AddToExistingGroup` is skipped | The tab stays loose. G's heal adds it with `add_to_end=true` (`koan_workspaces.cc:1180-1201`), so it ends up at the **bottom** of the Space. Zen puts it at the top of the list (or the start of the pinned section). Focus exit and re-entry also flash: G1 colour, all groups shown for a task, #150 rows moved out and back | Medium, visible on every use *(live check)* |
| Same path on a *non-active* Essential | No focus exit | Correct (group start, then `MoveAfterPinnedTabs`) | — |
| **Transient ungroup of a Space-pinned tab** (Mac Tab > "Group Tab" toggle, ⌃⇧PgUp/PgDn at the group edge, `tabs.ungroup`, `tabs.move` out of range) | Tab loose; G heals with `add_to_end=true` | `TabGroupedStateChanged` only sets `changed_group` when **both** groups are set, so it stays false. The heal's move fires `kMoved` → `Normalize` sees `pinned_in_space && !InPinnedRun` → **`ClearPinnedState`: the tab silently stops being pinned** (pinned URL gone) | Medium, state loss *(live check)* |
| Pin the last tab of a Space (menu redirect uses Koan; an extension's `tabs.update` doesn't) | Group emptied and destroyed | G heal recreates the Space with a new tab, same id | Low |
| Focus mode hides groups | Pinned container never filtered; other Spaces' groups hidden | Essentials in every Space, Space-pinned per Space: **intended** | — |
| `chrome://flags/#tab-groups-focusing` Disabled | No focus mode | Every Space's pinned tabs show at once, under Chrome group headers | G13 |
| Per-window Essentials | Pinned tabs belong to one `TabStripModel` | Each window has its own Essentials (ZEN_PARITY row 99, #75) | Parity gap, not a leak |

#### 5.2.7 Open PR #150 (pinned section above the separator)

The approach: the separator and New Tab rows become a child of the active Space's `TabGroupView`. `koan::PlaceSpaceTabRows` adds them to the group's vertical layout after the leading pinned run (`IsInPinnedSection` = `IsPinnedForUser`). They are moved by `MoveRowsTo` on `OnTabGroupFocusChanged`, and two new hook patches (`tab_group_view.cc` `ShouldSnapToTarget`, `tab_view.cc` `UpdateColors`) go with it.

Checked against upstream:
- **Safe:**
  - `TabGroupView::GetCollectionNodeFromView` returns null for foreign views (`tab_group_view.cc:414-422`).
  - The vertical layout iterates node children, not view children.
  - Link drop iterates `collection_node_->children()` (`:449-480`).
- **Fragile:** `TabCollectionAnimatingLayoutManager` keys its maps by `host_view()->children()` (`tab_collection_animating_layout_manager.cc:431, 569, 760, 1041, 1085`). It also has `CHECK(contains(children, child))` (`:636`, `NotFatalUntil::M162`) and `CHECK(!target_view_layout_map_.contains(child_view))` (`:888`). #150's `ForgetRows` and its teardown rules are the right mitigations, but they depend on upstream's internals. Add a REBASE row (PN6).
- **Synchronous reparent** inside `OnTabGroupFocusChanged`. Every upstream focus *exit* (the active-Essential unpin above, `AddToNewGroup` `kGroupCreated` on each Space creation, ungroup) moves the rows to the strip and back within a task *(live check: flicker or a CHECK in a DCHECK build)*.
- **Collides with #60.** Both PRs add foreign rows to the *same* `for (child : children)` loop in `tab_group_view_vertical_layout.cc`, each with its own running `height`.
  - #150 decides "pinned section" by `IsPinnedForUser`.
  - #60 makes folder tabs *not* pinned and puts them right after the pinned run.
  - Merged as they are, folders land **below** the separator and New Tab row. Zen puts them above, in the pinned section.
  - See FD2/FD3.

### 5.3 Changes

Rule set:
1. Essentials stay Chromium-pinned (**reuse**).
2. Space-pinned stays Koan state, but it is *sticky*: only an explicit user unpin or a drop outside the pinned run clears it.
3. Every Chromium entry point that pins or unpins goes through `KoanPinnedTabController` (**redirect**).
4. Chromium's own pinned persistence never competes with Koan's (**lock**).

| # | Change | Verdict | Hook | New patch? |
|---|---|---|---|---|
| PN1 | **Unpin into the Space in one move.** In `SetEssential(false)`, replace `SetTabPinned(false)` + `AddToExistingGroup` with a single `AddToExistingGroup(indices, space)`. That call unpins as it groups (`tab_strip_model.cc:4895-4945`), keeps splits (`kRetainCollectionTypes = {SPLIT}`), and leaves the active tab valid in the focused group, so there is no focus exit. Use the *focused* group captured **before** the call. Keep `SetTabPinned` only for windows without Spaces | override | `src/` | no |
| PN2 | **Sticky Space-pinned state.** In `TabGroupedStateChanged`, set `changed_group` whenever `new_group` is set and the tab is `pinned_in_space`, even if `old_group` was nullopt. That way a heal re-places the tab (`MoveIntoPinnedTabs`) instead of clearing it. Clear only on a drag (`TabDragController::IsActive()` seen during the move) or an explicit unpin | override | `src/` | no |
| PN3 | **Extension and programmatic pins.** Hook `TabStripModel::SetTabPinned` (1 line: `if (koan::RedirectSetTabPinned(this, index, pinned)) return …`) so every non-Koan caller is handled: `TabListBridge`, app launch, `ProfileResetter`, the AI tools. With Spaces on: `pinned=true` → `PinInSpace` (Zen semantics), `pinned=false` → unpin either kind. Koan's own `SetEssential` bypasses it with `changing_pin_`. Report `Tab.pinned = IsPinnedForUser` in `ExtensionTabUtil` (`extension_tab_util.cc:380`) and in the query matcher (`tabs_api.cc:737`). Decide against Zen live first: Firefox's `tabs.update({pinned})` = `gBrowser.pinTab` = a Space-pinned tab *(live check)* | redirect | existing `tab_strip_model.cc.patch` + 2 lines in a new `extension_tab_util.cc` / `tabs_api.cc` patch (or accept `Tab.pinned` = Essentials only, and document) | 1 (optional) |
| PN4 | **Lock `pinned_tabs`.** Under Koan, `PinnedTabCodec::ReadPinnedTabs` returns empty, or Koan `ClearPref(kPinnedTabs)` on profile load and `PinnedTabService` writes nothing. Replace it with Koan's pinned-only restore: G9's `koan.workspaces.list` grows to also hold Essentials and Space-pinned tabs (URL, pinned URL, label, icon, Space) and folders (FD6). On a startup without session restore, recreate them unloaded, as Zen's `#shouldRestoreOnlyPinned` does | lock + override | `src/` (pref clear) or a 1-line `pinned_tab_codec.cc` patch | no / optional |
| PN5 | **Mac Tab menu.** Zen has no Tab menu. Short term: in the shared `ValidateUserInterfaceItem` hunk (A6/G7), set the "Pin Tab" toggle from `IsPinnedForUser(active)` and its title "Unpin Tab"/"Pin Tab"; hide `IDC_PIN_TARGET_TAB`. Long term: hide the whole Tab menu, or rebuild it as Zen's "Spaces" menu | override / lock | extend `browser_native_widget_mac.mm.patch` | no (existing) |
| PN6 | **#150 before merge:** (a) defer `MoveRowsTo` from `OnTabGroupFocusChanged` when `new_focused_group_id` is nullopt (focus exits are always healed), so the rows never leave the Space for a heal; (b) define the pinned section through one predicate, `koan::IsInSpacePinnedSection(tab)` = Space-pinned **or in a folder** (FD2), shared with #60; (c) REBASE rows for `TabCollectionAnimatingLayoutManager`'s child-keyed maps and the `:636/:888` CHECKs; (d) run the DCHECK build with 20 Space switches + Space create/delete + active-Essential unpin | override | #150's `src/` | no |
| PN7 | **Full grid drop:** with 12 Essentials, a tab dropped on the grid goes back to the state it had (Space-pinned or normal) at its old position, instead of `RemoveFromEssentials` clearing it. Record the pre-drag state in `OnTabPinnedStateChanged` | override | `src/` | no |
| PN8 | **Freezing:** land G2 (`UpdateFocusFreezing` returns early for Spaces windows) so Space-pinned tabs in other Spaces never carry a `kFocusedGroup` vote. Until then, confirm `kPinnedTab` blocks the vote *(live check)* | override | existing `tab_view.cc.patch` (G2) | no |
| PN9 | **Restore into an open window** passing 12 Essentials: dedupe by pinned URL first, then demote the extras to *Space-pinned* (not plain) and keep their pinned URL | override | `src/` | no |
| PN10 | **Leak detector (DCHECK)** at the end of `Normalize`: every Space-pinned tab is in its Space's leading run; no Essential has a group; the Essentials count ≤ 12; no Space-pinned flag was cleared without a drag or an explicit unpin | — | `src/` | no |
| PN11 | Naming: rename any Koan identifier or doc text that says "pinned group" or "pin group"; add the collision list from §5.2.3 to `REBASE.md` next to the "re-audit `IsPinned()`" step | — | docs | no |

Cost: about 1-1.5 agent-days, one Mac build. 0-1 new patch files (PN3's extension half is optional); 2 existing patches gain 1-4 lines.

### 5.4 Tests (Mac loop, DCHECK build so PN10 fires)

Each test asserts:
- the active window is focused on a Space;
- no Essential has a group;
- Essentials ≤ 12;
- every `pinned_in_space` tab is in its Space's leading run, with its pinned URL kept.

1. **P1** Space with 3 normal tabs. Select an Essential, then Remove from Essentials: the tab is the **first** normal tab of the Space (below Space-pinned tabs), focus is never exited (log `OnTabGroupFocusChanged(nullopt)`), and there is no colour flash (PN1). Repeat with ⌘⇧D on the active Essential, and with a split Essential.
2. **P2** Space-pinned tab active: Mac Tab > "Group Tab", ⌃⇧PgDn at the Space's last tab, and an extension `tabs.ungroup`. Each time, the tab stays Space-pinned and returns to the pinned run (PN2).
3. **P3** Extension: `tabs.update(normalTab,{pinned:true})` (result per PN3's decision); `tabs.create({pinned:true})` ×13 (cap respected, no silent demotion of a Space-pinned tab); `tabs.query({pinned:true})` lists both kinds; `tabs.update(essential,{pinned:false})` (top of the Space, not the end); `tabs.update(splitTab,{pinned:true})` (the split stays together or the call is refused).
4. **P4** chrome://settings/onStartup = "Open the New Tab page". With 3 Essentials (custom label and icon, navigated away from the pinned URL) and 2 Space-pinned tabs in 2 Spaces: quit and relaunch. Everything comes back unloaded, with pinned URLs, labels, icons and Spaces, and nothing is duplicated (PN4, G9).
5. **P5** Tracked-pref tampering: edit `pinned_tabs` in `Preferences`, relaunch. No extra Essentials appear.
6. **P6** Mac menu bar Tab > Pin Tab on: a normal tab (checkmark off, pins to the Space), a Space-pinned tab (checkmark on), an Essential, the ⌥ alternate with a keyboard-focused tab (PN5).
7. **P7** 12 Essentials; drag a Space-pinned tab onto the grid and drop. It is back where it was, still pinned (PN7). Same with a normal tab: still normal.
8. **P8** Crash-restore into a window that already has 10 Essentials, from a session with 5: ≤12, no duplicate URLs, the extras are Space-pinned (PN9).
9. **P9** chrome://settings/reset: decide and document the outcome; no crash; Spaces intact.
10. **P10** A Space-pinned chat page in Space A; switch to B; wait 15 s. chrome://discards shows it not frozen (PN8).
11. **P11** (#150) DCHECK build: 20 Space switches by swipe and ⌃⌥→; create and delete Spaces; unpin the active Essential; close the window mid-switch; ⌘W on the last tab. No CHECK in `TabCollectionAnimatingLayoutManager`; the rows never show above the Essentials for a frame (slow-motion capture).
12. **P12** Close paths on an Essential and on a Space-pinned tab: ⌘W, File > Close Tab, the close button, middle click, Close Other Tabs, Close Tabs Below, back with no history. Each follows the close behaviour; Chromium's "press again to close pinned tab" toast never shows, including with the behaviour set to Close.
13. **P13** Duplicate an Essential at 12: the duplicate is demoted, the original is untouched. Drag an Essential to another window: it becomes an Essential there, ≤12 *(live check vs Zen's synced Essentials)*.
14. **P14** One split Essential among 6: tile widths and columns compared with Zen.

### 5.5 Top leaks (ranked)

1. **Medium-high: Space-pinned state is cleared after any transient ungroup** (heal → `kMoved` → `Normalize` → `ClearPinnedState`). The user's pinned tab silently becomes normal, and its pinned URL is lost. Reached from the Mac Tab menu, ⌃⇧PgUp/PgDn and extensions. **PN2.**
2. **Medium: `pinned_tabs` startup pref.** With any non-"continue" startup, Essentials come back without their Koan state and Space-pinned tabs vanish (Spaces too: G9). Zen restores exactly the pinned set. **PN4 with G9.**
3. **Medium: an unpinned *active* Essential exits focus mode and lands at the end of the Space.** It is visible every time (wrong position, flash, #150 rows bounce). **PN1.**
4. **Medium: extension and programmatic pins mean "Essential"** where Zen means "Space-pinned". They bypass the cap until `Normalize` silently demotes them; `Tab.pinned` hides Space-pinned tabs from extensions. **PN3.**
5. **Medium (#150 / #60): two PRs inject foreign rows into upstream's group layout loop** with different "pinned section" rules. Folders land below the separator, and both depend on `TabCollectionAnimatingLayoutManager` internals. **PN6 + FD2/FD3.**
6. **Low-medium:**
   - Mac Tab menu "Pin Tab" checkmark ignores Space-pinned tabs, and the Tab menu itself is not Zen's (PN5).
   - A full-grid drop strips a Space-pinned tab (PN7).
   - Focus freezing votes on Space-pinned tabs (PN8 / G2).
7. **Low:** restore past 12 (PN9); profile reset; naming collisions (PN11).

---

## 6. Folders

Ids: changes **FD**, tests **FO**. The tab-groups/omnibox/fullscreen audit already uses **F** for fullscreen, so folder tests are "FO" to avoid the collision.

**Summary.**
- **M158 has no nested tab groups.** A group can hold only tabs and splits, a group inside a group `CHECK`-fails, and the only "nested" string upstream is an Android error message. Koan's Spaces already *are* the one level of groups. So folders cannot be tab groups, and #60's choice is the only workable one: Koan-only metadata over ordinary tabs of the Space's group, drawn by hooks in upstream's vertical layouts.
- **#60 is not mergeable as is.**
  1. It was written against 158.0.8111.0. Its two hook patches target `tab_group_view_layout.cc` and `unpinned_tab_container_view_layout.cc`, which **do not exist in 158.0.8127.2**. They were split into `*_vertical_layout.cc` (#89). The PR is CONFLICTING, 33 commits behind develop (it predates #57 Spaces, #89, #147 and #150).
  2. It **inverts Zen's model**. In Zen a folder lives in the Space's *pinned* section and every folder tab is *pinned* (`ZenFolder.mjs:207-209` `get pinned()`, `ZenFolders.mjs:431-438` re-pins). #60 unpins tabs entering a folder and drops pinned tabs out of folders.
  3. Its membership is pruned on any group change, so upstream's transient ungroups (healed by G) can drop tabs out of folders.
  4. It and #150 hook the same layout loop independently.

### 6.1 Upstream: nesting, and what a folder may be

| Upstream item | Where (pristine) | Meaning for folders |
|---|---|---|
| `TabGroupTabCollection` children = `{SPLIT}` + tabs | `impl/tab_group_tab_collection.cc:20-22` | A group can't contain a group |
| `AddCollection` `CHECK(supported_child_collections_.Has(type))` | `public/tab_collection.h:247` | Inserting a group into a group **crashes** (any build) |
| `UnpinnedTabCollection` children = `{GROUP, SPLIT}` | `impl/unpinned_tab_collection.cc:17-18` | Groups exist only at the top level of the unpinned part |
| `PinnedTabCollection` children = `{SPLIT}` | `impl/pinned_tab_collection.cc:10-11` | Groups can't be pinned, so folders can't sit with Essentials |
| `TabStripCollection::InsertTabCollectionAt(..., parent_group)` | `public/tab_strip_collection.h:81-85`; `impl/tab_strip_collection.cc:346-350` | Only for a split inside a group; a group in a group hits the CHECK |
| No nested-group feature or flag: `kNestedTabGroups`, "subgroup", "parent group" are absent from `components/tabs`, `components/tab_groups`, `components/saved_tab_groups`, `chrome/browser/ui/tabs`, `views/tabs`, `tab_groups_api`. The only hit is `android_tab_strip_model_adapter.cc:185` "nested tab groups are not supported on Android" | grep (§ method) | Nothing upstream to reuse or to collide with |
| `TabGroupVisualData` = title, colour, collapsed | `components/tab_groups/tab_group_visual_data.h` | Spaces already use title + colour (G); a folder can't borrow a group |
| `chrome.tabGroups` / `tabs.group` / saved-group sync | `tab_groups_api.cc`, `tabs_api.cc:2105`, `components/saved_tab_groups` | Folders are invisible to all of them (fine: Zen 1.24b's folders are Firefox groups, but there's no Chromium equivalent to expose) |

Options for representing a folder, given Spaces = groups:

| Option | Verdict |
|---|---|
| Folder = another tab group, Space = Koan metadata (swap the levels) | Rejected. Spaces lean on group focus mode (G); folders would then need focus to show several groups at once, and upstream group UI (headers, editor, saved groups, `tabGroups`) would leak per folder |
| Folder = a split | No: splits are 2-tab layouts with their own UI |
| **Folder = Koan metadata over ordinary tabs of the Space's group, contiguous in model order, with rows drawn by layout hooks** (#60) | **Only workable model.** It has the same shape as Space-pinned tabs (§5): Koan state plus after-the-fact ordering |

### 6.2 #60's approach, checked against upstream (head 2dd65ded)

`KoanFolderController` (one per window) holds a tree: id, token, name, collapsed, parent, `space` (group id or none) and tab handles. `Normalize()` keeps each Space's folder tabs contiguous right after the Space-pinned run. `koan::FolderRows` in the two vertical layouts adds a `KoanFolderRowView` before each folder's tabs, indents them 14 px per level, and gives collapsed ones zero height. Persistence: window data `koan.folders` (tree with tokens) + per-tab token, restored through a `WindowRestoreHook`.

| Concern | Upstream reference | #60 today | Risk / Zen |
|---|---|---|---|
| **Base** | 158.0.8127.2 has `tab_group_view_vertical_layout.cc` / `unpinned_tab_container_view_vertical_layout.cc`; the old files are gone (`git cat-file -e` fails) | Patches `tab_group_view_layout.cc.patch`, `unpinned_tab_container_view_layout.cc.patch` | **Does not apply.** Must be ported onto develop's `tab_group_view_vertical_layout.cc.patch` (which already carries the Space-header hunk, plus #150's rows hunk) |
| **Pinned semantics** | n/a | `AddTabs` unpins (`RemoveFromEssentials` / `UnpinFromSpace`); `Prune` drops `IsPinnedForUser` tabs; `OnTabChangedAt` makes a pinned tab leave its folder | **Inverts Zen.** Zen: a folder is in the pinned section, its tabs are pinned (close behaviour reset/unload, pinned URL, Clear-proof), Essentials join with `removeEssentials(tab, false)` (`ZenFolder.mjs:290-295`) |
| **Placement vs #150** | — | Folder tabs after the pinned run; #60 says they sit "below the separator" | With #150 the separator follows `IsPinnedForUser` tabs, so folders land **under** "+ New Tab", among the normal tabs. Zen: header → pinned + folders → separator |
| **Two layout injectors** | `tab_group_view_vertical_layout.cc` child loop | `FolderRows::Place/Finish` in the loop; also replaces `child->GetVisible()` with `!is_collapsed()` and moves `set_y` | #150's `PlaceSpaceTabRows` adds rows in the same loop. Two running `height`s, two notions of section, and both `static_cast` the layout manager to `TabCollectionAnimatingLayoutManager` |
| **Membership vs upstream ungroup** | `SetTabPinnedImpl`, `MoveTabRelative`, `tabs.ungroup` all make the tab loose; G's heal re-adds it with `add_to_end=true` | `Prune` drops a tab when `tab->GetGroup() != folder.space`. `Changed()` posts the prune; G's `Update()` is posted too | **Race → state loss**: if Prune runs before the heal, the tab leaves its folder for good *(live check)* |
| **Focus mode** | Unpinned container hides every non-focused-group child, loose included (`unpinned_tab_container_view.cc:151-166`) | Ungrouped folders (`space = none`, for the "All tabs" view) get rows from the unpinned-container hook | In a Spaces window, a folder whose `space` is none: its rows may draw while its tabs are hidden by focus *(live check)*. Zen has no ungrouped folders (every folder has a Space) |
| **Collapsed folder = zero-height tabs** | Drop index and hover come from child bounds; `TabView`s stay in the focus and a11y trees | Height 0 but `IsVisible()` true during animation | A drag over a collapsed folder may target a hidden tab (several children at the same y); VoiceOver reads hidden tabs *(live check)* |
| **Drag in/out** | `tab_drag_handler.cc`; rows have no node, so they are not drop targets | Rule applied in `OnTabMoved` only while `!IsTabStripEditable()` (a drag); `Normalize` waits for the drag | Zen: drop on the folder **header** joins it at the start; Essentials dropped on a header join (live-results §3). Koan can't target the row. Folder rows themselves can't be dragged |
| **Keyboard moves** | `IDC_MOVE_TAB_NEXT/PREVIOUS` | Not a drag, so no rule; `Normalize` moves the tab back | ⌃⇧PgUp/PgDn inside a folder does nothing visible; at the folder's edge it doesn't leave the folder. Zen moves within the folder |
| **Session restore** | Group ids relabelled per restore (`session_restore.cc:1181-1190`); restored tabs carry extra data | Tokens per tab (fixes the earlier index bug); folder Space mapped by "place among the window's Spaces" | Fragile once G9/G10 change how Spaces are restored, and with crash-restore into an open window (#145) *(live check)* |
| **Startup without session restore** | `pinned_tabs` pref (§5) | Folders exist only in the session | All folders are lost (Zen restores pinned + folders: `ZenSessionManager.sys.mjs:321-343`). Same root as G9 / PN4 |
| **Tab restore** | ⌘⇧T restores single tabs or groups; folders aren't groups | A closed folder tab rejoins its folder if it still exists; after Delete Folder the tabs come back loose | Better than Zen's quirk (a plain tab group, live-results); keep |
| **Spaces emptied** | A group dies with its last tab | `KeepSpace` adds a fresh tab (review B1 fixed) | OK; G's heal would also recreate the Space |
| **Extensions** | `tabs.move`, `tabs.group`, `tabGroups.*` | `tabs.move` of a folder tab is undone by `Normalize`; `tabs.group` moves tabs into a new Space and `Prune` drops them from the folder | Document. No `chrome.tabGroups` exposure (none possible) |
| **Saved tab groups / sync** | Spaces are never saved (G) | Folders are never synced | OK (Zen doesn't sync folders either; window-sync is local) |
| **Mode switch** | `RegisterOnModeChanged` | `IsEnabled()` needs `ShouldDisplayVerticalTabs()`; it re-syncs on mode change | Moot once A1 locks vertical; keep the guard |
| **Rename** | — | Double-click renames | **Zen: double-click does *not* rename** (two toggles, live-results; `ZenTabRenamer` `explicit`). Menu "Rename Folder…" only |
| **Tests** | Mac-only process (memory) | `folders_drag.mjs` uses Linux Xvfb/XTest; e2e otherwise headless Ozone | Must be redone as Mac hand checks / Mac loop |
| **Conflicts with merged work** | — | Built before #57 (Spaces bar, "Move To", Create form) | The "+" Create menu, `koan_tab_menu` dispatcher (develop already reserves 3300 for folders, `REBASE.md:135`) and session hooks must be re-based on develop's versions |

### 6.3 Changes for #60 (must-change before merge)

| # | Change | Verdict | Hook | New patch? |
|---|---|---|---|---|
| FD1 | **Rebase onto develop (8127.2).** Port `FolderRows` into the existing `tab_group_view_vertical_layout.cc.patch` and a new `unpinned_tab_container_view_vertical_layout.cc.patch` (or drop the unpinned one, FD5). Delete the stale `*_layout.cc.patch` files. Re-run `git apply --check` on pristine 8127.2 | — | existing + 0-1 | 0-1 |
| FD2 | **Zen's model: folders belong to the pinned section, and folder tabs are Space-pinned.** `AddTabs` → `PinInSpace` (an Essential via the `removeEssentials(tab,false)` equivalent, keeping its pinned URL) instead of unpinning. `Prune` no longer drops pinned tabs. "Remove from Folder" leaves the tab pinned in the pinned section (Zen: "taking it out does not unpin it"). One predicate `koan::IsInSpacePinnedSection(tab)` = `pinned_in_space` (which then covers folder tabs) is shared by #150's `PlaceSpaceTabRows`, Clear and `Normalize` | override | `src/` | no |
| FD3 | **One owner of the Space group's layout loop.** A single `koan::SpaceGroupRows` (one hook call per child + one at the end in `tab_group_view_vertical_layout.cc`) lays out, in Zen's order: Space-pinned tabs and folder rows (with indents and collapse), then the separator/Clear and New Tab rows (#150), then normal tabs. `KoanPinnedTabController::Normalize` and `KoanFolderController::Normalize` merge into one ordering pass with one "pinned run" definition. Today the two fight: folder normalisation pushes pinned tabs, and the pinned run decides the separator | override | merge #150's and #60's hunks | no |
| FD4 | **Membership survives transient ungroups.** `Prune` drops a tab only when it closed, went to another *Space* (`new_group` set and ≠ `space`) or another window, never when it is loose (nullopt). Re-check after G's `Update()` (run Prune from `KoanWorkspaceController`'s post-heal callback, not a parallel post) | override | `src/` | no |
| FD5 | **No ungrouped folders in Spaces windows.** A folder's `space` is always a Space when Spaces are on (Zen). The unpinned-container hook is needed only for windows without Spaces; drop it if that view isn't a product requirement | lock | `src/` (-1 patch) | no |
| FD6 | **Persist folders with Spaces** (G9 / PN4 store), so a startup without session restore keeps them, as Zen's pinned-only restore does. Map the folder → Space link by Koan's stable Space key (G9), not by "place among the window's Spaces" | override | `src/` | no |
| FD7 | **Zen input parity:** remove double-click rename (menu only); dropping on a folder row's centre joins at the start (a row-level drop handler in Koan's row view that calls `MoveTabsToFolder`, since upstream can't target the row); ⌃⇧PgUp/PgDn move within the folder (re-point `kActionMoveTabNext/Previous` for folder tabs, same policy table as A4/G4) | override / redirect | `src/` | no |
| FD8 | **Collapsed tabs out of the hit, a11y and focus paths:** give zero-height tabs `SetVisible(false)` after the collapse animation (as upstream's own collapsed group does), and skip them as drop targets *(live check first)* | override | `src/` / FD3's hook | no |
| FD9 | **Mac-only verification.** Replace `folders_drag.mjs`'s Xvfb/XTest path with Mac hand checks (FO list). Run the DCHECK build with the shared detector (PN10 + a folder check: every folder's tabs are contiguous, in its Space, Space-pinned, no row without tabs in the focused Space) | — | `src/` | no |

### 6.4 Tests (Mac loop; DCHECK build; "FO" because "F" is fullscreen in audit 2)

1. **FO1** Patches apply on pristine 8127.2; build; no `*_layout.cc.patch` left (FD1).
2. **FO2** Space with 2 pinned tabs, folder "Work" (subfolder "A"), 3 normal tabs: the order is header → pinned → Work → A → separator/Clear → "+ New Tab" → normal (FD2/FD3). Same after a Space switch and back, and after a restart.
3. **FO3** Move to Folder on a normal tab, then on an Essential: both become Space-pinned folder tabs (the Essential keeps its pinned URL). ⌘W on a folder tab runs the pinned close behaviour, not a close (FD2).
4. **FO4** Folder tab active: Mac Tab > "Group Tab", ⌃⇧PgDn at the Space edge, extension `tabs.ungroup`. The tab is still in its folder afterwards (FD4).
5. **FO5** Collapse a folder holding the active tab: only the active tab shows, indented. Drag a tab over the collapsed folder (no hidden-tab drop target); VoiceOver skips the hidden tabs (FD8).
6. **FO6** Drop a loose tab on a folder row's centre: it joins at the start. Drop an Essential on a row: it leaves Essentials and joins. Drag a folder tab among the loose tabs: it leaves (FD7).
7. **FO7** Double-click a folder row: it toggles twice, no rename. Menu > Rename Folder… renames (FD7).
8. **FO8** Startup "Open the New Tab page": the folders, subfolders and their Spaces come back (FD6, PN4, G9).
9. **FO9** Crash-restore into an open window with 2 Spaces, from a session with 2 Spaces that each have a folder: each folder lands in the right Space (FD6).
10. **FO10** Delete Folder on a Space's only tabs: the Space survives with a fresh tab; ⌘⇧T reopens the tabs (loose or in the folder, document which).
11. **FO11** Convert folder to Space; Change Space; Unpack; Unload All. With 20 Space switches during a rename, then close the window mid-rename: no crash (review S6).
12. **FO12** Extensions: `tabs.move(folderTab, {index: end})` (snaps back, documented); `tabs.group([folderTab])` (new Space, out of the folder); `tabGroups.query({})` lists Spaces only; `tabGroups.update(space,{collapsed:true})` (G3 expands it, folders intact).
13. **FO13** chrome://flags `tab-groups-focusing` Disabled: folders don't crash. Rows follow their groups or are hidden (document).
14. **FO14** Nesting guard: create 5 levels; "New Subfolder…" is disabled at level 5; no code path tries to add a `TabGroupTabCollection` under a group (grep plus a DCHECK in Koan's own group creation).

### 6.5 Top risks (ranked)

1. **Blocker: #60 targets files that no longer exist in 8127.2** and is 33 commits behind (before #57, #89 and #150). It cannot be merged or built as is. **FD1.**
2. **High (parity): #60 makes folder tabs unpinned, below the separator.** Zen's folders are part of the pinned section, and their tabs are pinned (close = reset/unload, Clear-proof, pinned URL). Together with #150's `IsPinnedForUser` section rule this puts folders among the normal tabs. **FD2.**
3. **High (structural): two independent injectors and two normalisers in the same upstream layout loop and the same Space tab range** (#150 rows, #60 folder rows; pinned `Normalize` vs folder `Normalize`). They will fight over order and `y`, and both depend on `TabCollectionAnimatingLayoutManager` internals. **FD3 / PN6.**
4. **Medium: folder membership is pruned on any group change**, racing G's heal. Upstream ungroup paths (Mac Tab menu, ⌃⇧PgUp/PgDn, extensions) can drop tabs out of folders for good. **FD4.**
5. **Medium: folders live only in the session file.** Any non-"continue" startup loses them, along with Spaces and Space-pinned tabs. **FD6 with G9 / PN4.**
6. **Medium-low:**
   - Zen input mismatches: double-click rename, no drop on the row, keyboard moves undone (FD7).
   - Zero-height collapsed tabs stay in the hit, a11y and focus paths (FD8).
   - Ungrouped folders in Spaces windows (FD5).
7. **Low:** folders are invisible to `chrome.tabGroups` and sync. There is no Chromium equivalent to expose them through; document it.

---

## 7. What Zen suppresses in Firefox → Chromium equivalent → Koan status

Date: 2026-11-05. Read only (nothing built or run). Zen 1.24b from `~/work/koan/.cache/zen-desktop-1.24b`; Chromium 158.0.8127.2 pristine (`git show HEAD:` / `git grep HEAD`); Koan `develop` dcf9bdaa; open PRs #60 (folders, head 2dd65ded), #150 (pinned section, head b78d5b75), #90 (Mac shortcuts and menu bar), #159 (Spaces lockdown, G1-G14). Scope is the six areas of sections 1-6 only. The rest of Zen's Firefox overrides is in `~/work/koan/.agent/notes/upstream/zen-firefox-overrides.md`. That file points here for these areas, and it already uses **Z1-Z53**, so the new ids below start at **Z54**.

**How to read it.**
- Zen line numbers are **lines in the patch file**, not lines in Firefox. They come from these abbreviations:
  - `TB` = `src/browser/components/tabbrowser/Tabbrowser-sys-mjs.patch`
  - `tab.p`, `tg.p`, `tabs.p`, `dnd.p` = `tabbrowser/content/tab-mjs.patch`, `tabgroup-mjs.patch`, `tabs-mjs.patch`, `drag-and-drop-js.patch`
  - `SS.p` = `sessionstore/SessionStore-sys-mjs.patch`
  - `by` = `prefs/firefox/browser.yaml`
- Zen JS and CSS lines (`src/zen/**`) are file lines.
- Chromium lines are pristine HEAD. Where sections 1-6 already cite a line, it is reused from there.
- **Status** is one of: `done` (on develop), `PR #N`, `partial`, `missing` (with the change id from sections 1-6, G/A, or Z), `reuse` (upstream already behaves like Zen) or `not needed`.

### 7.1 Split view

| Zen suppresses (Firefox thing) | How (Zen file:line) | Chromium 158 equivalent (file:line) | Koan status |
|---|---|---|---|
| Firefox's own split view | pref `browser.tabs.splitView.enabled` = false (`by:86`). Zen's `gZenViewSplitter` owns it (`TB:9` `splitViewBrowsers`) | Upstream split tabs: `tab_strip_model.cc:1984`, `multi_contents_view.cc`, `kSplitViewHorizontal` (`tabs/features.cc:19`) | **reuse**: Koan builds on upstream split (the opposite of Zen). Leaks are SV1-SV16 (all missing) |
| Firefox's split entry points in the tab menu | JS: `gZenViewSplitter.updateContextMenuItems()` (`tab-context-menu-js.patch:10`) | `CommandAddToSplit` / `CommandSwapWithActiveSplit` / `CommandArrangeSplit` / split mute (`tab_menu_model.cc:345-411, 555-560`) | partial: relabels only (`koan_split_view.cc:20-40`). **SV5** missing |
| New split = Firefox NTP partner | JS: empty pane is a transparent `_forZenEmptyTab` browser (`TB:298-310`) | `chrome::NewSplitTab` → `split_new_tab_page.html` (`browser_commands.cc:1797-1817`; `webui_url_constants.h:323-324`) | missing **SV1** (must-change) |
| Split behaves as one unit: never collapses (`tg.p:143-145`), new tabs never go into it (`TB:658-660, 715-716`), moves wrap the group (`TB:1063-1095, 1139-1143`), closes without animation (`TB:766`) | JS wraps of `addTab`, `moveTabTo`, `removeTab`; `MozTabbrowserTabGroup.collapsed` | `SplitTabCollection` holds tabs only (`impl/split_tab_collection.cc:19`); insert keeps split contiguity (`tab_strip_model.cc:1790-1792`); `tabs.move` keeps splits (`tabs_api.cc:200-212`) | **reuse** (SV15) |
| **Alt+click on a sidebar tab.** Firefox gates "Alt doesn't select" behind its split pref; Zen forces it on, then splits the clicked tab with the selected one, or splits it out if it is already split | `tab.p:157-163` (`event.altKey … true`), `tab.p:177-196` (`contextSplitTabs` / `removeTabFromGroup`); pref `zen.splitView.enable-tab-click-split` = true (`prefs/zen/split-view.yaml:8`) | `TabView::OnMousePressed` selects on any press without Shift/Cmd; Alt means nothing (`tab_view.cc:616-677`) | **missing → Z55** |
| **Drag a tab over another tab row, hold 500 ms → split** (a fake half-row preview) | `ZenDragAndDrop.js:89-101, 1014`; prefs `enable-drag-over-split`, `drag-over-split-delayMC` = 500, `-threshold` = 40 (`split-view.yaml:17-24`) | A vertical drag only reorders (`TabDragHandler`, `tab_drag_handler.cc:136`) | **missing → Z56** |
| Tab drag ending over the page → split; tab and link drops on a split | `dnd.p:144-163` (`moveTabToSplitView` when `dropEffect == "none"`), `dnd.p:140` (`handleTabDrop`), `dnd.p:19-21` | `MultiContentsDropTargetView`; 1000 ms delay (`ui_features.cc:263-268`); velocity gate (`:270-291`); link drop + nudge | partial: reused, unstyled. **SV11** missing |
| Firefox adopts split views on a cross-window drop | `dnd.p:111-112` (`else if (false)`), `dnd.p:120-121` | `TabDragController` attach keeps the split | **not needed** (no window sync) |
| Pane min width 7 %, no snap | `zen.splitView.min-resize-width` = 7 (`split-view.yaml:11`) | `MultiContentsResizeArea`: 15 px snap + min clamp (`multi_contents_view.cc:754-795`) | live check only (§1.2.3, no id) → **Z57** |
| Link context menu: Zen adds "Split Link" | `nsContextMenu-sys-mjs.patch:9` | `IDC_CONTENT_CONTEXT_OPENLINKSPLITVIEW`; on a split tab it **replaces the other pane** (`render_view_context_menu.cc:1010-1040, 6150-6203`) | partial: relabelled (`koan_glance_controller.cc:552-561`). **SV6** missing |
| Session: split data per window and group | `SS.p:282-285, 348`; `ZenViewSplitter.mjs:2418, 2447`; `TabGroupState-sys-mjs.patch:11` | `kCommandSetSplitTab/Data` (`session_service_commands.cc:111-112`); restore (`session_restore.cc:1093-1138`) | **done** (reuse, SV15) |
| *(No Firefox counterpart, so Zen has nothing to hide)*: mini toolbar, toolbar split button, IPH, ⌘⌥N, ⌘⌥-click | — | `multi_contents_view_mini_toolbar.cc:85-170`; `split_tabs_button.cc:57-80`; `feature_constants.cc:278-286`; `accelerators_cocoa.mm:119`; `navigation_policy.cc:59-62` | missing **SV10, SV12, SV13, SV8, SV7**. PR #90 hides Chrome's Tab menu, which carries "Add Tab to New Split View" (part of SV8) |

### 7.2 Glance

| Zen suppresses (Firefox thing) | How (Zen file:line) | Chromium 158 equivalent (file:line) | Koan status |
|---|---|---|---|
| **A pinned "app tab" opening a link to another domain.** Firefox opens a plain new tab; Zen turns that tab into a Glance over the pinned tab | `BrowserDOMWindow-sys-mjs.patch:9-21` → `onTabOpen` → `shouldOpenTabInGlance` (`ZenGlanceManager.mjs:1708-1735`, `isAppTab` `:1716`); pref `zen.glance.open-essential-external-links` = true (`prefs/zen/glance.yaml:11`) | No pinned-tab link rule: `browser_navigator.cc` and `browser_web_contents_delegate.cc` have no pinned check, so the link navigates the pinned tab in place. The hook point is a navigation throttle (`ChromeContentBrowserClient::CreateThrottlesForNavigation`, `chrome_content_browser_client.cc:5786`) | **missing → Z54**. Related to Z8 (URL-bar typing on a pinned tab) |
| Glance tab hidden from tab counts and lists, and placed right after its parent | `TB:43-97` (`tabsWithoutGlance`; pinned counts skip `zen-glance-tab`); `tabs.p:114-119` | Koan's Glance is a non-tab `WebContents`, so `TabStripModel`, extensions and the session never see it | **done** by design. The cost is GL3 (missing tab helpers) |
| Tab operations on a Glance go to its parent: pin, unpin, move target, opener | `TB:156, 182, 661-663, 1065, 1103` | Popups from the Glance page go to the source tab's delegate (§2.1, `koan_glance_controller.cc:893-948`) | **done** |
| Closing the tab closes the Glance first | `TB:775-777` → `manageTabClose` (`ZenGlanceManager.mjs:1632-1655`) | ⌘W → `IDC_CLOSE_TAB` → `chrome::CloseTab` (`browser_commands.cc:1457-1530`) closes the parent page | missing **GL2** (must-change) |
| Bookmark items: Option-click opens a Glance | `browserPlacesViews-js.patch:36, 118, 127` → `openGlanceForBookmark` (`ZenGlanceManager.mjs:1887-1901`, activation key only) | Option-click on a bookmark = `kSaveToDisk` (`window_open_disposition_utils.cc:36-37`) | missing **GL6** |
| Page context menu "Open Link in Glance" | `nsContextMenu-sys-mjs.patch:10`; `ZenGlanceManager.mjs:219-235` | Koan command 55000 | **done** |
| Firefox tab and group hover previews | `browser.tabs.hoverPreview.enabled` = false (`by:11`); `browser.tabs.groups.hoverPreview.enabled` = false (`by:62`) | `TabView::OnMouseEntered` → `UpdateHoverCard` (`tab_view.cc:707-708`) | missing **GL8 / Z10** |
| Glance parent and split partner must keep painting; the unloader never takes them | `tab-mjs`/`browser-custom-element` `zenModeActive`; `TabUnloader-sys-mjs.patch:10` | The parent is the active tab, and the inactive split pane is visible, so neither can be discarded | **reuse** |
| Glance ids in the tab state; cleared on duplicate | `TabState-sys-mjs.patch:26-27`; `SS.p:226-227` | n/a | **not needed** |

### 7.3 Library, downloads, history, bookmarks

| Zen suppresses (Firefox thing) | How (Zen file:line) | Chromium 158 equivalent (file:line) | Koan status |
|---|---|---|---|
| Downloads panel opens on every download | `browser.download.alwaysOpenPanel` = false (`by:46`); Zen's arc animation instead | `download_bubble.partial_view_enabled` default true (`download_prefs.cc:368`); auto-open (`download_display_controller.cc:151-190`); reveal lock in fullscreen (`download_toolbar_ui_controller.cc:638-646`) | missing **LB3** (= Z11). The started animation is done (`download_toolbar_ui_controller.cc.patch`) |
| Downloads button auto-hide; the button lives in the sidebar | `browser.download.autohideButton` = false (`by:29`) | Ephemeral `kActionShowDownloads` (`download_toolbar_ui_controller.cc:146-161`) | missing **LB5** |
| History and Downloads commands open Firefox's Library window | `Browser:ShowAllHistory`, `key_gotoHistory`, `Tools:Downloads` → Zen Library (`src/zen/common/zen-sets.js:21-25`) | `IDC_SHOW_HISTORY` / `IDC_SHOW_DOWNLOADS` (`browser_command_controller.cc:1316-1318, 1327-1329`); Mac menus (`main_menu_builder.mm:401-405, 486-488`); ⌥⌘L (`global_keyboard_shortcuts_mac.mm:147`) | partial: keys only (`koan_shortcuts.cc:96-108`). PR #90 hides Window > Downloads, but its new Tools > Downloads still runs `IDC_SHOW_DOWNLOADS`. **LB1 / Z17** missing |
| Firefox's revamped sidebar and its vertical tabs. The legacy sidebar is moved *inside* the tab box | `sidebar.revamp` = false, `sidebar.verticalTabs` = false (`by:32, 36`); `browser-sidebar-js.patch:9-10` (`#browser` → `#tabbrowser-tabbox`) | Vertical tabs: A1-A14. `SidePanel` is a `BrowserView` child **beside** the content (`browser_view.cc:983`), on the right by default (`side_panel_prefs.cc:40-41`) | vertical tabs: PR #155. Side: **LB7** missing. Placement inside the content card: **Z58** (live check) |
| Bookmarks toolbar | `browser.toolbars.bookmarks.visibility` = "never" (`by:20`) | `kShowBookmarkBar` / `kBookmarkBarVisibilityState` | **done** (`bookmark_bar_controller.cc.patch`, `koan.bookmarks_bar.show` = false, `koan_prefs.cc:47`). PR #90 hides View > Always Show Bookmarks Bar. ⇧⌘B still flips Chromium's pref: **LB6** missing |
| Bookmarks shown in every Space | `browserPlacesViews-js.patch:5-28, 40-109` (filter by `isBookmarkInAnotherWorkspace`); `PlacesUIUtils-sys-mjs.patch:78-129` (`zen_bookmarks_workspaces` table) | `BookmarkBarView`; Mac `BookmarkMenuBridge` (`main_menu_builder.mm:412-445`) | missing **Z42** |
| Star panel: Zen anchors it to the site-data button and adds a favicon | `browser-places-js.patch:9-31, 40` | ⌘D bubble anchored to the location-bar star (`accelerators_cocoa.mm:54`) | missing **Z12** |
| Recently closed tabs in Library History | `zen.library.history.closed-tabs` = false (`prefs/zen/library.yaml:14`) | `TabRestoreService` | **done** (`kLibraryHistoryClosedTabs` = false, `koan_prefs.cc:43`) |
| Places organizer (⇧⌘O) | Zen keeps Places | `IDC_SHOW_BOOKMARK_MANAGER` → `chrome://bookmarks` (`chrome_pages.cc:323-326`) | reuse. PR #90 adds ⇧⌘O (**LB10**) |
| *(No Firefox counterpart)*: Journeys, reading list, dangerous-download prompt | — | §3.2.4; `tab_menu_model.cc:626-632`; `koan_library_downloads.cc:926-937` | missing **LB9, LB8, LB4** |

### 7.4 Media controls and picture-in-picture

| Zen suppresses (Firefox thing) | How (Zen file:line) | Chromium 158 equivalent (file:line) | Koan status |
|---|---|---|---|
| Auto-PiP on tab switch | `media.videocontrols.picture-in-picture.enable-when-switching-tabs.enabled` = false (`prefs/firefox/pip.yaml:9`) | `AutoPictureInPictureTabHelper` + `AUTO_PICTURE_IN_PICTURE` content setting; a Space switch is a tab switch (§4.2.3) | missing **MD2** (high) |
| Auto-PiP of a **muted** video or tab | `PictureInPictureChild-sys-mjs.patch:9` (`!video.muted`); `PictureInPicture-sys-mjs.patch:9-11` (`browser.audioMuted` → break) | Auto-PiP requires `WasRecentlyAudible` (`auto_picture_in_picture_tab_helper.cc:809-814`). Whether a muted tab counts as audible needs a live check | **reuse** once MD2 lands; live check |
| PiP back-to-tab only selects the tab; Zen also switches Space | `PictureInPicture-sys-mjs.patch:20-21` (`gZenWorkspaces.switchIfNeeded`) | `PictureInPictureWindowManager::FocusInitiator` (`picture_in_picture_window_manager.cc:286`); `VideoPictureInPictureWindowControllerImpl::FocusInitiator` (`video_picture_in_picture_window_controller_impl.cc:105`) → tab activation. The Space follows through focus mode | probably **done** through G's heal; test M3 |
| PiP player: Zen adds a "minimize" button (unpin icon, left on Mac) | `player-xhtml.patch:17-28`, `player-js.patch:10-13` (`closePipWindow({reason:"CloseButton"})`), `player-css.patch:10-22` | `VideoOverlayWindowViews`: close = `CloseAndPauseIfAvailable` (pauses) (`video_overlay_window_views.cc:2203-2207`) | **missing → Z59** (low; check live whether Zen's minimize keeps playing) |
| In-video PiP toggle kept on | `media.videocontrols.picture-in-picture.video-toggle.enabled` = true (`pip.yaml:5`) | No in-page PiP toggle. Entry points are the page, video controls, GMC and Koan's card | **done** (card PiP button, #66). Zen's button rule: **MD6** |
| Media UI: Zen's sidebar player (Firefox has no global media UI) | `src/zen/media/*` | GMC toolbar button `MediaToolbarButtonView`; Cast (`media_router::kMediaRouter`) | card **done** (#66). GMC hide **MD1**, Cast **MD5** missing |
| The Firefox unloader only walked visible tabs; Zen walks all Spaces and spares `zenModeActive` | `TabUnloader-sys-mjs.patch:10, 18-19` | Focus-mode freezing votes (`tab_view.cc:470-490`); Koan's `freezing_policy.cc.patch` drops `kPlayedAudio` for `kVoting` | missing **MD4** (or **G2**) |
| Media position API, PiP-mode event, seek workaround (engine additions) | `MediaController-cpp.patch:9-35`, `MediaController-webidl.patch`, `MediaStatusManager-cpp.patch:9-11` | `MediaSession` position state; `MediaPictureInPictureChanged` | **done** (`koan_media_controller.cc:881`) |
| Mute key, Mac PiP key | Zen keysets (§4) | `IDC_WINDOW_MUTE_SITE`; no PiP command | missing **MD3, MD8**. PR #90 maps ⌃M to `IDC_WINDOW_MUTE_SITE`, which mutes the site, not the tab, so MD3 still applies |
| PiP button in the URL bar hidden unless the pref is set | `zen-omnibox.css:212-216` (`zen.urlbar.show-pip-button`) | — | **Z34** (low) |

### 7.5 Pinned tabs and Essentials

| Zen suppresses (Firefox thing) | How (Zen file:line) | Chromium 158 equivalent (file:line) | Koan status |
|---|---|---|---|
| `pinTab` ungroups the tab and moves it into one pinned container | `TB:156-174`: Glance → parent; `gZenFolders.handleTabPin` else `ungroupTab`; Essentials go to the Space's Essentials section, other pins stay in the Space's pinned container | `SetTabPinnedImpl` ungroups (`tab_strip_model.cc:5574-5584`) and exits focus mode on the active tab (`:5291-5313`) | partial: menu and ⌘⇧D redirected (`ExecutePinnedTabMenuCommand`). **PN1** (active Essential), **PN3** (extensions and other callers) missing |
| `unpinTab` drops the tab right after the pinned tabs | `TB:182-201`: removes `zen-essential`, prepends to the active Space strip | Unpinned tab lands loose at `IndexOfFirstNonPinnedTab()-1` | missing **PN1** |
| Pinned index clamps (Firefox has one pinned range) | `TB:668-701, 1052-1055, 1107-1137`: Essentials < Space-pinned < normal | `ConstrainInsertionIndex/MoveIndex` (`tab_strip_model.cc:3829-3838`) | **done** through `Normalize`. Gaps: **PN2** (sticky Space-pinned state), **PN7** (full grid) |
| Firefox drag-to-pin zone, pinned drop indicator, promo, grid move animation | `browser.tabs.dragDrop.dragToPin.enabled` = false (`by:76`); `dnd.p:76-88` (`false &&`), `dnd.p:37-40, 258`; CSS `#pinned-drop-indicator`, `#drag-to-pin-promo-card` hidden (`vertical-tabs.css:23-27`) | Drag carries only tabs whose pinned state matches (`tab_drag_handler.cc:220-233`); pinned container drop → `MoveTabTo` | **done** (healed by `Normalize`, Zen's drop rules). **PN7** missing |
| Firefox's resizable pinned/unpinned splitter | `#vertical-pinned-tabs-splitter` hidden (`vertical-tabs.css:336-338`) | Pinned container capped at about half the strip with its own scroll view (`tab_strip_view_layout.cc:25-44`; `tab_strip_view.cc:180`) | live check only (§5.2.1, no id) → **Z60** |
| **Space pinned section is collapsible** (Firefox has none). Click the Space indicator, or View > "Collapse Pinned Tabs"; the active pinned tab stays visible | `ZenSpace.mjs:12` (`nsZenCollapsiblePins extends nsZenFolder`), `:211-216`; `ZenMenubar.mjs:46-72`; Firefox `tab.visible` / `tab.group` patched (`tab.p:81-85, 112-117`; `tg.p:248-250`) | Group collapse hides the **whole** group (`tab_group_header_view.cc:352-364`), and G3 forbids collapsed Spaces | **missing → Z61**. PR #90's body lists "Collapse Pinned Tabs" as not done |
| Pinned tabs loaded eagerly on restore | `browser.sessionstore.restore_pinned_tabs_on_demand` = true (`by:8`); `TB:548-552` | `BackgroundTabLoadingPolicy` | **done** (`background_tab_loading_policy.cc.patch`) |
| "Restore only if `startup.page` = 3" | `SS.p:57-61` (forced `true`); `browser.startup.page` = 3 (`by:5`) | `SessionStartupPref::GetDefaultStartupType` | **done** (`session_startup_pref.cc.patch`). Infobar: **Z3** missing |
| Closed-window data drops pinned tabs and turns groups into saved groups | `SS.p:410-420` (`pinned && false`, `groupId && false`), `SS.p:436` | `pinned_tabs` pref and codec (`pinned_tab_codec.cc:38-53`); saved tab groups | saved groups **done** (`koan::SavesTabGroups`). `pinned_tabs` lock: **PN4** missing. Pinned-only restore: **Z20** |
| Duplicate keeps nothing pinned: a duplicate is never an Essential and goes to the active Space | `SS.p:216-229` | `DuplicateTabAt` keeps pinned (`browser_commands.cc:397-405`) | missing **Z39** |
| Tab state: Essential implies pinned; static label and icon; pinned icon | `TabState-sys-mjs.patch:17-30, 46-48`; `SS.p:365-383` | `kCommandSetPinnedState` + per-tab extra data | **done** (`koan.pinned_tab`, `session_service_base.cc.patch`, `browser_tabrestore.cc.patch`) |
| Page title and favicon override the pinned label and icon | `TB:209-221` (`zenStaticIcon`), `TB:264-281` (`zenStaticLabel`) | `TabRendererData` from `WebContents` | **done** (`tab_data.cc.patch`; Change Label/Icon in `AddPinnedTabMenuItems`) |
| Reset-to-pinned-URL button; double-click an Essential to reset | `tab.p:9, 236-242, 250-251` | none | button **done** (`koan_pinned_tabs.cc:914-922`). Double-click: **Z41** missing |
| Firefox tab menu Pin / Unpin | `tab-context-menu-js.patch:11` (`updatePinnedTabContextMenu`) | `CommandTogglePinned` (`tab_menu_model.cc:539-551`) | **done** (redirect) |
| *(No Firefox counterpart)*: Mac Tab > Pin Tab, the `pinned_tabs` pref, profile reset, extension pins | — | `main_menu_builder.mm:543-547`; `browser_native_widget_mac.mm:603-607`; `tabs_api.cc:1461-1481`; `profile_resetter.cc:410-428` | PR #90 hides the Tab menu (most of **PN5**). **PN3, PN4** missing |

### 7.6 Folders

| Zen suppresses (Firefox thing) | How (Zen file:line) | Chromium 158 equivalent (file:line) | Koan status |
|---|---|---|---|
| Firefox tab groups UI (Zen reuses `tab-group` as `zen-folder` and as split groups) | `browser.tabs.groups.enabled` = false (`by:56`); `TB:1020-1021` (`isTabGroup` includes `zen-folder`) | Chromium tab groups = Koan Spaces | **PR #159** (G1-G14). Folders are Koan rows (**PR #60**) |
| Right-click on a group label opens Firefox's group editor | `tg.p:66-70` (listener removed) | Right-click on the header opens `TabGroupEditorBubbleView` (`tab_group_header_view.cc:355-361`) | **G6** (PR #159) |
| Middle-click on a group label "saves and closes" it | `tabs.p:38-42` (removed) | Saved-group close path | **G8** (PR #159) |
| Groups can't be pinned, and adding a pinned tab to a group unpins it | `tg.p:172-174` (`get pinned(){return false}` removed), `tg.p:263-265` (`unpinTab` removed); `TB:1187-1198` (`moveTabToGroup` pins or unpins to match the folder) | `AddToExistingGroup` unpins (`tab_strip_model.cc:4895-4949`); a pinned tab is never grouped (`tab_strip_collection.cc:740-775`) | **PR #60 does the opposite** (it unpins folder tabs). **FD2** must-change |
| Pin and unpin from inside a folder | `TB:162-164, 188-191` (`gZenFolders.handleTabPin/Unpin`) | `SetTabPinnedImpl` ungroups | **FD2 / PN1** |
| Flat groups. Zen nests them (max 5 subfolders) | `tg.p:14-16, 186-256` (inner container, `level`, `childGroupsAndTabs`); `zen.folders.max-subfolders` = 5 (`prefs/zen/folders.yaml:11`) | `TabGroupTabCollection` holds tabs and `SPLIT` only (`impl/tab_group_tab_collection.cc:20-22`) | **PR #60** (Koan's own tree). **FD1, FD3** |
| Firefox collapse hides every tab and animates; Zen keeps the active tab visible and skips the animation | `tg.p:153-155`; `tab.p:72-80` | `TabGroupView` collapse | PR #60. **FD8** (hit, a11y and focus paths) |
| New tab opened from a folder tab joins the folder | `TB:658-660` (folder dropped unless `zen.folders.owned-tabs-in-folder`, default false, `folders.yaml:14`) | Opener/active group inheritance (`tab_strip_model.cc:1754, 1777`) gives the **Space**. Folder membership is Koan's | live check on #60 (the folder must *not* grow) |
| Group line and overflow count | CSS `.tab-group-line`, `.tab-group-overflow-count-container` hidden (`zen-folders.css:7-9, 331-333`) | Chromium group line and collapsed count in `TabGroupView` / header | PR #60 (its own rows). **FD3** |
| Drag: folder expand and collapse rules; drops limited by `canDropElement` | `dnd.p:29, 193-225` (`animateGroupMove`); `TB:1132-1134` | `TabDragController` group drop | **FD7** (centre drop, keys) |
| Session: folders per window; group `pinned` / `essential` | `SS.p:293-296, 347`; `ZenFolders.mjs:1255, 1326`; `TabGroupState-sys-mjs.patch:9-11` | Window and tab extra data | missing **FD6** |
| Rename hidden while the sidebar is collapsed | `zen-folders.css:335-337` | — | PR #60 (check) |

### 7.7 New items not in sections 1-6

The ids continue after Z53 (`zen-firefox-overrides.md`).

- **Z54. A link from a pinned tab or Essential to another domain opens in Glance.** **Verdict: redirect.** Add a Koan `NavigationThrottle` (registered from the existing `chrome_content_browser_client.cc` hook at `CreateThrottlesForNavigation`). It acts on a renderer-initiated navigation with a user gesture to another site from an `IsPinnedForUser` tab, excluding `about:` and non-http(s). It cancels the navigation and calls `KoanGlanceController::Open`, gated by a `koan.glance.open_essential_external_links` pref (default true). *Mac test:* Essential on github.com, click a link to apple.com: a Glance opens, and the Essential stays on github.com.
- **Z55. Alt+click on a sidebar tab = split with the active tab / split out.** **Verdict: override.** In `TabView::OnMousePressed`, Koan's existing `tab_view.cc.patch` asks `koan::HandleTabAltClick` before selection. It is gated by `koan.split.tab_click_split` (default true) and goes through SV2's same-Space rule and the 2-pane toast. *Mac test:* Option-click a tab: a split with the active tab. Option-click a split member: it leaves the split and is selected.
- **Z56. Drag a tab over another tab row and hold 500 ms → split, with a half-row preview.** **Verdict: override.** Add a Koan timer in the vertical drag path (`TabDragHandler`) that calls `AddToNewSplit` on drop. Low priority after SV11.
- **Z57. Split pane minimum 7 %, no 15 px snap.** **Verdict: override.** Change the clamp in `multi_contents_view.cc` resize handling after a live comparison (§1.2.3).
- **Z58. The bookmarks side panel sits inside the content card**, as Zen puts Firefox's sidebar in `#tabbrowser-tabbox`. **Verdict: override**, after a live check against Zen's ⌘B. Pair with LB7.
- **Z59. PiP "minimize" control.** **Verdict: override (low).** Add a button to `VideoOverlayWindowViews` that closes without pausing (`Close(false)`) if Zen's minimize keeps playing *(live check)*.
- **Z60. Essentials grid never scrolls on its own.** **Verdict: lock.** Bypass the pinned-container height cap (`tab_strip_view_layout.cc:25-44`) for Koan, since Zen has no pinned splitter and at most 12 tiles.
- **Z61. Collapsible Space pinned section.** **Verdict: reuse.** Use #60's folder collapse for the Space's pinned run: Zen's `nsZenCollapsiblePins` *is* a folder. Click the Space header (Koan's own view; G3 still forbids group collapse), plus View > "Collapse Pinned Tabs" in PR #90's menu. The active pinned tab stays shown. Persist with FD6.

Cross-references, already tracked elsewhere and confirmed here from Zen's code: Z39 (duplicate is not pinned, `SS.p:216-229`), Z41 (double-click reset, `tab.p:250-251`), Z42 (bookmarks per Space), Z10/GL8 (hover cards), Z11/LB3 (bubble), Z17/LB1 (commands), Z20/PN4 (pinned-only restore), Z34 (URL-bar PiP).
