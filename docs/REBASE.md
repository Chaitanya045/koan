# Updating Koan to a new Chromium

Chromium ships a milestone every 4 weeks. Koan stays small on purpose so that
moving to a new revision is mechanical:

- All Koan code lives in `src/` (seen by GN as `//koan`).
- Upstream files carry one-to-a-few-line **hook patches**, one file per
  patched upstream file in `patches/`. Each calls into `//koan`.
- Bulk branding (product name strings, `BRANDING`, logos) is applied by
  `scripts/apply_branding.sh` rather than stored as patches.
- Upstream feature defaults Koan relies on are set in one place,
  `src/browser/koan_feature_overrides.cc`.

## Procedure

```sh
scripts/update_chromium.sh --check          # will today's patches apply?
scripts/update_chromium.sh 159.0.8150.0     # un-brand, un-patch, sync, re-apply
scripts/build.sh                            # most of the tree rebuilds
```

`update_chromium.sh` stops if a patch conflicts. Port that hook by hand in
`chromium/src`, then `scripts/apply_patches.sh export` and continue with
`scripts/apply_branding.sh`.

## After every rebase, re-audit the hooks

`update_chromium.sh` prints every `koan::` call site in `chromium/src`.
For each, check the surrounding upstream code still means what the hook
assumes:

| Hook | File | Assumption to re-check |
|---|---|---|
| BrowserView created/destroyed | `chrome/browser/ui/views/frame/browser_view.cc` | end of ctor: children exist, no widget yet; start of dtor: children alive |
| Compact sidebar width | `.../frame/layout/browser_view_tabbed_layout_impl.cc` (`CalculateHorizontalLayout`) | collapsed branch still sizes the strip; expand-on-hover still overlays |
| Content card | `.../frame/multi_contents_view.cc` | single-contents layout, separators and radii code paths unchanged |
| Toolbar overlay (no patch) | `BrowserView::ReparentTopContainerFor{Start,End}OfImmersive` | Koan's compact controller mirrors these; diff them. On macOS, immersive fullscreen still keeps the top container in `BrowserView::overlay_widget()` |
| Overlay view lookup (no patch) | `TopContainerOverlayView` class name in `browser_view.cc` | still a NonClientView child with that metadata name |
| Workspace switcher mount (no patch) | `VerticalTabStripRegionView` ctor | content area is still a `FlexLayoutView` child |
| Sidebar header | `chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_top_container.cc` (`CalculateProposedLayout`, `GetPreferredWidth`) | still a hand-written layout of collapse button + combo button; `VerticalTabStripTopContainer` is still a direct child of `VerticalTabStripRegionView` |
| Caption buttons | `chrome/browser/ui/views/frame/opaque_browser_frame_view.cc` (`HitTestCaptionButton`) | the frame's caption buttons are still direct children of the frame view (`ImageButton` in the GTK-native frame, `FrameCaptionButton` otherwise; Koan finds them by class name) and hit-testing still goes through `HitTestCaptionButton`. macOS has no such buttons (`BrowserFrameViewMac`; AppKit draws the traffic lights) |
| Floating URL bar (no patch) | `BrowserViewLayoutImpl::Layout` (top container outside BrowserView) | the layout still sets the top container's and toolbar's bounds each pass (Koan observes them and re-centres); `LocationBarView` is still a direct child of `ToolbarView` |
| Glance trigger | `chrome/browser/ui/browser_web_contents_delegate/...cc` | Shift+click still arrives as renderer-initiated `NEW_WINDOW` |
| Sidebar tab rows | `chrome/browser/ui/views/tabs/common/tab_strip_view_layout.cc` (`CalculateVerticalLayout`), `tab_strip_view.cc` (`IsPositionInWindowCaption`) | the vertical layout still places pinned container, separator, unpinned container top to bottom with a running `y`; Koan's rows view is a direct `TabStripView` child |
| Tab and tile sizes | `chrome/browser/ui/layout_constants.cc` | the `kVerticalTab*` constants still size vertical tab rows, pinned tiles and their radius |
| Close button on hover, favicon gap | `.../tabs/common/tab_view_vertical_layout.cc` (`IsChildVisible`, `OnInstalled`) | the close button's last branch still decides "active or hovered"; the icon's `padding` in `tab_children_configs_` is still the gap after the favicon (applied after the icon), so `koan::SidebarTabIconGap` sets the favicon-to-title gap |
| Tab rows and Essentials tiles | `.../tabs/common/vertical_tab_style_views.cc` (`PaintTab`), `pinned_tab_container_view.cc` (`CalculateProposedLayout`) | vertical tabs still paint their background through `PaintTab`; the pinned grid still computes `children_on_row` |
| Sidebar width limits | `chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc` (ctor, `GetMinimumSize`, the resize handler) | the saved width is still read through the state controller in the ctor (Koan clamps it to the active limits); the min/max and collapse snap width are applied in `GetMinimumSize` and the resize handler. Upstream also reads the `vertical_tab_strip_state.h` constants, unhooked, in `browser_view_tabbed_layout_impl.cc` (window minimum width), `tab_view.cc` (`UncollapsedMinWidth`), `vertical_tab_strip_top_container.cc` (combo button switch), `tab_view_vertical_layout.cc` (fallback width) and, on macOS, `browser_native_widget_mac.mm` (`GetGlassFrameWidth` caps the glass frame at 400px, so with Chromium's GlassFrame feature on a Koan sidebar wider than 400px outgrows it; the feature is off in Koan). Recheck that none of these starts depending on the limits |
| Sidebar rows lookup (no patch) | `VerticalTabStripRegionView`, `BrowserView::OnVerticalTabStripModeChanged` | the region outlives its `TabStripView` (rebuilt on a vertical/horizontal switch, after which Koan re-adds the rows); the region's content area still holds a `FrameSeparator` (hidden by Koan) and a `VerticalTabStripBottomContainer` with a `NewTabButton` (hidden), found by class name |
| Colour mixer | `chrome/browser/ui/color/chrome_color_mixers.cc` | Koan mixer still runs after Chrome's, before custom themes |
| Feature defaults | `chrome/browser/chrome_browser_field_trials.cc` | `RegisterFeatureOverrides` still exists; `kTabGroupsFocusing` still the focus-mode switch; `kInfiniteTabsFreezing` still exists (Koan turns it on; an upstream launch or removal needs `docs/PERFORMANCE.md` updated) |
| Power defaults | `chrome/browser/prefs/browser_prefs.cc` (`RegisterLocalState`) | Koan's call still comes after `performance_manager::user_tuning::prefs::RegisterLocalStatePrefs` (it changes defaults of prefs registered there) |
| Freezing exemptions | `components/performance_manager/freezing/freezing_policy.{h,cc}`, `.../public/freezing/cannot_freeze_reason.h`, `.../freezing/cannot_freeze_reason.cc` | `CannotFreezeReasonsForType()` still decides which reasons block which freezing type; `FreezingPolicy` is still a `PageLiveStateObserver` (pinned state) and still sees `OnIsVisibleChanged` / `OnIsAudibleChanged` |
| Restore tabs on demand | `chrome/browser/performance_manager/policies/background_tab_loading_policy.cc` (ctor) | `min_tabs_to_load_` / `max_tabs_to_load_` still gate restored-tab loads in `ShouldLoad()`, and a restored tab still loads when shown |
| Prefs | `chrome/browser/ui/tabs/tab_strip_prefs.cc` | vertical-tabs pref name and registration |
| Startup, infobar, paths, desktop id | `session_startup_pref.cc`, `infobar_utils.cc`, `chrome_paths_linux.cc`, `chrome_paths_mac.mm`, `channel_info_posix.cc`, `shell_integration_linux.cc` | one-line value swaps |
| Flags | `chrome/browser/about_flags.cc` | `kFeatureEntries` end marker |
| macOS keychain item | `components/os_crypt/common/keychain_password_mac.mm` | `kDefaultServiceName`/`kDefaultAccountName` still name the Safe Storage item, and nothing else hard-codes "Chromium Safe Storage" on macOS. Never rename it once profiles exist |
| Settings page | `chrome/browser/ui/webui/chrome_web_ui_configs.cc` | `RegisterChromeWebUIConfigs` still adds `WebUIConfig`s to the `WebUIConfigMap`; `WebUIDataSource::SetRequestFilter` still serves any path (the page's files are embedded, no .pak) |
| Tab selection on close | `chrome/browser/ui/tabs/tab_strip_model.cc` (`DetermineNewSelectedIndex`) | the opener preferences still come first and `is_eligible` still covers collapsed and focus-mode tabs; Koan's most-recently-used pick goes right after them |
| Close tab on Back | `chrome/browser/ui/tabs/back_to_opener/back_to_opener_controller.cc` (`HasValidOpener`, which `CanGoBackToOpener` now calls) | upstream's back-to-opener (`tabs::kBackToOpener`, enabled in `koan_feature_overrides.cc`) still closes the tab and activates its opener; `CanGoBack` / `ShouldEnableBackButton` still ask `CanGoBackToOpener` and the back button's menu (`BackForwardMenuModel`) still asks `HasValidOpener` |
| Ctrl+Tab options (no patch) | `IsReservedCommandOrKey` in `BrowserView::PreHandleKeyboardEvent` | reserved keys still go through `FocusManager::ProcessAccelerator`, so Koan's targets see Ctrl+Tab first; `chrome::IsCtrlTabMruEnabled` still reports Chromium's recently-used Ctrl+Tab (Koan defers to it) |
| Settings page: shortcut capture (no patch) | `BrowserView::PreHandleKeyboardEvent` | still returns early while the FocusManager's shortcut handling is suspended (the editor suspends it while recording a key) |

Upstream unit and browser tests that expect Chromium's defaults fail with
Koan's (they aren't run today): `background_tab_loading_policy_unittest.cc`,
the session-restore browser tests and `tab_restore_browsertest.cc` (restored
tabs now load on demand), and the `freezing_policy` tests that enumerate
`CannotFreezeReason`s or expect infinite-tabs freezing of pinned tabs.

Then run the smoke tests (all off-screen, see `.agent/notes/build.md`):

```sh
scripts/shot_ui.sh /tmp/k.png -- https://example.com            # look
PREFS='{"koan":{"compact_mode":{"enabled":true}}}' \
  scripts/shot_ui.sh /tmp/c.png -- https://example.com           # compact
```

and the interactive ones (`ui_session.sh` + `ui_input.mjs`, `run_profile.sh` +
`cdp_click.mjs`) for workspaces, Glance and session restore.
