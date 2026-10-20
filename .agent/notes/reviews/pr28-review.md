# PR 28 review — glance card opens full-size (fix/glance-card)

Scope: `src/browser/ui/views/koan_glance_controller.cc`, `scripts/e2e/glance.mjs`, `BUILD.gn`.
Chromium refs: `chromium/src` @ 158. Findings verified against `origin/develop...fix/glance-card`.

**First, the diagnosis is right.** The old animation scaled `card_`'s layer, and
`View::GetTransform()` returns the *layer* transform when the view has one
(`ui/views/view.cc:768-780`); `View::ConvertRectToParent` maps through it
(`view.cc:1383-1389`), and `NativeViewHost::Layout` feeds
`ConvertRectToWidget(GetContentsBounds())` into `ShowWidget`
(`ui/views/controls/native/native_view_host.cc:180-186`). So during the old
animation the hosted page was sized to 0.94× and no later layout shrunk it back
without the transform still being active — the page was frozen at 94%. Animating
the hosted window's own layer is the correct target.

`kNativeViewHostManagesLayers` is enabled by default in 158
(`ui/views/views_features.cc:67`) and Koan doesn't disable it, so the wrapper in
play is `NativeViewHostAura` (not the clip-window variant),
`native_view_host_aura.cc:313-319`. All the `ShowWidget` analysis below is for
that wrapper — worth stating in the PR because the two wrappers behave
differently (`NativeViewHostAuraWithClipWindow::ShowWidget` at
`native_view_host_aura_with_clip_window.cc:255-291` does the same
transform-write, so the conclusions hold either way).

## 1) Animating the hosted window's layer

**Safe overall: no blockers found.** Verified hazards:

- **Can `ShowWidget` stomp the animated transform? Yes, but harmlessly.**
  `NativeViewHostAura::ShowWidget` computes `original_transform_ * scale(w/native_w)`
  and writes it via `native_view()->SetTransform(...)`, which forwards to
  `layer()->SetTransform(...)` (`ui/aura/window.cc:577-584`) — the same value the
  animation animates (`native_view_host_aura.cc:251-261`). In the normal case
  `native_w == w` (`native_view_host.cc:181-186` sizes it from the same
  `ConvertRectToWidget`) so the computed transform is identity and the write is
  skipped ("Only set the transform when it is actually different",
  `native_view_host_aura.cc:256-260`). If a relayout *does* happen mid-animation
  and the computed transform differs from the in-flight animated value, it snaps
  the page to full size for the next frame; the running
  `LayerAnimationSequence` keeps ticking and overwrites on the next step, so the
  end state is always the animated identity. Worst case is a visible 1-frame
  stutter. *(Should-fix below.)*
- **Resize during the 200 ms.** `ShowWidget` also calls
  `SetBounds({x, y, native_w, native_h})` (`native_view_host_aura.cc:262`); the
  scale pivot was computed once from the old size (`koan_glance_controller.cc:134-135`
  in the branch), so a resize mid-animation scales about a stale centre for the
  remaining frames, converging to identity at the end. Cosmetic; no follow-up
  layout is needed because the final transform is identity.
- **Destroy mid-animation.** No callbacks are registered on the builder
  (`koan_glance_controller.cc:127-138`), so nothing captures raw pointers.
  `AnimationBuilder::Observer` sets
  `RequiresNotificationWhenAnimatorDestroyed() == true` and self-deletes when
  detached from its last sequence (`ui/views/animation/animation_builder.cc:177-186`,
  `OnDetachedFromSequence` at 177-181), and the header documents layered
  destruction (`animation_builder.h:40-49`). Closing Glance mid-flight
  (`Close() → DestroyWidget → CloseNow`, controller lines 273-287, 353-359)
  detaches/hides the page window (`native_view_host_aura.cc:113-143`) and later
  deletes the WebContents; the animator destroys its sequences, the observer is
  notified and clean up. No UAF. The `ExpandToTab` path re-hosts a live window:
  `AttachNativeView` captures `original_transform_ = transform()`
  (`native_view_host_aura.cc:86`) — at most a mid-animation scale value becomes
  the "original", which the next `ShowWidget` writes-over since
  `transform != computed`.
- **Layer null-ness in the builder.** `SetOpacity/SetTransform` on a
  `ui::LayerOwner*` resolve `target->layer()` at call time
  (`ui/views/animation/animation_sequence_block.cc:145-158`); a null layer there
  would crash (`AddAnimation` deref). The aura Window is Init'd by the RVH, so
  `layer()` is non-null in practice, but the guard at
  `koan_glance_controller.cc:132` only checks `native_view()`. *(Nit below.)*

## 2) Is `native_view()` non-null / attached after `LayoutRootViewIfNecessary()`?

**Yes.** `SetWebContents` runs in the `KoanGlanceView` ctor
(`koan_glance_controller.cc:96`), before `SetContentsView`, so by the time
`Open()` reaches `widget_->Show()` (line 262) the WebContents' aura window
already exists and `NativeViewHost::AddedToWidget` has reparented it into the
Glance widget ("ReparentNativeView", `added-to-widget` path
`native_view_host_aura.cc:145-167`, attach-time reparent at 62-92). Order in
`Open()` (lines 259-265): `SetContentsView → LayoutWidget → Show →
LayoutRootViewIfNecessary → PlayOpenAnimation`. `LayoutRootViewIfNecessary`
(`ui/views/widget/widget.cc:2642-2656`) only flushes `root_view_->needs_layout()`,
which is exactly what guarantees `NativeViewHost::Layout → ShowWidget` has run
*before* `PlayOpenAnimation` reads `page->bounds().size()` for the pivot
(view ctor). Two consequences worth noting:

- If `needs_layout` was already false (layout ran during `Show()`), the call is
  a no-op and the bounds are still correct — the added call is safe in both
  branches.
- If `native_view()` were somehow null (RVH not created), the code correctly
  degrades: the `if` guard skips the page fade and the scrim/buttons still
  animate full-size (`koan_glance_controller.cc:132-138`). Good defensive
  shape, though see Nit 4 below about the missing `layer()` check.

## 3) Focus, input, hit-testing during/after the animation

**Works, including mid-flight, and I verified why.** Aura located-event
targeting converts the event point into each candidate window's coordinate
space through the *live layer transform*
(`ui/aura/window.cc:805-807`: `Layer::ConvertPointToLayer(...,
use_target_transform=true)` called from `Window::ConvertPointToTarget`; the
targeter Walks children via `ConvertEventToTarget`,
`ui/aura/window_targeter.cc:326-356`). So during the 0.94 zoom-in a click maps
through the scale and lands at the visually correct spot on the page; clicks on
the 3%-wide visual margin around the card fall through to the widget root →
scrim → close-on-release, consistent with the visual. The kept
`SetCanProcessEventsWithinSubtree(false)` on the scrim
(`koan_glance_controller.cc:86`) and the `OnMousePressed/Released` pair
(159-167) are unchanged.

After the animation ends the transform is exactly `gfx::Transform()` (identity),
which is also what `ShowWidget` would compute, so the "different transform"
write is skipped and hit-testing is in the steady-state path.

Focus is orthogonal to the layer transform: `RequestFocus` on the WebView
(line 265) routes through the aura FocusClient; the aura window is Show()n by
`ShowWidget` (`native_view_host_aura.cc:264`) before
`PlayOpenAnimation` runs. Escape is handled at the overlay window level
(`HandleKeyboardEvent`, controller lines 417-429) and works from the first
frame. See Nit 2 about the page being *invisible but interactive* while its
opacity animates.

## 4) Is `LayoutWidget` still called on every relevant change?

**Mostly, with one structural gap.** Coverage today:

- `Open()` calls `LayoutWidget()` (line 261) — before `Show()`, and again
  implicitly via the browser-view layout when the widget is created.
- `OnBrowserViewLayout()` (lines 337-341) — driven by
  `KoanBrowserViewController::OnViewBoundsChanged(multi_contents_view)`
  (`koan_browser_view_controller.cc:93-100`), i.e. any *outer- bounds* change
  of `MultiContentsView` re-runs `LayoutWidget`, which re-reads
  `UseContentCard`/`ContentCardInsets` (controller:361-373).

The gap: nothing re-runs `LayoutWidget` when `ContentCardInsets` changes
**without** an outer-bounds change. `ContentCardInsets` depends on
`IsToolbarHidden` (top inset vs caption-strip height) and
`IsCompactModeActive` (leading margin) (`koan_content_card.cc:28-43`). Concrete
scenario: toggle compact mode open/close while Glance is showing. When the
toggle hides/shows the toolbar or the sidebar, `MultiContentsView`'s outer
bounds change and `OnViewBoundsChanged` fires, so the common cases are covered.
But any inset change that leaves `MultiContentsView::bounds()` untouched (e.g.
the caption-button-strip size growing under `IsToolbarHidden` while the window
is not re-laid out) leaves the Glance widget at the old bounds, and the overlay
either overlaps the caption strip (the very bug this hunk fixes) or leaves a
gutter un-scrimmed. Fix: after compact-mode toggles, explicitly ping
`glance()->OnBrowserViewLayout()` (mirror what compact mode already does for
itself at controller line 98), or key Glance off inset-path changes too.

Split view: `UseContentCard` doesn't exclude split mode
(`koan_content_card.cc:31-36`), so in split view the widget is inset
`TLBR(0, 0, 24, 24)` while `ActiveContentsRadii` skips rounded radii — the
overlay covers both panes and still works; visual congruence, pre-existing
behaviour, noting only so the skim/fade matches the corners actually drawn.

## 5) The e2e check in `scripts/e2e/glance.mjs`

**Sound in structure; two assumptions to harden.**

- The walk (`glance.mjs:47-63` new numbering: `DOM.getDocument(depth:-1)`,
  match `class == "KoanGlanceView"`, descend, match `class == "WebView"`) is
  legitimate: `ViewElement::GetAttributes` returns
  `{"class", view->GetClassName(), "name", ...}`
  (`components/ui_devtools/views/view_element.cc:152-155`), and
  `METADATA_HEADER` synthesizes `GetClassName` for both `KoanGlanceView` and
  `views::WebView` (`ui/base/metadata/metadata_macros_internal.h:60-65`).
- Reading `p.Width`/`p.Height` capitalized matches the metadata property names
  that `layout.mjs:46` and `workspaces.mjs:55` already rely on, and the values
  are unitless numbers, so `page === card` compares like-for-like.
- The check does capture the actual bug: the WebView *view* bounds are fine
  before and after the fix; the page's `innerWidth/innerHeight` is what was
  held at 94% (`native_view_host.cc:180-186` sizing from
  `ConvertRectToWidget`). Comparing card DIP against page CSS px is right at
  DSF 1.

Caveats (see Nits):

1. `window.innerWidth`/`innerHeight` are CSS px; the card values are DIP. The
   equality holds at DSF 1 (`run.sh` sets no scale factor) but silently fails
   under `--force-device-scale-factor` or page zoom.
2. `sleep(500)` is measured after the earlier checks; with the 200 ms animation
   plus any deferred layout this is fine today but there is no explicit wait
   for the animation to end.

## Findings

### Should-fix

**S-1. Mid-animation relayout can snap the page to full size for a frame
during the fade-in.** `NativeViewHostAura::ShowWidget` writes
`SetTransform(original_transform_ * scale)` whenever it differs from the
current transform (`native_view_host_aura.cc:251-261`), and it executes on
*every* layout of the host while the 200 ms sequence is running. Scenario:
Alt+click a link and immediately resize the browser window (or any
`OnViewBoundsChanged` fires, e.g. compact toggle → `OnBrowserViewLayout` →
`LayoutWidget` → root-view layout → `NativeViewHost::Layout`). Transform jumps
from ~0.97 to identity, then the animator re-applies the eased value next
step — a visible "pop" in a plausibly common interaction. Fix options (any
one): (a) skip the transform write while the page's animator is animating
TRANSFORM (`page->layer()->GetAnimator()->IsAnimatingProperty(
LayerAnimationElement::TRANSFORM)`), or (b) hold an
`AnimationBuilder::GetAbortHandle()` on the view and abort the sequence before
any `LayoutWidget`-triggered relayout, or (c) accept the glitch and document
it. The same guard should go where `LayoutWidget` is called from
`OnBrowserViewLayout`.
**S-2. Compact-mode/inset-only changes can leave the Glance widget misplaced** —
see section 4. Concrete scenario given there. Fix: notify the glance controller
on inset-affecting toggles, e.g. from `KoanCompactModeController` whenever
`IsToolbarHidden()`/`IsCompactModeActive()` flips while Glance is open.

### Nits

**N-1.** `page->layer()` is dereferenced by `SetOpacity`/`SetTransform`
(`animation_sequence_block.cc:153-158` → `target->layer()`) without a null
check at `koan_glance_controller.cc:132-135`; the `if` only tests
`native_view()`. An RVHA window is always Init'd, so this is defensive only,
but `if (page && page->layer())` costs nothing.

**N-2.** During the first ~100-200 ms the page is invisible (opacity animating
from 0) yet focused and click-targetable: clicks and typed keys land on content
the user cannot see. Chrome tolerates this in many fades; if it bothers you,
start the fade at ~0.15 opacity instead of 0, or
`web_view_->SetEnabled(false)` until `OnEnded`.

**N-3.** The stale-pivot edge: if the page window's bounds change
mid-animation (resize), `gfx::GetScaleTransform` anchor from
`koan_glance_controller.cc:134-135` is stale for the remaining frames; the
animation still lands on identity. Fine to leave, worth a comment.

**N-4.** Wait-for-animation in the e2e: replace `await sleep(500)`
(`glance.mjs` new hunk) with a bounded poll of the card size until stable (two
identical samples ~100 ms apart), so the test does not depend on a fixed
timing, and skip-guard the DSF/zoom assumption by comparing ratios
(`w_page / w_card` == 1 within epsilon) instead of string equality — that also
keeps the check valid if page zoom ≠ 100%.
echo done

Verdict: No blockers — animating the hosted window layer is the right fix and is destruction-safe; land after the two should-fixes (S-1 mid-animation transform stomp, S-2 inset-only relayout gap).
