# Extensions per Space profile (#473): design and upstream audit

Date: 2026-11-27. Chromium 158 (pristine `git show HEAD:`), Zen 1.24b. This builds on
`space-containers.md` (a Space's profile is a user container, a StoragePartition of its own) and on
block B3 of `agent-control.md` section 7.2 (the hooks that keep every extension out of agent
containers). B3 decides "all extensions or none" for a process or a tab; this decides "which
extensions", at the same places.

## What others do

- **Zen 1.24b** has nothing to copy. Extensions are the same in every workspace and every container,
  as in Firefox (`extensions` is in the shared column of Firefox's containers). The request is open
  ([zen-browser/desktop#14915](https://github.com/zen-browser/desktop/discussions/14915)); a
  community pull request for it was closed unmerged.
- **Firefox** has no per-container extension switch either. Multi-Account Containers can't stop
  another add-on's content scripts.
- **Brave** containers don't scope extensions. There is nothing in brave-core to compare with.
- **Chromium** scopes extensions per browser profile only (and per incognito via "Allow in
  Incognito", `util::IsExtensionVisibleToContext()` / `IsIncognitoEnabled()`). A
  StoragePartition has no extension setting. The per-profile checks are the model for the hooks
  below: every place upstream asks "is this extension visible to this context?" for a process, or
  "may this extension see this request/tab?", also asks Koan.

## The data

Each saved user container in `koan.containers.user` (`prefs::kUserContainers`) may have
`"extensions_off": ["<extension id>", ...]`. Missing or empty means every extension is on, as
today. The pure rule is `koan_container_extensions.{h,cc}` in `//koan/browser/containers:partition`
(unit tested): `ExtensionsOffIn()`, `IsExtensionOffIn()`, `SetExtensionOff()`. Only valid
extension ids (32 letters a-p) count; anything else in the list is ignored.

Because the list is in the profile's prefs with the container, every window and a restart agree,
and a Space that shares the profile shares the list. The list belongs to the **profile**, not to the
Space: two Spaces on profile "Work" have one list.

The hooks live in `//extensions` and `//chrome/browser/extensions`, which can depend on
`:partition` only (//base, //components/prefs, //components/user_prefs, //content/public/browser).
So `:partition` reads the list itself, through `user_prefs::UserPrefs::Get()` and its own copy of
the pref name (`kUserContainersPref`). `koan_space_extensions.cc` `static_assert`s that it equals
`prefs::kUserContainers`, so a rename fails to compile.

## Where an extension is turned off (upstream decision points)

A profile's tabs, frames, workers and renderer processes are all in its StoragePartition
(`space-containers.md`). Each hook asks for the partition of what it has (a process, a request's
process, a tab) and then for the list.

| Surface | Upstream decision point | Koan's check |
|---|---|---|
| Content scripts, `chrome.*` in pages and extension frames, programmatic injection | `RendererStartupHelper::InitializeProcess()`, `OnExtensionLoaded()`, `ActivateExtensionInProcess()`: the three places that skip an extension that isn't `IsExtensionVisibleToContext()` | the extension is never loaded into the profile's renderer processes. The renderer then refuses its content scripts (`UserScriptSet::GetInjectionForScript()`: no `ExtensionInjectionHost`) and its programmatic injections (`ScriptInjectionManager::HandleExecuteCode()`) |
| webRequest (every event, blocking or not, `extraHeaders` too, and WebSocket/WebTransport) | `WebRequestEventRouter::ListenerMatchesRequest()`, the one filter every event's listeners go through | no listener of the extension matches a request of the profile: subresources, main-frame and subframe navigations (see "Which profile a request is in") |
| declarativeNetRequest | `RulesetManager::ShouldEvaluateRulesetForRequest()` | the extension's rulesets aren't evaluated for the profile's requests, navigations included (so no block, redirect, header change, or matched-rule report) |
| Tab ids: tabs, scripting, webNavigation.getFrame/getAllFrames, pageCapture, sidePanel.open({tabId}), and the rest that look a tab up | `ExtensionTabUtil::GetTabById()` (B3's hunk), while `ExtensionFunction::RunWithValidation()` runs the extension's function | "No tab with id" for the profile's tabs, as for an agent's tab |
| The active tab, for the tabs functions given no tab id (`tabs.update`, `reload`, `goBack`/`goForward`, `getZoom`/`setZoom`, `detectLanguage`) | `GetTabsAPIDefaultWebContents()`, `TabsUpdateFunction::ComputeDefaultTabId()`, `TabsReloadFunction::Run()`, `TabsDetectLanguageFunction::Run()`: each takes `WindowController::GetActiveTab()` | "No selected tab" while a tab of the profile is the active one (`TabUnlessExtensionOff()`) |
| `tabs.query` | `TabsQueryFunction::BuildTabList()` (the empty tab's hunk) | the profile's tabs aren't listed |
| `windows.get`/`getAll`/`getCurrent`/`getLastFocused({populate: true})`, `windows.create`'s result | `BrowserExtensionWindowController::CreateTabList()` | the profile's tabs aren't in the window's `tabs` |
| Tab objects anywhere else (`action.onClicked`, `commands.onCommand`, `contextMenus.onClicked`, `tabs.create`/`update`/`move` results, MV2's `tabs.getSelected`) | `ExtensionTabUtil::GetScrubTabBehavior(contents)`, the one place that decides whether a tab object has `url`, `pendingUrl`, `title` and `favIconUrl` | scrubbed fully for a tab of the profile (defence in depth: most of these can't happen any more, below) |
| `tabs.onUpdated`, `tabs.onCreated` (the tab events with a tab object) | their will-dispatch callbacks, `WillDispatchTabUpdatedEvent()` / `WillDispatchTabCreatedEvent()` (per extension) | not sent to the extension for a tab of the profile |
| webNavigation events (all of them: onBeforeNavigate, onCommitted, onReferenceFragmentUpdated, onHistoryStateUpdated, onDOMContentLoaded, onCompleted, onErrorOccurred, onCreatedNavigationTarget, onTabReplaced) | `web_navigation_api_helpers.cc`, where all of them are built (eight helpers): `SkipExtensionsOffIn()` gives the event a will-dispatch callback (`Event::will_dispatch_callback`, upstream's per-extension point) before it is broadcast | not sent to the extension for a frame of the profile's tab |
| `tabs.captureVisibleTab` | `TabsCaptureVisibleTabFunction::GetWebContentsForID()` (a window's active tab, no tab id) | refused for a tab of the profile |
| `chrome.debugger` | `ExtensionMayAttachToWebContents()` (B3's hunk), `ExtensionMayAttachToAgentHost()` (attach by target id: the profile's workers), `DebuggerGetTargetsFunction::Run()` | attach refused; `getTargets()` doesn't list the profile's pages and workers (their URLs and titles) |
| Context menu items (`chrome.contextMenus`) | the page context menu's filter, `RenderViewContextMenu`'s `extension_items_` (upstream's `MenuItemMatchesParams()`, which `koan::ExtensionMenuItemFilter()` still calls) | no item of the extension in a page of the profile, so no `onClicked` with `pageUrl`/`linkUrl`/`selectionText`/the tab, and no activeTab from it |
| The extension's action from its keyboard shortcut (`_execute_action`) or the extensions menu | `ExtensionActionRunner::RunAction()`, the one place an action runs | nothing happens in a tab of the profile: no `action.onClicked`, popup, side panel or activeTab |
| Named keyboard commands (`chrome.commands`) | `ExtensionKeybindingRegistry::CommandExecuted()` | no `commands.onCommand` (and no activeTab) while a tab of the profile is active; the shortcut is still taken, so the page doesn't get it either. Global commands (no tab) are unchanged |
| `activeTab` | `ActiveTabPermissionGranter::GrantIfRequested()`, the one place it is granted | never granted for a tab of the profile, whatever asks |
| Toolbar button (Koan's URL pill) | Koan's own `KoanSidebarExtensionsView` | hidden while a tab of the profile is active |
| Toolbar button (Chromium's toolbar, multiple-toolbar layout) | `ExtensionsToolbarDesktop::IsActionVisibleOnToolbar()`, re-asked from `UpdateAllIcons()` (every tab switch, `ToolbarView::Update()`) | hidden while a tab of the profile is active |
| Side panel | `SidePanelUI` (public: `IsSidePanelEntryShowing()`, `Close()`, `RegisterSidePanelShown()`) | closed when it shows the extension's panel while a tab of the profile is active, and when the user switches to such a tab |

### Which profile a request is in

A navigation's request has no renderer process: `WebRequestAPI::MaybeProxyURLLoaderFactory()` makes the
proxy with `is_navigation ? -1 : render_process_id`, so `request.global_id.child_id` is not a process and
the first version of this PR, which asked that process, let every page load and iframe of the profile
through to webRequest (with its `Cookie` header under `extraHeaders`) and declarativeNetRequest (the review
of PR #515, finding 1; checked against 158's `web_request_api.cc`). Upstream has the same problem for
privileged WebContents and solves it where the request is made: `WebRequestInfoInitParams` sets
`is_privileged` from the navigation's `ExtensionNavigationUIData` or from the requesting process. Koan does
the same next to it: `koan_partition = koan::containers::PartitionOfRequest(frame tree node of the
navigation, requesting process)`, a new field of `WebRequestInfoInitParams` and `WebRequestInfo`
(`web_request_info.{h,cc}`). For a navigation the frame being navigated decides: its node's current
RenderFrameHost is in the tab's partition, also for a new tab whose renderer doesn't exist yet (the initial
RenderFrameHost), a prerendered page (its own frame tree, same WebContents) and service worker navigation
preload (`ServiceWorkerClient` passes the navigating frame, as a navigation). For anything else
(subresources, workers, WebSocket, WebTransport, prefetch, keepalive while the page lives) the requesting
process decides, as before. It is decided once, when the request is made and its frame or process is alive;
a redirect makes a new `WebRequestInfo` and decides again. The two hooks ask
`IsExtensionOffForRequest(context, request.koan_partition, id)`. What isn't covered: a request whose frame
and process are both gone when it is made (none comes from a live tab).

`ExtensionFunction` scope: `GetTabById()` has no extension parameter, and its callers (about 30)
don't pass one. Koan sets the calling extension for the time `RunWithValidation()` runs the
function (`ScopedExtensionCall`, a thread-local), and `GetTabById()` asks
`IsCallingExtensionOffIn()`. Functions that look their tab up synchronously in `Run()` are covered:
every tabs function, scripting (`CanAccessTarget()` in `Run()`), webNavigation's getters,
pageCapture, sidePanel. A function that looked a tab up only after an asynchronous step would not
be; none of the ones that reach page content does, and the renderer rule above still holds for
injection. A browser-internal `GetTabById()` (session restore, DevTools) has no calling extension and
is unchanged.

## Hard cases (decisions)

| Case | Decision |
|---|---|
| An extension turned off (or on) while the profile has open tabs | **The profile's tabs reload, in new renderer processes, when the user presses Done.** Whether an extension is loaded into a renderer is decided when the process starts (as upstream does for every extension), so a plain reload, which keeps the process, would keep the old set. Koan ends the profile's renderer processes and reloads each of its tabs once they are gone (`RestartProfileTabs()`); the processes that start load the new set, and the content script loader sends them its scripts again. The dialog says so before Done. Changes in several switches reload once. The checks that aren't per process (webRequest, declarativeNetRequest, tab ids, capture, debugger, toolbar, side panel) apply at once, before the reload. Upstream's own disable unloads an extension from running renderers; Koan can't do that per process without growing the patch into upstream's bookkeeping (`extension_process_map_`, the user script loader's private `SendUpdate()`), and a half-unloaded extension leaves what its scripts did in the page anyway. |
| Extension pages (`chrome-extension://`) | Unchanged. They open in the extension's own (default) partition (`space-containers.md` rule 2), so an extension's popup, options page and background keep working in every Space. An extension frame **inside** a page of the profile is in the profile's process, where the extension isn't loaded: it gets no extension APIs. |
| `chrome.cookies` | Unchanged: it reads and writes only the default partition (upstream). An extension off in a profile can't reach that profile's cookies through it, and an extension on there can't either. Documented. |
| An extension with host access to all sites | The switch still holds. It is Koan's block at the points above, not a permission change: the extension's permissions, its site access settings and its "Allow on all sites" stay as they are, and it keeps them in the other profiles. |
| A new extension | On everywhere, as today. A profile setting "new extensions start off here" is not added: it would need an install hook, and the default every browser has is on. Follow-up if asked. An id stays in a list after an uninstall, so an extension installed again is off where it was. |
| Extension-made tab groups (`chrome.tabs.group`) | Unchanged: a group an extension makes is a Space. Its tabs open in the default partition (Zen's add-on rule, `space-containers.md` rule 4). |
| Agent tabs | Unchanged: B3 still blocks every extension there, before Koan's per-profile check is asked. Agent containers have no list. |
| A Space with no profile | It uses the default partition, which every Space without a profile (and every window without Spaces) shares, so it can't have a list. Its Extensions... dialog lists the extensions with their switches on and explains this. Turning one off asks for a new profile's name (New Profile..., as Set Profile does); Create gives the Space that profile with the extension off. The Space's open tabs stay in the default partition, as with Set Profile; its new tabs open in the profile. Cancel leaves everything as it was. Proxy per Space (#445, PR #469) makes the same offer. |
| Tabs moved to another Space | Keep their partition (`space-containers.md`), so they keep their profile's list. The colour line already shows they are in another profile. |
| Tabs opened by a tab of the profile | Same partition (C2), so the same list. |
| Essentials | Each keeps the partition it had when pinned, and so its profile's list. |
| Incognito | No Spaces, no profiles, no lists. Upstream's "Allow in Incognito" applies. |
| A profile used by several Spaces | One list. The dialog's text names the profile, not the Space. |
| tabs.query, windows' tab lists and tab events (review finding 2) | **Left out.** The listings skip the profile's tabs (`BuildTabList()`, `CreateTabList()`), and the two events that carry a tab object, `onUpdated` and `onCreated`, are dropped per extension in their will-dispatch callbacks. `GetScrubTabBehavior()` scrubs every other tab object of the profile (no `url`, `title`, `favIconUrl`) as a second line. Chosen over scrubbing alone because a scrubbed `onUpdated` still says when the user loads a page and its `status`, and a scrubbed listing still counts and orders the tabs. B3's agent tabs still have the gap (there the user lets the agent browse). |
| Tab events with ids only | **Not sent** since #544 (they were the accepted gap of #515): `onActivated`, `onHighlighted`, `onMoved`, `onAttached`/`onDetached`, `onRemoved`, `onReplaced`, `onZoomChange` (and the deprecated `onSelectionChanged`/`onActiveChanged`, `onHighlightChanged`) for the profile's tabs. They are all built in `TabsEventRouter::DispatchEvent()`, which has no tab; each of its eight callers sets a `ScopedEventSkip` with the tab (onHighlighted: every highlighted tab, so an extension off in any of them gets none) and `DispatchEvent()` applies it ("The stores every profile shares" below). |
| webNavigation events (review finding 2) | **Dropped** for the profile's frames, per extension: every event is built in `web_navigation_api_helpers.cc`, and each gets `SkipExtensionsOffIn()` before it is broadcast (eight one-line calls; upstream's per-extension point is `Event::will_dispatch_callback`, which runs synchronously in the broadcast, so the tab is alive; the list is taken when the event is built). The getters were already blocked. |
| Context menus (review finding 3) | **No item** of the extension in a page of the profile (checked: before this, `PopulateExtensionItems()` added every extension's items, with no frame or profile check, and a click sent `onClicked(info, tab)` with `pageUrl`, `linkUrl`, `selectionText` and granted activeTab). The filter is upstream's `MenuItemMatchesParams()` plus the profile's list, taken when the menu opens. The extension's own pages (another partition) keep their items. |
| Keyboard shortcuts of an extension (`commands`, review finding 3) | **Do nothing** while a tab of the profile is active (checked: `CommandExecuted()` granted activeTab and sent the tab; `_execute_action` ran the action, which also granted activeTab and could open the popup or side panel). Named commands stop in `CommandExecuted()`, the action in `ExtensionActionRunner::RunAction()` (which also covers the extensions menu's click). The shortcut is still consumed, as Chromium does for an extension with a listener; global commands (outside the browser, no tab) are unchanged. |
| `activeTab` (review finding 3) | **Never granted** for a tab of the profile (`ActiveTabPermissionGranter::GrantIfRequested()`, the one place). With the three paths above closed this is a second line. |
| The omnibox keyword API | Harmless: `omnibox.onInputChanged`/`onInputEntered` give the text the user types after the extension's keyword, no tab. What the extension then does to "the current tab" with no tab id (`tabs.update({url})`) is refused while a tab of the profile is active (`TabUnlessExtensionOff()`), so it opens nothing in the profile. Typing its keyword is the user asking it. |
| `tabs.sendMessage`/`tabs.connect` to a tab of the profile | Harmless, not hooked: they don't go through an `ExtensionFunction` (the messaging service looks the tab up itself), but the extension isn't loaded in the profile's renderers, so no frame there listens: the call fails with "Receiving end does not exist". The extension learns no tab id to ask with (the listings and events above). Expected from the code; not on the build. |
| Profile-wide stores that aren't per partition | **Left out** since #544: history, recently closed tabs, top sites and downloads give an extension nothing of a profile it is off in ("The stores every profile shares" below; before, all of it was readable and the dialog said so). **Still visible**, on purpose: `chrome.bookmarks`, `chrome.readingList`, passwords and autofill (shared by every profile in Koan), `chrome.tabGroups` (the Spaces' names and colours), the windows (not their tabs). The dialog says: "... or read its history, closed tabs, top sites and downloads. Your bookmarks and reading list are shared by every profile, so it can still read those." |
| Extensions menu (the puzzle menu, Koan's site panel) | Still lists every extension: it is the management menu, like chrome://extensions. Clicking one that is off in the active tab's profile does nothing (`RunAction()`, above); its context menu (options, pin, remove) still works. |
| Default partition, windows without Spaces | Unchanged. |

## The stores every profile shares (#544)

History, the recently closed list, top sites and downloads are the browser profile's, shared by every Space
profile. Each entry knows its profile, so each API's results and events can leave a profile out:

- a **visit** by its tag, `visits.app_id` (#474, `space-history.md`: `HistoryProfileFromAppId()`);
- a **closed tab** by its session data, `tab_restore::Tab::extra_data[kContainerSessionKey]` (`PopulateTab()` copies
  every tab's, also in a closed window or group);
- a **download** by its partition, `DownloadItem::GetSerializedEmbedderDownloadData()` (a serialized
  `StoragePartitionConfig`, set for every download from a frame, kept for its life and in the downloads database,
  `DownloadRow::embedder_download_data`, so it holds after a restart), and its tab while it is open
  (`DownloadItemUtils::GetOriginalWebContents()`). Nothing new has to be stored;
- a **tab** by its partition, as in #515.

What an extension is off in is read from the profile's list each time (`ProfilesExtensionIsOffIn()`,
`ExtensionsOffForVisit()`, `ExtensionsOffInPartition()`, `IsExtensionOffInSessionData()` in
`koan_container_extensions.h`, unit tested). An extension that is off nowhere takes upstream's path unchanged.

Events are built in one function per API that doesn't know their subject (`TabsEventRouter::DispatchEvent()`,
`HistoryEventRouter::DispatchEvent()`, `ExtensionDownloadsEventRouter::DispatchEvent()`), called synchronously
from the places that do. Those places set a `ScopedEventSkip` (`koan_extension_events.h`: a thread-local scope, the
same pattern as `ScopedExtensionCall`), and the builder calls `SkipExtensionsInScope(event)`, which gives the event a
will-dispatch callback that skips the extensions off in the subject's profile (or, for onVisitRemoved, gives each
extension its own list). The sets are taken when the event is built, so an event queued for a lazy background page
holds no pointer.

| API | Where upstream builds it for the calling extension | Koan's hook (one call each; size in changed upstream lines) | Still visible |
|---|---|---|---|
| `history.search` | `HistorySearchFunction::Run()`: one `HistoryService::QueryHistory()` | `koan::QueryHistoryForExtension()` in place of the call (`history_api.cc`): a `HistoryDBTask` runs upstream's `HistoryBackend::QueryHistory()` once per tag the extension isn't off in (`QueryOptions::app_id`: `''` for the default profile, then every tag of `GetAllAppIds()`), keeps each page's latest visit among them, newest first, as many as asked. A page that also has hidden visits gets the count, typed count and last visit of the others (recounted from `GetVisitsForURL()` with `AddPageVisit()`'s rules: no reloads; typed by `incremented_omnibox_typed_score`) | A page visited there and elsewhere shows the URL row's **title**, which is the one last seen, maybe in the hidden profile. Visits from before the `app_id` column existed (NULL, not `''`) match no tag: such a page isn't found for an extension that is off somewhere (none in a Koan profile; imported history is `''`) |
| `history.getVisits` | `HistoryGetVisitsFunction::QueryComplete()` | `koan::EraseVisitsOffFor()` on the result's visits | Nothing |
| `history.onVisited` | `HistoryEventRouter::OnURLVisited()` → `DispatchEvent()` | a `ScopedEventSkip` of the visit's profile (`ExtensionsOffForVisit(visit_row.app_id)`), applied in `DispatchEvent()` | The visit of another profile carries the URL row's counts, which include the hidden visits |
| `history.onVisitRemoved` | `HistoryEventRouter::OnHistoryDeletions()` → `DispatchEvent()` | a `ScopedEventSkip` with `koan::RemovedPagesFilter()`: each extension gets the removed URLs without those that only had visits in profiles it is off in, by `SpaceHistoryIndex::PageProfilesOf()` (what the index knew before it reads the deletion again, 1 s later); none left: no event. With the index not read yet, no URL for an extension that is off anywhere | That something was removed, with `allHistory: true` for a clear of everything |
| `history.deleteUrl`/`deleteRange`/`deleteAll`/`addUrl` | | not hooked | An off extension can still **delete** the profile's visits (every profile's, as upstream), and add untagged visits |
| `sessions.getRecentlyClosed` | `SessionsGetRecentlyClosedFunction::Run()`'s loop over `TabRestoreService::entries()` (group entries are split into tabs there) and `CreateWindowModel()`'s tab loop | `koan::containers::HidesClosedEntry()` skips a tab of such a profile, and a window, group or split whose every tab is; a window with some keeps the others (`sessions_api.cc`) | `sessions.onChanged` (no arguments) still fires when the profile closes a tab. A window's `sessionId` and its other tabs |
| `sessions.restore` | `RestoreMostRecentlyClosed()`, `RestoreLocalSession()` | refused for an entry or tab it can't list (`HidesClosedEntry()`, `HidesClosedId()`): no restore without an id while the most recent entry is the profile's ("There are no recently closed sessions."), "Invalid session id" for its id | Restoring a mixed window still reopens its hidden tabs (the extension sees none of them afterwards) |
| `topSites.get` | `TopSitesGetFunction::Run()` → `OnMostVisitedURLsAvailable()` | the callback is wrapped by `koan::TopSitesForExtension()`: a `HistoryDBTask` drops the sites whose every visit is in a profile the extension is off in (`top_sites_api.cc`) | The ranking and the list's length still count the hidden visits, so a site visited a lot there ranks higher elsewhere |
| `downloads.search`, `erase` and every function with an id (`pause`, `resume`, `cancel`, `open`, `show`, `removeFile`, `acceptDanger`, `getFileIcon`) | `ShouldExport()`, which `RunDownloadQuery()` and `GetDownload()` ask for every item, synchronously in `RunInternal()` | `koan::containers::IsCallingExtensionOffInDownload()` (the calling extension of `ScopedExtensionCall`; `ExecuteFromQueue()`, which runs a function queued before the download manager loaded, sets the scope too) (`downloads_api.cc`) | The ids are sequential, so a gap says that a hidden download exists. If an extension's function makes the downloads system notify synchronously (e.g. `browsingData` deleting downloads), `ShouldExport()` also says no for that extension's hidden items in the events of that moment, for every extension (hides more) |
| `downloads.onCreated`, `onChanged`, `onErased`, `onDeterminingFilename` | `OnDownloadCreated()`, `OnDownloadUpdated()`, `OnDownloadRemoved()`, `OnDeterminingFilename()` → `DispatchEvent()` | a `ScopedEventSkip` of `ExtensionsOffInDownload(item)` in each, applied in `DispatchEvent()`; an off extension doesn't take part in naming the profile's downloads | `downloads.download()` started by the extension itself is in the default partition, so it sees it |
| Tab events with ids only | `TabsEventRouter::DispatchEvent()` (11 calls from 8 places) | a `ScopedEventSkip` of the tab at each place, applied in `DispatchEvent()` (`tabs_event_router.cc`) | `windows.onFocusChanged` and the other window events (the windows aren't per profile) |

Patch sizes (`scripts/ci/patch_report.py` against develop): new `history_api.cc` (24), `history/BUILD.gn` (3),
`sessions_api.cc` (28), `top_sites_api.cc` (8), `downloads_api.cc` (23); grown `tabs_event_router.cc` (11 -> 32),
`chrome/browser/extensions/BUILD.gn` (3 -> 4, `:extension_events`). +108 lines in all. The history and top sites
parts are in `koan_extension_history.{h,cc}` (`//koan/browser/ui/views`, header-only hook, defined in `:impl`)
because they need the history service and the Space history index; the rest is in `:extension_events`, whose deps
grow by `//components/download/public/common`, `//components/sessions` and `//content/public/browser`.

Not checked or not hooked, and why: `tabs.sendMessage` to the profile's tab (no frame there listens, above);
`chrome.processes` (dev channel only); `chrome.cookies` (the default partition only, above).

## Flag

`koan::features::kKoanSpaceExtensions` (`chrome://flags/#koan-space-extensions`, on by default). Off,
the Space menu has no Extensions... item, the pill's button has no context menu, and every hook
reads "on" (`IsOffIn()` in `koan_container_extensions.cc`), so the lists are kept but ignored.

## UI

- **Space menu → Extensions…** (after Set Profile): a dialog titled "Extensions in <profile>" (or
  "Extensions in this Space" for a Space without a profile), one row per installed extension that
  chrome://extensions shows (`ui_util::ShouldDisplayInExtensionSettings()`, enabled or not), its
  name and a switch. Off means off in this profile. Done saves and reloads the profile's tabs if
  anything changed; Cancel drops the changes. With no extensions installed, the dialog says so.
- **URL pill → Extensions button**: its context menu (right-click, or the context-menu key) has
  "Extensions in This Space…", the same dialog for the active Space. Its click still opens
  Chromium's extensions menu, as Zen's button opens its panel.

## Hooks

Each place is one call into `//koan`; one REBASE.md row lists them all. Sizes are changed upstream lines
(`scripts/ci/patch_report.py`), the whole PR against develop: 11 new patches and 7 grown, +174 lines.

New patches:
- `extensions/browser/api/web_request/web_request_info.h` (9) and `.cc` (9): the `koan_partition` field
  and `PartitionOfRequest()` next to `is_privileged`.
- `extensions/browser/api/web_request/extension_web_request_event_router.cc` (8): one check in
  `ListenerMatchesRequest()`.
- `extensions/browser/api/declarative_net_request/ruleset_manager.cc` (8): one check in
  `ShouldEvaluateRulesetForRequest()`.
- `extensions/browser/extension_function.cc` (5): the scope around `Run()`.
- `extensions/browser/permissions/active_tab_permission_granter.cc` (7): `GrantIfRequested()`.
- `chrome/browser/extensions/api/tabs/tabs_event_router.cc` (11): the two will-dispatch callbacks.
- `chrome/browser/extensions/api/web_navigation/web_navigation_api_helpers.cc` (19): eight
  `SkipExtensionsOffIn()` calls, one per event.
- `chrome/browser/extensions/browser_extension_window_controller.cc` (8): `CreateTabList()`.
- `chrome/browser/extensions/extension_action_runner.cc` (8): `RunAction()`.
- `chrome/browser/extensions/extension_keybinding_registry.cc` (8): `CommandExecuted()`.

Grown patches (each already a Koan hook file):
- `extensions/browser/renderer_startup_helper.cc` (13 -> 25): three `continue`/`return` conditions.
- `chrome/browser/extensions/extension_tab_util.cc` (10 -> 19): B3's `GetTabById()` hunk, and
  `GetScrubTabBehavior()`.
- `chrome/browser/extensions/api/debugger/debugger_api.cc` (9 -> 27): B3's hunk, the target-id attach,
  `getTargets()`.
- `chrome/browser/extensions/api/tabs/tabs_api.cc` (6 -> 25): the empty tab's `BuildTabList()` hunk,
  four active-tab places, captureVisibleTab.
- `chrome/browser/renderer_context_menu/render_view_context_menu.cc` (16 -> 23): the extension items'
  filter in the constructor (three upstream lines become Koan's filter, which calls upstream's
  `MenuItemMatchesParams()`).
- `chrome/browser/ui/views/extensions/extensions_toolbar_desktop.cc` (18 -> 26): one check in
  `UpdateIconVisibility()`, and `UpdateIconVisibility()` in `UpdateAllIcons()`.
- `chrome/browser/extensions/api/web_navigation/BUILD.gn` (1 -> 2): `//koan/browser/containers:extension_events`.

`:extension_events` is a new Koan target (`koan_extension_events.{h,cc}`): `SkipExtensionsOffIn()` needs
`extensions::Event`, so it can't be in `:partition`, which `//extensions/browser` depends on, and
`web_navigation` can't use `//koan/browser/ui/views`. Every other hook's target already depends on
`:partition` (or `//koan/browser/ui/views` for the toolbar and the context menu).

Upstream behaviour Koan depends on, each guarded by a test named in its REBASE row:
- An extension not loaded in a renderer gets no content script injection there (browser test
  `ContentScriptsDontRunWhereOff`).
- A process keeps its loaded extensions for life, so a change needs new processes (browser test
  `ChangeReloadsTheProfilesTabs`).

## Tests

- **Unit** (`koan_containers_unittests`, `ContainerExtensionsTest`): the list's parse (missing,
  empty, bad ids, other containers, agent ids), `SetExtensionOff()` on and off, idempotent, unknown
  container, the scope; a request's partition from its frame (navigation, no process) and from its
  process, none for the default partition or a gone frame or process (`ARequestIsInItsFramesOrProcesssPartition`);
  the list of a tab (`ListsWhatIsOffInATab`). `SkipExtensionsOffIn()` has no unit test: it needs
  `Extension` objects, which need the extensions test client this binary doesn't set up; the browser test
  covers it (webNavigation.onCommitted).
- **Browser** (`koan_space_extensions_browsertest.cc`), with a test extension that has a content
  script, a webRequest listener and a declarativeNetRequest block rule:
  - off in profile A: no content script in A's tab, no webRequest event and no DNR block for A's
    requests, `tabs.get` and `scripting.executeScript` of A's tab fail
  - still works in a Space without a profile and in profile B
  - the list is stored in `koan.containers.user` and read back
  - `NavigationsTabsAndMenusOnlyOutsideTheProfile` (the review's list): in profile A a main-frame and an
    iframe navigation reach no webRequest listener and no DNR rule, `tabs.query({})` doesn't list A's
    tab, no `tabs.onUpdated` and no `webNavigation.onCommitted` for it, `tabs.getZoom()` of the active
    tab fails, the page's context menu has no item of the extension; and all of these work in profile B
  - a change reloads the profile's tabs in new processes, and turning the extension back on brings
    its content script back
  - a Space without a profile can't have a list (its tabs are the default partition's)
  - `SharedStoresLeaveOutTheProfile` (#544): off in Bank, on in Work: history.search, getVisits and onVisited
    have none of Bank's visits (a page both visited: Work's visit only, visit count 1), downloads.search,
    search by id and onCreated none of Bank's downloads, topSites.get not Bank's page, getRecentlyClosed not
    Bank's closed tab and restore of its id fails, no onActivated and no onRemoved for Bank's tab; every one of
    them has Work's
- **By hand, Mac and Linux**: `scripts/mac/steps/space_extensions.md` (SE1-SE26), with a small unpacked
  test extension (a content script, webRequest listeners with `extraHeaders`, a DNR rule, tabs and
  webNavigation listeners, a context menu item, a keyboard command, an action and a side panel).

## The maintainer's review of PR #515 (2026-11-27)

Each finding was checked against 158's source before the change; all three hold.

1. **Navigations went past webRequest and DNR.** True: `MaybeProxyURLLoaderFactoryInternal()` passes
   `is_navigation ? -1 : render_process_id`, so `RenderProcessHost::FromID()` was null and the check said
   "not off" for every main-frame and subframe load. Fixed by deciding the partition when the request is
   made ("Which profile a request is in").
2. **The profile's tabs reached the extension.** True: `BuildTabList()`, `CreateTabList()`,
   `WillDispatchTabUpdatedEvent()`/`Created` and every webNavigation helper had no profile check. Fixed by
   leaving the tabs out and dropping the events per extension, with `GetScrubTabBehavior()` as a second
   line; the id-only tab events are still sent (Hard cases).
3. **Context menus, commands, activeTab.** All three were open: items showed (and `onClicked` gave the
   page's URL, selection and tab), a named command sent the tab and granted activeTab, `_execute_action` ran
   the action. Fixed at `extension_items_`'s filter, `CommandExecuted()`, `RunAction()` and
   `GrantIfRequested()`. The omnibox keyword API and `tabs.sendMessage` are harmless (Hard cases).

Found while doing it, not in the review: `debugger.getTargets()` listed the profile's pages and workers with
their URLs and titles, and an attach by target id to a worker of the profile wasn't refused; the tabs
functions that take the active tab when given no tab id (`tabs.update({url})` could navigate the profile's
tab); and the dialog's "can't see or change this profile's pages", which history made untrue. All closed or
reworded above.
