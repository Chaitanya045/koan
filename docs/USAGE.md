# Using Koan

Koan is Chromium with Zen's way of working: tabs in a sidebar, the page as a
floating card, workspaces, Glance and compact mode. Everything web-facing is
plain Chromium (rendering, DevTools, extensions, `chrome://` pages).

## Shortcuts

Koan uses Zen's default shortcuts (Zen shortcuts version 21, Linux). Where
Zen deliberately rebinds a key that Chrome uses, Koan follows Zen; the
"Chrome had" column says what moved. Everything not listed behaves as in
Chrome.

| Action | Keys | Chrome had |
|---|---|---|
| Toggle compact mode | Ctrl+S | Save page (now Ctrl+Alt+Shift+S) |
| Show the sidebar in compact mode | Ctrl+Alt+S | |
| Next / previous workspace | Ctrl+Alt+Right / Ctrl+Alt+Left | |
| Split view: side by side | Ctrl+Alt+V | |
| Split view: stacked | Ctrl+Alt+H | |
| Split view: grid (two panes in Chromium) | Ctrl+Alt+G | |
| Unsplit | Ctrl+Alt+U | |
| New split with an empty tab | Ctrl+Shift+* | |
| Copy the page's URL | Ctrl+Shift+C | Inspect element (now Ctrl+Shift+L) |
| Copy the page's URL as Markdown | Ctrl+Shift+Alt+C | |
| Pin / unpin tab | Ctrl+Shift+D | Bookmark all tabs |
| Close all unpinned tabs (in the workspace) | Ctrl+Shift+K | |
| Expand Glance to a tab | Ctrl+O while Glance is open | Open file (still, otherwise) |
| Glance a link | Alt+click | (Alt+click downloaded the link) |
| Close Glance | Esc, or click the dimmed page | |
| Save page | Ctrl+Alt+Shift+S | |
| Library (history) | Alt+Shift+L | |
| Inspect element | Ctrl+Shift+L | |
| New window | Ctrl+Shift+N | New incognito window |
| New private window | Ctrl+Shift+P | Print using system dialog |
| Mute site | Ctrl+M | |
| Bookmarks | Ctrl+B | |
| Downloads | Ctrl+Shift+Y | |
| Reading mode | Ctrl+Alt+R | |
| **Koan only:** toggle single toolbar | Ctrl+Alt+B | |
| **Koan only:** new workspace | Ctrl+Alt+N | |

When a page uses a key itself (for example Ctrl+S in an online editor), the
page gets it first, as in every browser.

## Sidebar

Vertical tabs are on by default. Pinned tabs sit in a grid at the top and
are shared by every workspace (Koan's Essentials). The workspace switcher
is at the bottom of the sidebar: **∞** shows all tabs, one round button per
workspace, **+** creates one.

## Workspaces

A workspace is a tab group shown on its own. New tabs open in the active
workspace. Rename or recolour a workspace from its group header's menu.
Workspaces and the active one are restored on restart (Koan restores the
previous session by default; change it in Settings > On startup).

## Compact mode

Ctrl+S hides the sidebar and toolbar; the page fills the window with a
thin margin. Move the pointer to the left edge to slide the sidebar out, or
to the top edge (or press Ctrl+L) to slide the toolbar down. The setting
applies to all windows and persists.

## Single toolbar

Ctrl+Alt+B switches to Zen's single-toolbar layout: no toolbar across the
top; back / forward / reload and a pill with the current site sit at the top
of the sidebar, and the page reaches the top of the window. Click the pill
or press Ctrl+L to edit the address: the toolbar slides down over the page
as a floating URL bar and slides away when you're done. Compact mode wins
when both are on.

## Glance

Alt+click a link (or Alt+Enter on a focused link) to preview it in a card
over the page, as in Zen. Shift+click opens a new window, as in Zen and
Firefox. Same-site links with a `download` attribute still download;
cross-site ones open in Glance (Chromium ignores `download` across sites,
so there Alt+click was the only way to download). Beside the card:
close, **expand to a tab**, or **open in split view** with the current tab.

## Turning things off

Every Koan feature has a switch in `chrome://flags` (search "Koan"):
Koan sidebar, compact mode, workspaces, Glance.

## Install in the app menu

```sh
scripts/install_desktop.sh               # adds Koan + icon for this user
scripts/install_desktop.sh --uninstall
```

Koan keeps its profile in `~/.config/koan`, separate from Chromium's.
