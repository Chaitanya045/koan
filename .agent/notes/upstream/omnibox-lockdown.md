# Omnibox lockdown: decisions (items O1-O10, O13, O15 of the audit)

The audit is `tab-groups-omnibox-fullscreen.md`, section 2. This file records
what was decided and where it lives, per the "Upstream audit first" rule.

| Item | Decision | Where |
|---|---|---|
| O1 popup flavour | **Override** `kWebUIOmniboxPopup`, `AimPopup`, `FullPopup`, `Simplification` off, so the Views popup Koan styles is the one that shows | `src/browser/koan_feature_overrides.cc` |
| O2 Cmd+L floats, pill click doesn't | **Own**: `edit_in_pill_` set by the pill's click path (`KoanSidebarNavView::FocusOmnibox` → `NoteEditInPill`); `DesiredPlacement` floats unless it is set; typing still floats (floating-on-type). **Hook** for Cmd+L while the bar is already open in the pill (no focus change then): `chrome::FocusLocationBar()` calls `koan::OnFocusLocationBarCommand()` first, which clears `edit_in_pill_` and floats it in a posted task. One hook covers the action and the legacy command | `koan_url_bar_controller.cc`, `koan_sidebar_nav_view.cc`; 1 hunk in `patches/.../browser_commands.cc.patch` |
| O3 no default NTP focus | **Redirect**: `ShouldFocusLocationBarByDefault` returns false in the single toolbar with replace-newtab on | `patches/.../browser_web_contents_delegate.cc.patch` (existing file, 1 hunk) |
| O4 invisible focus | **Lock**: upstream (`search::FocusOmnibox`) focuses the omnibox first and marks the focus invisible afterwards, so the check can't be made in the focus listener; the bar opens as for any focus and a posted task (`CloseIfFocusInvisible`) closes it again when the focus state is then `OMNIBOX_FOCUS_INVISIBLE` (one frame of lift for a search page's own box) | `koan_url_bar_controller.cc` |
| O5 AI off | **Override**: `kAimEnabled`, Lens, Glic, contextual tasks, NTP next features and composebox, model execution | `koan_feature_overrides.cc` |
| O6 `@gemini`, `@aimode` | **Lock**: `AiStarterPackFilter` (a `TemplateURLServiceObserver`, one per window) deactivates them once the search engines have loaded and on every change, so Settings can't switch them back on | `koan_browser_view_controller.cc` |
| O7 Zen's suggestion defaults | **Override** the defaults of `search.suggest_enabled` (false), `omnibox.show_ai_mode_omnibox_button` (false), `omnibox.ai_mode_settings` (1) | `koan_prefs.cc`; 1 hunk in `patches/.../browser_prefs.cc.patch` |
| O8 remote zero-suggest | **Reuse** upstream's own rule (no `SearchSuggestEnabled()`, no remote request) via O7; Koan's provider lists open tabs and top sites. **Hook** (found on the Mac: the empty bar showed no rows): desktop ZPS sorts matches into Google's sections (`DesktopWebURLZpsSection` with a limit of 0 URLs, the NTP section with no URL group at all), so open-tab and top-site matches were dropped; Koan appends a `ZpsSection` that takes `GROUP_OTHER_NAVS` (8 rows) | `koan_zero_suggest.cc`; **new patch** `components/omnibox/browser/autocomplete_result.cc.patch` (`SortAndCull`, 1 hunk); no `zero_suggest_provider.cc` patch |
| O9 switch-to-tab | **Redirect**: an open-tab row in a new-tab session keeps `kSwitchToTab` | `chrome_omnibox_client.cc.patch`, `koan_url_bar_controller.cc` |
| O10 engine choice | **Redirect**: the welcome records `ChoiceMadeLocation::kChoiceScreen` | `koan_welcome.cc` |
| O13 IPH | **Override** the tab-group, vertical-tab, split-view, side-panel, extensions, Glic and Lens IPH features. They stop promos being shown; the "Attempting to show IPH ... before browser initialization complete" log line (a promo asked for during startup) is logged whatever the feature state and stays (baseline on develop) | `koan_feature_overrides.cc` |
| Z36 Compose ("Help me write") | **Override**: `compose::features::kEnableCompose` off directly (zen-firefox-overrides.md), not only through model execution | `koan_feature_overrides.cc` |
| Z23 floating bar survives window blur | **Already covered**: `OnFocusChanged` ignores the focus loss of an inactive window, so the bar, its text and the new-tab session stay (Zen's `closeOnWindowBlur`=false); tested for the new-tab bar in `urlbar.mjs` ("Switching away and back keeps the new-tab bar") | `koan_url_bar_controller.cc` |
| O3 startup bar | **Hand to #96**: with no default focus a startup new tab page opens nothing, where Zen's `zen.urlbar.open-on-startup` (true) opens the New Tab bar when the window starts on the empty tab (ZenSpaceManager.mjs:1095-1117). #96's empty tab takes it over | `koan_url_bar_controller.cc` (`SuppressesDefaultUrlBarFocus`) |
| O4 invisible focus | **Lock**: a focus marked invisible (a page's own box: `search::FocusOmnibox(INVISIBLE)`) closes the bar with `Close()`, not just `EndEditing()`, so the omnibox does not keep a hidden focus that the pill, Cmd+L and typing could not reopen | `koan_url_bar_controller.cc` (`CloseIfFocusInvisible`) |
| O6 side effect | The starter-pack filter deactivates `@gemini` / `@aimode` with `SetIsActiveTemplateURL`, which writes `is_active=false` into the user's search-engine sync data (starter packs sync). Moot without Google API keys; the featured provider already hides `@aimode` through `kAimEnabled`, and `@gemini` would be hidden by `browser.gemini_settings`=1 (registered after the `browser_prefs.cc` hunk; moving the hunk is the alternative) | `koan_browser_view_controller.cc` |
| O8 scope | The Koan ZPS section applies to the omnibox of a web page, search results page or new tab page only (`UsesLocalZeroSuggestSection`), and takes only the provider's own matches: other `GROUP_OTHER_NAVS` matches are erased in ZPS there, as the zero URL limit dropped them | `autocomplete_result.cc.patch` |
| O9 | **Override**: an open tab's row always switches (`MaybeAdjustOmniboxDisposition`), unless the tab is gone (then new tab in a Cmd+T session, else the current one) | `chrome_omnibox_client.cc.patch` |
| O15 | **Log, no crash**: the WebUI popup / AI Mode popup being on is `LOG(ERROR)` (the Mac harness and `omnibox_lockdown.mjs` grep for it), because a user can flip the chrome://flags entry | `koan_url_bar_controller.cc` |

Not in this PR (stay with #96): Koan's empty NTP and the NTP entry points
(N1-N5, O11, O12), the empty tab, the Google-NTP `search.cc` hook, the
Customize Chrome panel beyond the `kNtpCustomizeChromeAutoOpen` override.
`scripts/e2e/palette.mjs` now reads the Views popup's `OmniboxResultView` /
`OmniboxTextView` properties; the property names are to be checked on the Mac.

Tests: `scripts/e2e/omnibox_lockdown.mjs` (audit tests 1, 2, 3, 4, 6, 7 in part;
8 and 9 need an EEA profile and a third-party NTP fixture).
