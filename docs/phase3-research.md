# Phase 3 research: Glance + split view

Based on ~/work/koan/chromium/src (Chromium 158.0.8111.0). All `file:line`
references relative to `chromium/src`. Read-only research; drafted
incrementally. Some sections are still pending; see TODOs.

## Q1. Upstream features that host a secondary WebContents over/beside the page

Candidates inspected, with where each lives and how it hosts its target:

### 1.1 Link preview (desktop) — deleted
`git log -S LinkPreview --all` finds no code commits (the single hit is a PGO
profile roll, `67e29b8287`); `rg -l "LinkPreview"` across `chrome/`,
`components/`, `content/` matches only
`chrome/browser/renderer_context_menu/render_view_context_menu_browsertest.cc`.
The desktop link preview anchor-bubble feature is gone in this checkout;
not a candidate.

### 1.2 Actor overlay (Glic) — in-page views::WebView overlay on ContentsContainerView
The best in-tree template for a Glance overlay.

- `chrome/browser/ui/views/frame/contents_container_view.cc:136-140` —
  inside `if (base::FeatureList::IsEnabled(features::kGlicActorUi) &&
  features::kGlicActorUiOverlay.Get())`, an
  `ActorOverlayWebView` (a `views::WebView`) is created and added to the
  ContentsContainerView, i.e. stacked **on top of** contents_view, not
  replacing it. It is child of the per-pane container, so in split view each
  pane is wrapped separately.
- `chrome/browser/actor/ui/actor_overlay_web_view.h:21-30` —
  `class ActorOverlayWebView : public views::WebView`; created with the
  `BrowserWindowInterface`, `SetBrowserContext(profile)` in the ctor
  (`actor_overlay_web_view.cc:30-31`) so it lazily creates *its own*
  WebContents via `LoadInitialURL(GURL(chrome::kChromeUIActorOverlayURL))`
  on first show (`actor_overlay_web_view.cc:53-55`).
- Show/hide dance (`actor_overlay_web_view.cc:52-87`):
  - `ShowUI()` — disables input to the underlying tab with WebContents'
    `ScopedIgnoreInputEvents` (`actor_overlay_web_view.cc:59-61`),
    makes the WebContents background transparent
    (`SetPageBaseBackgroundColor(SK_ColorTRANSPARENT)` +
    `RenderWidgetHostView::SetBackgroundColor`, lines 64-69), then
    `SetVisible(true)` + `web_contents()->WasShown()`.
  - `CloseUI()` — `SetVisible(false)`, resets the
    ScopedIgnoreInputEvents, `WasHidden()` (lines 76-87).
  - FocusBehavior NEVER + accessibility-ignored while hidden
    (`actor_overlay_web_view.cc:32-33`).
- Layout: the opaque ancestor logic that keeps the overlay aligned with the
  contents is in `contents_container_view.cc:555-558`
  (`if (actor_overlay_web_view_)` → same `child_layouts` wobble entry as
  contents; `contents_container_view.cc:543-546` does the same for
  `contents_scrim_view_`).

### 1.3 AI overlay dialog — bare views::WebView sized by its WebContents
`contents_container_view.cc:118-127` — behind `features::kAiOverlayDialog`
another `views::WebView(browser_view->GetProfile())` child is added with
`EnableSizingFromWebContents(gfx::Size(1, 1), gfx::Size(800, 600))`.
Relevant if Koan wants the Glance card to size itself from page layout;
otherwise ignore.

### 1.4 Lens overlay — a bare View in MultiContentsView; actual WebView is a widget/child
- `chrome/browser/ui/views/frame/multi_contents_view.cc:131-137` —
  `lens_overlay_view_` is an ordinary `views::View` with a FillLayout,
  added as a child of MultiContentsView after the contents views so it
  draws above them; wobble-registered at `multi_contents_view.cc:558-561`.
- The Lens WebView itself is mounted by `LensOverlayController` into that
  container view (see `chrome/browser/lens/context/contextual_ui` chrome/browser/lens/…,
  `lens_overlay_controller.cc`) — UNVERIFIED exact file:line; the pattern
  worth copying is "a container child in the MultiContentsView + controller
  that swaps WebContents in/out of a views::WebView".
- Equivalent in-pane overlays registered the same way (and referenced
  through ContentsContainerView, which owns identical children):
  `actor_overlay_web_view()` accessor `contents_container_view.h:83-84`,
  `read_anything_immersive_overlay_view()` `…h:86-87`, both laid out at
  `contents_container_view.cc:555-558` and `…cc:594-598`.

### 1.5 Side panel — adjacent, not over
Side panel content lives in the browser window layout, not over the page:
`browser_view.cc:1044-1047` has a side-panel transition scrim. Not the
Glance shape.

### 1.6 Picture-in-Picture — separate widget, not in-window
`ScrimView` is used by `chrome/browser/ui/views/overlay/video_overlay_window_views.{h,cc}`,
i.e. a real native widget; PiP content goes through
`ui::emoth` — actually `chrome/browser/ui/views/overlay/`, an
own frames. Not suitable for Glance (no browser chrome, no tab promotion);
mention only because its toolbar (`video_overlay_window_views`) is a good
reference for the Glance "close/expand" button row over a WebContents.

### 1.7 Recommendation
Glance template = ActorOverlayWebView pattern
(contents_container_view.cc:136-140) applied to a Koan-owned view:

- `//koan/browser/ui/views/koan_glance_controller` owns a
  `views::WebView` (created with the Profile) added as child of the
  ContentsContainerView above `contents_view_`, next to the actor overlay,
  gated behind `koan::features::kKoanGlance`.
- WebContents: reuse ActorOverlayWebView's "lazy `LoadInitialURL` +
  SetBrowserContext" recipe (`actor_overlay_web_view.cc:30-55`). A Glance
  WebContents is a plain, detached content::WebContents (see Q3 for
  promotion).
- Per-page dim: `contents_scrim_view_` already exists per pane
  (`contents_container_view.cc:130-131`, accessor `contents_container_view.h:77`);
  Koan taps the same visibility control via its own controller rather than
  the TabInterface::IsBlocked-based ScrimViewController (Q5).
- Input blocking of the background page:
  `content::WebContents::ScopedIgnoreInputEvents`
  (`actor_overlay_web_view.cc:59-61,76-87`).

TODO: confirm LensOverlayController mount file:lines.

## Q2. How link clicks with modifiers flow; where Koan can intercept Alt+click

### 2.1 The decision is made in the renderer, in blink
`third_party/blink/renderer/core/loader/navigation_policy.cc:51-79`,
`NavigationPolicyFromEventModifiers(button, ctrl, shift, alt, meta)`:

```
#if BUILDFLAG(IS_MAC)
  const bool new_tab_modifier = (button == 1) || meta;
#else
  const bool new_tab_modifier = (button == 1) || ctrl;
#endif
  if (new_tab_modifier && alt && !shift) {
    return kNavigationPolicySplitView;          // line 61-64
  }
  if (!new_tab_modifier && !shift && !alt) {
    return kNavigationPolicyCurrentTab;
  } else if (new_tab_modifier) {
    return shift ? kNavigationPolicyNewForegroundTab
                 : kNavigationPolicyNewBackgroundTab;
  }
  return shift ? kNavigationPolicyNewWindow : kNavigationPolicyDownload;   // line 75
```

- **Alt+click (no ctrl/shift) ⇒ kNavigationPolicyDownload.**
- **Ctrl+Alt+click (⌘+Alt on Mac) ⇒ kNavigationPolicySplitView** — upstream
  already has a "Ctrl+Alt click opens a split" gesture.
- Shift+click ⇒ kNavigationPolicyNewWindow.
- Synthesized events are sanity-checked in
  `NavigationPolicyFromEvent()` (`navigation_policy.cc:93-131`): a
  keyboard/gesture event's policy is only honored if
  `CurrentInputEvent::Get()` agrees (the real input event; thread-local in
  `third_party/blink/renderer/core/events/current_input_event.{h,cc}`,
  `CurrentInputEvent` is `CORE_EXPORT`).

### 2.2 Renderer→browser IPC split (why the browser can't just "see" Alt+click)
`content/renderer/render_frame_impl.cc:5828-5847` (`DuringNavigation` `…`):

```
// Depending on navigation policy, send one of three IPCs to the browser
// process: DownloadURL handles downloads, OpenURL handles all navigations
// that will end up in a different tab/window, and BeginNavigation handles
// everything else.
if (info->navigation_policy == blink::kWebNavigationPolicyDownload) {
  …
  frame_->DownloadURL(...);           // line 5837
  return;
}
if (info->navigation_policy != blink::kWebNavigationPolicyCurrentTab) {
  OpenURL(std::move(info));           // line 5843
  return;
}
```

- Consequences for Koan:
  - **Alt-only click never produces an `OpenURL` journey in the browser** —
    the renderer asks for a download over the `FrameHost::DownloadURL`
    mojo; no `OpenURLFromTab`, no `Navigate()`.
  - `Shift+click` does produce `OpenURL` with
    `WindowOpenDisposition::NEW_WINDOW` pre-baked in the params
    (`render_frame_impl.cc:6142`:
    `params->disposition = NavigationPolicyToDisposition(policy)`; the
    mapping is `render_frame_impl.cc:1273-1299`).
  - The renderer policy enum maps 1:1 onto `WindowOpenDisposition`
    (`render_frame_impl.cc:1281-1303` —
    `kWebNavigationPolicySplitView → NEW_SPLIT_VIEW` at line 1298-1303; the
    disposition enum's `MAX_VALUE = NEW_SPLIT_VIEW` is
    `ui/base/window_open_disposition.h:30,32`).

### 2.3 Browser-side handling of those dispositions (reference chain)
- Chrome's delegate intercept: `BrowserWebContentsDelegate::OpenURLFromTab`
  (`chrome/browser/ui/browser_web_contents_delegate/browser_web_contents_delegate.cc:529-600`).
  Line 549-563 special-cases `NEW_SPLIT_VIEW` when the source tab is
  already split (navigates the other pane instead of adding a tab), then
  hands the rest to `ChromePopupNavigationDelegate` + `chrome::Navigate()`.
- `Browser::OpenURLFromTab` delegates to the above
  (`chrome/browser/ui/browser.cc:554`).
- `chrome/browser/ui/navigator/browser_navigator.cc`:
  - `GetOrCreateBrowser()` switch at line 303 treats `NEW_SPLIT_VIEW` like
    new-tab dispositions for target-window selection;
  - `NormalizeDisposition()` at line 431/447/485: SAVE_TO_DISK and
    IGNORE_ACTION "result in no navigation" (line 429-431),
    `NEW_SPLIT_VIEW` clears `AddTabTypes::ADD_ACTIVE` (line 485-490);
  - actual split insertion at `browser_navigator.cc:1049-1077` (Q3).
- `ui::DispositionFromClick` (`ui/base/window_open_disposition_utils.cc:23-51`)
  — the views-side mirror used by **chrome-owned** click surfaces only
  (WebUI handlers, bookmarks/bookmark-bar clicks, WebApp UI): it interprets
  the same codes (split at lines 35-38, alt→`SAVE_TO_DISK` at line 48).
  NOT used for page-content link clicks — page links are decided in blink
  (§2.1). This is important for scoping the feature: "Alt+click on a link"
  always means blink, never chrome.

### 2.4 Where the modifier context can be re-derived by Koan (hook options)

Options, in increasing upstream-invasiveness:

**(a) Change the disposition decision in blink itself.** The decision site
is a 3-line edit in
`third_party/blink/renderer/core/loader/navigation_policy.cc:64/75`
(`NavigationPolicyFromEventModifiers`). `Alt+click` today falls into
`kNavigationPolicyDownload` (line 75). Under a Koan gate, map the
alt-only case to `kNavigationPolicyKoanGlance` (new) instead of
`kNavigationPolicyDownload`. Blink cannot include `chrome/browser/...`
features, so the gate must be a blink-visible mechanism (own blink Feature
in `blink::features`, or a command-line switch the renderer receives).
UNVERIFIED: whether adding a cross-component Feature from a Kean ruleset
into `blink::features` is acceptable in blink
(`third_party/blink/renderer/core/loader/navigation_policy.cc` includes
`third_party/blink/renderer/platform/runtime_enabled_features.h` and
`third_party/blink/public/...` only).

**(b) Intercept where `RenderFrameImpl` sends the download IPC.**
`content/renderer/render_frame_impl.cc:5833-5840` is a unique point: the
browser-side process already knows (via the IPC target frame) that it was
an Alt-only click, because we know both the URL and that
`info->navigation_policy == kWebNavigationPolicyDownload`. A different
`WindowOpenDisposition` (via a particle in the same `info`
object, e.g. by tracking a Koan field in the
`blink::mojom::OpenURLParams` or by custom call into
`koan::` injected chrome renderer client) can be added here.
infra: gives access to `content::WebContentsDelegate`.
This is feasible at the cost of a chrome/ change set rather than relative
to other kupuna.

**(c) Preserve the upstream `NEW_SPLIT_VIEW` pipeline and add one more
enumeration point.** This is the shape upstream itself gave "click a link
with Ctrl+Alt to open a split", and it minimises churn because every
relevant switch already knows `NEW_SPLIT_VIEW`. Steps:
   1. Add `KOAN_GLANCE` to `WindowOpenDisposition` **before** the
      `MAX_VALUE = NEW_SPLIT_VIEW` line in
      `ui/base/window_open_disposition.h:30-32`
      (1 line, keeps `MAX_VALUE` last).
   2. Add `kWebNavigationPolicyKoanGlance` to blink and map it in
      `blink/renderer/core/loader/navigation_policy.{h,cc}`
      (the same file where `kNavigationPolicySplitView` lives today,
      `navigation_policy.cc:62/155/204`).
   3. Add the mojom enum value + traits mapping in
      `ui/base/mojom/window_open_disposition.mojom` +
      `ui/base/mojom/window_open_disposition_mojom_traits.h`.
   4. In `content/renderer/render_frame_impl.cc:1281-1303`
      (`NavigationPolicyToDisposition`) add the case, and add the policy
      to the `ConsumeTransientUserActivation()` list at
      `render_frame_impl.cc:6155-6170` if the click opens a Glance over
      the current page.
   5. **Single chrome-side consumer hook**:
      `BrowserWebContentsDelegate::OpenURLFromTab`
      (`chrome/browser/ui/browser_web_contents_delegate/browser_web_contents_delegate.cc:549`) —
      immediately beside the NEW_SPLIT_VIEW special case — returns
      `koan::Glance::MaybeOpenFromOpenURLParams(browser_, source,
      params)` and returns `source` WebContents (skipping further
      navigation) or nullptr if Koan is off. Every other catch (omnibox,
      bookmark bar, tabstrip mid-click…) can then be re-routed through
      the same delegate via their own distance from `TabStripModel`
      windows, or disallowed for those surfaces.

**(d) Just intercept the click at the views layer (webcontents-views
click filtering)** — NOT viable for page links: page clicks are consumed
by `RenderWidgetHostView` and don't hover through any chrome-owned
`views::RequestHandler`'s OnMouseEvent(). Buried xref: every
browser-side mouse affordance (hovercards) creates a NEW bubble rather
than catching the click prior to the renderer.

### 2.5 Recommendation

Go with (c). Rationale: it reuses the exact plumbing upstream added
recently for "click a link with Ctrl+Alt to open a split view"
(`navigation_policy.cc:61-64` → `render_frame_impl.cc:1298` →
`browser_navigator.cc:303`), since the enum extension takes care of all
the mojom/delegate plumbing that already exists — the single
`chrome-side consumer touchpoint` is small: `KOAN_GLANCE` next to
`NEW_SPLIT_VIEW` in `OpenURLFromTab` (`browser_web_contents_delegate.cc:549`), 
with the blink decision gated by command line/flag in the renderer.
Also keeps Alt+click = DOWNLOAD as fallback if Koan is off, preserving
upstream behavior.

TODO loop before patch writing:
- [ ] Count all `switch (WindowOpenDisposition)` statements needing a
      KOAN_GLANCE default case
      (`browser_navigator.cc`, `browser_web_contents_delegate.cc`,
      omnibox, blocked-content, web-app capture, session-restore,
      `content/browser/web_contents/web_contents_impl.cc`).
- [ ] Confirm the exact list of files changed upstream with the
      `kWebNavigationPolicySplitView` introduction as a diff
      template, UNVERIFIED.
- [ ] Choose blink-side gating (blink::features vs renderer command
      line switch).
- [ ] Note that with feature ON, Alt+click never triggers download;
      we lose Alt+click Download. Confirm with product design that this
      is acceptable.
