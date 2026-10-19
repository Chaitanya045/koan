# Using Koan

Koan is Chromium with Zen's way of working: tabs in a sidebar, the page as a
floating card, workspaces, Glance and compact mode. Everything web-facing is
plain Chromium (rendering, DevTools, extensions, `chrome://` pages).

## Shortcuts

| Action | Keys |
|---|---|
| Toggle compact mode | Ctrl+Alt+C |
| Toggle single toolbar (URL bar in the sidebar) | Ctrl+Alt+B |
| Next / previous workspace | Ctrl+Alt+Right / Ctrl+Alt+Left |
| New workspace | Ctrl+Alt+N |
| Glance a link | Shift+click the link |
| Close Glance | Esc (or click the dimmed page) |
| Split view with a link | Ctrl+Alt+click the link (upstream) |
| Everything else | Chrome's shortcuts |

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

Ctrl+Alt+C hides the sidebar and toolbar; the page fills the window with a
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

Shift+click a link to preview it in a card over the page. Beside the card:
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
