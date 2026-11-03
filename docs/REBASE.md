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

## Following Chromium releases

Koan is a daily-driver browser, so it takes Chrome's security fixes, which
ship in Stable point releases about weekly. A trunk snapshot gets none of
them. So the pin is always a **release tag of the milestone we're on**,
never a main commit:

- `CHROMIUM_VERSION` line 1 is the tag (`158.0.8127.2`), line 2 the commit
  it points to. `scripts/bootstrap.sh` (Linux, the Mac CI and a local Mac
  alike) checks out exactly that commit; `scripts/ci/check_patches.sh`
  checks the patches against it.
- A milestone's tags are on its **release branch**, `refs/branch-heads/<BUILD>`
  (158.0.8127.x is `branch-heads/8127`), not on main. Fetch a tag by name:
  `git fetch --depth=1 origin refs/tags/158.0.8127.2`. A branch-head moves
  past its last tag (it collects fixes before they are tagged); don't pin it.
- Where Chromium is: [chromiumdash](https://chromiumdash.appspot.com/releases?platform=Mac)
  lists the Mac channels; the API behind it,
  `https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Mac&num=5`
  (also `Beta`), is what the watcher below reads.

| What happens | Do | Within |
|---|---|---|
| Our milestone gets a **point release** (`158.0.8127.x` -> `158.0.81xx.y`), Beta or Stable | bump the pin ("Point release" below) | a few days |
| A **newer milestone reaches Stable** | rebase onto it ("New milestone" below) | about 2 weeks of it reaching Stable |
| A newer milestone is only Beta or Dev | nothing | |

Until we take a new milestone we stay on our milestone's latest Stable point
release. A Beta of our own milestone is fine as the pin until the milestone
is promoted: its tags are the Stable ones.

### The watcher

`.github/workflows/chromium-releases.yml` (ubuntu, every 6 hours, `contents:
read` and `issues: write` only) polls chromiumdash for Mac Beta and Stable
(`scripts/ci/chromium_releases.py`). When our milestone has a newer point
release, or a newer milestone is at Stable, it runs
`scripts/ci/check_patches.sh` against that version and opens or updates the
one issue labelled `chromium-update`: versions, release dates, the date each
is due, and which patches fail. It closes the issue when the pin has caught
up, and says nothing while nothing changed. Run it by hand from the Actions
tab ("Run workflow", with *dry run* to only print), or locally with
`python3 scripts/ci/chromium_releases.py --dry-run`.

### Point release

Rarely touches the hooks.

```sh
scripts/ci/check_patches.sh 158.0.8140.5     # no checkout needed; what the issue shows
```

1. If every patch is `ok`: change `CHROMIUM_VERSION` to the new tag and its
   commit (`scripts/update_chromium.sh` writes it; or
   `git ls-remote https://chromium.googlesource.com/chromium/src refs/tags/158.0.8140.5`)
   and open a PR. CI checks the patches against it.
2. A `CONFLICT` or `MISSING` (upstream moved or renamed the file): port the
   hook on the PR's branch, as for a new milestone below.
3. Switch the checkout you build in: `scripts/update_chromium.sh 158.0.8140.5`.
   A point release changes few files, but a toolchain roll (`CLANG_REVISION` in
   `tools/clang/scripts/update.py`) rebuilds everything: look first. On
   macOS, the nightly build CI (#69, `macos.yml`, once PR #70 is merged)
   builds whatever `CHROMIUM_VERSION` pins, so it follows on its own; a local
   Mac build runs `scripts/update_chromium.sh` too.

### New milestone

As above, and a milestone rebases more:

1. `scripts/ci/check_patches.sh <tag>`, then port every conflict on a branch
   (below). Open PRs that touch hook patches need the same port: list them
   with `gh pr diff <n> --name-only | grep ^patches/` and check those patch
   files against the tag.
2. Upstream renames code and enums between milestones (M158 split the tab
   layouts into `*_vertical_layout.cc` files and renamed
   `WindowOpenDisposition::NEW_FOREGROUND_TAB` to `kNewForegroundTab`):
   patches that apply can still fail to compile. `grep` `src/` and the `+`
   lines of the patches for what the build breaks on.
3. `scripts/update_chromium.sh <tag>`, build, and the audit and smoke tests
   below.
4. Update the tables and prose here that name moved files.

## Procedure

```sh
scripts/ci/check_patches.sh 159.0.8150.0    # will today's patches apply? (no checkout)
scripts/update_chromium.sh --check          # ... to the checkout's HEAD
scripts/update_chromium.sh 159.0.8150.0     # un-brand, un-patch, fetch the tag, sync, re-apply
scripts/build.sh                            # most of the tree rebuilds
```

`update_chromium.sh` takes a release tag (not a commit or branch), checks the
patches against it before changing anything, and stops if one conflicts. It un-patches by putting back every file any
Koan patch set touched (this branch's, which may be newer than what is applied,
and the list in `$KOAN_PATCHED_FILES`, default `.cache/patched-files`), and
stops if anything else is modified.
Port that hook on a branch first: get the new file
(`curl -s 'https://chromium.googlesource.com/chromium/src/+/<tag>/<path>?format=TEXT' | base64 -d`
for a checkout-less port, or the file in `chromium/src` after the checkout),
make the same small change, and write `patches/<path>.patch`
(`scripts/apply_patches.sh export` from a checkout); renamed upstream files
get a renamed patch. Then `scripts/apply_branding.sh`.

## Shared hooks for Koan features

Several features need the same upstream places. Each is one hook, and
features plug into it in `src/` rather than patching upstream again:

- **Tab context menu**: `src/browser/ui/views/koan_tab_menu.h`. One hook at
  the end of `TabMenuModel::Build()` (`koan::AddKoanTabMenuItems`, with a
  `koan_submenus_` member in `TabMenuModel` that owns features' submenus) and
  one in each of `TabStripModel::IsContextMenuCommandEnabled()` /
  `ExecuteContextMenuCommand()`. A feature adds a `TabMenuFeature` (add its
  items, enabled state, run a command) to `kFeatures` in `koan_tab_menu.cc`
  and gets its own range of 100 command ids (`TabMenuFeatureId`: split view
  3000, pinned tabs 3100, Spaces 3200, folders 3300). Items are found by
  command id, never by position. `TabMenuContext::AddSubmenu()` gives a
  submenu whose items run like the menu's.
- **Session data, per tab and per window**:
  `src/browser/ui/views/koan_session_data.h`. A feature keeps its data under
  its own key ("koan.<feature>"), as Chromium's session extra data:
  - Per tab: `koan::SetTabSessionData(contents, key, data)` /
    `GetTabSessionData(contents, key)`. It comes back after a restart, when
    a closed tab or window is reopened and when a discard replaces the tab's
    WebContents, and follows the tab, not its index (e.g. folder
    membership).
  - Per window: `koan::SetWindowSessionData(browser, key, data)` /
    `GetWindowSessionData(browser, key)`, for state of the window rather
    than a tab (e.g. folders themselves: names, collapsed, empty ones). It
    comes back after a restart and when a closed window is reopened. Kept on
    `KoanBrowserViewController`, so only windows with Koan's UI have it.

  The store keeps a copy: call `Set...()` after every change. A feature that
  acts while a tab or window is restored adds a `TabRestoreHook` (it may
  replace the tab's pages) or `WindowRestoreHook` (the window exists, its
  tabs don't yet) to `kTabRestoreHooks` / `kWindowRestoreHooks` in
  `koan_session_data.cc`. The hooks: `SessionServiceBase::BuildCommandsForTab`
  and `BuildCommandsForBrowser` (the session file's rebuild drops extra data
  otherwise), `CreateRestoredTab` in `browser_tabrestore.cc`,
  `SessionRestoreImpl` in `session_restore.cc` (`CreateRestoredBrowser`, and
  where the first window is restored into the open browser), and
  `BrowserLiveTabContext::GetExtraDataForTab`, `GetExtraDataForWindow` and
  `Create` in `browser_live_tab_context.cc`.
- **"Is this tab pinned?"**: `koan::IsPinnedForUser(tab)` in
  `src/browser/ui/views/koan_pinned.h`: an Essential (Chromium's
  pinned tab) or a tab pinned to its Space, which Chromium doesn't know as
  pinned. Upstream's Close Other Tabs, Close Tabs Below and back-to-opener,
  and Koan's Clear, ask it; any feature that leaves pinned tabs alone (moving
  tabs between Spaces or into folders) should too. The freezing and Memory
  Saver exemptions get these tabs through the page's pinned state, which
  Koan sets for them.

Features that adopt these (Spaces, folders) drop their own copies of the
same files: `koan_tab_menu.{h,cc}`, `koan_session_data.{h,cc}`, and the
`tab_menu_model.{h,cc}`, `tab_strip_model.cc`, `sessions/BUILD.gn` and
`session_service_base.cc` patches, adding their entries in `src/` instead.
Their menu ids move into their `TabMenuFeatureId` range (e.g. folders' "Move
to Folder" from 3002, inside split view's range, to 3300+).

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
| Focused group header | `chrome/browser/ui/views/tabs/common/tab_group_view_vertical_layout.cc` (`CalculateProposedLayout`) | a focused group still lays out its header and the Unfocus pill above its tabs (Koan's Space header replaces both, in windows with Spaces: `koan::ShowsSpaceHeader()` finds the window through the view's widget). Koan hides a Space's group header (visible false, not only empty bounds, so screen readers and the keyboard don't reach upstream's group editor); nothing else sets the header's visibility, so recheck that upstream still doesn't |
| A Space opens on its last tab | `chrome/browser/ui/tabs/tab_strip_model.cc` (`EnterFocusMode`) | `EnterFocusMode` still activates a tab of the group only when the active tab isn't in it, chosen in one place (Koan's `PickSpaceTab()` replaces the first tab); every Space switch (Koan's, upstream's swipe and Shift+Alt+X) still goes through it |
| Spaces wrap around | `chrome/browser/ui/tabs/tab_strip_model.cc` (`RotateFocusedGroup`) | still what upstream's swipe controller calls; past the last or first group it still leaves focus mode (Koan wraps instead, `koan::WrapsSpaces()`) |
| Space swipes and the wheel (no patch) | `chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_focus_swipe_controller.cc` | still a default-priority pre-target handler on `VerticalTabStripRegionView` (Koan's wheel handler is `kSystem`, so it runs first); Windows/Linux still treat wheel events as swipes in `OnMouseEvent`, macOS `ScrollEvent`s in `OnScrollEvent`, a Mac mouse wheel's included: upstream would take a notched wheel as a swipe too, and it doesn't only because Koan's handler (`SpaceWheelHandler`, finding wheels by `ui::GetMouseWheelTick120ths`) runs first and stops the event. Recheck that order if either handler's priority or Koan's `Handle()` changes; `Widget::OnScrollEvent` still turns unhandled scroll events into wheel events |
| Another Space's tab shows its Space (no patch) | `TabStripModel::SetSelection` | activating a tab outside the focused group still leaves focus mode (Koan then focuses that tab's Space, keeping the tab) |
| A closed Space comes back (no patch) | `TabStripModel::AddToGroupForRestore` | still re-creates a group with a given id |
| New tabs in the active Space (no patch) | `chrome/browser/ui/navigator/browser_navigator.cc` (the `GetFocusedGroup()` fallback) | a new tab still joins the focused group |
| Unload Space (no patch) | `DiscardEligibilityPolicy::CanDiscard` (`chrome/browser/performance_manager/policies`) | still lists its `CannotDiscardReason`s for `URGENT` with `ignore_recent_visibility`; a new reason is "in use" for Koan's Unload unless `IsTabInUse()` in `koan_tab_unload.cc` lists it as not |
| Create a Space from a collapsed sidebar (no patch) | `VerticalTabStripRegionView::UpdateExpandOnHoverState` | a `kKeepExpanded` lock still expands a collapsed sidebar over the page, whether or not expand-on-hover is on |
| Space shortcuts first on macOS (no patch) | `chrome/browser/ui/cocoa/chrome_command_dispatcher_delegate.mm` (`prePerformKeyEquivalent`) | high-priority views accelerators (Koan's, on macOS) still run before Chrome's menu shortcuts such as Cmd+Option+arrows |
| Library panel (no patch) | `BrowserView::contents_container()`, `tab_strip_view()` | the contents container and the vertical tab strip region still span the area below the toolbar; the Library covers their union |
| Library: where the sidebar is (no patch) | `koan_library_controller.cc` `GetArea()` | the sidebar is the `VerticalTabStripRegionView` (found by class name), as for the workspace switcher |
| Library: History (no patch) | `HistoryService::QueryHistory` (`begin_time`/`end_time` paging, `REMOVE_DUPLICATES_PER_DAY`, `hostname_suffix`), `DeleteURLs`, `BrowsingDataRemover::Remove` with `chrome_browsing_data_remover::DATA_TYPE_HISTORY` (GN `//chrome/browser/browsing_data:constants`), `RemoveWithFilter`, `prefs::kAllowDeletingBrowserHistory` | the remover's history bit still clears the range and closed tabs; the pref still means what `chrome://history` checks |
| Library: closed tabs (no patch) | `TabRestoreService` (`entries()`, `RestoreEntryById`, `RemoveEntryById`, `LoadTabsFromLastSession`), `BrowserLiveTabContext::From` | entries keep `timestamp` and `normalized_navigation_index()` |
| Library: Downloads (no patch) | `AllDownloadItemNotifier`, `DownloadItem` (danger and insecure state, `SetOpenWhenComplete`), `DownloadUrlParameters` with `DownloadSource::RETRY`, `IconManager`, `DownloadToolbarUIController::OpenSecuritySubpage`, `OfflineItemUtils::GetContentIdForDownload` | Retry still mirrors the download bubble's; the bubble still shows a download's warning by content id |
| Library: reordering Spaces (no patch) | `TabStripModel::MoveGroupTo` (`KoanWorkspaceController::MoveSpace()`) | still takes the group's new first index counted without its own tabs |
| Sidebar header | `chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_top_container.cc` (`CalculateProposedLayout`, `GetPreferredWidth`) | still a hand-written layout of collapse button + combo button, one `views::ProposedLayout` (Koan mirrors it with tabs on the right); `VerticalTabStripTopContainer` is still a direct child of `VerticalTabStripRegionView` |
| Caption buttons | `chrome/browser/ui/views/frame/opaque_browser_frame_view.cc` (`HitTestCaptionButton`) | the frame's caption buttons are still direct children of the frame view (`ImageButton` in the GTK-native frame, `FrameCaptionButton` otherwise; Koan finds them by class name) and hit-testing still goes through `HitTestCaptionButton`. macOS has no such buttons (`BrowserFrameViewMac`; AppKit draws the traffic lights) |
| URL bar placement (no patch) | `BrowserViewLayoutImpl::Layout` (top container outside BrowserView) | the layout still sets the top container's and toolbar's bounds each pass (Koan observes them and moves the card over the pill or centres it); `LocationBarView` is still a direct child of `ToolbarView`. The layout still clips the top container and toolbar to their size at the end of each pass (crbug.com/390669712): Koan invalidates BrowserView's layout when it resizes the card outside a pass |
| URL bar: float on typing | `chrome/browser/ui/views/location_bar/location_bar_view.cc` (`OnChanged`, + its `BUILD.gn`: `//koan/browser/ui/views`) | still called on every omnibox text change, with `user_input_in_progress()` telling typing from reverts |
| URL bar: new tab from the bar | `chrome/browser/ui/omnibox/chrome_omnibox_client.cc` (`OnAutocompleteAccept`, + its `BUILD.gn`: `//koan/browser/ui/views`) | still the one place an omnibox match's disposition is used to navigate (`set_navigation_params` + `chrome::OpenCurrentURL`); `OmniboxEditModel::OpenMatch` still reverts the omnibox (`RevertAll`) before calling it, except for `kNewBackgroundTab` |
| URL bar: New Tab command | `chrome/browser/ui/browser_command_controller.cc` (`IDC_NEW_TAB` in `ExecuteCommandWithDisposition`, + `chrome/browser/ui/BUILD.gn` `browser_command_controller_impl`: `//koan/browser/ui/views`) | ⌘T on macOS, the menus' New Tab, and `AppController`'s File > New Tab when no window is key (`executeCommand:withProfile:`), still run `IDC_NEW_TAB` through here. On macOS ⌘T is a reserved command that runs before any views accelerator (`CommandDispatcher` → `BrowserNativeWidgetMac::ExecuteCommand`), so only this hook sees it there. Callers of `chrome::NewTab()` itself (e.g. `CreateNewTabGroup`) are not affected |
| New-tab button (no patch) | `BrowserActions` `kActionNewTab` | the tab strips' new-tab button still invokes `kActionNewTab`, whose callback calls `chrome::NewTab` directly; Koan swaps the callback per window. On Linux and Windows Ctrl+T is also a Koan accelerator: a reserved key there still reaches the FocusManager (`BrowserView::PreHandleKeyboardEvent`) before the command runs |
| Command palette provider | `components/omnibox/browser/autocomplete_controller.cc` (constructor, + its `BUILD.gn`: `//koan/browser/omnibox:hooks`) | providers are still built in the constructor into `providers_`; `AutocompleteProvider::Type::kBuiltin` still marks the omnibox-like controllers; `kPedal` matches with a `takeover_action` still run the action instead of navigating (`OmniboxEditModel::OpenMatch`), and `shortcut_boosted` still sorts a match above searches (`AutocompleteMatch::GetSortingOrder`) |
| Download started animation | `chrome/browser/ui/views/download/bubble/download_toolbar_ui_controller.cc` (`ShowPendingDownloadStartedAnimation`, + `chrome/browser/ui/views/download/BUILD.gn`: `//koan/browser/ui/views`) | still the one place Chrome's "download started" animation starts for a window; Koan skips it while its own (Zen's) plays |
| Media cards mount (no patch) | `VerticalTabStripRegionView` ctor | as for the Spaces bottom bar: the content area is a `FlexLayoutView` child; the cards go just above `KoanSpaceBarView` |
| Window drag from the page (no patch) | `RenderWidgetHost::AddMouseEventCallback`; `Widget::RunMoveLoop` (macOS), `ui::WmMoveResizeHandler` (Linux) | callbacks still see every mouse event forwarded to the page and can swallow it; on macOS `NativeWidgetMac::RunMoveLoop` still moves the window with the pointer until the button goes up; on Linux `DispatchHostWindowDragMovement(HTCAPTION, ...)` still starts a native move, as `WindowEventFilterLinux` does for the title bar |
| Glance trigger | `chrome/browser/ui/browser_web_contents_delegate/...cc` (`OpenURLFromTab`) | the activation click still arrives as renderer-initiated `kNewPopup` (the `WindowOpenDisposition` values were `NEW_POPUP` etc. before M158); a click Glance declines is reopened through `OpenURLFromTab` with its fallback disposition (no loop: the fallback is never `kNewPopup`) |
| Renderer switches | `chrome/browser/chrome_content_browser_client.cc` (`AppendExtraCommandLineSwitches`, + `chrome/browser/BUILD.gn` `core`) | the renderer branch still has the `RenderProcessHost` for `child_process_id` |
| Glance activation | `third_party/blink/renderer/core/loader/navigation_policy.cc` | `NavigationPolicyFromEventModifiers` still maps modifier clicks to policies; the popup policy is still never produced by link clicks |
| Link context menu | `chrome/browser/renderer_context_menu/render_view_context_menu.cc` (+ its `BUILD.gn`) | `AppendLinkItems()`'s split-view block unchanged; `ExecuteCommand` still records metrics for every id first (Koan's id 55000 runs before); id 55000 still unused in `chrome_command_ids.h` |
| Tab context menu (see "Shared hooks" above) | `chrome/browser/ui/tabs/tab_menu_model.cc` (one hook, `koan::AddKoanTabMenuItems`, at the end of `Build`), `tab_menu_model.h` (`koan_submenus_`), `tab_strip_model.cc` (`IsContextMenuCommandEnabled`, `ExecuteContextMenuCommand`) (+ `ui/tabs/BUILD.gn`) | the menu still has `CommandAddToSplit`, `CommandArrangeSplit`, `CommandTogglePinned`, `CommandCloseTab`, `CommandCloseOtherTabs` and `CommandCloseTabsToRight` items, which Koan relabels, tags or inserts around; the views controller still hands every command id to `TabStripModel` (Koan's ids are 3000-3399, see `koan_tab_menu.h`); `CommandTogglePinned` still reaches `ExecuteContextMenuCommand`; Spaces (`kSpaces`) still find the tab group items `CommandToggleFocusGroup` / `CommandAddToExistingGroup` / `CommandAddToNewGroup` / `CommandRemoveFromGroup` by command id and swap them for Move To |
| Sidebar tab rows | `chrome/browser/ui/views/tabs/common/tab_strip_view_layout.cc` (`CalculateVerticalLayout`), `tab_strip_view.cc` (`IsPositionInWindowCaption`) | the vertical layout still places pinned container, separator, unpinned container top to bottom with a running `y`; Koan's rows view is a direct `TabStripView` child |
| Look: system and core colours | `ui/color/color_mixers.{h,cc}` (`SetCoreColorMixerOverride`) | `AddColorMixers` still builds Ref, Sys, core default, native core, then `ui_color_mixer`: Koan's early mixer must sit after `AddNativeCoreColorMixer` and before `AddUiColorMixer`. A recipe's input colours (the ids it names: `{kColorX}`, `SetAlpha(kColorX, …)`) are resolved against the provider's last mixer (`ColorMixer::input_mixer_getter_`), not against the mixers before it, so a late mixer's value for `kColorX` is what every earlier recipe reads too. That makes a Koan colour that follows another colour of Koan's (or Chromium's) a cycle risk across mixers (a recipe in one mixer reading an id whose recipe in another reads it back): Koan's colours that follow others are computed values (`FloatingUrlBarCardColor`, `AddZenSuggestionColors`), and the unit test builds every look's provider. Only the id's *own* previous value (an override that keeps part of it) comes from the mixer before. The early mixer sits before `ui_color_mixer` so that the core and system colours it replaces are the ones the mixers above derive their own values from; if Chromium adds a mixer that sets core ids before `AddUiColorMixer`, move the call after it |
| Look: colour mode | `chrome/browser/themes/theme_service.cc` (`GetColorProviderKey`, + `themes/BUILD.gn`: `//koan/browser/ui/views`) | the key's `color_mode` is still what picks dark/light for every Views surface and, on macOS, the NSWindow appearance (`NativeWidgetNSWindowBridge::SetColorMode`); the hook runs last and the first call also installs the early mixer and loads the look |
| Look: shape | `chrome/browser/ui/views/chrome_layout_provider.cc` (`GetCornerRadiusMetric`, + `chrome/browser/ui/views/BUILD.gn` `layout_provider_impl`: `//koan/browser/ui/views`), `chrome/browser/ui/layout_constants.cc` | every `ShapeContextToken` in `ShapeStep` (`koan_look_controller.cc`) still exists (new ones fall back to Chromium's radius); vertical tab radius still comes from `kVerticalTabCornerRadius` |
| Look: "Search..." placeholder | `chrome/browser/ui/views/omnibox/omnibox_view_views.cc` (`InstallPlaceholderText`, `ShouldShowPlaceholderText`, `UpdatePlaceholderTextColor`, + its `BUILD.gn` `impl`: `//koan/browser/ui/views`) | still computes the placeholder with `omnibox::ComputePlaceholderText` and sets it with `SetPlaceholderText`; Koan's floating card swaps the text (`koan::AdjustOmniboxPlaceholder`) and calls `InstallPlaceholderText()` itself when the card opens or closes; `ShouldShowPlaceholderText()` still lets the card show it while focused (`koan::ShowsCardPlaceholder`; Chromium hides it then); `UpdatePlaceholderTextColor()` still sets the colour by id (`koan::AdjustOmniboxPlaceholderColorId`) |
| Look: suggestion rows (Zen's floating URL bar card) | `chrome/browser/ui/views/omnibox/omnibox_match_cell_view.cc` (`CalculatePreferredSize`: one-line row height), `omnibox_result_view.cc` (`GetPopupCellBackground`: the hovered/selected corners), `omnibox_text_view.cc` (`CreateRenderText`: font; `ApplyStyling`: the text colour id) (+ the same `BUILD.gn` dep) | a one-line row is still `kRowHeight` high; hovered/selected rows still get a rounded-rect background with `radii` from `half_row_height`; a row's text is still one `OmniboxTextView` with its font from `TypographyProvider::GetFont(CONTEXT_OMNIBOX_POPUP, ...)` and a colour id chosen per classification (kColorOmniboxText for plain text). Each hook gets the view (`this`, or `GetPopupCellBackground`'s `view`) and does nothing unless it is in a window whose floating URL bar is Zen's card (`koan::ShowsZenCardRows`; the popup's widget is a child of the browser window's), so other omnibox popups keep Chromium's rows. Koan only changes the numbers (49pt, radius 8, 14pt/500, the title at 70%): what rows say and their order are the omnibox providers' (#95) |
| Tab and tile sizes | `chrome/browser/ui/layout_constants.cc` | the `kVerticalTab*` constants still size vertical tab rows, pinned tiles and their radius |
| Close button on hover, favicon gap | `.../tabs/common/tab_view_vertical_layout.cc` (`IsChildVisible`, `OnInstalled`) | the close button's last branch still decides "active or hovered"; the icon's `padding` in `tab_children_configs_` is still the gap after the favicon (applied after the icon), so `koan::SidebarTabIconGap` sets the favicon-to-title gap |
| Tab rows and Essentials tiles | `.../tabs/common/vertical_tab_style_views.cc` (`PaintTab`), `pinned_tab_container_view_vertical_layout.cc` (`CalculateProposedLayout`) | vertical tabs still paint their background through `PaintTab`; the pinned grid still computes `children_on_row` |
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
| No "Unfocused tab group" toast | `chrome/browser/ui/tabs/tab_strip_model.cc` (`ExitFocusMode`) | the toast is still shown when `SetSelection` leaves focus mode (`TabGroupFocusExitReason::kTabOutsideGroupActivated`; Koan's Space switches do) |
| Spaces aren't saved groups | `chrome/browser/ui/tabs/saved_tab_groups/saved_tab_group_model_listener.cc` (`OnTabGroupAdded`, + `saved_tab_groups` `impl` dep), `chrome/browser/sessions/session_restore.cc` (`ProcessSavedGroup`, + `sessions` `impl` dep), `chrome/browser/ui/browser_live_tab_context.cc` (`AddRestoredTab`'s `SaveGroup`, + `browser_live_tab_context_impl` dep) | a local group still becomes a saved group only in these three places: a new group, a restored group (session restore saves an unsaved one, or connects a saved one, whose tabs the sync service then navigates to its saved URLs, which would undo #59's restore to the pinned URL), a reopened tab of an unsaved group. Tab restore still restores unsaved groups' tabs through `chrome::AddRestoredTab` (on demand) |
| Close tab on Back | `chrome/browser/ui/tabs/back_to_opener/back_to_opener_controller.cc` (`HasValidOpener`, which `CanGoBackToOpener` now calls) | upstream's back-to-opener (`tabs::kBackToOpener`, enabled in `koan_feature_overrides.cc`) still closes the tab and activates its opener; `CanGoBack` / `ShouldEnableBackButton` still ask `CanGoBackToOpener` and the back button's menu (`BackForwardMenuModel`) still asks `HasValidOpener` |
| Ctrl+Tab options (no patch) | `IsReservedCommandOrKey` in `BrowserView::PreHandleKeyboardEvent` | reserved keys still go through `FocusManager::ProcessAccelerator`, so Koan's targets see Ctrl+Tab first; `chrome::IsCtrlTabMruEnabled` still reports Chromium's recently-used Ctrl+Tab (Koan defers to it) |
| Settings page: shortcut capture (no patch) | `BrowserView::PreHandleKeyboardEvent` | still returns early while the FocusManager's shortcut handling is suspended (the editor suspends it while recording a key) |
| Pinned tabs in upstream's "leave pinned tabs" checks | `chrome/browser/ui/tabs/tab_strip_model.cc` (`GetTabsClosedByCommand`, which `HasTabsClosedByCommand` uses), `src/browser/koan_tab_hooks.cc` (`ShouldCloseTabOnBack`, no patch) | Close Other Tabs and Close Tabs Below still skip `IsPinned()` tabs there, and back-to-opener still asks `ShouldCloseTabOnBack`; each also asks `koan::IsPinnedForUser()`, which counts tabs pinned to their workspace. Other upstream pinned checks (freezing, Memory Saver) read the page's pinned state, which Koan sets for those tabs |
| Reopened tab keeps its pinned state | `chrome/browser/ui/browser_live_tab_context.cc` (`GetExtraDataForTab`) (+ `ui/BUILD.gn`) | the tab restore service still takes a closed tab's extra data from there and hands it to `CreateRestoredTab` |
| Close Tab command on a pinned tab | `chrome/browser/ui/browser_commands.cc` (`chrome::CloseTab`) (+ `ui/BUILD.gn`) | Ctrl+W, Ctrl+F4 and, on macOS, Cmd+W and the menu bar's Close Tab still run `IDC_CLOSE_TAB` through `chrome::CloseTab` |
| Tab row decoration | `chrome/browser/ui/views/tabs/common/tab_view.cc` (end of ctor) | `TabTitle` / `TabIcon` are still direct children of `TabView`, laid out by its layout manager |
| Pinned tab close button / middle click | `.../tabs/common/tab_strip_collection_controller.cc` (`CloseTab`) | both still end up in `CloseTab` |
| Custom tab label and icon | `chrome/browser/ui/tabs/tab_data.cc` (`TabData::FromTabInterface`) | tab views still read title and favicon from `TabData` |
| Koan tab and window data in the session | `chrome/browser/sessions/session_service_base.cc` (`BuildCommandsForTab`, `BuildCommandsForBrowser`), `chrome/browser/sessions/session_restore.cc` (`CreateRestoredBrowser`), `chrome/browser/ui/browser_tabrestore.cc` (`CreateRestoredTab`) (+ `sessions/BUILD.gn`, `ui/BUILD.gn`) | rebuilding the session file still drops tab and window extra data (Koan re-adds its keys); restored tabs still get `extra_data` and their entries before `NavigationController::Restore`; restored windows still get the window's `extra_data` in `CreateRestoredBrowser`, or are the open browser (`browser = browser_`, e.g. Restore after a crash) |
| Reopened window keeps Koan's window data | `chrome/browser/ui/browser_live_tab_context.cc` (`GetExtraDataForWindow`, `Create`) | a closed window's extra data still comes from `GetExtraDataForWindow` and goes to `Create` |

Then look for new upstream pinned checks: Zen's pinned tabs (pinned to their
Space) aren't Chromium's pinned tabs, so every `IsPinned()` / `IsTabPinned()`
upstream adds is a place that may close, move or freeze them. Compare the
call sites with the previous revision's and decide for each new one whether
it needs `koan::IsPinnedForUser()` (closing or moving tabs) or not (layout,
Chromium's own pinned container):

```sh
cd chromium/src
for rev in <previous-revision> HEAD; do
  git grep -nE 'IsTabPinned\(|->IsPinned\(\)' $rev -- chrome/browser/ui \
    chrome/browser/performance_manager components/performance_manager |
    sed -E "s/^[^:]+:([^:]+):[0-9]+:/\1:/" | sort > /tmp/pinned-$rev.txt
done
diff /tmp/pinned-<previous-revision>.txt /tmp/pinned-HEAD.txt
```

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
