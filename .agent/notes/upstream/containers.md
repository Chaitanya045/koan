# Upstream audit: containers (storage partitions in normal tabs), for issue #180 and #111

Date: 2026-11-10. Chromium 158.0.8127.2 (HEAD 681cd621e82e, pristine: `git show HEAD:`/`git grep HEAD` only; nothing
built or run). Cited from `.agent/notes/agent-control.md` §7 and §14 as E§n. Written from three read-only research
passes (Chromium source; Brave's Containers in brave-core; Zen 1.24b), edited by the implementer. Brave and Zen are
MPL-2.0: they were read for design and behaviour only and nothing of their code is in Koan.

## Decisions (lock / override / reuse)

| Upstream piece | Verdict | Lever |
|---|---|---|
| `SiteInstance::CreateForFixedStoragePartition` + `WebContents::CreateParams(profile, site_instance)` | **reuse** | the container tab primitive (E§1, E§8) |
| Fixed partition carried across navigations (E§2) | **reuse** | none needed; T-cont-2 guards it |
| `GetStoragePartitionConfigForSite` (E§3) | **lock** | not used: it is URL-keyed |
| `CreateForGuest` | **lock** | never used for tabs |
| noopener popups in `WebContentsImpl::CreateNewWindow` (E§4) | **override** | C1 |
| `CreateTargetContents` (E§4) | **override** | C2 |
| `CreateRestoredTab` (E§5) | **override without a patch** for agent tabs (blanked by `TabRestoreHook`); a C-row for #111 | `koan_session_data.h` |
| `TabLifecycleUnit::FinishDiscard` (E§6) | **override** | C3 (or `kWebContentsDiscard` later) |
| Prerender (E§6) | **override** | C4 |
| `GarbageCollectStoragePartitionsCommand` (E§1) | **override** | C5 |
| In-memory partitions live until profile shutdown (E§1) | **lock** (accepted cost) | `ClearData` at task end, a new partition per task |
| `AsyncObliterateStoragePartition(domain)` (E§1) | **reuse** | deleting a Saved container: one domain per container |
| `PermissionUtil::IsPermissionBlockedInPartition` | **override** | B1 (Brave overrides it the other way) |
| WebUSB/HID/Serial refused in non-default partitions | **reuse** | T-block-2 guards it |
| DevTools browser-target `Storage.*Cookies` reads only the default partition (E§7) | **lock** | agent policy denies cookies anyway (§4.3) |
| chrome:// and chrome-extension:// typed into a container tab load inside the container (E§2) | **lock** for now | agents cannot navigate there (`IsAllowedNavigationUrl`); the registry refuses WebUI targets; UNVERIFIED for the user typing them (T-cont-2 notes it) |

## What Brave does (brave-core master, 2026-11-10; behaviour only)

* **Identity.** One partition domain (`containers`) for all containers, the container's UUID as the partition name,
  so a container's path is `Storage/ext/containers/<hash>`; temporary containers are UUIDs with a `t-` prefix. Metadata
  in prefs (a synced list of name/icon/colour, and a local "used" snapshot so tabs of a container removed by sync still
  work). A tab's container is read from its SiteInstance's partition; there is no tab-to-container map.
* **Creation.** A new optional partition config on `NavigateParams` and on `tab_util::GetSiteInstanceForNewTab`; the
  hook is `CreateTargetContents`, which takes, in order, an explicit config, the source tab's partition, then the "new
  tabs open in" setting.
* **Keeping it.** noopener: a patch to `WebContentsImpl::CreateNewWindow` behind a new `ContentBrowserClient` hook.
  Ctrl-click and context menu: inherit through `source_site_instance` in `CreateTargetContents`. Discard: the
  container id is kept as user data on the replacement stub. Drag, duplicate: nothing needed.
* **Session.** The container is encoded into the restored URL itself (`containers+<uuid>:https://…`, on the virtual URL
  and inside the page state) so that a build without containers shows a blank page instead of loading the site in the
  default partition. That takes overrides in `SerializedNavigationEntry`, the session builder and driver, `PageState`
  and `NavigationEntry`; restore picks the SiteInstance from the selected entry, and skips WebUI URLs.
* **Permissions.** Brave makes `IsPermissionBlockedInPartition` return false for its containers, so permissions are
  profile-wide (upstream silently denied them); WebHID/USB/Serial stay blocked as upstream has them.
* **Other.** Clearing browsing data also loops over used containers; temporary-container tabs are never synced; zoom is
  shared with the default partition; Brave deletes orphaned container folders itself, because Chromium never does.
  Brave blocks nothing for extensions, passwords, autofill, history or downloads: its containers are for the user.

**What Koan takes:** the creation hook in `CreateTargetContents` (C2), the noopener patch (C1, but generic: it needs no
`ContentBrowserClient` method), discard (C3, by giving the replacement the old partition rather than user data),
partition = source of truth. **What Koan does differently:** one domain per container (so deleting one is one
upstream call), the agent kind in the domain (so the blocks need no service), the session marker in Koan's per-tab
session data rather than in the URL (agent tabs are never restored, so the fail-safe Brave buys with the URL encoding
is the restore hook's job, and it fails closed), and the blocks for agent containers.

## What Zen does (1.24b): containers per workspace (for #111)

* A workspace stores one `containerTabId` (Firefox's userContextId, 0 = none), persisted with the workspaces in the
  session file. A new workspace takes the selected tab's container; deleting a container resets the workspaces that
  used it to 0.
* New tabs that name no container, and every link from another app, open in the active workspace's container; those
  tabs are marked "default container" (and kept marked through restore, Glance, folders and pinning). A tab that asked
  for another container keeps it and stays in the active workspace unless `zen.workspaces.force-container-workspace`
  (false) is on and exactly one workspace has that container, in which case Zen moves the tab there and switches.
* Moving a tab between workspaces never changes its container (Firefox cannot either). Links and popups follow
  Firefox's opener inheritance; Glance opens in the current tab's container.
* Essentials are per container (`zen.workspaces.separate-essentials`, true): only the active workspace's container's
  Essentials show.
* The workspace menu has a container submenu (hidden in private windows or with no containers); the container colour
  line is hidden on default-container tabs (`zen.workspaces.hide-default-container-indicator`, true); Firefox's four
  built-in containers are not created (`zen.containers.create-default-identities`, false).

Koan's #111 reuses the core: `ContainerKind::kUser`, the same partition helpers, C1-C5 and the restore hook (plus the
restore-into-container row), and a Space's default container; none of the agent blocks apply.
Built in #318: `.agent/notes/upstream/space-containers.md` (note: 1.24b's shipped `separate-essentials` is on, and
`force-container-workspace` off).


HEAD = 681cd621e82e (pristine). All refs `path:line` at HEAD. "VERIFIED" = read in source; "UNVERIFIED" = inferred, not traced to the end.

## Summary
- The primitive Koan needs exists: `SiteInstance::CreateForFixedStoragePartition(ctx, url, config)` + `WebContents::CreateParams(ctx, site_instance)`. It sets a per-BrowsingInstance "fixed partition" bit, and **every navigation in that WebContents stays in the partition** (renderer- or browser-initiated, omnibox, cross-site, BI swaps, chrome://, about:blank, subframes).
- What does NOT stay without a patch: noopener popups / `target=_blank` (implicit noopener), ctrl/middle-click, context-menu "Open link in new tab", Cmd+T/split "new tab", session restore, tab restore, **tab discard on desktop** (Mac uses the replace-WebContents path), and prerender (omnibox/speculation rules). Same-WebContents moves (drag, detach, move to new window, split of an existing tab) and `Clone()` (Duplicate, back/forward into new tab) keep it.
- **Data loss risk**: Chrome's web-app GC command deletes everything under `<profile>/Storage/ext/` that isn't an extension or IWA path. Koan's persistent container paths have to be added to that allowlist.
- Closest precedent: the Android "MayLaunchUrl separate storage partition" tab (`chrome/browser/android/content/web_contents_factory.cc:43-80`). IWA does not use a fixed partition. It is URL-keyed through `GetStoragePartitionConfigForSite`.

## E§1. Core APIs
- `content/public/browser/storage_partition_config.h`
  - `:32 static StoragePartitionConfig CreateDefault(BrowserContext*)`. The domain is empty; it is in-memory when the context is OTR.
  - `:44 static StoragePartitionConfig Create(BrowserContext*, std::string_view partition_domain, std::string_view partition_name, bool in_memory)`. The domain must not be empty (CHECK at `content/public/browser/storage_partition_config.cc:37`). The header says the domain is `[a-z]*`. OTR forces in_memory (`.cc:39`).
  - `:49-55` accessors `partition_domain()`, `partition_name()`, `in_memory()`, `is_default()` (true when the domain is empty).
  - `:62-81` `enum class FallbackMode {kNone, kFallbackPartitionOnDisk, kFallbackPartitionInMemory}` and `set_fallback_to_partition_domain_for_blob_urls()`. This only lets blob URLs resolve from the (domain, "") partition. Koan should leave it at kNone.
- `content/public/browser/site_instance.h`
  - `:248 static scoped_refptr<SiteInstance> CreateForGuest(BrowserContext*, const StoragePartitionConfig&)`. This makes a guest SiteInfo with is_guest=true and fixed=true (`site_instance_impl.cc:227-243`). It is for `<webview>` only, and many guest-only code paths key on it. **Don't use it.**
  - `:255 static scoped_refptr<SiteInstance> CreateForFixedStoragePartition(BrowserContext*, const GURL& url, const StoragePartitionConfig&)`. Impl `site_instance_impl.cc:1060` -> `:247-260`: CHECK(!config.is_default()), then `CreateForUrlInfo(..., UrlInfo.WithStoragePartitionConfig(config), is_guest=false, is_fenced=false, is_fixed_storage_partition=true)`. `url` may be `GURL()` (Android does this).
  - The flag lives on the BrowsingInstance (`browsing_instance.h:136 is_fixed_storage_partition()`; it cannot change). It is read through `SiteInstanceImpl::IsFixedStoragePartition()` (`site_instance_impl.cc:1120`). This is **content-internal**: the public `SiteInstance` has no getter. From chrome/ use `si->GetSecurityPrincipal().GetStoragePartitionConfig()` (`security_principal.h:72`) and compare it to Koan's container domain.
- `content/public/browser/browser_context.h`
  - `:160 StoragePartition* GetStoragePartition(SiteInstance*, bool can_create = true)`
  - `:166 StoragePartition* GetStoragePartition(const StoragePartitionConfig&, bool can_create = true)`. Impl `browser_context.cc:144`. It CHECKs in_memory on OTR, then calls `StoragePartitionImplMap::Get` (`storage_partition_impl_map.cc:326-367`), which creates the partition lazily.
  - `:183-187` comment: "StoragePartitions will not be unloaded for the remainder of the BrowserContext's lifetime". **In-memory partitions are never destroyed before profile shutdown.** You can only empty them, with `StoragePartition::ClearData(...)` (`storage_partition.h:286`/`:313`). The Android precedent does exactly this in `web_contents_factory_data_deleter.cc:15-24` (`ClearData(REMOVE_DATA_MASK_ALL, StorageKey(), Time(), Time::Max(), DoNothing())` on WebContentsDestroyed). The partition object and its NetworkContext stay alive.
  - `:198 void AsyncObliterateStoragePartition(const std::string& partition_domain, OnceClosure on_gc_required, OnceClosure done)`. Impl `storage_partition_impl_map.cc:369-421`. It runs ClearData (everything except the shader cache) on loaded partitions of that domain and best-effort deletes `Storage/ext/<domain>` except paths in use. It calls on_gc_required if something couldn't be deleted. This is the "delete container" API (it works per domain, so use **one domain per container**, or accept domain granularity).
  - `:207 void GarbageCollectStoragePartitions(std::unordered_set<base::FilePath> active_paths, OnceClosure done)`. Impl `storage_partition_impl_map.cc:423-443`. It deletes **everything under `<profile>/Storage/ext/`** that isn't loaded or in `active_paths`.
  - There is no per-partition `DeletePartition`/unload API.
- On-disk path: `StoragePartitionImplMap::GetStoragePartitionPath(domain, name)` (`storage_partition_impl_map.cc:294-315`). The path is `<profile>/Storage/ext/<partition_domain>/def` when the name is empty, or `.../<hex(sha256(name)[0:6])>` (kPartitionNameHashBytes=6, `:109`). in_memory is **not** part of the path (TODO at `:302`). `StoragePartition::GetPath()` returns it (`storage_partition.h:99`).
- **GC hazard (VERIFIED)**: `web_app::GarbageCollectStoragePartitionsCommand::DoGarbageCollection` (`chrome/browser/web_applications/commands/garbage_collect_storage_partitions_command.cc:64-86`) builds its allowlist from extension and IWA paths only, then calls `profile_->GarbageCollectStoragePartitions(allowlist, ...)`. It runs at startup when pref `kShouldGarbageCollectStoragePartitions` is set (`web_app_provider.cc:553-556`), and `extensions/data_deleter.cc:83-87` sets that pref after an extension/app uninstall couldn't fully obliterate. **Hook: merge Koan container paths into `allowlist` at `:77`**, or have Koan's partitions loaded before then (UNVERIFIED ordering, so don't rely on it).

## E§2. How navigations keep the partition
- Every non-guest SiteInfo picks its partition in `SiteInfo::Create` (`content/browser/site_info.cc:286`). It uses `url_info.storage_partition_config` when set (`:305`). Otherwise it falls back to `GetStoragePartitionConfigForUrl` -> `ContentBrowserClient::GetStoragePartitionConfigForSite` (`:364-366`, `:894-903`).
- Places that set the fixed config so the embedder callback is never reached:
  1. `NavigationRequest::GetUrlInfo()` (`navigation_request.cc:4813`). At `:4876-4882`: if the current SI `IsFixedStoragePartition()`, then `url_info_init.WithStoragePartitionConfig(current partition)`. Subframes always inherit the parent's partition (`:4884-4891`).
  2. `SiteInstanceImpl::DeriveSiteInfo` (`site_instance_impl.cc:775-810`). Related lookups go through the BrowsingInstance, which injects its own `storage_partition_config_` into the UrlInfo (`browsing_instance.cc:268-281`; `:177-190` CHECKs one partition per BI). For unrelated lookups, `:803-807` forces the fixed config.
  3. `RenderFrameHostManager::ConvertToSiteInstance` (`render_frame_host_manager.cc:3939`). At `:4019-4026` (comment "e.g. <webview> tags"): for a new **unrelated** SI (any BI swap: cross-site browser-initiated, COOP, security swap), it copies the current partition into dest_url_info. `:4039-4043` creates it with `CreateForUrlInfo(..., IsGuest(), is_fenced, IsFixedStoragePartition())`, so the fixed bit carries over into the new BI.
  4. `ShouldSwapBrowsingInstancesForNavigation` (`:2737`). It forces a security BI swap if the partition would change (`:2894-2899`, via `DoesNavigationChangeStoragePartition` `:231-244`). With a fixed SI, DeriveSiteInfo already pins the config, so this never fires spuriously.
- Results (VERIFIED by code path):
  - Renderer-initiated same-tab navigations stay.
  - Browser-initiated navigations stay. Omnibox Enter in the current tab goes through `Navigate()` (`chrome/browser/ui/navigator/browser_navigator.cc:1028-1036`, `LoadURLWithParams` on the existing contents), then hits points 1 and 3.
  - Reload and history navigations stay.
  - about:blank stays. It keeps the initiator's/current SI (`render_frame_host_manager.cc:202-204`, `:3019`), so it is in the same BI.
  - chrome:// WebUI and chrome-extension:// typed into a container tab **also load inside the container partition**, because the forced config skips Chrome's override (e.g. the extension partition). I found no CHECK against WebUI in a non-default partition: grep of `is_default()` in content/browser/webui, child_process_security_policy_impl.cc, render_process_host_impl.cc and chrome_web_ui_controller_factory.cc finds nothing. `process_lock.cc:283` only formats the lock string. UNVERIFIED at runtime: WebUI behaviour in that process (e.g. NTP, settings). Recommendation: in Koan's omnibox/Navigate hook, send `chrome://`, `chrome-extension://` and `isolated-app://` to a default-partition tab.
  - `navigation_request.cc:6868` excludes non-default partitions from the SQL disk-cache NIK path. This is a perf-only difference.

## E§3. `GetStoragePartitionConfigForSite`
- Declared at `content/public/browser/content_browser_client.h:1192` as `virtual StoragePartitionConfig GetStoragePartitionConfigForSite(BrowserContext*, const GURL& site)`. The default is at `content_browser_client.cc:796`.
- Chrome's override is at `chrome/browser/chrome_content_browser_client.cc:1817-1858`. It returns the extension partition for `chrome-extension://` (`:1835-1841`), the IWA partition when `ShouldUrlUseApplicationIsolationLevel` holds (`:1844-1855`, `IsolatedWebAppUrlInfo::storage_partition_config`), and the default otherwise.
- When it is called: only from `SiteInfo::GetStoragePartitionConfigForUrl` (`site_info.cc:894`), i.e. when the UrlInfo has no config. Its other caller, `DownloadManagerImpl::GetStoragePartitionConfigForSiteUrl` (`download_manager_impl.cc:653`), serves legacy history migration.
- It does **not** override a fixed partition. It is never consulted while the BI is fixed (section 2). Koan must not try to implement containers through this hook: it is URL-keyed, and every site would land in the same partition.

## E§4. New windows and tabs
| Case | Path | Stays? | Hook |
|---|---|---|---|
| `window.open` without noopener | Blink `CreateNewWindow` -> `WebContentsImpl::CreateNewWindow` (`web_contents_impl.cc:5698`). `site_instance = source_site_instance` (`:5790-5792`), so it is the same BI. The new contents is inserted through Browser AddNewContents -> Navigate `contents_to_insert` (`browser_navigator.cc:981`). | YES | none |
| `window.open(..., 'noopener')`, `rel=noopener`, **`target=_blank` (implicit noopener)** | `web_contents_impl.cc:5780-5789`: guests get `CreateForGuest(partition_config)`, everyone else gets `SiteInstance::Create(GetBrowserContext())`, which is **default** | **NO** | Patch `:5780-5789`: when `source_site_instance->IsFixedStoragePartition()`, use `SiteInstanceImpl::CreateForFixedStoragePartition(ctx, params.target_url, partition_config)`. The session-storage map is already keyed by the opener's `partition_config` (`:5856`). Alternative: chrome-side `IsWebContentsCreationOverridden`/`CreateCustomWebContents` (`:5744-5760`). |
| ctrl/middle/shift-click, any non-CURRENT_TAB policy | `RenderFrameImpl::BeginNavigation` -> `OpenURL` (`content/renderer/render_frame_impl.cc:5857`) -> `Navigator::RequestOpenURL` (`navigator.cc:1068`, `source_site_instance = current` `:1146`) -> `Browser::OpenURLFromTab` -> `Navigate` -> `CreateTargetContents` | **NO** | `CreateTargetContents` (`browser_navigator.cc:565`). At `:578-581` the SI is `params.opener ? opener SI : tab_util::GetSiteInstanceForNewTab(profile, url)` (`chrome/browser/tab_contents/tab_util.cc:22`, which uses Create/CreateForURL, i.e. default). Add: if `params.source_site_instance` (or `source_contents`'s SI) has a Koan container config, use `CreateForFixedStoragePartition`. |
| Context menu "Open link in new tab/window" | `RenderViewContextMenu::OpenURLWithExtraHeaders` (`render_view_context_menu.cc:3901`, OpenURL at `:3963/3968`). `open_url_params.source_site_instance = site_instance_` (`components/renderer_context_menu/render_view_context_menu_base.cc:549`). Then Navigate -> CreateTargetContents. | **NO** | same `CreateTargetContents` hook ("Open link in incognito/profile" must stay out). `NavigateParams::source_site_instance` is `browser_navigator_params.h:336`; LoadURLParams copies it (`browser_navigator_params_utils.cc:45`) only as the initiator. It does not choose the partition. |
| Cmd+T, `NewSplitTab` (`browser_commands.cc:1797` -> `AddTabAt` `:1809`), omnibox Alt+Enter, extensions `tabs.create` | Navigate -> CreateTargetContents | NO (by design) | Koan "new tab in container" = build `WebContents::Create(CreateParams(profile, CreateForFixedStoragePartition(...)))` and pass `NavigateParams.contents_to_insert` (`browser_navigator_params.h:152`), or hook CreateTargetContents. |
| Cross-profile guard | `browser_navigator.cc:943-948` clears `source_site_instance`/opener when the profiles differ | n/a | keep the hook after this |

The Navigate hook does not cover noopener (it is created in content). Both hooks are needed.

## E§5. Session restore / tab restore
- `components/sessions`: grep of `partition` in `serialized_navigation_entry.h`, `content_serialized_navigation_builder.cc`, `session_types.h`, `tab_restore_types.h`, `session_service_commands.cc` finds **nothing**. Navigations restore as `NavigationEntry`s without SiteInstances.
- `chrome::CreateRestoredTab` (`chrome/browser/ui/browser_tabrestore.cc:54`). At `:74` it uses `WebContents::CreateParams create_params(browser->GetProfile())` with **no SiteInstance**, so `WebContentsImpl::Init` creates a default one (`web_contents_impl.cc:4509-4512`). Session storage is the default-keyed map (`:71-73`, `CreateMapWithDefaultSessionStorageNamespace`). So **restored tabs go to the default partition**.
- Hook: set `create_params.site_instance = CreateForFixedStoragePartition(profile, GURL(), cfg)` at `:74`, driven by `extra_data` (already a parameter, `:63`, `std::map<std::string,std::string>`). Callers: `AddRestoredTab` `:271`, `ReplaceRestoredTab` `:298`. Those are called from `session_restore.cc:487/494/1193` and `browser_live_tab_context.cc:396/455/485`.
- Persisting the container id:
  - Session (crash/startup): `SessionService::AddTabExtraData(window_id, tab_id, const char* key, const std::string&)` (`chrome/browser/sessions/session_service.h:132`). Precedent: `send_tab_to_self_activation_tracker.cc:96`. The value lands in `SessionTab::extra_data` (`session_types.h:114`).
  - Closed-tab restore: `BrowserLiveTabContext::GetExtraDataForTab` (`browser_live_tab_context.cc:199-210`, the Glic precedent `glic::PopulateGlicExtraData`). The value lands in `tab_restore::Tab::extra_data` (`tab_restore_types.h:77`).
- Restored-entry navigation then follows section 2, so it stays once the SI is fixed (VERIFIED by path).

## E§6. Moves, duplicate, discard
- Drag between windows, `TabStripModel::DetachTabAtForInsertion` (`tab_strip_model.cc:432`), `chrome::MoveTabsToNewWindow` (`browser_commands.cc:1690`), split view of existing tabs: all move the **same WebContents**, so the partition is kept (no SI change). VERIFIED for the detach API; the drag controller was not traced line by line.
- `chrome::DuplicateTabAt` (`browser_commands.cc:385`, `contents->Clone()` at `:394`), and back/forward with a new-tab disposition (`:684`, `:698`): `WebContentsImpl::Clone()` (`web_contents_impl.cc:4434-4452`) uses `CreateParams(ctx, GetSiteInstance())`, i.e. the **same BI**, so the partition is kept. `CopyStateFrom` also clones the session-storage map per partition (`navigation_controller_impl.cc:3060-3081`).
- Discard (`TabLifecycleUnitSource::TabLifecycleUnit::Discard`, `tab_lifecycle_unit.cc:359`):
  - With `features::kWebContentsDiscard` (`content/public/common/content_features.cc:381`, **disabled by default on non-Android, so on Mac**), `FinishDiscardAndPreserveWebContents` (`:337`) keeps the WebContents (`FrameTree::Discard`, `frame_tree.cc:1281`). That would keep the partition.
  - Mac default path: `FinishDiscard` (`:270`) creates `WebContents::Create(CreateParams(profile))` with **no SiteInstance** (`:274-285`), copies state, and swaps it in with `DiscardWebContents`. On reload, the new contents' current SI is default. FrameNavigationEntries keep the old fixed SI (`frame_navigation_entry.cc:56-60`), but `CanUseDestinationInstance` / `IsSuitableForUrlInfo` (`render_frame_host_manager.cc:3836-3899`) would reject it because the partition differs (UNVERIFIED, edge path). Treat this as **lost**.
  - Hook: set `create_params.site_instance` at `:274` from the old contents' partition, or enable `kWebContentsDiscard` in Koan.
- Prerender (omnibox "direct URL input", speculation rules): `PrerenderHost` creates `SiteInstanceImpl::CreateForURL`/`Create` (`content/browser/preloading/prerender/prerender_host.cc:525-529`), i.e. a **default partition**, and activation swaps that page into the tab. There is no partition guard in content/browser/preloading (grep). **Hook: `BrowserWebContentsDelegate::IsPrerender2Supported` (`chrome/browser/ui/browser_web_contents_delegate/browser_web_contents_delegate.cc:547`). Return kPreloadingDisabled for container tabs** (checked at `prerender_host_registry.cc:604`). Prefetch/preconnect for container tabs were not audited (UNVERIFIED).

## E§7. Downloads, DevTools, CHECKs
- Downloads: there is a single `DownloadManager` per BrowserContext (`browser_context.h:142 GetDownloadManager()`). Each item records its partition. It is serialized into history as `embedder_download_data.storage_partition_config` (`download_manager_impl.cc:570-599`, reverse `:601+`), and network/resume goes through that partition's NetworkContext (`GetStoragePartitionForConfig` `:286-291`, `CreatePendingSharedURLLoaderFactory` `:399-437`). Container downloads should just work and show in the normal downloads UI. A deleted in-memory container's downloads can't resume (UNVERIFIED).
- DevTools: page targets use the frame's process partition (`network_handler.cc:1411 storage_partition_ = process_host->GetStoragePartition()`). **But browser-target `Storage.getCookies`/`setCookies`/`clearCookies`** (`storage_handler.cc:435/464/491`) go through `FindStoragePartition` -> `GetDefaultStoragePartition()` (`:949-969`), so they **never see container cookies**. The koan CLI should use page-target `Network.getCookies`/`Network.getAllCookies`, or patch FindStoragePartition.
- CHECK/DCHECK scan: `git grep 'is_default()'` over chrome/browser/ui, chrome/browser/ui/tabs, chrome/browser/tab_contents, content/browser/web_contents finds **no hits**. The relevant content CHECKs are all consistency checks, and fixed partitions satisfy them:
  - `browsing_instance.cc:186` (one partition per BI)
  - `browsing_instance.cc:272` (UrlInfo vs BI config must match)
  - `web_contents_impl.cc:5741` (session storage namespace from the opener's partition)
  - `site_instance_impl.cc:253` (no default in CreateForFixedStoragePartition)
  - `browser_context.cc:148` (OTR -> in_memory only; containers in an incognito profile must pass in_memory=true, which `Create` does automatically)

## E§8. Precedents
- **Android, real tab, fixed partition (best template)**: `JNI_WebContentsFactory_CreateWebContentsWithSeparateStoragePartitionForExperiment` (`chrome/browser/android/content/web_contents_factory.cc:43-80`): `StoragePartitionConfig::Create(profile, "MayLaunchUrlUsesSeparateStoragePartitionDomain", "1", /*in_memory=*/true)`, then `SiteInstance::CreateForFixedStoragePartition(profile, GURL(), cfg)`, then `WebContents::CreateParams(profile, si)`, then `WebContents::Create`, plus `WebContentsFactoryDataDeleter` to ClearData on destroy. This shows that a plain tab with a fixed partition is a supported content configuration.
- `<webview>`: `WebViewGuest` uses `SiteInstance::CreateForGuest` (`extensions/browser/guest_view/web_view/web_view_guest.cc:502`), as does SlimWebView (`components/guest_view/browser/slim_web_view/slim_web_view_guest.cc:574`). Content has guest-only handling: noopener (`web_contents_impl.cc:5781-5785`), no proactive BI swap (`render_frame_host_manager.cc:3041`), MPArch guest windows.
- Controlled Frame: a `<webview>` guest inside an IWA. Its partition is `IsolatedWebAppUrlInfo::GetStoragePartitionConfigForControlledFrame(ctx, name, in_memory)` (`isolated_web_app_url_info.cc:122-131`), with the same domain as the IWA.
- IWA: not a fixed partition. Its windows/tabs are created normally, and the partition comes from the URL via `ChromeContentBrowserClient::GetStoragePartitionConfigForSite` -> `IsolatedWebAppUrlInfo::storage_partition_config(ctx)` (`isolated_web_app_url_info.cc:114-120`, domain from `iwa_origin_.GetPartitionDomain()`, name "", persistent). Leaving the origin is handled by `isolated_web_app_throttle.cc` (not read in detail). Koan's containers are URL-independent, so the IWA approach doesn't transfer. What Koan can reuse from IWA is its paths in the GC allowlist (`isolation_delegate().GetIsolatedStoragePaths()`) and the obliterate-on-uninstall flow (`extensions/data_deleter.cc:143`, `AsyncObliterateStoragePartition` + `kShouldGarbageCollectStoragePartitions`).

## Patch set the research suggested (the design, §14, takes C1-C5 from it)
1. Factory: `koan::CreateContainerSiteInstance(profile, container)` -> `CreateForFixedStoragePartition(profile, url, Create(profile, "koan<id-in-a-z>", "", in_memory))`. Use one domain per container so obliterate is per container.
2. `web_contents_impl.cc:5780-5789`: noopener keeps the fixed partition.
3. `browser_navigator.cc:578`, `CreateTargetContents`: inherit from source_site_instance/source_contents, and route chrome:// etc. to the default partition.
4. `browser_tabrestore.cc:74`, plus write `extra_data` through `SessionService::AddTabExtraData` and `BrowserLiveTabContext::GetExtraDataForTab`.
5. `tab_lifecycle_unit.cc:274`, or enable kWebContentsDiscard.
6. `BrowserWebContentsDelegate::IsPrerender2Supported` (`:547`): disable prerender for container tabs.
7. `garbage_collect_storage_partitions_command.cc:77`: add container paths to the allowlist.
8. Optional: DevTools `StorageHandler::FindStoragePartition` (`:949`).
