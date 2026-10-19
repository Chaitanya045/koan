# Koan Phase 2 research: Zen "Workspaces" + "Essentials"

Read-only research against the Koan checkout `chromium/src` at
**Chromium 158.0.8111.0** (`chrome/VERSION`: MAJOR=158, MINOR=0, BUILD=8111,
PATCH=0). All `file:line` pairs are relative to that tree. Builds may be
running; nothing here was built or patched.

This answers six questions and ends with a concrete recommended architecture,
its list of upstream hook points, and risks. Claims that could not be directly
verified in this checkout are marked **UNVERIFIED**.

---

## 0. Executive summary

- Upstream already contains a per-window "show only one tab group" mechanism:
  the `features::kTabGroupsFocusing` feature ("Tab Groups Focusing"). When a
  group is *focused*, the vertical tab strip hides every tab/group that is not
  in the focused group, **but keeps pinned tabs visible**, persists the focused
  group across restarts via window extra data, cycles groups with
  `RotateFocusedGroup`, and has a two-finger horizontal swipe handler. This is
  the closest upstream analogue to Zen Workspaces and should be the base.
- The tab strip is now a tree of `tabs::TabCollection`s
  (`TabStripCollection` → `PinnedTabCollection` / `UnpinnedTabCollection` →
  `TabGroupTabCollection` / `SplitTabCollection`). The vertical strip renders
  this tree through a separate `TabStripView` + `TabCollectionNode` view-model
  layer (`chrome/browser/ui/views/tabs/common/`), **not** through the older
  `TabStrip`/`TabContainerImpl` used by the horizontal strip.
- There is **no** upstream "Workspaces", "Projects", or "Essentials" tab
  feature (the only `kEssentials` symbol is the unrelated ChromeOS app-list
  collection). Both must be Koan-native.
- Pinned tabs in vertical mode are already laid out as a **multi-column icon
  grid** and live in their own container outside the group tree, i.e. they are
  already "shared across workspaces". That makes Essentials a reskin of the
  pinned region rather than a new subsystem.
- The recommended implementation: map each workspace to one tab group and drive
  `TabStripModel::selection_model_.focused_group` (the mechanism upstream
  already built), reskin/relabel the group UI, and put the Essentials grid +
  workspace switcher in the vertical region's content area above the tab list.

---

## 1. TabStripModel, groups, saved groups, and tab collections

### 1.1 The collection tree

`tabs::TabCollection` is the storage interface for tab-strip structure
(`components/tabs/public/tab_collection.h:40-45`). Its fixed type set is:

```
// components/tabs/public/tab_collection.h:152-161
enum class Type : uint8_t {
  MIN, TABSTRIP = MIN, PINNED, UNPINNED, GROUP, SPLIT, MAX = SPLIT
};
```

with a paired `LINT.ThenChange` to the view-model node enum
(`tab_collection.h:164` → `chrome/browser/ui/views/tabs/common/tab_collection_node.h:34`).
Concrete collections:

| Collection | File | Notes |
|---|---|---|
| `TabStripCollection` | `components/tabs/public/tab_strip_collection.h:33-45` | Root; owns a `PinnedTabCollection` and an `UnpinnedTabCollection` (`:44-45`) |
| `PinnedTabCollection` | `components/tabs/public/pinned_tab_collection.h` | Pinned region, no nesting |
| `UnpinnedTabCollection` | `components/tabs/public/unpinned_tab_collection.h` | Holds ungrouped tabs + group collections |
| `TabGroupTabCollection` | `components/tabs/public/tab_group_tab_collection.h:1-49` | Wraps a `tab_groups::TabGroupId` |
| `SplitTabCollection` | `components/tabs/public/split_tab_collection.h` | Wraps a `split_tabs::SplitTabId` |

The `TabStripCollection` also keeps **detached** group collections during
drag-and-drop (`tab_strip_collection.h:88-110`, impl
`components/tabs/impl/tab_strip_collection.cc:370-376`,
`:405-418`, `:522-531`). This is transient (the collection is removed from the
tree and re-inserted into another window), not a persistent hidden-tabs store.

`TabStripModel` (`chrome/browser/ui/tabs/tab_strip_model.h:200`) is the
source-of-truth wrapper around this tree; `contents_data_` is the owning
`TabStripCollection`. Public group API is the `AddToNewGroup` /
`AddToExistingGroup` / `AddToGroupForRestore` / `RemoveFromGroup` family
(`tab_strip_model.h:683-704`) plus `GetTabGroupForTab` (`:547`).

### 1.2 Tab groups vs saved tab groups

- `TabGroupModel` (`chrome/browser/ui/tabs/tab_group_model.h:27-79`) is owned by
  `TabStripModel` (`tab_strip_model.h:1474`) and maps `TabGroupId` → `TabGroup`.
- Group visual state (title, color, collapsed) is
  `tab_groups::TabGroupVisualData` (`components/tab_groups/tab_group_visual_data.h:18-48`);
  `is_collapsed()` is `:37`.
- **Saved tab groups** are the sync/persistence layer in
  `components/saved_tab_groups/public/` (`TabGroupSyncService`,
  `saved_tab_groups/public/tab_group_sync_service.h:49`). `SessionService`
  writes them alongside session data (`chrome/browser/sessions/session_service.cc:317-353`).

### 1.3 Is there an upstream "hide a set of tabs from the strip" concept?

**Yes — tab-group focus mode.** `features::kTabGroupsFocusing`
(`chrome/browser/ui/ui_features.cc:578`, disabled by default; chrome://flags
entry `tab-groups-focusing` in `chrome/browser/about_flags.cc`,
description in `chrome/browser/flag_descriptions.h:4555-4556`).

- `TabStripModel::GetFocusedGroup()` returns
  `selection_model_.focused_group()` when the feature is on
  (`chrome/browser/ui/tabs/tab_strip_model.cc:2216-2220`).
- `EnterFocusMode` / `ExitFocusMode` / `RotateFocusedGroup`
  (`tab_strip_model.cc:2223`, `:2292`, `:2315`) set that selection field and
  notify `OnTabGroupFocusChanged`.
- The **vertical** strip hides non-focused groups in the view-model layout:
  `UnpinnedTabContainerViewLayout::IsChildVisibleInContainer` returns false for
  any child whose group ≠ focused group
  (`chrome/browser/ui/views/tabs/common/unpinned_tab_container_view_layout.cc:375-391`),
  and `CalculateVerticalMinimumSize` counts only visible children
  (`:342-372`). `GetFocusedGroupId` is `:393-402`.
- The **horizontal/old** strip does the same by "implicitly collapsing" tabs
  outside the focused group and hiding their headers in
  `TabContainerImpl::SetTabSlotVisibility`
  (`chrome/browser/ui/views/tabs/tab_container_impl.cc:748-828`, esp. `:790-799`
  and `:767-773`).
- **Pinned tabs are never hidden**: focus mode only filters the *unpinned*
  container; the pinned container is a sibling. In the old path this is
  explicit at `tab_container_impl.cc:790-799`
  (`if (!tab->data().pinned) …is_collapsed_by_focus_mode`).

This is exactly the shape Koan's Workspaces need: "only the active collection's
tabs show, pinned icons always show." No other upstream surface hides tabs
without closing them. (Collapsed *groups* hide their own child tabs but always
show the group header, `tab_group_view.cc:345-358`; that is per-group, not a
workspace switch.)

---

## 2. Options to implement Workspaces

Requirement recap: named collections within one window; only the active
workspace's tabs show; switch by sidebar button / keyboard / swipe; new tabs
land in the active workspace; persisted across restarts; Essentials shared.

### Option (a) — one tab group per workspace + hide inactive groups from the view

Implement a workspace as a `TabGroupTabCollection` whose
`tab_groups::TabGroupVisualData` carries the workspace name/icon, and make the
"active workspace" the model's focused group. Upstream already does almost
everything.

What must change / be reused:

- **Hide/show**: reuse focus mode's filter
  (`unpinned_tab_container_view_layout.cc:375-391`). No new hiding logic if the
  active workspace is `GetFocusedGroup()`. Koan only needs to *treat a group as
  a workspace* and supply a switcher.
- **Switch shortcuts**: upstream commands already exist:
  `IDC_FOCUS_NEXT_TAB_GROUP` / `IDC_FOCUS_PREV_TAB_GROUP`
  (`chrome/app/chrome_command_ids.h:134-135`) →
  `FocusNextTabGroup` / `FocusPreviousTabGroup`
  (`chrome/browser/ui/browser_commands.cc:1877`, `:1907`), which cycle groups in
  focus mode via `FocusAdjacentTabGroupInFocusMode`
  (`browser_commands.cc:1884-1887`, `:1914-1916`). Current bindings are
  **Shift+Alt+X / Shift+Alt+Z** (`chrome/browser/ui/accelerator_table.cc:319-320`).
  Zen's Ctrl+Alt+Left/Right can be added as additional accelerators (Koan
  patch, one line each) or Koan can register its own commands.
- **Ctrl+Tab already skips hidden tabs**: `SelectNextTab`/`SelectPreviousTab`
  (`tab_strip_model.cc:1848`, `:1852`) go through `SelectRelativeTab`, which
  skips tabs in collapsed groups and tabs outside the focused group
  (`tab_strip_model.cc:4380-4415`, esp. `:4393-4405`). So tab cycling is
  correct for free.
- **Ctrl+1..9**: `IDC_SELECT_TAB_0..8` call `ActivateTabAt`
  (`chrome/browser/ui/browser_command_controller.cc:823-835`). `ActivateTabAt`
  does not consult the focused group, so Koan must either map the index over
  *visible* tabs or ignore indices pointing at hidden tabs.
- **Tab search**: the WebUI lists every tab from
  `TabStripService::GetTabsWithoutObservation()` and walks the whole container
  tree (`chrome/browser/ui/webui/tab_search/tab_search_page_handler.cc:470-490`,
  `:503-514`); it does **not** filter by focused group, so hidden-workspace tabs
  would appear. Koan must filter (or group) by workspace, or accept leakage.
- **New tabs**: insertion respects the `group` argument
  (`tab_strip_model.cc:3936-3957`). Upstream does not auto-assign a new tab to
  the focused group; Koan must pass/select the active workspace's group on
  new-tab creation (one Koan hook on the new-tab path plus `AddToExistingGroup`).
- **Model-level helpers already treat the focused group as special** for open/
  close/move commands (`tab_strip_model.cc:3833-3838`, `:3867-3875`,
  `:3954-3956`), so many secondary behaviours already "respect the workspace".

Risk: **lowest**. Mostly reuses upstream, and the whole behaviour is already
guarded by one feature flag that Koan can enable (or shadow with its own
feature).

### Option (b) — keep inactive workspace tabs out of the TabStripModel

Store each inactive workspace's tabs as detached `WebContents`/discarded tabs
owned by Koan, re-inserting them on switch.

- Upstream detach path exists (`DetachTabAtForInsertion`,
  `tab_strip_model.h:348`; `DetachWebContentsAtForInsertion`, `:363`;
  `DetachedTab` struct `:115-158`) but is designed for **tab drag between
  windows** and for historical/recent tabs, not for "parked but live" tabs.
- Tab lifecycle is deeply coupled to the model: `TabModel::OnAddedToModel`,
  model observers, `TabFeatures`, session mapping, freezing, tab search,
  `TabStripService`, `TabGroupSyncService`, discard, and process management all
  assume a tab lives in a model. Parking live `WebContents` outside it would
  require re-creating `TabModel` on every switch and re-establishing every
  feature; hidden tabs would have no `TabHandle`, so features keyed by
  `TabHandle` break.

Risk: **very high**; effectively a second tab-management subsystem that fights
upstream on every rebase. Not recommended.

### Option (c) — a new `TabCollection` type

Add `Type::WORKSPACE` next to `GROUP`/`SPLIT`
(`components/tabs/public/tab_collection.h:152-161`) and its view-model sibling
(`tab_collection_node.h:34`), a `WorkspaceCollection` storage class, a
`WorkspaceView`, and layout support throughout
`TabStripViewLayout`/`UnpinnedTabContainerViewLayout`/`TabCollectionNode`.

- Pros: the "correct" long-term model; multiple workspaces could exist
  simultaneously as sibling subcollections, and switching would be a
  reparent/visibility change inside the tree.
- Cons: the `LINT.ThenChange` pairs, `ValidateData`, `FindMovePositionRecursive`,
  drag-and-drop, context menus, session restore, `tab_search`, and the
  `tabs_api` Mojo tree builder all enumerate `TabCollection::Type`; adding a
  type means auditing all of them. Since focus mode already gives sibling-level
  hiding, this is a large cost for little near-term benefit.

Risk: **high** (breadth), but architecturally clean. Reasonable as a later
refactor *under* Koan if (a) proves insufficient.

### Recommendation for §2

Adopt **(a)**: workspaces = tab groups + focused-group visibility + a Koan
switcher/Essentials UI. Reuse `features::kTabGroupsFocusing` semantics. This is
also the direction `research-upstream.md` §7 anticipated (a "new Koan
tab-collection layer over `TabStripModel`"), but with the important discovery
that the layer can be the existing focus-mode field rather than new model code.

---

## 3. Vertical tab strip: where views are made, the top container, and hook points

### 3.1 The vertical strip is the *new* architecture

The vertical region does not build the old `TabStrip`/`TabContainerImpl`. It
creates a `TabStripView` driven by a `TabCollectionNode` tree:

- `VerticalTabStripRegionView` constructor
  (`chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc:124-234`).
- `BaseTabStripRegionView::InitializeTabStrip`
  (`chrome/browser/ui/views/frame/base_tab_strip_region_view.cc:78-122`)
  constructs `RootTabCollectionNode` (`:87-93`) and
  `TabStripCollectionController` (`:103-106`). Called for the vertical strip at
  `browser_view.cc:1625`.
- The per-collection views live in
  `chrome/browser/ui/views/tabs/common/`:
  `tab_strip_view.{h,cc}` (`TabStripView`), `pinned_tab_container_view.*`,
  `unpinned_tab_container_view.*`, `tab_group_view.*`, `split_tab_view.*`,
  `tab_view.*`, `tab_collection_node.*`, `tab_strip_view_layout.*`,
  `unpinned_tab_container_view_layout.*`, `tab_group_view_layout.*`.
- `TabStripView` owns two scroll views: `pinned_tabs_scroll_view_` (top) then a
  separator then `unpinned_tabs_scroll_view_`
  (`chrome/browser/ui/views/tabs/common/tab_strip_view.cc`, constructor).
  `AddScrollViewContents` routes the PINNED node to the pinned scroll view and
  the UNPINNED node to the other (`tab_strip_view.cc`,
  `AddScrollViewContents`).

### 3.2 Is there a filter hook point to hide groups/tabs?

Yes — the layout's visibility predicate is the single choke point:

```
// chrome/browser/ui/views/tabs/common/unpinned_tab_container_view_layout.cc:375-391
bool UnpinnedTabContainerViewLayout::IsChildVisibleInContainer(
    const UnpinnedTabContainerView* tab_container_view,
    std::optional<tab_groups::TabGroupId> focused_group_id,
    const views::View* child) const {
  if (!CanBeVisible(child)) return false;
  ...
  if (focused_group_id.has_value() &&
      GetGroupIdForChild(child) != focused_group_id.value()) {
    return false;
  }
  return true;
}
```

`GetFocusedGroupId` (`:393-402`) reads the controller/model. Because this is one
predicate, a Koan workspace "active workspace" is naturally expressed by the
model's focused group — no new filter is needed. A Koan-owned *additional* filter
(e.g. to hide a whole workspace group when focus mode is off) would be inserted
here, but this is the only place that decides vertical child visibility.

Both the group header and its children are laid out with the child's computed
visibility: `TabGroupViewLayout` uses `child->GetVisible()`
(`chrome/browser/ui/views/tabs/common/tab_group_view_layout.cc:142-168`) and the
group's own `UpdateChildVisibilityForCollapseState`
(`tab_group_view.cc:345-358`) only reflects the group's *collapsed* flag, not
focus mode.

### 3.3 The top container and where to add Essentials / a switcher

- The area above the tab list is `VerticalTabStripTopContainer`
  (`chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_top_container.h:33-140`),
  a sibling of the tab strip, added first in the region view
  (`vertical_tab_strip_region_view.cc:141-143`).
- The tab list lives in `content_area_view_`, a vertical `FlexLayoutView`
  (`vertical_tab_strip_region_view.cc:145-158`). Order inside it: a
  `top_button_separator_` (`:166-174`), the tab strip view inserted **directly
  after that separator** by `AddTabStripView`
  (`vertical_tab_strip_region_view.cc:868-886`, the insertion is `:881-886`),
  and `bottom_button_container_` (added at `:176-179`). `gemini_button_` is a
  placeholder added at `:193-194`.

**Hook point for Essentials + workspace switcher:** insert Koan views into
`content_area_view_` around the separator — i.e. a Koan hook called from
`AddTabStripView` (or from the region view constructor) that adds
`KoanEssentialsGridView` and `KoanWorkspaceSwitcherView` at a controlled index.
The layout is a flex layout, so no absolute math is needed. This is a 1-5 line
upstream touch in `vertical_tab_strip_region_view.cc`, with the views themselves
in `//koan`.

---

## 4. Pinned tabs in vertical mode → Essentials

Pinned tabs are **already a multi-column icon grid** in the vertical strip, and
they already sit outside the group tree.

- Pinned tabs live in their own `PinnedTabCollection`
  (`components/tabs/public/tab_strip_collection.h:44`) and render in a dedicated
  `PinnedTabContainerView` (`chrome/browser/ui/views/tabs/common/pinned_tab_container_view.h:23-90`),
  which is the contents of `pinned_tabs_scroll_view_` (`tab_strip_view.cc`,
  `AddScrollViewContents`).
- Grid math: `PinnedTabContainerView::CalculateVerticalLayout`
  (`chrome/browser/ui/views/tabs/common/pinned_tab_container_view.cc:375-479`)
  computes `children_on_row` from the available width and wraps into multiple
  rows (`:406-430`, wrap loop `:432-469`); **collapsed state forces one per row**
  (`:415-419`). Horizontal orientation is a single row
  (`CalculateHorizontalLayout`, `:333-373`). This is precisely an Essentials
  grid.
- Because the pinned container is a sibling of the unpinned (group) container,
  focus mode never hides it (see §1.3). So pinned tabs are naturally **shared
  across workspaces** when workspaces are focused groups.

**Conclusion:** Essentials can be "the pinned region, reskinned as an icon
grid." Two levels of fidelity:

- *Minimal*: keep using real pinned tabs; Koan restyles `PinnedTabContainerView`
  (background, spacing, tile shape) and adds an "add Essential" affordance that
  pins a tab. Pinned semantics (persist, always visible, synced) come for free.
- *Full Zen semantics*: pinned tabs in Zen are not the same as Essentials
  (Essentials are simpler bookmarks-like tiles; pinned tabs still exist
  separately). If Koan needs both, it can add a Koan-owned grid view fed from
  its own model and keep the pinned container hidden/repurposed. **UNVERIFIED**
  whether Zen's Essentials and pinned tabs are distinct models; the upstream
  structure supports either by choosing whether to reuse the
  `PinnedTabContainerView` or add a Koan sibling view.

Row count, tile size, and collapse behaviour are all local to
`pinned_tab_container_view.cc`, so a Koan-style grid does not require touching
the collection model.

---

## 5. Session persistence (groups, pinned state, and a per-tab workspace id)

### 5.1 How groups and pinned state are saved/restored today

- `SessionService` mirrors live model changes as commands:
  `SetTabGroup` (`chrome/browser/sessions/session_service.h:107-112`) and
  `SetTabGroupMetadata` (`:113-126`).
- Command ids: `kCommandSetTabGroup = 25`,
  `kCommandSetTabGroupMetadata2 = 27`
  (`components/sessions/core/session_service_commands.cc:97-100`).
- Pinned state is serialised per tab in the session tab data
  (`components/sessions/core/session_types.h:114` region) and restored through
  `LiveTabContext::IsTabPinned` (`components/sessions/core/live_tab_context.h:69`).
- Group metadata (title/color/collapsed) restores via
  `SessionTabGroup` (`session_service_commands.cc:780-804`) and
  `RestoreTabGroupMetadata` (`chrome/browser/sessions/session_restore.cc:1276+`).

So under option (a), **workspace membership (group id) and pinned/Essentials
state already persist** with no new persistence code.

### 5.2 The existing model for "active workspace across restart"

Focus mode already persists the active group, and this is the exact template for
persisting the active workspace:

- The key is
  `tabs::TabStripModelSelectionState::kFocusedTabGroupIdKey[] = "focused_tab_group_id"`
  (`chrome/browser/ui/tabs/tab_strip_model_selection_state.h:107-109`).
- Written incrementally when focus changes:
  `Browser::OnTabGroupFocusChanged` calls `SessionService::AddWindowExtraData`
  (`chrome/browser/ui/browser.cc:634-646`).
- Also written at session rebuild:
  `session_service_base.cc:771-781`.
- Read back for restore: `BrowserLiveTabContext::GetExtraDataForWindow`
  (`chrome/browser/ui/browser_live_tab_context.cc:208-228`) and
  `BrowserLiveTabContext::GetExtraDataForWindow`→create-params at
  `:552-560`; stored on `BrowserInitState::initial_focused_tab_group_id_`
  (`chrome/browser/ui/browser_init_state.cc:43-44`,
  `browser_init_state.h:91-92`); applied by `RestoreFocusedTabGroup`
  (`chrome/browser/sessions/session_restore.cc:1245-1269`).

### 5.3 Generic extra-data hooks (for a per-tab workspace id if ever needed)

If a workspace id must be attached **per tab** rather than via group
membership, upstream provides generic key/value persistence:

- `SessionService::AddTabExtraData` / `AddWindowExtraData`
  (`chrome/browser/sessions/session_service.h:132-137`), impl
  `session_service.cc:365-384`.
- Commands `kCommandAddTabExtraData = 33`, `kCommandAddWindowExtraData = 34`
  (`components/sessions/core/session_service_commands.cc:107-108`); restore at
  `:1020-1040`.
- Storage: `SessionTab::extra_data` / `SessionWindow::extra_data`
  (`components/sessions/core/session_types.h:114`, `:238`).
- Read/write seams: `LiveTabContext::GetExtraDataForTab/Window`
  (`live_tab_context.h:52-54`), implemented in
  `chrome/browser/ui/browser_live_tab_context.cc`.

This is the "`SetTabExtensionAppID`-like" hook: append a key such as
`koan.workspace` without touching group internals. Under option (a) it is
redundant (group id suffices), but it is the fallback for option (c) or for
storing workspace order/icons.

Also relevant: `LiveTabContext::GetWorkspace()` exists
(`live_tab_context.h:80`) but is about the window-manager workspace/desk on
ChromeOS, not tab workspaces — do not confuse the two.

---

## 6. Swipe / horizontal scroll on Linux

Upstream already ships a horizontal-swipe handler for the vertical strip that
rotates the focused group — i.e. **switches workspaces under option (a)**:

- `VerticalTabStripFocusSwipeController`
  (`chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_focus_swipe_controller.h:18-62`)
  installs itself as a **pre-target handler on `VerticalTabStripRegionView`**
  (`vertical_tab_strip_focus_swipe_controller.cc:31-33`).
- On Linux/Windows it overrides `OnMouseEvent` and reads horizontal wheel
  offsets from `ui::MouseWheelEvent::x_offset()`
  (`…_focus_swipe_controller.cc:34-43`). On macOS it uses
  `OnScrollEvent` with `scroll_event_phase()` / `momentum_phase()`
  (`:45-92`).
- Thresholds and axis lock: `kAxisLockThreshold = 15`, `kSwipeThreshold = 75`,
  `kGestureResetTimeout = 300ms`
  (`…_focus_swipe_controller.h:20-23`); axis lock and one-shot trigger logic at
  `…_focus_swipe_controller.cc:105-162`.
- It calls `TabStripModel::RotateFocusedGroup(forward)`
  (`…_focus_swipe_controller.cc:169-175`), respecting RTL (`:157-160`).
- It is created only when `features::kTabGroupsFocusing` is enabled
  (`chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc:228-231`).

Related plumbing: in vertical orientation the tab-strip scroll views disable the
horizontal scrollbar and use a vertical one
(`tab_strip_view.cc`, `SetScrollViewProperties`), so horizontal wheel input over
the strip is otherwise unused and free for swipe.

**Scope caveat:** this handler spans only the tab strip region view. Zen's
"horizontal swipe anywhere over the sidebar" is satisfied; a swipe over the web
contents (e.g. touchpad three-finger) would need a Koan pre-target handler on
`BrowserView`/`ContentsWebView`. **UNVERIFIED** whether upstream routes such
synthetic horizontal scrolls to a browser-level hook on Linux.

---

## Recommendation

**Architecture: workspaces are tab groups driven by upstream focus mode; the
active workspace is `TabStripModel`'s focused group; Essentials is the pinned
region reskinned as an icon grid.** Reuse upstream instead of building a parallel
tab manager.

```
KoanWorkspaceController  (per window, owned by KoanBrowserViewController)
  ├─ workspace metadata: group_id -> {name, icon}   (Koan store; see below)
  ├─ switch(n)            -> TabStripModel::EnterFocusMode(group) / RotateFocusedGroup
  ├─ active()             -> TabStripModel::GetFocusedGroup()
  ├─ moveWorkspace(...)   -> TabStripModel::MoveGroupTo (order = tab order)
  ├─ newTabGroupFor(task) -> TabStripModel::AddToNewGroup
  └─ persists icon/order via SessionService::AddWindowExtraData("koan.workspaces")

KoanEssentialsGridView      (Koan view; styled PinnedTabContainerView or sibling)
KoanWorkspaceSwitcherView   (Koan view; buttons calling switch(i))
```

Runtime dependencies Koan turns on: `features::kTabGroupsFocusing` (so focused
groups hide other groups) — preferably wrapped by `koan::features::kKoanWorkspaces`.

### Upstream hook points (each 1-5 lines)

1. `chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc:868-908`
   (`AddTabStripView`) and/or the constructor (`:124-234`): call
   `koan::InstallWorkspaceUI(region_view, content_area_view_)` once. Koan inserts
   the Essentials grid + workspace switcher into `content_area_view_` after
   `top_button_separator_`. **~3 lines.**
2. `chrome/browser/ui/views/frame/browser_view.cc`: inside the existing Phase 1
   `koan::OnBrowserViewCreated` hook, create/reach the
   `KoanWorkspaceController`. Reuses the Phase 1 patch; **0-1 extra lines.**
3. `chrome/browser/ui/accelerator_table.cc:316-321` (`kTabGroupAcceleratorMap`):
   add `Ctrl+Alt+Left/Right` → `IDC_FOCUS_PREV_TAB_GROUP` /
   `IDC_FOCUS_NEXT_TAB_GROUP`. **~2 lines.**
4. `chrome/browser/ui/browser_command_controller.cc` (command dispatch +
   `UpdateCommandEnabled`): add Koan commands "switch to workspace N" /
   "new workspace", dispatched to `koan::ExecuteKoanCommand`. **~3 lines.**
5. New-tab-to-active-workspace: prefer a Koan hook at the new-tab command layer
   (`chrome/browser/ui/browser.cc` tab-creation path) that adds the new tab to
   the focused group; avoid touching `tab_strip_model.cc` insertion internals.
   **~2-3 lines.**
6. Tab search: make hidden-workspace tabs not appear — patch
   `chrome/browser/ui/webui/tab_search/tab_search_page_handler.cc:470-490` to
   skip tabs the active workspace hides (or return them grouped). **~5 lines.**
7. Ctrl+1..9: patch `browser_command_controller.cc:823-835` so `IDC_SELECT_TAB_n`
   maps over visible tabs (or ignores hidden indices). **~3 lines.**
8. Workspace icon (upstream `TabGroupVisualData` has title+color only,
   `components/tab_groups/tab_group_visual_data.h:22-37`): store
   `group_id → icon` in a Koan pref and persist with
   `SessionService::AddWindowExtraData` (`session_service.h:137`) under
   `"koan.workspaces"`; no upstream struct change. **0 upstream lines.**
9. Only if focus mode's visibility policy is insufficient: extend
   `UnpinnedTabContainerViewLayout::IsChildVisibleInContainer`
   (`unpinned_tab_container_view_layout.cc:375-391`) with a Koan predicate.
   **Avoid initially.**

Essentials uses hook 1 and (optionally) a Koan restyle of
`PinnedTabContainerView` (`pinned_tab_container_view.cc:375-479`); no model
change.

---

## Risks

- **Feature churn / DAB.** `kTabGroupsFocusing` is disabled by default and lives
  in files created 2025-2026 with active development and `LINT.ThenChange`
  coupling (`components/tabs/public/tab_collection.h:164`,
  `chrome/browser/ui/views/tabs/common/tab_collection_node.h:34`). APIs and file
  layout can change every rebase. Mitigate by isolating every upstream call in
  `KoanWorkspaceController` and gating behind a Koan feature.
- **Empty workspaces.** `EnterFocusMode` CHECKs the group is non-empty
  (`tab_strip_model.cc:2241`, `tab_group->tab_count() > 0`). A workspace with
  zero tabs cannot be focused as-is; Koan must either guarantee ≥1 tab, allow an
  "unfocused/all-visible" state, or handle empty workspaces specially.
- **Collapse vs workspace.** Workspace groups are real groups, so the user can
  collapse one (`tab_group_view.cc:356-358`), which hides its tabs independently
  of workspace switching. Decide whether to remove the collapse affordance for
  workspace groups.
- **Tab search / recent tabs leakage.** Tab search and the container walk show
  all tabs including hidden workspace tabs
  (`tab_search_page_handler.cc:470-490`, `:503-514`) unless filtered (hook 6).
- **Ctrl+1..9 and Ctrl+Tab.** Ctrl+Tab is already correct (§2); Ctrl+1..9 is not
  and needs hook 7.
- **Horizontal fallback.** If a user disables vertical tabs, the legacy
  `TabStrip`/`TabContainerImpl` path handles focus mode via
  `SetTabSlotVisibility` (`tab_container_impl.cc:748-828`); Koan must verify
  workspaces behave the same there or force vertical mode.
- **Restore ordering.** `RestoreFocusedTabGroup` is a no-op unless the feature is
  enabled at restore time (`session_restore.cc:1250-1253`); enable the feature as
  early as vertical tabs are enabled.
- **Reorder semantics.** `RotateFocusedGroup` orders workspaces by first tab
  index (`tab_strip_model.cc:2335-2337`); explicit workspace reordering means
  moving groups (`TabStripModel::MoveGroupTo`, `tab_strip_model.h:430`).
- **Pinned vs Essentials semantics.** Zen Essentials are conceptually distinct
  from pinned tabs; if Koan must support both simultaneously, the pinned
  container cannot simply be relabeled. **UNVERIFIED** (no upstream or Zen
  source inspected for this specific distinction).
- **Rebase surface.** The highest-churn files touched are
  `vertical_tab_strip_region_view.cc` and the `tabs/common/` view-model files;
  keep each Koan patch to a single call site and re-check them every rebase,
  consistent with `phase1-design.md` §0/§5.
