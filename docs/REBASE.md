# Updating Koan to a new Chromium

Chromium ships a milestone every 4 weeks. Koan stays small on purpose so that
moving to a new revision is mechanical:

- All Koan code lives in `src/` (seen by GN as `//koan`).
- Upstream files carry one-to-a-few-line **hook patches**, one file per
  patched upstream file in `patches/`. Each calls into `//koan`, except the
  freezing exemptions in `components/performance_manager` (a `component()`
  shared library, which can't depend on `//koan`), which carry their logic in
  the patch; see the table. A static library linked only into `chrome`, such
  as `components/omnibox/browser`, may call into `//koan` through a
  header-only hooks target; the definition lives in `//koan/browser/ui/views:impl`,
  so a binary that links the library without `//chrome/browser` (e.g.
  `components_unittests`) wouldn't link.
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
| Strip beside the caption buttons; mirrored corners and content clip | `.../frame/layout/browser_view_tabbed_layout_impl.cc` (`CalculateProposedLayout`: toolbar height / caption button width for the vertical strip, the strip's corner views, the content clip insets; `ConfigureTopContainerBackground`) | with Koan's toolbar hidden, the strip still reaches the top and `SetCaptionButtonWidthForLayout` still makes its top row clear the leading caption buttons; the corner views and the top container's corners are still the only leading/trailing shapes beside the strip; `SetTargetContentBounds` still takes left/right clip insets |
| Tabs on the right | `.../frame/layout/browser_view_layout_impl.cc` (`Layout`) | the browser view's and the top container's layouts are still computed as a `ProposedLayout` from `BrowserLayoutParams` with leading/trailing exclusions, then applied (Koan swaps the exclusions and mirrors the result); the separated top container is still laid out from the same params (Koan narrows them by a visible sidebar in compact mode) |
| Sidebar resize side, drag-to-hide | `.../frame/vertical_tab_strip_region_view.cc` (`Layout`, `OnResize`) | the resize area is still positioned by hand in `Layout`; `OnResize` still snaps to collapsed below `kVerticalTabStripCollapseSnapWidth` |
| Content card | `.../frame/multi_contents_view.cc` | single-contents layout, separators and radii code paths unchanged |
| Toolbar overlay (no patch) | `BrowserView::ReparentTopContainerFor{Start,End}OfImmersive` | Koan's compact controller mirrors these; diff them. On macOS, immersive fullscreen still keeps the top container in `BrowserView::overlay_widget()` |
| Overlay view lookup (no patch) | `TopContainerOverlayView` class name in `browser_view.cc` | still a NonClientView child with that metadata name |
| Spaces bottom bar mount (no patch) | `VerticalTabStripRegionView` ctor | content area is still a `FlexLayoutView` child |
| Space header | `chrome/browser/ui/views/tabs/common/tab_strip_view_layout.cc` (`CalculateVerticalLayout`, + `tabs/common` `impl` dep) | the pinned container is still placed before the unpinned one, and `TabStripView` still accepts extra children (Koan adds the header there) |
| Focused group header | `chrome/browser/ui/views/tabs/common/tab_group_view_layout.cc` (`CalculateVerticalLayout`) | a focused group still lays out its header and the Unfocus pill above its tabs (Koan's Space header replaces both) |
| Sidebar header | `chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_top_container.cc` (`CalculateProposedLayout`, `GetPreferredWidth`) | still a hand-written layout of collapse button + combo button, one `views::ProposedLayout` (Koan mirrors it with tabs on the right); `VerticalTabStripTopContainer` is still a direct child of `VerticalTabStripRegionView` |
| Caption buttons | `chrome/browser/ui/views/frame/opaque_browser_frame_view.cc` (`HitTestCaptionButton`) | the frame's caption buttons are still direct children of the frame view (`ImageButton` in the GTK-native frame, `FrameCaptionButton` otherwise; Koan finds them by class name) and hit-testing still goes through `HitTestCaptionButton`. macOS has no such buttons (`BrowserFrameViewMac`; AppKit draws the traffic lights) |
| URL bar placement (no patch) | `BrowserViewLayoutImpl::Layout` (top container outside BrowserView) | the layout still sets the top container's and toolbar's bounds each pass (Koan observes them and moves the card over the pill or centres it); `LocationBarView` is still a direct child of `ToolbarView`. The layout still clips the top container and toolbar to their size at the end of each pass (crbug.com/390669712): Koan invalidates BrowserView's layout when it resizes the card outside a pass |
| URL bar: float on typing | `chrome/browser/ui/views/location_bar/location_bar_view.cc` (`OnChanged`, + its `BUILD.gn`: `//koan/browser/ui/views`) | still called on every omnibox text change, with `user_input_in_progress()` telling typing from reverts |
| URL bar: new tab from the bar | `chrome/browser/ui/omnibox/chrome_omnibox_client.cc` (`OnAutocompleteAccept`, + its `BUILD.gn`: `//koan/browser/ui/views`) | still the one place an omnibox match's disposition is used to navigate (`set_navigation_params` + `chrome::OpenCurrentURL`); `OmniboxEditModel::OpenMatch` still reverts the omnibox (`RevertAll`) before calling it, except for `NEW_BACKGROUND_TAB` |
| URL bar: New Tab command | `chrome/browser/ui/browser_command_controller.cc` (`IDC_NEW_TAB` in `ExecuteCommandWithDisposition`, + `chrome/browser/ui/BUILD.gn` `browser_command_controller_impl`: `//koan/browser/ui/views`) | ⌘T on macOS, the menus' New Tab and the Dock menu still run `IDC_NEW_TAB` through here. On macOS ⌘T is a reserved command that runs before any views accelerator (`CommandDispatcher` → `BrowserNativeWidgetMac::ExecuteCommand`), so only this hook sees it there. Callers of `chrome::NewTab()` itself (e.g. `CreateNewTabGroup`) are not affected |
| New-tab button (no patch) | `BrowserActions` `kActionNewTab` | the tab strips' new-tab button still invokes `kActionNewTab`, whose callback calls `chrome::NewTab` directly; Koan swaps the callback per window. On Linux and Windows Ctrl+T is also a Koan accelerator: a reserved key there still reaches the FocusManager (`BrowserView::PreHandleKeyboardEvent`) before the command runs |
| Command palette provider | `components/omnibox/browser/autocomplete_controller.cc` (constructor, + its `BUILD.gn`: `//koan/browser/omnibox:hooks`) | providers are still built in the constructor into `providers_`; `AutocompleteProvider::Type::kBuiltin` still marks the omnibox-like controllers; `kPedal` matches with a `takeover_action` still run the action instead of navigating (`OmniboxEditModel::OpenMatch`), and `shortcut_boosted` still sorts a match above searches (`AutocompleteMatch::GetSortingOrder`) |
| Glance trigger | `chrome/browser/ui/browser_web_contents_delegate/...cc` (`OpenURLFromTab`) | the activation click still arrives as renderer-initiated `NEW_POPUP`; a click Glance declines is reopened through `OpenURLFromTab` with its fallback disposition (no loop: the fallback is never `NEW_POPUP`) |
| Renderer switches | `chrome/browser/chrome_content_browser_client.cc` (`AppendExtraCommandLineSwitches`, + `chrome/browser/BUILD.gn` `core`) | the renderer branch still has the `RenderProcessHost` for `child_process_id` |
| Glance activation | `third_party/blink/renderer/core/loader/navigation_policy.cc` | `NavigationPolicyFromEventModifiers` still maps modifier clicks to policies; the popup policy is still never produced by link clicks |
| Link context menu | `chrome/browser/renderer_context_menu/render_view_context_menu.cc` (+ its `BUILD.gn`) | `AppendLinkItems()`'s split-view block unchanged; `ExecuteCommand` still records metrics for every id first (Koan's id 55000 runs before); id 55000 still unused in `chrome_command_ids.h` |
| Tab context menu | `chrome/browser/ui/tabs/tab_menu_model.cc` (+ `.h` member), `tab_strip_model.cc` (+ `ui/tabs/BUILD.gn`) | one call, `koan::AddZenTabMenuItems()`, after all of upstream's split view and tab group items, which it finds by command id: split items still `CommandAddToSplit` / `CommandArrangeSplit`, tab group items still `CommandToggleFocusGroup` / `CommandAddToExistingGroup` / `CommandAddToNewGroup` / `CommandRemoveFromGroup` (Spaces swap them for Move To); unknown commands still reach `TabStripModel::IsContextMenuCommandEnabled` / `ExecuteContextMenuCommand` (Koan's "Split Out Tab" is 3001) |
| Sidebar tab rows | `chrome/browser/ui/views/tabs/common/tab_strip_view_layout.cc` (`CalculateVerticalLayout`), `tab_strip_view.cc` (`IsPositionInWindowCaption`) | the vertical layout still places pinned container, separator, unpinned container top to bottom with a running `y`; Koan's rows view is a direct `TabStripView` child |
| Tab and tile sizes | `chrome/browser/ui/layout_constants.cc` | the `kVerticalTab*` constants still size vertical tab rows, pinned tiles and their radius |
| Close button on hover, favicon gap | `.../tabs/common/tab_view_vertical_layout.cc` (`IsChildVisible`, `OnInstalled`) | the close button's last branch still decides "active or hovered"; the icon's `padding` in `tab_children_configs_` is still the gap after the favicon (applied after the icon), so `koan::SidebarTabIconGap` sets the favicon-to-title gap |
| Tab rows and Essentials tiles | `.../tabs/common/vertical_tab_style_views.cc` (`PaintTab`), `pinned_tab_container_view.cc` (`CalculateProposedLayout`) | vertical tabs still paint their background through `PaintTab`; the pinned grid still computes `children_on_row` |
| Sidebar width limits | `chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc` (ctor, `GetMinimumSize`, the resize handler) | the saved width is still read through the state controller in the ctor (Koan clamps it to the active limits); the min/max and collapse snap width are applied in `GetMinimumSize` and the resize handler. Upstream also reads the `vertical_tab_strip_state.h` constants, unhooked, in `browser_view_tabbed_layout_impl.cc` (window minimum width), `tab_view.cc` (`UncollapsedMinWidth`), `vertical_tab_strip_top_container.cc` (combo button switch), `tab_view_vertical_layout.cc` (fallback width) and, on macOS, `browser_native_widget_mac.mm` (`GetGlassFrameWidth` caps the glass frame at 400px, so with Chromium's GlassFrame feature on a Koan sidebar wider than 400px outgrows it; the feature is off in Koan). Recheck that none of these starts depending on the limits |
| Sidebar rows lookup (no patch) | `VerticalTabStripRegionView`, `BrowserView::OnVerticalTabStripModeChanged` | the region outlives its `TabStripView` (rebuilt on a vertical/horizontal switch, after which Koan re-adds the rows); the region's content area still holds a `FrameSeparator` (hidden by Koan) and a `VerticalTabStripBottomContainer` with a `NewTabButton` (hidden), found by class name |
| Colour mixer | `chrome/browser/ui/color/chrome_color_mixers.cc` | Koan mixer still runs after Chrome's, before custom themes |
| Feature defaults | `chrome/browser/chrome_browser_field_trials.cc` | `RegisterFeatureOverrides` still exists; `kTabGroupsFocusing` still the focus-mode switch; `kInfiniteTabsFreezing` still exists (Koan turns it on; an upstream launch or removal needs `docs/PERFORMANCE.md` updated) |
| Power defaults | `chrome/browser/prefs/browser_prefs.cc` (`RegisterLocalState`) | Koan's call still comes after `performance_manager::user_tuning::prefs::RegisterLocalStatePrefs` (it changes defaults of prefs registered there) |
| Freezing exemptions | `components/performance_manager/freezing/freezing_policy.{h,cc}`, `.../public/freezing/cannot_freeze_reason.h`, `.../freezing/cannot_freeze_reason.cc` | **Not a call into `//koan`** (a component can't depend on `//koan/browser`): these four patches carry logic (two enum values, a timer, ~30 lines) and must be re-ported by hand when they conflict. Check that `CannotFreezeReasonsForType()` still decides which reasons block which freezing type, that `FreezingPolicy` is still a `PageLiveStateObserver` (pinned state) and still sees `OnIsAudibleChanged` / `OnMainFrameDocumentChanged`, and that `CannotFreezeReason` is still not recorded in a histogram, UKM or mojom enum (Koan's values, appended at the end, would be out of range) |
| Restore tabs on demand | `chrome/browser/performance_manager/policies/background_tab_loading_policy.cc` (ctor) | `min_tabs_to_load_` / `max_tabs_to_load_` still gate restored-tab loads in `ShouldLoad()`, and a restored tab still loads when shown |
| Prefs | `chrome/browser/ui/tabs/tab_strip_prefs.cc` | vertical-tabs pref name and registration |
| Startup, infobar, paths, desktop id | `session_startup_pref.cc`, `infobar_utils.cc`, `chrome_paths_linux.cc`, `chrome_paths_mac.mm`, `channel_info_posix.cc`, `shell_integration_linux.cc` | one-line value swaps |
| Flags | `chrome/browser/about_flags.cc` | `kFeatureEntries` end marker |
| macOS keychain item | `components/os_crypt/common/keychain_password_mac.mm` | `kDefaultServiceName`/`kDefaultAccountName` still name the Safe Storage item, and nothing else hard-codes "Chromium Safe Storage" on macOS. Never rename it once profiles exist |
| Settings page | `chrome/browser/ui/webui/chrome_web_ui_configs.cc` | `RegisterChromeWebUIConfigs` still adds `WebUIConfig`s to the `WebUIConfigMap`; `WebUIDataSource::SetRequestFilter` still serves any path (the page's files are embedded, no .pak) |
| Tab selection on close | `chrome/browser/ui/tabs/tab_strip_model.cc` (`DetermineNewSelectedIndex`) | the opener preferences still come first and `is_eligible` still covers collapsed and focus-mode tabs; Koan's most-recently-used pick goes right after them |
| No "Unfocused tab group" toast | `chrome/browser/ui/tabs/tab_strip_model.cc` (`SetSelection`) | the toast is still shown when a selection change leaves focus mode (Koan's Space switches do) |
| Spaces aren't saved groups | `chrome/browser/ui/tabs/saved_tab_groups/saved_tab_group_model_listener.cc` (`OnTabGroupAdded`, + `saved_tab_groups` `impl` dep) | a new local group still becomes a saved group only here; tab restore still restores unsaved groups' tabs through `chrome::AddRestoredTab` (on demand) |
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
