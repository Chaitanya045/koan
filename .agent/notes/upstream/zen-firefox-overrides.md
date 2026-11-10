# Zen's overrides of Firefox, mapped to Chromium 158 and Koan

Audit date: 2026-11-05. Zen tag 1.24b (`~/work/koan/.cache/zen-desktop-1.24b`), Chromium 158.0.8127.2 pristine HEAD (`git -C ~/work/koan/chromium/src show HEAD:<path>`), Koan `develop` at dcf9bdaa plus open PRs (#60, #62, #90, #96, #103, #105, #110, #121, #144, #150, #155, #158, #159).

**Why this exists.** Zen is a Firefox fork. To build its own behaviour it had to suppress, lock or replace Firefox's. Each of those overrides points at a Chromium behaviour that Koan probably has to override as well. Past Koan bugs were exactly this kind: the vertical-tab collapse rail, the omnibox WebUI popup with AI Mode, focus mode overriding theme colours, menu items shown again on validation, and Chromium's toolbar in fullscreen.

**Scope.**
- All 273 `src/**.patch` files.
- The prefs in `prefs/{firefox,privatefox,fastfox,zen}/*.yaml` and the shipped `defaults/preferences/firefox.js`. Lines 1560 and later are Zen's, generated from the yaml.
- CSS in `src/zen/**` that hides Firefox UI.
- JS in `src/zen/**` that wraps or replaces Firefox behaviour, plus Zen's own preference pane files.

**Not covered in depth here.**
- Themes, glass and the new tab page: see `themes-glass-ntp.md`.
- Split view, Glance, Library, media controls, pinned tabs, Essentials and folders: see `split-glance-library-media.md`.

Rows in those areas are short and point to those two files.

**How to read the tables.**
- **Src** is where the override lives in Zen: `patch` (a .patch file), `pref` (a yaml pref or firefox.js), or `css/js` (Zen's own code in `src/zen`).
- **Koan status** is one of:
  - `done (file)`: in `//koan` (`~/work/koan/src`) or `~/work/koan/patches`.
  - `covered (ID)`: in `vertical-tabs.md` (V-IDs) or `tab-groups-omnibox-fullscreen.md` (G-, O- and F-IDs).
  - `PR #N`: in an open PR.
  - `partial`, `missing`, `not needed`, or `pointer` (owned by one of the other two audits).
- **Rec.** is what Koan should do:
  - `lock`: force Chromium's state and stop it from changing.
  - `override`: change Chromium's behaviour.
  - `redirect`: send the entry point to Koan's feature instead.
  - `reuse`: Chromium already does it.
  - `not needed`: Mac-only scope, Gecko internals, or Koan doesn't need it.
- **ZP** means `~/work/koan/docs/ZEN_PARITY.md` (line number).
- "Single toolbar" is Koan's default layout, where the Chromium toolbar is hidden and the URL pill sits in the sidebar. "Multiple toolbars" (`Mode::kNone`) is the layout where Chromium's toolbar is visible.
- A row with more than one status is counted by its first one.

## Counts

343 rows:

| Koan status | Rows |
|---|---:|
| done | 101 |
| missing | 67 |
| partial | 53 |
| not needed / n/a | 46 |
| covered | 30 |
| PR | 24 |
| pointer | 18 |
| reuse | 4 |

The grouped table of `zen.*` prefs is counted separately: done 5, partial 6, PR 1, missing 5, not needed 1.

Where the overrides are:
- **About 45 of the 150 non-browser patches** (layout, gfx, dom, servo and widget) are plumbing for **Boosts**, Zen 1.24's per-site recolouring. See Z31.
- **The native drag and window-move services** account for a few more.
- **Most of the rest** is branding and build tooling.
- **The browser/ patches** are where nearly all the user-visible overrides are.

**Prefs by kind.** The tables are organised by area. Read by kind, the prefs fall into three groups:
- **Firefox UI features turned off:**
  - Firefox View, the sidebar revamp, Firefox tab groups and smart tab groups, Pocket and newtab sponsored content.
  - The AI chatbot, ML features, link previews and PDF alt text.
  - Extension recommendations, "More from Mozilla", and telemetry, studies and normandy.
- **Behaviour defaults:** URL bar, tabs, downloads, session restore, close warnings and macOS. These sit in the area tables.
- **Privacy and performance:** privatefox, fastfox and `performance.yaml`. These are in "Privacy defaults" and "Performance".

## Area tables

### URL bar & search


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | `themes/shared/urlbar-css.patch`: anchor-positioned joint background replaced by Zen's popover geometry (floating bar), go/revert buttons `display:none !important`, fade mask while hovering | Firefox 15x's anchored, joined URL bar + view | `omnibox_popup_view_views.cc`, `location_bar_view.cc` | covered (O1, O2, O14); PR #158 | override |
| patch | `urlbar/view-proton-css.patch`: results padding 8px, view margins simplified | Proton view margins | `omnibox_result_view.cc`, `omnibox_match_cell_view.cc` | done (`patches/.../omnibox_result_view.cc.patch`, `omnibox_match_cell_view.cc.patch`) | override |
| patch | `browser-js.patch` (`gDialogBox`): URL bar popovers blocked under a window-modal dialog | Firefox closes the view | omnibox popup closes on blur | n/a | not needed |
| patch | Popover/breakout rewrite, `zen-floating-urlbar` when `zen.urlbar.behavior=floating-on-type` and not focused by mousedown; mousedown handled as click when floating (`urlbar/content/UrlbarInputBase-mjs.patch`) | Firefox's in-place breakout that only grows downwards | Omnibox popup under the location bar; focus path `OmniboxViewViews::OnFocus` (`chrome/browser/ui/views/omnibox/omnibox_view_views.cc`) | done (`src/browser/ui/views/koan_url_bar_controller.cc`); covered (O2, O14) | override |
| patch | Blur: revert to the page URL when empty / single toolbar, then focus the page; `breakout-extend` removed (`UrlbarInputBase-mjs.patch`) | Typed text survives blur | `OmniboxViewViews::OnBlur` keeps user text per tab (`omnibox_view_views.cc:1850`) | done (`koan_url_bar_controller.cc` `OnFocusChanged` -> `Revert()`) | override |
| patch | `browser.urlbar.closeOnWindowBlur=false` (+ guards in `UrlbarInputBase` blur and `UrlbarView.on_blur`) (`UrlbarPrefs-sys-mjs.patch`, `content/UrlbarView-mjs.patch`) | View closes when the window loses focus | Views clears focus on widget deactivation, which closes the popup | done (`koan_url_bar_controller.cc` `OnFocusChanged` early return while inactive) | override |
| patch | New-tab session: `zen-newtab` makes Enter open a tab (`where="tab"`, reuse empty), `setURI` skipped, `focusedViaMousedown` autoOpen (`UrlbarInputBase`, `content/UrlbarChildController-mjs.patch`, `Tabbrowser` `addTab` guard) | Cmd+T opens an about:newtab tab | `chrome::NewTab` / `IDC_NEW_TAB` (`chrome/browser/ui/browser_commands.cc`) | done (`koan_url_bar_controller.cc` `OpenForNewTab`, `MaybeAdjustOmniboxDisposition`; `patches/chrome/browser/ui/omnibox/chrome_omnibox_client.cc.patch`) | redirect |
| patch | `getOpenUILinkWhere`: a pinned tab loading another domain from the bar opens a new tab (`zen.tabs.open-pinned-in-new-tab` true) (`UrlbarInputBase-mjs.patch`) | Current tab navigates | `ChromeOmniboxClient::OnAutocompleteAccept` disposition (`chrome/browser/ui/omnibox/chrome_omnibox_client.cc`) | missing (ZEN_PARITY: "Koan navigates the pinned tab away (#74)") | override |
| patch | `trimURL` -> host only (no `https://`, `www.`, lone `/`) in single toolbar, full URL while open; `UrlbarValueFormatter` keeps the domain visible (`UrlbarInputBase`, `UrlbarValueFormatter-sys-mjs.patch`) | Firefox trims only the scheme | Chromium elides scheme/`www` (pref `omnibox.prevent_url_elisions`) | done (`src/browser/ui/views/koan_sidebar_nav_view.cc` pill shows the host) | override |
| patch | Single-toolbar placeholder `zen-singletoolbar-urlbar-placeholder-with-name` (`UrlbarInputBase-mjs.patch`) | "Search with X or enter address" | `omnibox_placeholder_util.cc`, `omnibox_view_views.cc` | done (`koan_url_bar_controller.cc` `AdjustOmniboxPlaceholder`); covered (A3) | override |
| patch | Tab on an empty input -> commands mode; `ZEN_ACTIONS` source with unlimited results, Zen groups first, heuristic not counted (`UrlbarChildController`, `UrlbarInputBase`, `UrlbarShared-mjs.patch`, `UrlbarPrefs`, `UrlbarProvidersManager-sys-mjs.patch`, `UrlbarMuxerStandard-sys-mjs.patch`, `content/UrlbarResult-mjs.patch`) | Tab moves selection/focus | Tab = keyword mode / focus traversal (`omnibox_view_views.cc`) | done (`src/browser/ui/views/koan_command_palette.cc`; #96 types `>`); covered (S7) | override |
| patch | Restrict token `` ` `` / `WORKSPACES` source, "Spaces" search-mode chip, `shortcuts.workspaces` (`UrlbarShared`, `UrlbarProvidersManager`, `UrlbarPrefs`, `content/enUS-searchFeatures-ftl.patch`) | Only Firefox's `* % ^ # $ >` tokens | Starter pack `@tabs/@bookmarks/@history` (`components/search_engines/template_url_starter_pack_data.cc`) | partial ("Focus on <Space>" palette action; no `` ` `` mode) | override |
| patch | Tab-to-search results dropped; heuristic-fallback search kept beside a URL heuristic (`UrlbarMuxerStandard-sys-mjs.patch`) | Firefox's tab-to-search rows | Keyword hint "Press Tab to search X" | covered (S7, D3) | lock |
| patch | Heuristic fallback also offers "Search for" when the text looks like a URL/origin (`UrlbarProviderHeuristicFallback-sys-mjs.patch`) | Only for non-parsable origin-like strings | `SearchProvider` what-you-typed search row (`components/omnibox/browser/search_provider.cc`) | done (upstream default) | reuse |
| patch | Top sites: hide pinned and sponsored (`zen.urlbar.show-pinned`, `show-sponsored` false) (`UrlbarProviderTopSites-sys-mjs.patch`) | Sponsored/pinned tiles in zero-prefix | Zero-suggest most visited (no sponsored upstream) | PR #158 (O8, `koan_zero_suggest.cc`), PR #96 | reuse |
| patch | One-off engine buttons hidden (`zen.urlbar.hide-one-offs` true) (`search/SearchOneOffs-sys-mjs.patch`) | One-off engine row | None in Chromium's popup | done (none upstream) | not needed |
| patch | View: open on `click`, smooth `scrollIntoView` of the selection, no row wrap under 650 px, stays open without search mode (`content/UrlbarView-mjs.patch`) | Firefox view heuristics | `OmniboxPopupViewViews` (Views popup) | PR #158 (O1); covered (P2) | reuse |
| patch | Ctrl+N / Ctrl+P navigate rows on every platform (`UrlbarChildController-mjs.patch`) | Mac-only | Cocoa `moveDown:/moveUp:` -> `MOVE_DOWN/UP` (`omnibox_view_views.cc:2086-2089`) | done (upstream on Mac) | not needed |
| patch | Shift/Alt action override gated by `zen.urlbar.enable-overrides` (false) (`UrlbarInputBase-mjs.patch`) | Modifier switches "visit"/"search" | None (Alt+Enter = new tab) | done (none upstream) | not needed |
| patch | Search opening elsewhere reverts this bar; Paste & Go closes the view after the command; clicks on menupopup / new-tab button don't close the view (`UrlbarInputBase-mjs.patch`) | Firefox ordering | `OmniboxEditModel::OpenMatch` reverts on non-current dispositions | done (upstream) | reuse |
| patch | Popover blocker count: a window-modal dialog hides the floating bar (`UrlbarInputBase-mjs.patch`) | n/a (Zen's popover) | Tab-modal dialogs (`javascript_tab_modal_dialog_view_views.cc`) | missing (LIVE CHECK) | override |
| patch | Glance on search-select; `searchMode` of `ZEN_ACTIONS` not saved (`search/SearchUIUtils-sys-mjs.patch`, `sessionstore/TabState`) | n/a | see split-glance-library-media.md | n/a pointer (Glance) | not needed |
| pref | `browser.search.suggest.enabled{,.private}`=false (urlbar.yaml) | Remote search suggestions on | `prefs::kSearchSuggestEnabled` "search.suggest_enabled" (chrome/common/pref_names.h:383) | PR #158 (O7) | override |
| pref | `browser.urlbar.trending.featureGate`, `weather.featureGate`=false; `suggest.topsites`=true (locked) (urlbar.yaml) | Trending/weather in empty URL bar | Remote zero-suggest (ZeroSuggestProvider) | PR #158 (O8) | override |
| pref | `browser.urlbar.quicksuggest.enabled`, `suggest.quicksuggest.{sponsored,nonsponsored}` (locked false) (urlbar.yaml) | Firefox Suggest/sponsored results | No sponsored suggestions in Chromium | done (upstream) | not needed |
| pref | `browser.urlbar.scotchBonnet.enableOverride`=false (browser.yaml) | Firefox's new URL bar (search mode switcher, unified button) | WebUI omnibox popup `omnibox::internal::kWebUIOmniboxPopup` + AI Mode button | PR #158 (O1, O5) | lock |
| pref | `browser.urlbar.groupLabels.enabled`=false (urlbar.yaml) | Section headers ("Firefox Suggest") | Omnibox section headers in zero-suggest | PR #158 (Koan's local zero-suggest section) | override |
| pref | `browser.urlbar.quickactions.enabled`=true, `zen.urlbar.suggestions.quick-actions` | Quick actions off | Omnibox actions/pedals | done (koan_command_palette.cc, kCommandPaletteSuggestions) | redirect |
| pref | `browser.urlbar.clipboard.featureGate`=true (urlbar.yaml) | Clipboard suggestion off | ClipboardProvider subtypes (components/omnibox/browser/autocomplete_controller.cc:509) | missing (LIVE CHECK: Zen shows a "visit copied link" row on focus) | reuse |
| pref | `browser.urlbar.trimHttps`, `untrimOnUserInteraction.featureGate`=true (urlbar.yaml) | Showing `https://` | Scheme/trivial-subdomain elision; `omnibox.prevent_url_elisions` (components/omnibox/browser/omnibox_pref_names.h:70) | done (upstream default elides) | reuse |
| pref | `browser.urlbar.closeOnWindowBlur`=false (urlbar.yaml) | Popup closes when window loses focus | Omnibox popup closes on blur (OmniboxViewViews::OnBlur, chrome/browser/ui/views/omnibox/omnibox_view_views.cc) | missing (LIVE CHECK on Koan's floating URL bar) | override |
| pref | `browser.urlbar.keepPanelOpenDuringImeComposition`=true (urlbar.yaml) | IME closing the panel | Views omnibox keeps popup during IME composition | done (upstream) | reuse |
| pref | `browser.urlbar.trustPanel.featureGate`=false (urlbar.yaml) | New "trust panel" site info | Page info bubble | done (upstream page info) | reuse |
| pref | `media.videocontrols.picture-in-picture.urlbar-button.enabled`=true (locked), `zen.urlbar.show-pip-button` | No PiP button in URL bar | No omnibox PiP page action in Chromium | missing | override |
| pref | `browser.search.widget.new`=true (browser.yaml) | Old search bar widget | No separate search box in Chromium | done (n/a) | not needed |
| pref | (Zen welcome picks the engine) | n/a | Search-engine choice dialog, branded only (chrome/browser/search_engine_choice/search_engine_choice_dialog_service_factory.cc:40) | PR #158 (O10) | reuse |
| css/js | `#reader-mode-button`, `.urlbar-go-button`, `#star-button-box`, `#pageActionButton` always hidden; folded into the site-data panel (zen-omnibox.css:731-736) | Firefox's per-page URL bar buttons | Page actions: `chrome/browser/ui/views/bookmarks/bookmark_page_action_controller.cc`, page_action framework (`PageActionController::SetShouldHidePageActions`) | partial (done in pill + floating bar via `koan::HidesPageActionIcons`, location_bar_view.cc.patch; multiple-toolbars layout still shows Chromium's star / reading mode / etc.) | override |
| css/js | Single toolbar: `.identity-box-button`, `.urlbar-page-action`, `.unified-extensions-item` collapsed until the sidebar is hovered or a popup is open (zen-omnibox.css:150-170) | Always-visible page actions | same page_action framework | partial (pill has none; no hover-reveal of them) | override |
| css/js | Single toolbar: `#identity-permission-box > *` except the site-data button collapsed; `#notification-popup-box` hidden unless open (zen-omnibox.css:313-325) | Permission icons/anchors in the URL bar | Permission chips / content-setting icons in `location_bar_view.cc` | partial (pill shows none; prompt anchors, see Permissions) | override |
| css/js | `#identity-icon-label` hidden (zen-omnibox.css:100) | Text label in the identity box (extension name, "Firefox" on chrome pages) | `LocationIconView` label ("Not secure", "Dangerous", chrome:// name): `chrome/browser/ui/views/location_bar/location_icon_view.cc:117` | partial (pill has no label; multiple-toolbars bar shows Chromium's chip text) | override |
| css/js | `#tracking-protection-icon-container` hidden (`zen.urlbar.show-protections-icon`=false; zen-omnibox.css:505-508) | Tracking-protection shield | Cookie controls / tracking-protection page action: `chrome/browser/ui/views/location_bar/cookie_controls/cookie_controls_bubble_coordinator.cc:23` | partial (hidden in pill/floating; shown in multiple toolbars) | lock |
| css/js | `#picture-in-picture-button` hidden (`zen.urlbar.show-pip-button`=false), `#userContext-icons` / `#userContext-label` / `.action-contextualidentity` hidden (zen-omnibox.css:213-216, 391, 694, 739-742) | PiP and container indicators in URL bar | none (no omnibox PiP button, no containers) | n/a | not needed |
| css/js | `.search-panel-one-offs-header`, `#urlbar-search-mode-indicator-close` hidden; `#urlbar-results` hidden when empty (zen-omnibox.css:448, 714-716) | One-off search header, search-mode chip close | keyword-mode chip in `omnibox_view_views.cc`; no one-offs | covered (O1 Views popup) | not needed |
| css/js | `#zen-copy-url-button` added; hidden when disabled/empty tab and per `zen.urlbar.single-toolbar-show-copy-url` (zen-omnibox.css:190-209) | (adds Zen button) | none | done (`koan_sidebar_nav_view.cc` copy_button_) | reuse |
| css/js | `registerZenUrlbarProviders()`: `ZenUrlbarProviderGlobalActions` (HEURISTIC, commands, `>`-mode) (urlbar/ZenUBProvider.sys.mjs:15, ZenUBActionsProvider.sys.mjs:140-175) | Firefox's own heuristic result first | `AutocompleteController::InitializeAsyncProviders` `components/omnibox/browser/autocomplete_controller.cc:1357` | partial (`koan_command_palette.cc` + autocomplete_controller.cc.patch; ZP 164-167 "Different") | override |
| css/js | `ZenUrlbarProviderSidebar`: matches pinned tabs and folders from Zen's session object, all Spaces, even unloaded (ZenUBSidebarProvider.sys.mjs:25-60) | Firefox only matches open tabs of this window set | `TabMatcherDesktop::IsTabOpenWithURL` `chrome/browser/autocomplete/tab_matcher_desktop.cc:64` (Spaces are real tabs so "Switch to tab" already sees them) | partial (tabs via upstream switch-to-tab; folder results missing) | reuse |
| css/js | `ZenUBResultsLearner` reorders commands by usage, stores in a pref (ZenUBResultsLearner.sys.mjs:46-60) | Static ordering | same provider stack | partial (ZP 166 "Different") | override |
| css/js | `handleNewTab()`: Cmd+T / New Tab opens the floating URL bar instead of a tab (`zen.urlbar.replace-newtab`=true; ZenUrlbarController.mjs:83-160) | New tab creation | `case IDC_NEW_TAB:` `chrome/browser/ui/browser_command_controller.cc:785` | done (`koan::MaybeOpenUrlBarForNewTab`, ZP 53); tab-menu path covered (O12) | redirect |
| css/js | `gURLBar._setPlaceholder(null)` (ZenUrlbarController.mjs:50) | "Search with X or enter address" placeholder | `omnibox::ComputePlaceholderText` in `omnibox_view_views.cc` | done (omnibox_view_views.cc.patch `koan::AdjustOmniboxPlaceholder`) | override |
| css/js | `trimURL()`: domain only, no `www.`, in single toolbar (`zen.urlbar.show-domain-only-in-sidebar`; ZenUrlbarController.mjs:371-385) | Full trimmed URL | `url_formatter::FormatUrl` | done (`koan_sidebar_nav_view.cc:78-82`) | override |
| css/js | `getOpenUILinkWhere()`: URL bar load on another domain from a pinned tab opens a new tab (`zen.tabs.open-pinned-in-new-tab`=true; ZenUrlbarController.mjs:395-415) | Loads in the pinned tab | `ChromeOmniboxClient::OnAutocompleteAccept` `chrome/browser/ui/omnibox/chrome_omnibox_client.cc:947` | missing (ZP 129) | override |
| css/js | `enableCommandsMode()`: Tab in empty bar enters ZEN_ACTIONS search mode (ZenUrlbarController.mjs:336-360) | Tab moves focus | omnibox keyword mode | partial (Koan types `>`, ZP 167) | override |
| css/js | Search-mode "bounce" animation on `Zen:UrlbarSearchModeChanged` (ZenUrlbarController.mjs:307-330) | none (cosmetic) | keyword chip | missing (cosmetic) | override |

### Session store & restore


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | `BrowserWindowTracker-sys-mjs.patch`, `URILoadingHelper-sys-mjs.patch` (`openInWindow`): synced/unsynced window flags | Every window has the same session model | none (windows are independent) | missing (PR #90 body: "Koan has no window sync") | not needed (until Smart/Classic windows) |
| patch | `URILoadingHelper-sys-mjs.patch` (`switchToTabHavingURI`): searches every Space's browsers and switches Space; the empty tab is never reused | Switch-to-tab in visible tabs only; Firefox View tab skipped | `browser_navigator.cc` (`SWITCH_TO_TAB`); Space follows the active tab via focus mode | covered (O9; Space switch via `kTabGroupsFocusing`, #159) | reuse |
| patch | `browser-js.patch` `warnAboutClosingWindow`: counts visible, loaded tabs | Counts every open tab (also other Spaces, unloaded) | Chromium never warns on window close; ⌘Q hold-to-quit pref is off by default | n/a (Zen ships `browser.startup.page=3`, so the quit prompt doesn't fire) | not needed |
| patch | `willAutoRestore`, `isAutomaticRestoreEnabled`, `willRestore` forced `true` (`sessionstore/SessionStore-sys-mjs.patch`, `SessionStartup-sys-mjs.patch`) | Restore only with `browser.startup.page=3` or resume-once | `SessionStartupPref::GetDefaultStartupType` (`chrome/browser/prefs/session_startup_pref.cc:68`) | done (`patches/chrome/browser/prefs/session_startup_pref.cc.patch`; default only, the user can still pick another On startup) | override |
| patch | macOS: reopening after the last window closed restores the **whole** closed window (removed `AppConstants.platform == "macosx" \|\|`; `ZenSessionStore.onRestoringClosedWindow`) (`SessionStore-sys-mjs.patch`) | Firefox Mac reopens only the pinned tabs | Dock click with no windows -> `CreateBrowser` empty window (`chrome/browser/app_controller_mac.mm:1916-1925`); `AttemptSessionRestore` (`:278`) only for hidden-login-item / app-shim | missing | override |
| patch | Zen per-tab/window fields (`zenWorkspace`, `zenEssential`, `zenPinnedIcon`, static label/icon, glance ids; group `pinned/essential/splitView`; window `spaces`, `folders`, `splitViewData`, `activeZenSpace`) (`TabState-sys-mjs.patch`, `TabGroupState-sys-mjs.patch`, `SessionStore`) | n/a | `SessionServiceBase::BuildCommandsForTab/ForBrowser` (`chrome/browser/sessions/session_service_base.cc`) | done (`src/browser/ui/views/koan_session_data.cc`, `patches/chrome/browser/sessions/*.patch`) | override |
| patch | Zen's own session file hooks (`readFile`, `saveState`, `onFileRead`, `onCrashCheckpoints`, `onNewEmptySession`, `restoreNewWindow`) (`SessionFile`, `SessionSaver`, `SessionStartup`, `SessionStore` patches) | Single sessionstore file | Session service command log | done (Koan data rides in Chromium's session, `koan_session_data.cc`) | reuse |
| patch | Flush/collect every Space's browsers (`allUsedBrowsers`, `allStoredTabs`), extra save triggers `TabMove`, `TabGroupMoved`, essentials, `ZenWorkspaceDataChanged` (`TabStateFlusher-sys-mjs.patch`, `SessionStore`) | Only visible tabs' browsers | SessionService observes the whole TabStripModel | done (upstream; Spaces are groups) | not needed |
| patch | Duplicate tab: unpinned, not Essential, no sync id/glance, in the **active** Space (`SessionStore` duplicate path) | (Firefox also unpins; Zen clears its fields) | `DuplicateTabAt` keeps pinned (`chrome/browser/ui/browser_commands.cc:397-405`) | missing (Koan keeps Essentials pinned: `koan_pinned_tabs.cc:1207`) | override |
| patch | Undo close tab restores `pinned`, `essential`, original Space (`SessionStore` `undoCloseTab` path) | Firefox loses essential/space | `was_pinned_at_time_of_removal` (`browser_commands.cc:510`) + group | done (`patches/chrome/browser/ui/browser_live_tab_context.cc.patch`, `browser_tabrestore.cc.patch`) | reuse |
| patch | Pinned/grouped tabs are **not** split into saved groups on last-window close (`pinned && false`, `groupId && false`), saved-group dedupe off (`SessionStore`) | Firefox converts open groups to saved groups | Saved tab groups | done (`patches/chrome/browser/sessions/session_restore.cc.patch` `koan::SavesTabGroups`); covered (G8, G9) | lock |
| patch | Pinned tabs restored lazily (`restore_pinned_tabs_on_demand` true, `createLazyBrowser`) (`tabbrowser/Tabbrowser-sys-mjs.patch`) | Firefox loads pinned tabs eagerly | `BackgroundTabLoadingPolicy` | done (`patches/chrome/browser/performance_manager/policies/background_tab_loading_policy.cc.patch`) | override |
| patch | "Unsynced" windows (`isZenUnsynced`) kept out of regular-window logic and restored with their flag (`SessionStore`) | n/a | n/a (no window sync) | missing (#75 window sync) | not needed |
| patch | Save even in permanent private browsing (`if (false)` in `SessionSaver`, `SessionStartup`) | No session in always-private mode | Incognito-forced mode has no session | not needed | not needed |
| patch | Infra: `selectedIndex` 0-based, `_zenChangeLabelFlag` on restore titles, empty tab skipped in closed-tab list/home-page cleanup, getters for `#windows` (`SessionStore`) | n/a | n/a | not needed | not needed |
| patch | Prefs: `browser.sessionstore.interval` 40 s, `max_windows_undo` 2 | 15 s / 3 | SessionService save delay, TabRestoreService entries | not needed | not needed |
| patch | `JSONFile` `useSizeHints` and IOUtils `lengthHint` so large session writes don't realloc (`toolkit/modules/JSONFile-sys-mjs.patch`, `external-patches/firefox/issue_14990.patch`) | Full-reallocation JSON writes of sessionstore | SNSS command log (`components/sessions/core/command_storage_backend.cc`) | not needed | not needed |
| pref | `browser.startup.page`=3 (browser.yaml) | Start with home page | `prefs::kRestoreOnStartup` (chrome/common/pref_names.h:180) | done (patches/chrome/browser/prefs/session_startup_pref.cc.patch, kRestoreLastSessionByDefault) | override |
| pref | (side effect of the above) | n/a | `kSessionRestoreInfobar` ENABLED (chrome/browser/ui/ui_features.cc:392); shows when kRestoreOnStartup is at its default and means "continue" (chrome/browser/ui/views/session_restore_infobar/session_restore_infobar_controller.cc:51, _model.cc:44) | missing | lock |
| pref | `browser.sessionstore.restore_pinned_tabs_on_demand`=true (browser.yaml) | Eager load of pinned tabs | Background tab loading policy | done (kKoanRestoreTabsOnDemand; patches/.../background_tab_loading_policy.cc.patch) | override |
| pref | `browser.sessionstore.interval`=40000 (browser.yaml) | 15 s write interval | `kSaveDelay` 2.5 s (components/sessions/core/command_storage_manager.cc:39) | done (upstream is more frequent) | not needed |
| pref | `browser.sessionstore.max_windows_undo`=2 (zen/session-store.yaml) | 3 closed windows kept | `TabRestoreServiceHelper::kMaxEntries` = 25 tabs+windows (components/sessions/core/tab_restore_service_helper.h:70) | missing | not needed |
| css/js | Own `zen-sessions.jsonlz4` sidebar object (tabs, folders, groups, split, spaces) with rolling backups (sessionstore/ZenSessionManager.sys.mjs:44-120, 661) | Per-window sessionstore tabs only | SNSS session service; window extra data | done (`koan_session_data.cc`, session_restore.cc.patch `koan::OnRestoringWindow`) | reuse |
| css/js | `onFileRead`/`onCrashCheckpoints`: when `browser.startup.page`!=3 restore only pinned tabs/Essentials (ZenSessionManager.sys.mjs:333-342, 426-445) | Firefox restores nothing (new tab) | `StartupTabProviderImpl::GetPinnedTabs` `chrome/browser/ui/startup/startup_tab_provider.cc:125` (PinnedTabCodec: only Chromium-pinned = Essentials) | partial (Essentials reopen; Space pinned tabs, folders dropped; ZP 123) | override |
| css/js | `initialState.savedGroups = []` (ZenSessionManager.sys.mjs:370-372) | Saved tab groups restore | SavedTabGroup service | done (session_restore.cc.patch `koan::SavesTabGroups`, saved_tab_group_model_listener.cc.patch); covered (G8) | lock |
| css/js | Always creates a normal window when only private/popup windows were saved (ZenSessionManager.sys.mjs:398-409) | Firefox may open an empty-state window | `startup_browser_creator_impl.cc` | missing (unverified; low) | reuse |
| css/js | `onRestoringClosedWindow`: reopened windows keep only pinned tabs (ZenSessionManager.sys.mjs:561-571) | Full window restore | `TabRestoreService` / `browser_tabrestore.cc` | not needed (no window sync, see next) | not needed |
| css/js | `ZenWindowSync`: pinned tabs, Essentials, Spaces, folders, labels/icons mirrored to every window; the active tab's browser is swapped into the focused window (`swapBrowsersAndCloseOther`) (ZenWindowSync.sys.mjs:24-80, 908-980, 1164) | Independent windows | none (per-Browser `TabStripModel`) | missing (ZP 99, #75) | override |
| css/js | Unsynced windows (`cmd_zenNewNavigatorUnsynced`, Cmd+Shift+N) skipped on restore unless `zen.session-store.restore-unsynced-windows` (ZenSessionManager.sys.mjs:376-388) | n/a | none | missing (ZP 174, with #75) | override |

### Context menus


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | `nsContextMenu-sys-mjs.patch`: adds "Split Link to New Tab" and "Open Link in Glance" for http links (not mailto/tel) | (additive) | `chrome/browser/renderer_context_menu/render_view_context_menu.cc` link section | done (`patches/.../render_view_context_menu.cc.patch`, `koan_glance.h`) | reuse |
| patch | `navigator-toolbox-inc-xhtml.patch`: `#zen-overflow-extensions-list contextmenu="toolbar-context-menu"`, toolbox moved into sidebar | Firefox's toolbar context menu on the tab strip | `chrome/browser/ui/views/frame/browser_widget.cc:400` (`ShowContextMenuForViewImpl`, system menu on caption areas) | done for the sidebar (`KoanSidebarContextMenuController`, see `.agent/notes/zen-behaviour/context-menus.md` §2) | override |
| patch | Page context menu "Copy Link" shows Chromium's toast | Firefox: no toast; Zen routes ConfirmationHint to its toast (`browser-js.patch` `showToast`) | `render_view_context_menu.cc:1257` (`ToastId::kLinkCopied`), `:5450` (`kImageCopied`); `browser_commands.cc:2960` | missing (Chromium-style toast still shows) | redirect |
| patch | Tab menu: split-view and pinned items updated, AI separator / "Ask Chat Summarize" hidden (`tabbrowser/content/tab-context-menu-js.patch`) | Firefox's AI chat items | Glic share `tab_menu_model.cc:250`, AI Mode share `:293`, Read later `:628`, Send to devices `:220` | done (`koan_tab_menu.cc`, `koan_pinned_tabs.cc`) for split/pinned; AI: PR #158 (O5) + covered (A8) | override |
| patch | Toolbar menu: sidebar/vertical-tab toggles and customize separator always hidden; vertical strip detected under `TabsToolbar-customization-target` (`customizableui/ToolbarContextMenu-sys-mjs.patch`) | "Turn on vertical tabs", "Customize sidebar" | Vertical strip toggle/collapse items | covered (A4, A5, A8); done (sidebar context menu, ZEN_PARITY "Toolbar context menu") | lock |
| patch | Tab group label `contextmenu` -> edit modal removed (folders own it) (`tabbrowser/content/tabgroup-mjs.patch`) | Group edit panel | see split-glance-library-media.md (folders) | covered (G4-G6 for Spaces); folders: PR #60 | lock |
| css/js | `#debloatContextMenus()`: hide `context_openANewTab` (only when `zen.view.context-menu.refresh`, which is `@IS_TWILIGHT@` = off in 1.24b) (common/modules/ZenUIManager.mjs:541-552) | "New Tab Below" | `TabMenuModel` `CommandNewTabToRight` | covered (O12 routes it to the URL bar) | not needed |
| css/js | Toolbar context menu: prepend compact-mode submenu (hide sidebar/toolbar/both); add "Tabs on the right" before Customize; container "default profile" submenu (compact-mode/ZenCompactMode.mjs:255-283; common/modules/ZenVerticalTabsManager.mjs:69-106) | Firefox toolbar menu items only | Chromium vertical strip / toolbar context menus | done (`koan_compact_mode_controller.cc:618-627`); containers not needed | reuse |
| css/js | Tab context menu items (pinned / Essentials hide `context_pinTab`, `context_closeTab`, `context_unpinTab`; folders; split; space routing after `context_undoCloseTab`) (tabs/ZenPinnedTabManager.mjs:700-755, folders/ZenFolders.mjs:77, split-view/ZenViewSplitter.mjs:1216, space-routing/ZenSpaceRoutingManager.sys.mjs:40) | Default tab menu | `chrome/browser/ui/tabs/tab_menu_model.cc` | see split-glance-library-media.md (pinned/folders/split); Spaces covered (G5) | — |
| css/js | Page menu: "Open link in Glance", "Split link" (glance/ZenGlanceManager.mjs:222, split-view/ZenViewSplitter.mjs:143) | none | `render_view_context_menu.cc` | see split-glance-library-media.md | — |
| css/js | macOS native context menus/popovers: `widget.macos.native-popovers`=true; app menu, widget panels and overflow become NSPopover, height clamped (ZenUIManager.mjs:15-19, 260-275) | XUL arrow panels | views `MenuItemView` / bubbles | missing (look; patches agent for widget side) | override |

### Menus, keysets & shortcuts


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | `browser-sets-inc-xhtml.patch`: `openFileKb` loses `accel` | ⌘O Open File (Zen uses ⌘O for Expand Glance) | `chrome/browser/ui/cocoa/accelerators_cocoa.mm:68` (`IDC_OPEN_FILE` ⌘O); menu `main_menu_builder.mm:138` | PR #90 (unbound; the menu item gives up its key) | override |
| patch | same: `key_savePage` becomes ⌘⌥⇧S | ⌘S Save Page (Zen uses ⌘S for compact mode) | `accelerators_cocoa.mm:49` (`IDC_SAVE_PAGE` ⌘S); `main_menu_builder.mm:156` | PR #90 | override |
| patch | same: `bookmarkAllTabsKb` loses its key | ⇧⌘D Bookmark All Tabs (Zen uses it for pin) | `accelerators_cocoa.mm:102` (`IDC_BOOKMARK_ALL_TABS`); `main_menu_builder.mm:436` | PR #90 | override |
| patch | same: `key_stop` loses VK_ESCAPE | Esc stops page load (only ⌘. stays) | `chrome/browser/ui/accelerator_table.cc:53` (Esc → `IDC_CLOSE_FIND_OR_STOP`), `browser_command_controller.cc:1126-1131` | PR #90 (hook in `browser_command_controller.cc`) | override |
| patch | same: `viewOpenTabsSidebarKb` removed | ⌃-key "Open Tabs" sidebar panel | none (closest: Tab Search ⇧⌘A, `main_menu_builder.mm:568`, `IDC_TAB_SEARCH`) | covered (vertical-tabs A9: ⇧⌘A redirected) | not needed |
| patch | same: `key_undoCloseWindow` loses ⇧⌘N | ⇧⌘N reopens a closed window (Zen uses ⇧⌘N for New Blank Window) | ⇧⌘T `IDC_RESTORE_TAB` already restores windows (`accelerators_cocoa.mm:48`); ⇧⌘N is Incognito | PR #90 (⇧⌘N new window, ⇧⌘P private) | override |
| patch | same: `key_addTabSplitView` / `key_separateTabSplitView` / `key_duplicateTab` removed; `#include zen-keysets.inc.xhtml` | Firefox's native split-view keys; all of Zen's own keys added | split: see split-glance-library-media.md; Zen keys: Koan's table | PR #90 (`koan_shortcut_table.cc`, Zen's key dump) | override |
| patch | `browser-sets-js.patch`: `toggleSidebarKb` falls back to `SidebarController.toggle()` | With sidebar.revamp off, the key did nothing when no panel was open | Chromium side panel toggle (no default key on Mac) | PR #90 (⌘B bookmarks sidebar; no Firefox-sidebar toggle) | not needed |
| patch | `browser-profiles-js.patch`: profiles menu shown only if `browser.profiles.enabled` (Zen: false) | Firefox's new Profiles menu in the menu bar / app menu | Mac Profiles menu `main_menu_builder.mm:453` (`IDC_PROFILE_MAIN_MENU`); avatar button in toolbar | PR #90 (Profiles menu hidden); avatar in Mode::kNone toolbar not handled | lock |
| patch | `aboutDialog-js/-xhtml/-css.patch`: version "1.24b (Firefox N)", Help / Submit Feedback links removed, Mozilla links point to Zen | Mozilla help, feedback and community links in About | App menu About → `IDC_ABOUT` (`main_menu_builder.mm:54-56`) → chrome://settings/help (Google help/“Report an issue”); Help menu `IDC_FEEDBACK` (:588), `IDC_HELP_PAGE_VIA_MENU` (:599-601) | missing | redirect |
| patch | `CustomKeys` lists Zen's shortcuts and migrates legacy ones (`customkeys/CustomKeys-sys-mjs.patch`, `CustomKeysParent-sys-mjs.patch`) | Firefox's editor shows only Firefox keys | No editor; `chrome/browser/ui/views/accelerator_table.cc`, Mac `chrome/browser/ui/cocoa/main_menu_builder.mm` | done (`src/browser/koan_shortcut_table.cc`, `koan_shortcuts.cc`, koan-settings Keyboard Shortcuts); Mac keys/menu: PR #90 | override |
| patch | Ctrl+Tab MRU panel hides pending (unloaded) tabs (`zen.ctrlTab.show-pending-tabs` false) and the empty tab; no panel with no tabs (`tabbrowser/content/browser-ctrlTab-js.patch`) | Lists every tab | `browser.ctrl_tab_mru` (`chrome/browser/prefs/browser_prefs.cc:2079`) cycles discarded tabs too | partial (`koan_shortcuts.cc:333` reuses upstream MRU; `kCtrlTabSkipPending` only for in-order cycling) | override |
| patch | Tab clicks: Cmd+click no longer bails out early, Cmd+click on close/audio buttons ignored, Alt+click splits (`zen.splitView.enable-tab-click-split` true), double-click an Essential resets it (`tabbrowser/content/tab-mjs.patch`) | Accel+click multiselect, double-click close | Cmd+click toggles selection (TabView mouse handling, `chrome/browser/ui/views/tabs/common/tab_view.cc`) | missing (LIVE CHECK; Alt+click split: see split-glance-library-media.md) | override |
| patch | Tab-strip keydown ignored while a tab is being renamed (`tabbrowser/content/tabs-mjs.patch`) | Keyboard tab moves | n/a | not needed (Koan's inline field owns focus) | not needed |
| patch | Cmd+Opt+Left/Right no longer next/previous tab (Up/Down kept) (`toolkit/modules/ShortcutUtils-sys-mjs.patch`); Zen uses them for Spaces | Firefox Cmd+Opt+arrows tab switching | `global_keyboard_shortcuts_mac.mm:121-122` (hidden IDC_SELECT_NEXT/PREVIOUS_TAB) | done (`koan_shortcut_table.cc:152-153` Space forward/back) + PR #90 (high-priority reserved keys) | override |
| patch | Inspector key moved C → L: Cmd+Opt+L, Ctrl+Shift+L (`devtools/startup/locales/en-US/key-shortcuts-ftl.patch`, `devtools/client/locales/en-US/toolbox-properties.patch`), which frees Cmd+Shift+C for Copy URL | Cmd+Opt+C / Cmd+Shift+C open the inspector | `global_keyboard_shortcuts_mac.mm:148-149`, `accelerators_cocoa.mm:40`, menu `main_menu_builder.mm:355` | done (`koan_shortcut_table.cc:166,191`) + PR #90 (menu key relocation) | override |
| patch | Ctrl+Tab: skip Essentials or pending tabs, and skip glance/empty tabs (`toolkit/content/widgets/tabbox-js.patch`) | Ctrl+Tab cycles every visible tab | `IDC_SELECT_NEXT_TAB` / `TabStripModel::SelectNextTab` | done (`koan_shortcuts.cc` `CycleTabs`, prefs `kCtrlTabWithinKind`/`kCtrlTabSkipPending`) | override |
| css/js | `zenKeyset` (src/browser/base/content/zen-keysets.inc.xhtml): Cmd+S compact, Cmd+Opt+S sidebar, Ctrl+1..0 Spaces, Cmd+Opt+arrows Spaces, Cmd+Shift+K clear, Cmd+Opt+G/V/H/U split, Cmd+Shift+* empty split, Opt+Shift+L Library, Cmd+O Glance expand, Cmd+Shift+D pin, Cmd+Shift+N unsynced window, Cmd+Shift+C / Cmd+Opt+Shift+C copy URL | Firefox keys on those chords | `chrome/browser/ui/accelerator_table.cc` (e.g. :58 Bookmark All Tabs, :75 Save Page) | partial (`koan_shortcut_table.cc`; Mac uses Ctrl where Zen uses Cmd for S/D/K/save page: fixed in PR #90) | PR #90 |
| css/js | Firefox keys unbound/removed (cross-ref, from patches; asserted in tests/kbs/browser_zen_keys.js:66-96): `openFileKb`, `bookmarkAllTabsKb`, `key_stop` (Esc), `key_undoCloseWindow`, Save Page moved to Cmd+Opt+Shift+S, `key_duplicateTab` / split keys removed | Cmd+O, Cmd+Shift+D, Esc-stop, Cmd+Shift+N, Cmd+S | `IDC_OPEN_FILE`, `IDC_BOOKMARK_ALL_TABS`, `IDC_STOP`, `IDC_SAVE_PAGE`, `IDC_NEW_INCOGNITO_WINDOW` (accelerator_table.cc, browser_command_controller.cc) | PR #90 (menu keys + IDC_STOP/OPEN_FILE/BOOKMARK_ALL_TABS handled) | override |
| css/js | Shortcut customization moved to Firefox's `CustomKeys` (about:keyboard) with Zen groups; legacy JSON migrated (kbs/ZenKeyboardShortcuts.sys.mjs:20-80, 240-330) | Zen's old own editor | none (no shortcut editor) | done (chrome://koan-settings Keyboard Shortcuts, ZP 211 "Partial") | reuse |
| css/js | `ZEN_LIBRARY_TARGETS`: `Browser:ShowAllHistory`, `key_gotoHistory`, `Tools:Downloads` command events stopped and sent to the Library (common/zen-sets.js:22-26, 138-152) | Firefox Library window / history sidebar | `IDC_SHOW_HISTORY` / `IDC_SHOW_DOWNLOADS` `chrome/browser/ui/browser_command_controller.cc:1316,1327`; menu bar `main_menu_builder.mm:402` | partial (keys go to Library, `koan_shortcuts.cc`; app-menu / menu-bar items still open chrome://history, ZP 446) | redirect |
| css/js | Menu bar: View > Appearance (auto/light/dark), "Collapse pinned tabs" after Sidebar; new Spaces menu after View; "New Blank Window" in File and app menu (common/modules/ZenMenubar.mjs:24-125) | Firefox menu bar | `chrome/browser/ui/cocoa/main_menu_builder.mm` | PR #90 (Appearance, Spaces; Collapse Pinned Tabs and New Blank Window not done) | override |
| css/js | Hide `appMenuRecentlyClosedWindows`, `historyUndoWindowMenu` when window sync is on (ZenMenubar.mjs:128-140) | Reopen closed window | History menu "Recently Closed" `chrome/browser/ui/cocoa/history_menu_bridge.mm:152` | not needed (until window sync #75) | not needed |
| css/js | `#toolbar-menubar` hidden (zen-sidebar.css:32-34) | Win/Linux in-window menu bar | n/a on Mac | n/a | not needed (Mac-only) |

### Preferences/settings

(A2) Panes/sections Zen hides in about:preferences: `zen-preferences.css:403-415` hides **Data collection** (category + group), **Website advertising**

(A2) (category + group), the **Help** button, **Support Firefox**, **Tab group suggestions** (AI), the **sidebar chatbot** fieldset, the `layout`

(A2) subcategory (Browser layout / vertical tabs), the `support` group and the **browser theme** group; `:428-432` hides the **Home** pane while

(A2) `zen.urlbar.replace-newtab` is on. Prefs: `browser.preferences.moreFromMozilla` false (no **More from Mozilla**), `browser.preferences.aiControls`

(A2) false (no **AI controls**), and `config/aiFeatures-mjs.patch` deletes the AI rows for PDF.js alt text, smart tab-group suggestions and link-preview

(A2) key points. Zen keeps **Startup** in General (`main-js.patch` forces `createStartupConfig(false)`) and repeats Default browser + Startup in Tabs & Browsing

(A2) (`preferences-js.patch`).


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | Four rail categories after General: Look and Feel, Tab Management, Keyboard Shortcuts (`about:keyboard`), Zen Mods; panes always built (`preferences/preferences-xhtml.patch`, `preferences-js.patch`, `jar-mn.patch`) | Firefox panes only | chrome://settings sections (`chrome/browser/resources/settings/route.ts`) | done (`src/browser/ui/webui/koan_settings_*`, chrome://koan-settings; Zen Mods: partial, only `kLookCustomCss`) | override |
| patch | Hidden sections (CSS above): data collection, ads, help/support, browser layout, theme group, Home | Firefox shows them | Privacy "help improve" metrics (`privacy_page/personalization_options.html:54`), About (`route.ts:146`), Appearance tab-strip position + theme + home button (`appearance_page.html.ts`; `browser_ui_prefs.cc:127`), Ad privacy | partial (tab-strip position hidden: PR #155 / PR #90 `appearance_page.html.ts.patch`; theme/home rows, ad privacy shown) | lock |
| patch | AI controls off + three AI rows deleted (`config/aiFeatures-mjs.patch`) | Firefox AI controls pane | AI page `route.ts:178` (Gemini `:190`, tab organisation, history search, help me write) | missing (Glic/AIM features off in #96/#158, page itself stays) | lock |
| patch | Startup group kept + "Continue where you left off" for Spaces (`zen.workspaces.continue-where-left-off` false) (`main-js.patch`, `SessionStore` `#isCmdLineEmpty`) | Settings redesign hides Startup | On startup `route.ts:257` | done (upstream On startup, Koan default Continue) | reuse |
| patch | Default browser + Startup duplicated into Tabs & Browsing (`preferences-js.patch`) | n/a | Default browser `route.ts:250` | done (upstream) | reuse |
| patch | Language-change warning label (`main-inc-xhtml.patch`) | n/a | Languages page | not needed | not needed |
| patch | Spaces sync engine + "sync normal tabs" (`config/account-sync-mjs.patch`, `sync-inc-xhtml.patch`, `dialogs/syncChooseWhatToSync-*.patch`, `widgets/sync-engine-list/sync-engines-list-mjs.patch`) | n/a | Chromium sync (no Google keys in Koan) | not needed (ZEN_PARITY: Sync missing) | not needed |
| patch | Subdialogs load `zen-preferences.css` (`preferences-js.patch`) | Firefox dialog look | Settings dialogs | see themes-glass-ntp.md | not needed |
| patch | Sub-dialog close animation (`zen-closing`, 400 ms cap) and a two-rAF wait before showing (`toolkit/modules/SubDialog-sys-mjs.patch`) | Instant close/open of in-content and tab-modal dialogs | `components/constrained_window/constrained_window_views.cc`; Mac sheet animation `native_widget_ns_window_bridge.mm:1683` | missing | override (low) |
| patch | Prompt (alert/confirm/auth) sizing and padding: min 32em × 12rem, 30×26 px padding (`toolkit/components/prompts/content/commonDialog-css.patch`) | Firefox compact prompt layout | `components/javascript_dialogs/views/app_modal_dialog_view_views.cc`, `chrome/browser/ui/views/javascript_tab_modal_dialog_view_views.cc`, `login_view.cc` | missing | override (low) |
| patch | In-content page canvas colour, button padding, toggle accent, menulist and tree colours (`toolkit/themes/shared/in-content/common-shared-css.patch`, `moz-toggle-tokens-css.patch`, `menulist-css.patch`, `tree/tree-css.patch`) | Firefox Proton in-content look | chrome://settings `cr-toggle` / color provider | partial (Koan settings in `webui/koan_settings_*`; Chromium pages keep their look) | override (low) |
| patch | Reader view toolbar restyle (`toolkit/themes/shared/aboutReader-css.patch`) | Firefox reader toolbar chrome | Reading mode is a side panel (`browser_command_controller.cc:1578` IDC_SHOW_READING_MODE) | partial (Koan maps reader key to the side panel, `koan_shortcuts.cc` kReaderMode; Zen's reader is in the tab) | override (see candidates) |
| pref | `browser.settings-redesign.enabled`=true, `browser.preferences.experimental`=true (browser.yaml) | Old settings, Labs hidden | chrome://settings; Chrome Labs (chrome/browser/ui/toolbar/chrome_labs/chrome_labs_utils.cc:142, off on stable only) | partial (webui/koan_settings_*; Labs button LIVE CHECK) | redirect |
| pref | `browser.translations.newSettingsUI.enable`=true (newtab.yaml) | Old translations settings | `translate.enabled` (components/translate/core/browser/translate_pref_names.h:16) | done (upstream) | reuse |
| pref | `intl.multilingual.downloadEnabled`=false (multilingual.yaml) | Language pack downloads | Mac bundles every locale | done (n/a) | not needed |
| pref | `browser.profiles.enabled`=false (browser.yaml) | Firefox's new profile switcher | Avatar button/profile menu; profile picker `prefs::kBrowserProfilePickerShown` (pref_names.h:2559) | partial (single toolbar hides the avatar; multi-toolbar LIVE CHECK) | lock |
| css/js | Zen panes Look and Feel / Tab Management / Mods (zenLooksAndFeel.inc.xhtml, zenTabsManagement.inc.xhtml, zenMarketplace.inc.xhtml) and `Preferences.addSetting` list incl. `zen.view.drag-window-from-content`, `zen.tabs.select-recently-used-on-close`, `zen.tabs.close-on-back-with-no-history`, ctrl-tab rules (zen-settings.js:716-877) | Firefox panes (removals are in patches) | chrome://settings | done/partial (`webui/koan_settings_*`, ZP 211-225); Mods not needed | reuse |
| css/js | Customize mode: `#customization-toolbar-visibility-button`, `#customization-lwtheme-link` (`zen.theme.disable-lightweight`=true), `#overflowMenu-customize-button` hidden (common/styles/zen-single-components.css:23-31, 677-680) | Toolbar visibility, Firefox themes link, "Customize" in overflow | Customize Chrome side panel `IDC_SHOW_CUSTOMIZE_CHROME_TOOLBAR` `chrome/app/chrome_command_ids.h:269` | see themes-glass-ntp.md (themes link); toolbar part missing (low) | lock |

### Toolbar & customization


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | `browser-xhtml.patch`, `browser-box-inc-xhtml.patch`, `navigator-toolbox-inc-xhtml.patch`: navigator-toolbox moved into `#zen-main-app-wrapper`; `#titlebar` holds Essentials, tabs wrapper, media controls and sidebar icons; nav bar in `#zen-appcontent-navbar-container` | Firefox's top toolbox (tabs + nav bar above content) | `BrowserView` layout (`browser_view_tabbed_layout_impl.cc`), vertical tab strip region | covered (vertical-tabs §1, A1-A14); PR #155 | override |
| patch | `navigator-toolbox-js.patch`: middle-click on `#zen-tabs-wrapper` opens a new tab | Middle-click only on Firefox's tab strip | no new-tab-on-middle-click on the vertical strip's empty area | missing | override |
| patch | `navigator-toolbox-js.patch`, `browser-addons-js.patch`, `ExtensionsUI-sys-mjs.patch`: unified extensions panel lives in Zen's site-data panel (`zen-site-data-icon-button`), puzzle button gone, popups positioned by `gZenUIManager.panelUIPosition()` | Firefox's `#unified-extensions-button` and its panel anchored top-right | `chrome/browser/ui/views/extensions/extensions_toolbar_desktop.cc`, `extensions_menu_view.cc`, `extension_popup.cc` (all anchored to the toolbar) | missing (ZEN_PARITY: Extensions section #73; in single toolbar the puzzle and action icons sit in the hidden toolbar) | redirect |
| patch | `browser-pageActions-js.patch`, `browser-places-js.patch`, `main-popupset-inc-xhtml.patch`: edit-bookmark panel anchored to the site-data button, with the tab's favicon | Bookmark panel anchored to the star | `toolbar_view.cc:1297-1305` (`ShowBookmarkBubble` → `GetBubbleAnchor`, `CHECK(bookmark_star_icon)` :1301); `bookmarks/bookmark_bubble_view.cc` | covered (tab-groups-omnibox-fullscreen D1, live check); no redirect | redirect |
| patch | `toolbarbuttons-css.patch`: narrow-window (≤900/800/700px) button padding removed; button size from `--zen-toolbar-button-size`; spring rule also for `#zen-sidebar-top-buttons` | Toolbar buttons shrink in narrow windows | `chrome/browser/ui/layout_constants.cc` (toolbar insets) | done (`patches/chrome/browser/ui/layout_constants.cc.patch`); pointer themes-glass-ntp.md | reuse |
| patch | Toolbar contents in Mode::kNone | Zen's nav bar has no Firefox account / profile / app-menu extras | Chromium toolbar: avatar, ⋮ app menu, side-panel and tab-search buttons (`toolbar_view.cc`) | missing (no audit row) | lock |
| patch | Default placements: nav-bar without home, downloads, account (fxa) buttons; tab strip without new-tab, all-tabs, AI toggles; vertical defaults empty (`customizableui/CustomizableUI-sys-mjs.patch`) | Firefox's default buttons | `toolbar_view.cc`: home `:453` (pref `browser_ui_prefs.cc:127`, default off), Glic `:494/:588`, extensions `:507`, pinned actions `:517`, media `:561`, avatar `:603`, overflow `:613`, app menu `:627` | partial (single toolbar hides them: `koan_url_bar_controller.cc` `HideToolbarSiblings`; multiple-toolbars layout still shows avatar/media/pinned actions) | override |
| patch | `zen-sidebar-top-buttons` area: nav-bar widgets go to the sidebar's top row in single toolbar; extension buttons overflow there (`zen.view.overflow-webext-toolbar`, 55 %); overflow sizing hacks (`CustomizableUI`, `content/panelUI-js.patch`) | Overflow `»` panel | `ToolbarController` overflow (`chrome/browser/ui/views/toolbar/toolbar_controller.cc`), `ExtensionsToolbarContainer` | partial (nav buttons done: `koan_sidebar_nav_view.cc`; extension buttons missing, #73) | override |
| patch | Unified extensions panel anchored to `zen-site-data-icon-button` (`CustomizableUI`) | Puzzle button panel | `ExtensionsMenuView` anchored to `ExtensionsToolbarButton` | missing (#78 site panel extensions section) | redirect |
| patch | Customize mode hides `tabbrowser-tabbox`, `zen-workspace-icons` customizable (`customizableui/CustomizeMode-sys-mjs.patch`) | Firefox customize page | Customize Chrome side panel > Toolbar (`chrome/browser/ui/webui/side_panel/customize_chrome/customize_chrome_ui.cc`) | missing (Chromium's toolbar pinning still offered, does nothing visible in single toolbar) | lock |
| patch | App-menu notification popup `after_end` / `panelUIPosition` (`content/panelUI-inc-xhtml.patch`, `panelUI-js.patch`) | Anchored bottom-right | `AppMenuButton` badges/bubbles | not needed | not needed |
| patch | AI window's hamburger move disabled (`aiwindow/ui/modules/AIWindow-sys-mjs.patch`) | AI window moves the app menu | Glic | covered (A8) | lock |
| patch | Firefox sidebar ordered inside `tabbrowser-tabbox` (`sidebar/browser-sidebar-js.patch`) | Sidebar beside the tabbox | Side panel in `BrowserView` (`chrome/browser/ui/views/side_panel/`) | partial (LIVE CHECK whether the side panel sits inside Koan's content card) | override |
| patch | A panel or popup-notification anchored in `#navigator-toolbox` forces the compact-mode toolbox visible; a URL-bar-anchored one sets `has-popup-open` (`toolkit/content/widgets/panel-js.patch`, `toolkit/modules/PopupNotifications-sys-mjs.patch`) | Panels open anchored to a hidden toolbar | Bubbles anchor to views (`BubbleDialogDelegateView`); permission prompts anchor to the location bar | partial (`koan_compact_mode_controller.cc:1406-1434` keeps the toolbar for hover/focus/menus, not for open bubbles) | override |
| patch | Infobars: transparent, borderless, top border only (`toolkit/content/widgets/infobar-css.patch`) | Firefox's boxed coloured infobar | `chrome/browser/ui/views/infobars/infobar_view.cc` | missing (look) | override (low) |
| patch | Tab-strip arrowscrollbox never fires overflow/underflow; wheel handling during workspace swipe; `connectedMoveCallback` no-op (`toolkit/content/widgets/arrowscrollbox-js.patch`) | XUL overflow arrows / scroll on the tab strip | vertical tab strip scroll view | covered (vertical-tabs.md) | not needed |
| patch | Findbar ignores the Zen empty tab (`toolkit/content/widgets/findbar-js.patch`) | Find bar opens on the placeholder tab | `FindBarController` | not needed (no Zen empty tab; see zen-behaviour close-last-tab-and-empty-tab.md) | not needed |
| pref | `browser.toolbars.bookmarks.visibility`="never" (browser.yaml) | Bookmarks toolbar on NTP | `bookmark_bar.show_on_all_tabs` (components/bookmarks/common/bookmark_pref_names.h:41) | done (patches/chrome/browser/ui/bookmarks/bookmark_bar_controller.cc.patch, kBookmarksBarShow) | override |
| pref | `sidebar.revamp` locked off: Firefox sidebar panels (browser.yaml) | Bookmarks/history/AI sidebar | Side panel (reading list, bookmarks, history, Lens, Glic) | partial (Glic/Lens PR #158; reading-list/bookmarks side panel untouched, Library replaces them) | redirect |
| pref | `browser.nova.enabled`, `pdfjs.enableNova` (locked false) (browser.yaml) | Firefox's 2026 "Nova" redesign | Chromium's own refresh features (e.g. glow-up menus, vertical-tabs A7) | covered (vertical-tabs A7) | lock |
| pref | `browser.shell.customIcon.enabled`=false (browser.yaml) | Custom app icon | None (Dock icon fixed) | done (n/a) | not needed |
| pref | `extensions.getAddons.showPane`, `extensions.htmlaboutaddons.recommendations.enabled`=false (disablemozilla.yaml); `xpinstall.signatures.required`=true (extensions.yaml) | Add-on recommendations; unsigned add-ons | chrome://extensions has no recommendations; off-store installs blocked on Mac | done (upstream) | reuse |
| css/js | `ZenCustomizableUI.startup`: registers `zen-sidebar-top-buttons` and `zen-sidebar-foot-buttons` CUI areas, Library widget; moves `new-tab-button` to the foot; strips `overflows` from stop-reload (common/sys/ZenCustomizableUI.sys.mjs:30-50, 316-328) | Firefox nav-bar / TabsToolbar layout | toolbar `ToolbarView` + pinned actions | done (`koan_sidebar_nav_view.cc`, `koan_space_bar.cc`); user customization of these rows missing | override |
| css/js | `#zenInitBrowserLayout()`: moves `#nav-bar` and `#PersonalToolbar` out of the toolbox, notification deck and `#notifications-toolbar` above `#browser` (common/modules/ZenStartup.mjs:41-71) | Toolbox stacking | `BrowserView` top container | done (Koan layout; single toolbar hides toolbar) | override |
| css/js | `#unified-extensions-button` hidden (`zen.theme.hide-unified-extensions-button`=true); extensions list shown inside the site-data panel; empty-state / discover hidden (zen-single-components.css:296-334, 550-561; zen-overflowing-addons.css) | Extensions puzzle button | `ExtensionsToolbarButton` `chrome/browser/ui/views/extensions/extensions_toolbar_button.h:26` | missing (ZP 57/176, #73: extension popups unreachable in single toolbar) | redirect |
| css/js | `#firefox-view-button`, `#ai-window-toggle` hidden (zen-single-components.css:49-53) | Firefox View, AI window | tab search / side panel / Glic toolbar buttons | covered (A9 tab search; O5 AI off) | lock |
| css/js | `#alltabs-button` hidden outside customize; `#tabs-newtab-button`, `#vertical-tabs-newtab-button` hidden (zen-browser-ui.css:279-281; vertical-tabs.css:335, 1083) | List-all-tabs, Firefox new-tab buttons | vertical strip top container new-tab / tab-search buttons | covered (A9); PR #155, PR #150 (New Tab row) | lock |
| css/js | `#PanelUI-fxa-menu-services`, `#PanelUI-fxa-cta-menu` hidden (zen-single-components.css:38-42) | Mozilla account promos in app menu | Sign-in / sync rows `chrome/browser/ui/toolbar/app_menu_model.cc:413` (`IDC_SHOW_SIGNIN`) | missing (⋮ menu only reachable in multiple toolbars) | lock |
| css/js | `.private-browsing-indicator-with-label` hidden (zen-single-components.css:34-36) | "Private browsing" label in toolbar | Incognito avatar chip `chrome/browser/ui/views/profiles/avatar_toolbar_button.cc` | partial (no toolbar in single toolbar; shown in multiple toolbars) | override |
| css/js | `#widget-overflow-mainView > toolbarseparator` hidden, overflow list reversed (zen-single-components.css:677-684) | Overflow panel look | extensions overflow menu | see themes-glass-ntp.md | — |
| css/js | `ZenUIMigration` v?: Library button replaces downloads button once (common/sys/ZenUIMigration.sys.mjs:60-90); Welcome moves extension widgets to `AREA_ADDONS` (welcome/ZenWelcome.mjs:184-188) | Downloads toolbar button | download toolbar button | done (Library button in footer, ZP 186) | redirect |
| css/js | Status panel restyled as a pill (`zen.theme.styled-status-panel`=true on macOS; zen-single-components.css:262-275) | Firefox status panel look | `StatusBubbleViews::StatusBubbleViews` `chrome/browser/ui/views/status_bubble_views.cc:728` | missing | override |

### Window chrome, title bar & macOS widget


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | `themes/osx/browser-css.patch`: `.titlebar-buttonbox` margin `12px 4px` | Firefox's traffic-light margin (12px 0) | `browser_frame_view_mac.mm`, `browser_native_widget_mac.mm` (traffic-light placement) | done (`koan_lights_geometry.cc`, `koan_window_controls.cc`) | override |
| patch | `themes/shared/browser-shared-css.patch`: macOS buttonbox `order: -2` | Lights after the first toolbar item | same | done (lights first in the sidebar's top row; ZEN_PARITY row 20) | override |
| patch | `browser-fullScreenAndPointerLock-js.patch`: no `translate` on the toolbox when the macOS menu bar slides down in fullscreen | Toolbox shifts down by `_menubarShift` | `immersive_mode_controller_mac.mm` (menu-bar reveal offset), `browser_frame_view_mac.mm` | covered (F5, F9; `browser_frame_view_mac.mm.patch` → `TOOLBAR_NONE`) | override |
| patch | `browser-init-js.patch`: loads `zenThemeModifier.js` | (additive) | see themes-glass-ntp.md | see themes-glass-ntp.md | n/a |
| patch | `themes/windows/browser-css.patch`, `themes/linux/browser-css.patch`, `app/splash-rc.patch` | Windows accent titlebar, Linux CSD background, Windows manifest | n/a | n/a | not needed (Mac-only) |
| patch | Whole-window `NSVisualEffectView` wrapper `ZenWindowMaterialView`, material pref (default 1 = HUDWindow), behind-window blending (`widget/cocoa/nsCocoaWindow-mm.patch` `SetWindowClass`/`UpdateWindowMaterial`; `prefs/zen/macos.yaml` `zen.widget.macos.window-vibrancy` true) | Opaque Gecko window, vibrancy only in specific `-moz-appearance` regions | No window-level vibrancy; `native_widget_ns_window_bridge.mm:115` only handles an existing `NSVisualEffectView` | PR #103 | override |
| patch | `zen.view.grey-out-inactive-windows` (true): effect state `FollowsWindowActiveState`, otherwise forced Active (`nsCocoaWindow-mm.patch`, `widget/cocoa/VibrancyManager-mm.patch`) | Firefox vibrancy regions always follow window state | Inactive frame colour `browser_frame_view.cc:253-262` (`kColorFrameInactive`) | PR #103 (effect view follows active state; rest via #80) | override |
| patch | `Zen:BoostEditor` window becomes a non-activating panel that never becomes main (`nsCocoaWindow-h/mm.patch` `setZenNeverMain`, `canBecomeMainWindow`) | Every XUL top-level window can become main and steal the main-window look | `native_widget_mac_nswindow.mm:668` `canBecomeMainWindow` (NO only for child widgets) | missing (Boosts not ported) | not needed (until Boosts) |
| patch | Keep the opener's active look while a `zen-keep-opener-active` child window is raised (`dom/base/nsFocusManager-cpp/h.patch`) | Raising any window deactivates the previous browser window | Views `Widget::ShouldPaintAsActive` / `paint_as_active_lock` | missing (Boosts only) | not needed |
| patch | Dialog windows with `eDocumentWindowAnimation` get the AppKit document-window animation (`nsCocoaWindow-mm.patch` @5752) | Only top-level windows animate on show | `native_widget_ns_window_bridge.mm:426, 1683` (`NSWindowAnimationBehaviorDocumentWindow`) | reuse (Chromium already animates dialogs) | reuse |
| patch | Expose `tiled` root attribute on macOS via private `_tilingStateController` ivar (`external-patches/firefox/expose_tiled_attribute_to_all_platforms.patch`) | Gecko exposes tiling only on Linux | None: no tiled state in `ui/views` / `remote_cocoa` (grep of `_tilingStateController` finds nothing) | partial (Koan treats a zoomed Mac window as plain move: `koan_window_drag.cc:174-176`) | not needed (see Window dragging) |
| patch | Native NSPopover panels on (`widget.macos.native-popovers`) and empty-rect fix (`external-patches/firefox/native_macos_popovers_fix.patch`) | Firefox draws arrow panels as Gecko popups | Views bubbles (`BubbleDialogDelegateView`), not NSPopover | not needed (look in themes audit) | not needed |
| patch | Drop `CFBundleIconName` so macOS 26 uses the legacy `.icns`, not a Liquid Glass icon (`external-patches/firefox/no_liquid_glass_icon.patch`) | Firefox ships an asset-catalog Liquid Glass icon | `chrome/app/app-Info.plist:150-152` (`CFBundleIconFile` + `CFBundleIconName`) | partial (Koan deliberately ships `branding/mac/AppIcon.icon` for the glass icon; diverges from Zen) | override (product decision) |
| patch | Fullscreen: no patch outside `src/browser/` (borderless fullscreen is in browser CSS/prefs) | n/a | see tab-groups-omnibox-fullscreen.md F1-F13 | covered (F1-F13) | n/a |
| patch | Early skeleton-UI vertical-tabs pref reads `zen.tabs.vertical` (`xpfe/appshell/AppWindow-cpp.patch`) | Skeleton UI (Windows only) reads `sidebar.verticalTabs` | none | not needed (Windows-only) | not needed |
| pref | `widget.macos.native-popovers`=true, `zen.widget.macos.window-vibrancy/material` (zen/macos.yaml) | Non-native panels, opaque window | see themes-glass-ntp.md | PR #103 | override |
| pref | `widget.non-native-theme.scrollbar.style`=2, `.use-theme-accent`=true (browser.yaml) | Default scrollbars; system accent for form controls | Mac overlay scrollbars follow system; form-control accent from CSS `accent-color` | done (upstream on Mac) | not needed |
| pref | `browser.taskbarTabs.enabled`=false, `widget.windows.mica*` (zen/windows.yaml), `widget.gtk.*`, `zen.widget.linux.transparency` (zen/gtk.yaml), `browser.startup.preXulSkeletonUI`=false | Windows/Linux-only features | n/a | done (n/a) | not needed (Mac-only) |
| css/js | `#moveWindowButtons()`: on macOS the `.titlebar-buttonbox-container` is moved into the sidebar's top row, extra copies removed (ZenCustomizableUI.sys.mjs:296-313); titlebar spacers hidden (vertical-tabs.css:272-275) | Traffic lights in the tab strip title bar | `BrowserFrameViewMac::Layout` `chrome/browser/ui/views/frame/browser_frame_view_mac.mm:529` | done (`koan_traffic_lights*.cc/mm`, ZP 20 "Partial") | override |
| css/js | Mono window controls: traffic lights drawn as 3 toolbar-coloured dots, real buttons `opacity:0` until hovered (`zen.widget.mac.mono-window-controls`=true; zen-browser-ui.css:199-224) | Coloured AppKit lights | AppKit standard window buttons (`browser_frame_view_mac.mm`) | missing (noted only in zen-mac-spec.md:673) | override |
| css/js | `#navigator-toolbox` collapsed in DOM fullscreen and popup windows; `#zen-appcontent-navbar-wrapper` collapsed in DOM fullscreen; loading bar hidden in DOM fullscreen (zen-browser-ui.css:23-25; zen-toolbar.css:12-13; zen-single-components.css:813-815) | Toolbox in fullscreen / popups | content fullscreen / popup `Browser::TYPE_POPUP` | covered (F1; A1 popups have no Koan sidebar) | reuse |
| css/js | Zen page-load progress bar (Zen element; `zen.view.enable-loading-indicator`=true) (zen-single-components.css:790-815) | Firefox throbber-only feedback | none | missing (ZP 219, #82) | override |
| css/js | Windows/Linux-only: `windows-captions-fix-*.inc.css`, `@media not (-moz-platform: macos)` buttonbox, GTK reversed placement (compact-mode/windows-captions-fix-*.inc.css; zen-browser-ui.css:183-187) | Win/GTK captions | n/a | n/a | not needed (Mac-only) |

### Window dragging


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | Drag the window from empty page content in the top 5% (`zen.view.drag-window-from-content`, height 5%; `src/zen/window-drag/actors/ZenWindowDragChild.sys.mjs`, `nsZenWindowDragUtils.cpp` interactive-node and cursor checks) | Pages never move the window | Only `HTCAPTION` areas drag: `bridged_content_view.mm:376` | done (`koan_window_drag.cc`, `koan_zen_window_drag.h`, `koan_prefs.cc:46`) | override |
| patch | The OS performs the move: `beginNativeWindowMove` → `[NSWindow performWindowDragWithEvent:]` (`src/zen/drag-and-drop/nsZenNativeWindowMove.mm`), so you get macOS tiling and edge snapping | n/a (new) | Title bar uses `performWindowDragWithEvent` (`native_widget_mac_nswindow.mm:182`); `RunMoveLoop` is a manual `setFrame` loop (`window_move_loop.mm:83,183`) | partial (Koan's Mac path calls `Widget::RunMoveLoop`, `koan_window_drag.cc:265-283`, so there is no native tiling) | override |
| patch | 50 px threshold to start a drag when maximized or `tiled` (`ZenWindowDragParent.sys.mjs:42-51`) | n/a | none | partial (Mac uses 4 px always, `koan_window_drag.cc:174-176`) | override (low) |
| css/js | `ZenWindowDrag` actor: mousedown on non-interactive page area in top `height-percentage`=5% drags the window; 4 px / 50 px when snapped; not in fullscreen / pointer lock (window-drag/actors/ZenWindowDragChild.sys.mjs; ZenActorsManager.sys.mjs:57-72) | Page swallows the press | none in content; title-bar drag `native_widget_mac_nswindow.mm:182` | done (`koan_window_drag.cc`, `koan_zen_window_drag.cc`, ZP 245) | override |
| css/js | Native move via `-[NSWindow performWindowDragWithEvent:]` (drag-and-drop/nsZenNativeWindowMove.mm; ZenWindowDragParent.sys.mjs:31) | Gecko's emulated move | `NativeWidgetNSWindowBridge::RunMoveLoop` `components/remote_cocoa/app_shim/native_widget_ns_window_bridge.mm:1197` (emulated `CocoaWindowMoveLoop`) | partial (Koan uses `Widget::RunMoveLoop`, koan_window_drag.cc:279: no macOS edge tiling / Stage Manager behaviour of a real title-bar drag) | override |
| css/js | Sidebar resize: drag below 60 px turns compact mode on + one-time toast; double-click resets width to 230 px on macOS (ZenCustomizableUI.sys.mjs:14, 130-290) | Plain splitter | `vertical_tab_strip_region_view.cc:199`, `ui/views/controls/resize_area.cc:65` | done for drag-collapse (A11, ZP 28); double-click reset missing | override |

### Drag and drop


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | `ZenDragAndDrop` replaces `TabDragAndDrop`; drag image removed, drop translations and drag-to-pin off, pinned grid move off (`content/tabs-mjs.patch`, `content/drag-and-drop-js.patch`) | Firefox drag image and animations | `TabDragController` drags the real views (`chrome/browser/ui/views/tabs/dragging/tab_drag_controller.cc`) | covered (vertical-tabs A11); pinned drags: PR #150 | reuse |
| patch | Tab dragged out of the window opens an unsynced blank window (`zen.tabs.dnd-open-blank-window`, `replaceTabWithWindow`) (`Tabbrowser`) | Normal new window | `DetachIntoNewBrowserAndRunMoveLoop` (`tab_drag_controller.cc:1808`), `CreateBrowserForDrag` (`:2906`) | partial (a normal Koan window; window sync missing, #75) | reuse |
| patch | Cross-window drop does not adopt the selected tab / split views (window sync handles it) (`drag-and-drop-js.patch`, `Tabbrowser.adoptTab`) | Firefox adopts them | TabDragController attach | not needed (no window sync) | not needed |
| patch | Drops on tabs/pages create splits; folder expand rules on drag (`drag-and-drop-js.patch`) | n/a | see split-glance-library-media.md | pointer | not needed |
| patch | Tab drag image opacity set by Zen (1.0 for tab drags) instead of `DRAG_TRANSLUCENCY` (`widget/cocoa/nsDragService-mm.patch`; GTK twin `widget/gtk/nsDragSessionSource-cpp.patch`) | Firefox's 65%-alpha drag image | Tab drags are not native drags (`tabs/dragging/tab_drag_controller.cc`, tabs move live); web drag image alpha `drag_controller.cc:116` (0.75) | not needed (Chromium model differs) | not needed |
| patch | Multi-tab drag: extra `NSDraggingItem` per extra tab, `NSDraggingFormationStack` (macOS stacks them with a count badge) (`nsDragService-mm.patch`, `nsIZenDragAndDrop.idl addDragImage`) | One drag item only | `TabDragController` drags the selected tabs as live views; `ui/views/cocoa/drag_drop_client_mac.mm` has a single item | missing | not needed (visual; see candidates) |
| patch | Drop "landing": `animatesToDestination` + rewrite of `draggingFrame` so the image slides into the drop slot (`nsCocoaWindow-mm.patch` draggingEntered/performDrag; `nsZenDropLandingCocoa.mm`) | AppKit's default snap-back/vanish on drop | `bridged_content_view.mm:894,909` (no landing) | missing | not needed (visual) |
| patch | GTK drag opacity | (Linux) | n/a | not needed (Mac-only) | not needed |
| pref | `browser.tabs.dragDrop.dragToPin.enabled`, `.multiselectStacking` (locked false), `.moveOverThresholdPercent`=50 (browser.yaml) | Firefox's drag-to-pin zone, stacked multi-drag | Vertical strip drag (`tab_drag_handler`), pinned container drop | covered (vertical-tabs.md; drag owned by Koan sidebar) | override |
| css/js | `window.ZenDragAndDrop extends TabDragAndDrop`: overrides `startTabDrag`, `_animateTabMove`, `handle_dragover`, `handle_drop`, `handle_dragend` (super wrapped in try/catch), `_createDragImageForTabs` (stacked clones + count dot) (drag-and-drop/ZenDragAndDrop.js:63-180, 284, 704, 1147, 1475-1530) | Firefox tab drag visuals/rules | `chrome/browser/ui/views/tabs/dragging/tab_drag_controller.cc` | partial (upstream drag; pinned/Essentials rules in `koan_pinned_tabs.cc:795`) | override |
| css/js | Drag over a Space icon switches Space at once; hold at sidebar edge switches after `zen.tabs.dnd-switch-space-delay`=500 ms (ZenDragAndDrop.js:713-760, 828) | none | tab_drag_controller.cc | missing (ZP 97) | override |
| css/js | Drag tab onto page edge creates split; fake split preview (ZenDragAndDrop.js:913-1040, 1288) | none | upstream split-view drop targets | done (ZP 155 upstream split drop) / see split-glance-library-media.md | reuse |
| css/js | Out of window: drag image becomes the page preview; macOS drop-landing animation (`armDropLanding` -> `NSDraggingInfo.animatesToDestination`, landing rects) (ZenDragAndDrop.js:1043-1145; nsZenDropLandingCocoa.mm) | Plain tab-strip drag image | `TabDragController::DetachIntoNewBrowserAndRunMoveLoop` tab_drag_controller.cc:1808; `content/app_shim_remote_cocoa/web_drag_source_mac.mm` | missing (Chromium moves the real window instead) | not needed |
| css/js | Haptic feedback when the drop target changes (ZenDragAndDrop.js:1600-1730) | none | none (NSHapticFeedbackManager) | missing (ZP 39, #80) | override |
| css/js | Compact mode keeps the sidebar out while a tab drags (`_isTabBeingDragged`, cleared in `handle_dragend`) (ZenDragAndDrop.js:1525-1530) | n/a | — | missing (ZP 71) | override |
| css/js | Sidebar splitter forwards `dragover` to the tab container (ZenCustomizableUI.sys.mjs:226-228) | Drop dead zone on splitter | `vertical_tab_strip_region_view.cc:199` resize area | missing (unverified, low) | override |

### Tab strip (non-vertical)


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | `browser-commands-js.patch` (Back), `browser-js.patch` (`UpdateBackForwardCommands`), `browser-gestureSupport-js.patch` (swipe `canGoBack`): Back with no history closes the tab (`zen.tabs.close-on-back-with-no-history`) | Back disabled when there is no history | `browser_commands.cc:1016-1038` (`CanGoBack` / `ShouldEnableBackButton` include back-to-opener); Mac swipe `chrome_render_widget_host_view_mac_delegate.mm:172-197` uses the same `chrome::CanGoBack/GoBack` | done (`back_to_opener_controller.cc.patch`, `koan_tab_hooks.cc:66`, `kBackToOpener` on) | reuse |
| patch | `browser-gestureSupport-js.patch`: no swipe navigation when the swipe animation isn't supported | Unanimated swipe back/forward | Chromium's history swiper is always animated | n/a | not needed |
| patch | `browser-commands-js.patch` (`openTab`): new tab goes through `gZenUIManager.handleNewTab` (floating URL bar); clipboard search | Opens about:newtab | `browser_command_controller.cc` `IDC_NEW_TAB` | done (`browser_command_controller.cc.patch` `MaybeOpenUrlBarForNewTab`); O3, O12 | override |
| patch | `browser-commands-js.patch` (`closeTabOrWindow`): ⌘W on the empty tab closes the window (not in a split) | ⌘W closes the tab | `browser_commands.cc` `CloseTab`; window closes with its last tab | partial (Spaces get a new tab, `koan_workspaces.cc:1147`; no empty-tab concept, see `.agent/notes/zen-behaviour/close-last-tab-and-empty-tab.md`) | override |
| patch | `tabbrowser/ctrlTab-css.patch`: `--panel-shadow-margin: 0` on the Ctrl+Tab preview panel | Preview panel shadow margin | no Ctrl+Tab panel; MRU switch `browser_commands.cc:1562` (`IsCtrlTabMruEnabled`) | done (`koan_shortcuts.cc:322-336`, prefs `kCtrlTabWithinKind/SkipPending`) | reuse |
| patch | `tabbrowser/tabs-css.patch`: collapsed tab width 48px, attention dot and pinned-tab grayscale removed, Essentials replace `[pinned]` for media icons, new-tab button always shown | Firefox's vertical/pinned tab visuals | see vertical-tabs.md / split-glance-library-media.md | covered (vertical-tabs A1-A14; #150) | override |
| patch | Tab lists span all Spaces; pinned/Essential counts and insert bounds; `ariaFocusableItems`; `allTabs` with glance tabs (`Tabbrowser-sys-mjs.patch`, `content/tabs-mjs.patch`, `content/tab-mjs.patch`) | One strip | One TabStripModel; Spaces are groups | done (`koan_workspaces.cc`, pinned/Essentials code) | reuse |
| patch | New tabs without an index go to the **top** of the Space (`zen.view.show-newtab-button-top` true -> `_numVisiblePinTabsWithoutCollapsed`) (`Tabbrowser` `#insertTabAtIndex`) | Appended at the end | `TabStripModel::DetermineInsertionIndex` (`tab_strip_model.cc:5866`) appends typed tabs | partial (only the "+ New Tab" row: `koan_sidebar_tabs.cc:602`) | override |
| patch | Owner kept when you switch away (`oldTab.owner = null` removed) (`Tabbrowser`) | Firefox forgets the owner on switch | Openers forgotten on user switch (`tab_strip_model.cc:5837-5845`) and typed navigation (`:1411-1417`) | missing | override |
| patch | Tab to select on close: owner, then most recently used (`zen.tabs.select-recently-used-on-close`), never another Space (`Tabbrowser` `_findTabToBlurTo`) | Neighbour | `GetTabIndexAfterClosing` | done (`patches/chrome/browser/ui/tabs/tab_strip_model.cc.patch`, `koan_tab_hooks.cc` `PickRecentlyUsedTab`) | override |
| patch | Last tab: window stays, empty tab selected (`closeWindowWithLastTab` false, `selectEmptyTab`) (`Tabbrowser`) | Window closes | Empty TabStripModel closes the browser | partial (an emptied Space gets a tab back: `koan_workspaces.cc:1147`; the window's last tab: LIVE CHECK) | override |
| patch | Double-click opens a new tab only on `.zen-workspace-empty-space`; middle-click on a group label no longer save-and-closes (`content/tabs-mjs.patch`) | Double-click anywhere in the scrollbox | No double-click handler in the vertical strip; empty strip is caption (`tab_strip_view.cc:494`) | missing (LIVE CHECK) | override |
| patch | Open/close animations, label updates deferred for hidden Spaces, `unread` on background loads, progress-listener `try` (`Tabbrowser`) | n/a | n/a | covered (vertical-tabs audit, animations); rest not needed | not needed |
| patch | Unloader and tab switcher see every Space, skip Glance/split tabs (`TabUnloader-sys-mjs.patch`, `AsyncTabSwitcher-sys-mjs.patch`) | Visible tabs only | Memory Saver / freezing | done (`koan_prefs.cc` Memory Saver, `koan_feature_overrides.cc` freezing) | reuse |
| patch | Empty tab hidden from all-tabs menu, Ctrl+Tab, extensions API; extension-created active tab tells compact mode (`TabsList-sys-mjs.patch`, `extensions/parent/ext-tabs-js.patch`, `ext-browser-js.patch`) | n/a | Tab search, `chrome.tabs` | not needed (no hidden empty tab; NTP: see themes-glass-ntp.md; compact: PR #121) | not needed |
| pref | `browser.tabs.hoverPreview.enabled`=false, `browser.tabs.groups.hoverPreview.enabled`=false (locked) (browser.yaml) | Tab hover previews | Hover cards: `TabView` hover → `UpdateHoverCard` (chrome/browser/ui/views/tabs/common/tab_view.cc:708); `kTabHoverCardImages` (ui_features.cc:303); `prefs::kHoverCardImagesEnabled` (pref_names.h:3057, registered browser_ui_prefs.cc:75) | missing | lock |
| pref | `browser.tabs.groups.enabled`=false (browser.yaml) | Firefox tab groups | Chromium tab groups (Spaces sit on them) | covered (G1-G14), PR #159 | lock |
| pref | `sidebar.revamp`, `sidebar.verticalTabs` (locked false), `sidebar.verticalTabs.dragToPinPromo.dismissed` (browser/disablemozilla.yaml) | Firefox's sidebar + vertical tabs | Chromium vertical tab strip | covered (A1-A14), PR #155 | lock |
| pref | `browser.tabs.closeWindowWithLastTab`=false (browser.yaml) | Window closes with last tab | Window closes when the TabStripModel empties | done (koan_workspaces.cc: a Space's last tab is replaced by a new tab) | override |
| pref | `browser.tabs.unloadTabInContextMenu`=true (browser.yaml) | No "Unload tab" item | Discard (`TabLifecycleUnit`) has no menu item | done (koan_tab_unload.cc, patches/.../tab_menu_model.cc.patch) | override |
| pref | `browser.tabs.fadeOutExplicitlyUnloadedTabs`=true (browser.yaml) | Unloaded tabs look normal | Discarded-tab favicon treatment in TabIcon | partial (koan_pinned_tab_views.cc reads WasDiscarded; LIVE CHECK for unpinned rows) | reuse |
| pref | `browser.tabs.notes.enabled`=false (browser.yaml) | Tab notes | None in Chromium 158 | done (n/a) | not needed |
| pref | (Firefox tab declutter/organization n/a) | n/a | `kTabStripDeclutter` DISABLED (ui_features.cc:69); tab organization rides on model execution | done (default off; PR #158 kills model execution) | reuse |
| css/js | `.tab-close-button` / `.tab-reset-button` hidden except hover; pinned tab close hidden (vertical-tabs.css:712-714, 921-953, 1237-1239) | Always-visible close on selected tab | close button in `chrome/browser/ui/views/tabs/common/tab_view_vertical_layout.cc` | done (tab_view_vertical_layout.cc.patch, hover/focus only); pinned see split-glance-library-media.md | override |
| css/js | `.tab-throbber` hidden (`zen.theme.hide-tab-throbber`=true; vertical-tabs.css:1407-1411) | Loading spinner in tab | `TabIcon::PaintLoadingAnimation` `chrome/browser/ui/views/tabs/tab/tab_icon.cc:339` | missing | override |
| css/js | `.tab-context-line` hidden for default container (`zen.workspaces.hide-default-container-indicator`=true; zen-tabs.css:14-18) | Container colour line | tab group colour line | covered (G-series group colours) | not needed |
| css/js | `::part(overflow-start/end-indicator)` and scroll buttons hidden (vertical-tabs.css:504-507; zen-workspaces.css:414-417) | Arrowscrollbox shadows / scroll buttons | `chrome/browser/ui/views/tabs/horizontal/tab_scroll_button_container.cc`, `vertical_tab_strip_scroll_bar.cc` | covered (A1, horizontal strip never shown) | lock |
| css/js | `#tabbrowser-arrowscrollbox` forced `orient=vertical` (ZenStartup.mjs:107-110) | Horizontal strip | `VerticalTabStripStateController` | covered (A1) | lock |
| css/js | `shouldCloseTabOnBack()` (`zen.tabs.close-on-back-with-no-history`=true; common/modules/ZenCommonUtils.mjs:166-179) | Back does nothing | back_to_opener controller | done (back_to_opener_controller.cc.patch, `koan_tab_hooks.cc`) | override |
| css/js | `zen.tabs.select-recently-used-on-close` (prefs; JS in Tabbrowser patch) | Firefox owner/adjacent selection | `TabStripModel::DetermineNewSelectedIndex` `chrome/browser/ui/tabs/tab_strip_model.cc:806` | done (`koan_tab_hooks.cc`, ZP 222) | override |
| css/js | Tab rename (`.tab-label-container-editing` hidden while editing, tabs/ZenTabRenamer.mjs) | No rename | none | partial (pinned only, ZP 218) | override |

### Downloads


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | (No Zen patch in this scope touches downloads; Zen's are under `components/downloads`.) Chromium's download bubble auto-open | n/a | `download/bubble/download_toolbar_ui_controller.cc` | done (`download_toolbar_ui_controller.cc.patch`, `koan_zen_download_animation.cc`; Library downloads) | override |
| pref | `browser.download.alwaysOpenPanel`=false (browser.yaml) | Panel opens on each download | Download bubble partial view auto-opens: `prefs::kDownloadBubblePartialViewEnabled` (pref_names.h:66, registered true at chrome/browser/download/download_prefs.cc:368) | missing (patch only replaces the started animation) | override |
| pref | `browser.download.autohideButton`=false (browser.yaml) | Button hides when idle | Download button appears only after a download (DownloadToolbarUIController) | partial (single toolbar hides it; Koan routes to sidebar/Library) | redirect |
| pref | `browser.download.manager.addToRecentDocs`=false (browser.yaml) | Windows recent docs | n/a on Mac | done (n/a) | not needed (Mac-only) |
| pref | `zen.downloads.*` | (Zen feature) | Download-started animation | done (koan_download_animation.cc, kDownloadAnimation) | override |
| css/js | `ZenDownloadAnimation`: arc animation from click to Library/downloads button on `onDownloadAdded` (downloads/ZenDownloadAnimation.mjs:27-50) with `browser.download.alwaysOpenPanel`=false | Downloads panel auto-open | `download_toolbar_ui_controller.cc` (bubble auto-show) | done (`koan_zen_download_animation.cc`, download_toolbar_ui_controller.cc.patch `koan::ShowsDownloadAnimation`) | override |
| css/js | `Tools:Downloads` to Library Downloads section (zen-sets.js:25) | Library window | `IDC_SHOW_DOWNLOADS` `browser_command_controller.cc:1327` | partial (key only; see Menus row) | redirect |

### Permissions & site info


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | `browser-siteIdentity-js.patch`: "Clear cookies and site data" no longer closes the panel first; chrome-page label uses `brandShortName`; panel header hidden for chrome/extension/file pages | Identity popup closes before the prompt; "Firefox" short label | `PageInfoBubbleView` (`page_info/page_info_bubble_view.cc`); internal-page header in page info | partial (pill site button opens `PageInfoBubbleView`, `koan_sidebar_nav_view.cc:446-461`) | reuse |
| patch | `identity-block-css.patch`: magnifier on the empty tab, `#urlbar-label-box` chip styling removed, extension/search icons forced | Firefox's "Not Secure" / "Firefox" text chip in the identity box | `location_icon_view.cc:247` (`GetText` → `location_bar::GetSecurityChipText`) | partial (floating bar is always "editing" so no chip; Mode::kNone toolbar still shows "Not secure") | override |
| patch | Permission prompts in single toolbar (Zen: popup notifications anchored to the identity box in the pill) | n/a | `bubble_anchor_util_views.cc:37-103`: location bar chip if drawn, else the app menu, else an empty anchor | missing (`LocationBarView` and chips live in the hidden toolbar) | redirect |
| patch | `WebRTCParent-sys-mjs.patch`: sharing state goes to `gZenMediaController` | Firefox's sharing indicator only | `media/webrtc/media_stream_capture_indicator.cc`; `screen_capture_notification_ui_views.cc:467`; permission dashboard indicator chip | missing (ZEN_PARITY: sharing card missing); see split-glance-library-media.md | redirect |
| patch | `EncryptedMediaParent-sys-mjs.patch`: DRM notification bar never shown | "This site uses DRM" bar | no Widevine in Koan (ZEN_PARITY: DRM missing) | n/a | not needed |
| patch | Identity panel security row not iconic; feature callouts may anchor to the site-data button (`controlcenter/content/identityPanel-inc-xhtml.patch`, `asrouter/modules/FeatureCallout-sys-mjs.patch`) | Firefox identity popup look | `PageInfoBubbleView` | covered (D2) | redirect |
| patch | about:logins CSP `default-src chrome:` (for Zen styles) (`aboutlogins/content/aboutLogins-html.patch`) | Strict CSP | chrome://password-manager | not needed | not needed |
| css/js | `ZenSiteDataPanel` replaces the identity popup: permissions, extensions, share/reader/screenshot/bookmark actions; `#identity-popup-multiView toolbarseparator` hidden (urlbar/ZenSiteDataPanel.sys.mjs; zen-popup.css:251-254) | Firefox identity/permissions popups | `chrome/browser/ui/views/page_info/page_info_bubble_view.cc` | partial (pill opens Chromium page info; action row/extensions differ, ZP 56) | override |
| css/js | Permission request anchor kept on the URL bar box (`#notification-popup-box` shown while open, zen-omnibox.css:322-325) | — | permission prompt bubble anchored to location bar chip | partial (covered F11: toolbar-anchored bubbles when the toolbar is parked) | redirect |
| css/js | `#editBookmarkPanelBottomContent` hidden (zen-panels/bookmarks.css:55-56) | "Show editor when saving" | `chrome/browser/ui/views/bookmarks/bookmark_bubble_view.cc` (no such row) | n/a | not needed |

### Migration/import


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | Arc (Mac+Win) and Dia (Mac) migrators and profile paths (`migration/ChromeMigrationUtils-sys-mjs.patch`, `MigrationUtils-sys-mjs.patch`); Zen also keeps Chrome/Brave/Edge/Vivaldi/Opera import | Firefox lacks Arc/Dia | Mac importer list: Safari `importer_list.cc:87`, Firefox `:139`, bookmarks file `:193` only (no Chromium-family import) | partial (`koan_welcome.cc:1518` runs `IDC_IMPORT_SETTINGS`) | override |
| patch | `ZEN_SPACES` resource: Arc spaces -> Zen Spaces (checkbox, progress string, count) (`migration-wizard-constants-mjs.patch`, `content/migration-wizard-mjs.patch`, `MigrationWizardParent-sys-mjs.patch`) | n/a | none | missing | override |
| patch | Wizard opened with `migratorKey` + `profileId` preselected (from the welcome) (`content/migration-dialog-window-js.patch`, `MigrationWizardChild-sys-mjs.patch`, `migration-wizard-mjs.patch`) | Wizard opens on the default browser | Import dialog (`route.ts:168` importData) | partial (welcome opens the dialog without preselection) | override |
| patch | `onBookmarksImported` post-processing (`MigrationUtils`) | Import shows the bookmarks toolbar | Chromium import turns the bookmark bar on | done (ZEN_PARITY "Bookmarks bar": Koan ignores it, `kBookmarksBarShow`) | lock |
| pref | `browser.migrate.arc.enabled`, `browser.migrate.dia.enabled`=true; `content-modal.import-all.enabled`=false; `zen.migration.bookmarks-as-pinned-folders*` (zen/migration.yaml) | No Arc/Dia importers | Importers: IE, Firefox, Safari, bookmarks file, Edge only (components/user_data_importer/common/importer_type.h:19-29) | missing | override |
| css/js | `ArcProfileMigrator`, `DiaProfileMigrator` (Chromium-profile migrators + Arc sidebar to Zen Spaces/pins/folders, all profiles) (migration/ZenProfileMigrators.sys.mjs:95-240) | Firefox's Chrome-only list | `DetectSafariProfiles` / `DetectFirefoxProfiles` `chrome/browser/importer/importer_list.cc:75,96` (no Arc/Dia, no Spaces) | missing | override |

### Telemetry, updates & default browser


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | Quit warning counts loaded tabs incl. pinned (`BrowserGlue-sys-mjs.patch`) | Counts non-pinned tabs | Mac confirm-to-quit (`chrome/browser/ui/cocoa/confirm_quit_panel_controller.mm`, `browser.confirm_to_quit` `pref_names.h:409`) | not needed | not needed |
| patch | `gZenUIMigration.init` on profile upgrade (`BrowserGlue-sys-mjs.patch`) | n/a | Koan pref migrations | not needed | not needed |
| patch | Update host `updates.zen-browser.app` (`build/moz-build.patch`); updater signing OU (`build/moz-configure/update-programs-configure.patch`); keep `update-settings.ini`/UpdateSettings.framework in partial MARs (`tools/update-packaging/common-sh.patch`) | Mozilla AUS / Mozilla-signed updates | Unbranded Chromium has no Mac auto-updater; `upgrade_detector_impl.cc` only notices an on-disk version change | missing (release build paused) | not needed (now) |
| patch | Windows maintenance-service cert check skipped (`external-patches/firefox/override_cert_checks_temp.patch`); NSIS setup swap; Linux updater rpath | (Windows/Linux) | n/a | not needed (Mac-only) | not needed |
| patch | Update URL / addon compat use `ZEN_FIREFOX_VERSION` not Zen's version (`toolkit/modules/UpdateUtils-sys-mjs.patch`, `AppConstants-sys-mjs.patch`, `toolkit/modules/moz-build.patch`) | Firefox's app version in update/compat URLs | `version_info::GetVersion()` is Chromium 158 already | not needed | not needed |
| patch | Default profile name "Default (release)" / "Default Profile"; profile dir without vendor (`toolkit/profile/nsToolkitProfileService-cpp.patch`, `toolkit/xre/nsXREDirProvider-cpp.patch`, `toolkit/moz-configure.patch` vendor/app id) | `default-release` profile under `Mozilla/Firefox` | `chrome/common/chrome_paths_mac.mm` | done (`patches/chrome/common/chrome_paths_mac.mm.patch`, `koan_branding.h kMacProductDirName`) | override |
| patch | `about:credits` → zen-browser.app/about (`docshell/base/nsAboutRedirector-cpp.patch`) | Mozilla credits page | chrome://credits is the licence list (`webui_url_constants.cc:138`) | not needed (keep licences) | not needed |
| patch | Mac signing: Zen team id, provision profile, `rcodesign --for-notarization` (`security/mac/hardenedruntime/production/firefox-browser-xml.patch`, `tools/signing/macos/mach_commands-py.patch`) | Mozilla's signing identity | `chrome/installer/mac/signing/` | missing (PR #70 builds an unsigned dmg) | not needed (now) |
| patch | Sync engine `Spaces` (`services/sync/modules/service-sys-mjs.patch`) | Spaces not synced by Firefox Sync | Chrome Sync (needs Google keys; `kShowGoogleApiKeysInfoBar` false) | not needed | not needed |
| patch | No default containers on new profiles (`toolkit/components/contextualidentity/ContextualIdentityService-sys-mjs.patch`, `zen.containers.create-default-identities` false) | Personal/Work/Banking/Shopping containers | Chromium has no containers | not needed | not needed |
| pref | `datareporting.*`, `toolkit.telemetry.*` (13 prefs, most locked), `toolkit.coverage.*`, `browser.ping-centre.telemetry`, activity-stream telemetry (privatefox/privacy.yaml) | Telemetry/health-report upload and pings | UMA `metrics::prefs::kMetricsReportingEnabled` (components/metrics/metrics_pref_names.h:134); upload URLs only exist when branded (chrome/browser/metrics/server_urls_browsertest.cc:20) | done (build/koan-mac.gn, unbranded) | not needed |
| pref | `app.normandy.*`, `app.shield.optoutstudies.enabled` (locked), `toolkit.telemetry.pioneer-new-studies-available` (privacy.yaml) | Remote studies/experiments | Variations seed fetch is off unless branded or `--variations-server-url` (components/variations/service/variations_service.cc:285); field-trial testing config off in gn | done (build/koan-mac.gn) | not needed |
| pref | `browser.tabs.crashReporting.sendReport`, `browser.crashReports.unsubmittedCheck.autoSubmit2` (privacy.yaml) | Crash report submission | Crashpad upload needs branded build + metrics consent (kMetricsReportingEnabled) | done (build, unbranded) | not needed |
| pref | `browser.search.serpEventTelemetryCategorization.enabled` (locked), `browser.attribution.enabled` (privacy.yaml) | SERP/install attribution telemetry | No unbranded equivalent (RLZ is branded-only) | done (build, unbranded) | not needed |
| pref | `app.update.checkInstallTime.days` = 6 (browser.yaml, MOZILLA_OFFICIAL) | Update nag timing | Chromium has no updater in unbranded Mac builds | missing (no updater; Zen's `zen.updates.show-update-notification` also unmatched) | not needed |
| pref | `browser.aboutwelcome.enabled`=false, `browser.preonboarding.enabled`=false, `termsofuse.bypassNotification` (disablemozilla.yaml, browser.yaml) | Firefox onboarding/ToU modal | First-run flow + What's New (`whats_new::IsEnabled`, chrome/browser/ui/webui/whats_new/whats_new_util.cc:36: branded only) | done (koan_welcome.cc, kKoanWelcome; What's New off unbranded) | reuse |
| pref | (implied by Zen's own welcome asking once) | Firefox's default-browser nag | Startup infobars `ShowDefaultBrowserPrompt` + `PinInfoBarController` + `kPdfInfoBar` (chrome/browser/ui/startup/infobar_utils.cc:323-361; ui_features.cc:191); `prefs::kDefaultBrowserSettingEnabled` (pref_names.h:760) | missing | override |
| pref | `browser.referrals.enabled` (locked), `browser.preferences.moreFromMozilla` (locked), `browser.vpn_promo.enabled`, `browser.privatebrowsing.vpnpromourl`, `browser.contentblocking.report.{hide_vpn_banner,show_mobile_app}` (disablemozilla.yaml) | Mozilla product promos | Sign-in promos/interception `prefs::kSigninInterceptionEnabled` (chrome/browser/signin/dice_web_signin_interceptor.cc:546); IPH promos | partial (IPH for omnibox/Lens/Glic/tab groups in PR #158 O13, G11; sign-in promos not touched) | lock |
| pref | `browser.uitour.enabled`=false, `browser.newtabpage.activity-stream.asrouter.userprefs.cfr.{addons,features}`=false (privacy/disablemozilla.yaml) | UITour + contextual feature recommendations | Feature Engagement IPH (`feature_engagement::kIPH*`) | partial (PR #158 O13, vertical-tabs A12, G11; no blanket IPH off) | lock |
| css/js | `checkForZenUpdates()`: after a version change, a sidebar notification (release notes / donate) instead of a What's New tab (common/modules/ZenUpdates.mjs:12-45) | Firefox What's New page | `whats_new::ShouldShowForState` `chrome/browser/ui/startup/startup_browser_creator_impl.cc:539` | missing (no override found; verify Chromium doesn't open What's New after an update) | lock |

### Privacy defaults


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| pref | `privacy.globalprivacycontrol.enabled`=true (privacy.yaml) | GPC off by default | `universal_optout::features::kUniversalOptOutSettings` DISABLED (components/universal_optout/features.cc:29) + `universal_optout.enabled` default false (components/universal_optout/prefs.cc:15), read in chrome/browser/renderer_preferences_util.cc:160-167 | missing | override |
| pref | `privacy.userContext.enabled`/`.ui.enabled`=true (privacy.yaml) | Containers hidden | No container isolation in Chromium (nearest: profiles / StoragePartition) | missing (Spaces have no container) | not needed |
| pref | `browser.formfill.enable`=false (urlbar.yaml) | Form history autocomplete | `autofill::prefs::kAutofillProfileEnabled` (components/autofill/core/common/autofill_prefs.h:149) covers single-field autocomplete | missing | override |
| pref | `browser.privatebrowsing.forceMediaMemoryCache`, `browser.helperApps.deleteTempFileOnExit`, `browser.privatebrowsing.resetPBM.enabled`=false (privacy.yaml, browser.yaml) | Private-window disk traces / reset button | Incognito is in-memory by design | done (upstream) | reuse |
| pref | (Zen leaves Firefox Safe Browsing on, via Mozilla's key) | n/a | `safebrowsing.enabled` (components/safe_browsing/core/common/safe_browsing_prefs.h:39); the list updates need a Google API key Koan doesn't ship | partial (pref on; no key means no list updates; LIVE CHECK) | not needed (note) |
| pref | (Firefox has no Privacy Sandbox) | n/a | `kPrivacySandboxM1{Topics,Fledge,AdMeasurement}Enabled` (components/privacy_sandbox/privacy_sandbox_prefs.h:16-24), off until consent | done (upstream defaults off) | reuse |
| pref | `extensions.getAddons.cache.enabled`=false, `browser.discovery.enabled`=false (privacy/disablemozilla.yaml) | Add-on metadata/discovery fetches | Chrome Web Store fetches only on use | done (upstream) | not needed |

### AI / ML features


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| pref | `browser.ml.enable`, `browser.ml.chat.{enabled,shortcuts,shortcuts.custom,sidebar,menu}`, `browser.preferences.aiControls`=false (firefox/ai.yaml) | AI chatbot sidebar/menu, AI settings | `features::kGlic` (chrome/common/chrome_features.cc:528), `kGlicRollout` (:865), `omnibox::kAimEnabled`, Lens overlay, `contextual_tasks::kContextualTasks`, `kOptimizationGuideModelExecution` | PR #158 (O5) | lock |
| pref | `browser.ml.linkPreview.enabled`=false (ai.yaml) | AI link preview | No Chromium 158 desktop link-preview feature found; Compose `compose::features::kEnableCompose` ENABLED (components/compose/core/browser/compose_features.cc:13), gated by model execution | partial (PR #158 turns model execution off; Compose itself not disabled) | lock |
| pref | (same group) | | History embeddings `kHistoryEmbeddings` DISABLED (components/history_embeddings/core/history_embeddings_features.cc:26); Autofill AI `kAutofillAi*` (components/autofill/core/common/autofill_features.cc) | partial (default off / model execution off) | lock |

### Extensions & add-ons


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | `runtime.getBrowserInfo` says name "Firefox", Firefox version, plus a `zen` field (`toolkit/components/extensions/parent/ext-runtime-js.patch`); UA app name "Firefox" (`external-patches/librewolf/firefox-in-ua.patch`) | Extensions/sites seeing "Zen" | UA-CH brand list `user_agent_utils.cc:153,521` ("Chromium"), UA "Chrome/158" | reuse (Koan does not change UA/brands) | reuse |
| patch | AddonManager, Blocklist and AddonUpdateChecker compare against the Firefox version (`toolkit/mozapps/extensions/AddonManager-sys-mjs.patch`, `Blocklist-sys-mjs.patch`, `internal/AddonUpdateChecker-sys-mjs.patch`) | Add-ons rejected for unknown app version | CWS `prodversion` = Chromium version | reuse | reuse |
| patch | `tabs` API ignores untracked (empty) tabs, so there is no crash on a missing wrapper (`toolkit/components/extensions/parent/ext-tabs-base-js.patch`) | Throws on Zen's hidden empty tab | `extensions/browser/api/tabs` sees every `TabStripModel` tab (incl. other Spaces) | not needed (no hidden tab) | not needed |
| patch | Unsigned-addon enforcement off at build (`toolkit/moz-configure.patch` `require_signing` False) | Release builds require signed add-ons | Off-store CRX policy, developer-mode unpacked load | not needed | not needed |
| patch | Lightweight themes disabled and about:addons Themes category hidden (`toolkit/modules/LightweightThemeConsumer-sys-mjs.patch`, `toolkit/mozapps/extensions/content/aboutaddons-css.patch`, `zen.theme.disable-lightweight` true) | Extension themes recolour the browser | CWS themes via `ThemeService` | see themes-glass-ntp.md | see themes-glass-ntp.md |

### Bookmarks (places)


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | Bookmarks per Space: "Space" selector in the edit panel and dialog, `zen_bookmarks_workspaces` tables, menus/toolbar filtered by the active Space (`places/PlacesUIUtils-sys-mjs.patch`, `content/editBookmark-js.patch`, `editBookmarkPanel-inc-xhtml.patch`, `bookmarkProperties-xhtml.patch`, `browserPlacesViews-js.patch`) | All bookmarks everywhere (also the Mac menu bar's Bookmarks menu) | `BookmarkBubbleView`, `BookmarkEditorView`, Mac `chrome/browser/ui/cocoa/bookmarks/bookmark_menu_bridge.mm` | missing | override |
| patch | Bookmark items open in Glance (`browserPlacesViews-js.patch`) | n/a | see split-glance-library-media.md | pointer | not needed |

### Performance


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| pref | (Zen has tab unloader prefs in its own code, not yaml) | n/a | Memory Saver `kMemorySaverModeState` (components/performance_manager/public/user_tuning/prefs.h:35); `kInfiniteTabsFreezing` | done (koan_prefs.cc, koan_feature_overrides.cc) | override |

### Rendering/engine internals


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | **Boosts** core: `StyleAbsoluteColor::ToColor(frame)` / `CalcColor(style, frame)` run every resolved colour through `nsZenBoostsBackend::ResolveStyleColor` (accent tint, complementary rotation, inversion) carried on `BrowsingContext` fields (`layout/style/StyleColor-cpp.patch`, `ComputedStyle-cpp/h.patch`, `servo/ports/geckolib/cbindgen-toml.patch`, `docshell/base/BrowsingContext-h.patch`, `dom/chrome-webidl/BrowsingContext-webidl.patch`) | Page colours rendered as authored | none; would need Blink `StyleColor` resolve hook or a compositor colour filter (`cc::FilterOperation`) per WebContents | missing | not needed (feature decision; large) |
| patch | Boosts call-site plumbing (~35 patches): `layout/generic/*` (container, text, image, IFrame, text paint style), `layout/painting/*` (borders, gradients, image renderer), `layout/svg/*` (gradient, text, utils, filter, context paint, image context + cache key), `layout/tables`, `layout/mathml`, `layout/xul/tree`, `layout/base/PresShell-cpp` (default bg), `nsPresContext-h`, `image/AutoRestoreSVGState-h`, `gfx/layers/*`, `gfx/thebes/gfxUtils-*`, `widget/Theme*`, `widget/ThemeColors`, `widget/ScrollbarDrawing*`, `dom/base/nsAttrValue`, `dom/html/HTMLInputElement`, `dom/canvas/*`, `editor/libeditor/*`, `accessible/base/TextAttrs`, `accessible/windows/ia2/*`, `layout/style/{GeckoBindings,ServoCSSParser,StyleAnimationValue,nsStyleStruct,StyleColorInlines}` | Colour resolution without frame context | as above | missing | not needed |
| patch | `-moz-bool-pref("…")` media query in chrome/UA sheets (`servo/components/style/gecko/media_features-rs.patch`, `queries/feature*-rs.patch`) | Upstream removed `-moz-bool-pref` | n/a (Views reads prefs in C++) | not needed | not needed |
| patch | Swipe tracker consumer thresholds: `swipeSuccessThreshold`/`VelocityContribution` on `SimpleGestureEvent` (`widget/SwipeTracker-cpp/h.patch`, `widget/TouchEvents-h.patch`, `dom/events/SimpleGestureEvent-h.patch`, `dom/webidl/SimpleGestureEvent-webidl.patch`) | Fixed history-swipe thresholds | `chrome_render_widget_host_view_mac_history_swiper.mm` | done (`koan_swipe_tracker.cc`, `koan_space_swipe.cc` own thresholds) | override |
| patch | Swipe gestures start even if the system "Swipe between pages" is off (`widget/cocoa/nsCocoaUtils-mm.patch`, `zen.widget.macos.override-system-swipe-gestures` true) | Firefox obeys `isSwipeTrackingFromScrollEventsEnabled` | `history_swiper.mm:551` returns NO when `NSEvent.swipeTrackingFromScrollEventsEnabled` is false | partial (Space swipe ignores the setting; page back/forward swipe still obeys it) | override |
| patch | Static pref group `zen` (`modules/libpref/init/StaticPrefList-yaml.patch`, `modules/libpref/moz-build.patch`) | n/a | `koan_prefs.cc` | done (`koan_prefs.cc`) | n/a |
| pref | `pdfjs.enableHighlightEditor`, `.enableHighlightFloatingButton`=true, `pdfjs.enableScripting`=false (pdf.yaml) | PDF highlight off, PDF JS on | PDF annotations `kPdfInk2` ENABLED (pdf/pdf_features.cc:71), `pdf.enable_annotations` (pref_names.h:2912); PDF JS has no user pref | done (upstream annotations) / missing (PDF scripting stays on) | reuse |
| pref | `layout.css.corner-shape.enabled`=true (browser.yaml) | CSS `corner-shape` off | Blink `corner-shape` (third_party/blink/renderer/core/css/css_properties.json5:3300) | done (upstream) | reuse |
| pref | `gfx.webrender.compositor` (performance.yaml, Mac on) | Software composite | CoreAnimation delegated compositing on Mac | done (upstream) | not needed |
| pref | `general.smoothScroll.*`, `mousewheel.*` (fastfox/smoothscroll.yaml, all `!defined(XP_MACOSX)`) | Wheel physics on Win/Linux | n/a on Mac | done (n/a) | not needed (Mac-only) |

### Build/tooling/platform


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | `base/jar-mn.patch`, `base/moz-build.patch`, `themes/shared/jar-inc-mn.patch` | (build wiring for `src/zen`) | `//koan` GN targets | done (`src/browser/BUILD.gn`) | not needed |
| patch | `installer/package-manifest-in.patch`: crashreporter.app not packaged | Mozilla crash reporter shipped | Crashpad; uploads need Google keys Koan doesn't ship | n/a | not needed |
| patch | `installer/windows/nsis/*` (4 patches), `locales/en-US/installer/custom-properties.patch` | Windows installer and uninstall survey | n/a | n/a | not needed (Mac-only) |
| patch | Command-line flags `--blank-window [url]`, `--space <name\|uuid> [url]` (`BrowserContentHandler-sys-mjs.patch`) | n/a | `StartupBrowserCreator` switches (`chrome/browser/ui/startup/startup_browser_creator_impl.cc`) | missing | not needed |
| patch | Containers: creation panel anchored to the new-tab button, `userContextId` per Space in `addTab` (`contextualidentity/content/ContainerCreationPanel-mjs.patch`, `Tabbrowser`) | Firefox containers | none | not needed (ZEN_PARITY: no containers) | not needed |
| patch | PGO/LTO: CS-PGO options, free port for profileserver, UI training URLs (`build/moz-configure/lto-pgo-configure.patch`, `build/pgo/profileserver-py.patch`) | Fixed PGO port | Chromium ships PGO profiles (`chrome/build/*.pgo.txt`) | not needed | not needed |
| patch | Lint/format/test harness: eslint configs (3 top-level), `.prettierignore`, `.stylelintignore`, `.stylelintrc.js`, `python/mozlint/roller-py`, `tools/lint/*` (4), `testing/mochitest/browser-test-js` (keep 2 tabs), `testing/profiles/*/user-js` (2), `testing/xpcshell/runxpcshelltests-py` | Mozilla lint/test defaults | n/a | not needed | not needed |
| patch | Windows-only: NSIS makensis, desktop launcher off, ia2 colour (`toolkit/mozapps/installer/windows/nsis/makensis-mk.patch`, `toolkit/moz-configure.patch` launcher) | (Windows) | n/a | not needed (Mac-only) | not needed |
| patch | Linux/GTK-only: GTK drag opacity, updater rpath | (Linux) | n/a | not needed (Mac-only) | not needed |

### Themes/glass/NTP (pointer)


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | `browser-box-inc-xhtml.patch` (`#zen-browser-background`, grain), `navigator-toolbox-inc-xhtml.patch` (`#zen-toolbar-background`), `tabbrowser/content-area-css.patch` (tabpanel background removed, dialog scrim `light-dark(.3)`, `position: fixed`) | Opaque tab panel; Firefox dialog overlay colour | see themes-glass-ntp.md | see themes-glass-ntp.md | n/a |
| patch | `extensions/newtab/lib/ActivityStream-sys-mjs.patch`: sponsored top sites off | Sponsored shortcuts on the new tab page | see themes-glass-ntp.md (NTP) | covered (O11, #96) | lock |
| patch | `places/organizer-shared-css.patch`: Library window hides the Space selector in the bookmark editor | n/a | chrome://bookmarks | n/a | not needed |
| patch | Feature-callout native popovers on macOS (`aboutwelcome/content/aboutwelcome-css.patch`, `FeatureCallout` `nonnative` removed), screenshot overlay look (`screenshots/overlay/overlay-css.patch`), transparent browser only for the empty tab (`Tabbrowser` `_forZenEmptyTab`) | Firefox look | see themes-glass-ntp.md | pointer | n/a |
| patch | Design tokens: radii × `--zen-squircle-value`, panel shadow `--zen-big-shadow`, import `zen-theme.css`, toolbar bg removed (`toolkit/themes/shared/design-system/dist/tokens-shared-css.patch`, `src/toolbar-css.patch`); lightweight-theme text colour `--zen-colors-primary` | Firefox Proton tokens | see themes-glass-ntp.md | see themes-glass-ntp.md | see themes-glass-ntp.md |
| patch | WebRender cross-slice backdrop filters so acrylic URL bar/sidebar blur the page beneath (`external-patches/firefox/backdrop_filter_reads_web_content.patch`, gated on `zen.theme.acrylic-urlbar/sidebar`) | Backdrop-filter only reads its own picture-cache slice | cc layer background blur (`ui::Layer::SetBackgroundBlur`) reads lower layers | PR #105 | see themes-glass-ntp.md |
| patch | Zen Mods user stylesheet appended to chrome documents (`dom/base/Document-cpp.patch`, `layout/style/nsStyleSheetService-h.patch`) | No user chrome CSS store | Views UI has no CSS | missing | not needed (no CSS UI) |
| pref | `browser.newtabpage.activity-stream.feeds.{topsites,section.topstories}`, `system.showWeather`, `showSponsoredCheckboxes`=false, `browser.topsites.contile.enabled`=false, wallpapers on (newtab.yaml, disablemozilla.yaml) | Activity Stream feeds, Pocket stories, sponsored tiles | see themes-glass-ntp.md (NTP `ntp_features::*`) | PR #96 | redirect |
| pref | `zen.theme.*`, `zen.view.window.scheme` (theme.yaml, view.yaml) | (Zen feature) | see themes-glass-ntp.md | PR #62, #105 | override |
| css/js | Grain, button `::after`, toast/popup restyles, `.button-text` hides, emoji picker, gradient generator hides (zen-browser-ui.css:87, zen-buttons.css:85-88, zen-popup.css, spaces/zen-gradient-generator.css, common/emojis) | Firefox styling | see themes-glass-ntp.md | see themes-glass-ntp.md | — |

### Split/Glance/Library/media/pinned/folders (pointer)


| Src | Zen override (file) | Firefox behaviour suppressed | Chromium equivalent (file:line) | Koan status | Rec. |
|---|---|---|---|---|---|
| patch | `BrowserDOMWindow-sys-mjs.patch`: tabs opened by pages go to `gZenGlanceManager.onTabOpen` | Plain new tab | see split-glance-library-media.md | see split-glance-library-media.md | n/a |
| patch | Folders inside `tab-group` (`tabgroup-mjs.patch`: nested container, no unpin on add, animated removal), split-view groups, Glance tabs, pinned reset buttons and Essentials sections (`tab-mjs.patch`, `Tabbrowser`), space routing of new and external URLs (`Tabbrowser.addTab`, `BrowserContentHandler-sys-mjs.patch` `handURIToExistingBrowser`) | n/a | see split-glance-library-media.md | pointer (folders PR #60, pinned PR #150/#159) | n/a |
| patch | `zenModeActive` keeps a background browser active and painting (glance/split parent) (`toolkit/content/widgets/browser-custom-element-mjs.patch`); split panels visible in deck (`toolkit/content/xul-css.patch`); glance in tabbox selection (`tabbox-js.patch`) | Only the selected browser renders | see split-glance-library-media.md | see split-glance-library-media.md | see split-glance-library-media.md |
| patch | Media controller `getPositionState()`, `isBeingUsedInPIPModeOrFullscreen`, `pictureinpicturemodechange`, position notify on mandatory timeupdate and after seek (`dom/chrome-webidl/MediaController-webidl.patch`, `dom/media/mediacontrol/*.patch`, `dom/media/mediaelement/HTMLMediaElement-cpp.patch`) | No position API for chrome media controls | see split-glance-library-media.md (`MediaSessionObserver::MediaSessionPositionChanged`) | see split-glance-library-media.md | see split-glance-library-media.md |
| patch | PiP: no auto-PiP for muted video (`toolkit/actors/PictureInPictureChild-sys-mjs.patch`, `PictureInPicture-sys-mjs.patch`); back-to-tab switches Space; "minimize" (unpin icon) button in player (`pictureinpicture/content/player-js/xhtml.patch`, `themes/shared/pictureinpicture/player-css.patch`) | Auto-PiP regardless of mute; tab select without Space switch | Auto-PiP already needs `WasRecentlyAudible` (`auto_picture_in_picture_tab_helper.cc:791`); back-to-tab in `video_overlay_window_views.cc` | reuse (mute); covered (tab-groups-omnibox-fullscreen.md §3.2.4 PiP row) | see split-glance-library-media.md |
| pref | `browser.tabs.splitView.enabled` (locked false) (browser.yaml) | Firefox split view | see split-glance-library-media.md (`kSplitViewHorizontal`, chrome/browser/ui/tabs/features.cc:19) | partial (see that audit) | lock |
| pref | `media.videocontrols.picture-in-picture.video-toggle.enabled`=true, `.enable-when-switching-tabs.enabled`=false (pip.yaml) | PiP toggle on video, auto-PiP on tab switch | see split-glance-library-media.md; auto-PiP is the `AUTO_PICTURE_IN_PICTURE` content setting (chrome/browser/picture_in_picture/auto_picture_in_picture_tab_helper.cc:851) | partial (koan_media_controller.cc offers PiP; no on-video toggle) | override |
| pref | `media.eme.enabled`=true (zen/media.yaml) | DRM off on some builds | Widevine: `enable_widevine` true only when branded (third_party/widevine/cdm/widevine.gni:14); `prefs::kEnableEncryptedMedia` (pref_names.h:882) | missing (koan-mac.gn doesn't set enable_widevine; no CDM) | override |
| css/js | `.tab-group-line` hidden, group label hidden when collapsed sidebar (folders/zen-folders.css:7-8, 44-50); split group label/line (split-view/zen-split-group.inc.css:132-183); `#pinned-drop-indicator`, `#drag-to-pin-promo-card`, `#pinned-tabs-container`, `#EssentialsToolbar` hidden (vertical-tabs.css:23-26, 521-522, 853-856); Library/media/Glance `#statuspanel` hides (library/zen-library.css, media/zen-media-controls.css, glance/zen-glance.css:134) | Firefox tab groups / pinned UI | see split-glance-library-media.md | see split-glance-library-media.md | — |
| css/js | Spaces: workspace indicator hides, `context_zen*` workspace menu, `ZenSpaceManager` tab menu edits (spaces/zen-workspaces.css; spaces/ZenSpaceManager.mjs:1310-1410) | — | tab groups | covered (G1-G14) / PR #159 | — |

## zen.* prefs (Zen's own features), grouped

| Zen group (yaml) | Koan equivalent (koan_prefs.cc / koan_features.cc) | Status |
|---|---|---|
| zen.yaml tabs (`vertical`, `right-side`, `select-recently-used-on-close`, `close-on-back-with-no-history`, `ctrl-tab.*`, `ctrlTab.show-pending-tabs`, `essentials.max`, `rename-tabs`, `close-window-with-empty`) | kTabsOnRight, kSelectRecentlyUsedOnClose, kCloseOnBackWithNoHistory (+ tabs::kBackToOpener), kCtrlTabWithinKind, kCtrlTabSkipPending; kKoanSidebar; Essentials cap and rename in koan_essentials.cc / koan_pinned_tab_views.cc | done (no prefs for `show-newtab-vertical`, `open-pinned-in-new-tab`, `dnd-switch-space-delay`) |
| compact-mode.yaml | kCompactModeEnabled, kCompactModeHide, kCompactModeToolbarFlashPopup, kKoanCompactMode | partial (timings, `animate-sidebar`, `show-background-tab-toast`, `enable-at-startup`: PR #121/#144) |
| view.yaml | kSingleToolbarEnabled, kDragWindowFromContent (kKoanWindowDrag), kTabsOnRight | partial (no `show-newtab-button-top`, `grey-out-inactive-windows`, `mono-window-controls`, `show-three-dot-menu`, `hide-window-controls`, `borderless-fullscreen`, `show-clear-tabs-button`, `enable-loading-indicator`, `sidebar-expanded.max-width`) |
| zen-urlbar.yaml | kUrlBarBehavior, kUrlBarReplaceNewTab, kUrlBarShowCopyUrl, kCommandPaletteSuggestions, kKoanUrlBar | partial (no `show-pip-button`, `show-protections-icon`, `hide-one-offs`, `wait-to-clear`, `open-on-startup`, `show-domain-only-in-sidebar`) |
| workspaces.yaml (+ pinned-tab-manager) | kKoanWorkspaces, kWorkspaceTint*, kWorkspaceList (PR #158/#159); wrap-around and natural scroll in koan_workspaces.cc / koan_swipe_tracker.h; kPinnedTabCloseShortcutBehavior, kPinnedTabRestoreToPinnedUrl, kKoanPinnedTabs | partial (no `separate-essentials`, `continue-where-left-off`, containers, `wheel-close-if-pending`) |
| glance.yaml | kGlanceEnabled, kGlanceActivationMethod, kKoanGlance | partial (no `enable-contextmenu-search`, `open-essential-external-links`) |
| library.yaml | kLibraryLastSection, kLibraryHistoryClosedTabs, kKoanLibrary | done |
| media (zen.mediacontrols) | kMediaControlsEnabled, kKoanMediaControls | done |
| downloads.yaml | kDownloadAnimation, kKoanDownloadAnimation | done (duration/position fixed) |
| split-view.yaml | none (see split-glance-library-media.md) | missing |
| folders.yaml, live-folders.yaml | none in main (PR #60 folders); live folders none | PR #60 / missing |
| theme.yaml, macos.yaml | kLookStyle/Mode/Base/Accent/CustomCss (local state) | partial (PR #62/#103/#105) |
| boosts.yaml, mods.yaml | kLookCustomCss (global only) | missing (per-site boosts, mods) |
| session-store.yaml, window-sync.yaml, sync.yaml, share.yaml | koan_session_data.cc (Space data); no window sync / spaces sync / share | missing |
| welcome.yaml | kWelcomeScreenSeen, kKoanWelcome | done |
| static.yaml / zen.yaml `haptic-feedback`, watermark.yaml, updates.yaml | none | missing |
| gtk.yaml, windows.yaml | n/a | not needed (Mac-only) |

## Missing items, prioritised (Z-IDs)

Items found by more than one source are merged. A: browser patches. B: platform patches. C: prefs. D: Zen's CSS and JS. The order is by user-visible impact. `CRASH` marks a crash risk, explained under "Crash-risk notes".

### High

- **Z1. Extensions can't be reached in single toolbar** (A1, A2, D; #73, #78).
  - **Why:** the puzzle menu, pinned extension actions and `chrome.action` popups all live in the hidden `ToolbarView`. Zen puts them in the site-data panel off the pill's site button. As it stands, a user can't open uBlock's popup with the mouse.
  - **Mac test:** install uBlock Origin in single toolbar, then click the pill's site icon. Expected: an Extensions section with uBlock, and its popup opens anchored to the pill. Its own shortcut should also open the popup at the pill.
- **Z2. No Widevine DRM** (C; `media.eme.enabled`).
  - **Why:** an unbranded build has no `enable_widevine` (`third_party/widevine/cdm/widevine.gni:14`), so Netflix, Spotify Web and Disney+ fail. Fixing it takes a GN flag and CDM component signing, not a pref.
  - **Mac test:** open https://bitmovin.com/demos/drm. Expected: Widevine playback.
- **Z3. The session-restore infobar shows on every launch** (C).
  - **Why:** Koan makes "continue where you left off" the default value of `kRestoreOnStartup`. That is exactly the condition for `kSessionRestoreInfobar` (`chrome/browser/ui/ui_features.cc:392`, shown from `infobar_utils.cc:314`), so it shows "Koan will reopen your tabs". Zen shows nothing.
  - **Fix:** turn the feature off in `koan_feature_overrides.cc`.
  - **Mac test:** fresh profile, quit with 2 tabs, relaunch. Expected: no infobar.
- **Z4. Default-browser, Pin and PDF-viewer infobars on later launches** (C).
  - **Why:** `AddInfoBarsIfNecessary` (`chrome/browser/ui/startup/infobar_utils.cc:323-361`) still runs. Koan's patch only gates the API-keys infobar. Zen asks once, in its welcome.
  - **Mac test:** finish the welcome, answer "no" to the default-browser question, relaunch twice. Expected: no "isn't your default browser" or "default PDF viewer" infobar.
- **Z5. Permission prompts and in-use indicators have no visible anchor in single toolbar** (A1, B).
  - **Why:** permission chips and the camera/mic/location in-use chips live in the hidden `LocationBarView` (`bubble_anchor_util_views.cc:37-103`). Quiet-UI requests are never seen, and prompts fall back to the window anchor. This is security UI the user can't reach.
  - **Mac test:** on webcamtests.com, start the camera. Expected: the prompt hangs off the pill's site icon, and after you allow it an in-use icon shows in the pill. A site that asks twice for notifications (the quiet UI) should show an indicator in the pill.
- **Z6. Dock click after the last window closes should bring that window back** (A2; Zen removes the Mac-only branch in `SessionStore-sys-mjs.patch`).
  - **Why:** Chromium opens an empty window (`chrome/browser/app_controller_mac.mm:1916-1925`), so the user's Spaces seem to be gone.
  - **Mac test:** 2 Spaces with 3 tabs each, ⌘⇧W, click the Dock icon. Expected: the same window, Spaces and active tab come back. `CRASH`.
- **Z7. Window sync** (D; `ZenWindowSync.sys.mjs`, `window-sync.yaml`).
  - **Why:** in Zen, Essentials, pinned tabs and Spaces are shared by every window, and "New Blank Window" gives a window without them. Koan's windows are independent.
  - **Mac test:** add an Essential, ⌘N. Expected: the same Essential and Space list. ⌘⇧N should give no Essentials. `CRASH` if it is built by swapping WebContents between windows.
- **Z8. A pinned tab or Essential keeps its site** (A2, D; `zen.tabs.open-pinned-in-new-tab`).
  - **Why:** in Koan, typing another domain into the URL bar from a pinned tab navigates it away. Zen opens a new tab.
  - **Mac test:** on an Essential showing github.com, ⌘L, type `apple.com`, Enter. Expected: a new tab, and the Essential stays on github.com. A same-domain URL should load in place.
- **Z9. Import from Chrome, Arc, Dia, Brave, Edge and Vivaldi on macOS** (A2, C, D).
  - **Why:** Chromium's Mac importer only knows Safari, Firefox and a bookmarks file. Zen's welcome imports Chromium-family browsers and Arc/Dia, with Arc's Spaces and pins.
  - **Mac test:** with Chrome and Arc installed, a fresh profile, welcome > Import. Expected: both are listed, and Arc's Spaces and pins arrive as Koan Spaces and pinned tabs.
- **Z10. Tab hover cards** (C; `browser.tabs.hoverPreview.enabled`=false).
  - **Why:** `TabView::OnMouseEntered` calls `UpdateHoverCard` (`chrome/browser/ui/views/tabs/common/tab_view.cc:707-708`). Nothing in Koan turns this off.
  - **Mac test:** hover a sidebar tab for 1 s. Expected: no card. First check live whether the vertical strip pops the card at all.

### Medium

- **Z11. The download bubble auto-opens** (C; `alwaysOpenPanel`=false).
  - **Why:** the partial-view bubble anchors to the hidden toolbar's download button. Koan only suppresses the started animation (`download_toolbar_ui_controller.cc.patch`).
  - **Mac test:** download a file. Expected: only Koan's animation to the sidebar or Library, no bubble. `CRASH` (zero-size anchor; check on a DCHECK build).
- **Z12. The ⌘D bookmark bubble anchors to the hidden star** (A1).
  - **Mac test:** single toolbar, ⌘D. Expected: the edit bubble appears next to the pill's site icon. `CRASH` if the star page action is ever removed (`toolbar_view.cc:1300-1301`).
- **Z13. Window drag from page content should be a native AppKit drag** (B, D; Zen `nsZenNativeWindowMove.mm`).
  - **Why:** Koan uses `Widget::RunMoveLoop`, Chromium's emulated loop, so macOS 15+ edge tiling, the tiling preview and dragging to another Space don't work.
  - **Mac test:** drag the top 5% of example.com to the left screen edge. Expected: the tiling preview appears, and the window tiles. `CRASH` (release CHECKs in the move loop).
- **Z14. Compact mode should keep the toolbar shown while a bubble anchored to it is open** (B).
  - **Why:** permission prompts, site info, extension popups and the star can anchor to a toolbar that then slides away.
  - **Mac test:** compact mode on, with the toolbar hidden; open a page that calls `Notification.requestPermission()`. Expected: the toolbar slides in and stays until the bubble closes. `CRASH` (possible DCHECK).
- **Z15. The multiple-toolbars layout shows Chromium-only UI** (A1, A2, D).
  - **Why:** the avatar, ⋮ sign-in rows, side panel, tab search, Glic, the star, reading mode, the cookie "eye", and the "Not secure" text (`LocationIconView::GetText`) all show. Zen hides these in both layouts.
  - **Mac test:** Multiple toolbars, open http://neverssl.com. Expected: back/forward/reload, URL bar, downloads and extensions only; no text chip and no avatar.
- **Z16. Closing the window's last tab keeps the window on the empty tab; a second ⌘W closes it** (A1, A2; `zen.window.close-window-with-empty`).
  - **Why:** Koan re-adds a tab when a Space empties (`koan_workspaces.cc:1147`), so ⌘W ⌘W never closes the window.
  - **Mac test:** one window with one tab: ⌘W, then ⌘W. Expected: first the empty tab, then the window closes. `CRASH` (adding a tab while the strip is emptying).
- **Z17. History and Downloads menu items should open the Library** (D).
  - **Why:** menu bar History > Show All History and the downloads command still open chrome://history and chrome://downloads.
  - **Mac test:** History > Show All History. Expected: the Library opens at History, and no new tab appears.
- **Z18. Tab loading spinner hidden** (D; `zen.theme.hide-tab-throbber`=true).
  - **Mac test:** load a slow page and watch its sidebar row. Expected: no throbber. Check live what Zen shows in its place.
- **Z19. Mono traffic lights** (D; `zen.widget.mac.mono-window-controls`=true).
  - **Mac test:** keep the pointer away from the top-left. Expected: three grey dots in the toolbar tint, turning red, yellow and green on hover.
- **Z20. Restore pinned tabs and folders even when not resuming the session** (D).
  - **Mac test:** set On startup to the New Tab page, pin a tab in a Space and put a tab in a folder, quit with ⌘Q and relaunch. Expected: Essentials, the pinned tab and the folder are back (unloaded), and the unpinned tabs are gone.
- **Z21. Chromium toasts** (A1).
  - **Why:** "Link copied" and "Image copied" (`render_view_context_menu.cc:1257`, `:5450`) and `browser_commands.cc:2960` use `ToastController`, not `KoanToast`.
  - **Mac test:** right-click a link > Copy Link Address. Expected: a Zen-style toast or none. Check live Zen first.
- **Z22. About, Help and Report an Issue go to Google** (A1; `main_menu_builder.mm:54-56, 588, 599-601`).
  - **Mac test:** Koan menu > About Koan; Help > Koan Help; Help > Report an Issue. Expected: Koan's version and GitHub links, not the Google help centre or feedback form.
- **Z23. The floating URL bar closes when the window loses focus** (C; `closeOnWindowBlur`=false).
  - **Mac test:** ⌘T, type "abc", ⌘Tab away and back. Expected: the bar is still open with "abc".
- **Z24. Global Privacy Control is off** (C).
  - **Why:** Zen sends `Sec-GPC: 1`. Chromium needs `kUniversalOptOutSettings` plus the `universal_optout.enabled` pref.
  - **Mac test:** on a GPC test page, `navigator.globalPrivacyControl === true`.
- **Z25. Switching Space while dragging a tab** (D).
  - **Why:** hovering a Space's icon switches at once, and the sidebar edge switches after 500 ms.
  - **Mac test:** drag a tab onto Space 2's icon. Expected: Space 2 shows, and the tab drops into it.
- **Z26. The opener is kept across tab switches** (A2).
  - **Mac test:** on A, ⌘-click a link (B), switch to C, then to B, and close B. Expected: A is selected. Today Koan selects C, through most-recently-used order.
- **Z27. Tabs opened from the new-tab URL bar go to the top of the Space** (A2).
  - **Mac test:** ⌘T, type a URL, Enter. Expected: the new tab is first under the pinned section.
- **Z28. Chromium settings that Zen hides are still reachable** (A2, D).
  - **What:** the AI page (Gemini, tab organisation), ad privacy, the theme and home-button rows, Customize Chrome > Toolbar, and sign-in/sync rows. Zen hides the matching Firefox panes: Data collection, Website advertising, Support, AI controls, More from Mozilla, browser layout, theme and Home.
  - **Mac test:** open chrome://settings. Expected: none of those rows.
- **Z29. Two-finger back/forward swipe when "Swipe between pages" is off in System Settings** (B; Zen forces it on, while `history_swiper.mm:551` bails out).
  - **Mac test:** turn the system setting off, then swipe two fingers on a page with history. Expected: it navigates back. Check for a conflict with the Space-swipe region first.
- **Z30. Reader view in the tab, not a side panel** (B).
  - **Mac test:** on an article, press ⌘⌥R. Expected: the tab switches to the reader layout. Check that it doesn't fight the Koan sidebar (#155).
- **Z31. Boosts** (B, C; about 45 patches plus `boosts.yaml`).
  - **Why:** per-site accent, rotation and invert, with an editor panel. This is a product decision: the only Chromium routes are a Blink colour hook or a per-tab cc filter.
  - **Mac test (Zen reference):** right-click a site > Boost > pick an accent. Expected: the page tints live.

### Low (one line each; the test is in brackets)

- **Z32. Form history autocomplete is on** (C; `browser.formfill.enable`=false). [Submit a search twice, refocus the field: no dropdown.]
- **Z33. Clipboard URL suggestion** (C). [Copy a URL, ⌘T: a "visit copied link" row.]
- **Z34. PiP button in the URL bar** (C, D; `zen.urlbar.show-pip-button`). [YouTube video playing: a PiP icon in the pill.]
- **Z35. Sign-in interception and promos** (C; `dice_web_signin_interceptor.cc:546`). [Sign in to accounts.google.com: no "Sign in to Koan?" bubble.]
- **Z36. Compose ("Help me write") isn't disabled directly** (C; `kEnableCompose`). [Right-click a textarea: no entry, even without PR #158.]
- **Z37. No What's New tab after an update** (D). [Run a newer build over an old profile: no tab.]
- **Z38. Middle-click or double-click on empty sidebar space opens a new tab or the URL bar; double-click must not zoom the window** (A1, A2). [Middle-click and double-click below the last tab.]
- **Z39. Duplicating a pinned tab or Essential gives a normal tab in the active Space** (A2). [Right-click an Essential > Duplicate: the Essentials count is unchanged.]
- **Z40. MRU Ctrl+Tab skips unloaded tabs** (A2; `zen.ctrlTab.show-pending-tabs`=false). [Restart with pending tabs, Ctrl+Tab: only loaded tabs.]
- **Z41. Tab click modifiers: double-click an Essential to reset it** (A2). [Live-check Zen first.]
- **Z42. Bookmarks per Space; the Bookmarks menu filtered by Space** (A2). [A bookmark assigned to Space A is hidden in Space B.] **Blocked on Z7 (checked 2026-11-11).** Zen 1.24b has it: `ZenSpaceBookmarksStorage.js` keeps a `zen_bookmarks_workspaces` table (bookmark guid to workspace uuid, many to many) in Places; `browserPlacesViews-js.patch` filters the bookmarks toolbar, the menus and panel views with `isBookmarkInAnotherWorkspace` (`ZenSpaceManager.mjs:3379`: hidden only if assigned to other Spaces and not to the active one, so an unassigned bookmark shows everywhere); `editBookmark-js.patch` adds a Spaces checklist to the edit panel (not the Library window). The key is a workspace uuid that every window shares (`ZenWindowSync`). Koan has no such id: a Space is a tab group in one window, with a session-local `TabGroupId`, and `koan.workspaces.list` holds only the title, colour and active flag. Once Z7 gives Spaces a stable id shared across windows, the Chromium side is: store the ids in `BookmarkModel` meta info (`SetNodeMetaInfo`, saved in the Bookmarks file, no new storage); filter `BookmarkBarView`, `BookmarkMenuDelegate` (bar folders) and the macOS `BookmarkMenuBridge`; and add a Spaces row to `BookmarkBubbleView` and `BookmarkEditorView`.
- **Z43. The floating URL bar yields to modal dialogs** (A2). [`alert()` while the bar is open: the bar hides, then comes back.]
- **Z44. Spaces search mode in the URL bar (`` ` ``) and folder results** (A2, D). [⌘L, type `` ` ``: the Spaces chip; type a folder's name: a folder row.]
- **Z45. The un-snap threshold for a tiled window is 50 px, not 4 px** (B). [Tile left, move 10 px in the top band: no drag.]
- **Z46. Tab drag visuals: stacked image with a count, sliding into its slot** (B). [Drag 3 selected tabs: a stacked image with "3".]
- **Z47. Dialog and prompt sizing and close animation; infobar look** (B). [`alert("x")`: wider, roomy, fades out. An infobar: transparent with a top hairline.]
- **Z48. Styled status bubble** (D; `zen.theme.styled-status-panel`). [Hover a link: a rounded pill, weight 600, themed background.]
- **Z49. Double-clicking the sidebar edge resets its width to 230 px** (D). [Resize to 400 px, then double-click the edge.]
- **Z50. Compact mode holds the sidebar during a tab drag; haptic tick when the drop target changes** (D; #80). [Drag a tab from a revealed compact sidebar: it stays until the drop.]
- **Z51. Native NSPopover panels (`widget.macos.native-popovers`) and the position of the identity box in the pill** (D). [Compare with Zen live before changing.]
- **Z52. App icon on macOS 26: Zen removes `CFBundleIconName` (legacy art); Koan ships a Liquid Glass icon** (B). [A deliberate divergence; confirm with the user.]
- **Z53. Command-line `--blank-window` and `--space`** (A2). [Not needed unless Koan wants them.]

## Crash-risk notes

1. **Z13 and today's window drag.**
   - **Where:** `NativeWidgetNSWindowBridge::RunMoveLoop` has three release CHECKs: `!HasCapture()`, no global capture window, and `!window_move_loop_` (`components/remote_cocoa/app_shim/native_widget_ns_window_bridge.mm:1197-1202`, verified). Koan posts `RunMoveLoop` as a task from a page mouse move (`koan_window_drag.cc:265-286`).
   - **Risk:** if a menu, bubble, omnibox popup or tab drag takes capture between the post and the run, or a second drag starts, the browser crashes in a release build.
   - **Test:** start a page drag and press ⌘L or the Space-menu shortcut in the same instant; also do fast repeated double drags.
   - **Fix:** `performWindowDragWithEvent:` avoids the loop. Keep the WeakPtr and posted-task pattern.
2. **Z12, ⌘D.**
   - **Where:** `ToolbarView::ShowBookmarkBubble` has `CHECK(bookmark_star_icon)` (`chrome/browser/ui/views/toolbar/toolbar_view.cc:1300-1301`, verified).
   - **Risk:** Koan only hides page actions today. Removing or never creating `kActionBookmarkThisTab` would crash on ⌘D, on the menu item and from the extension API.
3. **Z16, keeping the window open after its last tab.**
   - **Risk:** re-adding a tab after `TabStripModel` empties or `OnWindowClosing` has started can re-enter the model and cause a use-after-free or a CHECK.
   - **Fix:** intercept in `chrome::CloseTab`, which `browser_commands.cc.patch` already patches, before removal.
   - **Test:** ⌘W, ⌘⇧W, the red button, and ⌘Q, each with one tab.
4. **Z6, Dock reopen.**
   - **Risk:** this must not race `SessionRestore::IsRestoring` (`app_controller_mac.mm:1703`) or a profile that is still loading.
   - **Fix:** reuse `AttemptSessionRestore` or TabRestoreService rather than building windows by hand.
5. **Z11, Z14 and Z5, bubbles anchored to a hidden or zero-size toolbar view.**
   - **Risk:** Chromium bubbles assume a visible anchor widget. Koan re-points `kWidgetForAnchoringKey` only in fullscreen (`koan_compact_mode_controller.cc:1148-1156`).
   - **Test:** check on a DCHECK build.
6. **Z7, window sync, if built.**
   - **Risk:** moving WebContents between `TabStripModel`s on every activation, as Zen does on `TabSelect` (`ZenWindowSync.sys.mjs:908-980`), risks observer re-entrancy and stale `TabInterface*` in Koan controllers. This is the same class of bug as the #128 teardown crash.
   - **Fix:** one owner, with deferred moves.
7. **Tab drag.**
   - **Background:** Zen wraps `handle_dragend` in try/catch (`ZenDragAndDrop.js:1494-1498`).
   - **Risk:** any Koan reaction that closes or regroups tabs synchronously inside a `TabDragController` callback can invalidate the controller's tab list.
   - **Fix:** keep post-drop fixes posted (`koan_pinned_tabs.cc:795` already does this).
8. **New upstream pref defaults** (Z4, Z11, Z24, Z32, Z35).
   - **Risk:** calling `SetDefaultPrefValue` before the owning component registers the pref CHECKs at startup. PR #158 hit this.
   - **Watch:** `universal_optout.enabled` and the download-bubble prefs are registered in component hooks, so check the order on the Mac.
9. **PR #90.**
   - **Where:** `HistoryMenuBridge::ShouldMenuItemBeVisible` hits `NOTREACHED()` for unknown items.
   - **Risk:** adding a Koan item, such as Library, to the History or Bookmarks menus would crash. This is already noted in `docs/REBASE.md` by #90.
