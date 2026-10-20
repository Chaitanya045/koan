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
| Window buttons hidden until the pointer reaches the top edge (single toolbar) | Same | The page starts 8px below the window top; the strip with the frame's buttons opens on hover and pushes the page down, like Zen's. |
| Floating URL bar when editing the address | Same | Centred card, `min(2/3 window width, 750px)`. Zen floats it once you type; Koan floats it on focus (Zen's "floating" setting). Esc closes the suggestions first, then the bar, as in Chrome. |
| Multiple-toolbars layout | Same | Ctrl+Alt+B (Zen sets this in Settings). |
| Ctrl+T opens the floating URL bar instead of a new-tab page | Missing | Ctrl+T opens Chrome's new-tab page. |
| Collapsed-toolbar layout | Missing | |
| Sidebar on the right | Missing | |
| Sidebar tab list: "+ New Tab" row above the tabs, tab rows, close button on hover | Same | The row opens the tab at the top of the workspace, as Zen does with its New Tab button on top; Chromium's "+" below the tabs is hidden. Rows are 36px with a 14px radius, Zen's selected (white at 80%, 18% in dark mode) and hover colours, a 16px favicon 10px before the title. Zen's faint shadow under the selected tab isn't drawn. Ctrl+T still opens the tab at the end. |
| Separator above the tabs with **Clear** on sidebar hover | Same | Shown while the workspace has unpinned tabs. Clear keeps the selected and audible tabs like Zen's, keeps the workspace (an emptied one gets a fresh tab) and Ctrl+Shift+T restores what it closed. Zen also keeps tabs in Picture-in-Picture or sharing camera/mic; Koan doesn't check those yet. In the "∞" view Clear only closes tabs that are in no workspace (Zen has no such view). |
| "Tabs Closed!" toast | Same | Top right, Zen's look; spring animation approximated with an ease-out scale. |
| Sidebar width 163–500 px, drag to resize | Same | Upstream resize handle with Zen's limits (Chromium's 126–400 px with the Koan sidebar off). |
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
| Choose to hide only the sidebar / only the toolbar | Missing | Tracked in #20. |

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
| Close all unpinned tabs (Ctrl+Shift+K, or Clear in the sidebar) | Same | Keeps the selected tab and the workspace, like Zen; Ctrl+Shift+T restores the tabs. |

## Glance and split view

| Zen | Koan | Notes |
|---|---|---|
| Alt+click a link opens Glance | Same | |
| Close with Esc or a click outside | Same | |
| Expand to a tab (Ctrl+O) | Same | |
| Open Glance in split view | Same | |
| Glance grows from the clicked link | Different | Koan's card fades and scales in from the centre. |
| Split view side by side / stacked | Same | Ctrl+Alt+V / Ctrl+Alt+H. |
| Split grid of 3–4 tabs (Ctrl+Alt+G) | Different | Chromium's split view has two panes; Ctrl+Alt+G splits side by side. |
| Drag a tab onto the page to split | Same | Upstream Chromium. |
| Unsplit (Ctrl+Alt+U) | Same | |

## Keyboard and settings

| Zen | Koan | Notes |
|---|---|---|
| Zen's default shortcuts | Same | All of Zen's defaults (shortcuts version 21, Linux); see `USAGE.md`. |
| Firefox's shortcuts Zen inherits | Mostly | Where Chrome already uses the key for something else (e.g. Ctrl+J downloads), Koan keeps Chrome's. |
| Shortcut editor in Settings | Partial | `chrome://koan-settings` > Keyboard Shortcuts: Zen's groups and labels, Esc saves, Backspace clears, conflicts refused, Reset to Default (confirmed; Zen also restarts). It lists Koan's 26 shortcuts (Zen's own and the Firefox ones Koan implements), not Zen's full list: Chromium's other shortcuts aren't editable, since Koan only adds keys on top of Chromium's. A key needs Ctrl, Alt or Win, or is a function key (Zen accepts any key; Koan's keys also work in the address bar, where a bare letter would break typing). |
| Zen settings pages | Partial | `chrome://koan-settings` (Ctrl+,): Look and Feel, Tab Management, Keyboard Shortcuts, with Zen's labels; Chromium's own settings are linked from its sidebar. Only the settings Koan has are shown (see below): 7 today, against Zen's few dozen. Comparison: `screenshots/settings-zen-vs-koan.png` (Zen left, Koan right). |
| Settings: Only Sidebar / Sidebar and Top Toolbar | Same | Look and Feel > Browser Layout. |
| Settings: Collapsed Sidebar layout, compact mode variants, dark theme styles, window dragging, Glance, URL bar behaviour | Missing | Added to the settings page as each feature lands (#35, #41, #44, #37, #36). |
| Settings: Vertical Tabs group (new tab button on the tab list / at the top, how to expand tabs) | Missing | The group is declared; no Koan setting yet. |
| Settings: pinned tab close behaviour, restore pinned tabs to their URL, container-specific Essentials | Missing | Pinned tabs / Essentials (#38); Chromium has no containers. |
| Switch to the most recently used tab on close | Same | Tab Management, on by default like Zen. A tab's opener still comes first, as in Zen (Firefox's owner rule). Only tabs you have looked at count: one opened in the background and never shown doesn't. In a workspace it picks among the workspace's tabs and the Essentials; collapsed groups are skipped. |
| Close tab and switch to its owner on Back with no history | Same | Tab Management, on by default. Upstream Chromium's back-to-opener: only for tabs opened from a link in another tab (Zen: tabs with an owner), never pinned ones. |
| Ctrl+Tab within Essentials or workspace tabs; ignore pending tabs | Same | Tab Management, off by default. In tab order (Chromium's default), Ctrl+Tab stays in the active workspace plus the Essentials, as upstream's group focus does, and these settings narrow it further. |
| Ctrl+Tab in recently used order (Firefox's setting, which Zen keeps) | Same | Chromium's own setting: chrome://settings > Appearance > "Cycle through the most recently used tabs with Ctrl+Tab" (off by default). It goes through the profile's tabs in every window and workspace. When it is on it wins for Ctrl+Tab over Koan's two Ctrl+Tab settings, which then apply only to Ctrl+Page Up/Down; the Koan rows say so, like Zen's warning for the same combination. |

## Browser basics

| Zen | Koan | Notes |
|---|---|---|
| Engine | Different | Chromium (Blink, V8) instead of Firefox (Gecko). |
| Extensions | Different | Chrome extensions (Chrome Web Store), not Firefox add-ons. |
| Sync | Missing | Koan ships without Google API keys, so Google sync isn't available. |
| Automatic updates | Missing | Install new releases from GitHub. |
| DRM video (Widevine) | Missing | Not bundled. |
