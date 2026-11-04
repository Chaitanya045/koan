# Omnibox lockdown: decisions (items O1-O10, O13, O15 of the audit)

The audit is `tab-groups-omnibox-fullscreen.md`, section 2. This file records
what was decided and where it lives, per the "Upstream audit first" rule.

| Item | Decision | Where |
|---|---|---|
| O1 popup flavour | **Override** `kWebUIOmniboxPopup`, `AimPopup`, `FullPopup`, `Simplification` off, so the Views popup Koan styles is the one that shows | `src/browser/koan_feature_overrides.cc` |
| O2 Cmd+L floats, pill click doesn't | **Own**: `edit_in_pill_` set by the pill's click path (`KoanSidebarNavView::FocusOmnibox` → `NoteEditInPill`); `DesiredPlacement` floats unless it is set; typing still floats (floating-on-type). Cmd+L pressed while the bar is already open in the pill is **not** handled (no focus change arrives; the audit's re-pointing of `kActionFocusLocation` is left out: with O3 the pill is only open after a click) | `koan_url_bar_controller.cc`, `koan_sidebar_nav_view.cc` |
| O3 no default NTP focus | **Redirect**: `ShouldFocusLocationBarByDefault` returns false in the single toolbar with replace-newtab on | `patches/.../browser_web_contents_delegate.cc.patch` (existing file, 1 hunk) |
| O4 invisible focus | **Lock**: `OnFocusChanged` ignores focus while the edit model's focus state is `OMNIBOX_FOCUS_INVISIBLE` | `koan_url_bar_controller.cc` |
| O5 AI off | **Override**: `kAimEnabled`, Lens, Glic, contextual tasks, NTP next features and composebox, model execution | `koan_feature_overrides.cc` |
| O6 `@gemini`, `@aimode` | **Lock**: deactivated at launch, once the search engines have loaded; the user can switch one back on until the next launch | `koan_browser_view_controller.cc` |
| O7 Zen's suggestion defaults | **Override** the defaults of `search.suggest_enabled` (false), `omnibox.show_ai_mode_omnibox_button` (false), `omnibox.ai_mode_settings` (1) | `koan_prefs.cc`; 1 hunk in `patches/.../browser_prefs.cc.patch` |
| O8 remote zero-suggest | **Reuse** upstream's own rule (no `SearchSuggestEnabled()`, no remote request) via O7; Koan's provider lists open tabs and top sites | `koan_zero_suggest.cc`; no `zero_suggest_provider.cc` patch |
| O9 switch-to-tab | **Redirect**: an open-tab row in a new-tab session keeps `kSwitchToTab` | `chrome_omnibox_client.cc.patch`, `koan_url_bar_controller.cc` |
| O10 engine choice | **Redirect**: the welcome records `ChoiceMadeLocation::kChoiceScreen` | `koan_welcome.cc` |
| O13 IPH | **Override** the tab-group, vertical-tab, split-view, side-panel, extensions, Glic and Lens IPH features. They stop promos being shown; the "Attempting to show IPH ... before browser initialization complete" log line (a promo asked for during startup) is logged whatever the feature state and stays (baseline on develop) | `koan_feature_overrides.cc` |
| O15 leak detector | **Lock** (loud): the URL bar controller DCHECKs that the WebUI popup is off | `koan_url_bar_controller.cc` |

Not in this PR (stay with #96): Koan's empty NTP and the NTP entry points
(N1-N5, O11, O12), the empty tab, the Google-NTP `search.cc` hook, the
Customize Chrome panel beyond the `kNtpCustomizeChromeAutoOpen` override.
`scripts/e2e/palette.mjs` reads rows from the WebUI popup and has to be ported
to the Views popup's result views on the Mac.

Tests: `scripts/e2e/omnibox_lockdown.mjs` (audit tests 1, 2, 3, 4, 6, 7 in part;
8 and 9 need an EEA profile and a third-party NTP fixture).
