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
- **macOS and Linux x86_64, no official releases** (2026-11-02, Linux
  added 2026-11-26). Koan is built for Apple Silicon Macs for the user's own
  use. The app comes from the macOS CI `.dmg` (#69) or a local Mac build. Linux
  x86_64 ships as a Flatpak nightly built by CI (`linux.yml`, the
  `nightly-linux` pre-release, [linux-ci.md](linux-ci.md); the owner decided
  on 2026-11-26, taking over PR #442 as #466), which replaces the earlier
  "Linux is only the agents' build and test bench": the development build
  there is unchanged. There's still no Windows packaging (PR #13, packaging and
  the release process, closed; #5 not planned).
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
- **The Glance settings reach blink on the command line, then pushed**
  (#37, #326): Zen's `zen.glance.enabled` and `zen.glance.activation-method`
  are the profile prefs `koan.glance.enabled` /
  `koan.glance.activation_method` (settings rows). Blink decides the click's
  disposition, so it needs the method: `koan::AppendRendererSwitches` (hook
  in `ChromeContentBrowserClient::AppendExtraCommandLineSwitches`) passes it
  (or "off") to each renderer when it starts, and on a change
  `koan::UpdateRendererSettings()` pushes it to every renderer process over
  Koan's channel-associated `koan.mojom.RendererSettings` (one hook in
  `ChromeContentRendererClient::RenderThreadStarted`), so open pages follow
  it at once, as in Zen. Not `blink::RendererPreferences`: a new field is a
  mojom + traits + struct patch, it is per WebContents (every tab, Glance
  page and extension page would need a sync), and the navigation-policy
  hook has no frame to read it from. The value is per process anyway (a
  process has one profile). It lives in the `//koan/common:glance_trigger`
  component, so blink core and chrome/renderer share one copy in component
  builds (see the constraint above). A feature parameter (the first
  version) can't be a setting. The
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
  the pill after a click and floats when opened with the keyboard (hook in
  `LocationBarView::OnChanged`). A pill click never floats it, typing
  included (Zen's `#openPopover` decides once, when the bar opens), which
  reverses #36's "floats on the first keystroke" (#96).
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

## Agent control and UI, 2026-11-19/20 (maintainer with the user)

- **Agent indicator on the controlled tab only** (#398): the user saw the old
  pill on every tab. The pill ("<agent> is controlling this tab · Take back /
  Take over · Stop", with an accent border) shows only on a tab an agent
  holds. Elsewhere there is a sidebar chip (name, "N waiting", a menu to go
  to the agent's tabs or Stop all), a corner dot when compact mode hides the
  sidebar, and dots on agent tabs in the tab list. T10 holds in every layout.
  Next: a menu-bar item and notifications, because the lease lets agents work
  behind hidden windows, where in-window marks can't be seen.
- **Agents find Koan, ego-style but opt-in** (#397): Settings > Agent Control >
  "Set up agents on this Mac" links `~/.local/bin/koan` and writes the small
  `koan-browser` skill only into installed agents' skill folders, only on
  click, with Remove. It never writes silently or into folders of agents that
  aren't installed: ego wrote up to 48 folders, and users complained.
  `npx skills add koan-browser/koan` installs the repo copy. The entry skill
  says only "run `koan skill`", so the guide matches the installed version.
- **`koan pair` asks for every capability by default** (#397), as part of "agents
  use the browser as the user does". The user can untick any of them in the
  dialog, and each take-over still asks. Settings has no per-agent capability
  switch: re-pairing changes capabilities, and the guide must never send
  agents to a switch that doesn't exist.
- **`koan` is the only way in.** Agents never use AppleScript JavaScript, a
  remote-debugging port or another browser. An agent on another machine runs
  `koan` on the Mac over ssh, by its full path, with one ssh connection
  reused or `koan shell`.
- **Speed comes from fewer, cheaper agent turns, done natively** (user: "Ego's
  speed is a gimmick"): event-based settle and `act --snapshot`, `choose`,
  `koan shell` (#401); native multi-step `act` batches that check pause and
  userNeeded before every step; interactive, scoped and diff snapshots with
  stable locators; the lease for background tabs; and an official
  ThinLTO+PGO build. Never a model-written JS runtime or raw CDP for agents.
  Speed is proven with measured calls and wall time on a real form flow.
- **Navigate on HTTP errors** (#401): `ok (HTTP 504 …)`, exit 0, because the tab
  shows the server's page. A server that doesn't answer in 15 s
  (`--timeout`) gives `Koan.timeout`.
- **URL pill is Zen's** (#400): pinned extension actions with badges, Copy link
  with its toast, and an Extensions (sliders) button that opens Chromium's
  extensions menu. Copy link and Extensions fade in on sidebar hover and hide
  on an empty tab or while editing. Site permissions stay on the favicon's site
  panel.
- **Top-row controls are client area** (#400): Koan's views in the vertical
  strip's top row go through `koan::IsSidebarClientArea`, or the Mac turns a
  press into a window drag. The empty part of the row still drags and zooms.
  UI DevTools clicks skip this hit test, so new controls near the window top
  need a hit-test browser test and a real-click Mac step.
- **No floating sidebar corners in compact mode** (#399): upstream's corner views
  don't follow the slid strip and showed as a dark notch on toggle and peek.
  A docked sidebar keeps them.
- **Official build** (approved 2026-11-20): ThinLTO+PGO in `out/KoanOfficial`,
  built on the Mac, offered to the user as a stable update only.
- **Auto-resume after the user stops using a tab** (user, 2026-11-20): when the
  user clicks or types in a tab an agent drives, the agent pauses, and now
  carries on by itself once they stop, from where they left the page. Left
  the tab (another tab, window or app, minimised): after N s without input
  (Settings > Agent Control, "When you stop using a tab, the agent continues
  after [5] s", 2 to 60 s, on by default; off is "Wait for Hand back"). Still
  on the tab: after N s the pill counts down "<agent> continues in 5s · Keep
  paused · Hand back", then resumes. Mouse movement over the page restarts
  the wait but never pauses. Only a pause from input in the page resumes by
  itself; Take over, Keep paused, focus in a password or payment field, a
  page asking the user (Koan.userNeeded), an interstitial, a dialog or a
  permission prompt keep it until Hand back. Stop, revocation and closing
  the tab end a pause without a resume. Koan.resumed {reason: "user-idle" |
  "handed-back", pausedMs, url, title, navigated, changed} makes old refs
  stale, and `koan wait <tab> --resumed` returns a fresh snapshot, so the
  agent goes on from the page as it is, in one turn. Principle: agents use
  the browser as the user does.
- **Cheap screenshots that don't cost accuracy** (user, 2026-11-20): models pay
  for images by area (~w×h/784 tokens, at most ~4,784 at 2576 px on the long
  edge), and `koan screenshot` captured device pixels (~4,800 tokens a shot on
  a Retina Mac). Now `--size micro|mini|normal|full` (long edge 320, 640, CSS
  pixels up to 1568, device pixels up to 2576; never enlarged), and the
  default is **normal, at 1×**. `--clip @ref|loc:…|x,y,w,h` crops one element
  (8 CSS px margin) or rectangle, sharp (device pixels) unless `--size` is
  given. The browser draws at the target scale (`Page.captureScreenshot` clip
  and scale), never a full capture shrunk in the CLI. `act --screenshot
  micro|mini|normal` returns one in the same answer. Sizes and clips work on
  the agent's own tabs only (the facade's filter: they resize the view); a
  user's tab is captured whole at device pixels, with a note. The guide's
  rules: read and act from the text snapshot, never from text in a micro or
  mini image; micro or mini only to check state (loaded, a dialog or error,
  layout changed); `--clip` for detail (small text, a price, an error, a
  chart, an image); normal before anything positional; a clip or a normal
  one when a small image is unclear, never a guess. PNG only: JPEG or WebP
  save bytes, not tokens.
- **One capture path for every tab** (user, 2026-11-20): `koan screenshot`
  (every size and `--clip`) and `act --screenshot` use `Koan.captureScaled`
  on the agent's own tabs and the user's alike. It copies the frame the
  compositor already drew, scaled on the GPU to the target size
  (`RenderWidgetHostView::CopyFromSurface` with an output size, as tab
  thumbnails do), and encodes the small bitmap on a worker thread. Why:
  CDP's clip and scale go through device emulation, which lays out and draws
  the page again at a new scale and then restores it. The page can see that
  (devicePixelRatio, resize, media queries, srcset reloads), a user's tab
  would flicker, so #409 had to give a user's tab a full device-pixel capture
  (~4,800 tokens), and encoding a 2880×1800 PNG is slow. The fallback: a
  hidden agent tab gets the lease's capturer and a fresh frame; if no surface
  comes within 1 s, an agent's own tab (which nobody looks at) is drawn by CDP
  with emulation and the answer says so, and a user's tab gets
  `Koan.noSurface`, never emulation. Masks for secret fields paint over the
  bitmap before it is encoded, in the same path.
- **Agents use the user's autofill, and never read the secrets** (user,
  2026-11-20: "I want agents to use my browser exactly the way I do. That
  means agents should be able to use my autofills but should not read those
  values (mainly for passwords)."). `koan autofill <tab> @field --list`
  shows the suggestions Chrome would offer, as the dropdown's labels only
  (never a password, a full card number, a CVC or an IBAN); `--pick N|label`
  (and act's `autofill @ref <pick>` step) fills through Chrome's own autofill
  and password manager, as the user's click on the row would. What Chrome
  asks the user first (Touch ID, the device password, a card's CVC) becomes
  Koan.userNeeded {reason: "autofill-verify"}, "<agent> needs you to verify a
  card", and the agent waits. Logins fill only for this site's own form
  (Koan.offSite). The agent never receives a secret: secret fields read
  "•••• (filled)" in DOM and accessibility answers on every tab; a frame
  holding one Koan filled refuses script and value reads (Koan.secretField)
  until it navigates or the field is cleared, while clicks, keys elsewhere
  and submit still work; screenshots paint over filled card fields; and an
  egress scrubber replaces each value Koan filled (in memory, for the tab's
  life) in every message to the agent, in its JSON, URL-encoded and base64
  forms. Agent tabs get no fill on load and no silent sign-in, so every
  password filled is one Koan knows. Screenshots: `Koan.captureScaled` (every
  `koan screenshot` size, `--clip`, `act --screenshot`) paints over the
  filled card-number, CVC and IBAN fields in the bitmap before encoding
  (`CdpFacade::CollectCaptureMasks()`); a relay client's raw
  `Page.captureScreenshot` and the CDP fallback are masked by decoding and
  re-encoding the PNG. A capture is refused (Koan.secretField) when a filled
  field's box cannot be located.
- **The final payment click is the user's** (maintainer, 2026-11-21, until
  the user decides "ask before paying"): after filling a payment form the
  agent never clicks the last Pay, Place order, Buy or Confirm button, even
  when the user asked it to buy. It stops before that click, tells the user
  what is ready (the amount and the card or account as the page shows them)
  and hands the tab over (`koan release` for a tab of the user's; its own
  tab is left open for them), as section 9.4 hands sign-in and payment
  surfaces to the user. Written as a rule in the `koan skill` guide, not
  enforced in the browser yet; it can become a setting ("let agents pay")
  later.
