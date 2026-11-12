# Containers per Space (#318): design and upstream audit

Date: 2026-11-12. Chromium 158 (pristine `git show HEAD:`), Zen 1.24b
(`.cache/zen-desktop-1.24b`, shipped defaults in `.cache/zen-src-1.24b/defaults/preferences/firefox.js`).
This builds on `.agent/notes/upstream/containers.md`, which covers the container core: the partition
primitive and hooks C1-C5, the restore hook, and Brave's design. Zen and Brave are MPL-2.0. They were
read for behaviour only, and none of their code is in Koan.

## What Zen does

- **Data.** A workspace has one `containerTabId` (a Firefox userContextId, where 0 means none). It
  is kept with the Spaces in the session and in sync. If the container is deleted, the Spaces that
  used it go back to 0 (`ZenSpaceManager.mjs` ~178).
- **UI.**
  - The Create-a-Space form has a **Profile** row (`zen-workspace-creation-profile`, tooltip
    "Profiles are used to separate cookies and site data between spaces."). A button on the row
    names the profile and opens Firefox's container menu, with "No Container" relabelled
    **Default**. The row defaults to Default.
  - The Space menu has a **Set Profile** submenu after Theme… (`popups.inc:20`), with Default and
    the containers. It is hidden in private windows and when there are no containers.
  - Containers are made in Firefox's settings. Zen creates none by default
    (`zen.containers.create-default-identities` false).
- **New tabs** (`Tabbrowser-sys-mjs.patch` → `getContextIdIfNeeded`, SM ~3309):
  - A tab that names no container, and every link from another app, opens in the active Space's
    container and is marked `zenDefaultUserContextId`.
  - A tab that names another container keeps it. Links and popups pass their opener's container,
    so links inherit.
  - An add-on's tab with no container stays in Firefox's default.
  - Zen's own empty tab is skipped.
  - A Space route uses the routed Space's container when the tab named none, or when its container
    equals the source Space's (`shouldUseRouteContainer`).
  - `force-container-workspace` (**false** in 1.24b) would move a tab to the one Space with its
    container.
- **Moving** a tab between Spaces never changes its container. Firefox can't change it in place.
- **Indicator.** Firefox's `.tab-context-line` is the container's colour, a 2 px line at the start
  edge of a vertical tab, inset by half the radius (3 px on Essentials). With
  `zen.workspaces.hide-default-container-indicator` (**true**), it is hidden on tabs that took
  their Space's container.
- **Essentials.** `zen.workspaces.separate-essentials` is **true** in the shipped defaults. Only
  the Essentials of the active Space's container show.
- **Glance** opens in the current tab's container.
- **Deleting a Space** leaves its container and its data alone. Containers live on their own.
- **Shared vs isolated** (Firefox containers): cookies, storage, cache, service workers and HTTP
  auth are per container. History, bookmarks, passwords, site permissions
  (`permissions.isolateBy.userContext` false) and extensions are shared.

## What Chromium does (158)

A new tab's partition is chosen in exactly one place, `CreateTargetContents()`
(`browser_navigator.cc`). It uses `params.opener`'s SiteInstance, else
`tab_util::GetSiteInstanceForNewTab()`, which is the default partition. Koan's C2 hook already
replaces that with the source's container for tabs a page opens.

`params.source_site_instance` is set only when a page or its context menu opens the tab:
Cmd-click, middle-click, "Open link in new tab", and Glance. It is not set for the omnibox,
Cmd+T, bookmarks, the Library, DevTools `Target.createTarget`, startup tabs or `chrome.tabs.create`.
`chrome.tabs.create` sets `initiator_origin` to the extension (`open_tab_helper.cc:296`).

`source_contents` is filled with the active tab for every navigation (`NavigateImpl`), so it says
nothing about where the tab came from. Everything after that (fixed partition across navigations,
noopener, discard, prerender, restore) is covered by C1-C5 and the restore hook.

## Design

**Containers.** The user's containers ("profiles" in the UI, Zen's word) are saved user
containers. Each has a `ContainerKind::kUser` id starting with `s`, which puts it on disk at
`Storage/ext/koanuser<id>`. They are kept in `koan.containers.user` (`prefs::kUserContainers`,
a list of `{id, name, color}`). The colour is a `tab_groups::TabGroupColorId`, picked round-robin,
so the indicator uses Chromium's tab group colour ids (light and dark). Pure logic is in
`src/browser/containers/koan_user_containers.{h,cc}`, unit tested.

**A Space's container** is part of its `SpaceData`. It is stored like its theme:
- in every tab of the Space, as session data `koan.space_container` (a `Serialize()`d user
  container, or `none`); a tab that joins the Space takes it
- in `koan.workspaces.list` (`"container"`), for a start without a session restore
- in window sync's `SpaceRecord.container`, so every window agrees

Only a valid, saved user container counts; anything else reads as none.

**Where new tabs go** (`koan::SiteInstanceForNewTab()`, `koan_space_containers.cc`). This is the
C2 hunk, now calling Koan's Spaces layer instead of the container core directly. The patch stays
the same size. The rules, in order (`NewTabContainer()`, unit tested):
1. A tab with an opener (`window.open`, same BrowsingInstance): upstream.
2. A URL that isn't a web page or the new tab page (chrome:// other than chrome://newtab,
   chrome-extension://, devtools://, isolated-app://): upstream, i.e. the default partition.
   Extension pages must stay in their own partition, and settings pages hold nothing per site.
3. A tab opened by a page (`source_site_instance`): its opener's partition, as Firefox's links
   inherit. The core's `SiteInstanceForNewTab()` keeps a container, and the default stays the
   default. The one exception is Zen's route rule: when a route sends the tab to another Space
   and the source tab was in its own Space's container, the tab takes the routed Space's container.
   Agent tabs are never re-homed.
4. A tab from an extension (`initiator_origin` chrome-extension://) with no page behind it: the
   default partition, as Zen's add-on rule.
5. A tab a route sends to another Space (http(s), in the active Space, including links from
   other apps): that Space's container.
6. Any other tab that no page opened: the Space it opens in (`params.group`, else the active
   Space). This covers Cmd+T, the URL bar, bookmarks, history, links from other apps, New Split,
   "+ New Tab" and DevTools. Its container if it has one, else the default.
7. A window without Spaces (Incognito, a New Blank Window, Spaces off) or a pinned tab
   (an Essential): upstream.

**The Space's empty tab** must be in its container, because a bookmark or a dropped link can
navigate it in place. Each update (`MatchEmptyTabsToSpaceContainers()`) replaces an empty tab that
isn't in its Space's container: a new blank page is made in the right partition, at the same place
and with the same selection, and the old one is closed. This one rule covers every way a Space
gets an empty tab: Create Space (made before the Space has its data), a Space recreated from
`koan.workspaces.list`, a closed Space coming back, the adopted last tab, and a container changed
on an existing Space (Set Profile, or another window's sync).

**The indicator** (`PaintSidebarTab`, no new patch). A 2 px line in the container's colour sits
at the start edge of a tab row, inset by half the corner radius; Essentials get 3 px, clipped to
the tile. It shows only on a tab in a user container other than its Space's (for Essentials, the
active Space's). That is Zen's `hide-default-container-indicator` on. Zen hides the line by a
sticky per-tab mark, Koan by comparing with the Space. They agree for tabs that stay in their
Space, and Koan's line also shows after a move. Spaces repaint their tabs when the container
changes.

## Hard cases (decisions)

| Case | Decision |
|---|---|
| What isolates | Cookies, localStorage, sessionStorage, IndexedDB, Cache Storage, the HTTP cache, service workers, HTTP auth, the network context (sockets, HSTS state), shared workers, renderer processes. Each is per StoragePartition. |
| What's shared | History, bookmarks, passwords, autofill, site permissions (content settings are per profile; Firefox shares them too), extensions, downloads, zoom, the omnibox's suggestions. As in Firefox's containers. |
| Notifications / push | Upstream refuses them outside a site's home partition (`IsPermissionBlockedInPartition`, kUndelegated). Koan keeps that: a container's push would wake the site's default-partition service worker. Documented limit. |
| WebUSB / HID / Serial / Bluetooth | Upstream refuses them in non-default partitions. Kept (Brave keeps it too). |
| Tab moved to another Space (menu, drag, Move To) | Keeps its partition, as in Zen. A WebContents can't change partition, and reopening would lose the page's state and form input. The colour line then shows it is in another container. |
| Tab dragged to another window | Same WebContents, same partition. |
| Essentials | Shared by every Space, as now. Each keeps the container it had when it was pinned. New Essentials made by Navigate (an extension's pinned tab) are left to upstream. Zen's `separate-essentials` (Essentials per container) is a follow-up: hiding tiles touches the pinned grid's layout, which other work is changing. Not done, said in ZEN_PARITY. |
| Pinned tabs | Keep their container. Session restore and `koan.workspaces.list` (`StoredTab` keeps the session data with `koan.container`) restore them into it. |
| Glance | From a page: the page's partition (C2 via the core, unchanged). |
| Split view | "New Split" adds a tab through Navigate with no page behind it, so it opens in the Space's container. A split of existing tabs keeps each tab's. |
| Window sync | The Space's container syncs with its record. Synced pinned tabs and Essentials already copy their tab's container to the placeholder (`koan_window_sync_items.cc`). |
| Session restore / restart | Saved user containers are on disk. Tabs restore into theirs (`SiteInstanceForRestoredTab`), and Space containers come back with the tabs' session data or the stored list. Storage-partition GC keeps the user's containers: `LoadSavedContainers()` loads them too, which is the C5 hook, unchanged. |
| A start without session restore | Spaces come back from `koan.workspaces.list` with their container, and their empty tabs are healed into it. Startup pages and Essentials restored from Chromium's pinned list open in the default partition (`PinnedTabCodec` keeps no container). Documented. |
| Extensions | Content scripts and webRequest run in container tabs as in any tab. `chrome.tabs.create` opens in the default partition (Zen's add-on rule). `chrome.cookies` sees only the default partition (upstream). Documented. |
| Downloads | Go through the container's network context, into the profile's one download list. |
| Incognito | No Spaces, so no containers. No Profile row and no Set Profile. Containers are never made off the record. |
| Deleting a Space | Its container and data stay, listed for reuse, as Zen leaves containers alone. Deleting a container (and its data) belongs to container management in Settings, a follow-up. |
| Clear browsing data | Upstream clears the default partition only. Container data stays until the container is deleted. Documented. |
| URL bar suggestions | Shared history and bookmarks, as in Firefox. Typed URLs open as rule 6. |
| Switch to Tab | Switches to the tab wherever it is, across containers. Upstream and Firefox both do (`switchTabs.searchAllContainers`). Agent tabs stay hidden (existing hook). |
| Space routing | Rules 3 and 5. The routed tab is created in the routed Space's container before routing moves it. |
| Prerender / NoStatePrefetch / preconnect | Off in container tabs (C4, B10). The prerendered page would be built in the default partition. Accepted cost. |
| Discard | The replacement is created in the old tab's partition (C3). |
| chrome:// pages in a container Space | Open in the default partition (rule 2), except the new tab page. A URL typed into such a page then loads in the default partition. Accepted: settings pages are rare starting points. |
| Agent containers | Untouched: agent tabs never take a Space's container, and agent Spaces have none. |

## Hooks

No new patch. The C2 hunk in `browser_navigator.cc` now calls `koan::SiteInstanceForNewTab(params,
url)` (`koan_space_containers.h`, in `//koan/browser/ui/views`, which the navigator already
depends on) instead of `koan::containers::SiteInstanceForNewTab()`. It is the same size, and
upstream's guard (`CreateTargetContents` is still the only place the new tab's SiteInstance is
picked) is the existing REBASE row. Everything else uses Koan's own extension points: the
controller's `Update()`, the sidebar paint hook, the session data, and the stored Space list.

## Tests

- **Unit tests** (`koan_containers_unittests`):
  - `UserContainersTest`: parse, colours, names
  - `NewTabContainerTest`: rules 1-7
  - `ContainerLineTest`: the indicator rule
- **Browser tests** (`koan_space_containers_browsertest.cc`):
  - a Space's container is stored and read back
  - a new tab in a container Space is in its partition, and in a Space without one it isn't
  - a link opened from a default tab keeps the default
  - a moved tab keeps its partition
  - the empty tab follows Set Profile
  - the list and the session restore the Space's container
- **e2e** `scripts/e2e/space_containers.mjs` (seeded `koan.workspaces.list` and containers): the
  reviewer's check with a local page.
  - Space A logs in (a cookie plus a localStorage theme). Space B (its own container) sees
    neither. Space C (A's container) sees both. Space D (none) sees neither.
  - Logging out in A leaves B's own login.
  - After a restart, A is still logged in.
  - Nothing fatal is logged.
