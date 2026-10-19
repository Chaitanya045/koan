# Koan Phase 3 research: Zen "Glance" + split view UX

Read-only research against `~/work/koan/chromium/src` at **Chromium
158.0.8111.0** (`chrome/VERSION`: MAJOR=158, MINOR=0, BUILD=8111, PATCH=0).
All `file:line` pairs are relative to that tree. Nothing was built, patched,
or written outside this document.

This answers the five questions and ends with a recommended architecture,
its upstream hook points, and risks. Claims that could not be verified in
this checkout are marked **UNVERIFIED**.

---

## Q1. Which upstream features already host a secondary WebContents over or beside the page

### 1.1 Desktop link preview — deleted in this checkout

`git -C ~/work/koan/chromium/src log --oneline -S LinkPreview` finds no code
commit (single hit is a PGO profile roll, `67e29b8287`). `rg -l "LinkPreview"`
over `chrome/`, `components/`, `content/` matches only the test
`chrome/browser/renderer_context_menu/render_view_context_menu_browsertest.cc`.
Not a template.

### 1.2 Actor overlay (Glic) — the best in-page template

A `views::WebView` stacked **on top of the page**, per pane, feature-gated:

- Created inside `ContentsContainerView` when
  `features::kGlicActorUi` + `features::kGlicActorUiOverlay` are both on:
  `chrome/browser/ui/views/frame/contents_container_view.cc:134-140`
  (feature check at 134-135, creation + AddChildView at 136-140).
  Feature definitions: `chrome/common/chrome_features.cc:308`
  (`BASE_FEATURE(kGlicActorUi, FEATURE_ENABLED_BY_DEFAULT)`) and
  `chrome/common/chrome_features.cc:385-386` (the `kGlicActorUiOverlay`
  bool param, default `true`).
- `chrome/browser/actor/ui/actor_overlay_web_view.h:21-30` —
  `class ActorOverlayWebView : public views::WebView`.
- The controller does the whole show/hide dance
  (`chrome/browser/actor/ui/actor_overlay_web_view.cc`):
  - ctor: `SetBrowserContext(browser->GetProfile())` so the WebView can
    lazily create its own WebContents (lines 30-31), `SetVisible(false)`,
    `FocusBehavior::NEVER`, accessibility-ignored (lines 32-33).
  - `ShowUI()`: on first show `LoadInitialURL(GURL(chrome::kChromeUIActorOverlayURL))`
    (lines 53-55); blocks all input to the background page with
    `tab->GetContents()->IgnoreInputEvents(std::nullopt)` guarded by a
    `content::WebContents::ScopedIgnoreInputEvents` (lines 59-61);
    makes the overlay WebContents transparent via
    `SetPageBaseBackgroundColor(SK_ColorTRANSPARENT)` + `RenderWidgetHostView::SetBackgroundColor`
    (lines 64-69); `SetVisible(true)` + `WasShown()` (lines 72-74).
  - `CloseUI()`: `SetVisible(false)`, reset the
    `ScopedIgnoreInputEvents`, `WasHidden()` (lines 76-87).
- Layout: because it is a fill-layer child of the per-pane container, it
  stays aligned with the contents; `contents_container_view.cc:555-558`
  registers it in the DelegatingLayoutManager's child layouts the same way
  the contents and the scrim are (`contents_container_view.cc:543-546`).

### 1.3 AI overlay dialog — bare `views::WebView` sized from its WebContents

`contents_container_view.cc:115-123`, behind `features::kAiOverlayDialog`:
`std::make_unique<views::WebView>(browser_view->GetProfile())` +
`EnableSizingFromWebContents(gfx::Size(1,1), gfx::Size(800,600))`.
Useful only if a Glance card should auto-size from page layout.

### 1.4 Read-anything overlay + Glic selection overlay

Siblings of the actor overlay inside each `ContentsContainerView`:
`contents_container_view.cc:124-128` (`ReadAnythingImmersiveOverlayView`),
accessors at `contents_container_view.h:83-87`. Same pattern: children of
the per-pane container above `contents_view_`.

### 1.5 Lens overlay — controller-owned WebView mounted into MultiContentsView

- The host container is an empty `views::View` `lens_overlay_view_` with
  FillLayout, added after the contents views so it draws above them:
  `chrome/browser/ui/views/frame/multi_contents_view.cc:131-137`
  (comment: "It must have a higher index than contents_view so that it is
  drawn on top of it"); registered in the layout at
  `multi_contents_view.cc:558-561`.
- The actual `views::WebView` is created and mounted by
  `OverlayBaseController::CreateViewForOverlay()`:
  `chrome/browser/ui/lens/overlay_base_controller.cc:230-301` — creates
  `views::WebView(tab_->GetContents()->GetBrowserContext())` (line 265),
  transparent background (lines 266-271), `web_view->set_allow_accelerators(true)`
  + `GetWebContents()->SetDelegate(this)` (lines 288-290, this is what
  enables Esc handling, see Q5), `LoadInitialURL()` (line 291), then
  `host_view->AddChildView(...)` (line 297).

### 1.6 Side panel and PiP — adjacent / separate-widget, not templates

- Side panel content renders in the browser window layout next to the
  contents (its transition scrim at `browser_view.cc:1044-1047`); wrong
  shape for a floating card.
- Document Picture-in-Picture opens its own top-level widget through
  `PictureInPictureWindowManager` (`NormalizeDisposition` calls
  `SetWindowParams` at `browser_navigator.cc:465-466`); the widget chrome
  lives in `chrome/browser/ui/views/overlay/video_overlay_window_views.{h,cc}`.
  Not usable for Glance (no page underneath to dim, no tab promotion), but
  its toolbar is a reference for the close/expand/split button row.

### 1.7 Verdict for Glance

The ActorOverlayWebView layout position (child of the **active**
`ContentsContainerView`, above its scrim) is the right home:
it is per-pane (Glance overlays the pane that initiated it), already
participates in the DelegatingLayoutManager so it tracks contents bounds
and corner radii (`contents_container_view.cc:283-294` apply rounded
corner radii to overlays), and the view ordering guarantees z-above the
scrim. A separate `views::Widget` (like hovercards) is NOT the template:
it would not dim with the page, would live outside the wobble/layout
system, and would complicate focus (see Q5).

---

## Q2. Link clicks with modifiers; where Koan can intercept Alt+click

### 2.1 The decision is made in the renderer, in blink

`third_party/blink/renderer/core/loader/navigation_policy.cc:51-75`,
`NavigationPolicyFromEventModifiers(button, ctrl, shift, alt, meta)`:

```
#if BUILDFLAG(IS_MAC)
  const bool new_tab_modifier = (button == 1) || meta;   // middle or Cmd
#else
  const bool new_tab_modifier = (button == 1) || ctrl;   // middle or Ctrl
#endif
  if (new_tab_modifier && alt && !shift) {
    return kNavigationPolicySplitView;                   // lines 60-63
  }
  ...
  return shift ? kNavigationPolicyNewWindow : kNavigationPolicyDownload;  // line 75
```

- **Alt+click (without Ctrl/Shift) ⇒ `kNavigationPolicyDownload`** — that
  is the "Alt means download" disposition, produced here in the renderer.
- **Upstream already has a split-view click gesture**: Ctrl+Alt+click
  (⌘+Alt on Mac) maps to `kNavigationPolicySplitView`
  (`navigation_policy.cc:60-63`).
- Synthesized events are vetted by `NavigationPolicyFromEvent()`
  (`navigation_policy.cc:93-131`): the DOM event's policy is only honored
  if it matches the real current input event (`CurrentInputEvent::Get()`,
  `third_party/blink/renderer/core/events/current_input_event.{h,cc}`,
  `CORE_EXPORT`). If a page script synthesises a "alt-click", split view
  falls back to CURRENT_TAB (`navigation_policy.cc:153-158`).

### 2.2 Renderer→browser IPC split — why the browser never "sees" Alt+click

`content/renderer/render_frame_impl.cc:5828-5847`:

```
// DownloadURL handles downloads, OpenURL handles all navigations
// that will end up in a different tab/window, BeginNavigation everything else.
if (info->navigation_policy == blink::kWebNavigationPolicyDownload) {
  ...
  frame_->DownloadURL(...);        // line 5837 — mojo straight to FrameHost
  return;
}
if (info->navigation_policy != blink::kWebNavigationPolicyCurrentTab) {
  OpenURL(std::move(info));        // line 5843
}
```

Consequence: for Alt+click the browser receives a `DownloadURL` mojo — no
`OpenURLFromTab`, no `Navigate()`, and the modifier information is gone by
then. `content::WebContentsDelegate::OpenURLFromTab` (implemented by
`BrowserWebContentsDelegate`, `chrome/browser/ui/browser_web_contents_delegate/browser_web_contents_delegate.cc:529-600`)
only sees dispositions that travelled through the `OpenURL` mojo with
`params->disposition = NavigationPolicyToDisposition(policy)`
(`render_frame_impl.cc:6142`; mapping table at
`render_frame_impl.cc:1273-1303`, e.g. split view at 1298-1303).
There is a single mojo enum for this:
`ui/base/mojom/window_open_disposition.mojom` ↔ `ui/base/window_open_disposition.h`
(`NEW_SPLIT_VIEW` at `ui/base/window_open_disposition.h:30-32`).

### 2.3 Browser-side handling of dispositions (the chain a Glance disposition rides)

- `BrowserWebContentsDelegate::OpenURLFromTab`
  (`browser_web_contents_delegate.cc:549-563`): already special-cases
  `NEW_SPLIT_VIEW` — when the source tab is in a split, it navigates the
  other pane instead of adding a tab. This `if` is the exact place to add
  the Koan hook.
- `chrome/browser/ui/navigator/browser_navigator.cc`:
  - window selection treats `NEW_SPLIT_VIEW` like new-tab (line 303);
  - `NormalizeDisposition` (lines 431/447/485): `SAVE_TO_DISK` /
    `IGNORE_ACTION` produce **no navigation** (429-433); `NEW_SPLIT_VIEW`
    clears `AddTabTypes::ADD_ACTIVE` (485-490) because
    `AddToNewSplit()` pairs with the active tab;
  - insertion + split at `browser_navigator.cc:1049-1077` (see Q3).
- `ui::DispositionFromClick` (`ui/base/window_open_disposition_utils.cc:23-51`)
  is the **views/webui-side mirror** used by chrome-owned click surfaces
  (WebUI handlers such as
  `chrome/browser/ui/webui/browser_command/browser_command_handler.cc`,
  bookmark-bar code, WebApp UI). It encodes the same table
  (split at lines 35-38, `alt → SAVE_TO_DISK` at line 48). Page-content
  links never pass through it — scoping note: "Alt+click on a link" is a
  blink disposition, always.

### 2.4 Hook options for "Alt+click a link opens Glance"

**(a) Blink-native: change the renderer policy decision.**
Three lines in `navigation_policy.cc` around line 60-75 mapping alt-only to
a Koan policy when the Koan Glance feature is on. Problem: blink cannot
include `chrome/browser/...` features, so the gate must be blink-visible
(a `blink::features::` Feature declared in
`third_party/blink/common/features.cc`, or a renderer command-line switch),
plus an enum value must exist end-to-end anyway (i.e. (c) is implied).
Kept as the *combinable tail* of (c), not a separate patch family.

**(b) Content/renderer hook at the download IPC branch.**
`content/renderer/render_frame_impl.cc:5833-5840` is the one place where a
patch knows both "this was an Alt-only click" and the URL. A Koan hook
could convert `DownloadURL` to `OpenURL` with a marker there. Cost: a
`content/` (and possibly blink mojom field) change; larger visual diff and
more upstream-churn risk than (c). Ruled out as primary.

**(c) RECOMMENDED: mirror the upstream `NEW_SPLIT_VIEW` plumbing with a
`KOAN_GLANCE` disposition.** Upstream itself extended this enum recently
for Ctrl+Alt-click split view; extending it once more touches the same
small set of engines:

1. `ui/base/window_open_disposition.h:30-32` — add `KOAN_GLANCE` before
   `MAX_VALUE = NEW_SPLIT_VIEW` (1 line).
2. `ui/base/mojom/window_open_disposition.mojom` +
   `ui/base/mojom/window_open_disposition_mojom_traits.h` — enum value +
   trait mapping.
3. blink: add `kWebNavigationPolicyKoanGlance` in
   `third_party/blink/public/web/web_navigation_policy.h`, map it in
   `third_party/blink/renderer/core/loader/navigation_policy.{h,cc}`
   (STATIC_ASSERT at `navigation_policy.cc:204` style), and in
   `NavigationPolicyFromEventModifiers` map the alt-only case to it when
   gated on (blink gate, see (a)).
4. `content/renderer/render_frame_impl.cc:1281-1303`
   (`NavigationPolicyToDisposition`) — add the case; add the policy to the
   `ConsumeTransientUserActivation()` list at
   `content/renderer/render_frame_impl.cc:6155-6170` if Glance consumes
   the activation (UNVERIFIED: check what `NEW_SPLIT_VIEW` does there and
   copy).
5. The single chrome-side consumer hook:
   `BrowserWebContentsDelegate::OpenURLFromTab`,
   `browser_web_contents_delegate.cc:549` — add next to the
   `NEW_SPLIT_VIEW` branch:
   `if (disposition == KOAN_GLANCE) return koan::Glance::MaybeOpenFromOpenURLParams(browser_, source, params);`
   which returns `source` WebContents (so callers treat it as in-page) or
   null when disabled. `browser_navigator.cc` also needs `KOAN_GLANCE`
   cases copied from `NEW_SPLIT_VIEW` (lines 303, 431/447/485) unless the
   delegate returns before `Navigate()` is called — UNVERIFIED which
   call sites never fail; safest is to keep the `browser_navigator.cc`
   switches symmetric.

Every other `switch (disposition)` site that upsets `-Wswitch` on enum
growth must be enumerated by diffing the commit that added
`kWebNavigationPolicySplitView`/`NEW_SPLIT_VIEW` — UNVERIFIED exact list;
use `rg -l "WindowOpenDisposition::NEW_SPLIT_VIEW" content/ chrome/ ui/`
as the seed.

**(d) Views-level mouse interception.** Not viable: page clicks are
consumed by `RenderWidgetHostView` and never pass through a chrome-owned
`views::View::OnMouseEvent`; there is no pre-renderer mouse hook exposed
to //chrome.

### 2.5 Also relevant: `ui::DispositionFromClick` on chrome surfaces

If Koan *also* wants Alt+click Glance on chrome-owned surfaces (bookmark
bar, WebUI history, etc.), `ui/base/window_open_disposition_utils.cc:23-51`
is the single shared interpreter — hook it by mapping `alt_key` to
`KOAN_GLANCE` inside `DispositionFromClick` under the Koan flag, so all
chrome surfaces get it for free. That keeps the (c) chrome-side surface
plus one function in `ui/base` — not a global compile-flag change.

---

## Q3. Promoting a Glance WebContents into a real tab, and into a split

### 3.1 Adopting a detached WebContents into the tab strip

Upstream already has three "adopt existing WebContents" paths:

- `TabStripModel::InsertWebContentsAt(int index, std::unique_ptr<content::WebContents>, int add_types, group)`
  — `chrome/browser/ui/tabs/tab_strip_model.h:288-293`.
- `TabStripModel::AddWebContents(...)` convenience wrapper —
  `tab_strip_model.h:597-604`.
- The newer tabstrip-agnostic `TabListInterface::InsertWebContentsAt(index, contents, should_pin, group)`
  — `chrome/browser/tab_list/tab_list_interface.h:95-103`.
- Real users (evidence the path is supported in production code):
  - DevTools "open in Chromium": takes an owned WebContents and inserts
    it: `chrome/browser/devtools/devtools_window.cc:2340-2355`
    (`tab_list->InsertWebContentsAt(0, std::move(owned_web_contents),
    false, std::nullopt)`).
  - Web-app (PWA/custom tab) "open in Chrome"-equivalent path — moves a
    WebContents from a non-normal browser window into a normal one:
    `chrome/browser/ui/web_applications/web_app_launch_utils.cc:358-403`
    (`target_tab_strip_model->InsertWebContentsAt(location, contents_move, add_types)`
    with `ADD_INHERIT_OPENER | ADD_ACTIVE`).
  - Contextual tasks inserts freshly created WebContents mid-strip and
    sets opener semantics: `chrome/browser/contextual_tasks/contextual_tasks_ui_service.cc:1049-1059`
    (`InsertWebContentsAt(active_index+1, ...)` then
    `SetOpenerForTab(..., active_tab)` then `ActivateTab`).

A Glance WebContents should therefore be kept as
`std::unique_ptr<content::WebContents>` (created with
`content::WebContents::Create(WebContents::CreateParams(profile))`), never
wrapped in a TabModel, so "expand to tab" is one
`InsertWebContentsAt(browser->tab_strip_model()->active_index() + 1, std::move(contents), ADD_ACTIVE)`
call plus opener wiring as in contextual_tasks above. It must NOT be
attached to the browser's `TabUIHelper`-associated helpers; UNVERIFIED
whether all tab-referencing helpers self-heal on adoption — DevTools /
web_app paths show that moving a raw WebContents in is accepted by
`TabStripModel` itself.

### 3.2 Promoting into a split with the current tab

- `TabStripModel::AddToNewSplit(std::vector<int> indices,
  split_tabs::SplitTabVisualData visual_data,
  split_tabs::SplitTabCreatedSource source)` —
  `chrome/browser/ui/tabs/tab_strip_model.h:666-669` (returns
  `split_tabs::SplitTabId`; reorders tabs to be contiguous, gives the new
  tab the active tab's pinned/group state).
- `split_tabs::SplitTabVisualData` —
  `components/split_tabs/split_tab_visual_data.h:25-62`: ctor takes
  `SplitTabLayout::{kSideBySide,kStacked}` and an optional
  `split_ratio` (comment: layout enum mirrored into
  `tab_search.mojom`/`enums.xml`).
- Recipe for the "Glance expand into split" flow: identify the current
  tab index (source tab), insert the Glance contents
  (`InsertWebContentsAt`), then `AddToNewSplit({new_index},
  SplitTabVisualData(SplitTabLayout::kSideBySide), source)`. Exactly the
  compose done by:
  - link click: `chrome/browser/ui/navigator/browser_navigator.cc:1049-1077`
    (this is how `NEW_SPLIT_VIEW` dispositions become splits — source
    `split_tabs::SplitTabCreatedSource::kLinkClick`);
  - link drop: `chrome/browser/ui/views/frame/multi_contents_view_delegate.cc:91-140`
    (visible in Q4 below);
  - tab drop: `multi_contents_view_delegate.cc:142-167`.
- `SplitTabCreatedSource` enum lives at
  `chrome/browser/ui/tabs/split_tab_metrics.h:17-33` (values
  `kLinkClick=11`, `kDragAndDropTab=2`, `kDragAndDropLink=1`,
  `kTabContextMenu=5`), LINT-linked to histogram enums — adding a Koan
  source value means touching `tools/metrics/histograms` too (call out in
  risks).
- Context-menu "split with this tab" already exists:
  `chrome/browser/ui/tabs/tab_menu_model.cc:337-375` —
  when the context tab is not in a split, `CommandAddToSplit` submenu
  (`IDS_TAB_CXMENU_NEW_SPLIT_WITH_CURRENT`) calls
  `TabStripModel::ExecuteAddToNewSplitCommand(context_index, layout)`
  (lambda at `tab_menu_model.cc:357-364`), and the
  swap-into-active-split variant is `CommandSwapWithActiveSplit`
  (`tab_menu_model.cc:340-351`). Availability is gated on
  `tabs::IsSplitViewHorizontalDirectAccessEnabledForTab()`
  (`chrome/browser/ui/tabs/features.cc:19-31,45-54`, `kSplitViewHorizontal` +
  `kSplitViewHorizontalDirectAccess` param). Zen's "Split with this tab"
  is a relabel/extension of this — Koan can call
  `ExecuteAddToNewSplitCommand` from its own controller instead of adding
  menu complexity.
- Another upstream entry point useful for Glance: `chrome/browser/ui/browser_commands.cc:1822-1841`
  `NewSplitTab(browser, layout, source)` — adds a NTP next to the active
  tab and immediately `AddToNewSplit` — a modifier to learn from when
  Glance has no URL (new split with empty partner).

---

## Q4. Split view entry points: drag-a-tab-onto-page, prefs, feature gates

### 4.1 Drag a tab onto the page to split — already fully wired, incl. vertical tabs

Drop-target machinery:

- `MultiContentsView` owns `MultiContentsDropTargetView`
  (`chrome/browser/ui/views/frame/multi_contents_view.cc:96`) and an
  `std::unique_ptr<MultiContentsViewDropTargetController>`
  `drop_target_controller_` (`multi_contents_view.cc:97`,
  getter at line 822).
- Controller: `chrome/browser/ui/views/frame/multi_contents_view_drop_target_controller.{h,cc}`.
  It decides side (START/END/BOTTOM) from pointer position:
  `…drop_target_controller.cc:325-395` (`HandleDragUpdate`), including
  the bottom (stacked) option gated on `tabs::kSplitViewHorizontal`
  (line 358-364).
- Tabs-drag entry: `browser_view.cc` `GetTabDragTarget()`, which the
  tab-drag controller queries per event:
  `chrome/browser/ui/views/frame/browser_view.cc:2510-2534` — checks the
  **vertical** tab strip region FIRST
  (`ShouldDrawVerticalTabStrip() → vertical_tab_strip_region_view_->GetTabDragTarget`).
  So tab drags in Koan's always-on vertical strip land in the drop target
  today.
- It hands over to
  `MultiContentsViewDropTargetController::HandleTabDrop()` (declared
  `multi_contents_view_drop_target_controller.h:61/83`, impl at
  `multi_contents_view_drop_target_controller.cc:262-275`) →
  `MultiContentsViewDelegateImpl::HandleTabDrop()`:
  `chrome/browser/ui/views/frame/multi_contents_view_delegate.cc:142-167`
  — `drag_controller.DetachTabAtForInsertion(source_view_index)` →
  `tab_strip_model_->InsertDetachedTabAt(new_tab_idx, std::move(detached_tab), ...)`
  → `tab_strip_model_->AddToNewSplit({inserted_tab_idx}, split_data,
  SplitTabCreatedSource::kDragAndDropTab)` → `ActivateTabAt`.
- Link drag (from outside the window) uses
  `BrowserView::PreHandleDragUpdate/Exit/Ended`
  (`browser_view.cc:3325-3350`, called from
  `BrowserWebContentsDelegate`, `browser_web_contents_delegate.cc:424-427`)
  → `MultiContentsViewDropTargetController::OnWebContentsDragUpdate(...)`
  (`…drop_target_controller.cc:282-313`) →
  `HandleLinkDrop` in `multi_contents_view_delegate.cc:91-140`
  (`Navigate()` then `AddToNewSplit` `kDragAndDropLink`).
- Which drag types: `MultiContentsDropTargetView::DragType::kTab` and
  `::kLink`; the "drop target
  available" condition is: normal browser type AND
  `prefs::kSplitViewDragAndDropEnabled` AND not already in a split
  (`multi_contents_view.cc:825-830` + `browser_view.cc:2527-2531`).

### 4.2 Prefs / features gating split view (defaults in this checkout)

- `prefs::kSplitViewDragAndDropEnabled` =
  `"browser.split_view_drag_and_drop_enabled"`
  (`chrome/common/pref_names.h:649-651`). Registered **default true** in
  `chrome/browser/ui/browser_ui_prefs.cc:129`
  (`RegisterBooleanPref(prefs::kSplitViewDragAndDropEnabled, true, ...)`).
  Cached by `MultiContentsView` (`multi_contents_view.cc:173-183` —
  `is_drag_drop_pref_enabled_` + change registrar for live updates).
  ⇒ Drag-to-split is ON by default, unconditionally.
- `tabs::kSplitViewHorizontal` —
  `chrome/browser/ui/tabs/features.cc:19`, **`FEATURE_ENABLED_BY_DEFAULT`**
  (gives stacked layouts, click-gesture split, direct tab access menu).
  Param `kSplitViewHorizontalDirectAccess` (default `false`, line 20-23)
  and `kSplitViewHorizontalDirectTabAccess` (24-27) select whether context
  menus offer direct layout choice (see `tab_menu_model.cc:352`).
- No `kSideBySide` feature exists in this checkout
  (`rg "SideBySide" chrome/common/chrome_features.cc` — no match;
  nearest is `kWebUISplitTabsButton`,
  `chrome/common/chrome_features.cc:2048`, DISABLED - UNVERIFIED purpose).
  Split view is otherwise not feature-flagged: `AddToNewSplit`,
  `MultiContentsView` split rendering, and context menus are always
  compiled in.
- Which buttons exist: toolbar/split button,
  `kWebUISplitTabsButton` (OFF by default), tab context menu, keyboard
  shortcut (`kKeyboardShortcut` in `split_tab_metrics.h`), NTP URL
  `chrome::kChromeUISplitViewNewTabPageURL` used by
  `browser_commands.cc:1831-1834`.

### 4.3 What Koan adds for split view

Zen extras Koan wants:

1. **Drag a tab onto the page to split** — already upstream
   (§4.1); Koan only needs to keep it enabled (it is default-on) and
   style it. If Koan wants it behind a Koan pref instead, gate inside
   `MultiContentsView::IsDragAndDropEnabled()`
   (`multi_contents_view.cc:825-830`) via a hook patch.
2. **"Split with this tab" action** — upstream menu path noted in §3.2
   (`tab_menu_model.cc:337-364` →
   `ExecuteAddToNewSplitCommand`); Koan can drive the same command from
   its own UI (e.g. tab-strip context menu under Koan styling, or from a
   Glance card).

---

## Q5. Scrim/dimming and focus for the overlay

### 5.1 Existing scrims in the browser window

- Per-pane scrims live INSIDE `ContentsContainerView`:
  - `contents_scrim_view_` — `contents_container_view.cc:130-131`
    (`AddChildView(std::make_unique<ScrimView>())`; accessor
    `contents_container_view.h:77`);
  - `devtools_scrim_view_` — `contents_container_view.cc:79-80`,
    accessor `contents_container_view.h:79`.
  Both keep rounded contents corners
  (`contents_container_view.cc:246`, `…cc:283`).
- Window-level: `window_scrim_view_` in
  `chrome/browser/ui/views/frame/browser_view.cc:1041-1042` (used to dim
  the window behind native macOS sheets / global modals —
  shown at `browser_view.cc:2999-3001`).
- `chrome/browser/ui/views/frame/scrim_view.h:14-22` —
  `class ScrimView : public views::View` painting `ui::kColorSysStateScrim`
  (semi-transparent black) behind whatever is behind it.

### 5.2 The managed dim: `ScrimViewController` + tab "blocked" state

- `chrome/browser/ui/views/frame/scrim_view_controller.{h,cc}`:
  a `TabStripModelObserver` that mirrors each tab's
  `tabs::TabInterface::IsBlocked()` into
  `contents_scrim_view_->SetVisible(...)`
  (`scrim_view_controller.cc:56-62`), reacting to
  `OnTabStripModelChanged` (selection), `OnTabBlockedStateChanged`,
  `OnSplitTabChanged` (h:14-31).
- Who *sets* blocked: the tab-modal dialog machinery:
  `TabStripModel::SetTabBlocked(int index, bool blocked)` —
  `chrome/browser/ui/tabs/tab_strip_model.cc:1421-1432`; and on insertion
  `tab_strip_model.cc:3985-3988` re-syncs from
  `web_modal::WebContentsModalDialogManager::IsDialogActive()`.
- `TabDialogManager::ShowDialog()` — the tab-confirmation dialog path
  locks the page underneath:
  `chrome/browser/ui/tabs/tab_dialog_manager.cc` (ShowDialog body
  starting at line 319): at lines 338-348 —
  `scoped_ignore_input_events_ = tab_interface_->GetContents()->IgnoreInputEvents(std::nullopt)`
  (343) and
  `browser_window_interface->capabilities()->SetWebContentsBlocked(contents, true)`
  (346) which drives `TabInterface::SetBlocked` →
  `ScrimViewController` → visible scrim. The Koan flow can reuse BOTH
  verbatim: while Glance shows, (a) `IgnoreInputEvents` on the host tab,
  (b) `SetWebContentsBlocked(host_contents, true)`. UNVERIFIED whether
  `SetWebContentsBlocked` has any side effects Koan wants (browser
  capabilities hiding); the raw path used by the actor overlay
  (`ScopedIgnoreInputEvents` only, `actor_overlay_web_view.cc:59-61`)
  dims nothing and blocks only input — pick one of the two profiles.
- Widget creation behind it: `TabDialogManager::CreateTabScopedDialog`
  makes a `views::Widget` child of the browser window host with
  `ModalType::kChild` (`tab_dialog_manager.cc:304-318`).
- `tabs::TabInterface::CanShowModalUI()/ShowModalUI()` —
  `components/tabs/public/tab_interface.h:269-270`; used by
  `OverlayBaseController::ShowModalUI()` to acquire the "I'm the tab's
  modal UI" slot (`chrome/browser/ui/lens/overlay_base_controller.cc:575-589`,
  guard `CanShowModalUI()` at 556-573). **Koan Glance should occupy the
  same slot** so existing bookkeeping (tab close while glance open, other
  modals, side-panel conflicts) keeps working.

### 5.3 What the Lens overlay does for the "background darkened" visuals

- Blur, not just dim: `LensOverlayBlurLayerDelegate` —
  `chrome/browser/ui/lens/lens_overlay_blur_layer_delegate.h:24-51`
  — a `ui::LayerDelegate` that repeatedly screenshots the background
  `RenderWidgetHost` and paints it blurred;
  `chrome/browser/ui/lens/lens_overlay_blur_layer_delegate.cc` uses
  `RenderWidgetHostView::CopyFromBackBuffer` UNVERIFIED.
- Z-order/focus bridge: the Lens overlay sits just above the page, and
  its bubble anchor views are inserted BEFORE the webview in the child
  list so that top chrome receives focus first when Tabbing
  (`overlay_base_controller.cc:240-261`).

### 5.4 Esc handling — the precedent is `WebContentsDelegate::HandleKeyboardEvent`

- The Lens overlay's WebContents is hosted in a child `views::WebView`
  with `SetDelegate(controller)` (see §1.5, `overlay_base_controller.cc:289-290`).
  Keys the page does not consume come back to the browser via
  `content::WebContentsDelegate::HandleKeyboardEvent`
  (override at `chrome/browser/ui/lens/lens_overlay_controller.{h:633-636,cc:1457-1467}`).
- The event then goes to `LensOverlayEventHandler::HandleKeyboardEvent`
  (`chrome/browser/ui/lens/lens_overlay_event_handler.{h:23-33,cc:37-70}`):
  `IsEscapeEvent(event)` (raw key down `ui::VKEY_ESCAPE`,
  `lens_overlay_event_handler.cc:20-23`) →
  `HideOverlay(LensOverlayDismissalSource::kEscapeKeyPress)` and **returns
  true**, i.e. swallow.
- `views::UnhandledKeyboardEventHandler`
  (`ui/views/controls/webview/unhandled_keyboard_event_handler.h`, member at
  `lens_overlay_event_handler.h:21-22`) is available to forward *other*
  unhandled keys (e.g. F-Keys) into the FocusManager; and
  `web_view->set_allow_accelerators(true)` (§1.5) lets browser
  accelerators still fire while the overlay WebContents is focused.
- Widget-based alternative precedent: `TabDialogManager::CreateTabScopedDialog`
  (`tab_dialog_manager.cc:304-318`) makes a `views::Widget` child of the
  browser window host with `ModalType::kChild`; such dialogs close on Esc
  by dialog defaults (UNVERIFIED - standard `views::DialogDelegate`
  behavior). Koan should NOT need this because a Glance card is *not* a
  modal dialog; delegate-handled Esc (above) is simpler and matches the
  host-page-not-consuming-Esc expectation, and Esc ALSO works when focus
  is on the host page if Koan additionally registers a
  `views::FocusManager` accelerator while the overlay is showing
  (UNVERIFIED; recommended call: try delegate-path first).

### 5.5 Focus semantics checklist for Glance

1. On open: acquire the tab-modal slot (`tab_->CanShowModalUI()`
   succeeded → `tab_->ShowModalUI()`), block host page input
   (`IgnoreInputEvents`), set scrim visible (via blocked state or direct
   `contents_scrim_view()->SetVisible(true)`).
2. Focus the Glance view: the overlay WebView must be focusable
   (unlike the actor overlay which is `FocusBehavior::NEVER` — see
   `actor_overlay_web_view.cc:32`); lens's WebView is a normal focus
   child (`overlay_base_controller.cc:265` + view insertion), and its
   `lens_overlay_view_` parent is in the focus chain (`multi_contents_view.cc:131-137`).
   Koan aims for `FocusBehavior::AL`, `RequestFocus()` on show; the Lens
   tab-focus behavior (anchor views before webview in child
   list, `overlay_base_controller.cc:240-261`) should be replicated so
   Tab walks appropriately between host chrome and the Glance card.
3. On close: reset `ScopedIgnoreInputEvents`, restore scrim hidden,
   `WasHidden()` on the Glance WebContents (mirror
   `actor_overlay_web_view.cc:76-87` so closed-over cards keep state
   alive if the user re-opened the same URL — decide the keeper
   semantics per Koan Glance UX).

---

## Recommendation (concrete architecture)

All new logic lives in `//koan` (`koan::features`,
`koan::GlanceController`), upstream surface = tiny hook patches.

### R1. Overlay view: Koan Glance inside `ContentsContainerView` (per pane)

- New `koan/browser/ui/views/koan_glance_web_view.{h,cc}` — a
  `views::WebView` subclass similar to
  `chrome/browser/actor/ui/actor_overlay_web_view.*`, but:
  - keeps its OWN `std::unique_ptr<content::WebContents>` created at
    Glance-open (never a chrome:// WebUI: it hosts real pages to be
    promoted later),
  - `FocusBehavior::AL` + focus within, since Glance is user-interactive,
  - close()/show() mirror `actor_overlay_web_view.cc:52-87`.
- Hook patch in `chrome/browser/ui/views/frame/contents_container_view.cc`
  (+3 lines): `if (koan::features::kKoanGlance.Get()) { koan_glance_view_ =
  AddChildView(koan::CreateGlanceView(browser_view)); }` placed after the
  scrim creation (contents_container_view.cc:130-131) and BEFORE the
  actor overlay so that scrim paints under the glance card; add the view
  to the DelegatingLayoutManager wobble list the same way the actor
  overlay is at `contents_container_view.cc:555-558`.
- The controller (per-window, owned like `KoanBrowserViewController`) via
  `koan/browser/ui/views/koan_browser_view_controller.h` tracks state per
  active tab (open/close positions, animation, "keep open" rules).

### R2. Block/dim the background page

- `content::WebContents::ScopedIgnoreInputEvents` +
  `capabilities()->SetWebContentsBlocked(contents, true)` exactly like
  `tab_dialog_manager.cc:338-348`; the existing
  `ScrimViewController` (`scrim_view_controller.cc:56-62`) shows the
  real per-pane scrim automatically. If Koan prefers no dialog-style
  capability side-effects (UNVERIFIED), fall back to
  `contents_scrim_view()->SetVisible(true)` directly via
  the per-pane `contents_container_view.h:77` accessor.
- Z-order: Glance card child of the ACTIVE pane's
  `ContentsContainerView`, above the scrim (child index — see R1), so
  the page dim + card = Zen look for free.

### R3. Open-from-Alt+click disposition hook

Add `WindowOpenDisposition::KOAN_GLANCE` and reuse the split-view-gesture
plumbing (the full §2.4(c) five files). The single chrome consumer hook
is at `browser_web_contents_delegate.cc:549`:
```
if (params.disposition == WindowOpenDisposition::KOAN_GLANCE
    && koan::features::kKoanGlance.Get()) {
  return koan::Glance::OpenFromOpenURLParams(browser(), source, params);
}
```
The Koan implementation:
- locates the window (BrowserView) from `browser()`,
- `koan::GlanceController::Open(url, source_tab)` opens the floating card
  in the Active pane (R1),
- returns `source` WebContents so no navigation/tab happens in the
  normal path.
Blink-side gate (Q2.4(a)) is a `blink::features::kKoanGlance` Feature in
`third_party/blink/common/features.cc` mapping alt-only clicks to
`KOAN_GLANCE` in `navigation_policy.cc` — still ALLOWING (i.e. not
breaking) the classic alt-click = download when Koan's flag is off.
UNVERIFIED: adding a Feature in blink and exposing it from
`chrome/browser/about_flags.cc` requires checking how existing
blink features are surfaced there before patching.

### R4. "Close / expand to tab / open in split" buttons inside the card

- The Glance card's button row is Koan views (not web UI) so it works on
  arbitrary URLs; keep it a views overlay that sits in the Glance card's
  parent (on top of the WebView) — same per-pane container children
  (positioning: `contents_container_view.cc:555-558` wobble entry
  approach).
- **Expand to tab**: one call —
  `tab_strip_model->InsertWebContentsAt(active_index + 1, std::move(web_contents),
  AddTabTypes::ADD_ACTIVE | AddTabTypes::ADD_INHERIT_OPENER)`
  (`chrome/browser/ui/tabs/tab_strip_model.h:288-293`) plus
  `SetOpenerForTab(new_tab_handle, source_tab_handle)` — as done in
  `contextual_tasks_ui_service.cc:1049-1059` — then close the overlay
  (ownership transfer since Koan kept the WebContents detached).
- **Open in split**: also one path —
  `tab_strip_model->InsertWebContentsAt(...ADD_ACTIVE...)` then
  `tab_strip_model->AddToNewSplit({inserted_index},
  split_tabs::SplitTabVisualData(SplitTabLayout::kSideBySide),
  split_tabs::SplitTabCreatedSource::kLinkClick)`
  ( `chrome/browser/ui/tabs/tab_strip_model.h:666-669`,
  `split_tab_visual_data.h:25-62`, sources list
  `split_tab_metrics.h:17-33`, link-click precedent
  `browser_navigator.cc:1049-1077`). UNVERIFIED whether
  `SplitTabCreatedSource::kLinkClick` or a new Koan entry is preferable;
  the new entry touches enum+histogram metadata (see Risks).
- Esc handling: delegate-based (Q5.4) + `set_allow_accelerators(true)`;
  on Esc, run close (unless the Glance page consumed Esc).

### R5. Split view: adopt, don't rebuild

- `MultiContentsView` split rendering, drop target, `AddToNewSplit`,
  context-menu actions are upstream and default-on
  (`browser_ui_prefs.cc:129`, `chrome/browser/ui/tabs/features.cc:19`).
  Koan keeps them.
- Styling only (Koan content-card look) already exists in Phase 1
  (`chrome/browser/ui/views/frame/multi_contents_view.cc` indirection in
  `koan_content_card.h`). No structural change needed for split.
- Optional hook for "Zen extra" Koan prefs: gate
  `MultiContentsView::IsDragAndDropEnabled()`
  (`multi_contents_view.cc:825-830`) behind `koan::features::kKoanGlance`
  or a new `kKoanSplitDragDrop` in later phases.

---

## Risks

1. **Alt+click = download swap.** Making Alt+click open Glance deletes
   the standard "download link" gesture while the Koan flag is enabled.
   Mitigation: feature default OFF gradually ramped / enabled at
   build-time for install experiments; add an about_flags override.
2. **Enum growth in `WindowOpenDisposition`.** A new disposition value
   hits every switch + mojo validation (traits). Mitigation: find every
   switch via the commit that introduced `NEW_SPLIT_VIEW` (UNVERIFIED how
   large the list is in this checkout; seed from
   `rg -l "NEW_SPLIT_VIEW"`), and add exhaustive default-cases.
3. **Renderer/blink patch footprint.** The alt-only gesture decision is
   in blink (`navigation_policy.cc`); a Koan feature in
   `blink::features` grows the blink-chrome boundary visibility of our
   flag. Prefer the smallest, most-contained gate: a
   `blink::features` Feature flag (single file + enum) with about_flags
   entry only in Chrome.
4. **`SplitTabCreatedSource` is histogram-LINT-protected**
   (`split_tab_metrics.h:16,35`): renumbering or new values require
   `tools/metrics/histograms/metadata/tab/enums.xml` edits and may break
   the metrics-presets test (UNVERIFIED); prefer reusing
   `kLinkClick` if a new metric is not needed.
5. **Blocked-state semantics.** `SetWebContentsBlocked` is also read for
   browser capabilities (UNVERIFIED hidden behaviors e.g. hiding the
   extension button or tab modal decoration) — verify both
   `web_modal` bookkeeping and Koan's visual target before adopting the
   path. The simpler profile (silently `SetVisible(scrim)` +
   `IgnoreInputEvents`) matches `actor_overlay_web_view` behavior and
   has no side-effects.
6. **Insert-as-tab migration vs opening partners.** `InsertWebContentsAt`
   skips some "ADD_ACTIVE"-related bookkeeping that `AddWebContents`
   uses (tab insertion history, `focus` notification UNVERIFIED); test
   opener chains after promote (Ctrl+Shift+T after "expand to tab" and
   tab-restore after crash).
7. **Per-browser per-window glue.** The current
   `KoanBrowserViewController` (per-window, owned by `BrowserView`)
   needs to track the ACTIVE tab's pane; glance state must follow pane
   switches (multi-pane) and pane-close. If a pane closes while a glance
   is open, mimic `actor_overlay_web_view.cc` closing logic.
8. **Blink-side input-edge guard.** The alt-only click gesture requires
   `Alt` held at click time; window-manager-global Alt behaviors (e.g.
   Alt+Space on some Linux wm) may eat some Alt-clicks on specific
   systems (UNVERIFIED). Test Wayland and X11.
9. **UNVERIFIED: `views::FocusManager`-wide Esc when the Glance page has
   focus.** The delegate path only catches keys the page does NOT
   consume. Some sites consume Esc via
   `event.preventDefault()`, which would swallow Esc. Fallback fix:
   register a `views::FocusManager`-owned `AcceleratorTarget` while
   Glance is open (see Q5.4) — cost: a focus-manager extra entry; verify
   ordering vs WebContents Accelerator handling (`web_view->
   set_allow_accelerators(true)`) before shipping.
10. **Drop-target behavior for tabs is today always-enabled by default
    pref.** Koan shipping it silently changes the visual tab UX (users
    may be surprised). Mitigation: keep the pref as-is and add Koan
    documentation rather than new code.

---

## Pending verification TODOs

- [ ] Count every `switch` on `WindowOpenDisposition` needing a
      `KOAN_GLANCE` case (diff-based, like the `NEW_SPLIT_VIEW`
      introduction).
- [ ] Verify the blinking changes when `kWebNavigationPolicySplitView`
      was introduced (use `git log -S kNavigationPolicySplitView` — read
      only).
- [ ] Verify whether `blink::features` requires a separate registration
      to be reachable from `chrome://flags` or whether about_flags
      handles blink features generically (UNVERIFIED).
- [ ] Check `SetWebContentsBlocked`'s full impact when a pane is in an
      unwanted state (modal records, browser capabilities) before using it.
- [ ] Confirm how a `views::FocusManager` accelerator competes with the
      focused WebContents (`set_allow_accelerators`) — write a manual test
      in phase 3 (UNVERIFIED).
- [ ] Verify and document what happens when dragging a tab from the
      vertical strip onto page content: the drag should reach the
      `drop_target_controller` and create a split
      (`browser_view.cc:2510-2534` exists to support this).
