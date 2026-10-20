# Review, round 2: PR #50, Zen sidebar tab list (Fixes #33)

Branch `feat/zen-sidebar-tabs` at `5c7e96f`, after `git fetch origin`, diffed against `origin/develop`. I reviewed the code only: nothing was built or run.
I checked it against Chromium 158 (`chromium/src` HEAD `67e29b82`, read with `git show HEAD:<path>`) and the Mac checkout (same revision, read-only), and I read Zen's source in `.cache/zen-src` where parity mattered.
Paths are relative to the worktree. Patched-upstream line numbers are from the pristine files with the PR's patches applied in a scratch directory.

**Verdict: safe to merge.** There are no blockers. One small should-fix (a toast that isn't behind the flag) can go in before the merge or straight after. The nits are optional.

---

## Blockers

None. All three round-1 blockers are fixed. What I checked:

- **B1 (use-after-free on a vertical/horizontal switch; rows never come back): fixed.**
  - `rows_`: the controller watches the rows view through `rows_observation_` (`koan_sidebar_tabs_controller.h:165`). `OnViewIsDeleting` (`koan_sidebar_tabs.cc:833-837`) clears `rows_`.
    - Upstream deletes the `TabStripView` in `ClearTabStripView` (`base_tab_strip_region_view.cc:442-453`), and that deletes its children too. So `rows_` is cleared synchronously during `ResetTabStrip()`, before any `OnTabStripModelChanged` can reach it.
    - Every later use (`UpdateClearRow`:648, `OnMouseEvent`:823, `OnBrowserViewDestroying`:568) checks for null.
  - Reinstall: the controller subscribes to `RegisterOnModeChanged` (`:547`) and posts `InstallRows` with `weak_factory_.GetWeakPtr()` (`:578-580`).
    - If the window closes before the task runs, there are two guards. The controller is destroyed in `DestroyFor`, which invalidates the WeakPtr. Even before that, `OnBrowserViewDestroying` sets `started_ = false`, and `InstallRows` returns on `!started_` (`:589`).
    - Two quick switches (V→H→V) post two tasks. The first one installs, and the second returns because `rows_` is set. A task that runs while the window is horizontal fails the `IsViewClass<VerticalTabStripRegionView>` check (`BrowserView::tab_strip_view()` returns the horizontal region, `browser_view.cc:1338-1346`).
    - `BrowserView` registers its own mode callback in its constructor (`browser_view.cc:1084-1089`), before Koan's (`OnAddedToWidget`), so the strip is rebuilt before the post either way.
  - Pre-target handler: the `VerticalTabStripRegionView` is created once in the `BrowserView` constructor (`browser_view.cc:993-1004`). It is only nulled in `~BrowserView`. `ResetTabStrip`/`InitializeTabStrip` never replace it, so `region_` can't dangle between modes.
    - `koan::OnBrowserViewDestroying` is the first statement of `~BrowserView` (`browser_view.cc.patch`). It removes the handler (`:564-567`) while the region is still alive, and only then does `ClearProperty` destroy the controller.
  - Window closing while vertical: the rows are removed (`:568-571`) before the controller goes. Their `Unretained(this)` callbacks never outlive it.
  - The toast is `CLIENT_OWNS_WIDGET`. Its `RootView`, and with it `toast_view_`, lives until `~Widget`, and `CloseNow` checks `native_widget_`. So a window closed while the toast shows is safe, even if the native child goes first.
- **B2 (Clear deletes the workspace): fixed in every case I traced** (`koan_shortcuts.cc:395-429`):
  - *An Essential is selected in workspace W, tabs A and B:* `to_close = {A, B}` and `empties_workspace` is true. A background tab is added into W at `to_close.back()+1` *before* anything closes. Closing from the back keeps the lower indices valid. W never becomes `IsEmpty()`, so `TabGroupStateChanged` (`tab_strip_model.cc:5216-5230`) never calls `ExitFocusMode`/`RemoveTabGroup`.
  - *The selected tab is in W:* it is kept, so W keeps at least that tab. If it is the only tab, or every tab is selected or audible, `closes_selected` adds a foreground tab into W first.
  - *The "∞" view:* `UnpinnedTabsForClear` (`:433-446`) only takes tabs with `GetTabGroupForTab(i) == nullopt`, so no grouped tab is touched. When the selected tab is the last ungrouped one, the fresh tab is ungrouped (`workspace` is nullopt).
  - *Collapsed groups:* only reachable in the ∞ view, where grouped tabs are excluded. In a focused workspace, the replacement tab is added with `group = W` before the closes.
  - Pinned tabs can't be in groups, so `TabGroup::tab_count()` equals the workspace's unpinned count.
  - `group_model()` is only dereferenced when `GetFocusedGroup()` has a value, which never happens in popup or app windows.
- **B3 (undo toast is false): fixed.** `koan_shortcuts.cc:425-426` passes `CLOSE_CREATE_HISTORICAL_TAB | CLOSE_USER_GESTURE`, so `DetachTabImpl` calls `CreateHistoricalTab` (`tab_strip_model.cc:948-949`). The restore entry records the group, so a restored tab goes back into its workspace. N2 below is about how many tabs one press brings back.

---

## Should fix

### S1. The "Tabs Closed!" toast isn't behind `kKoanSidebar`

- **Status of round-1 S1–S10:** fixed as claimed, except for this one gap in S1. Details are under "Checked and fine".
- **Where:**
  - `src/browser/ui/views/koan_shortcuts.cc:428` calls `controller_->sidebar_tabs()->ShowTabsClosedToast()` unconditionally;
  - `src/browser/ui/views/koan_sidebar_tabs.cc:703-752` (`ShowTabsClosedToast`) checks neither `started_` nor the flag.
- **What's wrong:** S1's fix gates `OnAddedToWidget` (`:540`), which covers the rows, the hidden "+" and the hidden `FrameSeparator`. Ctrl+Shift+K isn't behind `kKoanSidebar`: the table entry at `koan_shortcut_table.cc:73, 132` has no `enabled` predicate, unlike compact mode's. So the toast this PR adds for #33 also shows with the sidebar flag off. CONTRIBUTING asks for every user-visible feature to be behind a flag.
- **Failure scenario:** Start with `--disable-features=KoanSidebar`, open three tabs and press Ctrl+Shift+K. The tabs close as before, but Zen's gradient "Tabs Closed! Use Ctrl+Shift+T to undo." toast appears at the top right of an otherwise Chromium-styled window.
- **Fix:** Return early in `ShowTabsClosedToast()` when `!features::IsSidebarEnabled()`. `started_` alone isn't enough, because it is also false in a window that never had a vertical strip.
  - `CloseUnpinnedTabs`'s other changes also apply with the flag off: it now keeps the selected and audible tabs, and in the ∞ view closes only ungrouped tabs. They fix the existing shortcut (B2 needs them), so leaving them ungated is reasonable, but say so in the PR.

---

## Nits

### N1. REBASE.md's width-limits row overstates where the limits apply, and macOS's glass frame still uses 400px

- **Where:**
  - `docs/REBASE.md:48` says "the uncollapsed min/max width and the collapse snap width are still only applied there" (the region view).
  - Upstream also reads the constants in other places:
    - `browser_view_tabbed_layout_impl.cc:419`: the window's minimum width reserves 126px to uncollapse;
    - `tab_view.cc:1383`: `UncollapsedMinWidth`;
    - `vertical_tab_strip_top_container.cc:160`: the combo button flips at 91px rather than Koan's 109px;
    - `tab_view_vertical_layout.cc:51`: a fallback;
    - on macOS, `browser_native_widget_mac.mm:328-330`: `GetGlassFrameWidth()` caps the glass view at `kVerticalTabStripUncollapsedMaxWidth` (400) plus padding.
- **Failure scenario (latent):** On macOS 26 with Chromium's `GlassFrame` feature on (off by default, and Koan doesn't turn it on), drag the Koan sidebar to 450–500px. The glass backdrop stops at about 400px, and the last 50–100px of the sidebar sits outside it. While dragging, it's also capped at 400.
  - None of the other readers misbehaves visibly today. I traced the layout: the 126px reservation is the same squeeze upstream already accepts with its 240px default width.
- **Fix:** List these readers in the REBASE.md row so a rebase rechecks them. Optionally route the Mac glass cap through `koan::SidebarMaxWidth` when the glass frame becomes relevant.

### N2. Ctrl+Shift+T brings back one tab per press, but the docs say it restores "the tabs"

- **Where:**
  - `koan_shortcuts.cc:423-427` closes each tab with its own `CloseWebContentsAt`, and `DetachTabImpl` creates one restore entry per tab (`tab_strip_model.cc:948-949`).
  - The docs: `docs/USAGE.md:27, 87` and `docs/ZEN_PARITY.md:26, 60` ("Ctrl+Shift+T restores the tabs").
- **Failure scenario:** Clear closes five tabs. One Ctrl+Shift+T brings back only the top one, and four more presses are needed. The test (`sidebar_tabs.mjs:259`) only checks that one comes back.
- **Fix:** Reword the docs to say that each press brings back one tab. Restoring them all at once needs a batch restore entry, which is out of scope here.
  - I didn't check whether Zen restores the whole batch with one press. That depends on Firefox's `removeTabs`/SessionStore, which isn't in the Zen tree.

### N3. In the ∞ view the separator line disappears while tabs are listed

- **Where:** `koan_sidebar_tabs.cc:661-662` shows the Clear row only when `UnpinnedTabsForClear` is non-empty. In the ∞ view that list only holds ungrouped tabs (`koan_shortcuts.cc:441`), but the view lists every workspace's tabs.
- **Failure scenario:** With one Essential and all tabs in workspaces, switch to ∞. The Essentials tiles sit directly on the tab rows with no line between them. With one ungrouped tab, the line comes back.
- **Fix:** Show the line when any unpinned tab is listed, and show the Clear button only when there are ungrouped tabs. Or hide Clear but keep the row.

### N4. The ∞-view e2e check can pass without clicking Clear

- **Where:** `scripts/e2e/sidebar_tabs.mjs:312-319`. `if (button?.shown) await click(button)`, then the check only compares the switcher button count.
- **Failure scenario:** If the Clear button is hidden or missing at this point (no ungrouped tabs, or a renamed class), nothing is clicked and the check still passes.
- **Fix:** Assert that the button was shown and clicked. For example, open an ungrouped tab first, then check that the ungrouped tab count drops while each workspace's tab count stays the same.

### N5. A width saved with the flag on survives turning the flag off, and the reverse

- **Where:** `VerticalTabStripStateControllerImpl` restores the saved width with no clamp (`vertical_tab_strip_state_controller_impl.cc:80-81`). The region uses it directly (`vertical_tab_strip_region_view.cc:432, 545`).
- **Failure scenario:** With Koan's sidebar on, drag it to 480px, quit, and restart with `--disable-features=KoanSidebar`. Chromium's sidebar opens at 480px, above its own 400px maximum, until it is dragged. In the other direction, a 130px width saved with the flag off opens below Koan's 163px minimum.
- **Fix:** Optional. Clamp the restored width in the region with the `koan::SidebarMin/MaxWidth` helpers, or accept it and mention it in ZEN_PARITY.md.

### N6. REBASE.md doesn't list the favicon-gap hook

- **Where:** `patches/.../tab_view_vertical_layout.cc.patch`, first hunk (`OnInstalled`: `koan::SidebarTabIconGap(kHorizontalInset)`). The `docs/REBASE.md:46` row only names `IsChildVisible`.
- **Failure scenario:** On a rebase where upstream reorders `tab_children_configs_`, or turns the icon's `padding` into a leading inset, the hook silently changes the wrong gap, and the table gives no hint to check it.
  - I checked today's code: the padding is applied after the icon (`bounds_remaining.Inset(set_left(padding + min_width))`), so it really is the icon-to-title gap.
- **Fix:** Add `OnInstalled` (icon gap) to the row.

---

## Checked and fine

- **The round-1 should-fixes:**
  - **S1:** fixed except for the toast (S1 above). `OnAddedToWidget` returns early without the flag, so no observers, rows, pre-target handler or hidden views. Every hook returns upstream's value or does nothing with the flag off: `PaintSidebarTab`:489, `ClampEssentialsColumns`, `SidebarTabIconGap`/`Min`/`Max`/`CollapseSnapWidth`, the close-button rule and `layout_constants`. `AddKoanColors` now runs ungated, but it only defines Koan IDs. The one upstream ID it changes (`kColorVerticalTabPinnedOutline`) is still behind the flag.
  - **S2:** fixed. The `vertical_tab_strip_state.h` patch is gone, so upstream's constants and TODOs are untouched. The region's `GetMinimumSize` and resize handler pass upstream's values through when the flag is off. See N1 and N5 for the leftovers.
  - **S3:** fixed. `OnCollapseChanged` → `UpdateClearRow` hides the Clear row while collapsed (`:661-662`), leaving upstream's separator as the only line.
  - **S4:** fixed. I worked through the LTR and RTL coordinates:
    - New Tab: the icon sits at `w-24..w-8` and right-aligned text ends at `w-28`, a 4px gap as in LTR.
    - Clear: the icon sits at `w-18..w-8` and the text ends at `w-20`, a 2px gap.
    - The row line runs from the mirrored start to `button_->x()`, in unmirrored coordinates, mirrored once.
  - **S5:** fixed (`SetNotifyEnterExitOnChild(true)`, `:389`). Moving between the toast and its labels no longer sends exits to the toast.
  - **S6:** fixed: focus reveals Clear, the accessible description is set, and the focus rings are rounded.
  - **S7:** fixed (`AnnounceAlert`, which exists in `view_accessibility.h:571` on both checkouts).
  - **S8:** fixed. The rows, tiles, separator, Clear and toast paint only through `kColorKoan*` IDs. Fixed values (`SK_ColorWHITE`, `SK_ColorBLACK`, `#0f0f0f`) appear only in `koan_color_mixer.cc`. The views use `SK_ColorTRANSPARENT` only as the "no fill" and label sentinel. Koan's IDs start at `kChromeColorsEnd`; nothing else in Chromium uses that range, and `ColorProvider` keeps colours in a `std::map`.
  - **S9:** fixed: the tab goes in at the workspace's first index, or the first unpinned index in ∞.
  - **S10:** fixed, apart from N2 and N4.
- **The round-1 nits** N1, N2, N3, N5, N6, N8, N9 and N11 are done as described. N4 (shadow) and N10 were declined, with reasons given in the reply.
- **Hook patches:** all ten changed or new patches apply to the pristine files with `git apply` in a scratch directory, with exit 0 and no offset or fuzz. Each hunk is small, and each is a pass-through or no-op with the flag off:
  - `GetSidebarTabRowsHeight`/`PlaceSidebarTabRows` return 0 when no rows are installed;
  - `IsSidebarTabRowsView` is false;
  - `PaintSidebarTab` returns false.
  - `PaintTab`'s hook skips upstream's theme-image and `should_fill_background_tab_color_` paths only with the flag on. That flag is a theme property, not a dragging state.
  - `tab_strip_view_layout.cc` reads `size_bounds.width().value()` only after upstream's `is_bounded()` early return.
  - `kVerticalTabMinWidth=48` only sets the column count. In the collapsed sidebar the one tile is stretched to the available width (`pinned_tab_container_view.cc:430-433`), so it doesn't overflow.
- **GN:** no cycle. The upstream targets (`tabs/common:impl`, `frame:vertical_tab_strip_region_view`, `ui:layout_constants`) depend only on the header-only `//koan/browser/ui/views` and on `//koan/browser:features` (base only). `:impl` lists the deps that round-1 N8 asked for.
- **macOS (read-only on the Mac checkout, same revision):**
  - The only `BUILDFLAG(IS_MAC)` code is in `koan_sidebar_metrics.h`. It includes `build/build_config.h`, and both branches define the same names.
  - `EF_PLATFORM_ACCELERATOR` is `EF_COMMAND_DOWN` on Mac (`event_constants.h:54`), and `Accelerator::GetShortcutText()` exists.
  - `NativeWidgetMac::SetBounds` converts a `TYPE_CONTROL` child's parent-relative bounds to screen coordinates (`ConvertBoundsToScreenIfNeeded`, `native_widget_mac.mm:643-674`), so the toast lands at the window's top right.
  - `frame:vertical_tab_strip_region_view` builds on every platform, and nothing in the PR uses Aura.
