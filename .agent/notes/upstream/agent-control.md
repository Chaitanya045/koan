# Upstream audit for agent control (issue #180)

Date: 2026-11-07. Chromium 158.0.8127.2 (`681cd621e8`), Koan `develop` aa2ef917. Read only: every citation is
pristine source (`git show HEAD:<path>`, `git grep HEAD`), nothing was built or run, so every "works on the Mac"
verdict is a reading of the code and is marked UNVERIFIED where it needs hardware. The design that uses this audit
is `.agent/notes/agent-control.md`. Verdicts: **lock** (keep upstream's behaviour), **override** (Koan changes it;
the lever is named), **reuse** (an upstream facility Koan calls as is).

The four parts below were written from the source by four read-only subagents (Sonnet, high effort) and edited only
for heading levels; the summary table is mine, and I spot-checked the load-bearing claims against the source
(`IncrementCapturerCount` semantics, `Freezer::MaybeFreezePageNode`, the `GOOGLE_CHROME_BRANDING` gate, the Glic
perf-traits tracker, `EnableDefaultUserDataDirCheckForTesting`).

**Corrections from the #185 review (2026-11-07), applied in the design, not to the parts below:** `Accessibility.*`
is a renderer-side domain and is not trust-gated, so it is reachable by an untrusted client (only OOPIF behaviour needs
the test); the Glic dependency in A§10 is moot, because `PageLiveStateDecorator::SetGlicActuationState` and
`SetIsPinnedTab` are public setters and replace the freezer, discard and Glic patches (A§6, A§10 and C-row 7 are
superseded by design §8); `Network.enable` leaks cookies and `Authorization` through the `*ExtraInfo` events and
`Network.loadNetworkResource` / `deleteCookies` / `clearBrowserCache` are ungated (C§3c understated this; design
§4.3, §4.5); `Input.dispatchKeyEvent {commands}` can paste the clipboard (design §4.5); `chrome.debugger`-style frame
vetting is stricter than `MayAttachToURL` (design §4.4). On macOS `UnixDomainServerSocket::Credentials` carries only
uid/gid and `net::HttpServer` hides the descriptor, so the peer audit token needs Koan's own accept loop (design §4.1).

## 0. What the audit decides (one table)

| # | Question | Finding | Verdict | Where |
|---|---|---|---|---|
| 1 | Why does a background tab stop? | The views layer detaches the old tab's NSView (`WebView::SetWebContents`), `WebContentsViewCocoa` reports `kHidden`, the widget hides, `LayerTreeHost::SetVisible(false)`: no BeginMainFrame, so no rAF, no animation, no new pixels. Nothing in `browser_view`/the tab strip adds to it | lock | A§2 |
| 2 | Window covered, unfocused, minimized, other Space, app hidden | The Mac occlusion checker (1 s coalesced) hides the *active* tab; minimize/Cmd+H hide it synchronously through views. The three `kNotifyCompositorOf*OnMacOs` window-compositor features are **off** upstream and in Koan: keep them off | lock | A§3 |
| 3 | What keeps a hidden page rendering | `WebContents::IncrementCapturerCount`. `stay_hidden=false` → page told `kVisible` (rAF, CSS animations, timers unthrottled, explicit freeze cleared; `document.visibilityState` becomes "visible"); `stay_hidden=true` → `kHiddenButPainting` (rAF and timers run, `visibilityState` stays "hidden"). `GetVisibility()` stays HIDDEN either way | reuse (visible, `stay_awake=false`, `is_activity=false`) | A§7, B§4 |
| 4 | `stay_awake` | Only a `NoDisplaySleep` power assertion. Nothing renderer-side. Not a keep-alive; an opt-in "unattended run" | lock (off by default) | A§7 |
| 5 | What capture does **not** exempt | Performance Manager freezing (`Freezer` skips only a really VISIBLE page), discarding (`kVisible`/`kRecentlyVisible` use the real visibility), and the renderer's process priority (a captured background tab still votes `kBestEffort`: App-Nap-style suppression on the renderer) | override ×3 (§ "Keep-alive" in the design) | A§5-6 |
| 6 | Process priority | `GlicPerfTraitsTracker::NotifyActuationStateChanged(contents, kActuatingOnBackgroundTab)` casts the `kUserVisible` vote (upstream's own `ActorTask` recipe: capture + this call). Alternative: `RenderProcessHostImpl::OnBoostForBackgroundExecutionAdded` through a content patch | reuse (public call, `chrome/browser/glic/public/glic_perf_traits_tracker.h`) | A§5, A§10 |
| 7 | Freezing | `Freezer::MaybeFreezePageNode` (`components/performance_manager/freezing/freezer.cc:16-28`) returns early only for VISIBLE. One 3-line hook (`IsBeingCaptured()`), or a Koan `CannotFreezeReason::kAgentControlled` modelled on Koan's `kPinnedTab` (≈5 files) | override | A§6 |
| 8 | Discarding | A page-type DevTools attach sets `kDevToolsOpen` (`discard_eligibility_policy.cc:419`): free while a CDP page session is attached; otherwise one reason next to it | reuse + override if the session detaches | A§6 |
| 9 | Global switches | `--disable-background-timer-throttling`, `--disable-renderer-backgrounding`, `--disable-backgrounding-occluded-windows`, `kMacOcclusionForceShutoff`, intensive-throttling features: all process-wide, so they would keep **every** tab awake | lock (never flip) | A§9 |
| 10 | `Page.captureScreenshot` | Takes its own *transient hidden* capturer for the request (`page_handler.cc:1456-1462`), so it works on a hidden tab; `clip`/`captureBeyondViewport` resize the real NSView (relayout on the user's tab); `fromSurface:false` is trusted-only | reuse, with "plain viewport only" for the user's tab | B§1 |
| 11 | `Page.startScreencast` | No capturer: a hidden tab stalls silently. Needs the keep-alive lease | override (lease) | B§3 |
| 12 | `Page.bringToFront`, `Target.activateTarget` | Switch the user's tab and `[NSApp activateIgnoringOtherApps:YES]` + `makeKeyAndOrderFront:`: steals app focus | override (block, return a no-op success) | B§2 |
| 13 | `Target.createTarget` | Default = foreground tab in the user's most recently active window. `background:true,focus:false` avoids the tab switch but still calls `ShowInactive()` on the window. `hidden:true` = detached WebContents, no strip entry | override (Koan creates agent tabs itself) | B§5 |
| 14 | `Emulation.setFocusEmulationEnabled` | Holds a visible capturer for the *session's* life and fakes `hasFocus()` on a boolean shared by all clients | lock (not used; the lease does it) | B§4 |
| 15 | Input on a hidden tab | `Input.dispatch*` and `insertText` work with no OS focus, never move the real pointer; wheel/touch wait for a visual state, so hold the lease first. Untrusted clients' keys skip browser accelerators | reuse (keep the client untrusted) | B§6 |
| 16 | Real input vs CDP input | Every injected event carries `WebInputEvent::kFromDebugger`; it **does** reach `RenderWidgetHost::InputEventObserver` and `DidGetUserInteraction`; Chrome itself filters on the bit (`extension_actor_tracker.cc:129-138`). `Input.insertText` is invisible to observers; tab clicks/omnibox never reach the widget | reuse + Koan observers for UI activity | B§7 |
| 17 | Which targets a CDP client sees | `GetOrCreateAll()`: every profile, window, incognito page, DevTools window, WebUI, extension page. Nothing hides incognito. `Target.getBrowserContexts` lists only DevTools-created contexts | override (a registry) | B§9, C§3b |
| 18 | `--remote-debugging-port` on the default profile | Blocked in 158 **only for `GOOGLE_CHROME_BRANDING`** (`remote_debugging_server.cc:161-183`). Koan is not Chrome-branded, so the switch **works on Koan's default profile today** | override (turn the check on; § "Chrome 136+" in the design) | C§1a |
| 19 | Chrome's own default-profile path | `chrome://inspect#remote-debugging` (`kDevToolsAcceptDebuggingConnections`, on by default): loopback TCP, modal Allow/Don't-allow per connection, a **trusted** browser-target session, one global `DevToolsHttpHandler` slot | lock as is (off by default); not our transport | C§1b |
| 20 | Stock transports | `StartRemoteDebuggingServer` takes a `DevToolsSocketFactory` (a `net::UnixDomainServerSocket` works; Android does it) but it is a process-wide singleton and the clients are trusted. `--remote-debugging-pipe` needs fds 3/4 inherited at launch | lock | C§2a-b |
| 21 | In-process client | `DevToolsAgentHost::GetOrCreateFor(wc)->AttachClient(client)`; a `DevToolsAgentHostClient` with `IsTrusted/MayReadLocalFiles/MayWriteLocalFiles/AllowUnsafeOperations = false` loses the Browser/SystemInfo/Extensions domains, the browser target, local-file access, and `MayAttachToURL` scopes navigation and cookie output. `chrome.debugger` is the precedent | reuse (the core of the design) | C§2c |
| 22 | Cookies | `Network.getAllCookies` / `Storage.getCookies` are present for untrusted clients and return the profile's cookie jar (after `MayAttachToURL` host filtering) | override (deny by default) | C§3c |
| 23 | Policies | `RemoteDebuggingAllowed` (`kDevToolsRemoteDebuggingAllowed`) and `DeveloperToolsAvailability` are enforced on every in-process attach for free | reuse (honour both) | C§1a |

## Parts

* **A.** Rendering, occlusion, throttling, freezing, discarding, capture (macOS).
* **B.** CDP on hidden targets: screenshot, screencast, focus, createTarget, Input, user-input detection, target exposure.
* **C.** Remote-debugging gating and transport: switches, UDS, pipe, in-process client, threat facts, macOS sockets.
* **D.** `agent-browser` (Vercel Labs), the third-party client Koan should work with (not Chromium; read from its public repo and docs on 2026-11-07, UNVERIFIED where marked).

---

# Part A: rendering, occlusion, throttling, freezing, capture


Chromium 158.0.8127.2, read at HEAD through `git show`/`git grep` only. All paths are relative to `chromium/src`; `file:line` is at HEAD. Verdicts: **lock** (keep upstream), **override** (lever named), **reuse** (use as is). "UNVERIFIED" = inferred from code, not proven (no build or run was allowed).

### 0. Bottom line

1. A non-active tab is hidden by the views layer, not by the tab strip: `WebView::SetWebContents` detaches the old tab's NSView, the NSView hides, `WebContentsViewCocoa` reports `kHidden`, and `WebContentsImpl::UpdateWebContentsVisibility(HIDDEN)` runs (section 2). Nothing in `browser_view`/tab strip pauses rendering by itself (section 8).
2. `WebContents::IncrementCapturerCount` is the one upstream lever that overrides that hide. It changes only the **renderer/compositor-facing page visibility** (`kHiddenButPainting` or `kVisible`); `WebContents::GetVisibility()` stays `HIDDEN` (`web_contents_impl.cc:5256-5280`).
3. Because `GetVisibility()` stays `HIDDEN`, Performance Manager (PM) still sees a **background tab**: it can freeze it, discard it, and vote its process to `kBestEffort`. On Mac `kBestEffort` is an App-Nap-style suppression policy on the renderer (`process_mac.cc:183-191`). Capture exempts none of freezing, discarding or process priority (sections 5-7). These are the real gaps.
4. `stay_awake` is not "keep the renderer awake". It only takes a `kIOPMAssertionTypeNoDisplaySleep` power assertion (`web_contents_impl.cc:2831-2841`, `power_save_blocker_mac.cc:80`). Do not use it for the keep-alive.
5. `stay_hidden=false` (visible capture) is the right mode: it also clears an explicit freeze (`page_lifecycle_state_manager.cc:77-90`) and lets CSS animations start through the normal visible path. `stay_hidden=true` is the cheaper, "web page still reads hidden" mode.
6. Upstream already ships the exact recipe for an agent-driven background tab: `ActorTask::DidContentsEnterActorControl` (`chrome/browser/actor/actor_task.cc:975-992`) = capture (visible, awake) + `GlicPerfTraitsTracker::NotifyActuationStateChanged(kActuatingOnBackgroundTab)`. The second call is what fixes process priority.
7. Every command-line switch that disables this throttling is process-global (affects all tabs), so all of them are **lock** for Koan (section 9).
8. Not solved: system sleep / lid close, and an honest "UNVERIFIED on hardware" for minimized, other Space, app hidden and display sleep with a detached tab (section 10).

### 1. WebContents visibility model

| Item | file:line | What it does | Verdict |
|---|---|---|---|
| `Visibility {HIDDEN, OCCLUDED, VISIBLE}` setters `WasShown/WasHidden/WasOccluded` | `content/browser/web_contents/web_contents_impl.cc:3156,3161,3330` | Each calls `UpdateVisibilityAndNotifyPageAndView(v)`. `GetVisibility()` (3335) returns the stored `visibility_`. | lock |
| `CalculatePageVisibilityState` | `web_contents_impl.cc:5256-5280` | `kVisible` if `VISIBLE` or `visible_capturer_count_>0` or XR; else `kHiddenButPainting` if `hidden_capturer_count_>0` or PiP video/doc (or initial hidden-but-painting); else `kHidden`. `OCCLUDED` and `HIDDEN` both map to `kHidden`. | reuse (this is the capture hook) |
| `UpdateVisibilityAndNotifyPageAndView` | `web_contents_impl.cc:5286-5420` | Computes page visibility, pushes `SetFrameTreeVisibility(page_visibility)` to every RenderViewHost (5363 shown before the widgets, 5371 hidden after), calls `SetPrimaryMainFrameViewVisibility`, notifies `OnVisibilityChanged(new_visibility)`, and `GetController().SetActive(page_visibility != kHidden)` (5409). | lock |
| `SetPrimaryMainFrameViewVisibility` | `web_contents_impl.cc:5427-5455` | `view_is_visible = !IsCrashed() && page_visibility != kHidden`. Visible: `view->ShowWithVisibility(page_visibility)` (5448). Else `HIDDEN` -> `view->Hide()`, `OCCLUDED` -> `view->WasOccluded()` (5452). So a captured hidden tab keeps its RWHV shown. | reuse |
| `UpdateWebContentsVisibility` | `web_contents_impl.cc:12678-12730` | Entry used by native views. Turns `OCCLUDED` into `VISIBLE` when `--disable-backgrounding-occluded-windows` is set (12696-12703: global). Ignores everything until `did_first_set_visible_`. | lock (switch is global) |
| `CreateParams::initially_hidden_but_painting` | `web_contents.h:256`, `web_contents_impl.cc:4488-4493,5274` | Hidden-but-painting only **until first shown**; not a durable keep-alive. | lock |
| PM sees only the real visibility | `components/performance_manager/performance_manager_tab_helper.cc:385-391` | `page_node_->SetIsVisible(visibility == VISIBLE)`. Capture never reaches it. | see sections 5-6 |
| `kRFHHiddenWhenHiddenButPainting` (on) | `content/public/common/content_features.cc:1006`, `render_frame_host_impl.cc:4145-4180` | `RenderFrameHost::GetVisibilityState()` reports Hidden for a hidden-but-painting view. Browser-side API only, no renderer effect. | lock |

### 2. How a background tab is hidden on Mac (the path capture has to beat)

| Step | file:line | Behaviour | Verdict |
|---|---|---|---|
| `ContentsWebView::SetWebContents` -> `WebView::SetWebContents` | `chrome/browser/ui/views/frame/contents_web_view.cc:159`, `ui/views/controls/webview/webview.cc:119-151` | Calls `DetachWebContentsNativeView()` (639) before observing the new tab. No `WasHidden()` call anywhere in `frame/` or `tabs/` (grep: none). | lock |
| `NativeViewHostMac::NativeViewDetaching` | `ui/views/controls/native/native_view_host_mac.mm:167-190` | For a `ViewsHostable` view calls `ViewsHostableDetach()`. | lock |
| `WebContentsViewMac::ViewsHostableDetach` | `content/browser/web_contents/web_contents_view_mac.mm:826-850` | In-process NSView bridge `SetVisible(false)` + `ResetParentNSView`; **drops `video_capture_lock_`** (843); each child RWHV gets `SetParentUiLayer(nullptr)` and migrates out of remote_cocoa. | lock |
| `WebContentsViewCocoa updateWebContentsVisibility` | `content/app_shim_remote_cocoa/web_contents_view_cocoa.mm:531-543` | `isHiddenOrHasHiddenAncestor || !window` -> `kHidden`; `[window isOccluded]` -> `kOccluded`; else `kVisible`. Fires from `viewDidHide/viewDidUnhide/viewDidMoveToWindow` (597-609). | lock |
| `kOccluded` is delayed 1 s | `web_contents_view_cocoa.mm:492-529` (`kOcclusionUpdateDelayInSeconds`, 524) | `kVisible`/`kHidden` apply immediately; occlusion is coalesced 1 s. | lock |
| `WebContentsViewMac::OnWindowVisibilityChanged` | `web_contents_view_mac.mm:593-615` | Maps the mojo enum to `Visibility` and calls `UpdateWebContentsVisibility`. | lock |
| `RenderWidgetHostViewMac::Hide` / `WasOccluded` | `content/browser/renderer_host/render_widget_host_view_mac.mm:557-567, 623-640` | `Hide`: NSView hidden, `browser_compositor_->SetViewVisible(false)`, then `WasOccluded`. `WasOccluded`: `host()->WasHidden()` + `SetRenderWidgetHostIsHidden(true)`. | lock |
| `RenderWidgetHostImpl::WasHidden` | `render_widget_host_impl.cc:908-950` | `is_hidden_=true`, `blink_widget_->WasHidden()`, `UpdateClientPriority`, `NotifyVizOfPageVisibilityUpdates`. | lock |
| Renderer: `WidgetBase::SetHidden` -> `LayerTreeHost::SetVisible(false)` | `third_party/blink/renderer/platform/widget/widget_base.cc:619-633,1612-1633`, `cc/trees/layer_tree_host.cc:944-956` | No BeginMainFrame => no rAF, no lifecycle updates, no screenshots of new content. | lock |

What the shown path does on Mac (this is what a captured, detached tab uses):

| Item | file:line | Behaviour | Verdict |
|---|---|---|---|
| `RenderWidgetHostViewMac::ShowWithVisibility` | `render_widget_host_view_mac.mm:549-555` | `ns_view_->SetVisible(true)`, `SetViewVisible(true)`, then `OnShowWithPageVisibility`. | reuse |
| `RenderWidgetHostViewBase::OnShowWithPageVisibility` | `render_widget_host_view_base.cc:1146-1190` | If `host_->IsHidden()` -> `NotifyHostAndDelegateOnWasShown` (no tab-switch metric when not `kVisible`). Handles `kVisible`<->`kHiddenButPainting` flips without hiding. | reuse |
| `BrowserCompositorMac::UpdateState` | `content/browser/renderer_host/browser_compositor_view_mac.mm:214-229,231-290` | Parent ui layer present => `UseParentLayerCompositor` (the window's compositor). **No parent layer (detached tab) and widget not hidden => `HasOwnCompositor`**: a private `RecyclableCompositorMac`, `Unsuspend()`ed, `AttachToCompositor`. So a detached captured tab renders into its own compositor, independent of window state. | reuse |
| `RecyclableCompositorMac` | `ui/compositor/recyclable_compositor_mac.cc:56-64` | Born suspended; `Unsuspend()` in `TransitionToState`. | reuse |
| Frame source for the private compositor | `components/viz/service/frame_sinks/external_begin_frame_source_mac.cc:142-260` | Display link for the display id; invalid id falls back to a `DelayBasedTimeSource` (212-221). Timer BeginFrames, not tied to a window. | reuse (UNVERIFIED which id a window-less view reports) |

### 3. Occlusion, minimize, Space, app hide, screen sleep

| Mechanism | file:line | Behaviour | Verdict |
|---|---|---|---|
| `WebContentsOcclusionCheckerMac` | `content/app_shim_remote_cocoa/web_contents_occlusion_checker_mac.mm:450-530` (pass), `532-590` (`isWindowOccluded`), `640-668` (`setOccluded:`) | Browser-process singleton. Per window: macOS `occlusionState` not visible => occluded; plus manual "one window fully covers it" test (`NSContainsRect`, 624). Displays asleep => every window occluded (400-409, 534). Only windows containing a `WebContentsViewCocoa` count. Ordered-out (not minimized) windows are cleared to *not occluded* (490-493). | lock |
| Emergency switch | same file `:46` (`kMacOcclusionForceShutoff`, off) | macOS 26/Stage Manager kill switch; the only upstream way to disable the checker. Global. | lock |
| `kDetectSpaceChangeViaOcclusion` (off) | `ui/base/ui_base_features.cc:111`, used `native_widget_ns_window_bridge.mm:1569-1584` | Space changes use `onActiveSpace` only when off. | lock |
| `NativeWidgetNSWindowBridge::OnVisibilityChanged` | `components/remote_cocoa/app_shim/native_widget_ns_window_bridge.mm:1534-1567` | `window_visible_ = [window isVisible]`. Minimize (`windowDidMiniaturize`, `views_nswindow_delegate.mm:322`) and app hide (Cmd+H orders windows out) both reach it. | lock |
| Views propagation | `ui/views/widget/widget.cc:2128-2137`, `ui/views/controls/native/native_view_host.cc:218-227` | Window hidden => `PropagateVisibilityNotifications(false)` => `NativeViewHost::HideWidget` => `ViewsHostableSetVisible(false)` => active tab goes `kHidden` (not `kOccluded`) synchronously. This is "WebContentsViewCocoa handles hidden windows". | lock |
| `NativeWidgetMacNSWindowHost::UpdateCompositorVisibility` | `ui/views/cocoa/native_widget_mac_ns_window_host.mm:1133-1190` | `layer_visible = is_visible_ || video_capture_count_>0` (1151); invisible and no capture lock => `compositor_->Suspend()` (1184). With the three `kNotifyCompositorOf*` features it also toggles `Compositor::SetVisible`, again `|| video_capture_count_>0`. | lock |
| `kNotifyCompositorOfWindowVisibilityOnMacOs`, `...SpaceVisibilityOnMacOs`, `...WindowOcclusionOnMacOs` (all **off**) | `ui/views/views_features.cc:74,82,87` | Hide the window compositor when hidden/other Space/covered (occlusion debounced 1.5 s, `native_widget_mac_ns_window_host.mm:1242-1285`). Off by default, so a covered window keeps its compositor. | **lock: keep off** |
| Video capture lock | `native_widget_mac_ns_window_host.mm:1115-1131`, `native_view_host_mac.mm:113`, `web_contents_view_mac.mm:194-203` | `WebContentsViewMac::UpdateVideoCaptureLock` takes a window-level lock only if `IsBeingCaptured() && views_host_` (i.e. the tab is **attached**). Keeps the window compositor visible/unsuspended while captured, even minimized. Detached tabs get no lock (and need none: own compositor). | reuse (automatic) |
| Compositor/Display visibility | `ui/compositor/compositor.cc:581-610`, `components/viz/service/display/display.cc:427-465` | `SetVisible(false)` stops cc frame production for that compositor and frees back buffers. Applies to the window compositor only. | lock |

Situation matrix (what each does to the **active** tab and to a **detached non-active** agent tab; every row for the agent tab assumes visible capture is held):

| Situation | Active tab | Detached non-active tab, no capture | Detached tab with capture |
|---|---|---|---|
| Window unfocused | no change | `kHidden` | rendered (own compositor) |
| Covered by another window | `kOccluded` after >=1 s (kHidden-equivalent) | `kHidden` | rendered |
| Other Space | macOS occlusion => `kOccluded` | `kHidden` | rendered (UNVERIFIED on HW) |
| Minimized | `kHidden` via views propagation; window compositor suspended unless capture lock | `kHidden` | rendered (UNVERIFIED on HW) |
| App hidden (Cmd+H) | same as minimized | `kHidden` | rendered (UNVERIFIED on HW) |
| Display asleep | checker marks all windows occluded | `kHidden` | display link may stop (UNVERIFIED); `stay_awake` prevents it |
| System sleep / lid closed | everything stops | everything stops | everything stops (unsolved) |

### 4. Renderer: scheduler, rAF, animations, timers

| Item | file:line | Behaviour | Verdict |
|---|---|---|---|
| `WebViewImpl::SetVisibilityState` | `third_party/blink/renderer/core/exported/web_view_impl.cc:4330-4350` | `kHiddenButPainting` is treated as **visible for page scheduling** (`DispatchHiddenVisibilityTransitions`, 4338-4343). Flag is `stable` (`runtime_enabled_features.json5:2577`), no base feature gate. | reuse |
| `Page::IsPageVisible`, `Document::hidden` | `core/page/page.cc:866`, `core/dom/document.cc:2331-2343,2371` | True only for `kVisible`. So `document.visibilityState` is "hidden" for `kHiddenButPainting`, "visible" for `kVisible`. | reuse |
| `Page::SetVisibilityState` | `core/page/page.cc:806-860` | `visibilitychange` only on a flip to/from `kVisible`; hidden->hiddenButPainting fires nothing. Visible capture fires the event. | reuse |
| `PageSchedulerImpl::SetPageVisible` | `platform/scheduler/main_thread/page_scheduler_impl.cc:207-224` | Visible => throttling policy off, CPU budget/intensive throttling cancelled (671-686). | reuse |
| Background timer throttling | `frame_scheduler_impl.cc:1086-1113` (`ComputeThrottlingType`) | Page not visible => `kBackground` (1 wake-up/s); not visible and not audible => `kBackgroundIntensive` after the grace period (`page_scheduler_impl.cc:671-686`; 5 min per `docs/PERFORMANCE.md:23`). Skipped entirely when the page scheduler is visible. `TimerThrottlingForBackgroundTabs` is stable (`runtime_enabled_features.json5:6897`). | reuse (capture) / lock (switch) |
| Scheduler freeze guard | `page_scheduler_impl.cc:226-240` | `SetPageFrozen(true)` on a page the scheduler holds visible is **refused** (`DCHECK(false)`; silent no-op in Koan's no-DCHECK Mac build). `IsBackgrounded()` (807) false => `UpdateFrozenState` (827) unfreezes. So both capture modes defeat Blink's own freeze; PM's browser-side state still says frozen (UNVERIFIED consequences). | reuse |
| `kStopInBackground` (off on desktop) | `third_party/blink/common/features.cc:2214-2225` | Freeze scheduler queues after a grace delay in background. | lock |
| `kIntensiveWakeUpThrottling` (on) | `features.cc:900` | The 1/min throttle. Skipped for visible page scheduler. | lock |
| `kThrottleUnimportantFrameTimers`, `kThrottleFullscreenVideoActiveTab`, `kNoThrottlingVisibleAgent` (all off) | `features.cc:2256,2277,1739` | Foreground-page throttles; not active. | lock |
| rAF / lifecycle | `widget_base.cc:1612-1633`, `layer_tree_host.cc:944` | Driven by BeginMainFrame, gated on widget visibility only, **not** on page visibility. `kHiddenButPainting` shows the widget, so rAF runs (and `document.hidden` is true). | reuse |
| CSS / Web Animations | `core/animation/pending_animations.cc:61-67`, `document.cc:2331` | New animations on a non-visible page start through a 0 ms timer on the main thread instead of the compositor. `kVisible` uses the normal path. | reuse `kVisible` (hidden-but-painting UNVERIFIED for compositor-run animations) |
| Media | `content/renderer/media/renderer_web_media_player_delegate.cc:57-75,185-205` | `kHiddenButPainting` treated as shown for video. | reuse |
| Battery/energy saver | `content/child/child_thread_impl.cc:466-476`, `main_thread_scheduler_impl.cc:1177-1197`, `freezing_policy.cc:493,504-508` | Effects found: 32 ms wake alignment (`kBatterySaverModeAlignWakeUps`, off by default, `content_features.cc:212`), fullscreen-video timer throttle (off), and freezing CPU-heavy hidden tabs. **I found no frame-rate cap code** in `cc/`, `viz/`, `ui/compositor`, `content/`; `docs/PERFORMANCE.md:43` says energy saver "caps frame rates" (UNVERIFIED either way). | lock; freeze part see section 6 |

### 5. Process priority and App Nap

| Item | file:line | Behaviour | Verdict |
|---|---|---|---|
| Browser process | `content/app/mac_init.mm:26-30` | `NSAppSleepDisabled = YES`: the browser is not App-Napped. | lock |
| Per-renderer priority input | `render_process_host_impl.cc:6001-6056,6058-6160`, `render_widget_host_impl.cc:2302` | RWH `is_hidden_` feeds `visible_clients_`. A shown (captured) widget counts as visible. | reuse |
| PM override wins | `child_process_launcher.cc:403-430,434-460` | If `priority_override` is set, `is_background()` is simply `override == kBestEffort`, ignoring `visible`/`has_media_stream`. Only `boost_for_loading/discard/background_execution/pending_views` and foreground service workers beat it. | override target |
| PM sets the override | `components/performance_manager/graph/policies/process_priority_policy.cc:50`, enabled `performance_manager/features.cc:166` (`kPMProcessPriorityPolicy`, on), added `chrome_browser_main_extra_parts_performance_manager.cc:281-283` | Max of the votes for frames in the process. | lock |
| Not-visible frame => lowest vote | `components/performance_manager/decorators/frame_visibility_decorator.cc:21-26,160-166`, `priority/frame_visibility_voter.cc:34-36`, `base/process/process.h:194-199` | Frame visible only if `page_node->IsVisible() \|\| IsBeingMirrored(page_node)`. `IsBeingMirrored` comes from `MediaStreamCaptureIndicator` (`page_live_state_decorator_helper.cc:336`), **not** from `IncrementCapturerCount`. So a captured background tab votes `kMinValue == kBestEffort`. | override target |
| What `kBestEffort` does on Mac | `base/process/process_mac.cc:183-191` | `TASK_BACKGROUND_APPLICATION` + suppression policy (what App Nap does) on the renderer; also `SendProcessStateToRenderer(kHidden)` (`render_process_host_impl.cc:6162-6172`). `kUserVisible` is mapped to `kUserBlocking` unless `kUserVisibleProcessPriority` (off, `:1287`, mapping `:6112-6116`). | override target |
| `GlicActuationPriorityVoter` | `priority/glic_actuation_priority_voter.cc:34-57,114-128`, registered `performance_manager_lifetime.cc:142-145`, enabled `features.cc:323` | `kActuatingOnBackgroundTab` => `kUserVisible` vote on the page's **primary main frame** => not background. | **reuse** (public call, section 10) |
| Child frames inherit | `performance_manager_lifetime.cc:135-138` (Mac only, `kInheritParentPriority` on, `features.cc:169`) | Cross-process subframes copy the parent's vote, so they are covered. | reuse |
| `OnBoostForBackgroundExecutionAdded` | `render_process_host_impl.cc:3452-3465`; used by `content/browser/devtools/protocol/hidden_target_manager.cc:23-30,56-68` | Upstream's own "hidden target must not be App-Napped" fix. Beats PM's override (`child_process_launcher.cc:410-416`). Not public API (`RenderProcessHostImpl`). | override alternative (needs a content patch) |
| `--disable-renderer-backgrounding` | `render_process_host_impl.cc:1452-1465`, `content_switches.cc:243` | Forces `visible=true`, drops the override for **every** renderer. | lock (global) |
| `kIsolatesPriorityUseProcessPriority`, `kRestrictThreadPoolInBackground` (off) | `content_features.cc:646,1001` | Would add V8/thread-pool restrictions for backgrounded renderers. | lock |
| Other PM throttles (off) | `features.cc:152,271`, `chrome_browser_main_extra_parts_performance_manager.cc:268,277` | `BestEffortTaskInhibitingPolicy`, `FrameThrottlingPolicy`. | lock |

### 6. Freezing and discarding

#### Freezing (PM `FreezingPolicy`; Koan turns on `kInfiniteTabsFreezing`, `src/browser/koan_feature_overrides.cc:52`)

| Item | file:line | Behaviour | Verdict |
|---|---|---|---|
| Freeze mechanism | `components/performance_manager/freezing/freezer.cc:16-44` | Skips **only** if `GetVisibility() == VISIBLE`, else `contents->SetPageFrozen(true)` (`web_contents_impl.cc:4424`). A captured hidden tab is frozen like any other. Also reachable via `chrome://discards` (`discards_ui.cc:513`) and CDP `Page.setWebLifecycleState` (`page_handler.cc:2050`). | override target |
| Freezing is per browsing instance | `freezing_policy.cc:455-540` | Any `CannotFreezeReason` on any *connected* page (opener-related) protects all; so does a freeze of one. Types: `kVoting`, `kBatterySaver`, `kInfiniteTabs` (`freezing_policy.h:64-72`). | note |
| Upstream reasons | `components/performance_manager/public/freezing/cannot_freeze_reason.h:17-42` | `kVisible, kRecentlyVisible, kAudible, kRecentlyAudible, kFreezingOriginTrialOptOut, kHoldingWebLock, kHoldingIndexedDBLock, kHoldingBlockingIndexedDBLock, kConnectedTo{Usb,Bluetooth,Hid,Serial}, kCapturingVideo, kCapturingAudio, kBeingMirrored, kCapturingWindow, kCapturingDisplay, kWebRTC, kLoading, kNotificationPermission, kOptedOut, kMostRecentlyUsed, kWebUI`. Capturing/mirroring ones come from `MediaStreamCaptureIndicator`, not `IncrementCapturerCount`. **No DevTools/CDP reason.** `kVisible` follows `PageNode::IsVisible()` (`freezing_policy.cc:611`). | lock |
| Koan additions | `patches/components/performance_manager/freezing/*.patch` | `kPinnedTab`, `kPlayedAudio` (removed for `kVoting`). | reuse as the template |
| Periodic unfreeze | `freezing_policy.cc:1286-1300` | Infinite-tabs freezing unfreezes 5 s/min; not a keep-alive. | lock |
| Scheduler side | section 4 | Blink refuses to freeze a page it considers visible. | n/a |

#### Discarding (`DiscardEligibilityPolicy::CanDiscard`, `chrome/browser/performance_manager/policies/discard_eligibility_policy.cc:176-440`)

| Reason | line | Exempts agent tab? | Verdict |
|---|---|---|---|
| `kNotATab` / `kDiscardAttempted` / `kNoMainFrame` / `kAlreadyDiscarded` | 222-247 | no | lock |
| `kVisible` / `kRecentlyVisible` | 276-285 | **no**: `PageNode::IsVisible()` is the real visibility | lock |
| `kAudible`, `kRecentlyAudible`, `kPictureInPicture`, `kPdf`, `kInvalidURL`, `kWebApp` | 289-325 | only if the page does it | lock |
| `kNotWebOrInternal`, `kOptedOut` (always-keep-active list) | 337-350 | no | lock |
| `kNotificationsEnabled`, `kExtensionProtected`, `kCapturingVideo/Audio/Window/Display`, `kBeingMirrored`, `kConnectedToBluetooth/USB`, `kActiveTab`, `kPinnedTab`, `kBackgroundActivity` | 362-430 | no (capturing = `MediaStreamCaptureIndicator`, not `IncrementCapturerCount`) | lock |
| **`kDevToolsOpen`** | 419-423 | **yes, free**: any DevTools client attached to a page-type agent host sets it (`page_live_state_decorator_helper.cc:368-376`), unless `kAllowDevtoolsConnectedDiscard` (off, `discard_eligibility_policy.cc:37`). | **reuse** (UNVERIFIED if a `tab`-type-only attach counts: only `kTypePage` does) |
| `kFormInteractions`, `kUserEdits` | 433-441 | only if the agent typed | lock |

Koan's Memory Saver proactive discard (4 h) goes through this; frozen-memory-growth discard is off (`koan_feature_overrides.cc:60-62`).

### 7. The capture path: `IncrementCapturerCount(size, stay_hidden, stay_awake, is_activity)`

`content/public/browser/web_contents.h:925`, impl `web_contents_impl.cc:2805-2853`, decrement `13080-13115`.

| Parameter | What it does | file:line |
|---|---|---|
| `stay_hidden=true` | `++hidden_capturer_count_`; page visibility `kHiddenButPainting`; `document.visibilityState` stays hidden. Must pass an empty size (DCHECK). | `web_contents_impl.cc:2814-2820,5274` |
| `stay_hidden=false` | `++visible_capturer_count_`; page visibility `kVisible` (renderer is told it is always user-visible); `IsBeingVisiblyCaptured()` true. | `:2818,5271-5273,2859` |
| `stay_awake` | `++stay_awake_capturer_count_` and holds a `kPreventDisplaySleepAllowDimming` wake lock -> Mac `kIOPMAssertionTypeNoDisplaySleep`. Nothing renderer-side. | `:2822-2841`, `power_save_blocker_mac.cc:76-89` |
| `is_activity` | Updates `last_active_time` only if the **real** visibility is `VISIBLE` (`:5376-5379`). Irrelevant for a background tab. | |
| size | Only mirroring; sets `preferred_size_for_capture_`. Pass empty. | `:2828-2831` |

Bookkeeping: `hidden_capturer_count_`, `visible_capturer_count_`, `stay_awake_capturer_count_`; `IsBeingCaptured()` = either > 0 (2855), `IsBeingVisiblyCaptured()` = visible > 0 (2859). Each call ends in `UpdateVisibilityAndNotifyPageAndView(GetVisibility(), is_activity)` (2848, 13114) and `OnCapturerCountChanged` (2863: view -> `UpdateVideoCaptureLock`, surface-embed keep-alive). Release is the returned `ScopedClosureRunner`; destruction decrements. Counts are additive, so several holders are safe.

What a capture does and does **not** exempt:

| Mechanism | stay_hidden=true | stay_hidden=false |
|---|---|---|
| RWH shown, compositor produces frames, rAF runs | yes | yes |
| Blink timer throttling / intensive throttling / `kStopInBackground` | off (scheduler visible) | off |
| `document.visibilityState` | hidden | visible, `visibilitychange` fires on start/stop |
| Explicit freeze cleared | no (`frozen_explicitly_` kept) | yes (`page_lifecycle_state_manager.cc:79-90`) |
| `WebContents::GetVisibility()` | HIDDEN | HIDDEN |
| PM `IsVisible` / MRU list | not visible | not visible |
| PM freezing (`Freezer`) | **not exempt** | **not exempt** (browser side re-freezes; Blink refuses) |
| Discard (`kVisible`/`kRecentlyVisible`) | **not exempt** | **not exempt** |
| Process priority (PM vote) | **not exempt** (`kBestEffort`) | **not exempt** |
| Display/system sleep | only if `stay_awake` | only if `stay_awake` |
| Tab capture indicator / `MediaStreamCaptureIndicator` | none | none |
| Window compositor on Mac | attached tab only, via video-capture lock | same |

Who calls it today (HEAD): DevTools `Page.captureScreenshot` hidden+awake, scoped to the request (`page_handler.cc:1461`); DevTools `Emulation.setFocusEmulationEnabled(true)` visible, not awake, **held until disabled or the session detaches** (`emulation_handler.cc:1051-1066,225-234`, plus Blink `FocusController`, `inspector_emulation_agent.cc:604`); Chromium Actor/Glic visible+awake (`actor_task.cc:979`; `glic_webui_contents_manager.cc:236-239`; `glic_no_webview_contents_manager.cc:1279`); tab capture visible+awake (`web_contents_frame_tracker.cc:153-157`); loopback audio visible+awake (`forwarding_audio_stream_factory.cc:275`); tab thumbnails hidden (`thumbnail_tab_helper.cc:188`); contextual search, Lens: visible to wake a frozen tab (`tab_contextualization_controller.cc:328-347`, comment confirms visible capture unfreezes) and hidden for screenshots (676); read-anything, page-context fetcher (`page_context_fetcher.cc:619`), paint preview, DevTools toolbox (`devtools_window.cc:1662`), omnibox popup (`omnibox_everywhere_ui_manager.cc:750`), tab picker previews (`tab_desktop_media_list.cc:199-412`), long screenshots, glic guest warmup (`glic_guest_observer.cc:198-204`).

Consumers of `IsBeingCaptured` that could surprise us: `fullscreen_controller.cc:391,714` (keep fullscreen across tab switch), `WebView::OnBoundsChanged` letterbox (`webview.cc:304-312`, only fullscreen + preferred size), `offscreen_tab.cc`. None shows UI.

Verdicts: IncrementCapturerCount visible, awake=false, activity=false: **reuse**. The three exemptions it lacks (freeze, discard, priority) are **override**.

### 8. `chrome/browser/ui` independent pausing

| Item | file:line | Result | Verdict |
|---|---|---|---|
| `browser_view.cc` / `BrowserNativeWidgetMac` / `frame/*` | grep for `WasHidden\|WasShown\|SetVisibility\|IncrementCapturerCount` in `chrome/browser/ui/views/frame`, `tabs` | None that touch tab contents. `BrowserView::IsVisibleOnScreen` (`browser_view.cc:1827`) is only a query used by Glic/PiP. | lock |
| `MultiContentsView` / split view | `frame/multi_contents_view.cc` | Both panes attached (both visible); no hide logic. | lock |
| Session restore / new background tab | `browser_commands.cc:687`, `browser_tabrestore.cc:111-125,205-225` | Explicit `WasHidden()` on a newly created background tab; restored minimized/background tabs start hidden. | lock |
| `Target.createTarget` | `chrome/browser/devtools/chrome_devtools_manager_delegate.cc:442-461` | Always `kNewForegroundTab` in the last-used profile: **steals the active tab** (out of this audit's scope but it blocks requirement 1; Koan needs its own creation path with `kNewBackgroundTab`). | override |
| Koan `src/` | grep `WasHidden\|IncrementCapturerCount\|Visibility` | No Koan code touches tab visibility today. | n/a |
| Upstream "hidden target" | `hidden_target_manager.cc:96-103` | Unattached `initially_hidden` WebContents + priority boost; not a tab. Pattern only. | note |

### 9. Global switches and features (all **lock**: they affect every tab, so they violate "only agent tabs stay awake")

| Switch / feature | file:line |
|---|---|
| `--disable-background-timer-throttling` (all renderers; `TimerThrottlingForBackgroundTabs` off) | `content_switches.cc:96`, `render_process_host_impl.cc:3924`, `content/child/runtime_features.cc:341` |
| `--disable-renderer-backgrounding` | `content_switches.cc:243`, `render_process_host_impl.cc:1452-1465` |
| `--disable-backgrounding-occluded-windows` (turns `OCCLUDED` into `VISIBLE` for every tab) | `content_switches.cc:92`, `web_contents_impl.cc:12696-12703` |
| `kMacOcclusionForceShutoff` | `web_contents_occlusion_checker_mac.mm:46` |
| `kNotifyCompositorOf{Window,Space,WindowOcclusion}VisibilityOnMacOs` (keep off) | `views_features.cc:74-87` |
| `kIntensiveWakeUpThrottling`, `--intensive-wake-up-throttling-policy` | `features.cc:900`, `scheduler/common/features.cc:20-60` |
| `kAllowDevtoolsConnectedDiscard` (keep off: it would remove our free discard shield) | `discard_eligibility_policy.cc:37` |

Rule: do not flip any of these. Puppeteer-style flag sets are exactly what this product must avoid.

### 10. Minimal keep-alive for ONE agent tab

Per agent tab, owned by one Koan object (a `WebContentsUserData` holding a lease), created when the agent claims the tab and released on release, tab close, or idle timeout (visible capture runs the compositor at full vsync, so do not leave it on indefinitely):

1. **Render + scheduler (reuse, public API).**
   `contents->IncrementCapturerCount(gfx::Size(), /*stay_hidden=*/false, /*stay_awake=*/false, /*is_activity=*/false)`; keep the `ScopedClosureRunner`.
   Result: RWH shown with its own compositor, rAF and CSS animations run, Blink scheduler treats the page as visible (no 1 s or 1/min timer throttling), explicit freeze cleared, `visibilityState == "visible"`.
   CDP equivalent without a patch: `Emulation.setFocusEmulationEnabled(true)` on a **long-lived** session (same call with `is_activity=true`, `emulation_handler.cc:1059`; also makes the page think it has focus; lifetime = the session). Screenshots then work through `Page.captureScreenshot`, which adds its own transient hidden capture (`page_handler.cc:1461`).
   Use `stay_hidden=true` instead only if the agent must not trigger `visibilitychange` (then rAF/timers still run, but CSS animation start path is the timer one, UNVERIFIED for compositor animations).

2. **Process priority (reuse, public call).**
   `glic::GlicPerfTraitsTracker::GetInstance()->NotifyActuationStateChanged(contents, glic::GlicActuationState::kActuatingOnBackgroundTab)` (`glic_perf_traits_tracker.h:44`), reset to `kNone` on release. Casts the `kUserVisible` PM vote (`glic_actuation_priority_voter.cc:114-128`), which `RenderProcessHostImpl` maps to `kUserBlocking`; Mac child frames inherit it. This is the second half of upstream's `ActorTask` recipe. Cost: Koan's `src/browser` must depend on `//chrome/browser/glic`; if that is undesirable, call `RenderProcessHostImpl::OnBoostForBackgroundExecutionAdded()` from a content patch as `hidden_target_manager.cc:68` does.

3. **Freezing (override, Koan hook).** One of:
   - A (small): in `components/performance_manager/freezing/freezer.cc:16-28` return early when `contents->IsBeingCaptured()`. New file `patches/components/performance_manager/freezing/freezer.cc.patch`. Side effect: transient captures (thumbnails, screenshots) also skip freezing; the policy's `frozen` flag then disagrees with reality but only for the duration (UNVERIFIED benign).
   - B (precise, preferred): `CannotFreezeReason::kAgentControlled` modelled on Koan's `kPinnedTab` (`cannot_freeze_reason.h` enum + string, `freezing_policy.cc` handler like `OnIsPinnedTabChanged`, and a `PageLiveStateDecorator` bit set by the lease). Removed for `kVoting` only if wanted. About five patch files.
   Remember freezing is per browsing instance: a connected user tab shares the protection.

4. **Discarding (reuse, free).** Keep a CDP page-type session attached: `kDevToolsOpen` (`discard_eligibility_policy.cc:419`). If the CLI detaches between commands, add an agent reason next to line 419 in the same patch as step 3B.

5. **Do not set** `stay_awake` by default: it holds a `NoDisplaySleep` assertion, i.e. the Mac never sleeps its display or idles to sleep while an agent tab is leased. Offer it as an explicit "unattended run" option.

Resulting state for a non-active agent tab in an unfocused or covered window: rAF yes, CSS animations yes, timers unthrottled, screenshots yes, process at foreground priority, not frozen, not discarded. All other tabs keep every upstream behaviour (no global switch touched).

#### What stays unsolved or unproven

- **System sleep, lid closed, forced sleep**: nothing here prevents it; `stay_awake` only stops idle sleep.
- **Minimized, other Space, app hidden (Cmd+H)**: by code the detached tab does not depend on the window (own compositor, section 2; the active-tab case is covered by the video-capture lock, section 3), so expected to work, but UNVERIFIED on hardware. Specific risks to test: (a) which display id a window-less `RenderWidgetHostViewMac` reports and whether it falls into the `DelayBasedTimeSource` path (`external_begin_frame_source_mac.cc:212-221`); (b) whether a `CVDisplayLink`/`CADisplayLink` pauses when all windows are hidden or displays sleep; (c) macOS App Nap on GPU and helper processes (only the browser process is protected, `mac_init.mm:30`; renderer suppression is Chromium's own and is what step 2 addresses).
- **Agent tab as the *active* tab of a covered window**: visibility is held by the capture, but the window compositor also needs `video_capture_count_` (automatic for attached tabs) and relies on `kNotifyCompositorOf*` staying off.
- **Display sleep without `stay_awake`**: the occlusion checker marks everything occluded and the display link may stop.
- **Page-visible side effects of visible capture**: the page sees "visible", so a site that pauses when hidden will not pause; and the page may behave differently when the user is not actually looking.
- **Cross-origin offscreen iframes** are still throttled by frame-level rules (`frame_scheduler_impl.cc:1086-1113`), same as in the active tab.
- **Tab creation**: `Target.createTarget` foregrounds the tab (section 8); the keep-alive assumes Koan creates the tab in the background itself.

### 11. UNVERIFIED list

1. `WebContentsNSViewBridge::SetVisible` actually does `setHidden:` on the NSView (not opened; inferred from `viewDidHide` hook).
2. Browser-side `frozen_explicitly_` vs Blink's refused freeze when a PM freeze lands on a captured tab (`web_contents_impl.cc:4424-4432`, `page_scheduler_impl.cc:226-240`).
3. Compositor-run CSS animations under `kHiddenButPainting` (`pending_animations.cc:61-67`).
4. Energy-saver frame-rate cap: not found in this tree.
5. `kTypeTab`-only DevTools attach and `kDevToolsOpen` (only `kTypePage` is handled).
6. Window-less display id and display link behaviour on Mac; App Nap on non-browser helpers.
7. `Cmd+H` leaving the checker's occlusion flag cleared (490-493) while the views propagation hides the NSView: both read, interaction not run.

---

# Part B: CDP on hidden targets


All line numbers are at pristine `HEAD` of ~/work/koan/chromium/src. Verdicts: **lock** (keep upstream), **override** (named lever), **reuse** (use as is). Anything not read in source is marked UNVERIFIED.

Path shorthands (all lines are in these files unless stated):
- `PH` = content/browser/devtools/protocol/page_handler.cc
- `IH` = content/browser/devtools/protocol/input_handler.cc
- `EH` = content/browser/devtools/protocol/emulation_handler.cc
- `TH` = content/browser/devtools/protocol/target_handler.cc
- `CTH` = chrome/browser/devtools/protocol/target_handler.cc (the Chrome-level Target handler that really serves `Target.createTarget`)
- `CDM` = chrome/browser/devtools/chrome_devtools_manager_delegate.cc
- `WCI` = content/browser/web_contents/web_contents_impl.cc
- `RWHI` = content/browser/renderer_host/render_widget_host_impl.cc
- `NAV` = chrome/browser/ui/navigator/browser_navigator.cc

### 0. Summary of what matters

1. **The one upstream lever that keeps a hidden page awake is `WebContents::IncrementCapturerCount`** (content/public/browser/web_contents.h:890-929). Its doc says embedders "must increment the capturer count while capturing to ensure hidden rendering optimizations are disabled; renderers will be configured to produce compositor frames regardless of their backgrounded or on-screen occlusion state".
2. Two flavours, with very different page-visible effects (WCI:5256-5280):
   - `stay_hidden=true` gives `kHiddenButPainting`: widget is shown and produces frames, scheduler treats the page as visible (no timer throttling), but `document.visibilityState` stays `"hidden"` and `document.hidden` is true.
   - `stay_hidden=false` gives `kVisible`: the page is told it is user-visible (`visibilityState == "visible"`).
3. CDP already uses both: `Page.captureScreenshot` takes a **hidden** capturer for the duration of one capture (PH:1456-1462); `Emulation.setFocusEmulationEnabled(true)` takes a **visible** capturer for as long as it stays enabled and the session lives (EH:1051-1067). Chrome's own agent ("actor") keeps agent-controlled tabs awake with a visible capturer plus `host->Focus()` (chrome/browser/actor/actor_task.cc:973-994). Koan can reuse that directly from C++.
4. A CDP session is not enough on its own to keep a tab awake: the capturer in `setFocusEmulationEnabled` is owned by the session's `EmulationHandler` and is released when that session detaches (EH:225-229). A short-lived `koan` CLI invocation would lose it. A long-lived holder is needed (daemon holding the session, or a browser-side C++ holder).
5. `Page.bringToFront` and `Target.activateTarget` both end in `BrowserWebContentsDelegate::ActivateContents`, which switches the active tab and calls `window->Activate()` (macOS: `[NSApp activateIgnoringOtherApps:YES]` + `makeKeyAndOrderFront`). Must be blocked for agent use. Default `Target.createTarget` also foregrounds the new tab and focuses it. `background:true` avoids the tab switch.
6. Injected input is distinguishable from real input: every `Input.dispatch*` event carries `WebInputEvent::kFromDebugger` (IH:99, IH:167), it does reach `RenderWidgetHost::InputEventObserver` and `WebContentsObserver::DidGetUserInteraction`, and Chrome itself filters on that bit (chrome/browser/enterprise/connectors/reporting/extension_actor_tracker.cc:129-138).
7. Not solved by CDP alone: window-level focus/raise control, a persistent "keep awake" holder, per-target filtering (incognito, browser UI, extension pages), blocking real input into an agent tab while still letting injected input through, freezing/discard exemption, and creating a tab without it appearing in the user's visible tab strip as active.

### 1. Page.captureScreenshot: full path

Entry `PageHandler::CaptureScreenshot` PH:1405.

| item | file:line | behaviour | verdict |
|---|---|---|---|
| Preconditions | PH:1413-1417 | Fails `InternalError` if no `host_`, no RenderWidgetHost, or no RWHV. A tab whose renderer crashed has no view and gets an error, not a frame. | lock |
| Capturer | PH:1456-1462 | `wc->IncrementCapturerCount(gfx::Size(), stay_hidden=true, stay_awake=true, is_activity=false)`. Comment: "Tell page it needs to produce frames even if it doesn't want to (e.g. is not currently visible)". Handle lives in `PendingScreenshotRequest::capturer_handle` (PH:514-523) and is released when the request is destroyed (after `ScreenshotCaptured`). | reuse |
| What the capturer does | WCI:2805-2853 | `++hidden_capturer_count_`; `OnCapturerCountChanged()` (WCI:2863); a `kPreventDisplaySleepAllowDimming` wake lock when `stay_awake` (WCI:2834-2846); then `UpdateVisibilityAndNotifyPageAndView` (WCI:2848). | reuse |
| Resulting page state | WCI:5256-5280 | `visibility == VISIBLE || visible_capturer_count_>0` gives `kVisible`; else `hidden_capturer_count_>0` gives `kHiddenButPainting`; else `kHidden`. | reuse |
| Widget shown | WCI:5357-5366, 5427-5457 | For `page_visibility != kHidden`: `view->ShowWithVisibility(page_visibility)`, so the RWH is not hidden and the renderer compositor runs. Only `kHidden` calls `view->Hide()` / `WasOccluded()`. | reuse |
| Mac view | content/browser/renderer_host/render_widget_host_view_mac.mm:549-555; render_widget_host_view_base.cc:1146-1172 | `ShowWithVisibility` sets `is_visible_`, shows the compositor view, then `NotifyHostAndDelegateOnWasShown` if the host was hidden. | reuse |
| Tab not attached to a window | content/browser/renderer_host/browser_compositor_view_mac.mm:214-229 | If there is no parent layer and the host is not hidden, state becomes `HasOwnCompositor` (a private `RecyclableCompositorMac`), so a background tab still gets a compositor. UNVERIFIED that this path actually yields BeginFrames on an unattached background tab under Koan's tab model; test it. | reuse |
| Window occluded / minimized | content/browser/web_contents/web_contents_view_mac.mm:194-206; ui/views/cocoa/native_widget_mac_ns_window_host.mm:1114-1123, 1133-1187 | `IsBeingCaptured()` (any capturer count, hidden or visible) makes the active tab's view take a `VideoCaptureLock` on its window host; `UpdateCompositorVisibility` keeps the window compositor visible when `video_capture_count_>0` even if the window is hidden, minimized, on another space or occluded. BUT those three `kNotifyCompositorOf{Window,Space,Occlusion}...OnMacOs` features are `FEATURE_DISABLED_BY_DEFAULT` at HEAD (ui/views/views_features.cc:74-89) and Koan does not enable them (src/browser/koan_feature_overrides.cc:21-71), so the window compositor is not currently hidden on occlusion at all. The lock matters only if someone turns them on. Lock only attaches while the WebContents is attached to a window (`views_host_` set, web_contents_view_mac.mm:195). | reuse (and note the dependency) |
| Redraw + copy (default path) | RWHI:2367-2403, 3810-3818, 3996-4003 | Pending snapshot recorded, `blink_widget_->ForceRedraw(cb)` asks the renderer for a new frame, on ack `GetView()->CopyFromSurface(rect(), size(), timeout=0, ...)`. Timeout is `base::TimeDelta()` (no timeout). If the widget is hidden (no capturer) the renderer never produces the frame and the request would wait; the capturer exists to prevent exactly this. | reuse |
| Copy retries / failure mode | RWHI:4049-4080 | Up to 5 retries of `CopyFromSurface`; after that an empty `gfx::Image` is returned. | lock |
| Error to client | PH:1996-2000 | Empty image becomes `ServerError("Unable to capture screenshot")`. So the failure mode for a hidden widget is: a frame (with capturer), or "Unable to capture screenshot", or (renderer never answers `ForceRedraw`) a hang with no timeout. There is no empty-success. | lock |
| Surface not valid | content/browser/renderer_host/delegated_frame_host.cc:151-184, 226-227 | `CopyFromCompositingSurface` returns without issuing the request if `local_surface_id_` is invalid (e.g. navigation that never committed); the dropped request resolves as an empty result, which retries then fails. | lock |
| `kCDPScreenshotNewSurface` | PH:1607-1612; RWHI:2374-2384; content/common/features.cc:157 | Disabled by default. When enabled, `CHECK(page visibility != kHidden)` and uses `RequestRepaintOnNewSurface`. Comment: a fully hidden view "will stall because the surface is never presented". Leave disabled. | lock |
| `fromSurface:false` | PH:1470-1481; RWHI:2391-2402, 4029-4047 | Trusted clients only; uses `GetSnapshotFromBrowser(false)` -> `ui::GrabViewSnapshot` of the real NSView (and a Mac display wake lock). Requires a really on-screen window. Do not use for agent tabs. | lock |
| clip / emulation / `captureBeyondViewport` | PH:1484-1584, 1981-1994; render_widget_host_view_mac.mm:641-656 | Sets device-emulation params and calls `widget_host->GetView()->SetSize(clip size)`; on Mac that is `ns_view_->SetBounds`, i.e. a real resize of the page view, restored in `ScreenshotCaptured`. On the user's visible tab this flickers/relayouts. `captureBeyondViewport` also flips web preferences (`hide_scrollbars`, `record_whole_document`, PH:1540-1556). A plain viewport screenshot (no clip, no emulation) does not resize. | lock; for the user's current tab use plain viewport screenshots only |
| Full-page | PH:1423-1432, 1374-1403 | `GetFullPageSize` then clip + `captureBeyondViewport=true`; rejects >= 128K px. | lock |
| Screenshot does not raise anything | PH:1405-1620 | No call to Activate/Focus/ShowInactive. The only side effect on the window is the (conditional) video capture lock above. | reuse |

Net: `Page.captureScreenshot` works on hidden, occluded and (if the compositor is not suspended by window state) minimized tabs because of the capturer it takes. Its capturer is transient, so between screenshots the tab is hidden again and rAF/timers are not kept alive.

### 2. Page.bringToFront and Target.activateTarget

| item | file:line | behaviour | verdict |
|---|---|---|---|
| `Page.bringToFront` | PH:1806-1823 | `web_contents->Activate(); GetOutermostWebContents()->Focus();` | **override**: block the method for agent sessions (see lever below) |
| `WebContentsImpl::Activate` | WCI:4723-4728 | `delegate_->ActivateContents(this)`. | |
| `ActivateContents` (Browser) | chrome/browser/ui/browser_web_contents_delegate/browser_web_contents_delegate.cc:791-801 | `tab_strip_model->ActivateTabAt(index)` (switches the user's visible tab in that window) then `window_->Activate()`. Returns silently if contents not in a tab strip. | |
| `Widget::Activate` on Mac | ui/views/widget/native_widget_mac.mm:822-828; components/remote_cocoa/app_shim/native_widget_ns_window_bridge.mm:1061-1076 | `kShowAndActivateWindow`: `[NSApp activateIgnoringOtherApps:YES]` then `makeKeyAndOrderFront:`. Steals app activation and raises the window above other apps. | |
| `WebContentsImpl::Focus` | WCI:7312-7316; render_widget_host_view_mac.mm:666-688 | `view_->Focus()` -> `MakeFirstResponder` in the NSWindow. Moves keyboard focus away from the omnibox / user's page if that window is key. | |
| `Target.activateTarget` | TH:1293-1305; content/browser/devtools/render_frame_devtools_agent_host.cc:1013-1020 | `agent_host->Activate()` -> `wc->Activate()`, same chain as above (without the extra `Focus()`). | **override**: same lever |
| `DevToolsManagerDelegate::Activate` | CDM:272-292 | Not reached from `Target.activateTarget` on page hosts (content calls `agent_host->Activate()` directly). Additionally raises an undocked DevTools window. | lock |

Lever (pick one, listed from least to most invasive):
1. Client side: the `koan` CLI never sends these two methods. Insufficient as a guarantee for a general CDP endpoint.
2. Koan hook in the session: a `DevToolsManagerDelegate::HandleCommand` filter (CDM:294-306 shows the delegate gets every command before the handlers; `NotHandledCallback` lets it swallow one) that rejects `Page.bringToFront` / `Target.activateTarget` for the agent channel. UNVERIFIED that HandleCommand sees both domains in all session modes; the method is wired through `ChromeDevToolsSession` (sessions_ map, CDM:431-440).
3. Make `BrowserWebContentsDelegate::ActivateContents` (browser_web_contents_delegate.cc:791) skip contents flagged as agent-owned.

### 3. Page.startScreencast

| item | file:line | behaviour | verdict |
|---|---|---|---|
| Start | PH:1621-1688 | Requires `AssureTopLevelActiveFrame`; sets up encoder; computes `visible = !widget_host->IsHidden()` and sends `Page.screencastVisibilityChanged` (PH:1668-1669, 1866-1868); `video_consumer_->StartCapture()` (PH:1685). **No `IncrementCapturerCount` anywhere in this function.** | lock (understand) |
| Capturer path | content/browser/devtools/devtools_video_consumer.cc:69-77, 116-128; PH:586-608 | `viz::ClientFrameSinkVideoCapturer` targeted at the RWH's `FrameSinkId` (`SetFrameSinkId`, PH:605-608). It only receives frames that viz draws for that surface; it does not wake the renderer. | lock |
| Hidden tab | PH:616-623 | `RenderWidgetHostVisibilityChanged` just notifies the client (`screencastVisibilityChanged`). A hidden tab produces no frames, so the screencast stalls silently until visible. | lock |
| Keeps hidden tab producing frames? | | **No.** It must be paired with a capturer count (visible or hidden) held elsewhere. | **override**: hold `IncrementCapturerCount` while a screencast is wanted |
| Frame pacing | PH:1874-1934, 1870-1872 | Needs the client's `screencastFrameAck`; `maxFramesInFlight` default 3. A client that stops acking stalls the stream. | lock |
| Other screen APIs | PH:1690-1727 | `Page.startScreenRecording` (`MediaRecorder::Start(host_, ...)`) is a separate path. Not read in depth; UNVERIFIED whether it takes a capturer. | UNVERIFIED |

For "see the user's current tab" the screencast is fine (tab is visible). For agent tabs prefer on-demand `captureScreenshot` or hold a capturer yourself.

### 4. Making a hidden page believe it is visible / active

| item | file:line | behaviour | verdict |
|---|---|---|---|
| `Emulation.setFocusEmulationEnabled` (browser side) | EH:1051-1067 | Sets `RenderWidgetHostImpl::focus_emulation_enabled_` (render_widget_host_impl.h:563-566) and, on enable, holds `IncrementCapturerCount(gfx::Size(), stay_hidden=false, stay_awake=false, is_activity=true)` in `capture_handle_`. That is a **visible** capturer: `kVisible`, `visibilityState == "visible"`, widget shown, compositor kept running, no timer throttling. Released on disable or `Emulation.disable` (EH:225-229) i.e. on session detach. | **reuse** (the closest thing to a CDP "pretend visible+focused") |
| What it fakes in Blink | third_party/blink/renderer/core/inspector/inspector_emulation_agent.cc:604-612; third_party/blink/renderer/core/page/focus_controller.h:124-127, focus_controller.cc:2060-2078 | `IsActive()` and `IsFocused()` return `true` if emulating, so `document.hasFocus()` is true and `focus`/`blur`/`:focus` behave as focused. It does not by itself change visibility; the visibility effect comes from the capturer above. | reuse |
| Browser-side focus | content/browser/renderer_host/render_frame_host_impl.cc:13888-13903 | `RenderFrameHostImpl::IsFocused()` returns true when emulating even without OS focus. | reuse |
| Clash between sessions | EH:146-162, 1051-1056; focus_controller.cc:2060 | `focus_emulation_enabled_` on the RWH and `is_emulating_focus_` in `FocusController` are booleans, not counters. Two sessions both enabling it and one disabling it turn emulation off for both (the capturer is per session, so the page may still be visible while `hasFocus()` goes false). | lock; give each agent tab exactly one owner session |
| `Page.setWebLifecycleState` | PH:2037-2058 | `Frozen`: calls `web_contents->WasHidden()` then `SetPageFrozen(true)`. `Active`: only unfreezes. Cannot make a page visible. | lock; never send `frozen` to agent tabs |
| `Emulation.setIdleOverride` | EH:580-593 | Only the Idle Detection API (`GetIdleManager()`). No effect on visibility, throttling or focus. | lock |
| `Emulation.setVirtualTimePolicy` | blink inspector_emulation_agent.cc:633-640 | Virtual time budget for the page's scheduler; DevTools tasks run on an unpausable queue (frame_scheduler_impl.cc:615-621). Not a visibility lever and changes timer semantics. | lock |
| Any CDP method that sets visibility | grep of inspector_emulation_agent.cc, inspector_page_agent.cc, EH for visibility/hidden/occlusion | None found. `Emulation.setVisibleSize` (EH:916-924) only sets a device emulation size. | lock |
| `kHiddenButPainting` vs `kVisible` for rAF/timers | web_view_impl.cc:4330-4351; runtime_enabled_features.json5:2561-2580; page.cc:819-851,866-869; document.cc:2355-2373; pending_animations.cc:61-66; widget_input_handler_manager.cc:832 | With `DispatchHiddenVisibilityTransitions` (status stable, so I infer default on; UNVERIFIED for the generated base feature default) `kHiddenButPainting` is treated as visible for page scheduling (no throttling); `IsPageVisible()` is true only for `kVisible` so `document.hidden` is true. No visibility gate exists in rAF servicing (grep of scripted_animation_controller.cc / page_animator.cc finds none): rAF fires whenever BeginMainFrames arrive. Pending CSS animations on a not-`kVisible` page start through a zero-delay timer instead of waiting for a frame (pending_animations.cc:61-66), which is harmless. | reuse (either flavour keeps rAF/timers/animations running; choose `stay_hidden` by whether pages must see `visible`) |
| Page-visible polyfill | `Page.addScriptToEvaluateOnNewDocument` (blink inspector_page_agent) | Can override `document.visibilityState`, `document.hidden` and dispatch `visibilitychange`. UNVERIFIED in source but standard. It only fools JS that reads those properties; it cannot make timers or BeginFrames run, and it will be inconsistent with `IntersectionObserver`/media. Last resort only, and only needed for `stay_hidden=true`. | last resort |
| Process priority | content/browser/devtools/protocol/hidden_target_manager.cc:20-74; content/browser/renderer_host/render_process_host_impl.cc:3452-3464, 6076 | Upstream already has `RenderProcessHostImpl::OnBoostForBackgroundExecutionAdded/Removed` to stop PerformanceManager dropping a hidden target's process to `kBestEffort`/App Nap. Internal (`RenderProcessHostImpl`), not public API. A shown widget (capturer) also counts as a visible client; see RWHI:2465-2468 for how hidden state feeds `UpdateClientPriority` (UNVERIFIED for the exact priority outcome). | **override**: Koan C++ can call it for agent tab processes |
| Actor precedent | chrome/browser/actor/actor_task.cc:91-106, 973-994 | Chrome's agent: `SetFocusState(contents, true)` (= `RenderWidgetHost::Focus()`), then `IncrementCapturerCount(Size(), stay_hidden=false, stay_awake=true, is_activity=true)`, notify the perf-traits tracker `kActuatingOnBackgroundTab`, and (Mac) `ForbidExternalPopupMenus`. | **reuse** as Koan's design template (use `stay_awake=false` unless display sleep prevention is wanted: it takes a `kPreventDisplaySleepAllowDimming` wake lock, WCI:2834-2846) |

### 5. Target.createTarget, and window control

The effective handler is the Chrome-level one (CTH:95-284). The content-level `TargetHandler::CreateTarget` (TH:1367-1458) is only the fallthrough for `hidden:true` (CTH:110-113), and the legacy `CreateNewTarget` (CDM:442-461, always `kNewForegroundTab`, ignores `new_window`) is used by the HTTP `/json/new` handler (content/browser/devtools/devtools_http_handler.cc:695).

| option | file:line | behaviour | verdict |
|---|---|---|---|
| profile | CTH:114-131 | `browserContextId` -> `DevToolsBrowserContextManager::GetProfileById` (default profile or DevTools-created OTR contexts only); else `ProfileManager::GetLastUsedProfile()`. | reuse |
| Which Browser window | CTH:142-160 | If not `newWindow`: first window in `ForEachCurrentBrowserWindowInterfaceOrderedByActivation` with the same profile and not closing, i.e. **the user's most recently active window**. If none exists, a new window is created (CTH:200). | reuse; **override** if agent tabs should live in a dedicated window/group |
| `background` / `focus` defaults | CTH:133-141 | `should_focus = focus.value_or(!background)`; `create_in_background = background || focus==false`. `background:false` (default) and omitted `focus` means focus + foreground. `focus:true` with `background:true` is rejected. | reuse |
| Disposition | CTH:31-52, 229-233 | `newWindow`: `kNewWindow`. else `background ? kNewBackgroundTab : kNewForegroundTab` with `params.browser = target`. If `background`: `window_action = kShowWindowInactive`. | |
| Foreground tab | NAV:530-533 | `ADD_ACTIVE`: the new tab becomes the active tab of the user's window immediately. The user's current tab is switched away. | lock = unacceptable for agents |
| Background tab | NAV:510-515, 592-594 | `ADD_ACTIVE` removed; `create_params.initially_hidden = true`. The tab appears in the strip but is not selected; the WebContents starts hidden. | **reuse: always `background:true, focus:false`** |
| `kShowWindowInactive` effect | NAV:610-657 (ScopedBrowserShower destructor); components/remote_cocoa/app_shim/native_widget_ns_window_bridge.mm:1077-1091 | On every background-tab creation the destructor calls `window->ShowInactive()`. On Mac the bridge then orders the window below the app's main window (same screen) or, when the main window is key and the browser window is on another screen, `orderFrontKeepWindowKeyState` which can cause a Space switch (comment at 1083-1089, 526). No effect if the window is miniaturized (guard at 1078). UNVERIFIED whether the bridge returns early when the window is already visible, so the above may be a no-op for the normal case. Needs a Mac test with Koan on another Space / display. | **override** if it misbehaves: patch `CreateNavigateParams` (CTH:31-52) to `kNoAction` for agent creates |
| `should_focus` | CTH:270-272 | `navigated_or_inserted_contents->Focus()` only when focused. | lock (avoid by `background:true`) |
| New window options | CTH:202-227, 238-269 | `left/top/width/height/windowState` only for new windows; `minimized` is applied after creation (`Minimize()`), `fullscreen` toggles browser fullscreen. | lock |
| `hidden:true` | TH:1405-1439; hidden_target_manager.cc:90-126 | A WebContents with no window and no tab-strip entry (`initially_hidden`), kept alive for the lifetime of the creating session (CloseContents erases it), process boosted by `OnBoostForBackgroundExecutionAdded`. Requires remote debugging targets to exist (TH:1409-1415), `background` omitted or true, no `forTab`, no `newWindow`. Hidden WebContents is `kHidden` by default; only a capturer (screenshot/your own) makes it paint. `Browser.getWindowForTarget` fails on it ("Browser window not found", chrome/browser/devtools/protocol/browser_handler.cc:119-121). | **reuse candidate** for agent-only tabs the user never sees; but a tab the user may later want to take over must be adoptable into a window (no upstream API; Koan C++ needed) |
| `forTab` | CTH:274-278 | Returns a `tab` target (parent of frame targets) when true. Tab targets are the only ones with embedder data (CDM:367-405: `tabStripIndex`, `tabActive`, `tabPinned`, `tabGroupId`). | reuse |
| Privileged URLs | CTH:179-198 | `chrome-untrusted://`, `devtools://`, `isolated-app://` rejected for untrusted clients; `file:` requires `MayReadLocalFiles`. `chrome://` pages are allowed. | lock |

`Browser.*` window methods (chrome/browser/devtools/protocol/browser_handler.cc):
- `getWindowForTarget` (92-127): finds the Browser containing the target's WebContents via `ForEachCurrentBrowserWindowInterfaceOrderedByActivation`; returns `windowId` (SessionID) and bounds with state `minimized/maximized/fullscreen/normal` (51-77).
- `getWindowBounds` (129-139), `setWindowBounds` (146-207): state transitions (`Minimize()`, `Maximize()`, `Restore()`, `EnterFullscreen`, `SetBounds`). These change the user's window: never expose to agent sessions. **lock + block**.
- It reports *window state*, not "is this the user's active window". There is no CDP field for OS focus of a window; `tabActive` (CDM:398) is per-window active tab only. To find "the user's current tab" use the first window in activation order (what CTH:148 uses) and its `tabActive` tab: this is C++ knowledge, not exposed over CDP as "front window". Gap.

### 6. Input domain on hidden targets

| item | file:line | behaviour | verdict |
|---|---|---|---|
| Path of mouse events | IH:1405-1444, 1639-1661, 749-776; RWHI:1706-1773 | `Input.dispatchMouseEvent` does hit-test async (`GetRenderWidgetHostAtPointAsynchronously`), then `InputInjector::InjectMouseEvent` -> `RenderWidgetHostImpl::ForwardMouseEvent`. Events are synthesized `WebMouseEvent`s through the renderer input router. They never touch NSEvent, the OS cursor or the OS focus. Does not move the real pointer. | reuse |
| Page focus per event | IH:721, 761, 786, 817, 1310, 1363; RWHI:1475-1485, 1519-1578 | Every injected event calls `widget_host_->Focus()` = `SetPageFocus(true)`: tells Blink the page is focused/active (`document.hasFocus()` true, key events dispatched). This is *page* focus (`FocusState::kFocused` over mojo), not NSWindow key status and not first responder. Side effects on the same RWH only. Does not blur the user's tab: the user's page keeps its own `is_focused_`. Touch also enables the TouchEmulator (IH:817-820). | reuse |
| Needs OS focus? | IH:778-808; RWHI:1848-1980 | No. | reuse |
| Hidden widget accepted? | RWHI:1726 (`IsIgnoringWebInputEvents` checks only process-blocked/`delegate_`/`ShouldIgnoreWebInputEvents`); components/input/render_input_router.cc:438-444 | Not rejected for being hidden. Hang monitor is not started while hidden (render_input_router.cc:441). | reuse |
| Acks on hidden widget | third_party/blink/renderer/platform/widget/input/widget_input_handler_manager.cc:818-835; widget_base.cc:1612-1633 | If the widget is hidden, the input-processed callback is resolved immediately instead of waiting for a frame; on hide, pending ones are flushed. So no deadlock, but also no guarantee the page has rendered the effect. With a capturer the widget is not hidden at all. | reuse |
| Wheel / touch wait for a visual state | IH:1435-1441, 2081-2085; RWHI:2269-2285 | Wheel and touch (`Input.dispatchMouseEvent type=mouseWheel`, `dispatchTouchEvent`) first `InsertVisualStateCallback`, answered by the compositor after the next commit. On a widget with no frame production (no capturer) this can wait. UNVERIFIED for hidden compositor behaviour; mitigate by always holding a capturer before sending wheel/touch. | reuse + hold capturer |
| Keyboard | IH:1196-1289; RWHI:1848-1980 | Routed to the focused sub-widget (IH:1272-1278). If the client is not trusted (`IsTrusted()`, passed at render_frame_devtools_agent_host.cc:433-435) there is no `os_event` and `skip_if_unhandled=true` (IH:1282-1285), which makes `RenderWidgetHostImpl` **skip `PreHandleKeyboardEvent` and `HandleKeyboardEvent`** (RWHI:1918, 2833-2835). So injected Cmd+W / Cmd+T from an untrusted client do not trigger browser accelerators. A trusted client with `nativeVirtualKeyCode` does. Koan's CLI channel must be untrusted for this. | reuse (keep channel untrusted); verify `IsTrusted()` of Koan's client |
| `Input.insertText` / IME | IH:1291-1314, 1316-1370 | Calls `widget_host->Focus()` then `WidgetInputHandler::ImeCommitText` directly; no WebInputEvent, so it is **invisible to `InputEventObserver`**. Works on hidden widgets (renderer-side), requires a focused editable. | reuse |
| Unhandled-event bubbling when hidden | RWHI:2656-2662, 2830-2836 | Browser-level fallbacks for unconsumed mouse/keys (back/forward buttons, accelerators) only run when `!IsHidden()`. With a capturer the widget is not hidden, so these run for real input only; injected key events skip them anyway (above). | lock |
| Drag | IH:756, 871-876 | Mouse events go through `drag_controller_`; no OS drag session for pure CDP drags (UNVERIFIED for file drags). | lock |
| `userGesture` | n/a here | Real activation tokens: input events create user activation like real input; `Runtime.evaluate userGesture:true` gives transient activation without input (section 8). | reuse |

### 7. Detecting REAL user input vs CDP-injected input

| item | file:line | behaviour | verdict |
|---|---|---|---|
| Marker on injected events | IH:99 (keyboard modifiers), IH:167 (mouse/wheel button modifiers) | Every `Input.dispatchKeyEvent`, `dispatchMouseEvent` (incl. wheel) event carries `WebInputEvent::kFromDebugger` in `GetModifiers()`. Synthetic gesture drivers in content set it too (content/common/input/synthetic_mouse_driver.cc:58,92,124; synthetic_touch_driver.cc:57-102). Real OS events never do. | **reuse** as the discriminator |
| Injected events DO reach observers | components/input/render_input_router.cc:476-481, 588; RWHI:2956-2973; IH:838-880 | `RenderInputRouter::OnInputDispatchedToRendererResult` -> `NotifyObserversOfInputEvent` -> every `RenderWidgetHost::InputEventObserver::OnInputEvent(host, event, source)`. DevTools' own `InputInjector` relies on this (IH:839-843, sets `input_queued_`), and `OnInputEventAck` (RWHI:2975-2982) filters on `kFromDebugger` (IH:849). So an observer sees injected and real events alike and must test the bit. | reuse |
| `InputEventObserver` API | content/public/browser/render_widget_host.h:243-272, 275-276 | `AddInputEventObserver(observer)` per RWH; `OnInputEvent`, `OnInputEventAck`. All event types (mouse move/down/up, wheel, key down/up/char, touch, gestures). A new RWH after cross-process navigation needs re-registering (observe `RenderFrameHostChanged`). | reuse (preferred for "pause the agent") |
| `DidGetUserInteraction` | content/public/browser/web_contents_observer.h:659-665; WCI:11283-11325, 11307-11319 | Fires for mouseDown, gestureScrollBegin, touchStart, rawKeyDown/keyDown only (`IsUserInteractionInputType`, web_contents_impl.cc ~343-352), only when the event was dispatched to the renderer and the widget is in the primary frame tree. Injected events fire it too, with the same `kFromDebugger` bit. **Chrome precedent:** chrome/browser/enterprise/connectors/reporting/extension_actor_tracker.cc:129-138 ignores events with `kFromDebugger` to tell `chrome.debugger` actors from the user. | reuse (coarser, but simplest) |
| Pre-handlers | RWHI:1750, 1929; WCI:4812-4837, 4881-4890 | `WebContentsDelegate::PreHandleMouseEvent / PreHandleKeyboardEvent` (and `HandleKeyboardEvent` after the renderer) see real keys (accelerators). Injected untrusted keys skip them (above), so these hooks effectively see only real input for keyboard. Mouse: `PreHandleMouseEvent` is called for injected mouse events too (RWHI:1750, not gated by `skip_if_unhandled`), use the bit. | reuse (keyboard); not needed otherwise |
| Audit callback | content/public/browser/web_contents.h:1713-1728; WCI:11327-11354; RWHI:1726 | `WebContents::IgnoreInputEvents(audit_callback)`: while the returned scoper lives, the callback sees **every** web input event before dispatch and returns false to drop it. Drops injected events too unless the callback returns true for `kFromDebugger`. Header caveat: focus events and `ui::Event`s are always ignored without asking the callback, and `ShouldIgnoreInputEvents()` becomes true as soon as any callback is installed (WCI:11344-11348); the only upstream consumer of that bool is the Aura view (render_widget_host_view_aura.cc:2942), not Mac. Chrome's agent uses the callback-less form to block the user while acting (chrome/browser/actor/ui/actor_overlay_web_view.cc:43-46). | **reuse**: for "block real input while agent drives", return `true` for `kFromDebugger`, `false` (and signal take-over) otherwise |
| Browser UI interaction | n/a | Clicks on the tab strip, omnibox typing, window focus changes do not pass through any RWH observer. Use `TabStripModelObserver` (tab activated), `BrowserWindowInterface` activation observers, `WebContentsObserver::OnVisibilityChanged` (real `Visibility`, WCI:5314-5319) for "user looked at this tab". | **override**: C++ in Koan |
| IME / `insertText` | IH:1291-1314 | Not an `InputEvent`, not seen by `InputEventObserver` (real IME commits arrive the same way on Mac, key events precede them). | lock |

Recommended take-over design (all upstream APIs): one `InputEventObserver` per agent-tab RWH; pause when an event arrives with `!(event.GetModifiers() & kFromDebugger)` and type in {mouse down, key down, wheel, touch start}; optionally combine with `IgnoreInputEvents(audit_cb)` to drop real events during an atomic agent action. `kFromDebugger` is spoofable by the agent only through the CDP itself, which is fine.

### 8. Runtime / DOM / Accessibility on hidden tabs, sessions

| item | file:line | behaviour | verdict |
|---|---|---|---|
| Runtime.evaluate | blink web_dev_tools_agent_impl.cc:473-478; frame_scheduler_impl.cc:615-621, 1462-1470 | The DevTools agent mojo receiver is bound to `TaskType::kInternalInspector`, which is unpausable, ignores virtual time and is excluded from task deferral. It runs on hidden/throttled pages. A *frozen* page (Koan enables `InfiniteTabsFreezing`, src/browser/koan_feature_overrides.cc:52) will not answer until unfrozen (UNVERIFIED for freeze vs this queue). | reuse; ensure agent tabs are never frozen |
| DOM / CSS | | Computed in the renderer on demand; layout updates are forced synchronously by the inspector. Box models stay valid on hidden tabs. UNVERIFIED for stale geometry on never-painted tabs. | reuse |
| Accessibility domain | WCI:5321-5326, 5389-5392 | Platform AX mode is dropped for `HIDDEN` contents (`OnWebContentsHidden`) but the CDP Accessibility agent builds its own tree in the renderer. UNVERIFIED whether the tree is complete for a hidden page with no AX mode. A `kHiddenButPainting` page is not `Visibility::HIDDEN`, so platform AX stays on for capturer-held tabs. | UNVERIFIED |
| userGesture | | `Runtime.evaluate userGesture:true` / `Input.*` set transient user activation (Blink side). UNVERIFIED in source here. | reuse |
| Multi-client | content/browser/devtools/devtools_agent_host_impl.cc:313-342 | `AttachInternal` allows multiple clients: `sessions_` is a vector, one session per client, exclusivity is not enforced. Flattened child sessions hang under a root session (TH:471-540 `Session::Attach`). The Target handler may refuse attach via `AllowInspectingTarget` (devtools_agent_host_impl.cc:318-321). | reuse |
| Per-session state conflicts | EH:1051-1066, 225-234 | Emulation, focus emulation, Network/Fetch interception are per session but several are applied to shared state (single `FocusController` bool; one device-emulation state in the RWH). Two clients doing `Emulation.*` clobber each other. | lock: one owner session per agent tab |
| Session lifetime | EH:225-229; devtools_agent_host_impl.cc:361-377 | Session detach destroys per-session handlers, releasing the focus-emulation capturer and screencast. When the last session leaves `NotifyDetached()` runs. | **override**: Koan C++ holder for the capturer |

### 9. Which targets are exposed to a CDP client

| item | file:line | behaviour | verdict |
|---|---|---|---|
| Enumeration | content/browser/devtools/devtools_agent_host_impl.cc:179-193; render_frame_devtools_agent_host.cc:293-309 | `GetOrCreateAll()` = shared/service/dedicated workers + every RenderFrameHost frame target (for all outermost WebContents, `GetAllWebContents()`) + WebContents/tab targets. Includes every profile in the process, every window, incognito windows, devtools windows, extension pages, chrome:// pages and browser WebUI. | **override**: filter |
| Attach gate | devtools_agent_host_impl.cc:313-321; render_frame_devtools_agent_host.cc:398-401, 1157-1171; content/public/browser/devtools_manager_delegate.h:84, 156 | `DevToolsManagerDelegate::AllowInspectingTarget` (per attach) and `AllowInspectingRenderFrameHost` (per frame) are the filter points; plus `DevToolsAgentHostClient::MayAttachToRenderFrameHost(rfh)` / `MayAttachToURL` (devtools_agent_host_client.h:47-51), a per-client veto. | **override** (lever: client hook `MayAttachToRenderFrameHost`, or Koan delegate patch in CDM:407-429) |
| Chrome's implementation | CDM:407-429; chrome/browser/devtools/devtools_availability_checker.cc:365-416 | `IsInspectionAllowed(profile, host)`: enterprise policy `DeveloperToolsAvailability` and URL allow/block lists, extension-specific policy, IWA rules. Nothing about incognito or Koan UI. | lock; layer Koan rules on top |
| Target types | CDM:308-333 | `browser_ui` (webui_browser), `app` (IWA), `page` (anything with a `TabInterface` or a `views::WebView`), extension types via `GetExtensionInfo`, `other`. If Koan's sidebar/toolbar/new-tab WebUIs are `WebView` contents they report as `page`: indistinguishable from tabs by type alone. | override: block by delegate or filter by `TabInterface`/Koan UI marker |
| Tab-only embedder data | CDM:367-405 | Only for `tab` targets: `tabStripIndex`, `tabActive`, `tabPinned`, `tabGroupId`. Good for locating the user's current tab. | reuse |
| Incognito / OTR | devtools_browser_context_manager.cc:54-60, 74-109; TH:1625-1651 | `Target.getBrowserContexts` returns only OTR contexts that **DevTools created**, plus the default (original) profile id; **the user's own incognito windows are not listed as contexts but their pages still appear in `getTargets` and can be attached to** (no OTR check in `IsInspectionAllowed`). `Target.createBrowserContext` makes a throwaway OTR profile (`CreateUniqueForDevTools`). | **override**: reject any target whose `BrowserContext` is OTR and not DevTools-created; also useful: use a DevTools OTR context if agent sandboxing is wanted later |
| Connection consent | CDM:540-560 | `AcceptDebugging` shows a per-connection permission dialog for the default context (remote-debugging port path). Koan's own pipe/socket channel bypasses it unless routed through here. | decide per channel |
| Chrome's built-in agent targets | content/browser/devtools/protocol/hidden_target_manager.cc | Hidden targets (above) are also enumerable. | lock |
| Extension pages / service workers | CDM:322-331, 369 | Reported with types such as `background_page`, `service_worker`; attachable unless `IsInspectionAllowed` blocks them. | override (filter) |

### 10. Gaps: what a CDP client alone cannot do on a hidden tab, and the in-browser C++ needed

1. **Persistent keep-awake.** The only CDP lever (`setFocusEmulationEnabled`) lives and dies with one session, and drags in fake focus on a boolean shared with other clients. Needed: a Koan C++ `AgentTabKeepAlive` (per agent WebContents) holding `IncrementCapturerCount(Size(), stay_hidden=?, stay_awake=false, is_activity=false)` independent of any CDP session, plus `RenderWidgetHost::Focus()` if page focus is wanted, as in chrome/browser/actor/actor_task.cc:973-994. Choose `stay_hidden=false` (page believes visible) vs `true` (page still sees `hidden`, but scheduler and compositor are awake): decision for the owner, noting the first is the actor's choice.
2. **Window-level wake.** The `VideoCaptureLock` path (web_contents_view_mac.mm:194-206) only matters when the three `kNotifyCompositorOf*OnMacOs` features are on. If the owner turns them on for power reasons, agent tabs in a covered/minimized window need that lock (it attaches only while the tab is the window's attached/active contents). For background (non-active) tabs the `HasOwnCompositor` path (browser_compositor_view_mac.mm:214-229) must be verified on a real Mac.
3. **Process priority and App Nap.** CDP cannot touch it. Reuse `RenderProcessHostImpl::OnBoostForBackgroundExecutionAdded/Removed` (render_process_host_impl.cc:3452-3464), as `HiddenTargetWebContentsObserver` does (hidden_target_manager.cc:20-74).
4. **Freezing / discarding.** Koan enables `InfiniteTabsFreezing`. `IncrementCapturerCount` does not set any `CannotFreezeReason`/`CannotDiscardReason` (those capture reasons are about media streams: discard_eligibility_policy.cc:139-142, 376-381). A frozen agent tab ignores timers and likely CDP. Needs a Koan exemption in the freezing policy for agent tabs. Discard is protected while DevTools is open (discard_eligibility_policy.cc:416-421); UNVERIFIED that a CDP attach counts as "DevTools open".
5. **Never foreground, never activate the app.** CDP offers `Page.bringToFront`, `Target.activateTarget`, `Browser.setWindowBounds` and default `Target.createTarget` which all can switch the user's tab or raise/activate the window (sections 2 and 5). Needs a method filter on the agent channel (delegate `HandleCommand` or `MayAttach...` hooks) and, ideally, a Koan variant of `CreateNavigateParams` (CTH:31-52) that uses `kNewBackgroundTab` + `kNoAction` window action for agent creates (avoids even `ShowInactive`).
6. **Agent tab placement.** CDP can create a tab in the user's most recently active window (strip entry visible, background) or a hidden detached WebContents (`hidden:true`). It cannot choose a window/group, hand a hidden target to a window, or adopt a tab for the user. Needs Koan C++ (TabStripModel insert / group assignment / detach-reattach of a WebContents).
7. **Identifying the user's current tab and front window.** `tabActive` is per window. No CDP signal for "front window" or OS focus. Use `ForEachCurrentBrowserWindowInterfaceOrderedByActivation` in C++ (as CTH:148 does) and expose a Koan-only query.
8. **Take-over.** Detection of real page input is all upstream C++ (section 7): `InputEventObserver` or `DidGetUserInteraction` + the `kFromDebugger` bit; blocking real input during an atomic action via `IgnoreInputEvents(audit_cb)`. Real user interaction with the browser UI (tab click, window focus) is not visible at RWH level and needs `TabStripModelObserver` / window activation observers. Pairing this with the CDP session needs a Koan-side event bridge (the `koan` CLI cannot see browser-side input otherwise).
9. **Target allow-list.** CDP gives no way to hide targets from a client. Needs a Koan `AllowInspectingTarget` / client `MayAttachToRenderFrameHost` filter: exclude OTR (incognito) contexts, Koan WebUI pages, DevTools windows, optionally extension pages; keep only normal tab pages of the default profile.
10. **Screenshot of the user's visible tab without relayout.** `clip`/`captureBeyondViewport`/emulation resize the real NSView on Mac (section 1). A Koan-side screenshot utility (or a rule: plain viewport only for the user's tab; use `getLayoutMetrics` + scroll stitching otherwise) is needed to avoid flicker.
11. **Visibility polyfill.** If `stay_hidden=true` is chosen and pages must see `visible`, `Page.addScriptToEvaluateOnNewDocument` is the only CDP route and is imperfect; the cleaner route is `stay_hidden=false` from C++.
12. **Unverified items to test before relying on this:** (a) BeginFrames for an unattached background tab with a capturer on Mac (HasOwnCompositor); (b) `Page.screencast`/`startScreenRecording` taking a capturer; (c) behaviour of `InsertVisualStateCallback` (wheel/touch) on a hidden compositor; (d) Accessibility/DOM geometry on never-painted tabs; (e) `ShowInactive` side effects on an already visible window and across Spaces; (f) whether a CDP attach protects from discard and whether freezing blocks `kInternalInspector` tasks; (g) `DispatchHiddenVisibilityTransitions` effective default; (h) exact `IsTrusted()` of Koan's CDP client (it controls accelerator pass-through and `fromSurface:false`).

---

# Part C: remote-debugging gating and transport


Paths are relative to `~/work/koan/chromium/src` at HEAD unless prefixed `koan:` (the Koan repo,
`/home/chaitanya/.t3/worktrees/koan/feat-agent-control`). Verdicts: **lock** / **override** / **reuse**.
UNVERIFIED = inferred, not read.

### 0. Short answers

1. The 158 "ignore the switches on the default user-data-dir" gate is compiled in only for
   `GOOGLE_CHROME_BRANDING`. Koan is not Chrome-branded (no `is_chrome_branded` in `koan:build/koan-mac.gn`,
   `koan:build/koan-release.gn`; default `false` at `build/config/chrome_build.gni:12`). So today
   `--remote-debugging-port` on `~/Library/Application Support/Koan` is not blocked. That is an accident
   of branding, not a feature to depend on. Do not use the switches for agent control anyway (section 2).
2. Best transport: a Koan-owned CDP gateway in the browser process. It listens on a unix domain socket
   (`net::UnixDomainServerSocket` + `net::HttpServer`, own WebSocket). It authenticates peers (uid check,
   then a pairing token). It attaches one **untrusted** in-process `DevToolsAgentHostClient` per agent tab.
   It exposes a Koan-defined browser-level facade (`Koan.*` plus a small allowlisted `Target.*`).
   It does not reuse `DevToolsAgentHost::StartRemoteDebuggingServer`, which is a process-wide singleton and
   would collide with `chrome://inspect`'s approval mode.
3. The strongest cheap hardening is that content already withholds domains from clients whose
   `IsTrusted()` is false (section 2c). Browser target, `Extensions`, `Browser`, `SystemInfo`, `Tracing`,
   `Target.setRemoteLocations` and `Target.exposeDevToolsProtocol` disappear for such a client.

### 1. How remote debugging is gated in 158

#### 1a. The default-user-data-dir gate

| item | file:line | behaviour | verdict |
|---|---|---|---|
| `IsRemoteDebuggingAllowed()` | `chrome/browser/devtools/remote_debugging_server.cc:161-183` | Step 1 (`:164`): local-state pref `kDevToolsRemoteDebuggingAllowed` false returns `kDisabledByPolicy`. Step 2 (`:168-181`, mac/win/linux only): `default_user_data_dir_check_enabled && is_default_user_data_dir.value_or(true)` returns `kDisabledByDefaultUserDataDir`. | reuse the policy pref; the dir check does not apply to Koan |
| Branding switch | `remote_debugging_server.cc:169-174` | `GOOGLE_CHROME_BRANDING` makes the check `constexpr true`. Otherwise it equals `g_enable_default_user_data_dir_check_for_chromium_branding_for_testing`, default false (`:50-53`), set only by `EnableDefaultUserDataDirCheckForTesting()` (`:290-295`). That function has no callers outside its definition and header (git grep). | lock (leave it off) |
| What it compares | `chrome/common/chrome_paths.cc:524-538` | `IsUsingDefaultDataDirectory()` compares `PathService DIR_USER_DATA` (after `--user-data-dir`) to `GetDefaultUserDataDirectory()`. Equal means default. `nullopt` (lookup failed) is treated as default (`.value_or(true)`). | lock |
| Koan's default dir | `chrome/common/chrome_paths_mac.mm:31-60` plus `koan:patches/chrome/common/chrome_paths_mac.mm.patch` and `koan:src/common/koan_branding.h:34` | Default dir is `~/Library/Application Support/Koan` (`kMacProductDirName`). `Info.plist` key `CrProductDirName` overrides it (`:31-38`). A Koan-only symlink or alias dir would not change the string comparison. | reuse for path discovery |
| Where gates run | `chrome/browser/devtools/remote_debugging_server.cc:335-424` | `GetInstance()` runs the allowed-pref check (`:338`). `--remote-debugging-pipe` (`:353-361`) and `--remote-debugging-port` (`:363-395`) each call `IsRemoteDebuggingAllowed`. Port 0 means ephemeral and writes `DevToolsActivePort` to `DIR_USER_DATA` (`:367-373`). The block returns `unexpected(reason)`, so no server is created. | lock |
| Message | `chrome/browser/browser_process_impl.cc:1132-1138` | stderr: "requires a non-default data directory". Called from `chrome/browser/chrome_browser_main.cc:1953` via `BrowserProcessImpl::CreateDevToolsProtocolHandler` (`browser_process_impl.cc:1111-1141`). | n/a |
| Policy `RemoteDebuggingAllowed` | `chrome/browser/policy/configuration_policy_handler_list_factory.cc:548-549`, pref registered `browser_process_impl.cc:1396` (default true) | A mandatory policy makes the pref false. Stops a running server live: `browser_process_impl.cc:1144-1161` calls `remote_debugging_server_->StopServer()` and clears `kDevToolsRemoteDebuggingEnabled`. Observer wired at `:542-545`. Policy yaml: `components/policy/resources/templates/policy_definitions/Miscellaneous/RemoteDebuggingAllowed.yaml`. | **reuse**: Koan's gateway must read `kDevToolsRemoteDebuggingAllowed` at start and observe it, so an enterprise "no remote debugging" is honoured |
| Policy `DeveloperToolsAvailability` (+ Allowlist/Blocklist) | `chrome/browser/devtools/chrome_devtools_session_base.cc:40-68`; `chrome/browser/devtools/devtools_availability_checker.cc:60-140`; `chrome/browser/devtools/chrome_devtools_manager_delegate.cc:407-429` | Profile pref `kDevToolsAvailability`. Enforced in `AllowInspectingTarget` / `AllowInspectingRenderFrameHost`, which content calls in **every** `AttachClient` (`content/browser/devtools/devtools_agent_host_impl.cc:312-322`) and on rendering-frame changes (`render_frame_devtools_agent_host.cc:1167`). Policy change force-detaches sessions (`chrome_devtools_session_base.cc:60-68`). | **reuse**: an in-process attach honours it for free, no extra code |

#### 1b. Chrome's own "allow on the default profile" path: approval mode (do not use for agent control)

| item | file:line | behaviour | verdict |
|---|---|---|---|
| Feature | `chrome/browser/devtools/features.cc:205-210` | `kDevToolsAcceptDebuggingConnections` is **enabled by default** (disabled on ChromeOS). | lock (but know it exists in Koan) |
| Pref + toggle | `browser_process_impl.cc:1397` (`kDevToolsRemoteDebuggingEnabled`, default false, local state); `chrome/browser/ui/webui/inspect/inspect_ui.cc:689-719` | The `chrome://inspect#remote-debugging` switch sets the pref. | lock |
| Start path | `remote_debugging_server.cc:399-417`, `:216-283` | With no switch given, `GetInstance` still returns a server and `StartHttpServerInApprovalModeIfEnabled` watches the pref. The port comes from an existing `DevToolsActivePort` in `DIR_USER_DATA`, else 9222 (`:194-214`). Binds 127.0.0.1 with fallback to a free port (`:115-158`). Uses `RemoteDebuggingServerMode::kWithApprovalOnly`. This path is not subject to the default-dir check (it never calls `IsRemoteDebuggingAllowed`). It checks only the allowed pref (`:236`, `:257`). | lock |
| Per-connection approval | `content/browser/devtools/devtools_http_handler.cc:866-879`, `:820-837`; `chrome/browser/devtools/chrome_devtools_manager_delegate.cc:540-560`; `chrome/browser/devtools/devtools_connection_dialog.cc` | `/json/*` returns 404 in this mode (`:605-609`). Only `/devtools/browser*` is accepted, and only after a modal "Allow / Don't allow" dialog. The browser-level guid is not required (`:866`). Active connections drive an infobar (`chrome_devtools_manager_delegate.cc:562-594`). | lock; see why below |
| Why not for Koan | same | (i) A modal per connection is unusable for a CLI that connects often. (ii) It is TCP loopback: any local uid can connect, there is no peer identity. (iii) The session is a **trusted browser-target** session (full Browser/Storage/Extensions/Fetch/IO). (iv) It uses the single global `DevToolsHttpHandler` slot (below). The CLI could read `DevToolsActivePort` (`content/public/common/content_constants.cc:69`) but this is the wrong trust model. | n/a |

#### 1c. Switches, ephemeral-port file, origin allowlist

| item | file:line | behaviour | verdict |
|---|---|---|---|
| `--remote-debugging-port` | `remote_debugging_server.cc:363-395`, `TCPServerSocketFactory` `:69-111` | Listens 127.0.0.1 (then `::1`). TCP only. | lock (not Koan's transport) |
| `--remote-debugging-pipe` | `remote_debugging_server.cc:353-361` | Starts the pipe handler (section 2b). | lock |
| `DevToolsActivePort` | `content/browser/devtools/devtools_http_handler.cc:308-318` | Written only `if (ip_address)` and a non-empty output dir. Contents: `"<port>\n<browser_guid_path>"`. A UDS server has no IP, so it is never written. | n/a |
| `--remote-allow-origins` | `content/public/common/content_switches.cc:603`; `devtools_http_handler.cc:845-864`, `:968-975` | Comma list, lower-cased. A WebSocket upgrade carrying an `Origin` header is allowed only if same-origin to the server IP, or listed, or `*`. | lock; a UDS gateway makes it moot |
| `kRemoteDebuggingAddress` | only `content/shell/common/shell_switches.h:46` and `content/shell/browser/shell_devtools_manager_delegate.cc:144-147` (content shell). No use in `chrome/` (git grep); `chrome://inspect` only displays an address (`inspect.js:1020`). | `--remote-debugging-address` does not exist for Chrome/Koan. The port is always loopback (`remote_debugging_server.cc:86-95`). | n/a |
| chrome://inspect "Discover network targets"/remote locations | `chrome/browser/devtools/chrome_devtools_manager_delegate.cc:507-538`; `chrome/browser/devtools/protocol/target_handler.cc:72-93` | `Target.setRemoteLocations` is refused to untrusted clients (`:75-78`). | lock via untrusted client |

#### 1d. What the Koan code can legitimately do

| lever | where | verdict |
|---|---|---|
| (A) Do nothing about the dir check | `remote_debugging_server.cc:172-175` | Koan is non-branded so it is off. Relying on it is fragile. If Koan ever sets `is_chrome_branded`, switches silently stop working on the default dir. Plan for that: the gateway below does not call `RemoteDebuggingServer` at all. |
| (B) Own server, own socket factory through `DevToolsAgentHost::StartRemoteDebuggingServer` | `content/public/browser/devtools_agent_host.h:144-149`; implementation `devtools_agent_host_impl.cc:206-217` | Works (Android does it, section 2a) but the handler is one global (`GetDevToolsHttpHandler()`, `devtools_agent_host_impl.cc:75-82`). Starting a second replaces the first. `RemoteDebuggingServer::StartHttpServer/StopHttpServer` (`:311-323`) call the same functions. Toggling `chrome://inspect#remote-debugging` therefore kills or replaces Koan's server. It would require Koan to lock the inspect toggle (hide `UpdateRemoteDebuggingEnabled`, `inspect_ui.cc:881-901`, or pin the pref). Also forces the stock `/json` + browser-guid + trusted browser-target model. **Not recommended.** |
| (C) Own server, not through content's handler | `net::HttpServer` + `net::UnixDomainServerSocket` directly, attaching clients via the public `DevToolsAgentHost` API | **Recommended.** No singleton, no `/json` surface, no Host/Origin heuristics, custom auth. |
| (D) Hook the check | `remote_debugging_server.cc:162` | Unnecessary; a Koan patch here only adds rebase cost (`koan:docs/REBASE.md`). |

### 2. Transports to reuse

#### 2a. `DevToolsSocketFactory` + `net::UnixDomainServerSocket`

| item | file:line | finding | verdict |
|---|---|---|---|
| Factory interface | `content/public/browser/devtools_socket_factory.h:18-29` | `CreateForHttpServer()` returns `std::unique_ptr<net::ServerSocket>`; it runs on the handler thread, not a BrowserThread (`:16`). Any `ServerSocket` subclass is fine. | reuse (only if option B) |
| UDS server | `net/socket/unix_domain_server_socket_posix.h:28-103`; compiled for all POSIX incl. macOS: `net/BUILD.gn:49,1775-1781` (`enable_unix_sockets = is_posix && !is_ios`) | `UnixDomainServerSocket(AuthCallback, use_abstract_namespace)`, `BindAndListen(path, backlog)`. Abstract namespace is Android/Linux only: `net/base/sockaddr_util_posix.cc:54-63` returns false elsewhere. On macOS use a filesystem path with `use_abstract_namespace=false`. | reuse |
| Path limit | `net/base/sockaddr_util_posix.cc:33-40` | `path_size > sizeof(sun_path)` returns `ERR_ADDRESS_INVALID` (`unix_domain_server_socket_posix.cc:75-77`). macOS `sun_path` is 104 bytes (system header, UNVERIFIED here), so max 103 chars. | see section 5 |
| Bind side effects | `unix_domain_server_socket_posix.cc:70-101` | No `unlink`, no `chmod`. `bind()` fails `EADDRINUSE` if a stale socket file exists. Mode is `0777 & ~umask`. Koan code must `unlink` after a liveness probe and `fchmod/chmod 0600` (race-free if the parent dir is 0700). | override (Koan wrapper) |
| Peer auth | `unix_domain_server_socket_posix.h:30-46`, `.cc:37-53`, `:180-192` | `AuthCallback` receives `Credentials`: on Linux/Android `pid, uid, gid`; **on macOS only `uid, gid`** (via `getpeereid`). If the callback returns false the connection is dropped and the next `accept` is tried. A raw `AcceptSocketDescriptor` is also offered (`:77`). Pid on macOS: `getsockopt(fd, SOL_LOCAL, LOCAL_PEERPID)`, precedent `base/posix/unix_domain_socket.cc:194`. | reuse for uid check |
| `net::HttpServer` over UDS | `net/server/http_server.cc:180-190` (accept loop is socket-agnostic); `chrome/browser/android/devtools_server.cc:90-105,143-157` | Precedent: Android passes a UDS factory to the stock `DevToolsHttpHandler`. Content shell does the same for Android only: `content/shell/browser/shell_devtools_manager_delegate.cc:52-81`. | reuse |
| Consequences of no IP | `http_server.cc:289-290` + `net/socket/unix_domain_client_socket_posix.cc:68-77`; `net/socket/unix_domain_server_socket_posix.cc:103-109`; `devtools_http_handler.cc:295-299,319-323` | `GetPeerAddress`/`GetLocalAddress` return `ERR_ADDRESS_INVALID`. So `request.peer` is empty, `server_ip_address_` is null, `DevToolsActivePort` and the "DevTools listening on ws://" line are not produced, and same-origin checks fail (any `Origin` header gets 403 unless allowlisted). | fine; browsers cannot reach a UDS |
| Limits | `devtools_http_handler.cc:100-101`; `net/server/http_server.cc:305-308` | Stock handler raises buffers to 256 MB send / 100 MB receive; `HttpServer` body cap 100 MB. A Koan server must set the same via `SetSendBufferSize/SetReceiveBufferSize` or large screenshots and `Page.printToPDF` streams will stall. | override |

#### 2b. `--remote-debugging-pipe` / `DevToolsPipeHandler`

| item | file:line | finding | verdict |
|---|---|---|---|
| Fixed fds | `content/public/browser/devtools_agent_host.h:71-74,156`; `devtools_agent_host_impl.cc:220-235` | Reads fd 3, writes fd 4 (mac/linux); only Windows can take `--remote-debugging-io-pipes` (`:224-232`). | lock |
| Framing | `content/browser/devtools/devtools_pipe_handler.cc:290-301,410-424` | ASCIIZ (NUL-terminated JSON) or `=cbor` mode. | n/a |
| Privilege | `devtools_pipe_handler.cc:403-405,471-473` | Attaches to the **browser target** with `AllowUnsafeOperations() == true`. Most privileged client in the tree. | lock |
| Usable for Koan? | same | The fds must be inherited at process creation, so only for a browser spawned by the CLI. Koan is launched by the user from the Dock. Cannot attach later. Also one process-wide handler (`SetDevToolsPipeHandler` is a static singleton, `devtools_agent_host_impl.cc:84-87`). | **not usable**; use as a template for an in-process client only |
| Side effect | `chrome/browser/devtools/chrome_devtools_manager_delegate.cc:194-222` | With pipe or port plus `--no-startup-window`/headless, a keep-alive keeps the app running. Irrelevant to Koan. | n/a |

#### 2c. In-process client: `AttachClient` / `DevToolsAgentHostClient`

This is the core of the recommendation. Signatures at HEAD:

```
// content/public/browser/devtools_agent_host.h
static scoped_refptr<DevToolsAgentHost> GetOrCreateFor(WebContents*);          // :87 frame/page target
static scoped_refptr<DevToolsAgentHost> GetOrCreateForTab(WebContents*);       // :94 tab target
static scoped_refptr<DevToolsAgentHost> GetForId(const std::string&);          // :83
virtual bool AttachClient(DevToolsAgentHostClient*) = 0;                       // :172
virtual bool DetachClient(DevToolsAgentHostClient*) = 0;                       // :176
virtual void DispatchProtocolMessage(DevToolsAgentHostClient*, base::span<const uint8_t>) = 0; // :182
static void AddObserver(DevToolsAgentHostObserver*);                           // :160
// content/public/browser/devtools_agent_host_client.h  (:24-91)
virtual void DispatchProtocolMessage(DevToolsAgentHost*, base::span<const uint8_t>) = 0;  // host -> client
virtual void AgentHostClosed(DevToolsAgentHost*) = 0;
virtual bool MayAttachToURL(const GURL&, bool is_webui);                       // default true
virtual bool MayAttachToRenderFrameHost(RenderFrameHost*);                     // default true
virtual bool IsTrusted();                // default TRUE (devtools_agent_host_client.cc:19)
virtual bool MayReadLocalFiles();        // default true (:25)
virtual bool MayWriteLocalFiles();       // default true (:29)
virtual bool AllowUnsafeOperations();    // default false (:39)
virtual std::optional<url::Origin> GetNavigationInitiatorOrigin();
virtual bool UsesBinaryProtocol();       // false = JSON in/out, content transcodes
virtual std::string GetTypeForMetrics(); // must be "DevTools"|"Extension"|"RemoteDebugger"|"Other"
                                         // (CHECK at chrome_devtools_session_base.cc:87-89)
```

Key facts, all verified:

| fact | file:line | consequence | verdict |
|---|---|---|---|
| Messages are JSON when `UsesBinaryProtocol()` is false; session ids for flattened child sessions are inserted/consumed by `DevToolsSession` | `content/public/browser/devtools_agent_host_client_channel.h:10-30`; `devtools_session.cc:338-377` | The gateway can forward `{id, method, params, sessionId}` JSON verbatim and filter by parsing it. | reuse |
| The stock WebSocket client does **not** override `IsTrusted` etc. | `devtools_http_handler.cc:336-406` | Every remote debugger is trusted, with file read/write allowed. This is why the stock path is too powerful. | lock (do not use) |
| Untrusted clients cannot attach to the browser target | `content/browser/devtools/browser_devtools_agent_host.cc:225-227` | An in-process untrusted client works at page level only. | reuse as a feature |
| Domains withheld from untrusted clients | `devtools_session.h:107-117` and `:245-266`; Chrome side `chrome/browser/devtools/chrome_devtools_session.cc:42-48,52-126` | Allowed for untrusted: Audits, DOM, Debugger, DeviceOrientation, Emulation, Input, Inspector, IO, Overlay, Network, Fetch, Storage, Target, Page, Tracing(handler present, internal gating), Log, WebAuthn, WebMCP. Missing: Browser, SystemInfo, Extensions (chrome side only Page/Emulation/Target/WebMCP in the untrusted whitelist, `:42-48`), PWA, Security, Cast, Ads, Autofill, Memory, ServiceWorker, BluetoothEmulation, etc. | reuse |
| Page-level behaviour changes with trust | `render_frame_devtools_agent_host.cc:465-489`; `page_handler.cc:1470-1475`; `tracing_handler.cc:799,819,1081` | `Target` becomes `kAutoAttachOnly` (no getTargets/createTarget/attachToTarget, `target_handler.cc:1100,1382,1463`); `Page.captureScreenshot fromSurface:false` refused; Tracing refuses system backend. `Target.createTarget` to `chrome-untrusted://` or `devtools://` refused (`target_handler.cc:1396-1403`). | reuse |
| Local file gates | `page_handler.cc:988-990` (`Page.navigate file://`); `network_handler.cc:4206` (`Network.loadNetworkResource file://`); `dom_handler.cc:37,47` (`DOM.setFileInputFiles`); `content/browser/devtools/protocol/browser_handler.cc:581` (`Browser.setDownloadBehavior`, written via `MayWriteLocalFiles`) | Set `MayReadLocalFiles() = false`, `MayWriteLocalFiles() = false`. | reuse |
| URL scoping | `MayAttachToURL` users: `page_handler.cc:1001` (navigate), `render_frame_devtools_agent_host.cc:822-833` (`OnNavigationRequestWillBeSent` force-detaches the session when the tab navigates to a disallowed URL), `network_handler.cc:2359-2370` (`CanAccessCookie`), `:2382` (`Network.getCookies` urls), `storage_handler.cc:455-460` (`Storage.getCookies` filter) and `:623`, `emulation_handler.cc:175` | One override, `MayAttachToURL(url, is_webui)` returning false for any scheme but http/https/about:blank (also false for `is_webui` true), simultaneously blocks navigation to `chrome://settings`, extension pages and file URLs, **filters `Network.getAllCookies`/`Storage.getCookies`** to http(s) hosts, and detaches the agent if the page lands on a forbidden URL. `MayAttachToRenderFrameHost` is the frame-level twin (`render_frame_devtools_agent_host.cc:1155-1170`). | **override** (Koan client class) |
| Precedent for restricted in-process clients | `chrome/browser/extensions/api/debugger/debugger_api.cc:1103-1138` (`MayAttachToRenderFrameHost`, `MayAttachToURL` false for WebUI, `IsTrusted`, `MayWriteLocalFiles = false`) and attach at `:1265` (`GetOrCreateFor(web_contents)`) | `chrome.debugger` is an untrusted per-tab client with an infobar. Koan's client is the same pattern with a socket on the other side. | reuse the pattern |
| Observing attach | `content/public/browser/devtools_agent_host_observer.h:15-45` (`DevToolsAgentHostAttached/Detached`, `...Created/Destroyed/Navigated`); client sees `Target.attachedToTarget` as ordinary messages in `DispatchProtocolMessage` | For an "agent connected" indicator you do not need the observer when Koan owns the client. Use the observer only to also show when **any other** client (DevTools window, extension) attaches. | reuse |
| Attach is on the UI thread | `devtools_http_handler.cc:349,385,395` (`CHECK_CURRENTLY_ON(UI)`) | Socket I/O on an IO-pump thread (as the stock handler's `Chrome_DevToolsHandlerThread`, `:76,957-959`); hop to UI for `AttachClient`/`DispatchProtocolMessage`. | reuse the pattern |
| Tab selection | `GetOrCreateFor(wc)` returns the page (frame) target. `GetOrCreateForTab(wc)` the tab target (`devtools_agent_host.h:87,94`). `Target.createTarget` default `for_tab` follows the session mode. | Use `GetOrCreateFor` first (matches what Playwright/puppeteer expect and what extensions use); revisit `Tab` targets if prerender/bfcache behaviour matters. | UNVERIFIED which is better for rAF in occluded windows |

#### 2d. `DevToolsManagerDelegate::HandleCommand` for custom methods

| item | file:line | finding | verdict |
|---|---|---|---|
| Hook | `content/public/browser/devtools_manager_delegate.h:158-163`; call site `content/browser/devtools/devtools_session.cc:395-401` | Every message of every session goes to `delegate->HandleCommand(channel, message, not_handled_callback)` first. The delegate may answer or call the callback to pass on. | override possible |
| Chrome's impl | `chrome/browser/devtools/chrome_devtools_manager_delegate.cc:294-306`; `chrome_devtools_session_base.cc:70-94` | `ChromeDevToolsSessionBase::HandleCommand` runs `dispatcher_.Dispatch(...)` over generated Chrome domains (`inspector_protocol_config.json`: Page/Browser/Target/Extensions/Emulation/...). Unknown domains fall through. A new `Koan.*` domain would need a PDL plus the generator, or a hand-written intercept before `it->second->HandleCommand`. | override only if forced |
| Verdict | | **Avoid.** With a Koan-owned gateway the `Koan.*` methods are handled in the gateway before anything reaches the agent host. No PDL, no change to `ChromeDevToolsManagerDelegate`, no patch of upstream code. | **lock** |

### 3. Threat-model facts for the stock transport and for the proxy's allow/deny list

#### 3a. Stock transport defaults (why it must not be exposed as is)

| fact | file:line | note |
|---|---|---|
| Host header must be IP or `localhost` (DNS-rebinding guard) | `devtools_http_handler.cc:129-137,478-484,532-539` | Empty Host passes. Applies to HTTP and WebSocket upgrade. |
| Origin check on WebSocket | `devtools_http_handler.cc:845-864` | Any `Origin` must be same-origin to the listener or allowlisted. Non-browser local clients send no Origin, so they pass. Same-uid and other-uid local processes can connect to a TCP port. |
| Browser websocket path guid | `devtools_http_handler.cc:949-954,881-894` | `/devtools/browser/<random uuid>` unless `IsBrowserTargetDiscoverable()` (`devtools_manager_delegate.h:172-179`; not overridden on desktop Chrome). The guid is handed out by `GET /json/version` (`:634-651`) and `DevToolsActivePort`. Anyone who can reach the port gets it. |
| Page websocket needs no secret | `devtools_http_handler.cc:896-923` | `/devtools/page/<id>` with ids from `/json/list`. |
| `/json` surface | `devtools_http_handler.cc:603-750` | `list`, `version`, `protocol`, `new` (PUT only), `activate`, `close`. Not auth-protected. `list` calls `RemoteDebuggingTargets` (all targets incl. incognito). |
| Message sizes | `:100-101` | 256 MB / 100 MB buffers. |
| Doc'd trust model | `third_party/blink/public/devtools_protocol/SECURITY.md` | Access "explicitly controlled and authorized", "browser-level features ... never granted across a trust boundary", initial access requires the user running a debugging port or pipe. A Koan allowlist proxy is the implementation of "explicitly controlled and authorized". |

#### 3b. Off-the-record contexts and existing incognito filtering

| item | file:line | finding |
|---|---|---|
| No filtering | grep of `chrome/browser/devtools` + `content/browser/devtools` for `IsOffTheRecord` finds only `devtools_window.cc:478`, `chrome_devtools_manager_delegate.cc:216` (keep-alive), `devtools_http_service_handler.cc:146` (Android) | No code hides incognito from `Target.getTargets` or `/json/list`. `Target.getTargets` returns `DevToolsAgentHost::GetOrCreateAll()` minus initial WebUI (`content/browser/devtools/protocol/target_handler.cc:1474-1485`). |
| CDP-created contexts | `chrome/browser/devtools/devtools_browser_context_manager.cc:86-116` | `Target.createBrowserContext` creates an OTR profile via `CreateUniqueForDevTools`. `GetBrowserContexts` lists only these. `GetDefaultBrowserContext` = `GetLastUsedProfileIfLoaded()->GetOriginalProfile()` (`:112-116`). `Target.createTarget` uses `ProfileManager::GetLastUsedProfile()` (`chrome/browser/devtools/protocol/target_handler.cc:114-131`; `chrome_devtools_manager_delegate.cc:446`). |
| What the proxy needs | | Resolve each `WebContents` to `Profile::FromBrowserContext(...)`. Reject if `IsOffTheRecord()` or `IsGuestSession()`, or if the profile is not the agent-enabled one. Do this when mapping targets, not in the client, because `AttachClient` has no incognito concept. |
| Foreground default | `chrome_devtools_manager_delegate.cc:442-461`, but Chrome's own `TargetHandler::CreateTarget` honours `background`/`focus` (`chrome/browser/devtools/protocol/target_handler.cc:35-55,133-140`) | `Navigate` with `kNewForegroundTab` steals the active tab. Koan's `Koan.openTab` must create tabs through Koan code (background, in the agent's tab group/space) and then `GetOrCreateFor(contents)`. |

#### 3c. CDP methods that exceed "drive a page", with proposed disposition

Ownership: "gone" = absent for an untrusted in-process client (domain not created). "gate" = reachable, so the proxy must filter.

| method | where | for untrusted page client | proxy action |
|---|---|---|---|
| `Browser.close` | `chrome/browser/devtools/protocol/browser_handler.cc:141-144` (`CloseBrowserSoon`, `chrome_devtools_manager_delegate.cc:613-621` calls `ExitIgnoreUnloadHandlers`) | gone (no Browser domain: `chrome_devtools_session.cc:118-122` needs trusted) | deny |
| `Browser.setWindowBounds` / `getWindowForTarget` | `browser_handler.cc:92,146-215` | gone | deny (Koan owns window geometry) |
| `Browser.setDownloadBehavior`, `Page.setDownloadBehavior` | content `browser_handler.cc:559-585`; `page_handler.cc:1825-1843` | Page variant exists but needs `browser_handler_`; write gated by `MayWriteLocalFiles` (`allow_set_download_behavior_`) | deny |
| `Browser.grantPermissions`, `setPermission` | content `browser_handler.cc:473-` | gone | deny, or Koan-mediated |
| `Storage.getCookies`, `Network.getAllCookies` | `storage_handler.cc:435-462`; `network_handler.cc:2391-2414` | **present** (Storage and Network are untrusted-allowed); returns the **whole profile's** cookie jar after `MayAttachToURL` filtering | deny `Storage.getCookies`/`getCookiesForUrls`-like and `Network.getAllCookies`; allow `Network.getCookies` only for http(s) URLs of the tab's own site; `setCookie(s)` per policy |
| `Storage.clearDataForOrigin`, `clearCookies`, `overrideQuotaForOrigin` | `storage_handler.cc` | present | deny by default |
| `IO.read` / `IO.resolveBlob` | `content/browser/devtools/protocol/io_handler.cc` | present (IO is untrusted-allowed) but only reads streams created by this session | allow (needed for large PDFs/screenshots) |
| `Page.printToPDF` | chrome `inspector_protocol_config.json` (`printToPDF` included) | present; output is data/stream, not a file write. With `file://` blocked by `MayReadLocalFiles` no local read. | allow |
| `Fetch.enable` + `continueRequest`/`fulfillRequest` | content `FetchHandler` (untrusted-allowed) | present; sees headers incl. Authorization/cookies of the tab | gate: allow only with explicit `Koan` capability; log use |
| `Network.setRequestInterception`, `Network.enable` | content | present | allow Network.enable; deny interception |
| `Extensions.loadUnpacked`, `triggerAction`, `*StorageItems`, `uninstall` | `chrome/browser/devtools/protocol/extensions_handler.cc:168,229,269,311` | **gone** for untrusted. Note: for **any trusted browser-target session** (including stock remote-debugging) `allow_loading_extensions_` is true (`chrome_devtools_session.cc:101-106`), so stock transport can load arbitrary unpacked extensions from a path. | deny (defence in depth) |
| `SystemInfo.getInfo`, `getProcessInfo` | `system_info_handler.cc` (chrome); content `SystemInfoHandler` | gone | deny |
| `Target.setRemoteLocations` | `chrome/.../target_handler.cc:72-93` | refused to untrusted | deny |
| `Target.exposeDevToolsProtocol` | content `target_handler.cc:1328-1365` | `kBrowser` mode only | deny |
| `Target.createBrowserContext`/`disposeBrowserContext`/`attachToBrowserTarget` | `devtools_browser_context_manager.cc:86-` | browser-only | deny; Koan decides context |
| `Page.navigate` / `Target.createTarget` URL | `page_handler.cc:975-1005`, `target_handler.cc:1396-1403` | gated by `MayAttachToURL` plus scheme rules | enforced by client override |
| `Runtime.evaluate`, `Page.addScriptToEvaluateOnNewDocument`, `Input.*`, `DOM.*` | untrusted-allowed | page-level JS runs with the tab's own credentials. This is the intended capability and cannot be limited to less than "can act as the user on that origin". | allow; this is the risk the user accepts with agent control |
| `Emulation.setUserAgentOverride`, `Emulation.setDeviceMetricsOverride` | `emulation_handler.cc:175` | present | allow |
| `Tracing.*`, `Memory.*`, `Overlay.*` | content | Tracing handler exists for untrusted (`devtools_session.h:262`) but system backend refused (`tracing_handler.cc:819`); `Memory` gone | deny `Tracing.*` |
| `DOM.setFileInputFiles` | `dom_handler.cc:37,47` | refused when `MayReadLocalFiles=false` | file uploads then need a Koan-mediated method (user-approved paths) |

Disposition summary: implement the allowlist as deny-by-default per domain (`Page`, `Runtime`, `DOM`, `Input`, `Network` (subset), `Emulation` (subset), `Log`, `Inspector`, `Target.setAutoAttach` for children, `IO`), with the untrusted client as the lower layer and the gateway's method table as the upper layer. Every denied call gets a CDP error response, never a silent drop.

### 4. Koan integration points (koan: repo)

| need | location | notes |
|---|---|---|
| Toggle pref (browser-wide, local state, like `chrome://inspect`'s) | register in `koan:src/browser/koan_prefs.cc:51` (`RegisterLocalStatePrefDefaults`), declare in `koan:src/browser/koan_prefs.h` near the look prefs; the patch that calls it is `koan:patches/chrome/browser/prefs/browser_prefs.cc.patch:17` | Local state, not profile prefs: the gateway is a browser-process singleton. Same split already used for `kLookStyle` (`koan_prefs.cc:52-56`). Suggested names: `koan.agent_control.enabled` (bool, default false), `koan.agent_control.paired_clients` (dict: name, id, token hash, created, last seen), `koan.agent_control.profile` (string). Also read `kDevToolsRemoteDebuggingAllowed` at start. |
| Settings row | `koan:src/browser/ui/webui/koan_settings_rows.h:20-24` ("add a Row to kRows ... in koan_settings_rows.cc"), `Row.local_state = true` (`:80`), `depends_on` (`:69`), `note` (`:84`) | Add a `kCheckbox` row in a new group (e.g. "Agent control") plus a `note` warning ("lets local tools drive tabs"). The list of paired clients and a "Revoke all" button are not expressible as a `Row`. Needs a small `koan_settings_handler.cc` addition (`koan:src/browser/ui/webui/koan_settings_handler.cc`). |
| Indicator | `koan:src/browser/ui/views/koan_toast.h:40-61` (`KoanToast::Show`/`ShowWithAction`, 2 s pill at the top corner) for "Agent connected / disconnected"; `koan:src/browser/koan_tab_hooks.h`/`koan:src/browser/ui/views/koan_url_bar.h:56-78` for per-tab marks near the URL pill; `koan:src/browser/ui/views/koan_sidebar_tabs.cc` for a dot on agent tabs | A toast is transient. A persistent "agent is connected" marker should live on the URL pill (so it is visible in compact mode, cf. #163 "in-use dot") and per agent tab. Upstream analogue to copy semantically: `ExtensionDevToolsInfoBarController` (`debugger_api.cc:1163-1200`) and `DevToolsRemoteServerInfobarDelegate` (`devtools_remote_server_infobar_delegate.cc`). |
| Pairing dialog | none yet | Model on `DevToolsConnectionDialog` (`chrome/browser/devtools/devtools_connection_dialog.cc:60-100`, a `ui::DialogModel`). |
| Where the gateway lives | new `koan:src/browser/agent/` target, created from a Koan post-startup hook | Wire into `BrowserProcessImpl` lifetime through the same kind of one-line Koan hook patch used for prefs; the gateway must be torn down before profile manager shutdown (cf. `remote_debugging_server.cc:299-304`). |
| Existing CDP scripts | `koan:scripts/cdp_eval.mjs`, `koan:scripts/perf/cdp.mjs`, Mac harness `koan:scripts/mac/README.md:5-8` | They use `--remote-debugging-port` with a test profile. Keep as is; they are a dev path and are not affected by the gateway. |

### 5. macOS specifics

| item | detail | verdict |
|---|---|---|
| Socket location | Primary: `<user-data-dir>/AgentControl/agent.sock` with `AgentControl/` mode 0700, owned by the user. Default path length is `7 + len(user) + 34 + 10 + 11`, about 80 chars for `chaitanya`. It breaks for `--user-data-dir` under a deep path (e2e). Fallback when `path.size() > 103`: `/tmp/koan-<uid>/<8-hex-of-hash(user-data-dir)>.sock`, dir created 0700 and verified `lstat` uid == getuid() and not a symlink. Avoid `$TMPDIR` (`/var/folders/xx/.../T/` already about 49 chars). Avoid `chdir` + relative path tricks (process-global, unsafe in a browser). | override (Koan code) |
| Discovery file | `<user-data-dir>/AgentControl/agent.json` 0600: `{ "socket": "...", "pid": N, "version": "...", "profile": "..." }`. CLI resolves the dir as `~/Library/Application Support/Koan` (hard-coded default, `koan_branding.h:34`) or `$KOAN_USER_DATA_DIR`/`--user-data-dir`; validates pid is alive and `connect()` succeeds before trusting the file. | Koan |
| Permissions | Create dir 0700, then bind, then `chmod(path, 0600)`. macOS honours file permissions on `connect()` for socket files (UNVERIFIED for macOS 27); the directory mode is the reliable control because path traversal is always checked. | Koan |
| Peer check | `UnixDomainServerSocket::AuthCallback` (`unix_domain_server_socket_posix.h:46`) with `credentials.user_id == geteuid()`. On macOS there is no `process_id` in `Credentials` (`:32-38`), so take the pid with `LOCAL_PEERPID` (precedent `base/posix/unix_domain_socket.cc:194`) via `AcceptSocketDescriptor` if the dialog should show the peer's executable path. Optional hardening: verify the peer's code signature with Security.framework (`SecCodeCopyGuestWithAttributes` by pid, Team ID check). UNVERIFIED feasibility; not required for v1. | reuse |
| Single owner | Koan has one browser process per user data dir (process singleton), so one socket per dir; a second launch forwards to the first. A crashed browser leaves a stale socket: unlink after a failed `connect()` probe. | Koan |
| Child processes | Helpers are spawned with `POSIX_SPAWN_CLOEXEC_DEFAULT` (`base/process/launch_mac.cc:270`), so the listener fd is not inherited. | reuse |
| Pairing token storage | Prefer a **file** (0600) for v1: the CLI is a plain binary, not Koan.app. The Keychain item Koan already uses (`koan:src/common/koan_branding.h:42-43`, `components/os_crypt/common/keychain_password_mac.mm`, patched) is ACL-bound to Koan.app's code signature, so the CLI could not read it without a prompt or an explicit trusted-application ACL (needs both binaries signed with the same identity; UNVERIFIED). Browser side stores only the token's SHA-256 + name + created time in the paired-clients pref; the raw token lives in the CLI's `~/.koan/clients/<name>.token` (0600). A same-uid attacker who reads that file can already read the profile files, so the file does not lower the bar; the Keychain would only matter against a different-uid or sandboxed reader. | override: file first, Keychain later |

### 6. Recommended transport architecture

```
koan CLI --UDS(0600 in 0700 dir)--> KoanAgentGateway (browser process)
   Koan.pair / Koan.authenticate           |
   JSON frames over HTTP/WebSocket         |  UI thread
                                           v
   AgentSession (per connection)  --- allowlist table, Koan.* handlers, target facade
        |  one KoanAgentClient per agent tab
        v
   content::DevToolsAgentHost::GetOrCreateFor(contents)->AttachClient(client)
```

Classes (new, `koan:src/browser/agent/`; none needs an upstream patch except the startup hook and the prefs/Settings row):

* `class KoanAgentGateway` (browser-process singleton, UI thread owner). Thread `Koan_AgentIO` with `MessagePumpType::IO` (as `devtools_http_handler.cc:957-959`). On it: `net::HttpServer(std::make_unique<net::UnixDomainServerSocket>(auth_cb, /*use_abstract_namespace=*/false), delegate)`, `BindAndListen(path, 10)`. `auth_cb` = `[](const Credentials& c){ return c.user_id == geteuid(); }`. Implements `net::HttpServer::Delegate`: only `OnWebSocketRequest` accepts, path `/koan/v1`; any HTTP request gets 404. Set 256/100 MB buffers (`:100-101`). Honour `kDevToolsRemoteDebuggingAllowed` (stop on policy flip like `browser_process_impl.cc:1144-1161`).
* `class KoanAgentSession` (one per WebSocket). State machine: `kUnauthenticated` accepts only `Koan.pair {clientName}` and `Koan.authenticate {token}`; everything else gets an error and a close after N failures. Pairing shows a modal (clone of `DevToolsConnectionDialog`) and mints a 256-bit token (`base::RandBytes`); store the hash in the paired-clients pref. `Koan.authenticate` compares in constant time (`crypto::SecureMemEqual`).
* `class KoanAgentClient : public content::DevToolsAgentHostClient` (per agent tab): `IsTrusted() = false`, `MayReadLocalFiles() = false`, `MayWriteLocalFiles() = false`, `AllowUnsafeOperations() = false`, `UsesBinaryProtocol() = false`, `GetTypeForMetrics() = "Other"`, `MayAttachToURL(url, is_webui) = !is_webui && (url.SchemeIsHTTPOrHTTPS() || url == about:blank)`, `MayAttachToRenderFrameHost` rejects frames whose profile is OTR/guest or not the agent profile. `DispatchProtocolMessage` forwards agent-host events to the session; `AgentHostClosed` emits `Inspector.detached`-equivalent (cf. `devtools_http_handler.cc:367-381`).
* `class KoanAgentTargetRegistry`: maps `target id`/`WebContents` to "agent-owned" (created by `Koan.openTab`) or "observed" (the user's current tab, only after the user opted in via the takeover UI). Implements the browser-level facade: `Koan.listTabs`, `Koan.openTab {url, background:true}` (via Koan tab code, not `Delegate::CreateNewTarget`), `Koan.attach {targetId}` -> returns a `sessionId` to use in flattened-CDP frames, `Koan.detach`, `Koan.pause/resume` (user input pauses the agent: the session stops dispatching mutating calls). Filters out OTR, guest, WebUI, extension pages, DevTools windows, and tabs not shared. Emits `Koan.targetCreated/Destroyed`, and a synthetic `Target.attachedToTarget` so standard CDP clients can still use `sessionId` multiplexing.
* Message routing: parse each inbound JSON frame (`base::JSONReader`), read `method` and optional `sessionId`. If method starts with `Koan.`, handle in the session. Else find the `KoanAgentClient` for `sessionId`, check method against `kAllowedMethods` (domain-level allow + per-method deny list from section 3c), then `agent_host->DispatchProtocolMessage(client, bytes)`. Responses and events come back through `KoanAgentClient::DispatchProtocolMessage` and are written to the WebSocket after redacting any denied-domain events.

Why this and not B/C:
* Reusing `StartRemoteDebuggingServer` collides with `chrome://inspect`'s approval mode (single global handler) and gives a trusted browser-target session.
* Pipe: not attachable to a running browser.
* A trusted browser-level proxy (`DevToolsAgentHost::CreateForBrowser`, `devtools_agent_host.h:120`) is the compatibility fallback if Playwright/puppeteer must run unmodified. It gets the full `Target` domain for free but leaves a single allowlist bug between the CLI and `Extensions.loadUnpacked`/`Browser.close`/cookies for all profiles. Prefer it only for a dev flag.

Not solved / open:
1. Third-party CDP libraries expect browser-level `Target.*`; the facade must emulate `Target.setAutoAttach`/`attachedToTarget` carefully (flattened sessions). A compatibility shim in the CLI is simpler than server-side emulation.
2. `Network.getAllCookies` and `Storage.getCookies` hit the profile-wide jar; the only server-side bound is `MayAttachToURL` host filtering (all http(s) cookies pass). If agents must not read cookies for other sites, deny these methods outright and offer a Koan method that returns cookies for the tab's site only.
3. A page-level agent can always exfiltrate what the page can read. Per-origin scoping is policy in the gateway, not in CDP.
4. Frame target vs tab target (`GetOrCreateFor` vs `GetOrCreateForTab`) and rAF/timers under occlusion belong to the other audits; transport choice does not constrain them.
5. User-input pause semantics (requirement 2) are implementable in the gateway but depend on a signal from Koan's tab/input code. Not designed here.

### 7. Risk list

| # | risk | severity | mitigation |
|---|---|---|---|
| 1 | Any same-uid process can connect to the socket | high | 0700 dir, 0600 socket, `getpeereid` uid check, pairing token, per-connection modal on first pair, kill switch (pref off closes socket + sessions) |
| 2 | Gateway allowlist bug exposes a trusted session | high | Untrusted in-process client as lower layer (domains genuinely absent); never create a trusted client |
| 3 | Cookie exfiltration through `Network.getAllCookies`/`Storage.getCookies` | high | Deny; per-site Koan method; scheme filter in `MayAttachToURL` |
| 4 | Agent reaches incognito/guest/WebUI/extension tabs | high | Resolve every target through the registry; reject OTR/guest; `MayAttachToURL` drops WebUI and chrome-extension |
| 5 | Stale `chrome://inspect` toggle replaces Koan's server | medium if option B used, none for recommended design | Don't use `StartRemoteDebuggingServer` |
| 6 | Enterprise policy `RemoteDebuggingAllowed=false` ignored | medium | Read the pref and observe; stop the gateway on change (`browser_process_impl.cc:1144-1161` model) |
| 7 | `sun_path` overflow with deep user-data dirs | low | Fallback short path under `/tmp/koan-<uid>/` (verified owner, no symlink) |
| 8 | Stale socket or lock after crash | low | `connect()` probe, then `unlink` |
| 9 | Upgrades: `DevToolsAgentHostClient` flags change meaning between milestones (e.g. new `IsTrusted` checks, `IsDomainAvailableToUntrustedClient` list edits at `devtools_session.h:245-266`, `chrome_devtools_session.cc:42-48`) | medium | Rebase checklist item in `koan:docs/REBASE.md`; a browser test per denied domain |
| 10 | `Page.navigate` race: forbidden-URL detach is on `OnNavigationRequestWillBeSent` (`render_frame_devtools_agent_host.cc:822-833`), after the request is queued | low | Acceptable; subresource reads are page-level anyway |
| 11 | `Target.createTarget` is foreground by default | medium (UX, requirement 1) | Create tabs from Koan code, background |
| 12 | Gateway on wrong thread (`DevToolsAgentHostClient` is UI-only) | medium | IO thread for sockets only; one hop per frame |
| 13 | Branding flip enables the stock default-dir gate, breaking switch-based dev flows | low | Dev flows use `--user-data-dir`; gateway independent |
| 14 | `AllowInspectingTarget` returns false (policy) so `AttachClient` fails silently | low | Check the return value (`devtools_http_handler.cc:350` has a TODO for the same) and surface an error to the CLI |
| 15 | Large frames (screenshots, PDFs) stall at default `HttpServer` buffers | low | Set 256/100 MB buffers; stream via `IO.read` |

---

# Part D: agent-browser


Sources: README and docs at https://github.com/vercel-labs/agent-browser (`docs/content/docs/cdp-mode.mdx`, `configuration.mdx`, `plugins.mdx`), plus Rust sources at `cli/src/native/...` on `main`, read from raw.githubusercontent.com as of 2026-11-07. Nothing was installed or run. Method names below come from grepping the downloaded source. Where I only know a behaviour from the docs, or could not see the code path, I mark it UNVERIFIED.

### 1. Architecture

- It is a native Rust CLI and a Rust daemon. It is **not** Playwright and **not** Node. README: "native Rust CLI", and the daemon controls the browser "over the Chrome DevTools Protocol". AGENTS.md: "The browser automation daemon lives in `cli/src/native/`, which also holds the CDP client". The Node/pnpm part is only the docs site and the dashboard package.
- The CDP client is hand-written (`cli/src/native/cdp/client.rs`) on `tokio-tungstenite`. It sends JSON with an incrementing `id` and keeps a `pending` oneshot map. Every command has a 30 s timeout, with WebSocket pings every 30 s and TCP keepalive. Events go to a private per-`sessionId` route first, then method taps, then a broadcast (capacity 4096).
- Daemon model: the CLI is a thin client. The first command starts a background daemon for the `--session` name (default `default`). Each session gets its own daemon and browser, and the daemon idles out after 1 h (`AGENT_BROWSER_IDLE_TIMEOUT_MS`, default 3600000).
- CLI to daemon IPC is a Unix socket (`<socket dir>/<session>.sock`) plus a `.pid` file. On Windows it is TCP on 127.0.0.1 with a `.port` file (`cli/src/connection.rs`, `native/daemon.rs`). The socket dir is chosen as `AGENT_BROWSER_SOCKET_DIR` > `XDG_RUNTIME_DIR` > `~/.agent-browser` > tmpdir. This is the daemon's own socket and has nothing to do with the CDP link.
- It also supports Lightpanda and Obscura via `--engine`, an iOS Simulator path over WebDriver, and cloud providers.

### 2. Attaching to an existing browser

Commands and flags (`cdp-mode.mdx`):

```
google-chrome --remote-debugging-port=9222
agent-browser connect 9222
agent-browser --cdp 9222 snapshot
```

- `--cdp <port | ws:// | wss:// | http(s)://>`. The doc says `--cdp` accepts "either a port number or a `ws://` or `wss://` URL". `resolve_cdp_url` in `native/browser.rs` also accepts `http(s)://`.
- `--cdp-headers '<json object>'` / `AGENT_BROWSER_CDP_HEADERS`. From the docs: "pass a JSON object of string values ... separate from `--headers`". In `configuration.mdx` the table lists it as "JSON object of headers for CDP discovery and the WebSocket handshake, such as `Authorization`". It has no config-file key.
- `--auto-connect` / `AGENT_BROWSER_AUTO_CONNECT=1`. See the DevToolsActivePort notes below.
- `--profile` / `AGENT_BROWSER_PROFILE` is a Chrome profile name or directory. It applies to local launches. Its interaction with `--cdp` is UNVERIFIED. `--session`, `--pin-tab`, `--allowed-domains` and `-p/--provider` also exist.

How the endpoint is resolved (`native/browser.rs::resolve_cdp_url`):

- **A `ws://` or `wss://` URL is used verbatim, with no HTTP request at all**: `if input.starts_with("ws://") || input.starts_with("wss://") { return Ok(input.to_string()); }`. So a URL like `ws://127.0.0.1:PORT/devtools/browser/<id>?token=...` connects straight to the websocket. A token in the query string is kept.
- A bare port, or an `http(s)://host:port` URL, goes through `discover_cdp_url_with_headers` (`cdp/discovery.rs`). It tries three steps in order: `GET /json/version` (`webSocketDebuggerUrl`), then `/json/list` (prefers the `browser` target), then a direct WebSocket to `/devtools/browser` with `Browser.getVersion` id 1. The code comment says the last one is for Chrome 136+ UI-based debugging, which has no HTTP endpoints. `rewrite_ws_host` swaps Chrome's advertised host:port for the one actually reached. The query string is forwarded (`append_query`).
- `https://` input discovers over https/wss with no plaintext fallback. The docs say "For `https`/`wss` URLs, discovery stays encrypted with no fallback." A test named `secure_discovery_never_sends_headers_in_plaintext` covers this.
- `http(s)://host` with no port and an empty path is treated as a provider endpoint. The scheme is flipped to ws/wss and it connects directly, without probing :9222.
- **Authorization header / token in the URL: both work.** Headers from `--cdp-headers` go into the discovery requests and the WebSocket handshake (`CdpClient::connect_with_headers` inserts them into the handshake request). They are not forwarded across origins on redirects. A token in the URL query survives, as above.
- **Unix sockets: not supported.** `client.rs` only handles `MaybeTlsStream::Plain` and `Rustls`. Anything else returns `"Unsupported CDP stream type"`, and `resolve_cdp_url` only accepts ws/http/port. UNVERIFIED for `ws+unix://` forms, but nothing in the tree parses them.
- Before connecting, `connect_cdp_when_listening` polls the TCP port for up to 2 s (`CDP_PORT_WAIT`) while it refuses connections. This only applies when the URL has an explicit port (`cdp_port_address`). A `ws://` URL with a port gets this TCP probe.
- **DevToolsActivePort** is read only by `--auto-connect` (`cdp/chrome.rs::auto_connect_cdp`). It loops over `get_chrome_user_data_dirs()` (Chrome, Canary, Chromium and Brave default dirs, so not a Koan profile dir) and calls `read_devtools_active_port` (line 1 is the port, line 2 is the ws path). It builds `ws://127.0.0.1:<port><ws_path>` and verifies it with `Browser.getVersion`. If the handshake hangs, it reports "waiting for remote-debugging approval" (Chrome 144+ prompt). Otherwise it falls back to HTTP discovery, then probes ports 9222 and 9229. There is no way to point it at a custom DevToolsActivePort path (UNVERIFIED beyond what I read in `get_chrome_user_data_dirs`).

### 3. CDP methods used (grep of `cli/src/native/*.rs`, so it is a superset of any single command)

- **Target**: `setDiscoverTargets`, `getTargets`, `getTargetInfo`, `createTarget`, `attachToTarget` (`flatten: true`), `detachFromTarget`, `setAutoAttach` (`autoAttach`, `waitForDebuggerOnStart: true`, `flatten: true`), `activateTarget`, `closeTarget`, and the events `targetCreated`, `targetInfoChanged`, `targetDestroyed`, `attachedToTarget`, `detachedFromTarget`. Also `Target.createBrowserContext`, only in `window new` (`actions.rs::handle_window_new`).
- **Page**: `enable`, `navigate`, `reload`, `getFrameTree`, `bringToFront`, `captureScreenshot` (`fromSurface: true`), `printToPDF`, `handleJavaScriptDialog`, `addScriptToEvaluateOnNewDocument` (and the remove call), `createIsolatedWorld`, `setDocumentContent`, `startScreencast` / `stopScreencast` / `screencastFrameAck`. Events: `loadEventFired`, `domContentEventFired`, `frameNavigated`, `navigatedWithinDocument`, `frameDetached`, dialog and download events.
- **Runtime**: `enable`, `evaluate`, `callFunctionOn`, `releaseObject`, `addBinding`, `runIfWaitingForDebugger`, and the events `consoleAPICalled` and `exceptionThrown`.
- **Accessibility / DOM**: `Accessibility.enable` and `Accessibility.getFullAXTree`. `DOM.enable`, `describeNode`, `resolveNode` and `setFileInputFiles`. Also `Page.getFrameTree` for frame identity. I saw no `DOM.getDocument` / `querySelectorAll` in the grep for the snapshot path, but the snapshot agent report did list them. Treat DOM.getDocument / DOM.querySelectorAll as likely but UNVERIFIED.
- **Input**: `dispatchMouseEvent`, `dispatchKeyEvent`, `dispatchTouchEvent`, `insertText`.
- **Network**: `enable`, `getAllCookies`, `getCookies`, `setCookies`, `clearBrowserCookies`, `setExtraHTTPHeaders`, `emulateNetworkConditions`, `getResponseBody`. Plus **Fetch**: `enable`, `requestPaused`, `continueRequest`, `fulfillRequest`, `failRequest`, `authRequired`, `continueWithAuth`, `disable`. These serve `network route`, `--headers`, `--allowed-domains` and proxy auth.
- **Emulation**: `setDeviceMetricsOverride`, `setUserAgentOverride`, `setEmulatedMedia`, `setTimezoneOverride`, `setLocaleOverride`, `setGeolocationOverride`. **Browser**: `getVersion`, `getWindowForTarget`, `setContentsSize`, `grantPermissions`, `setDownloadBehavior`, `close`. **Other**: `Security.setIgnoreCertificateErrors`, `DOMStorage.setDOMStorageItem`, `WebMCP.*` (optional), `Inspector.detached`.
- Not used: `Storage.*`, `Emulation.setFocusEmulationEnabled`, `Target.disposeBrowserContext`, `Target.getBrowserContexts`. Cookies go through `Network.*` on a page session, so they act on that page's browser context. Not Playwright, so no `connectOverCDP` requirements apply.

What attaching actually needs (`BrowserManager::connect_cdp_inner` then `discover_and_attach_targets`): `Target.setDiscoverTargets {discover:true}`, `Target.getTargets`, then `Target.attachToTarget {flatten:true}` for **every** target of type `page` or `webview` whose URL is not chrome-internal (`should_track_target`). If there are none, it does `Target.createTarget about:blank`. Then it probes for a live renderer via `Runtime.evaluate "1"` and calls `Page.enable`, `Runtime.enable` and `Network.enable` on the chosen tab, and `Target.setAutoAttach` (flatten) per session, best effort. A hard failure of any `Page.enable`, `Runtime.enable`, `Network.enable`, `getTargets` or `attachToTarget` closes the connection and fails the connect.

### 4. Features and background-tab sensitivity

- `snapshot` (AX tree with `@e1` refs, mapped by `backendDOMNodeId`, durable per frame/session, iframes recursed and cross-origin ones use their own session), `-i` interactive only, `find`, `get`, `is`, `read`.
- `click`, `dblclick`, `fill`, `type`, `press`, `hover`, `drag`, `scroll`, `upload`, and `mouse` / `keyboard`. All of these use `Input.dispatch*` or `insertText`.
- `tab` (new, switch, close, labels), `frame`, `dialog`, `window new`.
- `screenshot` (`Page.captureScreenshot`, `fromSurface`), `pdf`, `record` (screencast piped to ffmpeg), `stream` (screencast over a local WebSocket, `AGENT_BROWSER_STREAM_PORT`), `dashboard`, `diff`, `trace`, `profiler`, `console`, `errors`.
- `cookies`, `storage`, `state save/load` (cookies via `Network.getAllCookies`, local/sessionStorage via `Runtime.evaluate`; load uses `Network.setCookies` and `DOMStorage.setDOMStorageItem`), `network route` / `--headers` (Fetch), `auth` vault, `profiles`, `--session`, `--pin-tab`.
- `Page.bringToFront` appears in exactly three places (`browser.rs` lines near 2064 and 2275, `actions.rs` ~3981): inside `tab switch` (errors ignored), the `bring_to_front` method, and `--auto-connect`'s `open_fresh_tab_for_auto_connect`. The plain `--cdp` path does not call it, and `screenshot.rs` does not.
- No `setFocusEmulationEnabled`, and I found no rAF or `visibilityState` waiting. `wait` uses lifecycle events and `Runtime.evaluate` polling. UNVERIFIED: whether screenshot or screencast on a hidden tab stalls is a Chromium behaviour. `Page.captureScreenshot fromSurface` and `startScreencast` need a produced compositor frame, so they are the likeliest calls to hang on an occluded or backgrounded tab. `Input.*` and `Runtime.evaluate` do not.
- It has liveness handling for discarded tabs: a `Runtime.evaluate "1"` probe, `Target.activateTarget` to revive, and a "wedged tab" report. For an attached browser it never closes or replaces the user's tab (`kept`).

### 5. Own tabs, contexts and graceful failure

- `tab new` is `Target.createTarget {url}` with **no** `browserContextId`, so the new tab lands in the browser's default context. `--auto-connect` always opens a fresh tab and calls `Page.bringToFront` on it. Plain `--cdp` adopts the existing tabs and only creates one when none exist. The first live page becomes the active tab. With `--pin-tab` a session binds to its own tab and never adopts others.
- `window new` is the only place that creates a browser context (`Target.createBrowserContext`). It is never disposed there.
- `Browser.close` is sent **only** for a browser it launched itself. For attached browsers it just closes the websocket: `"For external connections (--auto-connect, --cdp) we just disconnect without shutting down the user's browser."` So a denied `Browser.close` is a non-issue.
- `Browser.getVersion` is used as the liveness check (3 s) and by the outdated-browser product check. A denial would make the connection read as dead (`is_connection_alive`), so allow it.
- `Browser.setDownloadBehavior`, `Browser.grantPermissions`, the `Emulation.*` calls and `Browser.setContentsSize` are issued on demand, and errors come back as that command's error. Emulation failures at tab creation are ignored. The default-on set of domains (`Page`/`Runtime`/`Network.enable`) is hard-fail. `WebMCP.enable` and the per-session `Target.setAutoAttach` are best effort.
- `Target.setAutoAttach` at the browser level (no sessionId) is sent only when network controls are installed (`--allowed-domains`, `network route`, proxy auth). It requires a non-direct connection.

### 6. Config / env surface and extension points

- Resolution order: `~/.agent-browser/config.json`, then `./agent-browser.json` (allowlisted keys only; `cdp`, `autoConnect`, `headers`, `provider`, `profile`, `session` are ignored there with a warning), then `AGENT_BROWSER_*` env, then flags. `--config` / `AGENT_BROWSER_CONFIG` loads one file only.
- Relevant env: `AGENT_BROWSER_CDP`, `AGENT_BROWSER_CDP_HEADERS`, `AGENT_BROWSER_AUTO_CONNECT`, `AGENT_BROWSER_PROVIDER`, `AGENT_BROWSER_PROFILE`, `AGENT_BROWSER_SESSION`, `AGENT_BROWSER_PIN_TAB`, `AGENT_BROWSER_SOCKET_DIR`, `AGENT_BROWSER_DEFAULT_TIMEOUT` (25000), `AGENT_BROWSER_PLUGINS`.
- Plugins (`plugins.mdx`, `cli/src/plugins.rs`): a plugin is a local executable that speaks JSON on stdin and stdout, protocol `agent-browser.plugin.v1`. Declaring `"capabilities": ["browser.provider"]` makes it a browser provider, selected with `agent-browser --provider <name>`. On `browser.launch` it returns `{ "browser": { "cdpUrl": "ws://127.0.0.1:9222/devtools/browser/session", "directPage": false, "metadata": {...}, "cleanup": {...} } }`. `cdpUrl` also accepts the aliases `wsUrl`, `connectUrl` and `cdp_url`. The doc says nothing about custom headers on the provider path, but `attach_provider_cdp` takes an optional `ws_headers`, so header support there is UNVERIFIED. A URL with the token in its query string needs no headers anyway. `directPage: true` means the websocket IS the page session and skips every browser-level `Target.*` call, so it does not fit Koan's multi-tab use.
- Built-in providers (Browserbase, Browserless, Browser Use, Kernel, AgentCore) all reduce to a remote CDP URL. There is also a `remote-agent-browser` provider doc that I did not read.

### Fit with a Koan token-protected unix-socket/loopback CDP endpoint

**Works out of the box**
- A loopback TCP endpoint that speaks raw CDP over WebSocket, reached as `agent-browser --cdp "ws://127.0.0.1:PORT/devtools/browser/<id>?token=<secret>" ...`. The ws:// URL is used as-is with no `/json/version` and no HTTP server, so a ws-only endpoint is fine. The token-in-URL form keeps the query string.
- An `Authorization` (or any) header is also supported, via `AGENT_BROWSER_CDP_HEADERS='{"Authorization":"Bearer ..."}'`, sent on the websocket upgrade. Prefer the env var so the token stays out of argv. Use `--session` to keep one daemon per Koan window or agent, and `--pin-tab` if several sessions share the browser.
- If the endpoint also serves `/json/version` and `/json/list`, a plain `--cdp <port>` works too, but that is not needed.
- Attached-browser teardown is safe: no `Browser.close`, and no closing of the user's tabs on timeouts.
- Everything in section 3 runs per `sessionId` over flat sessions, so the endpoint needs flattened target sessions.

**Needs a relay or a Koan-side change**
- **Unix socket:** the client cannot dial a unix socket, and there is no ws+unix scheme. A local relay is required: a loopback TCP-to-unix bridge (for example `socat TCP-LISTEN:PORT,bind=127.0.0.1,fork UNIX-CONNECT:/path.sock`). Note that the bridge itself widens access to any local process. Keep the token check in the Koan endpoint, or put a small token-injecting relay in front of it.
- **Required methods:** the endpoint must not reject `Target.setDiscoverTargets`, `Target.getTargets`, `Target.attachToTarget {flatten:true}`, `Target.createTarget`, `Target.setAutoAttach`, `Target.activateTarget` / `closeTarget`, `Browser.getVersion`, and per-tab `Page.enable`, `Runtime.enable`, `Network.enable`. The connect flow fails hard if the three domain enables, `getTargets` or `attachToTarget` error out.
- **Tabs:** it attaches to **all** page-type targets at connect, and `tab new` and `--auto-connect` create tabs in the default context. If Koan wants the agent confined to one tab, it should expose only that target, or the client should use `--pin-tab`.
- **Hidden tabs:** if Koan backgrounds the agent's tab, `screenshot`, `record` and `stream` (compositor-dependent) are the risk. The rest of the interaction path (`snapshot`, `click`, `type`, `eval`, `cookies`) is expected to keep working. All of this is UNVERIFIED without a live test.
- **`--profile` and `--cdp`:** do not combine them. The effect is UNVERIFIED.
- **If the token must go in a header and the cloud-provider path is wanted:** write a plugin provider returning the `cdpUrl`. Because header support in that path is UNVERIFIED, put the token in the URL query instead.
