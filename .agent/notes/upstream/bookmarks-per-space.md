# Bookmarks per Space (#480): design and upstream audit

Zen 1.24b ties a bookmark to Spaces. Its bookmarks toolbar and menus show the
active Space's bookmarks and the ones that belong to no Space. This note is
the design, the map of every Chromium 158 surface that lists bookmarks, and
the decisions taken. Code: `koan_bookmark_spaces_rule.h` (the rule, unit
tested), `koan_bookmark_spaces.h` (the window side), and
`koan_bookmark_spaces_hooks.h` (what upstream calls). Window sync made it
possible: a Space has an id that every window shares
(`window_sync::SpaceRecord::id`, `window-sync.md`).

## What Zen does (1.24b source)

- **Storage.** `src/zen/spaces/ZenSpaceBookmarksStorage.js` keeps a table in
  places.sqlite, `zen_bookmarks_workspaces (bookmark_guid, workspace_uuid)`,
  one row per bookmark and Space. A bookmark (or a folder) can be in several
  Spaces. No row means no Space. A second table logs changes for Zen's sync.
  The workspace uuid has no foreign key: Spaces live in the session.
- **Rule.** `gZenWorkspaces.isBookmarkInAnotherWorkspace(node)`
  (`ZenSpaceManager.mjs`) hides a node if it is in another Space and not in
  the active one. Ids of Spaces that are gone still count there: in Zen a
  bookmark whose only Space was deleted stays hidden everywhere (nothing
  removes the rows).
- **Where it applies.** `browserPlacesViews-js.patch` filters
  `PlacesViewBase._rebuildPopup` (every bookmarks menu and popup: the
  Bookmarks menu, the toolbar's folder menus and the chevron) and
  `PlacesToolbar._rebuild` (the toolbar). The filter applies to URL and
  folder nodes; separators always show. Each level is filtered by its own
  node, so the contents of a hidden folder can't be reached.
  `ZenSpaceManager._invalidateBookmarkContainers()` rebuilds
  `PlacesToolbar`, `bookmarks-menu-button` and the `BMB_*` menus when the
  Space changes, or on `workspace-bookmarks-updated`.
- **Not filtered.** The Library (`PlacesTreeView`), the bookmarks sidebar
  (also a tree view), and the URL bar. Zen's urlbar patches have a Spaces
  search mode (`RESTRICT_WORKSPACE`), but nothing filters bookmark results by
  Space. Private windows and disabled workspaces show everything
  (`privateWindowOrDisabled`).
- **Editor.** `editBookmark-js.patch` and `editBookmarkPanel-inc-xhtml.patch`
  add a "Spaces" row (`zen-bookmark-edit-panel-workspace-selector`) to the
  star panel and the bookmark properties dialog: a dropdown of checkboxes,
  one per Space, summarised as "Work, Home" or "-". It is shown for
  bookmarks and folders. The Library's own editor leaves it out
  (`windowtype == "Places:Organizer"` returns early). New bookmarks start in
  no Space. `PlacesUIUtils-sys-mjs.patch` saves the choice with the
  bookmark's other edits.
- **No setting.** Zen has no pref to turn it off.

What Zen turns off in Firefox for this: nothing. The patches add a filter
and a field and hide no Firefox UI (`zen-firefox-overrides.md` has no
bookmarks entry).

Brave has no per-container bookmarks (brave/brave-browser#55380 is open), so
there is nothing to compare with there.

## Koan's design

- **Storage: the bookmark's meta info.** `BookmarkNode` meta info key
  `koan_spaces` holds the Spaces' sync ids, comma-separated. A bookmark in no
  Space has no key, the same as a bookmark from before this change. The key
  is in the Bookmarks file (JSON), so it survives a restart, a copy of the
  file, cut/copy/paste of bookmarks (`BookmarkNodeData` carries meta info)
  and undo. Chromium's HTML export and import (`bookmark_html_writer.cc`)
  don't write meta info, so they lose it, as any HTML bookmarks file would.
  Koan has no bookmark sync, so `unsynced_meta_info` doesn't matter. The
  upstream format is unchanged.
- **Rule** (`bookmark_spaces::IsShown()`): Zen's, except that ids of Spaces
  that are gone don't count. So a bookmark left with no Space shows
  everywhere, as the issue asks. A node is shown if it and every folder it
  is in pass (`KoanBookmarkSpaces::IsShown()`). That is what the bar and the
  menus can reach, and it gives the suggestions the same answer.
- **A window's filter** (`KoanBookmarkSpaces::FilterFor()`): the sync id of
  the window's active Space and the ids of the profile's Spaces
  (`KoanWindowSync::model()`). No filter, so every bookmark shows: when the
  setting is off, in a window outside window sync (private, New Blank Window,
  window sync off, before the window joins), and when the active Space has
  no sync id (an agent's Space, which never syncs).
- **Deleting a Space** (`KoanWorkspaceController::OnDeleteConfirmed()` →
  `KoanBookmarkSpaces::OnSpaceDeleted()`, before its tabs close): its id
  comes off every bookmark, and a bookmark left with none loses the key.
  This runs only for the user's delete in the window it was made in. A Space
  that leaves the model in another way (a reconcile) is not stripped. Its id
  just stops counting, so nothing is lost if the Space comes back. Renaming,
  re-iconing or moving a Space changes nothing, because bookmarks name its
  id.
- **Setting.** `koan.bookmarks.per_space` (`prefs::kBookmarksPerSpace`), on
  by default, in Settings > Tab Management > Workspaces. When it is off, every
  bookmark shows and the bubble row and Spaces… go away. The assignments are
  kept for when it comes back on.
- **Assigning.** Two places:
  1. The bookmark bubble (⌘D) gets a **Spaces** combobox under Folder:
     "All Spaces" (the default, Zen's "-"), then each Space. A bookmark in
     several Spaces starts on an extra entry that names them ("Work,
     Home"). It is saved with the bubble's other edits (Done, or closing),
     and only if it changed. Decision: a single-choice combobox, not Zen's
     checkbox dropdown. Upstream's `DialogModel` has no multi-select field,
     and a combobox lines up with Name and Folder. Several Spaces are set
     with (2).
  2. **Spaces…** in the bookmark context menu (the bar, its folder menus,
     the overflow menu, All Bookmarks, the app menu's Bookmarks), after
     Edit…/Rename…, for bookmarks and folders, also several at once. It
     opens a dialog with a checkbox per Space (Zen's checkbox list), titled
     Spaces. A Space is ticked if every selected bookmark is in it. Save
     writes the ticks to all of them, but only if something changed, so a
     mixed selection keeps its Spaces. It is not offered for permanent or
     managed folders, or when editing bookmarks is turned off by policy.
     Decision: this stands in for Zen's field in the properties dialog.
     Chromium's editor (`BookmarkEditorView`) and its folder rename dialog
     would each need their own layout patch.

## Upstream surfaces (Chromium 158), and what Koan does with each

| Surface | Code | Decision |
|---|---|---|
| Bookmarks bar | `BookmarkBarView::Layout()` lays the buttons out in order. `bookmark_buttons_` mirrors the bar folder's children (index = model index), and buttons are made as they fit | **override (hook)**: a button of another Space's bookmark is laid out with no room. It stays visible but can't take focus and is left out of the accessibility tree (`CollapseBookmarkBarButton()`). It is not hidden, because `GetFirstHiddenNodeIndex()` (the overflow menu's start), `GetBookmarkFolderForButtonAtLocation()` and the drop location loop all stop at the first hidden button. Relayout: `KoanBookmarkSpaces::Refresh()` on a Space switch, on a meta info change (`BookmarkMetaInfoChanged`: the bar's `BookmarkNodeChanged` doesn't fire for it), when the profile's Spaces or the window's ids change (`KoanWindowSync::AddSpacesChangedCallback()`) and on a change of the setting |
| Bar folder menus, the overflow (») menu, All Bookmarks, the app menu's Bookmarks | `BookmarkMenuDelegate::BuildMenu()` / `BuildNodeMenuItemAt()` | **override (hook)**: the item is built, then hidden (`HidesBookmark()`), so the delegate's index arithmetic (`AddBookmarkNode`, drops) keeps one item per child. Menus are built when they open, so they follow the Space then |
| macOS menu bar Bookmarks menu | `BookmarkMenuBridge` (per profile): `UpdateRootMenuIfInvalid()`, `AddNodeToMenu()` | **override (hook, Mac only)**: rebuilt at each opening while the setting is on, leaving out other Spaces' bookmarks for the profile's most recently active window. `menuHasKeyEquivalent:` returns NO, so this runs only when the menu opens |
| URL bar bookmark suggestions (also the `@bookmarks` keyword) | `BookmarkProvider::DoAutocomplete()` over `BookmarkModel::GetBookmarksMatching()` | **override (hook)**: other Spaces' bookmarks are dropped for the active window (the provider has no window, `GlobalBrowserCollection::GetLastActiveBrowser()`). Beyond Zen, which doesn't filter its URL bar: the issue asks for it, and it goes with history per Space. The history providers (HQP, HUP, shortcuts) still suggest the same pages as visited pages (see "Not done") |
| Bookmark bubble (⌘D, Bookmark This Page in the site panel and the menus) | `BookmarkBubbleView::ShowBubble()` (`ui::DialogModel`) | **override (hook)**: the Spaces row |
| Bookmark context menu | `BookmarkContextMenuController::BuildMenu()` / `ExecuteCommand()` | **override (hook)**: Spaces…, command `koan::kBookmarkSpacesCommand` (51030, inside upstream's gap 51029-51033: a clash fails to compile) |
| Side panel bookmarks (⌘B, Power Bookmarks WebUI) and its context menu | `BookmarksPageHandler`, its own `BookmarkContextMenu` list | **reuse**: shows every bookmark, as Zen's bookmarks sidebar does. Its menu builds its own list from the controller's items, so it has no Spaces… |
| Bookmark manager `chrome://bookmarks` | WebUI over `chrome.bookmarks` / `bookmarkManagerPrivate` | **reuse**: shows every bookmark, as Zen's Library. It doesn't show each bookmark's Spaces: not done (see below) |
| Bookmark editor and folder rename (Edit…, Rename…, Add Page…, New Folder…) | `BookmarkEditorView`, the folder editor | **reuse**: no Spaces row; Spaces… stands in for it. New bookmarks and folders start in no Space |
| Extensions | `chrome.bookmarks` (`BookmarksAPI`) over the model | **reuse**: they see every bookmark, as the issue asks. Meta info isn't exposed to them |
| Bookmarks file, undo, copy/paste, drag and drop | `BookmarkCodec` (meta_info), `BookmarkUndoService`, `BookmarkNodeData` | **reuse**: the key goes with the bookmark. A dragged or pasted copy keeps its Spaces. A new bookmark dropped on the bar has none |
| HTML export and import, other browsers' imports | `bookmark_html_writer.cc`, importers | **reuse**: no Spaces, the format has no place for them. Zen's own import leaves Zen's table out (see below) |
| Bookmark star | `BookmarkPageActionController` (hidden in Koan's URL bar, Z15) | **reuse**: a page bookmarked in another Space is still "bookmarked" (the bubble edits it), as in Zen |
| Managed bookmarks, permanent folders | `ManagedBookmarkService`, the bar/other/mobile nodes | **lock**: never given Spaces (no row, no Spaces…). `IsShown()` stops at permanent nodes |
| Saved tab groups on the bar | `SavedTabGroupBar` | **reuse**: not bookmarks; Koan keeps them off by default |
| Bookmark sync | not in Koan | none |
| NTP shortcuts | most visited, not bookmarks | none |

## Upstream behaviour Koan depends on (guards)

- The bar's loop still sets each button's visibility and bounds from
  `can_render_button_bounds` and `x`, and the first hidden button still
  means "the first that didn't fit": `KoanBookmarkSpacesBrowserTest.
  BarFollowsTheSpace` (a collapsed button keeps width 0 and can't take
  focus).
- `BookmarkModel::GetBookmarksMatching()` nodes are `BookmarkNode`s:
  `SuggestionsFollowTheSpace`.
- Meta info writes notify `BookmarkMetaInfoChanged` and not
  `BookmarkNodeChanged`: `SettingOffShowsEverything` (the bar follows a
  write).
- A hidden `MenuItemView` is skipped by the menu's layout and keyboard
  navigation: Linux steps 4 and 5 of the PR, Mac step BPS-2.

## Not done

- **`chrome://bookmarks` doesn't show each bookmark's Spaces.** The manager
  is WebUI over `chrome.bookmarks`, whose `BookmarkTreeNode` has no meta
  info. Showing the Spaces needs a Koan API to the page and a patch to the
  manager's item element. Zen's Library doesn't show them either.
- **Zen's assignments aren't imported.** The Zen importer reads places.sqlite
  through Chromium's Firefox importer, which doesn't read
  `zen_bookmarks_workspaces`. Mapping Zen's workspace uuids to the Spaces
  that #363's import makes is a follow-up.
- **History suggestions.** A bookmarked page of another Space can still
  come up as a visited page. Fixing that is history per Space (filed next to
  this).
- **Several Spaces in the bubble.** The bubble picks one Space or all;
  several are set with Spaces….
