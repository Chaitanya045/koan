# PR #23 review — feat/zen-layout: single-toolbar default + floating URL bar

Reviewed against `develop...feat/zen-layout` (commit `167c544`). Chromium APIs
verified against the checkout at `chromium/src` (Chromium 158). No files under
`chromium/src` were modified; nothing was built.

Severity: **Blocker** / **Should-fix** / **Nit**.

---

## Findings

### SF-1 (Should-fix) — Unfloat forces hidden toolbar children back to visible

`KoanCompactModeController::SetUrlBarFloating` (src/browser/ui/views/koan_compact_mode_controller.cc:260-298)
captures every *currently visible* sibling of `LocationBarView` in
`ToolbarView`, hides them, and on unfloat sets every captured view back to
`SetVisible(true)`. Two failure modes, both real because ToolbarView children
change visibility dynamically in Chromium 158 (e.g. avatar
`chrome/browser/ui/views/toolbar/toolbar_view.cc:606`, glic button
`toolbar_view.cc:1128-1136`, chrome-labs `toolbar_view.cc:541`, home button
`toolbar_view.cc:673`):

1. **Wrongly shown after unfloat.** While floating, other code hides a
   captured view (e.g. the avatar button disappears after a sign-in state
   change, or the glic button is turned off). Unfloat then calls
   `SetVisible(true)` on it unconditionally (controller.cc:293-297), showing
   something that should be hidden. Because the toolbar is otherwise invisible
   in single-toolbar mode, the user then sees a stray button on the next
   toolbar reveal.
2. **Intruder while floating.** A sibling hidden at capture time (not
   tracked) that other code shows while the URL bar floats (e.g. the glic
   actor icon appears mid-session) pops up inside the floating card, breaking
   the "just the location bar" invariant (and the e2e check at
   scripts/e2e/layout.mjs:124-127).

**Fix:** record each captured view's *pre-float* visibility and restore that
value instead of `true`; while floating, observe the ToolbarView
(`OnChildViewRemoved`/`ViewObserver`) or hide via a targeted mechanism so
newly-visible siblings are suppressed too (e.g. set `ToolbarView`'s clip/layer
mask to the location-bar rect instead of per-child visibility).

### SF-2 (Should-fix) — Floating URL bar survives entering fullscreen

Nothing checks fullscreen/immersive state. If the omnibox has focus and the
user enters fullscreen (F11), focus usually stays in the omnibox, so
`url_bar_floating_` stays true; `LayoutFloatingUrlBar` (controller.cc:310-326)
re-applies the floating rect on every `OnViewBoundsChanged` and every
`OnBrowserViewLayout` (controller.cc:151), so the card is pinned over the
fullscreen page, fighting the fullscreen layout. The popup widget also keeps
anchoring to the omnibox.

**Fix:** in `OnBrowserViewLayout` (or `SetUrlBarFloating` entry) unfocus or
unfloat when `browser_view_->browser()->window()->IsFullscreen()`; at minimum,
guard `LayoutFloatingUrlBar` on the top container being visible.

### SF-3 (Should-fix) — Favicon observation can be reset by the *previous* tab's destruction

`KoanSidebarNavView::WebContentsDestroyed` (src/browser/ui/views/koan_sidebar_nav_view.cc:209-212)
unconditionally does `favicon_observation_.Reset()`. But `WebContentsDestroyed`
is delivered for the *observed* WebContents, while `favicon_observation_` may
already track a different driver: closing the active tab fires
`TabStripModelObserver` first (`OnTabStripModelChanged` → `ObserveActiveTab`
re-observes the new active tab, koan_sidebar_nav_view.cc:192-199), and the old
WebContents is destroyed *afterwards*. The late `WebContentsDestroyed()` then
resets the observation of the **new** tab's driver, and the pill loses favicon
updates (falls back to the globe) until the next tab switch.

No use-after-free: `WebContentsImpl` notifies `WebContentsObserver` before
tearing down `WebContentsUserData` (the driver), and `ScopedObservation` +
`IsObserving()` guard the read (koan_sidebar_nav_view.cc:249-254) — the
lifetime question in the brief checks out as safe. But the reset should be
conditional: only reset if the destroyed contents is still the one whose
driver is observed (e.g. keep a `raw_ptr<favicon::FaviconDriver>` or compare
`web_contents()` before the observer detaches).

Related, same file: `TabStripModelChange::Replaced` (tab replace/discard)
does not set `active_tab_changed()`, so `ObserveActiveTab` is not called and
the pill goes stale/blank until the next selection change
(koan_sidebar_nav_view.cc:196-198). Handle `change.type() ==
TabStripModelChange::Type::kReplaced` as well.

### SF-4 (Should-fix) — Dangling `sidebar_nav_` / `sidebar_nav_buttons_` if the vertical-tab-strip region is ever replaced

`Deactivate` dereferences `sidebar_nav_->parent()` and
`sidebar_nav_buttons_->parent()` (koan_compact_mode_controller.cc:466-474).
Both are `raw_ptr`s into the upstream `VerticalTabStripRegionView` /
`VerticalTabStripTopContainer`. If that region is destroyed and recreated
while single-toolbar is active (vertical-tabs preference or feature toggle),
the raw pointers dangle and `Deactivate` is a use-after-free. The `sidebar_nav_`
side is pre-existing, but the PR extends the pattern to `sidebar_nav_buttons_`.

**Fix:** track the buttons with a `views::ViewTracker` (like
`hidden_for_floating_`) or hold the `std::unique_ptr` ownership so removal is
self-describing; guard `parent()` non-null regardless.

### N-1 (Nit) — Hook can be skipped in the collapsed branch, leaving nav buttons unpositioned

In `VerticalTabStripTopContainer::CalculateProposedLayout`, the collapsed
branch only calls `koan::LayoutSidebarHeaderTrailingViews` inside
`if (collapse_button_ && collapse_button_->GetVisible())` (patch at
patches/chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_top_container.cc.patch,
context chromium/src/chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_top_container.cc:185-196).
If the collapse button were ever invisible, the Koan buttons get no
`ChildLayout` entry and keep stale bounds/visibility (a `views::ProposedLayout`
entry is the only thing that sets them). Today nothing sets `collapse_button_`
invisible (no `SetVisible` call on it in the file), so this is defensive only.
The uncollapsed branch (vertical_tab_strip_top_container.cc:332-333) is
unconditional and always emits an entry — including a
`visible=false` entry when the buttons don't fit (koan_sidebar_nav_view.cc:90),
which is the right way to avoid "children that never get a layout entry".

### N-2 (Nit) — `GetSidebarHeaderTrailingWidth` counts buttons that will be hidden

`GetPreferredWidth` adds the trailing width unconditionally
(vertical_tab_strip_top_container.cc:524 / koan_sidebar_nav_view.cc:97-105),
even when `LayoutSidebarHeaderTrailingViews` will hide the buttons (no fit in
the collapsed strip). This inflates `GetPreferredWidth` and shifts the
overflow/wrap decisions that use it
(vertical_tab_strip_top_container.cc:497). Cosmetic at worst; a follow-up can
skip hidden trailing views.

### N-3 (Nit) — `InstallSidebarNav` failures are never retried

`UpdateActive` early-returns when `desired == mode_`
(koan_compact_mode_controller.cc:355-361), and `OnBrowserViewLayout` only
re-evaluates when the mode differs (koan_compact_mode_controller.cc:142-144).
If `InstallSidebarNav` silently no-ops at Activate time because
`tab_strip_view()` is null or not a `VerticalTabStripRegionView` yet
(koan_compact_mode_controller.cc:436-441, e.g. during startup ordering), the
nav buttons *and* the pill never install for the life of the window. Pre-existing
for the pill; the PR extends it to the buttons. A cheap fix is to retry
`InstallSidebarNav` from `OnBrowserViewLayout` when `!sidebar_nav_`.

### N-4 (Nit) — e2e brittleness (scripts/e2e/layout.mjs)

- Exact-equality geometry assumes the client width equals `WINDOW_WIDTH = 1280`
  (lines 103-105, 121-123, 134): frame borders/zoom/launched-window drift
  break it; use a tolerance.
- `uiEvent`/`viewKey` reuse `nodeId`s from an earlier `views()` snapshot;
  UI DevTools node ids are only meaningful for that `DOM.getDocument`
  traversal, and any re-serialization invalidates them.
- Every `send` resolves with `{timedOut:true}` after 5 s and the walk never
  retries; one busy UI thread turns a run into silent failures. One
  `views()` traversal does one `CSS.getMatchedStylesForNode` round-trip per
  view, so this is also slow.
- `click()` hardcodes (10,10) inside the pill — fine today, fragile if the
  pill gains a leading icon.

### N-5 (Nit) — style/minor

- `EnabledStateChangedForCommand` parameter indentation is off
  (koan_sidebar_nav_view.cc:144-145).
- `hidden_for_floating_` could be a plain `std::vector<views::ViewTracker>`
  instead of `std::vector<std::unique_ptr<views::ViewTracker>>`
  (koan_compact_mode_controller.h).
- The floating card's height is `toolbar->GetPreferredSize().height()`
  (koan_compact_mode_controller.cc:321), so a visible bookmark bar is
  silently clipped by `masks_to_bounds` while floating. Matches Zen, but
  worth a comment; the vertical centering uses the constant
  `kFloatingUrlBarOpenHeight = 330` regardless of actual popup height, so on
  short windows the card sits low and the popup can overflow the window edge.

---

## Non-issues checked (for the record)

- **`ViewTracker` lifetime:** nulls out on view deletion
  (chromium/src/ui/views/view_tracker.h:19-21); the unfloat loop guards
  `tracker->view()` (koan_compact_mode_controller.cc:293-297). Window
  destruction while floating goes through `OnBrowserViewDestroying` →
  `Deactivate` → `SetUrlBarFloating(false)` (controller.cc:155-163, 463),
  before the view tree is torn down.
- **Bounds re-application cannot loop:** `View::SetBoundsRect` early-returns
  when bounds are unchanged (chromium/src/ui/views/view.cc:441-444), and the
  floating rect is a pure function of the window size + toolbar preferred
  height, so the controller↔BrowserViewLayoutImpl interplay converges.
  `laying_out_floating_` (AutoReset) prevents synchronous re-entry from the
  observer; BrowserViewLayoutImpl has its own reentrancy guard
  (browser_view_layout_impl.cc:236-239). The reset each pass that motivates
  the observer is real: chromium/src/chrome/browser/ui/views/frame/layout/browser_view_layout_impl.cc:243-268.
- **`ConvertRectToTarget` usage** is legal: browser_view and the overlay view
  share the widget but not a view hierarchy, which the API explicitly allows
  (chromium/src/ui/views/view.h:1029-1036).
- **Reparenting + observers:** `ScopedObservation<View, ViewObserver>` for
  `top_container_observation_`/`toolbar_observation_` survives reparenting and
  auto-unregisters when the observed view dies; both are `Reset()` before the
  Deactivate reparent (koan_compact_mode_controller.cc:291-292).
- **Omnibox popup focus:** in 158 the popup is its own widget
  (chromium/src/chrome/browser/ui/views/omnibox/omnibox_popup_view_views.cc:384-390),
  so focus inside the popup never reaches this widget's FocusManager and
  cannot spuriously unfloat the bar; selection/accept returns focus to the
  page, which unfloats. The omnibox context menu behaves the same way.
- **`KoanSidebarNavButtons` command observers** are registered/unregistered
  symmetrically (koan_sidebar_nav_view.cc:132-142); the button callbacks bind
  the `BrowserWindowInterface`, which outlives the views.
- **Upstream patch correctness:** both `CalculateProposedLayout` branches and
  `GetPreferredWidth` call the hooks at the right places, applied as claimed
  (vertical_tab_strip_top_container.cc:194, 332, 524), and the BUILD.gn patch
  adds the `//koan/browser/ui/views` dep.
- **e2e modifier flags** use the CDP convention (Alt=1, Ctrl=2, Shift=8), not
  `ui::EventFlags` — correct as written.

---

## Verdict

**Approve with should-fixes** — no blockers found; fix SF-1/SF-3 before merge, SF-2/SF-4 soon after.
