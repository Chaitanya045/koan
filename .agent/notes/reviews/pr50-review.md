# Review: PR #50, Zen sidebar tab list (Fixes #33)

Branch `feat/zen-sidebar-tabs` at `eee084b`, diffed against `origin/develop`. I reviewed the code only; nothing was built or run.
I checked it against Chromium 158 (`chromium/src` HEAD), Zen 1.23.3b's source in `.cache/zen-src`, issue #33, the agent brief and CONTRIBUTING.md.
Paths are relative to the worktree. Upstream line numbers are from `git show HEAD:<path>`.

---

## Blockers

### B1. Use-after-free when the window switches between vertical and horizontal tabs, and the rows never come back

- **Where:**
  - `src/browser/ui/views/koan_sidebar_tabs.cc:433-494` (`InstallRows`), `:496-515` (`UpdateClearRow`), `:421-431` (`OnBrowserViewDestroying`) and `:626-635` (`OnMouseEvent`).
  - `src/browser/ui/views/koan_sidebar_tabs_controller.h:77` (`raw_ptr<KoanSidebarTabRowsView> rows_`).
- **What's wrong:** `rows_` is a child of the `TabStripView`, but the `TabStripView` doesn't live as long as the window:
  - `BrowserView::OnVerticalTabStripModeChanged` (upstream `browser_view.cc:1604-1631`) calls `vertical_tab_strip_region_view_->ResetTabStrip()`.
  - That runs `root_node_->Reset()`, then `ClearTabStripView()`, then `RemoveTabStripView()` (`base_tab_strip_region_view.cc:124-148, 440-453`). The `TabStripView` is destroyed, and the Koan rows view inside it goes with it.
  - The controller is never told. It stays a `TabStripModelObserver`, so `rows_` now dangles.
  - `InstallRows()` only runs once, from `OnAddedToWidget`, and returns early while `rows_` is non-null. So going back to vertical tabs creates a new `TabStripView` with no rows. A window that starts with horizontal tabs never gets them either.
- **Failure scenario:**
  1. Right-click the tab strip and pick "Show tabs at top" (`IDC_TOGGLE_VERTICAL_TABS`).
  2. Open or switch a tab. `OnTabStripModelChanged` calls `UpdateClearRow()`, which runs `rows_->clear_row()->SetVisible()` on the freed view and crashes (BRP-quarantined memory).
  3. Even with no tab change, closing the window crashes in `OnBrowserViewDestroying` at `rows_->parent()->RemoveChildViewT(...)`.
  4. Back in vertical mode, the sidebar has no New Tab row and no Clear, and Chromium's "+" is visible again.
- **Fix:**
  - Track the rows view's lifetime: observe it with `views::ViewObserver::OnViewIsDeleting`, or hold it in a `views::ViewTracker`. Null `rows_`, remove the model observer, and drop the region handler when it goes.
  - Reinstall on mode change: `tabs::VerticalTabStripStateController::RegisterOnModeChanged` (`vertical_tab_strip_state_controller.h:102`) exists. Post a task so it runs after `InitializeTabStrip()`.
  - Simpler alternative: have the hook create the rows from `TabStripView`'s vertical setup, so they always live with the strip.
  - Add an e2e step that toggles vertical tabs off and on, then opens and closes a tab.

### B2. Clear (and Ctrl+Shift+K) can delete the focused workspace

- **Where:** `src/browser/ui/views/koan_shortcuts.cc:317-348` (`CloseUnpinnedTabs`).
- **What's wrong:** Koan workspaces are focused tab groups (`koan_workspaces.cc`), and Chromium deletes a group when its last tab leaves it. In `TabStripModel::TabGroupStateChanged` (upstream `tab_strip_model.cc` around line 5218), when the group `IsEmpty()`, it calls `ExitFocusMode(kLastTabClosed)` and then `RemoveTabGroup`.
  - develop always added a fresh tab inside the workspace before closing.
  - The PR only adds one in the "nothing closable" branch (`:337-343`).
  - When the selected tab is an Essential (pinned tabs stay in the model and stay selectable in focus mode; see `had_pinned_tabs_in_focus_session_`), every workspace tab passes `IsClosableByClear`. They all close, and nothing is added.
- **Failure scenario:**
  1. In "Space 1", with tabs A and B, click an Essentials tile.
  2. Point at the sidebar and click "Clear".
  3. A and B close, the group is removed, focus mode exits, and "Space 1" (its name, emoji and colour) is gone from the switcher.
  - In Zen, a space survives with no tabs.
  - Related, and pre-existing for the shortcut but now one click away: in the "∞" (all tabs) view, Clear closes the unpinned tabs of every group. Every workspace except the one holding the selected tab is deleted.
- **Fix:**
  - If a workspace is focused and `to_close` covers every tab in it, first add a background tab in the workspace: `chrome::AddTabAt(..., /*foreground=*/false, workspace)`. That keeps the selected Essential active, as Zen does.
  - Decide what Clear means in the "∞" view. Scope it to ungrouped tabs, or hide Clear there.
  - Add an e2e case: in a workspace with an Essential selected, Clear keeps the workspace.

### B3. The toast promises "Use Ctrl+Shift+T to undo", but the closed tabs can't be restored

- **Where:** `src/browser/ui/views/koan_shortcuts.cc:345` (`model->CloseWebContentsAt(*it, TabCloseTypes::CLOSE_USER_GESTURE)`) and `:347`.
- **What's wrong:** `TabStripModel` saves a closed tab to `TabRestoreService` only when `CLOSE_CREATE_HISTORICAL_TAB` is set:
  - `CloseTabs` (upstream `tab_strip_model.cc:4229-4230`) sets `create_historical_tab = close_types & CLOSE_CREATE_HISTORICAL_TAB`.
  - `DetachTabImpl` (`:946-949`) calls `delegate_->CreateHistoricalTab` only when that is true.
  - Koan passes `CLOSE_USER_GESTURE` alone, so none of the tabs Clear closes reach the restore list. The bug was latent on develop, but this PR adds the toast that advertises the undo.
- **Failure scenario:** Click Clear on three tabs. The toast says "Tabs Closed! Use Ctrl+Shift+T to undo." Ctrl+Shift+T restores whatever was closed before, or nothing, and the three tabs are lost.
- **Fix:**
  - Pass `TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB | TabCloseTypes::CLOSE_USER_GESTURE`, the pair upstream uses for bulk closes at `tab_strip_model.cc:3494`.
  - Extend `sidebar_tabs.mjs`: after Clear, press Ctrl+Shift+T and check the page count goes back up.

---

## Should fix

### S1. The Koan rows aren't behind `kKoanSidebar`

- **Where:** `src/browser/ui/views/koan_sidebar_tabs.cc:433-494`.
- **What's wrong:** `InstallRows()` checks only that the region is a `VerticalTabStripRegionView`. With `kKoanSidebar` disabled, Chromium's vertical tabs can still be on: the pref was set earlier, or the user picks vertical tabs. Then Koan still:
  - inserts the rows;
  - hides Chromium's "+" and the top separator;
  - shows Clear and the toast.

  The layout constants, Essentials painting and close-on-hover hooks are gated, so with the flag off the window becomes half Koan, half Chromium. CONTRIBUTING requires every user-visible feature to sit behind a flag.
- **Failure scenario:** Start with `--disable-features=KoanSidebar` on a profile with vertical tabs on. The tab strip looks like Chromium's, except there is no "+" and Zen's New Tab and Clear rows appear.
- **Fix:** Return early from `InstallRows()` when `!features::IsSidebarEnabled()`.

### S2. The sidebar width limits change upstream constants for everyone

- **Where:** `patches/chrome/browser/ui/tabs/vertical_tab_strip_state.h.patch`.
- **What's wrong:** `kVerticalTabStripUncollapsedMinWidth` and `kVerticalTabStripUncollapsedMaxWidth` are `constexpr`, so the new values 163 and 500 apply whether or not `kKoanSidebar` is on. They also feed other values:
  - `kVerticalTabStripCollapseSnapWidth` (91 becomes 109);
  - `TabView::UncollapsedMinWidth()` (`tab_view.cc:1380`), which sets when the close button switches to collapsed behaviour;
  - the window's minimum width (`browser_view_tabbed_layout_impl.cc:419`).

  The patch also deletes upstream's TODO comments, which makes the rebase diff bigger than it needs to be.
- **Failure scenario:** With the flag off, Chromium's vertical tab strip can't be narrowed below 163px and snaps to collapsed at 109px.
- **Fix:**
  - Preferred: gate at the two runtime call sites instead. Clamp in `VerticalTabStripRegionView` (`:533`, `:756-757`) through a small Koan helper.
  - Otherwise, record in ZEN_PARITY.md and REBASE.md that this change can't be gated.
  - Either way, keep the upstream TODO lines in the patch.

### S3. The collapsed sidebar shows two separator lines

- **Where:** `src/browser/ui/views/koan_sidebar_tabs.cc:240-248` (`KoanClearTabsRow::OnPaint`), and the hook in `tab_strip_view_layout.cc.patch`.
- **What's wrong:** Upstream shows its own `tabs_separator` (a `FrameSeparator`) when `pinned && unpinned && is_collapsed` (`tab_strip_view_layout.cc:274-276, 323-334`). Koan's Clear row also draws its line whenever the workspace has unpinned tabs, including when collapsed (the button is then 0px wide, and the line spans the row).
- **Failure scenario:** Collapse the sidebar while you have an Essential and a tab. The upstream separator sits right above Koan's line, giving two 1px lines about 8px apart.
- **Fix:** Draw only one of them. Hide Koan's line while `TabStripView::is_collapsed_`, which needs a hook argument or a check on the row's width. Or hide upstream's separator under the flag.

### S4. The New Tab and Clear rows are custom-painted without RTL mirroring

- **Where:** `src/browser/ui/views/koan_sidebar_tabs.cc:100-124` (`KoanNewTabRow`), `:166-195` (`KoanClearButton`) and `:240-248` (`KoanClearTabsRow`).
- **What's wrong:** Each of these paints at fixed LTR coordinates (`kNewTabIconX`, `kNewTabLabelX`, `TEXT_ALIGN_LEFT`, a line from `kSeparatorInset` to `button_->x()`). `View::flip_canvas_on_paint_for_rtl_ui_` defaults to false (`ui/views/view.h:2619`), and none of these views turns it on. The child bounds set in `Layout()` are mirrored automatically, but the painting isn't.
- **Failure scenario:** In an RTL locale (`--lang=ar`):
  - The New Tab "+" and label sit on the left, while the tabs' favicons and titles are on the right.
  - In the Clear row, the button is mirrored to the left end, but `OnPaint` still draws the line from x=4 to `button_->x()` in LTR coordinates. The line runs under the button and leaves a gap at the other end.
- **Fix:** Call `SetFlipCanvasOnPaintForRTLUI(true)` on the three views. Better: build the New Tab row from an `ImageView` and a `Label` in a `BoxLayout`, which mirror for free.

### S5. Hovering the toast's text doesn't keep it open

- **Where:** `src/browser/ui/views/koan_sidebar_tabs.cc:311-379` (`KoanToastView::OnMouseEntered`/`OnMouseExited`).
- **What's wrong:** The toast is mostly three `views::Label` children, which are event targets. `RootView::HandleMouseEnteredOrMoved` (`root_view.cc:875-905`) makes the Label the mouse-move handler and sends `kMouseExited` to the toast view, because it doesn't set `SetNotifyEnterExitOnChild(true)`.
- **Failure scenario:** Pointing at "Tabs Closed! Use…" never stops the timer, so the toast hides after 2s. Pointing at the 10px padding first and then moving onto the text restarts the hide timer. Zen keeps the toast while the pointer is anywhere over it.
- **Fix:** Call `SetNotifyEnterExitOnChild(true)` in the `KoanToastView` constructor, or `SetCanProcessEventsWithinSubtree(false)` on the labels.

### S6. Keyboard focus on Clear shows a focus ring around a bare line

- **Where:** `src/browser/ui/views/koan_sidebar_tabs.cc:141-146, 166-178`.
- **What's wrong:** `KoanClearButton` is focusable: `Button` sets `PlatformStyle::kDefaultFocusBehavior`, which is ALWAYS on Linux, and installs a focus ring on focus. But it only paints its label while `revealed_` (pointer over the sidebar) or hovered.
- **Failure scenario:** A keyboard user tabs through the sidebar. Focus lands on a focus ring drawn around a line, with no "Clear" text. The accessible name "Clear" is also vague out of context.
- **Fix:**
  - Treat `HasFocus()` as revealed: override `OnFocus`/`OnBlur` and call `SchedulePaint`.
  - Give it Zen's tooltip text "Close all unpinned tabs" as the accessible description, or use "Clear (close all unpinned tabs)" as the name.
  - Optionally give the rows a rounded focus-ring path using `SidebarRowCornerRadius()` (see N5).

### S7. Screen readers get no feedback from the toast

- **Where:** `src/browser/ui/views/koan_sidebar_tabs.cc:524-579`.
- **What's wrong:** The toast is a non-activatable `TYPE_CONTROL` widget with no role and no announcement. Clear and Ctrl+Shift+K close many tabs at once, and a screen-reader user is never told, or told how to undo.
- **Fix:** Call `toast_view_->GetViewAccessibility().AnnounceText(u"Tabs closed. Use Ctrl+Shift+T to undo.")` after `Show()`, or give it `ax::mojom::Role::kAlert`.

### S8. Issue checklist items not done and not documented: tab row colours, padding and favicon size

- **Where:**
  - `src/browser/ui/color/koan_color_mixer.cc:68-88`;
  - `docs/ZEN_PARITY.md:25`;
  - issue #33, checkbox 2: "Zen's height, radius, padding, active/hover colours, close button only on hover, favicon size".
- **What's wrong:** The PR changes the row height (36px), the radius (14px) and close-on-hover only.
  - The unpinned rows still use Chromium's active and hover tab colours, its padding and its favicon size. Zen's are in `vertical-tabs.css:1181` (`--tab-background-color-selected: light-dark(rgba(255,255,255,0.85), rgba(255,255,255,0.2))`, the same values the PR already uses for Essentials) and `zen-theme.css:167` (hover is 7% of the reverse colour).
  - ZEN_PARITY.md:25 nonetheless says "Same … Sizes and colours from Zen's `vertical-tabs.css`".
- **Fix:**
  - Map the vertical tab background IDs (`kColorTabBackgroundActive*` and the hover recipes) to the Essentials colour IDs or new Koan IDs in the mixer, under the flag.
  - Otherwise, mark the row "Different" and list what's left.

### S9. The New Tab row adds the tab at the bottom; Zen's top row puts new tabs at the top (partly verified)

- **Where:** `src/browser/ui/views/koan_sidebar_tabs.cc:84-92`.
- **What's wrong:** The row runs `IDC_NEW_TAB`. The tab is appended at the end, and in a workspace it is adopted with `add_to_end=true` (`koan_workspaces.cc` `AdoptUngroupedTabs`). With `zen.view.show-newtab-button-top` (on by default, `zen-default-prefs.js:122`), Zen places tabs at the top of the space: `ZenSpaceManager.mjs:1765-1785` inserts at `container.firstChild` when the pref is on.
  - I verified the insertion point for tabs moved into a space. I did not trace a plain Ctrl+T end to end in Zen.
- **Failure scenario:** With 30 tabs, clicking "+ New Tab" at the top opens the tab off-screen at the bottom of the list.
- **Fix:** Check in a running Zen. If confirmed, insert at the first unpinned index of the workspace (`chrome::AddTabAt(browser, GURL(), first_unpinned_index, true, workspace)`), or record the difference in ZEN_PARITY.md.

### S10. The e2e test misses the regressions above

- **Where:** `scripts/e2e/sidebar_tabs.mjs`.
- **What's wrong:** The test checks geometry and click-through only. It doesn't cover:
  - Ctrl+Shift+T after Clear (would catch B3);
  - Clear inside a focused workspace with an Essential selected (B2);
  - toggling vertical tabs off and on (B1);
  - that Clear is hidden until the pointer is over the sidebar. The test clicks the button without hovering, and `Button` fires whether or not `revealed_` is set, so the hover reveal, the main Zen behaviour, is untested;
  - that the active tab's close button is hidden until hover;
  - the collapsed sidebar (S3).

  Also, line 178 (`!find(vs,'KoanClearTabsRow')?.shown`) passes even when the row doesn't exist at all.
- **Fix:**
  - Add those cases. For hover, send a UI DevTools `mouseMoved` into the region and compare the `KoanClearButton` paint state, for example through a test-visible property or the accessible name, or at least check that it is still a line before hovering.
  - Assert that the row exists before asserting it is hidden.

---

## Nits

- **N1. Workspaces lose the Clear arrow when nothing is closable.**
  - `koan_sidebar_tabs.cc:503-514`.
  - Zen's `can-close` is `tabs.length === closableTabs.length`, and `#getClosableTabs` returns all tabs when none are safe (`ZenSpaceManager.mjs:1636-1662, 2923-2931`). So Zen also shows the arrow when only the selected or audible tabs are left, for example a single selected tab.
  - Koan's `all_closable` is false in that case.
  - Fix: `SetShowIcon(all_closable || none_closable)`.

- **N2. The arrow state goes stale when audio starts or stops.**
  - `UpdateClearRow` runs only from `OnTabStripModelChanged` and `OnTabGroupFocusChanged`, but audible changes arrive through `TabChangedAt`.
  - Fix: override `TabChangedAt` (for `TabChangeType::kAll`) or watch the audible-state helper.

- **N3. The toast can linger invisibly and swallow clicks.**
  - `koan_sidebar_tabs.cc:550-553, 587-602`.
  - Hovering during the 200ms fade stops `toast_timer_`, which at that point holds the `CloseToast` task. The widget stays at opacity 0, and its full-size bounds keep taking clicks meant for the page underneath until the pointer leaves and another 2.2s pass.
  - Fix: ignore hover once hiding has started, or use a separate timer for the close.

- **N4. Toast timing and look differ slightly from Zen.**
  - Zen starts its 2s timer after the 0.5s spring finishes (`ZenToasts.mjs:76-83`). Koan starts it immediately, so the toast is visible about 0.5s less.
  - Zen's key cap is `light-dark(rgba(255,255,255,0.4), 0.1)` and the toast has a `0 0 22px 2px` shadow (`zen-popup.css:345, 363`). Koan uses 10% in both modes and draws no shadow.
  - The toast doesn't move when the window is resized while it's shown.

- **N5. Small paint details on the rows.**
  - `KoanClearButton::Font()` builds a derived `FontList` on every `CalculatePreferredSize` and paint (`:159, :192, :198`). Cache it.
  - The rows' focus ring uses the default rectangle, not the 14px row radius. Set a `HighlightPathGenerator`.

- **N6. Hard-coded SkColors in the mixer.**
  - `koan_color_mixer.cc:51, 55, 59, 61, 63` use `SK_ColorWHITE`, `SK_ColorBLACK` and `0x0f0f0f`, but `koan_color_id.h` says the IDs are "defined from Chrome's colours".
  - Keeping them in the mixer is acceptable for #48, but fix the comment, or derive them (for example `kColorSysOnPrimary` for the toast text).

- **N7. Essentials tiles lose the multi-select look and may paint oddly in splits.**
  - `koan_essentials.cc:49` paints only `kActive` as selected, so multi-selected (`kSelected`) pinned tiles look idle.
  - Uncertain, not rendered: in a pinned split tab, each half would paint its own idle tile inside the `SplitTabView` background.

- **N8. GN deps are incomplete for `gn check`.**
  - `src/browser/ui/views/BUILD.gn` `:impl` now includes `chrome/browser/ui/layout_constants.h` (through `koan_sidebar_metrics.h`), `chrome/browser/ui/views/tabs/common/tab_strip_view.h` and `ui/color/color_provider.h`, but doesn't list `//chrome/browser/ui:layout_constants`, `//chrome/browser/ui/views/tabs/common` or `//ui/color`.
  - It compiles because of transitive deps, but `gn check` would flag it.

- **N9. REBASE.md misses the view-tree assumptions that have no patch.**
  - `InstallRows` finds views by class-name strings: `VerticalTabStripRegionView`, `FlexLayoutView`, `FrameSeparator`, `VerticalTabStripBottomContainer` and `NewTabButton`. An upstream rename silently disables the feature.
  - Add a "(no patch)" row, as done for the floating URL bar.
  - Where deps allow, prefer `views::IsViewClass<>` or upstream accessors.

- **N10. Separator hide animation.** Zen animates the separator's height and opacity (0.08s) when it hides (`vertical-tabs.css:139-146, 186-190`). Koan toggles visibility instantly. Not important.

- **N11. Commit history.** It has "WIP sidebar tabs (previous agent)" and "fix compile: include separator.h" commits. They're harmless with a squash merge, but CONTRIBUTING asks for fixups to be folded in.

---

## Checked and fine

- All 9 changed or added patches apply cleanly to the pristine Chromium 158 files with `git apply --check`, with no offsets. The hunks are small, and the `layout_constants.cc` change is gated on `kKoanSidebar`.
- Every upstream API used exists with the signature used:
  - `TabStripView::GetTabsSeparator`, `TabStyleViewDelegate::IsPinned`/`IsHoverAnimationActive`/`GetView`;
  - `ProposedLayout::child_layouts`, `gfx::TransformAboutPivot`, `FontList::DeriveWith*`;
  - `kArrowDownwardIcon`, `vector_icons::kAdd2Icon`, `ui::AlphaBlend`/`SetAlpha`.
- Child order puts the rows (index after the separator) before the unpinned scroll view, so focus order is right.
- The rows' height is subtracted inside the bounded-height branch before the pinned grid is sized.
- The `IsPositionInWindowCaption` hook is correct: without it the rows would count as caption and drag the window.
- The sidebar-hover detection is sound. `VerticalTabStripRegionView` sets `SetNotifyEnterExitOnChild(true)` (`vertical_tab_strip_region_view.cc:233`), and `RootView::NotifyEnterExitOfDescendant` dispatches to the region with the region as target, so the `event->target() == region_` filter works.
- The toast widget is `TYPE_CONTROL`, so `InitParams::CanActivate()` is false (`widget.cc:241-248`): it doesn't steal focus from the page or the omnibox.
- The toast is closed in `OnBrowserViewDestroying`, and the hover callback uses a `WeakPtr`.
- Zen values match:
  - separator 22px with the line inset 4px from the start and the line-through-button trick;
  - Clear: 10px, weight 500, 2px gap, 8px start padding, 50% then 100% opacity, icon moved down 2px while pressed;
  - New Tab row at 70%, hover at 7%;
  - Essentials 46px tall, 4px gap, at most 4 columns, the selected and hover colours (`vertical-tabs.css:1181, 1204`). I didn't check the idle tile colour;
  - toast 42px, 14px radius, the gradient, 2s timeout;
  - width 163–500px;
  - strings "Clear", "Tabs Closed! Use … to undo." and "Close all unpinned tabs".
- `clang-format --dry-run --Werror` is clean on all new and changed C++ files.
