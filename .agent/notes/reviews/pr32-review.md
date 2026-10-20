# PR #32 review: hover-revealed window controls (feat/hover-window-controls)

Reviewer: review sub-agent (static review; no build/run). Repo `/home/chaitanya/work/koan`, branch
`feat/hover-window-controls`, diff `origin/develop...feat/hover-window-controls`.
Chromium 158 tree: `/home/chaitanya/work/koan/chromium/src` (read-only). All `chromium/src/...` line
numbers below are from that tree; koan paths are relative to the repo.

## What the PR does

- `koan_content_card.cc:25-33`: in hidden-toolbar modes the card's top inset is `kContentCardMargin`
  (8 px) while `IsCaptionStripCollapsed()`, else the old `max(margin, GetCaptionButtonAreaSize().height())`.
- Single-toolbar activation (`koan_compact_mode_controller.cc`, `Activate`) creates a `KoanHotZone`
  (`caption_hot_zone_`) spanning the top edge from `multi_contents_view()->x()` rightwards; hover
  calls `SetCaptionStripRevealed(true)`, which starts a 100 ms repeating timer that collapses the
  strip 400 ms after neither the hot zone nor a caption button is hovered (`CheckCaptionStripHide`).
- `UpdateCaptionButtons()` finds direct children of the current `BrowserFrameView` whose class is
  `"ImageButton"` / `"FrameCaptionButton"`, sets `SetCanProcessEventsWithinSubtree(shown)`, and
  fades each on a layer (opacity 0 ↔ 1, 120 ms) — the button's own visibility stays driven by the
  frame's layout. It runs on every `OnBrowserViewLayout` (the frame view is recreated on theme
  changes, per `.agent/notes/build.md:101-102`).
- `koan_window_controls.cc:14-24` unchanged: `GetCaptionButtonAreaSize` reads only
  `GetBrowserLayoutParams().trailing_exclusion.content`.

The event path this interacts with (established from the sources, needed to judge the findings):

- Mouse events on Linux/Aura get a non-client classification at the host before views dispatch:
  `DesktopWindowTreeHostLinux::DispatchEvent` computes `GetNonClientComponent` (widget.cc:2297-2305
  → `NonClientView::NonClientHitTest`, non_client_view.cc:99-107 →
  `OpaqueBrowserFrameView::NonClientHitTest`) and marks events that are not HTCLIENT/HTNOWHERE with
  `EF_IS_NON_CLIENT`, then hands them to `WindowEventFilterLinux::HandleLocatedEventWithHitTest`
  (desktop_window_tree_host_linux.cc:399-416). That filter acts on HTCAPTION (drag, double-click
  maximise) and on HTMINBUTTON/HTMAXBUTTON/HTCLOSE only for **right-click** (window menu);
  left-presses on HTCLOSE fall through to normal views dispatch (window_event_filter_linux.cc:42-140,
  `ui::CanPerformDragOrResize(HTCLOSE)` is false).
- Views dispatch to caption buttons goes through `ViewTargeterDelegate::TargetForRect`
  (view_targeter_delegate.cc:34-56): children in reverse z-order, skipping views with
  `GetCanProcessEventsWithinSubtree() == false`, first intersecting child wins.
- Structure of the frame: the caption buttons are added by `InitViews` at frame-view creation
  (browser_frame_view_factory_views.cc:90/112 → opaque_browser_frame_view.cc:159+), and the
  ClientView is inserted **afterwards** as the last child of the frame view
  (non_client_view.cc:42-56 `SetFrameView` → `frame_view_->InsertClientView(client_view_)` →
  frame_view.cc:145-147 `AddChildViewRaw`). So within the frame view the ClientView subtree is
  **on top of** the caption buttons in z-order. The overlay view (
  TopContainerOverlayView, non_client_view.cc:74) is above the frame view, but Koan blocks its
  events while the toolbar is hidden (browser_view.cc:580-594 requires a hit child; koan
  compact-mode controller sets `top_container->SetCanProcessEventsWithinSubtree(revealed)`,
  koan_compact_mode_controller.cc:630/368), so strip clicks fall through to the ClientView subtree,
  in which KoanHotZone/MultiContentsView are the deepest targets.

## Findings

### B1 (Blocker) — the revealed caption buttons cannot be clicked: they sit below the ClientView in z-order

Static analysis of the targeting chain says left-clicks can never reach the frame's caption buttons
in Koan:

1. Buttons are children of the frame *view*; the ClientView `BrowserView` is also a child of the
   same view, added last (frame_view.cc:145-147, non_client_view.cc:51-52), i.e. above the buttons.
2. `TargetForRect` returns the **first** (topmost) intersecting child's target immediately for
   point-based targeting (view_targeter_delegate.cc:66-70); it does not fall through to the frame's
   buttons behind the ClientView.
3. Koan's client view covers the top strip: this PR moves the card up to the 8 px gutter
   (`koan_content_card.cc:25-33`), and the pre-PR notes record "the frame's caption buttons paint
   ABOVE the client view" (`.agent/notes/status.md:79-81`).
4. The upstream escape hatch used by Window Controls Overlay is exactly this problem: it re-adds
   every button to the **top** of the frame view's children and paints them on a layer
   (opaque_browser_frame_view_layout.cc:222-233, `host->AddChildView(button)` +
   `SetPaintToLayer()`), noting `OpaqueBrowserFrameView::NonClientHitTest` must then route those
   regions to HTCLIENT so the client-side keeps them (opaque_browser_frame_view.cc:383-394).

Consequence: the PR's core promise — "the window buttons appear at the top edge on hover" — likely
delivers paint but not function: hovering reveals the buttons, clicking them does nothing (the
click targets `KoanHotZone`/`MultiContentsView`). The e2e added in this PR only asserts view
bounds (`scripts/e2e/layout.mjs:117-123`, `shortcuts.mjs`), not clicks, so this has not been
verified. Note this is **pre-existing** (buttons live below the ClientView today too) — but this PR
is the change that makes the buttons the *primary* affordance, so it must land together with the fix.

Concrete scenario: single-toolbar mode, hover top edge, click the revealed close button — nothing
happens; user must Alt+F4 / WM menu.

Fix: on reveal (or once, at activation), re-add the caption buttons above the client view and keep
them there while `caption_strip_collapsible_` (mirroring `SetWindowControlsOverlayEnabled`'s
`SetWindowControlsOverlayEnabled` at opaque_browser_frame_view_layout.cc:222-233), restore z-order
on `Deactivate`. The existing `SetCanProcessEventsWithinSubtree(shown)` then does exactly what the
PR comment says ("let a hidden one ignore the pointer"). Verify with a real click (XTest) close +
the Fitts-corner case.

### B2 (Blocker) — leading-edge button layouts (GNOME "close:") make the strip un-revealable and reserve nothing

`GetCaptionButtonAreaSize` (koan_window_controls.cc:14-24) reads only `trailing_exclusion`. On
Linux the frame fills leading **and** trailing exclusions separately from
`WindowButtonOrderProvider` (browser_frame_view_linux.cc:154-198); with GNOME's all-leading
layout (e.g. `close:`), `trailing_exclusion` is empty:

1. `OnCaptionHotZoneHovered` early-returns when the size is empty
   (`koan_compact_mode_controller.cc`, `OnCaptionHotZoneHovered: ~728`) → the strip can never be
   revealed; the buttons are permanently faded to opacity 0 → **no mouse way to close/minimise/
   maximise at all** in single-toolbar (+compact-keeps-nav) mode.
2. Even for split layouts ("close:minimize,maximize:" — buttons on both sides), the hot zone starts
   at `multi_contents_view()->x()` (`LayoutHotZones`), so hovering the leading (left) strip never
   reveals, and the revealed inset (which only encodes the trailing height) leaves the leading
   buttons overlapping the card/sidebar area.

Scenario: user on GNOME with the "close:" button layout toggles to single toolbar — window
controls silently vanish (collapsed) and cannot be brought back by hovering.

Fix: read both exclusions; strip height = `max(leading.height, trailing.height)`; when
`CaptionButtonsOnLeadingEdge()` (browser_frame_view_linux.cc:200-204), extend the hot zone left to
cover the leading strip (over the sidebar's own top row, which already has ×/nav buttons — decide
precedence) or reserve the leading gutter like the trailing one. Add a GNOME-order e2e case
(WindowButtonOrderProvider is overridable in tests).

### S1 (Should-fix) — while collapsed, the faded buttons are still hit-tested by the frame; right-click pops the WM menu (and would close on Windows)

`OpaqueBrowserFrameView::HitTestCaptionButton` checks only `button->GetVisible() &&
GetMirroredBounds().Contains(point)` (opaque_browser_frame_view.cc:121-124) — it ignores layer
opacity and `SetCanProcessEventsWithinSubtree`. So while collapsed:

- `NonClientHitTest` still returns HTCLOSE/HTMAXBUTTON/HTMINBUTTON for the hidden buttons
  (opaque_browser_frame_view.cc:396-406). On Linux this surfaces as:
  - right-click on the page's top-right corner (over the invisible close button) opens the window
    controls menu with the strip collapsed (window_event_filter_linux.cc:94-103) — inconsistent
    with the "hidden" state;
  - left-clicks are safe (left-press HTCLOSE is not acted on by the filter — see path section —
    and views dispatch skips the button because of `SetCanProcessEventsWithinSubtree(false)`,
    view_targeter_delegate.cc:48-49), so **no accidental close on Linux today**.
- If/when Koan is ever built for Windows/ChromeOS, the same HTCLOSE goes through the WM
  (`browser_frame_view_win` / BrowserFrameViewChromeOS use the same `OpaqueBrowserFrameView`
  hit test), where the OS itself closes the window on left-press over the hidden button — click on
  page's top-right corner = window closed. Do not copy this scheme to those platforms without a
  frame-side gate.

Also note hiding via `SetVisible(false)` is not an option for the same reason the PR chose layers:
`OpaqueBrowserFrameViewLayout` re-shows buttons during its Layout (the PR comment in
`UpdateCaptionButtons` says exactly this), so a visibility race just malfunctions differently.

Fix: gate the frame-side hit test. Cleanest is a small hook patch (Koan already patches
browser_view.cc / browser_view_tabbed_layout_impl.cc, so there's precedent): add a virtual to
`BrowserFrameView` (e.g. `CaptionButtonsInteractive()`) consulted by `HitTestCaptionButton`, set
from the controller, instead of string-matching children from outside the frame.

### S2 (Should-fix) — toolbar hot zone and caption hot zone fully overlap in compact+single-toolbar mode

In compact mode that keeps the sidebar nav, both `toolbar_hot_zone_`
(bounds [0, width) × [0, gutter.top)) and `caption_hot_zone_` ([contents.x, width) × [0,
gutter.top)) occupy the same pixels. Hovering the top edge reveals **both** the toolbar and the
caption strip; then moving the pointer down onto the revealed toolbar sends exit to the hot zones
(they shrink to the same strip and the deep target becomes the toolbar subtree), and the caption
strip collapses 400 ms later while the toolbar stays — or vice versa depending on order — leaving
a half-hidden state and 24 px of page jump mid-interaction (the two independent 100 ms hide timers
also race). Fix: a defined precedence — in compact+keeps-nav, either share one reveal state (a
single `SetStripsRevealed`) or let the toolbar hot zone own the edge and have the caption strip
follow it.

### N1 (Nit) — frame-view recreation can flash fully-visible buttons for a frame

`UpdateCaptionButtons` runs on `OnBrowserViewLayout`, but a recreated frame view (theme change,
e.g. workspace tint per `.agent/notes/build.md:101-102`) starts with no layer and opacity 1 and
full `CanProcessEventsWithinSubtree`. Between `SetFrameView` and the next layout pass the collapsed
strip may paint its buttons at full opacity for a frame. Belt-and-braces: observe frame changes
(the controller can detect `GetFrameView()` pointer changes in the next layout, which it already
does) or hook `OnBrowserViewInitViewsComplete`.

### N2 (Nit) — residual layers and string-matched class names

- Once faded, buttons keep `SetPaintToLayer` layers forever (`UpdateCaptionButtons` never destroys
  them). Harmless, but note upstream's WCO code explicitly `DestroyLayer()`s buttons when it
  restores them (opaque_browser_frame_view_layout.cc:234-236); consider the same on
  `Deactivate`/reveal-in-restore path so a later WCO/PWA path doesn't meet an unowned layer.
- `IsCaptionButton` matches by string ("ImageButton"/"FrameCaptionButton",
  koan_compact_mode_controller.cc:96-99). Both are stable upstream class names for the two Linux
  styles, but a `SetID`/identifier or a virtual on `BrowserFrameView` is more robust; also fine to
  keep with a `docs/REBASE.md` note listing this probe next to the other probes.

### Answers to the specific checks

1. **Opacity / `SetCanProcessEventsWithinSubtree` on frame caption buttons.** Nothing in the frame
   resets them outside WCO enable/disable (`OpaqueBrowserFrameViewLayout::SetWindowControlsOverlayEnabled`
   — PWA-only; Koan windows aren't PWAs here). View dispatch is correctly blocked by
   `SetCanProcessEventsWithinSubtree(false)` (view_targeter_delegate.cc:48-49). But the *non-client*
   hit test ignores both (opaque_browser_frame_view.cc:121-124, 396-406) — see S1: HTCLOSE etc. are
   still reported; on Linux only right-click acts on it (menu), no accidental close from left
   clicks; on Windows it would be an accidental-close blocker. Additionally the buttons are
   currently unreachable for left clicks *entirely* (B1), which is why S1 hasn't been noticed.
2. **Leaks/loops.** No re-entrancy loop: `OnContentCardChanged` only `InvalidateLayout()`s (deferred);
   `UpdateCaptionButtons` from layout is idempotent (target-opacity guard) and opacity animations
   don't invalidate layout; `SetPaintToLayer` only fires once per button. Hover reveal can't flicker:
   reveal expands the hot zone to the caption height, so a pointer at y<caption stays "in" it; the
   collapse path only shrinks it after the pointer already left. The enter/exit dispatch itself is
   not gated by `EF_IS_NON_CLIENT` (root_view.cc:915-1000 only gates cursor updates), so the hot
   zone receives enter/exit even over HTCAPTION/HTCLIENT-classified gutter. The one real interaction
   defect is the zone overlap (S2). No leaks: `caption_hot_zone_` is removed with
   `RemoveChildViewT` on Deactivate, the timer is a member and auto-stops; re-activation is guarded
   (`UpdateActive` Deactivate→Activate, no double-create).
3. **Drag/resize from the top edge.** Unchanged by the PR: `GraphicsNonClientHitTest` is geometric —
   `BrowserView::NonClientHitTest` (browser_view.cc:4638-4851) and the frame's tail
   (opaque_browser_frame_view.cc:408-421) compute HTCAPTION/HTTOP from frame padding and toolbar
   position, while the PR only moves the card. `BrowserFrameViewLinux::NonClientHitTest` keeps
   HTTOP for the top 4 px when shadows are off (browser_frame_view_linux.cc:116-123), and
   `GetHTComponentForFrame`'s HTCAPTION fallback covers the gutter. Note: if the actual Koan build
   doesn't expose HTCAPTION over the gutter (toolbar transform can shift `GetClientAreaTop()`),
   that's pre-existing — worth one manual drag check while collapsed and while revealed.
4. **Maximized windows and leading layouts.** Maximized: fine — buttons move to the very top with
   Fitts-corner bounds, the revealed inset is the caption height, and the base
   `GetBrowserLayoutParams` computes caption height as `caption.bottom - visual_client_area.y`
   (browser_frame_view.cc:130-152). Leading layouts: broken end-to-end — see B2 (that is the "does
   the exclusion cover both" answer: the Linux subclass fills both, but Koan reads only trailing).
5. **Deactivate/cleanup.** OK: `Deactivate` stops the timer, resets `caption_unwanted_since_`,
   removes the hot zone, and flips collapsible off *before* calling `UpdateCaptionButtons`, so
   buttons are restored to opacity 1 / events on and continue to be restorable after frame
   recreation. Window-close while revealed is covered by the controller teardown
   (browser_view.cc:1121-1125 hooks) and the widget-owned views dying with it.

## Verdict

Blockers: B1 (revealed buttons not clickable — verify by hand, then fix via the WCO re-add pattern)
and B2 (leading button layouts unusable); S1/S2/N1/N2 otherwise clean design with correct
timer/loop/deactivation handling.
**Verdict: Request changes — merge-blocked on B1 (button clickability, needs manual click
verification + fix) and B2 (leading-edge button layouts); the rest is Should-fix/Nit.**
