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
| Floating URL bar when editing the address | Same | Centred card, `min(2/3 window width, 750px)`. Zen floats it once you type; Koan floats it on focus (Zen's "floating" setting). Esc closes the suggestions first, then the bar, as in Chrome. |
| Multiple-toolbars layout | Same | Ctrl+Alt+B (Zen sets this in Settings). |
| Ctrl+T opens the floating URL bar instead of a new-tab page | Missing | Ctrl+T opens Chrome's new-tab page. |
| Collapsed-toolbar layout | Missing | |
| Sidebar on the right | Missing | |
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
| Essentials shared by all workspaces | Same | Pinned tabs, shown as a grid at the top of the sidebar. |
| Per-workspace pinned tabs separate from Essentials | Missing | In Koan, pinned tabs are the Essentials. |
| Close all unpinned tabs (Ctrl+Shift+K) | Same | |

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
| Shortcut editor in Settings | Missing | Shortcuts are fixed. |
| Zen settings pages | Different | Koan features are switched in `chrome://flags` (search "Koan") and with shortcuts. |

## Browser basics

| Zen | Koan | Notes |
|---|---|---|
| Engine | Different | Chromium (Blink, V8) instead of Firefox (Gecko). |
| Extensions | Different | Chrome extensions (Chrome Web Store), not Firefox add-ons. |
| Sync | Missing | Koan ships without Google API keys, so Google sync isn't available. |
| Automatic updates | Missing | Install new releases from GitHub. |
| DRM video (Widevine) | Missing | Not bundled. |
