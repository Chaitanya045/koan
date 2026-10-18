# Upstream Chromium research for Koan (late 2026)

Scope: how far upstream Chrome's own desktop UI can carry Koan's Zen-style UX, and
what we must build ourselves. All findings were read against **`main` at the time of
this investigation** via the GitHub mirror (`raw.githubusercontent.com/chromium/chromium/main/...`),
`chromium.googlesource.com`, the Chromium bug tracker, and press coverage. Every claim is
cited; anything I could not directly confirm is marked **UNVERIFIED**.

Tenor of the result: **two of the six features Koan wants already exist upstream in native
Views and have launched** (vertical tabs, split view). One (compact mode) can partly reuse
upstream collapse/animation code but has **no Linux fullscreen auto-hide to inherit**. One
(Glance) had an upstream experiment that was **deleted**. Workspaces and Essentials have no
upstream equivalent.

---

## 1. Vertical tabs

**Status: LAUNCHED in Chrome (stable). The desktop feature flag has been removed; the feature is now gated by a user preference, not a `base::Feature`.**

### Flag names and history

- The original desktop flags were declared in `chrome/browser/ui/tabs/features.{h,cc}`:
  `kVerticalTabs`, `kVerticalTabsLaunch`, `kVerticalTabsToggleInTabContextMenu`,
  `kVerticalTabsPreviewBadge`, `kVerticalTabsNewBadge`, `kVerticalTabsExpandOnHover`, and the
  param `kVerticalTabsExpandOnHoverDelay` (default 500 ms). See the M146-era diff at
  <https://chromium.googlesource.com/chromium/src/+/528f9390eb569a4cfcf1d24141b2a8625fe79033%5E%21/>
  and the EOH-click-delay addition at
  <https://chromium.googlesource.com/chromium/src/+/a0e817b4ac33bd38e5fdb49c1dc433fec9b83d34%5E%21/>.
- The flag was briefly in the Beta branch and pulled back in M145:
  <https://chromium.googlesource.com/chromium/src/+/9c2ee1d49b653cf9a1401e868e59027e26fe8dd1>
  ("[M145] Revert Vertical Tabs flag from flags page").
- It reappeared in M146 (`chrome://flags/#vertical-tabs`), per press:
  <https://www.bram.us/2026/01/16/chrome-145-adds-experimental-support-for-vertical-tabs/>
  and <https://www.askvg.com/tip-how-to-enable-or-disable-vertical-tabs-in-google-chrome/>.
- **It then launched to the Stable channel (~Chrome 146, March 2026)**:
  <https://www.reddit.com/r/browsers/comments/1s0lbzq/vertical_tabs_have_finally_arrived_in_the_stable/>
  and <https://www.askvg.com/tip-how-to-enable-or-disable-vertical-tabs-in-google-chrome/>.
- **The desktop flag was subsequently removed** in a commit titled
  "[Vertical Tabs] Clean up flags entries for launched feature":
  <https://chromium.googlesource.com/chromium/src/+/b3a67749c444b79dd4015c4745ef66f2b26cd9ec>.
  Confirming this, on current `main`:
  - `chrome/browser/ui/tabs/features.{h,cc}` no longer declare `kVerticalTabs`/`kVerticalTabsLaunch`;
    only `kVerticalTabsNewBadge` survives
    (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/tabs/features.cc>,
    <https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/tabs/features.h>).
  - `chrome/browser/about_flags.cc` on `main` contains **no** `"vertical-tabs"` desktop entry; the
    only vertical-tabs flag is `"android-vertical-tabs"` (`kAndroidVerticalTabs`)
    (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/about_flags.cc>,
    added in <https://chromium.googlesource.com/chromium/src/+/1a1ab850db8d9d024fd5d6504f056709dc85d5f0>).
  - The runtime gate is now a **preference**: `prefs::kVerticalTabsEnabled` (and
    `kVerticalTabsExpandOnHoverEnabled`, `kVerticalTabsCollapsedState`, `kVerticalTabsUncollapsedWidth`),
    read by the state controller
    (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/tabs/vertical_tab_strip_state_controller_impl.cc>).

### Key source and classes

- `chrome/browser/ui/views/frame/vertical_tab_strip_region_view.{h,cc}` — `VerticalTabStripRegionView`
  (implements `BaseTabStripRegionView`, `views::ResizeAreaDelegate`, `OmniboxTabHelper::Observer`,
  `tabs::VerticalTabStripStateController::Delegate`, `OrganizerPanelHost`, `views::LayoutDelegate`).
  It owns `vertical_tab_strip_top_container`, `_bottom_container`, a `views::ResizeArea`,
  `ShadowFrameView`, and the expand-on-hover machinery
  (<https://chromium.googlesource.com/chromium/src/+/refs/heads/main/chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h>).
- `chrome/browser/ui/views/tabs/vertical/` — `VerticalTabStripTopContainer`,
  `VerticalTabStripBottomContainer`, `VerticalTabStripScrollBar`,
  `VerticalTabStripSegmentedControl`, `VerticalTabStripExpandOnHoverLock`,
  `VerticalTabStripFocusSwipeController`
  (<https://github.com/chromium/chromium/tree/main/chrome/browser/ui/views/tabs/vertical>).
- `chrome/browser/ui/tabs/vertical_tab_strip_state_controller.{h,cc}` +
  `_impl.{h,cc}` — `tabs::VerticalTabStripStateController` manages display-mode, collapse state
  (`VerticalTabStripCollapseState::kCollapsed/kCollapsing/kExpanded`), uncollapsed width and
  expand-on-hover, and serialises to the session service; API includes
  `ShouldDisplayVerticalTabs()`, `RequestCollapse()`, `GetEnableStateLock()`,
  `RegisterOnCollapseChanged()`
  (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h>,
  <https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/tabs/vertical_tab_strip_state_controller_impl.cc>).
- State enum in `chrome/browser/ui/tabs/vertical_tab_strip_state.h`
  (<https://github.com/chromium/chromium/tree/main/chrome/browser/ui/tabs>).
- Wiring: `BrowserView` holds `vertical_tab_strip_region_view_` / `vertical_tab_strip_container_`
  and exposes `ShouldDrawVerticalTabStrip()`, `IsVerticalTabStripCollapsed()`,
  `vertical_tab_strip_region_view_for_testing()`
  (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/views/frame/browser_view.h>).
- The state controller is created per-window in
  `chrome/browser/ui/browser_window/internal/browser_window_features.cc`
  (<https://chromium.googlesource.com/chromium/src/+/17bce74f46c734869ad57b8fd5fa14ded7ec1a5c/chrome/browser/ui/browser_window/internal/browser_window_features.cc>).

### Notes for Koan

Vertical tabs upstream is already a **native Views vertical strip with collapse/expand-on-hover
animation, resize area, swipe controller and session persistence** — almost exactly the Koan
sidebar. It is a launched, supported surface, so Koan should *reskin/extend* it, not replace it.

---

## 2. Split view / side-by-side tabs

**Status: LAUNCHED in Chrome (Stable ~Chrome 143, December 2025). The launch flag `#side-by-side` / feature `SideBySide` is gone from `about_flags.cc`; the surviving feature constant is `tabs::kSplitViewHorizontal` ("Stacked Split Views") for the top/bottom orientation.**

### Launch / flag evidence

- The experiment was `chrome://flags/#side-by-side` (`Features::kSideBySide`), documented at
  ChromeOS 139: <https://chromeunboxed.com/you-can-try-chromes-awesome-new-split-tabs-feature-right-now-in-chromeos-139/>
  and <https://itdaily.com/news/software/chromeos-139-split-tabs/>.
- It shipped in stable Chrome 143 (Dec 2, 2025):
  <https://gadgetbond.com/google-chrome-split-tabs-feature-v143-stable-update/>. Google's own
  help page documents the shipped UX ("Add tab to new split view", drag-to-edge, stacked vs
  side-by-side, swap): <https://support.google.com/chrome/answer/16971124?hl=en>.
- On current `main`, `chrome/browser/about_flags.cc` has **no** `"side-by-side"` entry
  (verified by grep; see the file:
  <https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/about_flags.cc>).
- The remaining split constant is in `chrome/browser/ui/tabs/features.{h,cc}`:
  `BASE_FEATURE(kSplitViewHorizontal, base::FEATURE_ENABLED_BY_DEFAULT)` with params
  `kSplitViewHorizontalDirectAccess` / `kSplitViewHorizontalDirectTabAccess`, and helpers
  `IsSplitViewHorizontalIndirectAccessEnabled()` /
  `IsSplitViewHorizontalDirectAccessEnabledForTab()`
  (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/tabs/features.cc>).
  Display name "Stacked Split Views"
  (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/flag_descriptions.h>).
- Additional enabled-by-default split features live in `chrome/browser/ui/ui_features.{h,cc}`:
  `kSplitViewTabDraggingUpdates` and `kSplitViewDragAndDropVelocity`
  (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/ui_features.h>).

### Key source and classes

- `chrome/browser/ui/views/frame/multi_contents_view.{h,cc}` — `MultiContentsView` "shows up to two
  contents web views side by side"; holds `contents_container_views_`, a `MultiContentsResizeArea`
  (resize delegate), `MultiContentsDropTargetView`, `MultiContentsViewDropTargetController`,
  `MultiContentsViewMiniToolbar`, background view, separators, and snap points; exposes
  `IsInSplitView()`, `ShowSplitView()`, `CloseSplitView()`, `SwapContentsInSplitView()`,
  `GetSplitLayout()`, `GetSplitRatio()`, `SetActiveIndex()`
  (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/views/frame/multi_contents_view.h>).
- `chrome/browser/ui/views/frame/multi_contents_view_delegate.{h,cc}` —
  `MultiContentsViewDelegateImpl` handles focus sync, reverse, resize persistence via
  `SessionService`, and link/tab drag-and-drop into a split
  (<https://chromium.googlesource.com/chromium/src/+/687492b872a0a78df3fa53f2b627a2c2509f1ff2/chrome/browser/ui/views/frame/multi_contents_view_delegate.cc>).
- Model side: `chrome/browser/ui/tabs/tab_strip_model.h` — `TabStripModel::AddToNewSplit(...)`,
  `GetSplitData()`, `ReverseTabsInSplit()`, `UpdateSplitRatio()`, `GetActiveTab()`
  (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/tabs/tab_strip_model.h>).
- `components/split_tabs/` — `split_tab_id.h`, `split_tab_visual_data.{h,cc}`
  (`SplitTabVisualData`, `SplitTabLayout::kVertical/kHorizontal`, `split_ratio`)
  (<https://github.com/chromium/chromium/tree/main/components/split_tabs>).
- `components/tabs/public/` — `split_tab_data.h`, `split_tab_collection.h`, `tab_interface.h`
  (`TabInterface::IsSplit()/GetSplit()`)
  (<https://github.com/chromium/chromium/tree/main/components/tabs/public>).
- `BrowserView` entry points: `IsInSplitView()`, `multi_contents_view()`, private
  `ShowSplitView()/HideSplitView()/UpdateActiveTabInSplitView()`, and
  `OnSplitTabChanged(const SplitTabChange&)`
  (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/views/frame/browser_view.h>).
  The refactor that moved `BrowserView` off `contents_web_view_` to `multi_contents_view_` is
  <https://github.com/chromium/chromium/commit/4895def295372979a6c68a43ddedea7f0ab6d0b7>.

### Notes for Koan

Split view is production, multi-pane (2), resizable, drag-to-create, session-persisted, and
ships in native Views. Koan Phase 3 should consume it directly; if a top/bottom variant is
wanted it is already behind `tabs::kSplitViewHorizontal`.

---

## 3. Tab strip architecture: where to hook to replace the horizontal strip

The tab strip is now split behind an **abstract region interface** plus a controller/model pair.
The useful seam is `TabStripRegionView`, not `TabStrip`.

| Component | File (path on `main`) | Role |
|---|---|---|
| `BrowserView` | `chrome/browser/ui/views/frame/browser_view.{h,cc}` | Top-level `ClientView`. Owns `top_container_`, `horizontal_tab_strip_region_view_`, `toolbar_`, `bookmark_bar_view_`, `infobar_container_`, `multi_contents_view_`, `side_panel_`, and (optionally) the vertical strip. `tab_strip_view()` returns *either* the horizontal or the vertical region. |
| `TabStripRegionView` (abstract) | `chrome/browser/ui/views/frame/tab_strip_region_view.h` | Single interaction point for all tab-strip consumers; "should only be owned by BrowserView". Declares `InitializeTabStrip/ResetTabStrip`, `GetTabAnchorView`, drag/drop, `GetExpandOnHoverLock(ExpandOnHoverLockType)`, `GetHoverCardController`. `ExpandOnHoverLock{Type}` (`kForceCollapse`/`kKeepCurrentState`/`kKeepExpanded`) lives here. |
| `BaseTabStripRegionView` | `chrome/browser/ui/views/frame/base_tab_strip_region_view.{h,cc}` | Shared base for the horizontal/vertical region views. |
| `HorizontalTabStripRegionViewOld` / `...New` | `chrome/browser/ui/views/frame/horizontal_tab_strip_region_view.{h,cc}` | Current horizontal containers. `using HorizontalTabStripRegionView = HorizontalTabStripRegionViewOld;` and factory `CreateHorizontalTabStripRegionView(BrowserView*)`. Own the `TabStrip`, `NewTabButton`, `TabStripComboButton`, `TabStripActionContainer`, scroll container. |
| `VerticalTabStripRegionView` | `chrome/browser/ui/views/frame/vertical_tab_strip_region_view.{h,cc}` | The vertical equivalent (see §1). |
| `TabStrip` | `chrome/browser/ui/views/tabs/tab_strip.h` | A `views::View` representing a `TabStripModel`: creates/contains `Tab`s, implements `TabStripModelObserver`, drag-and-drop, hover cards, groups and split slots (`OnSplitCreated/Removed/ContentsChanged`). Contains `TabContainer`, `TabDragContext`. |
| `TabStripModel` | `chrome/browser/ui/tabs/tab_strip_model.{h,cc}` | The source-of-truth tab list: `AddTabAt`, `MoveTab*`, `AddToNewSplit`, `GetActiveTab`, `IsTabPinned`, `SupportsTabGroups`. |
| `BrowserViewLayout` | `chrome/browser/ui/views/frame/layout/browser_view_layout.h` (+ `browser_view_tabbed_layout_impl.{h,cc}`, `browser_view_layout_delegate.h`) | Computes the bounds of the tab strip region, top container, contents and side panel. **Note the move into `frame/layout/`.** |
| `BrowserFrameView` | `chrome/browser/ui/views/frame/browser_frame_view.{h,cc}` (+ platform impls incl. `browser_frame_view_linux.{h,cc}`, `browser_frame_view_linux_native.{h,cc}`) | Window frame (title bar, borders, caption buttons), `GetTopInset()`, `IsFrameCondensed()`, `ShouldHideTopUIInFullscreen()`; created by `chrome::CreateBrowserFrameView(...)`. |
| Layout params | `chrome/browser/ui/views/frame/layout/browser_view_layout_params.{h,cc}` | Geometry passed to layout impls. |

Relevant helper feature: `tabs::kTabStripUnification` — *"Unifies horizontal and vertical tab
strips to share infrastructure."* (currently `FEATURE_DISABLED_BY_DEFAULT`)
(<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/tabs/features.cc>,
<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/flag_descriptions.h>).
This is directly aligned with Koan's "one strip, two orientations" plan.

**Recommended hook:** give `BrowserView` a Koan `TabStripRegionView` subclass via
`CreateHorizontalTabStripRegionView` / the vertical region factory, and drive it from the existing
`TabStripModel`. Do **not** fork `TabStrip`/`TabStripModel`.

---

## 4. Link preview / "peek" (Zen "Glance")

**Status: the upstream desktop link-preview experiment was REMOVED (April 2026). There is no shipped desktop peek/Glance to build on. The closest surviving upstream pieces are tab hover cards and the Lens overlay.**

- Upstream had an experimental **Link-Preview** feature (`chrome://flags/#link-preview`, i.e.
  `blink::features` link-preview / `kLinkPreview`) with trigger variants **Alt+Click, Alt+Hover,
  Long press**, opening a small preview window that could be promoted to a tab. Historical README:
  <https://chromium.googlesource.com/chromium/src/+/main/chrome/browser/preloading/preview/README.md>
  and triggerer source at tag 143.0.7457.2:
  <https://chromium.googlesource.com/chromium/src/+/refs/tags/143.0.7457.2/chrome/renderer/web_link_preview_triggerer_impl.h>.
- **It was deleted from desktop**: commit "LinkPreview: Remove Desktop Link Preview feature"
  removes flags, triggerers, `chrome/browser/preloading/preview/*`,
  `chrome/renderer/web_link_preview_triggerer_impl.*`, and the Blink
  `web_link_preview_triggerer.h`:
  <https://chromium.googlesource.com/chromium/src/+/831a9ebe6002555d0572d611e0cf98ce9df5d731>.
  Press confirmation: <https://onmsft.com/news/google-is-killing-chromes-link-preview-feature-from-desktop/>
  (and <https://www.pcworld.com/article/2426019/google-chromes-hidden-link-preview-feature-is-actually-pretty-useful.html>
  for the original behaviour).
- **Android** keeps a stable "Preview page" (long-press a link), which is unaffected by the
  desktop removal: <https://onmsft.com/news/google-is-killing-chromes-link-preview-feature-from-desktop/>.
- Surviving desktop-adjacent primitives Koan could reuse:
  - **Tab hover cards** — `features::kTabHoverCardImages` and the `TabHoverCardController`
    referenced throughout `TabStrip`
    (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/ui_features.h>,
    <https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/views/tabs/tab_strip.h>).
  - **Lens overlay** — a full-page overlay over `ContentsWebView` inside `MultiContentsView`
    (`lens_overlay_view_`), with `LensOverlayEntryPointController`
    (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/views/frame/multi_contents_view.h>,
    <https://chromium.googlesource.com/chromium/src/+/17bce74f46c734869ad57b8fd5fa14ded7ec1a5c/chrome/browser/ui/browser_window/internal/browser_window_features.cc>).
- There is **no upstream "peek"** (Arc-style) feature. **UNVERIFIED that any current Chromium
  feature matches Zen Glance.**

**Implication:** Glance must be Koan-native. A practical base is a Koan-owned overlay hosting a
`WebContents` (or reusing the Lens-overlay pattern) inside the existing `MultiContentsView`/
contents area, with a hover/modifier trigger.

---

## 5. Immersive / fullscreen toolbar auto-hide on Linux (for compact mode)

**Status: Linux desktop has NO immersive fullscreen controller — Koan must implement toolbar/
sidebar auto-hide itself, reusing only the *patterns* (and non-Linux classes as reference).**

- `chrome/browser/ui/immersive/immersive_mode_controller.h` states verbatim: *"Currently,
  immersive mode is only available for Chrome OS and macOS."* It defines the
  `ImmersiveModeController` interface, `ImmersiveRevealedLock`, and the observer hooks
  (`OnImmersiveRevealStarted/Ended`), plus `GetTopContainerVerticalOffset(top_container_size)` —
  the exact primitive needed to slide top chrome offscreen
  (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/immersive/immersive_mode_controller.h>).
- The factory proves Linux is a stub: `CreateImmersiveModeController(...)` returns
  `ImmersiveModeControllerChromeos` on ChromeOS and `ImmersiveModeControllerMac` on macOS, but
  `ImmersiveModeControllerStub` in the `#else` (i.e. Linux/Windows) branch
  (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/views/frame/immersive_mode_controller_factory_views.cc>).
- Reference implementations to copy from: `immersive_mode_controller_chromeos.{h,cc}` and
  `immersive_mode_controller_mac.{h,mm}` (same directory,
  <https://github.com/chromium/chromium/tree/main/chrome/browser/ui/views/frame>).
- The other auto-hide mechanism is **top-controls slide**, which is Android/ChromeOS-tablet only:
  `TopControlsSlideController` doc says *"implements the Android-like browser top controls …
  sliding behavior when the current tab's page is scrolled by touch gestures … when in tablet
  mode"*, with `SetShownRatio()` / `GetShownRatio()`
  (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/views/frame/top_controls_slide_controller.h>;
  impl `top_controls_slide_controller_chromeos.cc`,
  <https://chromium.googlesource.com/chromium/src/+/7abc1afa76f5243aa0597d5fdca9cc69cf0962c7/chrome/browser/ui/views/frame/top_controls_slide_controller_chromeos.cc>).
- `BrowserView` already has the reparenting plumbing immersive uses: `TopContainerView`,
  `ReparentTopContainerForStartOfImmersive()` / `...ForEndOfImmersive()`, and
  `RevealTopContainerIfNeeded()` (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/views/frame/browser_view.h>).
- `BrowserFrameView` supplies `ShouldHideTopUIInFullscreen()` / `UpdateFullscreenTopUI()` /
  `IsFrameCondensed()` hooks for the frame side
  (<https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/views/frame/browser_frame_view.h>).
- Background on the ChromeOS immersive model:
  <https://www.chromium.org/developers/design-documents/immersive-fullscreen/>.

**Implication for compact mode:** implement a Koan `ImmersiveModeController`-style class (or a
`TopContainerView` slide controller) that hides/shows the `top_container_` and the *vertical*
tab strip. Reuse `ImmersiveRevealedLock` semantics and `GetTopContainerVerticalOffset`; keep the
actual animation in Koan so Linux stays supported.

---

## 6. How Brave structures its Chromium overrides (low-rebase-cost model)

Brave's `brave-core` repo is the reference for a low-rebase-cost Chromium fork. Braille: **almost
all changes live in `src/brave/`; upstream files are touched only through `chromium_src/`
overrides, and hand-written patches are a last resort.**

- Repo layout:
  - `chromium_src/` mirrors upstream paths and **shadows** them at compile time:
    <https://github.com/brave/brave-core/tree/master/chromium_src>. Rules: `#include
    "chrome/browser/profiles/profile.h"` actually includes `src/brave/chromium_src/.../profile.h`
    if it exists, and compiling `.../profile.cc` compiles the override instead.
  - `patches/` holds `.patch` files (one per upstream file, e.g. `chrome-browser-about_flags.cc.patch`)
    generated from edits made in `src/`:
    <https://github.com/brave/brave-core/tree/master/patches>.
  - Docs: <https://github.com/brave/brave-core/blob/master/docs/patching_and_chromium_src.md> and
    the best-practices page
    <https://fossies.org/linux/brave-core/docs/best-practices/chromium-src-overrides.md>.
- Preference order (from the docs): (1) change only inside `src/brave`; (2) `chromium_src`
  override that `#include`s the original (`#include <...>`) and wraps via a `#define` hook, or
  subclass and override; (3) full-file override (only for stubbing/disabling); (4) **patch
  `.patch`** — keep it minimal (one-liners ideal).
- Header patching idiom: add a `BRAVE_<FILE>_H` placeholder as the last `public:` member upstream,
  then define it in the `chromium_src` header to inject friends/members:
  see the `BRAVE_BROWSER_H` example in
  <https://github.com/brave/brave-core/blob/master/docs/patching_and_chromium_src.md>.
- Tooling: `pnpm run apply_patches` / `update_patches`; a `check_chromium_src.py` that flags
  overrides whose target/symbol no longer exists, run during rebases:
  <https://github.com/brave/brave-browser/wiki/Chromium-rebases>. Siso build handlers
  (`redirect_cc`, `chromium_src_inputs`) make the shadowing/dedup work without touching upstream
  BUILD.gn: <https://github.com/brave/brave-core/blob/master/docs/siso_customization.md>.
- Explicit warning from Brave: do **not** copy whole upstream files for changes (the "Electron"
  failure mode) — copy only hooks so rebases stay mechanical.

Koan should mirror this: a `koan/chromium_src/` shadow tree plus a small `patches/` directory and
the same `check_chromium_src` idea.

---

## Recommendations for Koan Phase 1

1. **Build the sidebar on the launched upstream vertical tab strip**, not from scratch: subclass/
   reskin `VerticalTabStripRegionView` + `tabs::VerticalTabStripStateController` (§1); you inherit
   resize, collapse, expand-on-hover and session persistence for free.
2. **Hook via `TabStripRegionView` and the region factory**, not `TabStrip`/`BrowserView` internals:
   add a Koan region subclass selected where `CreateHorizontalTabStripRegionView` / the vertical
   region is constructed (§3), so `browser_view.cc` stays nearly untouched.
3. **Reuse upstream collapse/EOH animation** (`VerticalTabStripCollapseState`,
   `ExpandOnHoverLock`, `VerticalTabStripFocusSwipeController`) for compact-mode sidebar
   auto-hide/swipe; only add the trigger policy (§1, §5).
4. **Write Koan's own toolbar auto-hide for Linux** — upstream immersive is a stub on Linux and
   top-controls-slide is tablet-only; copy the `ImmersiveModeController` +
   `GetTopContainerVerticalOffset` pattern and the `BrowserView` reparenting helpers, kept in
   `koan/` (§5).
5. **Do not spend time on split view** — it is launched, native Views, resizable, drag-to-create
   and session-persisted; consume `MultiContentsView` + `TabStripModel::AddToNewSplit` (§2).
6. **Treat Glance as greenfield**: the only upstream desktop peek was deleted in April 2026;
   plan a Koan overlay over the contents area, optionally modeled on the Lens-overlay pattern and
   tab hover cards (`kTabHoverCardImages`) (§4).
7. **Workspaces and Essentials have no upstream equivalent** — design them as a new Koan tab-
   collection layer over `TabStripModel`; watch `tabs::kTabStripUnification` as the eventual
   one-strip direction (§3). *(UNVERIFIED: no Chromium workspace/Essentials feature was found.)*
8. **Adopt Brave's rebase model now**: `koan/chromium_src/` shadow tree + minimal `patches/`, prefer
   `#define`-hook/subclass over full-file copies, and port a `check_chromium_src`-style validator
   into CI (§6).
9. **Gate everything behind a Koan-owned `base::Feature`** (plus prefs for user state, mirroring
   `prefs::kVerticalTabsEnabled`) so a bad rebase can disable a surface without reverting code (§1).
10. **Verify against a pinned Chromium milestone, not floating `main`**: vertical-tabs and
    split-view flag names changed twice in six months (M145 revert → M146 relaunch → launch);
    pin a milestone and re-run the §1/§2 flag audit each 4-weekly rebase.
