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
| webRequest (every event, blocking or not, and WebSocket/WebTransport) | `WebRequestEventRouter::ListenerMatchesRequest()`, the one filter every event's listeners go through | no listener of the extension matches a request whose process is in the profile |
| declarativeNetRequest | `RulesetManager::ShouldEvaluateRulesetForRequest()` | the extension's rulesets aren't evaluated for the profile's requests (so no block, redirect, header change, or matched-rule report) |
| Tab ids: tabs, scripting, webNavigation.getFrame/getAllFrames, pageCapture, sidePanel.open({tabId}), and the rest that look a tab up | `ExtensionTabUtil::GetTabById()` (B3's hunk), while `ExtensionFunction::RunWithValidation()` runs the extension's function | "No tab with id" for the profile's tabs, as for an agent's tab |
| `tabs.captureVisibleTab` | `TabsCaptureVisibleTabFunction::GetWebContentsForID()` (a window's active tab, no tab id) | refused for a tab of the profile |
| `chrome.debugger` | `ExtensionMayAttachToWebContents()` (B3's hunk) | refused |
| Toolbar button (Koan's URL pill) | Koan's own `KoanSidebarExtensionsView` | hidden while a tab of the profile is active |
| Toolbar button (Chromium's toolbar, multiple-toolbar layout) | `ExtensionsToolbarDesktop::IsActionVisibleOnToolbar()`, re-asked from `UpdateAllIcons()` (every tab switch, `ToolbarView::Update()`) | hidden while a tab of the profile is active |
| Side panel | `SidePanelUI` (public: `IsSidePanelEntryShowing()`, `Close()`, `RegisterSidePanelShown()`) | closed when it shows the extension's panel while a tab of the profile is active, and when the user switches to such a tab |

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
| tabs.query and tab events | Still list the profile's tabs, with URLs and titles to an extension with the "tabs" permission: the accepted gap B3 has (Chromium has no per-tab hide flag; the tab model is read in dozens of places). |
| webNavigation events | Still reported for the profile's tabs (the per-tab observer can't filter per extension without a hook in every dispatch). The getters are blocked. Follow-up. |
| Keyboard shortcuts of an extension (`commands`) | Still fire; the extension's action has no button there, and what it then tries on the tab is refused as above. |
| Extensions menu (the puzzle menu, Koan's site panel) | Still lists every extension: it is the management menu, like chrome://extensions. |
| Default partition, windows without Spaces | Unchanged. |

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

Grown patches (each already a Koan hook file, each with its REBASE.md row):
- `extensions/browser/renderer_startup_helper.cc`: three `continue`/`return` lines.
- `extensions/browser/api/web_request/extension_web_request_event_router.cc` (new): one check in
  `ListenerMatchesRequest()`.
- `extensions/browser/api/declarative_net_request/ruleset_manager.cc` (new): one check in
  `ShouldEvaluateRulesetForRequest()`.
- `extensions/browser/extension_function.cc` (new): the scope around `Run()`.
- `chrome/browser/extensions/extension_tab_util.cc`: one more condition in B3's hunk.
- `chrome/browser/extensions/api/debugger/debugger_api.cc`: one more condition in B3's hunk.
- `chrome/browser/extensions/api/tabs/tabs_api.cc`: one check in captureVisibleTab.
- `chrome/browser/ui/views/extensions/extensions_toolbar_desktop.cc`: one check in
  `IsActionVisibleOnToolbar()`, and `UpdateIconVisibility()` in `UpdateAllIcons()`.

No BUILD.gn change: every target already depends on `//koan/browser/containers:partition` (or
`//koan/browser/ui/views` for the toolbar).

Upstream behaviour Koan depends on, each guarded by a test named in its REBASE row:
- An extension not loaded in a renderer gets no content script injection there (browser test
  `ContentScriptsDontRunWhereOff`).
- A process keeps its loaded extensions for life, so a change needs new processes (browser test
  `ChangeReloadsTheProfilesTabs`).

## Tests

- **Unit** (`koan_containers_unittests`, `ContainerExtensionsTest`): the list's parse (missing,
  empty, bad ids, other containers, agent ids), `SetExtensionOff()` on and off, idempotent, unknown
  container, the scope.
- **Browser** (`koan_space_extensions_browsertest.cc`), with a test extension that has a content
  script, a webRequest listener and a declarativeNetRequest block rule:
  - off in profile A: no content script in A's tab, no webRequest event and no DNR block for A's
    requests, `tabs.get` and `scripting.executeScript` of A's tab fail
  - still works in a Space without a profile and in profile B
  - the list is stored in `koan.containers.user` and read back
  - a change reloads the profile's tabs in new processes, and turning the extension back on brings
    its content script back
  - a Space without a profile can't have a list (its tabs are the default partition's)
- **By hand, Mac and Linux**: `scripts/mac/steps/space_extensions.md` (SE1-SE18), with a small unpacked
  test extension (a content script, a webRequest listener, a DNR rule, an action and a side panel).
