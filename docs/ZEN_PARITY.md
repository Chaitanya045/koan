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
| Single-toolbar layout (address in the sidebar), the default | Same | Sidebar top row: toggle, back / forward / reload (plus Chromium's tab groups / tab search); URL pill below: the site's name, the copy-URL button on hover, the site panel button. |
| Window buttons hidden until the pointer reaches the top edge (single toolbar) | Same | The page starts 8px below the window top; the strip with the frame's buttons opens on hover and pushes the page down, like Zen's. Buttons on the sidebar's side stay in its top row, as Zen places them (`ZenVerticalTabsManager._updateEvent`). Where compact mode hides the toolbar, the buttons come with the revealed toolbar. |
| Window buttons (traffic lights) on macOS | Different | Zen hides them with the toolbars and shows them at the top edge (or in the sidebar's top row, tabs on the left). Koan can't hide AppKit's buttons yet (needs NSWindow's standard window buttons, #51): they stay at the top left, over the page's corner when the sidebar is hidden or on the right; in the sidebar's top row with tabs on the left, as in Zen. |
| Multiple-toolbars layout | Same | Ctrl+Alt+B (Zen sets this in Settings). |
| Collapsed-sidebar layout (icons only, expands on hover) | Same | The sidebar toggle collapses it (Chromium's collapsed vertical tabs); the toolbar is then above the page. Like Zen, single toolbar needs the full sidebar: collapsing leaves it, expanding comes back to it. |
| Tabs on the right (`zen.tabs.vertical.right-side`) | Same | Sidebar context menu, and Settings > Look and Feel > Vertical Tabs. The whole window is mirrored, including the sidebar's top row, its resize edge and the compact-mode reveal edge. Tabs themselves keep their left-to-right layout, as in Zen. Windows without Koan's sidebar (horizontal tabs, popups, apps) aren't mirrored. The setting is logical, like the rest of the UI: in a right-to-left UI it puts the sidebar on the left. |
| Dragging the sidebar's edge closed hides it | Same | Turns compact mode on with the sidebar hidden, and shows Zen's "Sidebar hidden! Use Ctrl+S to bring the sidebar back" toast once (with the compact mode shortcut as set; ⌘S on macOS). Leaving compact mode brings the sidebar back at its width. With "Hide toolbar" chosen, the drag switches compact mode to hiding both, so that the sidebar does hide (Zen just turns compact mode on, which there hides only the toolbar). |
| Toolbar context menu on the sidebar | Same | Compact Mode submenu (just the toggle with single toolbar, as in Zen) and Tabs on the right. Zen's other entries (customize toolbar, ...) don't apply. |
| Sidebar tab list: "+ New Tab" row above the tabs, tab rows, close button on hover | Same | The row opens the tab at the top of the workspace, as Zen does with its New Tab button on top; Chromium's "+" below the tabs is hidden. Rows are 36px with a 14px radius, Zen's selected (white at 80%, 18% in dark mode) and hover colours, a 16px favicon 10px before the title. Zen's faint shadow under the selected tab isn't drawn. Ctrl+T opens the URL bar instead (see URL bar); with replace-newtab off, it opens the tab at the end. |
| Separator above the tabs with **Clear** on sidebar hover | Same | Shown while the workspace has unpinned tabs. Clear keeps the selected and audible tabs like Zen's, keeps the workspace (an emptied one gets a fresh tab) and Ctrl+Shift+T reopens what it closed, the last closed tab per press. Zen also keeps tabs in Picture-in-Picture or sharing camera/mic; Koan doesn't check those yet. In the "∞" view Clear only closes tabs that are in no workspace (Zen has no such view). |
| "Tabs Closed!" toast | Same | Top right (top left with tabs on the right, as in Zen), Zen's look; spring animation approximated with an ease-out scale. |
| Sidebar width 163–500 px, drag to resize | Same | Upstream resize handle with Zen's limits (Chromium's 126–400 px with the Koan sidebar off; a saved width is clamped to whichever applies). |
| Theme follows the system light/dark mode | Same | |
| Thin overlay scrollbars, shown while scrolling | Same | Chromium's overlay scrollbars, on by default on Linux in Koan. Zen follows GTK's overlay-scrolling setting ("Always Show Scrollbars" in GNOME's accessibility settings); Koan always overlays for now (#30). For classic scrollbars, start Koan with `--disable-features=OverlayScrollbar`. Comparison: `screenshots/scrollbars-zen-vs-koan.png` (Zen left, Koan right, while scrolling). |
| Gradient theme picker | Different | Workspaces tint the browser with one colour (Chromium's Material colour system), no gradient editor. |
| Zen Mods (community CSS themes) | Missing | Koan's UI is native Views, not CSS, so mods can't apply. Chrome themes from the Chrome Web Store work. |

## URL bar

| Zen | Koan | Notes |
|---|---|---|
| URL bar behaviour: normal / floating only when typing (default) / always floating (`zen.urlbar.behavior`) | Same | `koan.urlbar.behavior`, same values and labels in Koan's settings (Look and Feel > URL Bar). "Floating on type": Ctrl+L and a click on the pill edit in the pill, the first keystroke floats the bar. Zen floats a *mouse*-opened bar never (it stays in place, widened); Koan floats it on typing too, as #36 asks. |
| Floating URL bar look | Same | Centred card, `min(2/3 window width, 750px)`, centred vertically as if 333px tall, radius 15, text 1.15em, Zen's long shadow (`0 30px 140px -15px`, 80% / 60% black), fades in. Zen doesn't dim the page beyond that shadow. The suggestions are Chrome's. |
| Click outside closes the floating bar | Different | The click only closes the bar; in Zen it also reaches what you clicked. |
| Switching to another window keeps the bar | Same | What was typed, and a new-tab session, survive Alt-Tab / ⌘-Tab. |
| Compact mode: the sidebar stays out while its URL bar is edited | Different | Koan floats the bar right away when it's opened from the peeked sidebar's pill: Chromium collapses an expanded-on-hover sidebar while the omnibox's suggestions are open. |
| Pill and card details | Different | The site panel button is always shown (Zen shows it on hover), the pill has no hover background, and the floating card has no 0.5px outline or medium-weight text. |
| Ctrl+T / New Tab opens the URL bar instead of a tab (`zen.urlbar.replace-newtab`) | Same | `koan.urlbar.replace_newtab`. Esc, a click outside or Ctrl+T again create nothing; an unfinished search comes back within 45 s (`zen.urlbar.wait-to-clear`; Zen also drops it on a tab switch, Koan doesn't). The sidebar's "+ New Tab" row, the tab strip's "+", the menus' New Tab and ⌘T on macOS (Chromium's New Tab command) do the same. With horizontal tabs (Chromium's own setting; Zen has none), the tab strip's "+" still opens a new-tab page. What you open goes next to the current tab (Chrome's placement for a new foreground tab); with replace-newtab off, the New Tab row opens its tab at the top of the space, like Zen. Zen also unhighlights the current tab meanwhile; Koan doesn't. |
| Copy-URL button in the pill on hover (`zen.urlbar.single-toolbar-show-copy-url`) | Same | `koan.urlbar.show_copy_url`. |
| "Copied current URL!" toast | Partial | The same toast as "Tabs Closed!" (one `KoanToast` per window); the copy button, Ctrl+Shift+C and Ctrl+Shift+Alt+C (⌘⇧C, ⌘⇧⌥C on macOS; "Copied current URL as Markdown!") show it. Zen's toast on macOS also has a Share button; Koan's doesn't. Zen copies the URL without tracking parameters (its stripped URI); Koan copies it as is. |
| Site panel (security, permissions, extensions; share, reader mode, screenshot, bookmark) | Different | The pill's site button opens Chromium's page info bubble (security, permissions, cookies, site settings). Extensions and the action row aren't in it yet. |
| URL bar commands (command palette) | Partial | See Command palette below (#40). |

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
| Pinned URL, "Back to pinned url" reset button | Same | Hover the slash after the icon; Ctrl+click separates the page into a new tab. Essentials show no label, as in Zen. |
| Reset Pinned Tab, Replace with Current URL, Edit Pinned URL… | Same | Always offered for a pinned tab, as in Zen; a reset ignores no part of the URL (a hash-routed app's `#inbox` is reset too), while the reset button only shows once the page itself changed. Replace and Edit… are in an "Edit Pinned URL" ("Edit Essential URL") submenu, as in Zen. Zen's "Reset Pinned Tab to Pinned URL" shortcut is in the shortcut editor, without a key, as in Zen. |
| Closing a pinned tab: reset, unload and switch | Same | Ctrl+W, Ctrl+F4 (Cmd+W and the menu bar on macOS), the close button and middle click; Zen's six behaviours in Settings > Tab Management > Pinned Tabs. |
| Restore pinned tabs to their pinned URL on startup | Same | Settings > Tab Management > Pinned Tabs, off by default like Zen.  Pinned URLs, labels and icons come back with the session (Koan continues where you left off by default); with Settings > On startup set to open a new tab page, Chromium reopens Essentials from its own list and their current URL becomes the pinned URL. |
| Change Label… / Change Icon… | Same | Inline rename; the icon picker is a grid of emoji plus a field for any other, smaller than Zen's searchable picker. |
| Unload a tab | Same | Chromium's tab discarding. Resetting a tab that isn't loaded keeps it unloaded until it is shown, like Zen. Reset, unload and switch on a loaded tab away from its pinned URL loads the pinned page once before unloading it (Zen rewrites the unloaded tab's state instead; Chromium can't change an unloaded tab's page without loading it). |
| Toasts: pinned URL replaced / updated / invalid, tab renamed | Same | Zen's texts, in Koan's toast. |
| Unload all tabs of a folder | Missing | Koan has no folders yet (#42); folders can use the same unload. |
| At most 12 Essentials | Different | "Add to Essentials" shows how many are used ("3 / 12") and is off when the tab (a split view counts two) doesn't fit. Zen refuses a 13th dropped on the tiles; Koan accepts the drop, then moves the tab back to the workspace as a normal tab once the drag is over. The same for one duplicated from an Essential. |
| Dragging pinned tabs | Same | Dragged out of the workspace's pinned tabs, a pinned tab is unpinned; a tab dropped among them is pinned; an Essential dropped among them keeps its pinned URL as a pinned tab. Applied once the tabs stop moving, since Chromium moves them along the drag. Moved to another workspace, a pinned tab stays pinned, at the top. |
| Pinned tabs in a split view | Same | A split view is one unit, as in Zen: pinning, unpinning, Add to / Remove from Essentials and dragging take both tabs along and keep them split. Splitting a pinned tab with a normal one pins both. |
| Close Other Tabs, Close Tabs Below, Back with no history | Same | Leave pinned tabs and Essentials alone. |
| Pinned tabs that belong to their workspace | Different | Ctrl+Shift+D or "Pin": the tab stays at the top of its workspace, survives Clear and has a pinned URL. Zen lists them above the workspace's separator; in Koan they are the first tabs below it, because Chromium's tab strip puts its separator between its pinned tiles (the Essentials) and the workspace's tab group, which holds both kinds. |
| Close all unpinned tabs (Ctrl+Shift+K, or Clear in the sidebar) | Same | Keeps the selected tab and the workspace, like Zen; Ctrl+Shift+T reopens the last closed tab, one per press. Pinned tabs and Essentials stay. |

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
| Add to / Remove from Essentials | Same | As in the tab's context menu; Pin / Unpin Tab pin the tab to its workspace. |
| Open Theme Picker, Open Library, New Folder, Collapse / Expand All Folders | Missing | Not in Koan yet (#41, #43, #42); each feature adds its actions to the palette's registry. |
| Reset Pinned Tab | Same | Offered while the tab is away from its pinned URL, as in Zen. |
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
| Settings: URL bar behaviour, replace new tab, copy URL button | Same | Look and Feel > URL Bar. |
| Settings: compact mode variants and flash popup, tabs on the right | Same | Look and Feel. |
| Settings: Collapsed Sidebar layout card, dark theme styles, window dragging | Missing | Added to the settings page as each feature lands (#41, #44). The collapsed layout is the sidebar's collapsed state (Chromium's, not a pref), so the layout picker doesn't offer it yet; the sidebar toggle does. Glance's are there (#37). |
| Settings: Vertical Tabs group (new tab button on the tab list / at the top, how to expand tabs) | Missing | The group is declared; no Koan setting yet. |
| Settings: pinned tab close behaviour, restore pinned tabs to their URL | Same | Tab Management > Pinned Tabs. |
| Settings: container-specific Essentials | Missing | Chromium has no containers. |
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

## Smaller pieces

| Zen | Koan | Notes |
|---|---|---|
| Media controller at the bottom of the sidebar | Same | A card per background tab playing media (through a media session): site icon, previous / play-pause / next, mute; on hover title, artist, seekable progress, picture-in-picture and close. Shows after a second of sound, hides while its tab is selected or in picture-in-picture, back half a second after switching away; up to three cards stack. The same media session drives macOS Now Playing and the media keys (Chromium's, on by default on macOS). Zen's microphone / camera sharing card and its scrolling of long titles are missing. |
| Download animation | Same | The icon flies along Zen's arc (same timing, scale and fade) from the last click to the downloads button, or, when the toolbar is hidden, to a downloads tile at the bottom of the sidebar's side (right with tabs on the right, as in Zen). Koan's tile shows a download arrow instead of Zen's library icon. Chrome's own "download started" arrow is skipped while Zen's plays. |
| Drag the window from the top of the page (`zen.view.drag-window-from-content`) | Same | Top 5% of the page, empty space only (Zen's checks: no text, link, form control, media, frame, focusable or ARIA widget, interactive cursor); 4px threshold, 50px when maximized. On macOS the window follows the pointer until the button is released (Chromium's window move loop, as for a tab dragged out); on Linux the window manager moves it. Settings > Look and Feel > Window dragging. The page gets the button released when the drag starts, so a click on the empty space there may still fire. Only the active tab's page drags (in split view, the active panel). |
| Welcome screen on first run | Different | Same flow and words (title, import, colours, search engine, Essentials, default browser, done) in native views. Koan paints a gradient instead of Zen's video, shows the Essentials as initials instead of the sites' logos, offers nine colours instead of Zen's gradient picker, and doesn't resize the window. Zen's ad-blocker page is left out: Chrome extensions install from the Chrome Web Store with the user's confirmation, so Koan can't install one for you. |
| Toasts | Same | One toast per window (`KoanToast`) with Zen's look: "Copied current URL!", "Tabs Closed! Use [Ctrl+Shift+T] to undo.", the split view limit, "Sidebar hidden!", and "New background tab opened!" (compact mode with the sidebar hidden, a page opening a tab in the background; its tab button switches to the tab) and "Workspace has been successfully renamed!". Showing a toast replaces the one before (Zen stacks them in place). The pinned-tab toasts belong with #38's pinned tab URL editing. |

## Browser basics

| Zen | Koan | Notes |
|---|---|---|
| Engine | Different | Chromium (Blink, V8) instead of Firefox (Gecko). |
| Extensions | Different | Chrome extensions (Chrome Web Store), not Firefox add-ons. |
| Sync | Missing | Koan ships without Google API keys, so Google sync isn't available. |
| Automatic updates | Missing | Install new releases from GitHub. |
| DRM video (Widevine) | Missing | Not bundled. |
