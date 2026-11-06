# Decisions

- **Chromium fork with native C++ Views UI**, not Electron/CEF. Electron lacks
  full extension support, `chrome://` pages, profiles and sync, and the user
  wants maximum UI performance. Rarely used screens (settings, editors) may be
  Lit WebUI, never React.
- **Build on upstream features**: launched vertical tabs
  (`VerticalTabStripRegionView`) and split view (`MultiContentsView`).
  Glance, workspaces, Essentials and Linux compact mode are Koan-native.
- **Hook patches, not `chromium_src` shadowing** (for now): global include
  changes force a full rebuild; our patch set is ~30 lines. Revisit past ~30
  patched files. See `.agent/notes/design-phase1-sidebar-compact.md` §1.3.
- **Compact mode is not an `ImmersiveModeController`**: that path is tied to
  fullscreen and ChromeOS/macOS frame code. Koan uses its own controller,
  reusing vertical-tab expand-on-hover with collapsed width 0, plus a
  layer-transform slide for the toolbar.
- ~~Track Chromium main for now~~ (superseded 2026-11-02): **follow Chromium
  release branches** (#86). Pin a release tag of the milestone, never a main
  commit. Take every Beta/Stable point release, and rebase onto each new
  milestone within about 2 weeks of it reaching Stable. A daily-driver
  browser needs Chrome's weekly security fixes. The M158 move, the routine and
  the checkout switch: [chromium-releases.md](chromium-releases.md).
- **macOS only, no official releases** (2026-11-02). Koan is built for
  Apple Silicon Macs for the user's own use. The app comes from the macOS CI
  `.dmg` (#69) or a local Mac build. Linux is only the agents' build and test
  bench. There's no Linux or Windows packaging (PR #13 closed, #5 not
  planned).
- **Keep the native Views architecture** (architecture review, 2026-11-02,
  [architecture-review.md](architecture-review.md)). Use C++ Views for
  anything always visible, animated, or used per click or keystroke. WebUI
  is fine for occasional, form- or list-heavy pages. Customisation comes
  through tokens and themes (#48), not CSS.
- Name: **Koan** (not "Zen"/"Chrome" — trademarks).
- **Koan feature names must be globally unique and `kKoan`-prefixed.**
  `BASE_DECLARE_FEATURE` is `extern "C"` (`base/feature.h:48`), so the C++
  namespace doesn't mangle the symbol. Today all users are in
  `//chrome/browser` source_sets (one link unit), so no export macro is
  needed; if a real `component()` ever references one, declare it
  `COMPONENT_EXPORT(...) BASE_DECLARE_FEATURE(...)`.
- **Flag metadata deferred:** `koan-*` flags aren't in
  `chrome/browser/flag-metadata.json` / `enums.xml` (`LoginCustomFlags`), so
  `AboutFlagsTest.EveryFlagHasMetadata` and `AboutFlagsHistogramTest` will
  fail. Add the entries before running those tests (Phase 4).
- **Zen look via colours + MultiContentsView card, not new views.** The
  toolbar takes the frame colour (Koan colour mixer, runs after Chrome's
  mixers, before custom themes), and the single contents become a card using
  the same mechanism as upstream split view (insets + rounded active
  contents). Only the active contents are rounded: rounding
  MultiContentsView's background too leaves unpainted (black) corners
  because nothing is drawn behind it.
- **Zen's shortcuts, not Chrome's, where they collide** (#11): Koan follows
  Zen's defaults (ZenKeyboardShortcuts.mjs v21) in one table,
  `KoanShortcutController`, registered after BrowserView's accelerators so
  it wins; contextual ones fall through.
- **Glance on Alt+click via blink** (#17): with Koan Glance on, an Alt-only
  click becomes `kNavigationPolicyNewPopup`, which link clicks never produce
  otherwise; synthesized Alt+clicks and window.open() keep upstream
  behaviour. Constraint: blink core links its own copy of
  `//koan/browser:features` (component build), so a Koan feature must only
  be read in one process type (browser *or* renderer) unless the target is
  made a proper exported component.
- **The Glance settings reach blink on the command line** (#37):
  Zen's `zen.glance.enabled` and `zen.glance.activation-method` are the
  profile prefs `koan.glance.enabled` / `koan.glance.activation_method`
  (settings rows). Blink decides the click's disposition, so it needs the
  method: `koan::AppendRendererSwitches` (hook in
  `ChromeContentBrowserClient::AppendExtraCommandLineSwitches`) passes it
  (or "off") to each renderer. A change reaches renderer processes started
  afterwards (the settings row says so). Live updates would need a
  `blink::RendererPreferences` field (mojom + traits, a widely included
  header); a feature parameter (the first version) can't be a setting. The
  browser side reads the prefs live: a click Glance declines (Glance off,
  not a web link, not the active tab of a normal window) is reopened with
  what its modifier means without Glance, so a stale renderer or an odd
  link never gets a bare popup window. The blink hook runs before upstream's
  Ctrl (new tab) and plain-click branches.
- **Koan context menu commands use their own ids** (#37): 55000 for the
  link menu ("Open Link in Glance", handled before upstream's metrics,
  which NOTREACHED on unknown ids), 3001 for the tab menu ("Split Out
  Tab", handled before TabStripModel's command switches).
- **Floating URL bar = the real toolbar, narrowed** (#22): while the
  omnibox has focus in single toolbar / compact mode, the top container
  (already in the overlay view) is re-laid out as a centred card and every
  ToolbarView child except LocationBarView is hidden. Koan observes the top
  container and toolbar bounds to undo BrowserView's full-width layout. No
  second omnibox, so everything Chrome's omnibox does keeps working; the
  popup anchors to the location bar by itself. #36 moved it into
  `KoanUrlBarController` and added Zen's behaviours: the omnibox edits over
  the pill and floats on the first keystroke (hook in
  `LocationBarView::OnChanged`).
- **Ctrl+T without a tab (#36)**: no hook in `chrome::NewTab()` (its callers,
  e.g. `CreateNewTabGroup`, use the tab it returns). Ctrl+T is a Koan
  accelerator, the new-tab button's `kActionNewTab` callback is swapped per
  window, and `ChromeOmniboxClient::OnAutocompleteAccept` turns CURRENT_TAB
  into NEW_FOREGROUND_TAB while the bar is open for a new tab.
- **Settings page = chrome://koan-settings, plain WebUI driven by a C++
  table** (#39): not a section in chrome://settings (that would mean
  patching its Polymer/TS build). The page's HTML/CSS/JS are embedded into
  the binary by a GN action (`embed_resources.py`) and served with
  `WebUIDataSource::SetRequestFilter`, so no grit target, resource id or
  .pak list changes upstream; one hook patch registers the `WebUIConfig`.
  What the page shows is `koan_settings_rows.cc` (panes > groups > rows
  bound to profile prefs); the handler only writes prefs listed there.
  Features apply changes live by observing their prefs. The shortcut
  editor suspends the window's FocusManager shortcuts while recording a
  key (as chrome://extensions/shortcuts does), because Chromium pre-handles
  non-Chrome accelerators (Koan's) before the page sees them.
- **Library = a child widget sliding over the page** (#43): like Glance, the
  panel is a TYPE_CONTROL child widget (so it sits above the page's native
  window and gets the pointer), its view layer translated for Zen's spring.
  Zen also slides the page aside; Chromium draws the page in its own native
  window, which view transforms don't move, so Koan's panel covers it
  instead. The widget is kept for 30s after closing (Zen unmounts its
  sections then) and does no work while closed. History, Downloads and the
  closed tabs come from Chromium's services (HistoryService,
  DownloadManager, TabRestoreService); Spaces is a thin view over
  KoanWorkspaceController (#34/#57 own Space data).
- **Window dragging from the page watches the page's mouse events, not a
  hit test** (#44): `KoanWindowDragController` adds a
  `RenderWidgetHost::AddMouseEventCallback` to the active tab's page. A
  press in the top 5% asks the page, in an isolated world, whether the point
  is empty space (Zen's ZenWindowDragChild checks), so nothing is decided
  synchronously in a hit test; moving past the threshold swallows the rest
  of the gesture, releases the button for the page and starts the move:
  `Widget::RunMoveLoop` on macOS, `WmMoveResizeHandler` on Linux. Synthetic
  CDP mouse events go through the same callback, so the e2e test can check
  what the page sees even though headless has no window manager. The same
  callback tells the download animation where the page was clicked (the
  page is a native view on macOS, so Views' event handlers never see it).
- **Folders are Koan's own model, drawn by upstream's tab strip** (#42):
  Zen's folders are nested Firefox tab groups in a Space's pinned section.
  Chromium's tab groups can't nest (`TabGroupTabCollection` allows only
  splits) or be pinned, and Koan's Spaces already are tab groups, so a
  folder is a Koan object (name, collapsed, parent folder, Space, tab
  handles), per window. Its tabs stay ordinary tabs of the Space and, as in
  Zen, are pinned to it (#59), so folders sit in the pinned section.
  The Space group's vertical layout has one Koan hook, `koan::SpaceGroupRows`:
  a `KoanFolderRowView` (a child of the group view) before each folder's
  tabs, tabs indented 14px per level and, in a collapsed folder, given zero
  height and hidden once upstream's 200 ms layout animation is over; then
  #150's separator and New Tab rows. Drag, close, hover cards and context
  menus of the tabs stay upstream's. There is no ordering pass of its own:
  Koan's folder actions arrange the folders' tabs once after the change, and
  Koan's pinned tabs' Normalize()
  (one pass, one wait for drags and session restore) keeps folder tabs
  together for moves Koan didn't make (`ArrangeFolders()`). Drags are decided
  when the drag ends, from where each dragged tab was dropped (one back where
  it started keeps its folder; a drop on a row's middle joins at its start).
  Persisted through #59's session data (`koan_session_data.h`, key
  "koan.folders"), with no session patch of its own: the folder tree in the
  window's data (each folder with a random token), and each folder tab's
  token in its own data, so membership follows the tab, not its index. A
  `WindowRestoreHook` brings the folders back before the tabs, which join them
  by token. Without a session restore, a profile pref (`koan.folders.list`)
  brings back the folders of the window the user was in, with their tabs.
