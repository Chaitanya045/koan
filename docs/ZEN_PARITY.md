# Koan vs Zen

How Koan compares with Zen, feature by feature. "Same" means it behaves the
way Zen does; "Different" says how and why; "Missing" means Koan doesn't
have it yet.

Koan's engine is Chromium, so everything about rendering, web platform
features, DevTools and extensions is Chrome's, not Firefox's. That is the
point of Koan.

## Layout and look

| Zen | Koan | Notes |
|---|---|---|
| Vertical tabs in a sidebar | Same | Upstream Chromium vertical tabs, on by default. |
| Page as a floating rounded card | Same | |
| Sidebar and toolbar on one surface | Same | |
| Single-toolbar layout (address in the sidebar), the default | Same | Sidebar top row: toggle, back / forward / reload (plus Chromium's tab groups / tab search); URL pill with the site's icon below. |
| Window buttons hidden until the pointer reaches the top edge (single toolbar) | Same | The page starts 8px below the window top; the strip with the frame's buttons opens on hover and pushes the page down, like Zen's. Buttons on the sidebar's side stay in its top row, as Zen places them (`ZenVerticalTabsManager._updateEvent`). Where compact mode hides the toolbar, the buttons come with the revealed toolbar. |
| Window buttons (traffic lights) on macOS | Different | Zen hides them with the toolbars and shows them at the top edge (or in the sidebar's top row, tabs on the left). Koan can't hide AppKit's buttons yet (needs NSWindow's standard window buttons, #51): they stay at the top left, over the page's corner when the sidebar is hidden or on the right; in the sidebar's top row with tabs on the left, as in Zen. |
| Floating URL bar when editing the address | Same | Centred card, `min(2/3 window width, 750px)`. Zen floats it once you type; Koan floats it on focus (Zen's "floating" setting). Esc closes the suggestions first, then the bar, as in Chrome. |
| Multiple-toolbars layout | Same | Ctrl+Alt+B (Zen sets this in Settings). |
| Ctrl+T opens the floating URL bar instead of a new-tab page | Missing | Ctrl+T opens Chrome's new-tab page. |
| Collapsed-sidebar layout (icons only, expands on hover) | Same | The sidebar toggle collapses it (Chromium's collapsed vertical tabs); the toolbar is then above the page. Like Zen, single toolbar needs the full sidebar: collapsing leaves it, expanding comes back to it. |
| Tabs on the right (`zen.tabs.vertical.right-side`) | Same | Sidebar context menu, and Settings > Look and Feel > Vertical Tabs. The whole window is mirrored, including the sidebar's top row, its resize edge and the compact-mode reveal edge. Tabs themselves keep their left-to-right layout, as in Zen. Windows without Koan's sidebar (horizontal tabs, popups, apps) aren't mirrored. The setting is logical, like the rest of the UI: in a right-to-left UI it puts the sidebar on the left. |
| Dragging the sidebar's edge closed hides it | Same | Turns compact mode on with the sidebar hidden, and shows Zen's "Sidebar hidden! Use Ctrl+S to bring the sidebar back" toast once (with the compact mode shortcut as set; ⌘S on macOS). Leaving compact mode brings the sidebar back at its width. With "Hide toolbar" chosen, the drag switches compact mode to hiding both, so that the sidebar does hide (Zen just turns compact mode on, which there hides only the toolbar). |
| Toolbar context menu on the sidebar | Same | Compact Mode submenu (just the toggle with single toolbar, as in Zen) and Tabs on the right. Zen's other entries (customize toolbar, ...) don't apply. |
| Sidebar tab list: "+ New Tab" row above the tabs, tab rows, close button on hover | Same | The row opens the tab at the top of the workspace, as Zen does with its New Tab button on top; Chromium's "+" below the tabs is hidden. Rows are 36px with a 14px radius, Zen's selected (white at 80%, 18% in dark mode) and hover colours, a 16px favicon 10px before the title. Zen's faint shadow under the selected tab isn't drawn. Ctrl+T still opens the tab at the end. |
| Separator above the tabs with **Clear** on sidebar hover | Same | Shown while the workspace has unpinned tabs. Clear keeps the selected and audible tabs like Zen's, keeps the workspace (an emptied one gets a fresh tab) and Ctrl+Shift+T reopens what it closed, the last closed tab per press. Zen also keeps tabs in Picture-in-Picture or sharing camera/mic; Koan doesn't check those yet. In the "∞" view Clear only closes tabs that are in no workspace (Zen has no such view). |
| "Tabs Closed!" toast | Same | Top right (top left with tabs on the right, as in Zen), Zen's look; spring animation approximated with an ease-out scale. |
| Sidebar width 163–500 px, drag to resize | Same | Upstream resize handle with Zen's limits (Chromium's 126–400 px with the Koan sidebar off; a saved width is clamped to whichever applies). |
| Theme follows the system light/dark mode | Same | |
| Thin overlay scrollbars, shown while scrolling | Same | Chromium's overlay scrollbars, on by default on Linux in Koan. Zen follows GTK's overlay-scrolling setting ("Always Show Scrollbars" in GNOME's accessibility settings); Koan always overlays for now (#30). For classic scrollbars, start Koan with `--disable-features=OverlayScrollbar`. Comparison: `screenshots/scrollbars-zen-vs-koan.png` (Zen left, Koan right, while scrolling). |
| Gradient theme picker | Different | Workspaces tint the browser with one colour (Chromium's Material colour system), no gradient editor. |
| Zen Mods (community CSS themes) | Missing | Koan's UI is native Views, not CSS, so mods can't apply. Chrome themes from the Chrome Web Store work. |

## Compact mode

| Zen | Koan | Notes |
|---|---|---|
| Toggle with Ctrl+S | Same | |
| Hide the sidebar, reveal at the left edge | Same | With single toolbar (the default) that's all, like Zen. |
| Hide the toolbar too (multiple toolbars), reveal at the top edge | Same | |
| Peek the sidebar (Ctrl+Alt+S) | Same | |
| Choose to hide only the sidebar / only the toolbar / both | Same | Zen's "Compact Mode" submenu in the sidebar's context menu; and in Settings > Look and Feel > Show in compact view (pref `koan.compact_mode.hide`). With single toolbar compact mode hides the sidebar only (Zen offers just the toggle there). A revealed toolbar is above the page only, beside a visible sidebar (Zen's navbar). Hiding just the toolbar is disabled with a collapsed sidebar and the window buttons on its side, as in Zen; a window in that state hides the sidebar instead. Zen also rewrites the setting to "Hide sidebar" then; Koan keeps it (one setting serves every window). |
| Briefly pop the sidebar up when switching tabs (`toolbar-flash-popup`) | Same | Off by default, 800 ms, not in split view, as in Zen. In Settings > Look and Feel, with Zen's label. |
| Themed background for the compact sidebar/toolbar | Same | Koan's compact sidebar and toolbar always use the window's (workspace-tinted) colours. Zen 1.23 no longer has the `zen.view.compact.color-toolbar` / `color-sidebar` prefs (the setting's string is left over, unused), so there is nothing to switch off. |

## Workspaces and Essentials

| Zen | Koan | Notes |
|---|---|---|
| Named workspaces, one shown at a time | Same | Built on Chromium tab groups shown one at a time. |
| Switch with Ctrl+Alt+Left/Right | Same | |
| Switcher in the sidebar | Same | Round buttons at the bottom of the sidebar; "∞" shows all tabs. |
| New tabs open in the active workspace | Same | |
| Restored after restart | Same | Koan restores the previous session by default, like Zen. |
| Per-workspace colour | Different | One tint colour per workspace (the group colour), not a gradient. |
| Workspace icons (emoji) | Same | A leading emoji in the workspace name is its icon; new workspaces get one per colour. |
| Containers per workspace | Missing | Chromium has profiles, not containers. |
| Essentials shared by all workspaces | Same | Pinned tabs, shown as Zen's tiles (46px tall, up to four across) at the top of the sidebar. |
| Essentials tile glows with the site's icon colour when selected | Different | The selected tile is plain white at 85% (20% in dark mode), Zen's look with `zen.theme.essentials-favicon-bg` off. |
| At most 12 Essentials | Missing | Pinning isn't capped yet (pinned tabs and Essentials are #38). |
| Per-workspace pinned tabs separate from Essentials | Missing | In Koan, pinned tabs are the Essentials. |
| Close all unpinned tabs (Ctrl+Shift+K, or Clear in the sidebar) | Same | Keeps the selected tab and the workspace, like Zen; Ctrl+Shift+T reopens the last closed tab, one per press. |

## Glance and split view

| Zen | Koan | Notes |
|---|---|---|
| Alt+click a link opens Glance | Same | |
| Glance on/off | Same | "Enable Glance" in Koan's settings (Glance group); the "Koan Glance" flag removes it altogether. |
| Trigger method Alt / Ctrl / Shift / Meta + click | Different | Same choices (no Ctrl on macOS, where Ctrl+click is a right-click; Alt = Option, Zen's default everywhere), but a change only reaches pages in renderer processes started afterwards; Zen applies it at once. |
| "Open Link in Glance" in the link context menu | Same | |
| Card grows from the clicked link, shrinks back on close | Similar | Zen's 350 ms, similar easing (no overshoot), card size (80% × full height) and fade timings. Zen grows a snapshot of the link's own rect along a small arc; Koan grows a small card straight out of the click (the browser doesn't know the link's rect). |
| Page behind fades to 30% and shrinks to 97% | Similar | Same fade (the window background laid over the page at 70%); no shrink, which would show the page's square container behind its rounded corners. |
| Glance on a link in the Glance page | Different | Zen 1.23 ignores it; Koan shows the link in the card (as #37 asks). |
| Close with Esc or a click outside | Same | beforeunload can keep the page; while typing, the first Esc turns the close button red and a second closes (Zen asks whenever anything in the page has focus, and labels the button "Confirm"). macOS: no grow/shrink (the page is an NSView); not verified on a Mac. |
| Expand to a tab (Ctrl+O), split Glance | Same | Zen's button column: 34 px round buttons, 12 px apart, beside the card. No expand animation. |
| Split view side by side / stacked | Same | Ctrl+Alt+V / Ctrl+Alt+H (⌘⌥ on macOS, Zen's accel). |
| Split grid of 3–4 tabs (Ctrl+Alt+G) | Missing | Chromium's split view has exactly two panes: `TabStripModel::AddToNewSplit` CHECKs one or two tabs, `SplitTabLayout` is side by side or stacked, `MultiContentsView` holds two contents. Ctrl+Alt+G splits side by side. |
| Tab context menu "Add Split View… / Join N Tabs / Split Out Tab" | Same | Zen's labels on upstream's split item; "Join N Tabs" is disabled past two tabs. Upstream's "Arrange split view" and "Move tab into split view" submenus stay. |
| "Split Link to New Tab" in the link context menu | Same | Upstream's item, so it sits before "Open link in incognito window" (Zen: after the private window item, next to "Open Link in Glance"). In a split, upstream's "Open link in left/right view" instead (Zen would add a third panel). |
| Drag a tab onto the page edge to split | Same | Upstream's split view drop targets (on by default). |
| Split resize handles | Similar | Both: a gap between the panes with a resize cursor; upstream shows a small handle on hover. No Zen hover header (rearrange / unsplit buttons). |
| Toast when the split limit is reached | Same | Zen's "Can't add more panels to the split view!" when a third panel would join (Koan's limit is two). |
| Unsplit (Ctrl+Alt+U) | Same | |

## Command palette

| Zen | Koan | Notes |
|---|---|---|
| Actions suggested while typing in the URL bar | Different | Zen's labels and fuzzy matching from three characters, right below the first suggestion, but at most three (Zen: five), so they don't crowd history and search suggestions out of Chromium's eight rows. A Chromium omnibox provider, so it works with any URL bar look. |
| Shortcut shown with each action | Same | In Zen's notation, as in the settings page ("Ctrl+Alt+⇧+C", "Ctrl+,"; ⌘ glyphs on macOS once Koan's Mac keys land in #51), as the row's dimmed second part rather than a tag. It is the key in effect, so a shortcut changed in chrome://koan-settings shows. |
| Learns the actions you use | Different | Used actions come first, and passed-over ones drop below the search suggestions (Zen's results learner). Zen also runs a used action on Enter for any text matching it; Koan only when you type its whole label, so typing the start of a site's address (`pri` for a site, after using Print) still opens the site. |
| Actions mode: Tab in an empty URL bar lists every action | Different | Koan types `>` (you can type it too; a second Tab leaves it) instead of showing an "Actions" chip, and lists as many actions as Chromium's popup has rows (about eight) above a "> – search" row; type to filter. As in Zen, Enter runs the first action once two characters are typed. |
| "Focus on" a Space | Same | Matched by the Space's name, which is shown beside "Focus on". |
| Add to / Remove from Essentials | Different | Koan's Essentials are its pinned tabs, so these pin and unpin, like Pin / Unpin Tab. |
| Open Theme Picker, Open Library, New Folder, Collapse / Expand All Folders | Missing | Not in Koan yet (#41, #43, #42); each feature adds its actions to the palette's registry. |
| Reset Pinned Tab | Missing | Koan's pinned tabs don't keep their pinned URL yet (#38). |
| Toggle Tabs on right | Missing | Chromium's vertical tabs can't be on the right (see "Sidebar on the right"). |
| New Blank Window | Missing | Koan doesn't sync windows, so it would be the same as Open New Window. |
| Open Space Routing, New Boost | Missing | Koan has no Space routing or Boosts. |
| Extension actions ("Extension" + name) | Missing | An extension's popup opens from its toolbar button, which the single-toolbar layout hides. |

Comparison (Zen left, Koan right): `screenshots/palette-zen-vs-koan-typed.png` (typing) and `screenshots/palette-zen-vs-koan-actions.png` (Tab in an empty URL bar).

## Keyboard and settings

| Zen | Koan | Notes |
|---|---|---|
| Zen's default shortcuts | Same | All of Zen's defaults (shortcuts version 21, Linux); see `USAGE.md`. |
| Firefox's shortcuts Zen inherits | Mostly | Where Chrome already uses the key for something else (e.g. Ctrl+J downloads), Koan keeps Chrome's. |
| Shortcut editor in Settings | Partial | `chrome://koan-settings` > Keyboard Shortcuts: Zen's groups and labels, Esc saves, Backspace clears, conflicts refused, Reset to Default (confirmed; Zen also restarts). It lists Koan's 26 shortcuts (Zen's own and the Firefox ones Koan implements), not Zen's full list: Chromium's other shortcuts aren't editable, since Koan only adds keys on top of Chromium's. A key needs Ctrl, Alt or Win, or is a function key (Zen accepts any key; Koan's keys also work in the address bar, where a bare letter would break typing). |
| Zen settings pages | Partial | `chrome://koan-settings` (Ctrl+,): Look and Feel, Tab Management, Keyboard Shortcuts, with Zen's labels; Chromium's own settings are linked from its sidebar. Only the settings Koan has are shown (see below): 13 today, against Zen's few dozen. Comparison: `screenshots/settings-zen-vs-koan.png` (Zen left, Koan right). |
| Settings: Only Sidebar / Sidebar and Top Toolbar | Same | Look and Feel > Browser Layout. |
| Settings: compact mode variants and flash popup, tabs on the right | Same | Look and Feel. |
| Settings: Collapsed Sidebar layout card, dark theme styles, window dragging, URL bar behaviour | Missing | Added to the settings page as each feature lands (#41, #44, #36). The collapsed layout is the sidebar's collapsed state (Chromium's, not a pref), so the layout picker doesn't offer it yet; the sidebar toggle does. Glance's are there (#37). |
| Settings: Vertical Tabs group (new tab button on the tab list / at the top, how to expand tabs) | Missing | The group is declared; no Koan setting yet. |
| Settings: pinned tab close behaviour, restore pinned tabs to their URL, container-specific Essentials | Missing | Pinned tabs / Essentials (#38); Chromium has no containers. |
| Switch to the most recently used tab on close | Same | Tab Management, on by default like Zen. A tab's opener still comes first, as in Zen (Firefox's owner rule). Only tabs you have looked at count: one opened in the background and never shown doesn't. In a workspace it picks among the workspace's tabs and the Essentials; collapsed groups are skipped. |
| Close tab and switch to its owner on Back with no history | Same | Tab Management, on by default. Upstream Chromium's back-to-opener: only for tabs opened from a link in another tab (Zen: tabs with an owner), never pinned ones. |
| Ctrl+Tab within Essentials or workspace tabs; ignore pending tabs | Same | Tab Management, off by default. In tab order (Chromium's default), Ctrl+Tab stays in the active workspace plus the Essentials, as upstream's group focus does, and these settings narrow it further. |
| Ctrl+Tab in recently used order (Firefox's setting, which Zen keeps) | Same | Chromium's own setting: chrome://settings > Appearance > "Cycle through the most recently used tabs with Ctrl+Tab" (off by default). It goes through the profile's tabs in every window and workspace. When it is on it wins for Ctrl+Tab over Koan's two Ctrl+Tab settings, which then apply only to Ctrl+Page Up/Down; the Koan rows say so, like Zen's warning for the same combination. |

## Performance and power

Details, measurements and the reasoning for each default: `PERFORMANCE.md`.

| Zen | Koan | Notes |
|---|---|---|
| Restored tabs load when first opened | Same | Chromium loads 4 to 20 background tabs at startup; Koan loads only the visible one (`chrome://flags/#koan-restore-tabs-on-demand`). |
| Tab unloading under memory pressure only (Firefox's low-memory unloader) | Different | Chromium's Memory Saver, on by default: tabs hidden for 4 h are discarded until opened (Settings > Performance). |
| Unload Space / Unload all other Spaces (workspace menu) | Missing | Koan has no Space menu yet (#34); Memory Saver discards a Space's tabs once they've been hidden for 4 h. |
| Background tabs throttled (Firefox timer budgets) | Different | Chromium's timer throttling, plus freezing of all but the 5 most recently used background tabs; pinned tabs (Essentials) and tabs that played audio are never frozen. Switch in Koan's settings (Tab Management > Performance). |
| No energy saver | Different | Chromium's energy saver, as in Chrome: on a MacBook it turns on below 20 % battery (Settings > Performance). |

## Browser basics

| Zen | Koan | Notes |
|---|---|---|
| Engine | Different | Chromium (Blink, V8) instead of Firefox (Gecko). |
| Extensions | Different | Chrome extensions (Chrome Web Store), not Firefox add-ons. |
| Sync | Missing | Koan ships without Google API keys, so Google sync isn't available. |
| Automatic updates | Missing | Install new releases from GitHub. |
| DRM video (Widevine) | Missing | Not bundled. |
