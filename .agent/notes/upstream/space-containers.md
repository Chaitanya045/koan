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
on an existing Space (Set Profile, or another window's sync). Create Space and Set Profile also
run it at once, before they return (#543): until then the empty tab is in the partition of the
Space that was active, and a bookmark or a link dropped on it in that task would load there.

The profile menu (Default, the profiles, New Profile…) is a submenu of the Space menu, so its
command ids are apart from the Space menu's: views runs a menu command by looking its id up in the
whole menu, parent first (`MenuModelAdapter::ExecuteCommand()`). With the same ids, Set Profile ▸
Default ran Change Name and ▸ New Profile… ran Change Icon (#543; macOS's native menus were not
affected).

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
| What's shared | Bookmarks, passwords, autofill, site permissions (content settings are per profile; Firefox shares them too), extensions, downloads, zoom. As in Firefox's containers. History is kept apart per profile since #474 (`space-history.md`): one history, each visit tagged with its profile. |
| Notifications / push | Since #478: a page's own notifications (`new Notification()`) are allowed in a Space's profile. Since #530: a service worker's too (`registration.showNotification()`), routed to that profile's worker. Push stays refused there. See "Notifications (#478)" and "Service-worker notifications (#530)" below. Agent containers: all refused, as before. |
| WebUSB / HID / Serial / Bluetooth | Upstream refuses them in non-default partitions. Kept (Brave keeps it too). |
| Tab moved to another Space (menu, drag, Move To) | Keeps its partition, as in Zen. A WebContents can't change partition, and reopening would lose the page's state and form input. The colour line then shows it is in another container. |
| Tab dragged to another window | Same WebContents, same partition. |
| Essentials | Each keeps the container it had when it was pinned. New Essentials made by Navigate (an extension's pinned tab) are left to upstream. Zen's `separate-essentials` (Essentials per container) is done in #479: see "Essentials per profile" below. |
| Pinned tabs | Keep their container. Session restore and `koan.workspaces.list` (`StoredTab` keeps the session data with `koan.container`) restore them into it. |
| Glance | From a page: the page's partition (C2 via the core, unchanged). |
| Split view | "New Split" adds a tab through Navigate with no page behind it, so it opens in the Space's container. A split of existing tabs keeps each tab's. |
| Window sync | The Space's container syncs with its record. Synced pinned tabs and Essentials already copy their tab's container to the placeholder (`koan_window_sync_items.cc`). |
| Session restore / restart | Saved user containers are on disk. Tabs restore into theirs (`SiteInstanceForRestoredTab`), and Space containers come back with the tabs' session data or the stored list. Storage-partition GC keeps the user's containers: `LoadSavedContainers()` loads them too, which is the C5 hook, unchanged. |
| A start without session restore | Spaces come back from `koan.workspaces.list` with their container, and their empty tabs are healed into it. Startup pages and Essentials restored from Chromium's pinned list open in the default partition (`PinnedTabCodec` keeps no container). Documented. |
| Extensions | Content scripts and webRequest run in container tabs as in any tab. `chrome.tabs.create` opens in the default partition (Zen's add-on rule). `chrome.cookies` sees only the default partition (upstream). Documented. |
| Downloads | Go through the container's network context, into the profile's one download list. |
| Incognito | No Spaces, so no containers. No Profile row and no Set Profile. Containers are never made off the record. |
| Deleting a Space | Its container and data stay, listed for reuse, as Zen leaves containers alone. Deleting a container (and its data) is in Settings (#477, "Managing profiles" below). |
| Clear browsing data | Clears every user container too, for the same types and time range (#477, below). |
| URL bar suggestions | Since #474, the history suggestions are the active Space's profile's (`space-history.md`); bookmarks are shared, as in Firefox. Typed URLs open as rule 6. |
| Switch to Tab | Since #474, only tabs of the active Space's profile (`space-history.md`); off with Keep history apart for each profile, across containers as upstream and Firefox (`switchTabs.searchAllContainers`). Agent tabs stay hidden (existing hook). |
| Space routing | Rules 3 and 5. The routed tab is created in the routed Space's container before routing moves it. |
| Prerender / NoStatePrefetch / preconnect / search prefetch | Off in container tabs (C4, B10, and the search prefetch and browser-side preconnect hooks in `space-proxy.md`). Each would load or connect in the default partition, with its cookies and outside a Space's proxy. Accepted cost. |
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

## Notifications (#478)

Date: 2026-11-27. Chromium 158 (pristine). Decided before coding; the reviewer can object here.

### What upstream does

Chromium's model is one **home partition per origin** (`BrowserContext::GetStoragePartitionForUrl()`, which
`ChromeContentBrowserClient::GetStoragePartitionConfigForSite()` answers: the default partition for web sites, an
extension's or an isolated web app's own). Every background path finds the site's data through it:

- `NotificationEventDispatcherImpl::DispatchNotificationEvent()` (`notification_event_dispatcher_impl.cc`): a
  **service-worker notification's** click or close reads the notification from
  `GetStoragePartitionForUrl(origin)->GetPlatformNotificationContext()` and wakes that partition's service worker.
- `PushMessagingRouter::DeliverMessage()`: a **push message** goes to `GetStoragePartitionForUrl(origin)`'s
  service worker with the registration id the subscription was made with. A subscription is keyed by origin and
  service-worker registration id only (`PushMessagingAppIdentifier`, `PushMessagingServiceImpl`, one per profile),
  and registration ids are per partition, so the same id can name another registration in the default partition.
- DevTools' background-service logging for both does the same.

So `PermissionUtil::IsPermissionBlockedInPartition()` refuses `NOTIFICATIONS`, the only `kUndelegated` permission,
in any partition but the site's home one: it covers push as well, which asks for the notifications permission.
`PermissionManager` asks it before every request and status, so the answer is `denied` with no prompt. Content
settings are per profile, keyed by origin: a grant is the site's in every partition. `PlatformNotificationServiceImpl`
is per profile and shows what content gives it; the notification ids are profile-wide
(`ReadNextPersistentNotificationId()` is the profile's), with a tag in place of the number when the page gives one.

A **page's own notification** (non-persistent: `new Notification()` in a document or a dedicated worker) has none of
this: content keeps its listener, the page's frame, by notification id (`NotificationEventDispatcherImpl`'s
non-persistent listeners), and show, click and close go straight back to that page. Nothing looks up a partition.

Others:
- **Brave**'s containers return false from `IsPermissionBlockedInPartition()` for every container partition, so
  service-worker notifications and push are allowed there too. Brave has no change in `content/browser/push_messaging`
  or `content/browser/notifications` (GitHub code search, 2026-11-27), so those clicks and messages go to the default partition.
- **Isolated web apps** don't meet the problem: the app's origin's home partition *is* its own partition.
- **`<webview>` and Controlled Frame** guests are outside the site's home partition, so notifications are refused
  there; `PermissionContextBase` refuses most permissions for guests anyway (`IsPermissionRequestable()`).
- **Firefox (Zen)** keeps a push subscription's origin attributes, container included (UNVERIFIED: Firefox's
  PushService, not in Zen's tree), so its containers don't meet this.

### Decision

| Case | Decision | Why |
|---|---|---|
| A page's notifications in a Space's profile (user container) | **Allowed.** The page asks, and the answer is the site's, shared with the default partition and every profile, like any site permission (Firefox's containers share them too). | Nothing in that path depends on the partition. |
| Service-worker notifications (`registration.showNotification()`, also from an open page) in a Space's profile | **Allowed since #530** (refused by #478's N1 until then). | The notification's id names its partition, so its click and close reach that profile's worker: "Service-worker notifications (#530)" below. |
| Push in a Space's profile | **Refused**: content sees no push service there (`SERVICE_NOT_AVAILABLE`, "push service not available"), as with Chromium's push kill switch. | Messages are routed by origin to the default partition (`DeliverMessage()`), and the subscription key would collide with the default partition's registration ids. Making it work needs the partition in the subscription (stored in prefs per profile), in delivery, and in the notification click path: four content patches plus a stored-format change. A follow-up, not this issue. Whether Koan's builds can push at all (they configure no Google API keys, and GCM needs them) is a separate, unverified question. |
| Agent containers | **Unchanged**: every permission refused, never asked (B1). | Agents must not notify the user as a site. |
| Other partitions (extensions, isolated apps, guests) | Upstream's. | Koan's hook answers only for its own containers. |
| Another `kUndelegated` permission in a later Chromium | Stays refused in a Space's profile: the hook allows `NOTIFICATIONS` by name. | The same routing problem would likely apply. |

**What the user sees.**
- A page in a Space's profile asks for and shows notifications as in any tab.
- The line under the notification's title names the Space after the site ("mail.example.com · Work"; for a tab in no
  Space, an Essential, its profile's name), so two accounts of one site can be told apart. Past 28 characters
  (what a macOS banner's subtitle fits, which shows a set line untrimmed) the site is cut to its domain, so the
  Space's name stays. On macOS that line is the
  notification's subtitle; on Linux it is the start of the body. A default tab's notification is upstream's (the
  site only).
- A click shows the notification's tab: its window, its Space and the tab, as Firefox does; Chromium leaves it to the
  page's own click handler, and that still runs. If the tab has closed, the click shows the Space it was in, and
  upstream then opens the site in a new tab there, so in that Space's profile.
- The site panel of a web page in a Space's profile says "Push notifications aren't available in this Space's
  profile. The site can notify you while its tab is open.", unless the site is blocked from notifying. It is a
  fact about the profile, so it shows whether or not the site tried: a site that subscribes to push without asking
  first never gets a prompt, and the note is the only sign.

**Known limits.**
- Two tabs of one site (two accounts) that give their notifications the same tag replace each other's
  notification, as two tabs of one site do upstream: the ids are the origin and the tag.
- A shared worker's notification has no page: no Space in its line and no tab to show.
- After a restart a notification left in macOS's Notification Center has no tab: upstream's new tab opens in the
  active Space.

### Hooks

- **B1, grown** (`components/permissions/permission_util.cc`, `IsPermissionBlockedInPartition()`): one call,
  `koan::containers::PermissionBlockedInContainer(process, permission == NOTIFICATIONS)`; true for an agent container,
  false for notifications in a valid user container, else upstream decides. 4 lines more than the agent block it replaces.
- **N1** (gone since #530) `content/browser/notifications/blink_notification_service_impl.cc`
  (`DisplayPersistentNotification()`) refused in a partition whose domain is a user container's.
- **N2** `content/browser/push_messaging/push_messaging_manager.cc` (`GetService()`): no push service for a process in
  a user container. Every subscribe, unsubscribe and lookup already handles a missing service.
- **N3** `content/public/browser/notification_event_dispatcher.h` + `notification_event_dispatcher_impl.{h,cc}`:
  `GetNonPersistentNotificationFrame(id)`, the frame content already keeps for a page's notification. Upstream
  exposes no way from a notification to its page; the alternative, guessing the tab from the origin, is wrong exactly
  when one site is open in two profiles.
- **N4** `chrome/browser/notifications/platform_notification_service_impl.cc` (`DisplayNotification()`, after
  `CreateNotificationFromData()`) and `non_persistent_notification_handler.cc` (`OnClick()`, before dispatching), +
  `notifications/BUILD.gn` (`//koan/browser/ui/views`): `koan::OnShowingPageNotification()` and
  `koan::OnPageNotificationClicked()` (`koan_page_notifications.h`). `NotificationDisplayService::Observer` sees a
  notification only as const and has no click event.

### Push and a container's service workers (#596)

Date: 2026-11-28. Chromium 158 (pristine).

- Every partition of a profile has its own `PushMessagingContext` (`StoragePartitionImpl::Initialize()`), which
  observes that partition's service workers and calls the **profile's** push service:
  `OnRegistrationDeleted(id, scope, key)` → `PushMessagingService::DidDeleteServiceWorkerRegistration(origin, id)`,
  `OnStorageWiped()` → `DidDeleteServiceWorkerDatabase()`. Neither passes the partition.
- `PushMessagingServiceImpl` keys a subscription by origin and registration id
  (`PushMessagingAppIdentifier::FindByServiceWorker()`), and registration ids come from each partition's own
  service-worker database, a counter from 0 (`ServiceWorkerDatabase`, `INITDATA_NEXT_REGISTRATION_ID`). So a site's
  worker in a Space's profile or an agent container has the id of its subscribed worker in the default partition
  whenever the two counters line up. On a fresh Linux profile the default partition's id 0 is a built-in
  extension's worker, so a site's first worker there is 1, and a profile's second worker is 1 too. Unregistering it there, or clearing the container's data (`StoragePartition::ClearData()` deletes every
  registration, which sends `OnRegistrationDeleted()`), unsubscribes the default partition's subscription:
  `UnsubscribeInternal()` clears the subscription id from the **default** partition's worker
  (`PushMessagingService::ClearPushSubscriptionId()`, `GetStoragePartitionForUrl(origin)`), deletes the app
  identifier and unsubscribes from the push server. A wiped database in any partition unsubscribes everything.
- Push is refused in Koan's containers (N2; agent containers have no notification permission), so they never have a
  subscription of their own. **H1** (below) drops both events for a Koan container's partition. Other partitions
  (extensions', isolated apps', guests') keep upstream's behaviour: an isolated app's push is its partition's.
  H1 holds only while N2 refuses push in containers: if push is ever allowed in a Space's profile (#530 part 2),
  H1 must change with it, to unsubscribe that profile's own subscriptions (keyed by partition) instead of dropping
  the events.

### Hooks (#596)

- **H1** `content/browser/push_messaging/push_messaging_context.cc` (`OnRegistrationDeleted()`, `OnStorageWiped()`):
  return first when the partition's domain is a Koan container's
  (`koan::containers::IsContainerPartitionDomain()`, `:domain`), or when the context has no partition any more (at
  shutdown). No extension point: the observer is content's, and the push service gets no partition to tell them apart.

## Service-worker notifications (#530)

Date: 2026-11-28. Chromium 158 (pristine). Part 1 of #530; part 2 (push) is the design note at the end, with no
code yet.

### What upstream does

- A service worker's notification (`registration.showNotification()`, a *persistent* one) is written to the
  partition's own notification database (`PlatformNotificationContextImpl`, one per StoragePartition, under its
  `Platform Notifications` folder) and shown through the profile's `PlatformNotificationServiceImpl`. Its id is made
  in `DoWriteNotificationData()` by `NotificationIdGenerator::GenerateForPersistentNotification()`:
  `p[b]#<origin>#<0|1><number|tag>`. The number is the profile's (`ReadNextPersistentNotificationId()`); with a tag
  the id is the origin and the tag only, so the same site's notifications in two partitions with one tag had one id.
- What the platform keeps is that id, the origin and the profile (macOS: in the notification's userInfo, across a
  restart; Linux: in memory, closed at exit). A click, an action button or a close goes
  `PersistentNotificationHandler::OnClick()` / `OnClose()` → `NotificationEventDispatcherImpl::
  DispatchNotificationClickEvent()` / `DispatchNotificationCloseEvent()` → `DispatchNotificationEvent()`, which takes
  `BrowserContext::GetStoragePartitionForUrl(origin)` (the default partition for a web site) and reads the
  notification there by id (`ReadNotificationDataAndRecordInteraction()`), then finds the service worker by the
  stored registration id in that partition (`FindReadyRegistrationForId()`). Nothing else is looked up. If the read
  fails, `OnClickCompleted()` closes the notification (`kDatabaseError`).
- `getNotifications()` and `notification.close()` from a page or worker use its own partition's database
  (`BlinkNotificationServiceImpl`), so they are already per partition.
- `clients.openWindow()` from the click goes `service_worker_client_utils::OpenWindow()` →
  `ContentBrowserClient::OpenURL(site_instance, params)` with the worker's SiteInstance, and
  `ChromeContentBrowserClient::OpenURL()` navigates with `NavigateParams` that carry no partition: the tab is the
  active Space's. Content then looks for the new window among the worker's partition's clients
  (`DidNavigate()`), so in another partition the promise resolves to null. `client.focus()` activates the client's
  own tab (`WebContents::Activate()`), which Koan already follows into its Space (upstream's focus mode exits, the
  Space of the active tab shows).
- DevTools' background-service log of these events (`devtools_event_logging.cc`) also takes the origin's partition.

**Brave** lets containers have the notification permission (`chromium_src/components/permissions/permission_util.cc`)
and has no override of `notification_event_dispatcher_impl.cc` or `platform_notification_context_impl.cc` (GitHub
code search, 2026-11-28): a container's service-worker notification is shown, and its click goes to the default
partition, where the id isn't found and the notification is closed. **Firefox** keys a notification by its
principal, origin attributes and so the container included (UNVERIFIED: Firefox's `dom/notification`, not in Zen's
tree), so its clicks reach the container's worker.

### Design

**The partition travels in the id.** The notification's id is the one thing content, the platform and macOS's
Notification Center keep with a notification, and the per-partition database is keyed by it, so the id is where the
partition goes. For a user container (a Space's profile) the id is `p[b]koanuser<id>#<origin>#...`: the
partition's domain before the first `#` (`TagPersistentNotificationId()`, `koan_partition_domain.h`, no deps).
`DispatchNotificationEvent()` reads it back (`PartitionDomainOfNotificationId()`) and takes that partition instead
of the origin's. Only well-formed user container domains count (`koanuser` and 2-40 lowercase letters), before the
origin, so the site's tag (after it) can't make one. The default partition's ids are upstream's, unchanged.

| Case | Decision |
|---|---|
| Show in a Space's profile | Allowed (#478's N1 refusal removed). The permission is the site's, shared, as for a page's notifications. |
| Click, action button, reply, close | Reach the profile's worker: the dispatcher reads that partition's database. The default partition's and another profile's never see it: their databases don't have the id. |
| The same site and tag in two profiles | Two ids, two notifications (they replaced each other before). |
| `getNotifications()` | Already the partition's own database; returns that profile's only. |
| The label | The line under the title names a Space with the profile after the site, as a page's (`OnShowingServiceWorkerNotification()`): the Space of a tab of the site in that profile, else the shown Space if it has the profile, else the first that has it, windows by last use; with none, the profile's name. |
| `clients.openWindow()` | Opens in the profile, in that same Space (`PlaceServiceWorkerWindow()` sets `params.browser` and `params.group`; `koan::SiteInstanceForNewTab()` rule 6 then gives the Space's profile), and the Space shows with the new tab. With no Space of that profile, the tab still opens in the profile (`params.source_site_instance` = the worker, rule 3), in the Space it opens in, with the profile's line. A payment handler's window takes the same path. |
| `client.focus()` | The client's own tab, so its own Space (Koan follows the active tab). |
| Restart, a notification still on screen (macOS) | Its id still names the profile, so the click routes there. Koan loads the partition first if the profile is listed (`koan::LoadNotificationPartition()` in `PersistentNotificationHandler::OnClick()`/`OnClose()`, N8), since no tab of it may have loaded it yet. |
| A profile that no longer exists | Not listed, so N8 doesn't load it, and N6 looks the partition up with `can_create = false`: the click, action or close is dropped (`kDatabaseError`, upstream then closes the notification) and the partition, folder and all, isn't created again. In the session the profile was deleted in, its partition is still loaded (content can't unload one) and its database is what the deletion left. |
| Agent containers | Unchanged: no permission (B1), so no notification. Their domains are never tagged or read back. |
| DevTools' notification log | Still the default partition's (not hooked; rare and harmless). |

### Hooks

- **N5** `content/browser/notifications/platform_notification_context_impl.{h,cc}`: a `const std::string
  koan_partition_domain_` member (2 lines + comment), set in the constructor from the partition
  (`ServiceWorkerContextWrapper::storage_partition()`, set before the context is made; 6 lines), and one call in
  `DoWriteNotificationData()` after the id is generated (2 lines + comment). The id has to be made on the database's
  sequence, where the partition can't be read.
- **N6** `content/browser/notifications/notification_event_dispatcher_impl.cc` (`DispatchNotificationEvent()`, the
  N3 patch's second hunk): after upstream's `GetStoragePartitionForUrl()`, the partition the id names, only if loaded
  (`can_create = false`); none drops the event (14 lines + comment). No extension point: content picks the partition
  from the origin alone.
- **N8** `chrome/browser/notifications/persistent_notification_handler.cc` (`OnClick()`, `OnClose()`, right before
  the dispatch): `koan::LoadNotificationPartition()` loads the partition the id names if that profile is listed
  (`koan.containers.user`), so N6 finds it after a restart; content can't read Koan's profile list. 2 lines each +
  include, `chrome/browser/notifications/BUILD.gn` already has `//koan/browser/ui/views` (N4).
- **N7** `chrome/browser/notifications/platform_notification_service_impl.cc` (`DisplayPersistentNotification()`,
  the N4 patch's second hunk: one call, `koan::OnShowingServiceWorkerNotification()`) and
  `chrome/browser/chrome_content_browser_client.cc` (`OpenURL()`: one call, `koan::PlaceServiceWorkerWindow()`;
  the file's existing patch grows by a hunk and an include). `NotificationDisplayService::Observer` sees a
  notification only as const; `NavigateParams` has no field for the worker's SiteInstance.
- **N1 removed** (`blink_notification_service_impl.cc`, 13 lines): that file is pristine again.

### Push (#530 part 2): design note

**Does push work in Koan today, in the default partition?** Yes, checked on 2026-11-28 with the develop nightly
(eb72989) on Linux, a local test site and a real VAPID key (`~/work/koan/.cache/pr-test/out/530/tools/push_probe.mjs`):
`pushManager.subscribe()` resolved after about 33 s with an FCM endpoint (`https://jmt17.google.com/fcm/send/...`),
FCM accepted an empty VAPID-signed message (201), and the worker got the `push` event and showed its notification
within seconds. Koan's log has GCM errors on the way (`PHONE_REGISTRATION_ERROR`, then MCS `401 Authentication
Failed: wrong_secret`, "Failed to log in to GCM, resetting connection"), but GCM retries and gets through: Web Push
uses GCM's check-in and registration, not the Google API keys Koan doesn't have. Not checked: the Mac build (same
code), and how long a connection stays up. Google could stop serving unkeyed Chromium builds; nothing says it does
today.

**What carrying the partition through push takes** (Chromium 158):
1. **Subscription identity.** A subscription is a `push_messaging::AppIdentifier`
   (`components/push_messaging/app_identifier.cc`: an app id `wp:<origin>#<guid>`, kept in
   `prefs::kPushMessagingAppIdentifierMap` with the value `origin#registration id#expiration#user-visible`), found by
   `PushMessagingAppIdentifier::FindByServiceWorker(profile, origin, registration_id)`. Registration ids are per partition and start at 0, so
   the same site's worker in two partitions often has the same id. The partition must join the stored value (a
   stored-format change, read back by an old Koan as a malformed entry and dropped) and every lookup.
2. **Content's calls carry no partition.** `PushMessagingService::SubscribeFromDocument/Worker()` get a frame or a
   process (the embedder can derive the partition), but `GetSubscriptionInfo()` and `Unsubscribe()` get only
   origin and registration id; `PushMessagingManager` (content) would pass it: a `content/public` API change.
3. **Delivery.** `PushMessagingServiceImpl::OnMessage()` → `PushMessagingRouter::DeliverMessage()` →
   `GetStoragePartitionForUrl(origin)`: the partition from the app identifier must reach the router (another
   `content/public` change). `PushMessagingNotificationManager::EnforceUserVisibleOnlyRequirements()` counts the
   origin's notifications in `GetStoragePartitionForUrl(origin)` too (`push_messaging_notification_manager.cc`), and
   would show Chromium's "This site has been updated in the background" for every profile message without the fix.
   `GetSWData()`, `ClearPushSubscriptionId()`, `UpdatePushSubscriptionId()` (content statics) also take the origin's
   partition.
4. **The click** of a notification a push message shows: done by part 1 (it is shown from the profile's worker, so
   its id names the profile).
5. **Clean-up.** `PushMessagingContext` (one per partition) tells the profile's push service
   `DidDeleteServiceWorkerRegistration(origin, registration_id)` and `DidDeleteServiceWorkerDatabase()` with no
   partition. Without push in profiles this unsubscribed the default partition's subscription with the same origin
   and registration id when a site's worker in a Space's profile (or an agent container) was unregistered or its
   data cleared, and a profile's wiped worker database unsubscribed every subscription of the user. #597 fixed that
   on its own (**H1**, `push_messaging_context.cc`, "Push and a container's service workers (#596)" above): it drops
   both events for a container's partition. Push per profile must change H1: the events must then reach the
   profile's own subscriptions (with the partition), not be dropped.

That is about six to eight hook patches (content: `push_messaging_manager.cc`, `push_messaging_router.cc`,
`push_messaging_context.cc`, `push_messaging_service.h` and its statics; components and chrome:
`components/push_messaging/app_identifier.{h,cc}`, `push_messaging_app_identifier.cc`, `push_messaging_service_impl.cc`,
`push_messaging_notification_manager.cc`), a few of them over 20 lines, plus the stored-format change: more than
#511 counted, and the largest patch set of any Koan feature in one upstream area.

**Is it worth it?** Push works in Koan, so the keys don't settle it; the cost does. What users get: a profile's web
mail, calendar or chat notifies while its tab is closed. What they have since part 1: the same notifications while
the tab is open, which is how most people use those apps on a desktop, plus the site panel's note. Recommendation:
**keep push refused in profiles for now** (the site panel note and USAGE.md say so), the clean-up bug above is fixed on
its own (#597's H1, which protects default-partition subscriptions today and must change with it), and do push per profile as its own issue
only if users ask, with the app-identifier change designed to stay readable by upstream's parser (a separate pref
for Koan's partition per app id, so an old Koan or an upstream reset loses only the profile subscriptions).

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
- **Notifications** (#478, #530): `ContainerPermissionsTest` and `PartitionNotificationIdTest`
  (`koan_containers_unittests`: the hook, and upstream's check around it; the id with the partition); browser tests
  `KoanSpaceNotificationsBrowserTest.*` (a page in a Space's profile gets the prompt and shows a notification naming
  its Space; a default tab still does, and the answer is shared; a click shows the tab in its Space; a service
  worker's notification in profile A reaches A's worker only, not Default's or profile B's, with its own
  `getNotifications()`; `openWindow()` opens in A's Space and `focus()` shows it; a click or close for a gone profile finds
  nothing and creates no partition; a listed profile's partition is loaded for its notification's events; push is refused; the site panel's note); Mac steps in `scripts/mac/steps/space_notifications.md`.
- **Push** (#596): browser tests `KoanContainerPushBrowserTest.*` (a worker with the default subscription's origin
  and registration id, unregistered in a Space's profile or cleared with an agent container, leaves the subscription;
  the default partition's own unregistration still unsubscribes it).
- **e2e** `scripts/e2e/space_containers.mjs` (seeded `koan.workspaces.list` and containers): the
  reviewer's check with a local page.
  - Space A logs in (a cookie plus a localStorage theme). Space B (its own container) sees
    neither. Space C (A's container) sees both. Space D (none) sees neither.
  - Logging out in A leaves B's own login.
  - After a restart, A is still logged in.
  - Nothing fatal is logged.

## Essentials per profile (#479)

Zen 1.24b, `zen.workspaces.separate-essentials` (**true** in the shipped
defaults; the Settings checkbox "Enable container-specific essentials" sits in
the Pinned Tabs group, `zenTabsManagement.inc.xhtml`):

- `ZenSpaceManager.getEssentialsSection(container)`: one
  `.zen-essentials-container` per userContextId; it is hidden unless the
  active workspace's `containerTabId` is its container. A tab goes into the
  section of its own `usercontextid`.
- `ZenPinnedTabManager.canEssentialBeAdded()`: refused when the tab's
  container isn't the active workspace's, or when `_numZenEssentials` (every
  Essential of the window, all containers) is at `zen.tabs.essentials.max`.
  The context menu item is disabled then, and a drag over the grid isn't
  offered. Replicated adds (window sync) skip the check.
- `switchTabIfNeeded()`: selecting an Essential of another container switches
  to the first workspace with that container.
- `shouldAnimateEssentials`: with the setting on, the switch animation moves
  each section with its workspaces: a section stays when both workspaces
  share it, slides out or in when only one has it (also under a swipe).
- Zen reads the pref once at startup (`containerSpecificEssentials`).

**Koan** (`koan_essentials_profiles.h`, `containers::EssentialsProfiles`):

- `koan.workspaces.separate_essentials`, default on, row "Enable
  profile-specific Essentials" in Settings > Pinned Tabs, where Zen has it
  (the issue named Workspaces; Zen's source puts it with the pinned tabs).
  Applies at once in every window (a pref observer schedules each window's
  update).
- An Essential's profile is `ContainerOf()` its tab. Its grid is that
  profile's when a Space of the window has it, else the default one's.
  Zen hides an Essential whose container no workspace has (it becomes
  unreachable, zen-browser/desktop#8199); Koan shows it in the Spaces without
  a profile.
- The grid's layout leaves the hidden tiles out of the children it places
  (the hook `GetEssentialsLayoutChildren()`, unchanged) and adds them as
  hidden child layouts (`AddHiddenEssentialsLayouts()`, 3 new lines in the
  same patch). On a change of the grid shown the controller snaps the grid
  (`ResetViewsToTargetLayout()`), so no tile grows in from its hidden bounds.
- **Decision: refuse another profile's tab** (Zen's way) rather than adding
  it to its own profile's grid, where it would vanish from view at once:
  "Add to Essentials" is disabled (menu, command palette), a drag over the
  grid shows no tile, and a drop there goes back with a toast ("Only tabs of
  this Space's profile can be its Essentials"). Zen has no toast.
- **Decision: the limit of 12 per profile**, as the issue asks. Zen counts
  every Essential of the window; per profile is what the grid shows, and
  the badge ("3 / 12") is the tab's profile's.
- Imported Essentials (#363) open in the default partition, so they land in
  the default grid and count there.
- ⌘1-⌘9 (`IsTabSelectable`) and Ctrl+Tab (`SelectRelativeTab`, Koan's
  `CycleTabs()` and recently-used walk) pass over hidden Essentials
  (`koan::IsHiddenTab()`, which the two hunks call instead of
  `IsEmptyTab()`).
- An Essential of a hidden grid that becomes active (Switch to Tab, an
  extension, a restored selection) shows the first Space of its grid
  (`ShowActiveEssential()`, Zen's `switchTabIfNeeded()`). One hidden by a
  change of its Space's profile gives way to the Space's last active tab.
- The switch animation adds the pinned scroll view to the sliding views when
  the two Spaces' grids differ; a swipe moves it with the fingers only
  toward a Space with another grid.
- Window sync keeps each placeholder in its tab's container, and every window
  picks its own grid from its own active Space. Session restore keeps
  `koan.container`, so an Essential comes back in its profile.
- Limits: with a profile on every Space, the default grid shows nowhere. A
  hidden split-view Essential still counts toward upstream's tile width
  (`contains_split`).

## Managing profiles (#477)

Date: 2026-11-27. Settings > Tab Management > **Profiles**: rename, colour, Clear Data, Delete,
and Clear browsing data across profiles. Chromium 158 pristine, Zen 1.24b, Firefox's containers
(Zen keeps Firefox's container settings), Brave's containers (behaviour only).

### What upstream offers

- `BrowserContext::AsyncObliterateStoragePartition(domain, on_gc_required, done)`
  (`storage_partition_impl_map.cc` `AsyncObliterate`): `ClearData` (all but the shader cache) on
  every *loaded* partition of the domain, then a best-effort delete of `Storage/ext/<domain>`
  except the paths of loaded partitions; `on_gc_required` if something was kept. A loaded partition
  is never unloaded before the profile goes (`browser_context.h`), so the folder of a profile used
  this session can only go at the next start.
- `GarbageCollectStoragePartitionsCommand`: at startup, when
  `prefs::kShouldGarbageCollectStoragePartitions` is set, deletes every folder under
  `Storage/ext/` that isn't an extension's, an IWA's or loaded. The C5 hook loads the listed
  containers first (`LoadSavedContainers()`), so a profile out of the list is collected.
- `StoragePartition::ClearData(mask, StorageKey(), begin, end)` + `ClearCodeCaches` + the
  network context's `ClearHttpCache` / `ClearHttpAuthCache`: everything one partition holds.
- `BrowsingDataRemover` with a `BrowsingDataFilterBuilder` whose `SetStoragePartitionConfig()`
  is set works on that partition only, for `DATA_TYPE_ON_STORAGE_PARTITION` types. Upstream uses
  exactly that for isolated web apps in `ChromeBrowsingDataRemoverDelegate::RemoveEmbedderData()`
  (one nested `RemoveWithFilter()` per IWA partition when the removal named none). But the
  delegate also clears profile-wide things on a cookies removal (federated site settings, media
  device salts, origin trial tokens, the new tab page's storage in the *default* partition), so it
  is right for Clear browsing data (which clears those anyway) and wrong for clearing one profile.

### What Zen and Firefox do

- Zen observes `contextual-identity-deleted` and sets every workspace with that
  `containerTabId` back to 0 (`ZenSpaceManager.mjs` ~177). Containers are managed in Firefox's
  settings (Containers: name, colour, icon; Remove).
- Firefox's Remove asks "If you remove this Container now, N container tabs will be closed",
  closes them and clears the container's data (`clearData` by `userContextId`). Clear data /
  Clear recent history clears every container.
- Brave loops Clear browsing data over its containers and deletes orphaned container folders
  itself.

### Decisions

| Question | Decision |
|---|---|
| Open tabs on delete | **Closed**, as Firefox does, after the question names how many (the Spaces' hidden empty tabs aren't counted). `CloseWebContentsAt(CLOSE_NONE)`: no Recently Closed entry. A Space left without tabs gets its empty tab back, in the default partition (Koan keeps empty Spaces). A page with a "Leave site?" handler can keep its tab; its data is deleted anyway and the tab is in a partition no list names. |
| The Spaces that used it | Back to **Default** in every window, first (Zen's rule), while they still read it. After that, a Space reads a container that isn't listed as Default (`SpaceContainerIfListed()` in `ReadSpace()` and the stored-Spaces parse), so whatever still names it (a reopened window, `koan.workspaces.list`, a synced record) is Default too, and no new tab can recreate the partition. |
| A deleted profile's tabs restored later (Recently Closed, a session from before a crash, the stored pinned tabs) | `SiteInstanceForRestoredTab()` asks a check the browser side installs (`SetUserContainerCheck()`, from every window with Spaces before its tabs restore): unlisted → no SiteInstance → the restore hook blanks the tab and closes it. The partition is never made again. |
| Removing the data, also after a crash | Order: Spaces reset → out of `koan.containers.user` → tabs close → `kShouldGarbageCollectStoragePartitions` set and **committed to disk** → `ClearData` (+ HTTP, code and auth caches) on the partition if loaded → `AsyncObliterateStoragePartition`. A crash before the commit leaves the profile listed and whole (nothing half-deleted); after it, the next start's collection deletes the folder, since nothing loads or lists it. |
| Clear this profile's data | Partition-only APIs (above), never the browsing data remover, so nothing of another profile or of the profile-wide data is touched. Its tabs stay open. |
| Clear browsing data across profiles | **The IWA path**: a tiny hook after the IWA block in `RemoveEmbedderData()` calls `koan::containers::RemoveUserContainersData()`, which queues one `RemoveWithFilter()` per listed profile with the same time range, `remove_mask & DATA_TYPE_ON_STORAGE_PARTITION`, origin types, and a `Copy()` of the filter scoped to the profile's partition. So the dialog's time range, Forget About This Site's one site (a `kDelete` filter for its registrable domain) and the browsingData extension API all cover the profiles. Agent containers are not included (they are cleared when their task ends, and deleted in Agent Control). |
| Its own checkbox in the dialog? | **No**, as Firefox has none: with profiles, the dialog's "Cookies and other site data" and "Cached images and files" read "…, in every profile" (strings overridden in `ExtendSettingsUI()`, no patch). |
| Forget About This Site | Since #474 (history per profile) the Library acts on the profile its Which profile? filter shows, or every profile with All profiles, and names each partition in its removals itself. A removal that names no partition reaches every profile through the hook, so with Default shown the default partition's own types go in a removal that names the default partition, and the profile-wide types (site settings, engagement) in one with only those, which the hook leaves alone. |
| What else a profile holds (merged after this was designed) | Delete also covers them. **History** (#474, `visits.app_id`): `ForgetSpaceProfileHistory()` runs the Library's one-profile path (`DeleteProfileHistory()`: a `HistoryDBTask` with `RemoveVisits()`, never an all-time `ExpireHistoryBetween()`), which also forgets the profile's tabs in **Close tabs I haven't used for**'s list (#482, by the container in their session data). The history database writes a removal some seconds later, so the profile stays in `koan.containers.deleted_history`, written with the list, until a start (`ResumeForgettingSpaceProfiles()`, from the first window) finds none of its visits; an id that isn't an unlisted user container is dropped, never read as the default profile's (whose app_id is empty). Every history deletion makes Chromium delete the last session (`BrowsingDataHistoryObserverService` → `RemoveNavigationEntries()` → `SessionService::DeleteLastSession()`, crbug.com/40063610), so that start waits until the session is restored when it is restoring or the last run didn't end cleanly; the first build deleted the visits before the crash restore read the session and lost the whole session (found on the Linux build of d7c3384d). **Extension list** (#473): `extensions_off` in the profile's entry, read from the pref with no cache, so it goes with the entry. **Essentials** (#510): pinned tabs in its partition, closed with its tabs; the question names them and its pinned tabs. **Notifications** (#530): those showing close (`CloseSpaceProfileNotifications()`: a service worker's by the partition in its id, a page's by the container recorded when it showed); a click on one left elsewhere is dropped (`LoadNotificationPartition()`). **Empty tabs** (#561): resetting the Spaces moves them into Default at once. |
| Rename / colour live | The list is a pref; the Set Profile menu reads it when it opens, the colour line when it paints, and every window repaints its container tabs and updates its Spaces on a change (`OnUserContainersChanged()`); the Create-a-Space form observes it too. Names are refused as New Profile… refuses them; colours are Firefox's eight. |
| Size on disk | `ComputeDirectorySize()` of each profile's partition path, off the UI thread. Getting the path loads the partition (as `LoadSavedContainers()` does). |
| Per-profile settings (#445 and its neighbours: a proxy, extensions, developer settings) | The proxy (#445) and the extension list (#473) are keys of the profile's entry, so they go with Delete. Their rows in Settings' profile row are a follow-up (`KoanProfilesHandler::Profiles()` and `renderProfile()`); today they are set from the Space's menu and a switch. |
| Hooks | One new patch, `chrome_browsing_data_remover_delegate.cc` (5 lines) + its `BUILD.gn` (1 line). Everything else is Koan's: the settings plugin, prefs, the partition core's check, the workspace controller. |

### Tests

- Unit (`koan_containers_unittests`): `UserContainersTest.RenameAndRecolour`,
  `UserContainersTest.RemoveAndListedSpaceContainer`,
  `ContainerRestoreTest.DeletedProfilesTabsDontComeBack`.
- Browser (`KoanSpaceProfilesBrowserTest`): rename and colour reach two windows; delete resets
  the Spaces in two windows, closes its tabs and keeps the other profile's and the default's
  cookies; Recently Closed doesn't bring it back; Clear Data clears only it; Clear browsing data
  and a one-site removal clear the profiles; after a restart its folder is gone; delete forgets
  its visits, auto-closed tabs and extension list and keeps the others'; its empty tabs are in
  Default at once and the question counts its Essentials; its notifications close; visits a
  crash kept go at the next start (`PRE_PRE_`/`PRE_`/`CrashKeptVisitsGoAtTheNextStart`).
- `scripts/tests/test_settings_shell.py`: the page's messages are the handler's.
