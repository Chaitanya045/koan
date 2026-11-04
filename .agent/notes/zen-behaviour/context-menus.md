# Context menus (sidebar and tab)

Zen 1.24b. Source: `src/browser/base/content/zen-panels/popups.inc` (static menus), the `parseXULToFragment` blocks in
`ZenPinnedTabManager.mjs`, `ZenFolders.mjs`, `ZenViewSplitter.mjs`, `ZenCompactMode.mjs`, `ZenGradientGenerator.mjs`,
`ZenSpaceRoutingManager.sys.mjs`, `ZenUIManager.mjs` (`#debloatContextMenus`), locales (`zen-general`, `zen-vertical-tabs`,
`zen-workspaces`, `zen-folders`, `zen-space-routing`). Native menus: the macOS menu bar and the context menus are real `NSMenu`s
(spec 15.4: no Zen styling). **The exact order of items inside the Firefox tab menu was not read from a Firefox source here**; section 6
lists a one-call live dump that gives it. Related: `folders.md` (folder menu), `spaces.md`, `essentials-and-pinned.md`, `split-view.md`.

## 1. Tab context menu (right-click a tab row)

Firefox's `tabContextMenu`, plus Zen additions:

| Item (en-US label) | Source | Shown when |
|---|---|---|
| **Add to Essentials** `n / 12` (badge text from `tab-context-zen-add-essential-badge`) | `context_zen-add-essential` | not already an Essential and **not in a folder** (`!!contextTab.group` hides it); disabled when `canEssentialBeAdded` is false (12 reached, wrong container, ...) |
| **Remove from Essentials** | `context_zen-remove-essential` | only on an Essential |
| separator `context_zen-pinned-tab-separator` | | only on a pinned tab (single selection) |
| **Reset Pinned Tab** / **Reset Essential Tab** (access key R) | `context_zen-reset-pinned-tab` | pinned, not multi-selected |
| **Edit Pinned Page** submenu (label "Edit Essential URL" for Essentials): **Replace with Current URL**, **Edit…** | `context_zen-edit-pinned-page` | pinned, not multi-selected |
| **Change Label…** | `context_zen-edit-tab-title` | hidden for Essentials, when `zen.tabs.rename-tabs` is false, or when the sidebar is collapsed to icons |
| **Change Icon…** | `context_zen-edit-tab-icon` | (condition not read; opens the SVG icon picker, "none" offered when an icon is set) |
| **Move to Folder** > (existing folders, separator, **New Folder**) | `context_zenMoveToFolder`, placed **before** Firefox's "Move to Group" | (condition not read in source; Essentials are removed from Essentials when moved into a folder, `folders.md`) |
| **Join N Tabs** / **Add Split View…** / **Split Out Tab** | `context_zenSplitTabs`, placed before `context_moveTabToSplitView` | split view rules in `split-view.md`; hidden if > 4 tabs selected or the empty tab is included |
| **Share Split View…** | `context_zenShareSplitView` | existing split group and sharing enabled |
| **Add Route for Domain** (plural for several tabs) | `context_zen-add-domain-to-routing` | (condition not read) |

Changes to Firefox's own items:
- **Pin Tab** (`context_pinTab`) is **hidden** when pinned tabs are disabled; Zen inserts its own pinned items **before** it (`element` inserted with `.before`).
- **Unpin Tab / Unpin Selected Tabs** are hidden for Essentials.
- **Close Tab** (`context_closeTab`) is **hidden on Essentials** (an Essential is removed from Essentials first, or unloaded with Cmd+W).
- The Firefox "**New Tab Below**" item (`context_openANewTab`) is **hidden** when `zen.view.context-menu.refresh` is true (it "doesn't properly work with Zen").
- "Duplicate Tab" is Firefox's `Browser:DuplicateTab` command (the shortcut key is unbound, see `keyboard-shortcuts.md`).

## 2. Sidebar / toolbar context menu (right-click empty sidebar chrome: `toolbar-context-menu`)

- At the **top** (prepended): **Compact Mode** submenu (multi-toolbar layouts) or a single checkbox **Enable compact mode** (single-toolbar), then a separator.
  The submenu: "Enable compact mode" (checkbox, `cmd_zenCompactModeToggle`), separator, radios **Hide sidebar**, **Hide toolbar**, **Hide both**.
- **Tabs on the right** (`zen-toolbar-context-tabs-right`).
- After "Open a New Tab" (`toolbar-context-openANewTab`): **New Folder** (`zen-context-menu-new-folder-toolbar`, disabled in private windows) and the **Live Folder** submenu
  (GitHub Pull Requests / GitHub Issues / RSS; Live Folders are out of scope).
- Before "Set Profile" (`zen-toolbar-context-set-profile`): **Theme…** (`zenToolbarThemePicker`).

## 3. Space menus

- **Space more-actions menu** (`zenWorkspaceMoreActions`, the `...` on the Space header, also from the Library's Spaces cards): **Change Name…**, **Change Icon…**, **Theme…**,
  **Set Profile** submenu (containers; hidden in private windows or without containers), separator, **Manage Spaces** (opens the Library on Spaces),
  **Unload Space**, **Unload All Other Spaces**, separator, **Create Space**, **Share Space**, **Delete Space**, separator, **Space Routing Settings**.
  `cmd_zenCtxDeleteWorkspace` -> `gZenWorkspaces.contextDeleteWorkspace` (confirmation: see `spaces.md`).
- **Create New popup** (`zenCreateNewPopup`, the "+" next to the Spaces): **Create Space**, **Create Folder**, separator, **New Empty Split**, **New Tab**. The "+" icon
  rotates 45 degrees over 0.2 s while the popup is open and back when it closes.

## 4. Folder menu

Right-click a folder header: in `folders.md` section 4.5 (Rename Folder…, Change Icon…, Unload All Tabs, New Subfolder…, Change Space, Convert folder to Space, Share Folder…, Unpack Folder, Delete Folder).

## 5. Page context menu and site-data panel

- Link in a page: **Open Link in Glance** (`context-zenOpenLinkInGlance`, inserted before the "open" separator), **Split Link to New Tab** (`context-zenSplitLink`; disabled when a split is full).
  See `glance.md` and `split-view.md`.
- The identity (site data) panel's menu has **Clear site data** (`context_zenClearSiteData`), **Add Extensions** (`context_zenOpenGetAddons`) and **All Site Settings** (`context_zenOpenSiteSettings`).

## 6. LIVE CHECK (one dump, then a few checks)

1. Dump the real order: for each of `tabContextMenu`, `toolbar-context-menu`, `zenWorkspaceMoreActions`, `zenFolderActions`, `zenCreateNewPopup`, open it with
   `popup.openPopup(anchor, "after_pointer", 0, 0, true, false)` (as done in the compact-mode checks, which works for the tab menu) and list
   `[...popup.children].map(c => [c.id || c.localName, c.label, c.hidden, c.disabled])`, for three tab states (normal, pinned, Essential) and for a multi-selection.
2. Confirm the "Move to Space" entry: this file found no Zen-specific "Move to Space" item; if the live dump shows one (Firefox has "Move to Window/Group"), add it.
3. The Space "Delete Space" confirmation text and the Share items (share is out of scope).
4. Whether the tab menu in the Library Spaces cards is exactly the real menu (the section opens the real tab's menu: `library.md` 7).

## 7. Test script (Mac loop)

1. Right-click a normal tab: Move to Folder, Join/Add Split View, no pinned items, no Essentials remove, "Add to Essentials  n / 12"; Close Tab present.
2. Right-click a pinned tab: Reset Pinned Tab, Edit Pinned Page > Replace with Current URL / Edit…, Change Label…, no "Add to Essentials" if in a folder.
3. Right-click an Essential: Remove from Essentials, Reset Essential Tab, Edit Essential URL; no Close Tab, no Unpin, no Change Label.
4. Right-click empty sidebar space: Compact Mode item first, Tabs on the right, New Folder after "Open a New Tab", Theme… before Set Profile.
5. Space "..." menu in order above; Delete Space asks to confirm.
6. In private windows: New Folder and Create Space disabled.
