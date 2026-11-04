# Settings (Zen's additions to the Settings page)

Zen 1.24b. Source: `src/browser/components/preferences/` (`preferences-xhtml.patch` adds the categories, `zenLooksAndFeel.inc.xhtml`,
`zenTabsManagement.inc.xhtml`, `zenMarketplace.inc.xhtml`, `zen-settings.js`, `zen-preferences-links.xhtml`),
`locales/en-US/browser/browser/preferences/zen-preferences.ftl`, `prefs/zen/*.yaml` for defaults. Not live-checked: the Settings
page was not opened in the live sessions (see LIVE CHECK). Koan has its own Settings; this file lists what Zen's page adds and what
each control changes. Shortcut editor: `keyboard-shortcuts.md`.

## 1. Page structure

Zen adds **four categories to the left rail** of the Settings page (`moz-page-nav-button`s added right after "General"):

| Rail item | View | Titles |
|---|---|---|
| **Look and Feel** | `paneZenLooks` | sections: "Sidebar and tabs layout" (browser layout list, Vertical Tabs), "Theme Settings" (compact view, window dragging), "Glance" |
| **Tab Management** | `paneZenTabManagement` | sections "Workspaces", "Pinned Tabs" |
| **Keyboard Shortcuts** | `paneZenCKS` (custom keys editor, accessibility icon) | the shortcut editor (`keyboard-shortcuts.md` section 4) |
| **Zen Mods** | `paneZenMarketplace` | mods list, import/export, check for updates, "Visit Store" link |

Controls are Firefox `preference=` bound checkboxes and menulists: the change applies **immediately** (no Save button). Three of them ask for a **restart**
(prompt "Restart now / later"): `zen.glance.enabled`, `zen.workspaces.separate-essentials`, `zen.window-sync.sync-only-pinned-tabs` (and the Ctrl+Tab
"by recently used" warning toggles a hint line instead).

## 2. Look and Feel

### 2.1 Browser Layout ("Choose the layout that suits you best")

A row of **three picture cards** (PNG previews `single-toolbar.png`, `multiple-toolbar.png`, `collapsed.png`), click to select (the selected card gets the
`selected` class; a disabled card ignores clicks):

| Card (label) | Prefs set |
|---|---|
| **Only Sidebar** (`single`) | `zen.view.sidebar-expanded` = true, `zen.view.use-single-toolbar` = **true** (the shipped default) |
| **Sidebar and Top Toolbar** (`multiple`) | `sidebar-expanded` = true, `use-single-toolbar` = false |
| **Collapsed Sidebar** (`collapsed`) | `sidebar-expanded` = **false** (icons-only sidebar), `use-single-toolbar` = false |

The selection also follows the prefs if they change elsewhere (e.g. the sidebar toggle button), via an observer.

### 2.2 Vertical Tabs ("Manage your tabs in a vertical layout")

- Checkbox **"Show New Tab Button on Tab List"** (`zen.tabs.show-newtab-vertical`, default **true**): shows the "+ New Tab" row in the tab list.
  Indented child checkbox **"Move the new tab button to the top"** (`zen.view.show-newtab-button-top`, default true): puts that row above the tabs.
- Footer: a link "manage themes" to `about:addons`.
- (Strings exist for sidebar expand behaviours "Expand Tabs by Default / Don't Expand by Default / Expand on Hover (won't work in compact mode)"; the 1.24b page markup does not use them: unused locale entries.)

### 2.3 Theme Settings

- **Show in compact view** ("Only show the toolbars you use!"): checkbox **"Briefly make the toolbar popup when switching or opening new tabs in compact mode"** - `zen.view.compact.toolbar-flash-popup`.
  The settings page registers default true, the shipped pref is false (see `compact-mode.md`, the 800 ms flash).
- **Window dragging**: checkbox **"Allow dragging the window from web pages"** ("Move the window by dragging empty space at the top of websites, just like the titlebar.") - `zen.view.drag-window-from-content` (default true).

### 2.4 Glance

Checkbox **Enable Glance** (`zen.glance.enabled`, restart prompt) and a **Trigger method** dropdown (`zen.glance.activation-method`): *Ctrl + Click*, *Alt + Click*
(default), *Shift + Click*, *Meta (Command) + Click*. See `glance.md`.

## 3. Tab Management

### 3.1 Workspaces ("With workspaces, you can have multiple browsing sessions at once!")

| Control | Pref (default) |
|---|---|
| **Sync only pinned tabs in workspaces** (restart) | `zen.window-sync.sync-only-pinned-tabs` (false) |
| **Hide the default container indicator in the tab bar** | `zen.workspaces.hide-default-container-indicator` (true) |
| **Switch to workspace where container is set as default when opening container tabs** | `zen.workspaces.force-container-workspace` (false in code default) |
| **Close tab and switch to its owner tab (or most recently used tab) when going back with no history** | `zen.tabs.close-on-back-with-no-history` (true) |
| **Ignore Pending tabs when cycling with Ctrl+Tab** | `zen.tabs.ctrl-tab.ignore-pending-tabs` |
| **Ctrl+Tab cycles within Essential or Workspace tabs only** | `zen.tabs.ctrl-tab.ignore-essential-tabs` (false); when on, a hint line "Ctrl+Tab will cycle by recently used order, as it is enabled" appears |
| **When closing a tab, switch to the most recently used tab instead of the next tab** | `zen.tabs.select-recently-used-on-close` (true) |
| (code) Continue where I left off | `zen.workspaces.continue-where-left-off` (false) |
| (code) Open a new tab if the last unpinned tab is closed | `zen.workspaces.open-new-tab-if-last-unpinned-tab-is-closed` (false) |
| (code) Tab unloader timeout, minutes | `zen.tab-unloader.timeout-minutes` (10 in the page's preference list) |

(Not all registered preferences have a control in the 1.24b markup: the "(code)" rows are registered in `zen-settings.js` only.)

### 3.2 Pinned Tabs ("Manage additional behavior of pinned tabs")

- **"Restore pinned tabs to their originally pinned URL on startup"** - `zen.pinned-tab-manager.restore-pinned-tabs-to-pinned-url` (false).
- **"Enable container-specific essentials"** - `zen.workspaces.separate-essentials` (true; restart).
- **Close Tab Shortcut Behavior** dropdown - `zen.pinned-tab-manager.close-shortcut-behavior`: "Reset URL, unload and switch to next tab" (**default** `reset-unload-switch`),
  "Unload and switch to next tab" (`unload-switch`), "Reset URL and switch to next tab" (`reset-switch`), "Switch to next tab" (`switch`), "Reset URL" (`reset`), "Close tab" (`close`). Behaviour in `essentials-and-pinned.md`
  (live-verified for the default and `close`).

## 4. Zen Mods

Mods list (`gZenMarketplaceManager`): each installed mod row with enable/disable toggle, remove (confirm "Are you sure you want to remove this mod?"), a header with its title/author;
a toggle **"Disable all mods"** (`zen.themes.disable-all`); buttons **Check for updates** ("Mods updated successfully" / "Couldn't find any updates!"), **Import**, **Export**
(success/failure strings), link **Visit Store**; **Auto-update mods** (`zen.mods.auto-update`). Koan has no mods: out of scope.

## 5. Other prefs a Settings page is expected to expose (outside Zen's panes)

Dark theme style / compact style radio strings (`zen-dark-theme-styles-*`, `zen-compact-mode-styles-*`) exist in the locale but are not wired in the 1.24b markup.

## 6. LIVE CHECK

1. Open `about:preferences` in a fresh profile: the rail order, the four Zen categories, the exact section headings, the layout cards' look (2 x capture), whether controls apply without restart.
2. Verify the defaults printed above from `Services.prefs` (esp. `force-container-workspace`, `tab-unloader.timeout-minutes`, `compact.toolbar-flash-popup`).
3. Click each layout card and watch the sidebar change (animation? immediate?).
4. The restart prompts for Glance / Essentials separation / sync only pinned.

## 7. Test script

1. Open Settings; the rail shows Look and Feel, Tab Management, Keyboard Shortcuts and Zen Mods after General (Zen), or Koan's equivalent pages.
2. Layout: select "Collapsed Sidebar": the sidebar becomes icons only at once; "Only Sidebar": back, no top toolbar; "Sidebar and Top Toolbar": a top toolbar appears.
3. Toggle "New tab button under the last tab" and "Move to top": the "+ New Tab" row appears/disappears and moves above the tab list.
4. Glance trigger dropdown: pick Ctrl; Option-click no longer opens a Glance.
5. Pinned close behaviour dropdown: set to "Close"; Cmd+W on a pinned tab closes it.
