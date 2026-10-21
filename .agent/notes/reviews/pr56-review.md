# Review: PR #56, compact mode variants, tabs on the right, collapsed sidebar (Fixes #35, #20)

Branch `feat/zen-compact-layouts` at `3d57eb4`, diffed against `origin/develop` (`git diff origin/develop...HEAD`, 38 files). I reviewed the code only; nothing was built or run, and the branch was not modified.
I checked it against Chromium 158 (`chromium/src` HEAD `67e29b82`), Zen 1.23.3b's source in `.cache/zen-src` (plus `modules/ZenCustomizableUI.sys.mjs` from its `omni.ja`, which the extraction does not include), issues #35 and #20, and PR #56.
Paths are relative to the worktree. Line numbers in `src/` are branch HEAD. Line numbers in upstream files are from the patched file (`git show HEAD:<path>` + the branch's patch, applied in a scratch directory).
"Uncertain" means derived from code reading only, with a plausible but unconfirmed runtime effect.

## What I verified and found fine

- All five patches apply cleanly with `git apply --check` to pristine `git show HEAD:<path>` files, and all five apply together.
- Every upstream API the PR calls exists with the signature used:
  - `VerticalTabStripStateController::{From, RegisterOnCollapseChanged(callback(CollapseState)), RequestCollapse, IsCollapsed}`.
  - `BaseTabStripRegionView::GetExpandOnHoverLock`, and `ExpandOnHoverLockType`.
  - `TabStripSelectionChange::active_tab_changed()`, `MultiContentsView::IsInSplitView()`, `ui::Layer::StackAbove`, `views::MenuController::GetForOwnerWidget`.
  - `views::MenuRunner::RunMenuAt(Widget*, nullptr, Rect, anchor, source_type)`, `views::ChildLayout`, `BrowserViewLayoutImpl::ProposedLayout::children`.
  - `ResizeAreaDelegate::OnResize` (already flips the sign in RTL, so the PR's extra sign flip composes correctly).
- `vertical_tab_strip_top_container.cc`, `browser_view_layout_impl.cc` and `vertical_tab_strip_region_view.cc` build against the dep lines the BUILD.gn patch adds (`//koan/browser/ui/views` on `vertical_tab_strip_region_view`, `browser_view_layout_impl`, `browser_view`).
- `-std=c++23` is the Chromium default here (`build/config/compiler/BUILD.gn:730`), so the deducing-this lambda is valid.
- No hard-coded colours or radii in the new code. Gutters use `kContentCardMargin`. The only new constant is `kFlashDuration`.
- `VerticalTabStripExpandOnHoverLock` clears its region pointer when the region is destroyed (`ClearRegionViewOnDestruction`), so the hover, peek and flash locks cannot dangle when the tab strip is rebuilt.
- #32 holds: only page-side caption buttons fade (`IsCaptionButtonBesideSidebar`), and `HitTestCaptionButton` honours `GetCanProcessEventsWithinSubtree`.
- #47 holds: the only new timer is one-shot (`flash_timer_`). `UpdatePointerMonitor` still creates an EventMonitor only while the toolbar, the caption strip or a peek is out, and the slid-out toolbar layer is still hidden by `HideAwayToolbarLayer`.
- #52 holds: fullscreen still makes `DesiredMode()` return `kNone` through `UseContentCard`. `IsCaptionButton` is still `false` on Mac, and the Mac-only code is inside `BUILDFLAG(IS_MAC)`. I found no Aura or Ozone use outside guards.
- #50 holds: `OnResize` still clamps through `koan::SidebarMinWidth/MaxWidth/CollapseSnapWidth`.

---

## Blockers

### B1. Re-activating compact mode with the sidebar kept hidden drops single toolbar

- **Where:**
  - `src/browser/ui/views/koan_compact_mode_controller.cc:760-770` (`IsSidebarCollapsedByUser`).
  - `:839-847` (`Activate`, which calls `DesiredSingleToolbar()` after `Deactivate` has run).
  - `:968-976` and `:1055-1064` (`Deactivate(keep_sidebar_hidden=true)`).
  - Callers: `:346-354` (`OnTabsOnRightChanged`), `:806-824` (`UpdateActive`), `:299-315` (`ToggleSingleToolbar`).
- **What's wrong:**
  - `IsSidebarCollapsedByUser()` returns `collapsed_before_` only while `hides_sidebar()` is true.
  - `Deactivate()` sets `mode_ = kNone` and `hide_sidebar_ = false` before `Activate()` runs. With `keep_sidebar_hidden` the sidebar stays collapsed and `collapsed_before_` stays set, but `IsSidebarCollapsedByUser()` no longer consults it. It falls through to `state->IsCollapsed()`, which is true.
  - So `Activate()` computes `single_toolbar = DesiredSingleToolbar() = pref && !collapsed = false`. That gives `hide_toolbar_ = false` and `compact_keeps_sidebar_nav_ = false`.
  - The comment at `:840` says this is decided "before `mode_` changes", but `UpdateActive` and `OnTabsOnRightChanged` have already deactivated.
- **Failure scenarios (defaults: single toolbar on, hide = sidebar):**
  1. Press Ctrl+S, then Ctrl+Alt+S to peek the sidebar. Right-click an empty area and pick "Tabs on the right". `OnTabsOnRightChanged` runs `Deactivate(true)` then `Activate(kCompact)`. The window comes back as multiple-toolbars compact: the toolbar is above the page, the sidebar nav is removed, and `koan.single_toolbar` is still true. Nothing re-evaluates it (`OnBrowserViewLayout` compares only `mode_` with `DesiredMode()`), so it is stuck until compact mode is toggled.
  2. Compact on, multi-toolbar pref, sidebar hidden. Press Ctrl+Alt+B to go back to single toolbar. `UpdateActive` sees `DesiredSingleToolbar() != compact_keeps_sidebar_nav_`, deactivates with keep, and `Activate` again computes `false`. The layout never changes although the pref is now true. The direction single to multi works, because the pref is false there.
- **Why the tests pass:** `power.mjs:358` presses Ctrl+Alt+B back to single toolbar while compact (multi to single) and then immediately leaves compact mode, with no check in between. `compact_variants` and `tabs_right` seed `single_toolbar: false` or set tabs-right at startup, so the churn path never runs with single toolbar on.
- **Fix:**
  - In `IsSidebarCollapsedByUser()`, return `*collapsed_before_` whenever `collapsed_before_` has a value, not only when `hides_sidebar()`. It is set only while compact mode hid the sidebar and is reset when the sidebar is restored, so that is the correct meaning.
  - Or compute `single_toolbar` and `collapsed_by_user` once in `UpdateActive` / `OnTabsOnRightChanged`, before `Deactivate`, and pass them into `Activate`.
  - Add an e2e step: single toolbar on, compact on, toggle tabs-right through the menu, and assert `KoanSidebarNavButtons` is still present and the top container is still in `TopContainerOverlayView`. Do the same for multi to single with Ctrl+Alt+B in compact mode.

---

## Should fix

### S1. The tabs-on-right mirror applies to every window, not just the vertical-tab layout

- **Where:**
  - `src/browser/ui/views/koan_browser_view_controller.cc:149-158` (`IsSidebarOnRight` returns the pref with no window check).
  - Patch `patches/.../layout/browser_view_layout_impl.cc.patch` (upstream `browser_view_layout_impl.cc:236-305`, `Layout`, which is `final` and shared by the tabbed, app and popup layouts).
  - `koan_browser_view_hooks.cc:12-14`: the controller is created for every `BrowserView`.
- **What's wrong:** `Layout()` swaps the exclusions and mirrors whenever `koan.tabs.on_right` is true. It never checks `ShouldDrawVerticalTabStrip()`, the window type, or `features::IsSidebarEnabled()`. The settings row is hidden when the Koan sidebar is off, but the pref stays set.
- **Failure scenario:**
  1. Turn on "Tabs on the right".
  2. Switch a window to horizontal tabs ("Show tabs at top", `IDC_TOGGLE_VERTICAL_TABS`), or run with `--disable-features=KoanSidebar`.
  3. In `BrowserViewTabbedLayoutImpl::CalculateProposedLayout` (`:1097-1140`), `side_panel_leading = IsRightAligned() == IsRTL()` and the panel's bounds are mirrored. A right-aligned side panel (reading list, bookmarks) is laid out on the left, while its own shadow, resize handle and animation assume the right. The horizontal tab strip's leading margin lands on the wrong end.
  4. Popup and app windows go through the same hook. For a single full-width child the mirror is a no-op, which I checked for the popup layout, but children that are not symmetric about the client area move.
- **Fix:** Gate the hook (or `IsSidebarOnRight`) on the window actually having the vertical strip: `browser_view->GetIsNormalType() && browser_view->ShouldDrawVerticalTabStrip()` and `features::IsSidebarEnabled()`. Use that same predicate in `IsSidebarOnRight` so the card insets, hot zones, toast and header mirror stay in agreement. Add a check to `tabs_right` that a horizontal-tabs window is not mirrored.

### S2. With "Hide toolbar" and a visible sidebar, the revealed toolbar spans the whole window, over the sidebar's top row (uncertain)

- **Where:** Patch `browser_view_layout_impl.cc.patch` and upstream `browser_view_layout_impl.cc:262-285`. The separated top-container branch calls `CalculateTopContainerLayout(top_container_layout, params, /*needs_exclusion=*/true)` with the window-wide `params`. `BrowserViewTabbedLayoutImpl::CalculateTopContainerLayout` (`:1438-1500`) has no vertical-strip handling. Reveal: `koan_compact_mode_controller.cc:1069-1124`.
- **What's wrong:**
  - Before this PR, compact mode always hid the sidebar, so a revealed top container in the overlay never had a visible sidebar beneath it. In the new toolbar-only variant it does.
  - The top container's bounds are the full window width (`GetTopContainerBoundsInParent`). The overlay view is above the client view, so the revealed toolbar covers the sidebar's top row (toggle, tab search, and for left or right buttons the nav buttons). `KoanCaptionButtonsTargeter` only lets caption buttons through.
  - Zen's navbar in this mode sits over the page area only.
- **Failure scenario:** Multi toolbars, hide = toolbar. Move the pointer to the top edge above the page. The toolbar slides in across the sidebar header, which becomes unclickable until it hides. `compact_variants` checks only the hidden state and the hot zones, never the revealed bounds.
- **Fix:** Check on screen first (the PR screenshot shows the hidden state only). If it overlaps, inset the separated top container's `params` by the visible strip's width on the sidebar side. That is a small addition in the existing tabbed-layout hunk, or Koan can set the top container's bounds itself in `OnBrowserViewLayout`, as it does for the floating URL bar. Add a revealed-state geometry check to `compact_variants`. If it is confirmed this is a blocker, since it breaks the headline #20 variant.

### S3. "Hide toolbar" is not disabled in the state Zen disables it, and the docs say it is

- **Where:**
  - `koan_compact_mode_controller.cc:530-533` (`IsCommandIdEnabled`), `:787-798` (`IsHideToolbarOnlyBlocked`, the guard at `:790`).
  - Docs: `docs/ZEN_PARITY.md:44` and `docs/USAGE.md:133-135` ("neither does Koan").
- **What's wrong:**
  - `IsHideToolbarOnlyBlocked()` returns false whenever `PrefHidesSidebar()` is true. That is the case for pref `sidebar` (the default) and `both`. The guard exists so `DesiredHideToolbar()` can fall back, but `IsCommandIdEnabled` reuses it.
  - Zen's `checkIfIllegalState()` (`ZenCompactMode.mjs:337-363`) disables the menu item whenever the sidebar is collapsed and the window buttons are on its side, whatever the current hide setting is.
- **Failure scenario:**
  1. Mac (traffic lights on the left) with multi toolbars and the sidebar collapsed to icons.
  2. Open the compact menu with the pref at its default, `sidebar`. "Hide toolbar" is enabled.
  3. Click it. The pref becomes `toolbar`, and `DesiredHideToolbar()` now falls back to hiding the sidebar. The radio shows "Hide sidebar" while the pref says toolbar. After the sidebar is expanded the mode silently becomes toolbar-only.
  4. Headless Linux has no caption buttons, so no e2e can see this.
- **Fix:** Split the predicate. Use a pure "collapsed and buttons on the sidebar's side" test for `IsCommandIdEnabled`, and keep the pref-aware one for `DesiredHideToolbar`. Like Zen, also rewrite the pref to `sidebar` when the blocked state is reached. Add a unit-level or e2e seam, for example an overridable caption-area size.

### S4. The context-menu model is destroyed before the previous `MenuRunner` (lifetime)

- **Where:** `koan_compact_mode_controller.cc:481-482` (`context_menu_model_ = ...`, `compact_menu_model_.reset()`) before `:502` (`menu_runner_ = ...`).
- **What's wrong:** On the second and later right-clicks, the old model is freed while the old runner is still alive. `~MenuRunner` runs `MenuRunnerImplAdapter::Release`, which deletes the `MenuModelAdapter`. Its destructor does `menu_model_->SetMenuModelDelegate(nullptr)` (`ui/views/controls/menu/menu_model_adapter.cc:37-41`) on the freed model. The member is `raw_ptr<..., DanglingUntriaged>`, so upstream already tolerates this class of bug, but it is a use-after-free write and a virtual call.
- **Failure scenario:** Open the sidebar menu twice in one window. `compact_variants` does exactly that and passes in a non-ASan build, so that is not evidence of safety.
- **Fix:** Call `menu_runner_.reset()` first (and only then replace or reset the models), or declare `menu_runner_` after the models so it is destroyed first. Also make `menu_runner_` live in the order models, then runner, in the header.

### S5. No expand-on-hover lock while the sidebar menu is open (uncertain)

- **Where:** `koan_compact_mode_controller.cc:475-508` (`ShowContextMenu`). Upstream's own menus take `ExpandOnHoverLockType::kKeepCurrentState` (`tab_strip_region_view.h:36-38` says "should be used when the user is interacting with a bubble or context menu"; see `system_menu_model_delegate.cc:318`).
- **What's wrong:** In compact mode the sidebar is only an expand-on-hover overlay. Once the menu (a separate widget) has the pointer, the strip's hover tracking sees the pointer leave. The hover lock from the hot zone is released on exit (`:1238`), and a keyboard peek lock lasts about 3 s. The sidebar then slides away under the open menu, and the "Compact Mode" submenu opens to the right of the sidebar.
- **Failure scenario:** Hover the left edge, right-click an empty part of the sidebar, move to "Compact Mode". The sidebar slides away mid-menu. The command still works, but the anchor disappears. The e2e avoids this by peeking first (`compact_variants.mjs:83`).
- **Fix:** Take a `kKeepCurrentState` lock before `RunMenuAt` and release it on close (`MenuRunner` has no close callback with this constructor, so use `MenuRunner`'s `on_menu_closed_callback`, or reset the lock in `ExecuteCommand`/next show). Add a hover-only e2e.

### S6. The mirror does not flip the content clip insets used while the sidebar or side panel animates

- **Where:** Upstream `browser_view_tabbed_layout_impl.cc:1305-1316`. The `clip_insets` come from `unclipped_contents_region.InsetsFrom(contents_layout.bounds)` and go to `MultiContentsView::SetTargetContentBounds` as a side effect of `CalculateProposedLayout`. `multi_contents_view.cc:568-578` turns them into outsets of the active contents container. The Koan mirror (patch `:19-31`) only flips child bounds.
- **What's wrong:** The insets are left and right specific. With tabs on the right they are applied on the wrong sides while `clip_content_for_animation` is true: during a vertical-strip expand or collapse animation, and during side-panel animations. `kSidePanelFlyoverAnimation` is enabled by default on Linux and Windows (not Mac, `ui_features.cc:320-326`).
- **Failure scenario:** Tabs on the right, multiple toolbars, click the sidebar toggle. While the strip animates, the web contents are laid out and clipped on the wrong side (stretched toward the window edge instead of toward the strip). Not visible on Mac by default. Uncertain on screen; the geometry is clear from the code.
- **Fix:** Swap `clip_insets` left and right when mirroring. The simplest place is inside the Layout hook, before `ApplyLayout`: after `CalculateProposedLayout`, re-read the multi-contents target bounds through a small `koan::` hook, or skip the animation clip when mirrored. Test with a tabs-right toggle plus a frame dump.

### S7. Dragging the sidebar closed flips single toolbar off for the rest of the drag (uncertain)

- **Where:** Patch `vertical_tab_strip_region_view.cc.patch:61, 90-92` and `koan_compact_mode_controller.cc:556-583, 760-775`.
- **What's wrong:** Upstream collapses the strip live once the dragged width passes the snap width (`RequestCollapse(true)`). `OnSidebarDragCollapsed` runs only on release, and `collapsed_by_drag_` is set there. During the drag `OnSidebarCollapseChanged` calls `UpdateActiveSoon`, `IsSidebarCollapsedByUser()` is true, and `DesiredSingleToolbar()` is false. So `UpdateActive` deactivates single toolbar mid-drag: it removes the nav row and URL pill and moves the top container back above the page. Releasing then activates compact mode, and the sidebar nav is added back. Zen decides this while dragging.
- **Failure scenario:** Default config. Drag the sidebar edge past the threshold. The toolbar flashes in above the page and the sidebar's header changes under the pointer, then compact mode takes over on release. If the user drags back out before releasing, the layout flips back. `sidebar_drag.mjs` only checks the end state.
- **Fix:** In `IsSidebarCollapsedByUser()`, return false while `state->IsResizing()` (`VerticalTabStripStateController::IsResizing` exists), or set `collapsed_by_drag_` at the first collapse during the drag rather than on release. Extend `sidebar_drag` to sample the mode between the drag steps.

### S8. macOS default compact mode now keeps a permanent top band; not documented (uncertain)

- **Where:** `koan_compact_mode_controller.cc:888-901` (`Activate`), `koan_content_card.cc:44-52`.
- **What's wrong:** On Mac, `caption_strip_collapsible_ = !hides_sidebar() && !sidebar_on_right_`. With the defaults (single toolbar, compact mode hiding the sidebar, tabs left) that is false: the strip above the page stays at the traffic-light height (`max(kContentCardMargin, caption height)`) for the whole session, and the top-edge hot zone is not created. Before this PR (and per #52's "caption-button hiding is a no-op on Mac") the strip was collapsed to the 8 px gutter. Zen hides the traffic lights together with the sidebar. The PR body explains the choice, but `docs/ZEN_PARITY.md:20` still says "Same" for window buttons hidden until the pointer reaches the top edge, with no macOS note. The hook for hiding them is only a comment in `Activate()`.
- **Failure scenario:** On the product platform, compact mode shows a roughly 30 px empty band above the page. The real fix needs NSWindow's standard window buttons.
- **Fix:** Decide deliberately, then put it in docs: add a macOS row to `ZEN_PARITY.md` and `USAGE.md`, and open a follow-up issue for hiding the traffic lights. If the band is unwanted, keep the collapsed 8 px strip on Mac and let the traffic lights sit in the card's corner, as before. Since this cannot be tested headless, a Mac smoke test is needed before merge.

### S9. The e2e tests do not cover the paths that break

- **Where:** `scripts/e2e/` (new and changed tests).
- **What's missing**, each of which would have caught a finding above:
  - Single toolbar plus compact plus tabs-right or variant churn (B1).
  - A revealed toolbar's bounds with a visible sidebar (S2).
  - The blocked "Hide toolbar" item (S3), which needs a seam for caption-button areas.
  - A second menu open in the same window (S4), and a hover-only menu (S5).
  - Mid-drag state (S7).
  - Horizontal-tabs windows with the pref on (S1).
  - Settings rows: `settings.mjs` was not touched, so the three new rows are not checked at all.
  - RTL: none, and nothing in the PR mentions it.
- **Fix:** Add the checks above. `tabs_right` also never asserts the top-edge hot zone or the toolbar slide with tabs on the right.

---

## Nits

- **N1. Docs contradict each other and the settings count is wrong.**
  - `docs/ZEN_PARITY.md:23` says the collapsed layout is "Also in Settings (Look and Feel), as in Zen", while `:98` says that settings card is Missing. Drop the first claim.
  - `:95` says "10 today"; `kRows` has 13 rows now (it had 10 on develop).
  - `docs/USAGE.md` has a very long line in the "Tabs on the right" paragraph.
- **N2. Drag-to-hide rewrites the variant.** `koan_compact_mode_controller.cc:565-567` switches `toolbar` to `both` when the drag hides the sidebar. Zen's `#collapseSidebarIntoCompactMode` (`ZenCustomizableUI.sys.mjs`) only toggles compact mode; with Zen's toolbar-only setting the sidebar would not hide. The PR's behaviour is sensible, but it changes a user pref without telling the user. Say so in `ZEN_PARITY.md`, or restore the pref when compact mode is turned off. Also, the hint pref is only set when a shortcut exists (matches Zen).
- **N3. RTL is unverified.** The pref is "logical right" (it mirrors to the visual left in RTL), consistent with Views' mirroring of child bounds. But `KoanToast::OnBrowserViewLayout` (`koan_toast.cc:211-219`) positions the toast widget in physical screen coordinates from the logical `IsSidebarOnRight`, so in RTL the toast lands on the sidebar's side. `IsCaptionButtonBesideSidebar` (`:1302-1314`) compares logical bounds, which should be consistent, but nothing tests it. Uncertain.
- **N4. Corner views are repositioned but not flipped.** `vertical_tab_strip_top_corner` and `vertical_tab_strip_bottom_corner` (`CustomFloatingCorner`, `kTopLeading` and `kBottomLeading`, `browser_view.cc:1007-1022`) and the top container background's `kTopTrailing` corner (`browser_view_tabbed_layout_impl.cc:1536`) keep their paint orientation after the mirror. Whether they are visible with Koan's card is unknown; the screenshot (`docs/screenshots/tabs-right.png`) shows no artefact on Linux, but only the multi-toolbar layout in a non-maximized window.
- **N5. Link-drop arrow position assumes a left strip.** `VerticalTabStripRegionView::GetLinkDropArrowPosition` (`vertical_tab_strip_region_view.cc:679-745`) puts the arrow at the strip's left edge pointing right ("outside the window"). With tabs on the right it points into the strip from the page side. Cosmetic, only when dragging a link onto tabs. Uncertain.
- **N6. Caption-button layers are never destroyed.** `UpdateCaptionButtons()` (`:1331-1375`) calls `SetPaintToLayer()` on first hide and keeps the layers after leaving compact mode, and it re-stacks them above the toolbar layer on every `OnBrowserViewLayout`. Harmless, but destroy the layers in `Deactivate()` once opacity is back to 1.
- **N7. Duplicate default.** The 2 s toast timeout now lives as a literal default in `koan_toast.h:46` after `kToastTimeout` was removed from the `.cc`. Make it a named constant in the header.
- **N8. The Layout hook is the biggest patch of the PR.** It is about 35 added lines, with a recursive deducing-this lambda in the patched upstream function. `ProposedLayout` is protected so it cannot move into `src/`, but the lambda could be a template helper in `koan_compact_mode.h`, leaving about 8 lines in the patch. Also add `#include <utility>` for `std::swap` (it relies on a transitive include).
- **N9. Invalid pref value.** `PrefHidesSidebar()` and `PrefHidesToolbar()` (`:336-344`) treat any unknown string as `both`, and the settings menu then shows nothing selected. Sanitize in `RegisterProfilePrefs` or on read.
- **N10. `collapsed_by_drag_` can stay set.** `:286, 764`: it is cleared only in `Activate()`. If the drag handler runs while the mode cannot activate (feature off after the check, window going fullscreen), `IsSidebarCollapsedByUser()` reports false for a collapsed sidebar until the next activation.
- **N11. Menu strings.** The labels (`u"Enable compact mode"`, etc., `:485-501`) are hard-coded English literals. Fine for now (Koan does this elsewhere), but they are not marked for later localisation.
