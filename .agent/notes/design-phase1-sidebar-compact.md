# Koan Phase 1 design: Zen sidebar + compact mode

Based on the checkout at Chromium 158.0.8111.0 (main @ 67e29b8). All
`file:line` references are relative to `chromium/src` at that revision.

## 0. Ground rules

- **Never change global compile flags or include paths.** Anything in
  `build/config/BUILDCONFIG.gn` `default_compiler_configs`
  (`build/config/BUILDCONFIG.gn:367`) or
  `//build/config/compiler:default_include_dirs`
  (`build/config/compiler/BUILD.gn:301`) changes the command line of every
  object file and forces a full 10–15 h rebuild on this machine. This rules
  out Brave's `chromium_src` header shadowing for now (see §1.3).
- Upstream edits are **hook patches**: a 1–3 line call into `//koan` code.
  Everything substantial lives in `//koan`.
- Every behaviour is behind a Koan `base::Feature`, so a bad rebase can be
  neutralised from chrome://flags without reverting code.
- Patches are applied only when no build is running.

## 1. Source layout and build integration

### 1.1 Layout

```
~/work/koan/                      (Koan git repo)
├── src/                          Koan C++ sources → symlinked as chromium/src/koan
│   ├── BUILD.gn
│   ├── browser/
│   │   ├── koan_features.{h,cc}
│   │   ├── koan_flag_entries.h
│   │   ├── koan_prefs.{h,cc}
│   │   └── ui/views/
│   │       ├── BUILD.gn
│   │       ├── koan_browser_view_hooks.{h,cc}
│   │       ├── koan_sidebar_style.{h,cc}
│   │       └── koan_compact_mode_controller.{h,cc}
├── patches/                      one .patch per upstream file
└── scripts/apply_patches.sh      git apply / --reverse helper with check mode
```

`chromium/src/koan` is a symlink to `~/work/koan/src`, so Koan code is
versioned in the Koan repo while GN sees it as `//koan`. Because `//` is
already in `default_include_dirs` (`build/config/compiler/BUILD.gn:303`),
`#include "koan/browser/koan_features.h"` works from any upstream file with
no build-config change.

### 1.2 Linking `//koan` into `chrome`

Upstream views subdirectories follow a header target + `:impl` pattern, e.g.
`chrome/browser/ui/views/tabs/vertical/BUILD.gn` (`source_set("vertical")`
with headers, `source_set("impl")` with `.cc` files). Both are pulled into
`source_set("browser")` (`chrome/browser/BUILD.gn:2215`) inside its
`if (toolkit_views)` block (`chrome/browser/BUILD.gn:4245`), at
`chrome/browser/BUILD.gn:4579-4580`.

Koan mirrors this:

- `//koan/browser:features` — `koan_features.{h,cc}`, deps `//base` only, so
  any upstream target can depend on it cheaply.
- `//koan/browser/ui/views` (headers) and `//koan/browser/ui/views:impl`
  (sources), with the same deps style as `tabs/vertical:impl`.
- **Patch `chrome/browser/BUILD.gn`** (+3 lines) next to line 4580:
  `"//koan/browser:features"`, `"//koan/browser/ui/views"`,
  `"//koan/browser/ui/views:impl"`.

Upstream targets that call a Koan hook add the matching Koan header target to
their own `deps` (one line in that target's BUILD.gn). `gn check` enforces
this, so keep hook headers in the header-only targets.

### 1.3 Why no `chromium_src` (yet)

Brave shadows headers by putting `//brave/chromium_src` ahead of `//` in the
include path, and redirects `.cc` compiles through a custom compiler wrapper
(`redirect_cc`). Both change every compile command. For Koan the cost/benefit
is wrong at this stage: our patch set is small (§5), and the global change
means a full rebuild. Revisit if the patch count grows past ~30 files; if
adopted then, scope the include dir to the `//chrome/browser/ui/*` targets
only, not `default_compiler_configs`.

## 2. Feature flags

`koan/browser/koan_features.{h,cc}`, namespace `koan::features`:

| Feature | Default | Controls |
|---|---|---|
| `kKoanSidebar` | enabled | Vertical tabs on by default + Zen sidebar styling |
| `kKoanCompactMode` | enabled | Compact mode available (toggle via shortcut/menu) |

Registration in chrome://flags: `kFeatureEntries` is at
`chrome/browser/about_flags.cc:5417`; its end marker
`// Add new entries above this line.` is at
`chrome/browser/about_flags.cc:14513`.

- `koan/browser/koan_flag_entries.h` defines a `KOAN_FEATURE_ENTRIES` macro
  expanding to the Koan `FeatureEntry` initialisers, with names and
  descriptions as string literals in that header (no edits to
  `flag_descriptions.{h,cc}`).
- `about_flags.cc` is compiled in `source_set("core")`
  (`chrome/browser/BUILD.gn:999`), not `:browser`, so `:core` gets the
  `//koan/browser:flag_entries` dep (as built in step 1).
- **Patch `about_flags.cc`** (+2 lines): one `#include` and one
  `KOAN_FEATURE_ENTRIES` line just above the end marker.
- The flag-metadata unit tests (`about_flags_unittest.cc`,
  `flag-metadata.json`, `enums.xml`) will complain about unknown entries.
  Those are test-only; add Koan entries to `chrome/browser/flag-metadata.json`
  later (Phase 4) if we run those tests.

## 3. Sidebar

### 3.1 How vertical tabs are switched on today

- Pref `prefs::kVerticalTabsEnabled` (`"vertical_tabs.enabled"`,
  `chrome/common/pref_names.h:1274`) is registered **default false** at
  `chrome/browser/ui/tabs/tab_strip_prefs.cc:28`.
- `VerticalTabStripStateControllerImpl` caches it
  (`chrome/browser/ui/tabs/vertical_tab_strip_state_controller_impl.cc:61-64`)
  and `ShouldDisplayVerticalTabs()` returns it (`…_impl.cc:163`).
- `BrowserView::ShouldDrawVerticalTabStrip()`
  (`chrome/browser/ui/views/frame/browser_view.cc:1470`) additionally requires
  a normal (non-app, non-popup) browser window.
- Default expanded width is `kVerticalTabStripDefaultUncollapsedWidth = 240`
  (`chrome/browser/ui/tabs/vertical_tab_strip_state.h:10`), which matches
  Zen's default sidebar width, so no change is needed there.

**Koan change — patch `tab_strip_prefs.cc:28`** (1 line): register the default
as `koan::features::IsSidebarEnabled()` instead of `false`. Profile prefs are
registered after `FeatureList` initialisation, so the feature state is
available. Users who explicitly switch to horizontal tabs keep their choice,
since a user-set pref value overrides the default.

### 3.2 How the vertical strip is built and laid out

- `BrowserView` constructor builds both strips: the horizontal region at
  `browser_view.cc:986`, then, if a `VerticalTabStripStateController` exists,
  a `VerticalTabStripRegionView` at `browser_view.cc:991-1004`, plus the
  floating top/bottom corner views right after it. On glass-frame builds it
  also adds a blur backdrop.
- Layout is `BrowserViewTabbedLayoutImpl`
  (`chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.{h,cc}`,
  ~2000 lines). Its `HorizontalLayout` struct (`…_impl.cc:122-145`) splits the
  window width between vertical strip, toolbar and side panels;
  `CalculateProposedLayout` sizes the strip at `…_impl.cc:451-497` (preferred
  vs. minimum width, collapsed state), and
  `GetVerticalTabStripContentOverlap()` (`…_impl.cc:382`) handles the strip
  overlapping contents during expand-on-hover.

**Mismatch with `.agent/notes/research-upstream-chromium.md`:** it describes the layout file as
`browser_view_layout.h` with a tabbed impl. In this checkout the tabbed logic
lives in `browser_view_tabbed_layout_impl.cc`, which is shared with
`browser_view_layout_impl.cc` and the popup/app variants. Hooks go into the
tabbed impl only.

Today the toolbar sits **to the right of** the vertical strip, at the top of
the content column. That is already close to Zen's "multiple toolbars" mode.

### 3.3 Zen styling plan

Phase 1 delivers Zen's look with the toolbar still in the content column.
Moving the URL bar into the sidebar (Zen "single toolbar") is Phase 1b,
because it reparents `ToolbarView` out of `top_container_` and touches far
more layout code.

| Zen trait | Mechanism | Where |
|---|---|---|
| Vertical tabs by default | pref default (§3.1) | patch `tab_strip_prefs.cc` |
| Content area as a floating rounded card with a margin around it | contents inset + corner radius via `views::ShapeContextTokens::kContentSeparatorRadius`, which upstream already uses for the floating corners (`browser_view.cc:1006-1012`) | Koan `KoanSidebarStyle`, called from one hook in `DoPostLayoutVisualAdjustments` (`…_tabbed_layout_impl.h:47`) |
| Sidebar blends into the window frame (no separator line or shadow) | colour mixer override for `kColorVerticalTabStripShadow` (`chrome/browser/ui/color/chrome_color_id.h:1168`, the only vertical-strip colour id) and the frame/toolbar colours the strip paints with | Koan colour mixer, registered from `chrome/browser/ui/color/chrome_color_mixers.cc` (+1-line patch) |
| Fewer buttons in the sidebar header | hide the segmented control and top-container buttons Zen doesn't have | Koan hook in `VerticalTabStripRegionView` init (`vertical_tab_strip_region_view.cc`) |

**Subclass vs. patch:** `VerticalTabStripRegionView` is constructed directly
with `std::make_unique` in `BrowserView`, and its children are private. A
subclass would still need a `browser_view.cc` patch to construct it and would
break on every refactor of the private members. Prefer **post-construction
styling hooks**: a single `koan::OnBrowserViewCreated(BrowserView*)` call at
the end of the `BrowserView` constructor, which walks the known child views
by element identifier (`views::kElementIdentifierKey`, as upstream uses at
`browser_view.cc:1013-1015`) and adjusts them. Element identifiers are far
more stable across rebases than private member layouts.

## 4. Compact mode on Linux

### 4.1 Why not implement `ImmersiveModeController`

`chrome::CreateImmersiveModeController`
(`chrome/browser/ui/views/frame/immersive_mode_controller_factory_views.cc:20-35`)
returns `ImmersiveModeControllerStub` on Linux (line 34). It is created per
window from `browser_window_features.cc:418`. On ChromeOS it is only enabled
from `BrowserView::FullscreenStateChanged()` (`browser_view.cc:2279-2285`),
and the layout delegate only consults it on macOS
(`browser_view_layout_delegate_impl.cc:263-286`).

So immersive mode is tied to fullscreen and to ChromeOS/macOS-specific frame
code. Zen's compact mode works in a normal window. Implementing a Linux
`ImmersiveModeController` would inherit fullscreen assumptions (top-container
reparenting, find-bar offsets, infobar offsets) that we don't want.

### 4.2 Design: `KoanCompactModeController`

A per-window controller owned by Koan, created from the
`OnBrowserViewCreated` hook, with three states: `kOff`, `kHidden`,
`kRevealed`.

**Sidebar.** Reuse upstream's expand-on-hover machinery. The vertical strip
already supports a collapsed state that expands over the contents on hover
(`VerticalTabStripCollapseState`, `GetVerticalTabStripContentOverlap()` at
`…_tabbed_layout_impl.cc:382`, and `ExpandOnHoverLock` in
`tab_strip_region_view.h`). Compact mode makes the collapsed width **0**
instead of the icon-only width, so:

- hidden: the strip takes no layout space and contents fill the window;
- revealed: the existing expand-on-hover path draws the strip as an overlay
  over the contents, with no relayout of the web contents (important for
  smoothness: resizing a `WebContents` triggers a page relayout every frame).

Hook (in `CalculateProposedLayout`, at the collapsed-width calculation,
`…_impl.cc:461-479`): `if (koan::CompactModeHidesSidebar(browser_view)) min = preferred = 0;`.
The reveal trigger is a 4–6 px hot zone at the window's leading edge,
implemented as a transparent Koan view with `OnMouseEntered`, which takes an
`ExpandOnHoverLock(kKeepExpanded)` while the pointer is inside the strip.

**Toolbar.** The top container (`top_container_`, `browser_view.cc:951`) is
offset rather than resized:

- hidden: layout gives the top container height 0 for contents positioning,
  but the view keeps its natural height and is painted as an overlay above
  the contents;
- reveal/hide is a transform on the top container's layer
  (`SetPaintToLayer()`), animated with
  `views::AnimationBuilder` (`ui/views/animation/animation_builder.h`):
  `.Once().SetDuration(150ms).SetTransform(top_container, translate(0, -h), gfx::Tween::EASE_OUT)`.
  Transforms run on the compositor thread (`ui/compositor/layer_animator.h`),
  so the slide stays at 60+ fps even if the UI thread is busy. This is
  where Zen-level smoothness comes from.
- trigger: a hot zone at the top edge, plus "reveal while the omnibox has
  focus" (ask `LocationBarView` focus state), and a short hide delay
  (`base::OneShotTimer`, ~300 ms) to avoid flicker.

Hook (in `CalculateProposedLayout`, where the top container's bounds are
computed): `if (koan::CompactModeHidesToolbar(browser_view)) { contents_top = 0; }`.

**Toggle.** (Corrected later: Zen's default is Ctrl+S, see docs/USAGE.md; Koan shortcuts now live in KoanShortcutController, no accelerator-table patch.) Originally planned as Ctrl+Alt+C. Register a Koan command id through
the accelerator table (`chrome/browser/ui/accelerator_table.cc`, +1
line) and handle it in `koan::ExecuteKoanCommand`, called from
`BrowserCommandController` (+3 lines). Persist the state in a Koan profile
pref `koan.compact_mode.enabled`.

### 4.3 Animation APIs available

- `views::AnimationBuilder` (`ui/views/animation/animation_builder.h`):
  declarative layer animations (opacity, transform, bounds) with tweens.
- `gfx::SlideAnimation` (`ui/gfx/animation/slide_animation.h`): main-thread
  0→1 animation for anything that must re-run layout per frame.
- `gfx::Tween` (`ui/gfx/animation/tween.h`): easing curves
  (`EASE_OUT`, `FAST_OUT_SLOW_IN`, etc.).
- `ui::LayerAnimator` (`ui/compositor/layer_animator.h`): the compositor
  animator behind `AnimationBuilder`.

Rule: anything that moves on screen uses layer transforms (compositor
thread). Use `SlideAnimation` only where layout truly must change per frame.

## 5. Upstream patches (ordered by rebase risk, highest first)

| # | File | Lines | Risk | Why |
|---|---|---|---|---|
| 1 | `chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc` | ~8 | High | 2000-line file under active development; compact-mode hooks in `CalculateProposedLayout` |
| 2 | `chrome/browser/ui/views/frame/browser_view.cc` | ~3 | Medium | `OnBrowserViewCreated` / destroy hooks |
| 3 | `chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc` | ~3 | Medium | header-button visibility hook |
| 4 | `chrome/browser/ui/color/chrome_color_mixers.cc` | ~2 | Low | register the Koan colour mixer |
| 5 | `chrome/browser/ui/browser_command_controller.cc` | ~3 | Low | Koan command dispatch |
| 6 | `chrome/browser/ui/accelerator_table.cc` | ~1 | Low | Ctrl+Alt+C |
| 7 | `chrome/browser/ui/tabs/tab_strip_prefs.cc` | ~2 | Low | vertical tabs default |
| 8 | `chrome/browser/about_flags.cc` | ~2 | Low | flag entries |
| 9 | `chrome/browser/BUILD.gn` + BUILD.gn deps of patched targets | ~6 | Low | link `//koan` |

About 30 changed lines across 9–12 files. The low-risk patches touch append-only
lists or single expressions, and the high-risk ones are confined to two files
that we re-check on every rebase.

## 6. Implementation order

Each step builds and can be tested on its own. Start only after the first full
build has finished.

1. **Skeleton.** `src/` tree, `chromium/src/koan` symlink,
   `scripts/apply_patches.sh`, `//koan/browser:features`, patches 9 and 8.
   Test: build `chrome`; `chrome://flags` shows the two Koan flags.
2. **Vertical tabs by default.** Patch 7. Test: fresh profile
   (`--user-data-dir=/tmp/koan-p1`) opens with vertical tabs; the flag set to
   Disabled gives horizontal tabs.
3. **Hook plumbing.** `koan_browser_view_hooks`, patch 2, no visible change.
   Test: a log line per window, no crashes opening/closing windows.
   *Done:* `OnBrowserViewCreated` runs at the end of the constructor (no
   widget yet; the controller observes `OnViewAddedToWidget`),
   `OnBrowserViewDestroying` first thing in the destructor. Per-window state
   is a `KoanBrowserViewController` owned by the BrowserView through an owned
   `ui::ClassProperty`, so there is no global map. `:impl` is linked from
   `chrome/browser:browser`'s toolkit_views deps; `frame:browser_view` depends
   only on the header target.
4. **Zen styling.** Colour mixer (patch 4), header-button hook (patch 3),
   rounded floating content card. Test: screenshots against Zen, light and
   dark themes.
5. **Compact mode: sidebar.** Patch 1 (sidebar half), leading-edge hot zone,
   expand-on-hover lock. Test: toggle on, hover left edge reveals the sidebar
   as an overlay, page does not reflow.
6. **Compact mode: toolbar.** Patch 1 (toolbar half), layer-transform slide,
   omnibox-focus reveal. Test: Ctrl+L reveals and focuses the URL bar; hiding
   is smooth.
7. **Toggle and persistence.** Patches 5 and 6, Koan pref. Test: Ctrl+Alt+C
   toggles; state survives restart.
8. **Phase 1b (separate design).** Single-toolbar mode: URL bar and
   navigation buttons moved into the sidebar header.

Incremental rebuilds after each step only recompile the touched files and
relink: minutes, not hours (component build).

## 7. Open issues (from review)

- **Views anchored below the toolbar in compact mode** (raised by DeepSeek V4.1
  Flash). With `contents_top = 0` and the top container drawn as an overlay,
  the infobar container, bookmark bar and find bar lose their anchor. Decide
  per view before step 6: bookmark bar hides with the toolbar; infobars and
  the find bar anchor to the contents top while hidden and to the toolbar
  bottom while revealed.
- **Hit-testing of the hidden toolbar** (raised by GLM 5.3 Flash). A layer
  transform animated through `AnimationBuilder` moves pixels but not the
  view's bounds, so a "hidden" toolbar can still receive clicks and hovers
  over the page. While hidden, the top container must be
  `SetCanProcessEventsWithinSubtree(false)` (or `SetVisible(false)` after the
  hide animation ends), with the hot zone as the only event target.
- **Edge hot zone vs. upstream hover trigger.** The 4–6 px hot-zone view is
  separate from upstream's expand-on-hover trigger and could swallow
  clicks at the window edge (e.g. on scrollbars of RTL pages). Prototype
  both, and prefer extending the upstream trigger if it can fire at width 0.
