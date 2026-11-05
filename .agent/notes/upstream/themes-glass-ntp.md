# Upstream-behaviour audit 3: themes and colours, macOS glass, New Tab Page and first run

Date: 2026-11-05. Chromium 158.0.8127.2 (`681cd621e8`), Koan `develop` dcf9bdaa, plus the open PRs in these areas:
- **#62** theme picker, stacked on #105;
- **#103** and **#105** glass, both drafts;
- **#96** NTP, a draft;
- **#159** Spaces lockdown, read for the colour overlap.

The method follows `vertical-tabs.md` and `tab-groups-omnibox-fullscreen.md`, and the audit was read-only:
- Pristine Chromium comes from `git show HEAD:` and `git grep HEAD`.
- Zen comes from `.cache/zen-desktop-1.24b` and `.cache/zen-src-1.24b/defaults/preferences/firefox.js` (called **FJS** below).
- The PR diffs come from `gh pr diff`.

Nothing was built or run. Verdicts marked *(live check)* need the Mac. All line numbers are pristine upstream unless they name a Koan file.

**IDs.** These are changes, not tests:
- **T** = themes and colours;
- **GL** = glass (G is already the tab-groups audit's prefix);
- **N** = NTP and first run.

Mac tests are **TT**, **GT** and **NT**. G1, O10 and F1 refer to the earlier audit.

**Summary.** The pattern from audits 1 and 2 shows up again, with one new twist: **three Koan layers now write the same upstream knob**.

- **Colours.** `Widget::SetUserColorOverride` on the browser widget has three writers:
  - upstream focus mode;
  - #159's `ApplySpaceColor`;
  - #62's `KoanThemeController::Update`.

  `BrowserWidget::GetColorProviderKey` applies whichever wrote last. It does so *after* ThemeService and after `koan::AdjustColorProviderKey`, and it also drops extension themes. A user colour is GM3's seed, so the Space's accent restyles **every** Chromium surface (bubbles, menus, settings WebUI through colors.css) in Material You tones. Zen has opaque grey panels and native menus there.
- **Glass.** It is Chromium 158's own `GlassFrameService` frame (macOS 26+ only), bent to "every window, whole window, NSVisualEffectView".
  - The bending is sound, but #103 deletes the `OpaqueFrameBackgroundView`. Occlusion and subview sorting look for that view by identifier.
  - #105 restyles colours through a **process-wide** `NotifyOnNativeThemeUpdated()` each time the URL bar opens.
  - The CHECK rules hold in #62 only through a posted task, so there is a one-paint window between a new opaque layer and the fix.
- **First run.** It has never been exercised. Every Mac run passes `--no-first-run`, `--no-default-browser-check` and usually `--use-mock-keychain`. Those three flags hide:
  - Koan's own welcome (it is gated on `IsChromeFirstRun()`);
  - Chromium's DICE first-run experience (FRE) in the profile picker;
  - the EEA choice dialog, the session-restore infobar, the default-browser infobar and the PDF infobar;
  - the keychain path behind #141.

  B6, B7 and B16 from the 2026-11-04 user test are the visible tip.
- **NTP.** #96's hook is in the right place (`NewTabURLDetails::ForProfile`). But its page bakes the colour in at request time, so after #62 a Space switch leaves the wrong colour on screen. Upstream's own colors.css updater would fix that for free.

---

## 0. Cross-cutting

1. **One owner of the window's colour key.** `SetUserColorOverride` and `SetColorModeOverride` on `BrowserWidget` must have exactly one Koan writer, `KoanThemeController`, once #62 lands. #159's `ApplySpaceColor()` becomes a call into it, and the upstream hook in `UpdateFocusModeTheme` (`tab_strip_collection_controller.cc:625-647`) stays swallowed. Today #62 and #159 both carry `MigrateWorkspaceTint()` and both delete `UpdateTint`: whichever merges second must drop its copy (T1).
2. **One "never opaque" rule for every view Koan paints translucently.** The rule: after any `SetPaintToLayer()`, call `SetFillsBoundsOpaquely(false)` in the same statement, at the creation site. A posted fix-up does not count. This applies to:
   - Koan's own creations: `koan_compact_mode_controller.cc:1062`, `:1666` (already done);
   - every upstream re-creation Koan triggers: `MultiContentsBackgroundView::OnThemeChanged` → `SetPaintToLayer(new_type)` at `multi_contents_background_view.cc:31-35`, and `BrowserFrameViewMac::OnGlassFrameEligibilityChanged` → `SetFillsBoundsOpaquely(!eligible)` at `browser_frame_view_mac.mm:615-620`.

   The `CustomCornersBackground` CHECKs (`custom_corners_background.cc:160, 331`) stay. Never patch them (GL3).
3. **The test harness must stop hiding first run.** Add a first-run e2e profile that runs **without** `--no-first-run`, `--no-default-browser-check` or `--use-mock-keychain`, and run it on every release candidate (NT1-NT6). Today `scripts/koan_test_env.sh:27`, `scripts/run_mac.sh:39-42` and the user-test sessions all pass these flags.
4. **Feature-override block.** These need rows in `koan_feature_overrides.cc` and `docs/REBASE.md`:
   - `kSessionRestoreInfobar`, `kPdfInfoBar`, `kSeparateDefaultAndPinPrompt` (N6, N7);
   - possibly `kOSCryptAsyncAvailabilityInfoBar` (N8, only after the root cause is fixed);
   - `kGlassFrame` (already in #103) and the glass IPH (already in #103);
   - `whats_new::kForceEnabled`, which must stay off.
5. **One Mac menu table** (G7/F3, `koan::HidesMenuCommand`). Nothing in these areas adds a Mac menu item, but Zen's View > Appearance (Auto/Light/Dark, `ZenMenubar.mjs:5-45`) is a menu-bar item that Koan lacks (T9).

## Highest-risk items, all three areas (ranked)

| # | Severity | Risk | Who hits it | Fix |
|---|---|---|---|---|
| 1 | High (process) | **First run is never tested.** Koan's welcome, Chromium's DICE FRE, the EEA choice dialog, the default-browser and session-restore infobars, and the keychain path are all behind flags every run passes | Every new user | §0.3, NT1-NT6 |
| 2 | High | **Two to three writers of `SetUserColorOverride`** (upstream focus mode, #159, #62). The last writer wins, which causes flicker and the wrong accent on a Space switch or theme edit. Merging #62 after #159 also conflicts textually (`UpdateTint` removal, duplicate migration) | Every Spaces window | T1 |
| 3 | High | **Space accent = GM3 seed for all of Chromium.** Bubbles, dialogs, the omnibox popup, infobars and chrome://settings become Material You tones of the Space colour. Zen's panels are opaque `rgb(31,31,31)`/`rgb(244,244,244)`, and its menus are native | Every themed Space | T2 *(live check TT3)* |
| 4 | High | **#141: the "Relaunch … keep it encrypted" infobar.** `MaybeShowOSCryptAsyncAvailabilityInfoBar` runs before the automation and `--no-first-run` early returns, whenever the keychain read returns empty (prompt denied, or ACL bound to a different ad-hoc code signature) | Fresh profile, new build, unsigned app | N8 (root cause: stable signing) |
| 5 | High | **#103 removes `OpaqueFrameBackgroundView`.** Its identifier is used by `IsBackgroundEffectView` (`native_widget_ns_window_bridge.mm:114-130`, subview sort `:298`) and by occlusion (`web_contents_occlusion_checker_mac.mm:78-88, 503-527`). The NSVisualEffectView is still sorted below because of its class, but a Koan window can no longer occlude windows beneath it | Every glass window | GL4 |
| 6 | High | **#105 calls `NativeTheme::NotifyOnNativeThemeUpdated()` on every URL-bar open and close.** Each call regenerates the frame and ThemeChanged on **every window of every profile** (`browser_widget.cc:474-476` → `UserChangedTheme`), plus `GlassFrameService` re-evaluation. `SetFloatingCardGlass` is process-global state | Cmd+T / Cmd+L with "Glass URL bar" on | GL8 |
| 7 | High | **Opaque-layer CHECK window in #62.** `PaintBackground` detects an opaque layer *during* paint and only posts the fix, but `CustomCornersBackground::Paint` CHECKs on that same paint. Upstream re-creates layers on its own triggers (`MultiContentsBackgroundView::OnThemeChanged`, `BrowserFrameViewMac` eligibility) | Compact reveal, fullscreen toggle, glass on/off | §0.2, GL3 |
| 8 | Medium | **Chromium's DICE FRE (profile picker intro) on first run.** `StartupBrowserCreator` (`:768-779`) shows it before any browser window, and it is not branding-gated (`enable_dice_support` is on for Mac) *(live check NT1)* | Every new user | N2 |
| 9 | Medium | **Session-restore infobar.** Koan's default `kRestoreOnStartup=LAST` is the *default value*, so `SessionRestoreInfobarController` shows "turn off from restart" on each startup, up to its max count | Every user from the second launch | N6 |
| 10 | Medium | **Default-browser + pin + PDF infobars** from the second launch on (`infobar_utils.cc:323-361`). Zen asks once, in the welcome | Every user who didn't make Koan the default | N7 |
| 11 | Medium | **NTP colour is baked at request time** (#96 `BuildStyle`). After #62, a Space switch or theme edit leaves the old colour, and the page is opaque where Zen's empty tab shows the glass | Every empty tab | N1 |
| 12 | Medium | **Customize Chrome side panel is still reachable.** It comes from the chrome://settings/appearance "Theme" row, Chromium's NTP button and auto-open, the app menu in `kNone` mode, and the NTP footer. Its colour picker writes `kUserColor`, which a Space override silently masks | Settings, Chromium NTP | T4 |
| 13 | Medium | **Extension themes.** Zen disables them (`zen.theme.disable-lightweight=true`). Koan instead turns off glass (`GlassFrameService::IsBrowserEligibleForGlass`, `:285-290`) and turns off Space themes (#62 `IsEnabled`). A Web Store theme therefore makes Koan look like Chromium. (The earlier audit's "Firefox themes keep working" is wrong for 1.24b) | Web Store theme, theme sync | T5 |
| 14 | Medium | **Glass is macOS 26+ only and off by default on Intel** (`ui_base_features.cc:526-533`, `glass_frame_service.cc:102-106, 120-124`). A window only becomes eligible after its first activation (`tracked_browsers_`) | macOS 15 users; restored background windows | GL6 |
| 15 | Medium | **Root scrollbar scheme follows `kColorToolbar` luminance** (`chrome_content_browser_client.cc:4138-4143`). #62's website-appearance hook fixes `prefers-color-scheme` but not page scrollbars, so a light Space under Dark appearance gives dark pages with light scrollbars | Themed Spaces | T6 |
| 16 | Medium | **NSWindow appearance = `kColorToolbar` luminance** (`native_widget_mac.mm:607-621`, from `browser_view.cc:6113`). The traffic lights, the window's NSAppearance (inherited by the effect view with `appearance=nil`) and sheets flip with the Space theme | Themed Spaces | T7 *(live check GT4)* |
| 17 | Low-medium | **`ColorProviderManager` cache is unbounded per `user_color`** (`color_provider_manager.cc:101-126` only evicts by system-theme version). Every distinct accent stays resident | Theme picker use over time | T8 |
| 18 | Low | **EEA choice dialog twice** (O10, in #158). Chromium's third-party DSE NTP (a remote Bing NTP) appears if the user picks "Chromium's new tab page". The incognito NTP is Chromium's | EEA / setting / private windows | N4, N5, O10 |

**Suggested packaging** (each needs one Mac build):
1. **"First-run lockdown":** N2, N6-N8, NT1-NT6, plus the §0.3 harness profile. This is independent of the open PRs, needs only `src/` changes and feature overrides, and takes about one day.
2. **Before #62 merges:** T1 (single colour owner, rebase on #159), T2 decision, T4, §0.2 at creation sites, GL3.
3. **Before #103 and #105 merge:** GL4 (keep an opaque view under the card), GL8 (per-window restyle), GL5/GL6 decisions.
4. **Before #96 merges:** N1 (live colour via colors.css), N3.

---

## 1. Themes and colours vs Koan's theme picker

### 1.1 How it works upstream (the model)

- **ThemeService** (`chrome/browser/themes/theme_service.{h,cc}`) is per profile. It owns:
  - the extension theme (`SetTheme` `:268` h; `kCurrentThemeID` = `extensions.theme.id`, `pref_names.h:682`);
  - the user colour: `SetUserColor` `:208` h, writing `kUserColor` = `browser.theme.user_color2` (`pref_names.h:693`); variant `kBrowserColorVariant` (`:696`); grayscale `kGrayscaleThemeEnabled` (`:703`);
  - autogenerated and policy colours (`kPolicyThemeColor`);
  - device theme (`UseDeviceTheme`, ChromeOS/Win);
  - the browser colour scheme: `SetBrowserColorScheme` `:198` h, writing `kBrowserColorScheme` = `browser.theme.color_scheme2` (`pref_names.h:689`), system/light/dark.

  Prefs are registered at `theme_service.cc:310-333`. Changes to scheme, variant, grayscale and user colour fire `NotifyThemeChanged` (`:389-409`).
- **The colour key.** `ThemeService::GetColorProviderKey` (`theme_service.cc:705-786`) fills:
  - `color_mode`: incognito forced dark, isolated forced light, else the scheme;
  - `user_color`: none when isolated, else policy > autogenerated > `kUserColor`;
  - `user_color_source`: incognito → grayscale; device → accent; GTK; grayscale; baseline; else accent with a `CHECK(user_color)`;
  - `scheme_variant`;
  - `custom_theme`: null for incognito and isolated profiles.

  Koan hooks the end of this function with `koan::AdjustColorProviderKey` (`patches/chrome/browser/themes/theme_service.cc.patch`, `src/browser/ui/views/koan_look_controller.cc:185-211`). That hook installs the core mixer override and sets `color_mode` for the shadcn look only.
- **The per-window layer.** `BrowserWidget::GetColorProviderKey` (`browser_widget.cc:478-526`) takes ThemeService's key, sets `custom_theme = GetCustomTheme()`, then **re-applies the widget overrides**:
  - `color_mode_override` at `:490-492`;
  - `user_color_override` at `:493-496`, which forces `user_color_source = kAccent`.

  With a user-colour override, `GetThemeProvider()` returns the *default* provider (`:326-336`: no theme images) and `GetCustomTheme()` returns null (`:352-360`). Upstream's only writer is focus mode: `TabStripCollectionController::UpdateFocusModeTheme` (`tab_strip_collection_controller.cc:625-647`, group dialog colour, ephemeral groups skipped).
- **The mixer pipeline.** `ui::AddColorMixers` (`ui/color/color_mixers.cc`) runs ref (GM3 palette from `user_color` + `scheme_variant`) → sys → core default → native core (Mac: `ui/color/mac/native_color_mixers_mac.mm`, which picks NSAppearance by `color_mode` at `:62`) → **Koan's core override** (`patches/ui/color/color_mixers.cc.patch`) → ui → material. Then `AddChromeColorMixers` (`chrome_color_mixers.cc`) adds the Chrome mixers, native chrome, **`koan::AddKoanColorMixer`** (patch), and finally the custom theme's mixers.

  A recipe's inputs resolve against the **last** mixer, so two mixers that name each other's ids form a cycle. That cycle overflowed the stack on macOS launch, and 15565a45 fixed it. Koan's ids start at `kChromeColorsEnd` (`chrome_color_id.h:1244`; `koan_color_id.h`).
- **Propagation.** Pref change → `ThemeService::NotifyThemeChanged` → `BrowserWidget::UserChangedTheme` (`browser_widget.cc:263-305`: `RegenerateFrameOnThemeChange`, owned widgets' `ThemeChanged`). `NativeTheme` updates reach every widget through `OnNativeThemeUpdated` (`:474-476`). A widget override setter re-keys only that widget.

  **Providers are cached per key forever**: `ColorProviderManager::GetColorProviderFor` (`color_provider_manager.cc:101-126`) only evicts by system-theme version.
- **WebUI colours.** `chrome://theme/colors.css?sets=ui,chrome` (`theme_source.cc:136`) → `ThemeSource::GenerateColorsCss` (`:272-…`) resolves **every id** from `kUiColorsStart..End` and `kChromeColorsStart..End`, but not Koan's ids. `ThemeColorsSourceManager` (`theme_colors_source_manager.cc:60-110`) pre-generates it for each WebUI load, from the **window's** provider when there is one, else from ThemeService's key. So one cyclic or undefined Chrome-range recipe breaks every WebUI page, and Koan's `scripts/e2e/colors.mjs` resolves all of them.
- **Web content.** `GetPreferredColorScheme` (`chrome_content_browser_client.cc:4094-4145`):
  - regular pages and WebUI follow `web_contents->GetColorMode()`, i.e. the window's key, overrides included;
  - incognito pages follow the device;
  - forced colours win;
  - the root scrollbar follows `kColorToolbar` luminance (`:4138-4143`).
- **Sync.** `ThemeSyncableService` (`theme_syncable_service.cc`) syncs the THEMES type: extension id, user colour + variant, grayscale, autogenerated colour **and `browser_color_scheme`** (`:228-233, 341-358, 418-440, 501, 536-546`). It installs a synced extension theme through the pending extension manager.
- **The macOS appearance path.**
  - The process appearance is watched by `os_settings_provider_mac.mm:31, 131` (`effectiveAppearance` → `NativeTheme::preferred_color_scheme`).
  - **The window appearance** is set from `NativeWidgetMac::SetBackgroundColor` (`native_widget_mac.mm:607-621`), whose dark/light comes from the luminance of the widget background. `BrowserView` sets that background to `kColorToolbar` (`browser_view.cc:6113`). It reaches `NativeWidgetNSWindowBridge::SetColorMode` (`native_widget_ns_window_bridge.mm:1387-1394`, `setAppearance:` Aqua/DarkAqua).
  - `color_utils::IsDark` ignores alpha, so a fully transparent toolbar colour would read as dark.
  - Context menus copy the window appearance (`chrome_swizzle_services_menu_updater.mm:96`).

### 1.2 Inventory

| # | Upstream item (pristine file:line) | What it does | Conflicts with Zen? | Recommendation | Koan today / PR |
|---|---|---|---|---|---|
| T-a | `kBrowserColorScheme`, `SetBrowserColorScheme` (`theme_service.cc:683-696`); settings "Mode" select (`appearance_page.html.ts:65-80`) | system/light/dark for the browser; web pages follow it (via the colour key) | No. It is exactly `zen.view.window.scheme` 2/1/0 (FJS:1864), which websites also follow | **Reuse.** It is the Zen scheme (#62 already does this). The pref also **syncs** (THEMES); Zen's doesn't, which is acceptable | #62 reuses it |
| T-b | `kUserColor` / `kBrowserColorVariant` / grayscale; Customize Chrome's colour picker (`theme_color_picker_handler.cc:96-111`) | profile GM3 seed | Zen has no profile colour; the colour is per Space | **Lock** in Spaces windows (masked by the override anyway); **redirect** the UI (T4) | #159 G1 stops writing it; #62 migration restores old values |
| T-c | Widget `user_color_override` / `color_mode_override` (`browser_widget.cc:490-496`) | per-window seed and mode, applied after ThemeService and Koan's key hook | This is the right knob for Zen's per-window Space theme | **Reuse with one owner** (T1) | three writers (upstream, #159, #62) |
| T-d | GM3 dynamic colour: ref/sys mixers from `user_color` + `scheme_variant` | **every** Chromium colour is derived from the seed (menus, bubbles, dialogs, omnibox popup, infobars, find bar, settings) | **Yes.** Zen's accent tints the sidebar and pill only, its panels are fixed greys (spec 15.4), and its menus are native | **Override** (T2). Feed the seed only to Koan's ids, or pin `scheme_variant` / feed Chromium's leaf ids from Zen values | #62 passes the primary as `user_color` → whole-UI tonal palette *(live check TT3)* |
| T-e | Extension themes (`SetTheme`, `GetCustomTheme`, theme images in `ThemedBackground`) + Web Store link in settings (`appearance_page.html.ts:14-25`) + sync installs | images and colours over the frame and NTP | **Yes.** Zen disables LWT (`zen.theme.disable-lightweight=true`, FJS:1802; patch `LightweightThemeConsumer-sys-mjs.patch:19`) and hides the Themes UI | **Lock** (T5): ignore the custom theme in Koan windows, hide the settings Theme row's Web Store path | Glass off + Space themes off under an extension theme: Chromium look |
| T-f | Policy theme `BrowserThemeColor` (`kPolicyThemeColor`, `theme_service.cc:321, 389-393, 739-742`) | autogenerated seed, managed | n/a for Zen | **Reuse**, low priority. Under #62 a Space override wins over policy; document this | — |
| T-g | Incognito / guest / isolated (`theme_service.cc:721-725, 750-755, 782-784`; `chrome_content_browser_client.cc:4118-4122`) | dark, grayscale, no custom theme; incognito pages follow the device | Zen private windows have their own background, and pages follow the window scheme | **Reuse** (keep Chromium's incognito look). Keep Space overrides out of OTR windows | `AdjustColorProviderKey` and #62 skip OTR (good). Check #159's `ApplySpaceColor` skips OTR too (an override forces `kAccent` over incognito grayscale) |
| T-h | Customize Chrome side panel: `IDC_SHOW_CUSTOMIZE_CHROME_SIDE_PANEL` / `_TOOLBAR` (`browser_command_controller.cc:1592-1602, 1951-1954`), app menu (`app_menu_model.cc:1099-1107`), `kActionSidePanelShowCustomizeChrome*` (`browser_actions.cc:494, 2268, 2388`), NTP auto-open (`new_tab_page_feature_promo_helper.cc:43-128`), NTP footer (`footer_context_menu.cc:93`), settings Theme row, page handler `SetDefaultColor/UseDefaultTheme/SetFollowDeviceTheme/SetBackgroundImage` (`customize_chrome_page_handler.cc:226-240, 903`) | Chrome's theme, colour, NTP-background and toolbar editor | **Yes.** Zen's equivalent is the theme picker | **Redirect** both commands and the action to Koan's picker (per-window `ActionItem`, as `InstallNewTabAction`). Hide the settings Theme row or point it at the picker (T4) | none |
| T-i | `ThemeSource::GenerateColorsCss` / `ThemeColorsSourceManager` (`theme_source.cc:272`, `theme_colors_source_manager.cc:91`) | resolves every ui+chrome id for each WebUI | No | **Reuse**, and keep the invariant: Koan recipes in the Chrome range use values or ids only from earlier mixers. `colors.mjs` runs in every look × scheme × Space theme | `colors.mjs` (15565a45); extend to #62 Space keys (TT8) |
| T-j | Window NSAppearance from `kColorToolbar` luminance (`native_widget_mac.mm:607-621`, `browser_view.cc:6113`) | traffic-light contrast, window appearance | Partly. Zen's window appearance follows the scheme / colour decision (`shouldBeDarkMode`) | **Reuse**, but never make `kColorToolbar` transparent (glass does it through `CustomCornersBackground` alpha, which is correct). Its luminance must equal Zen's dark decision (T7) | #103 keeps `kColorToolbar` opaque (good) |
| T-k | Root scrollbar scheme from `kColorToolbar` (`chrome_content_browser_client.cc:4138-4143`) | page scrollbars | Yes, once the Space mode ≠ appearance | **Override** in the same `PrefersDarkWebColorScheme` hook (T6) | #62 hooks only `preferred_color_scheme` |
| T-l | Focus-mode colour (`tab_strip_collection_controller.cc:625-647`) | per-window override from the group colour | Yes (G1) | **Lock** through #159's hook; then **redirect** to T1's owner | #159 |
| T-m | Tab-group colour ids (`GetTabGroupDialogColorId`, `kColorTabGroup*`), resolved under the window key | group chips and dialog colours shift with the seed | Spaces have no group colour in Zen | **Reuse** only as Koan's Space-colour enum. Never read them back to paint (they change with the override) | #159 uses `TintSeed`, not the dialog id (good) |
| T-n | `ColorProviderManager` cache (`color_provider_manager.cc:101-126`) | one provider per distinct key, never evicted | No | **Reuse** with a rule: no continuous values into `SetUserColorOverride` (drag, animation) (T8) | #62 freezes tokens while dragging (good); presets and Library previews still add keys |
| T-o | Native scheme: `NativeTheme` / `os_settings_provider_mac.mm` | system dark/light | No | **Reuse** (`kSystem`) | — |

### 1.3 Changes

| ID | Change | Where | New patch? |
|---|---|---|---|
| T1 | **Single owner.** `KoanThemeController` (after #62) is the only code that sets the browser widget's `user_color_override` / `color_mode_override`. #159's `koan::ApplySpaceColor()` keeps swallowing upstream's `UpdateFocusModeTheme`, then calls the owner instead of writing the widget. Keep one `MigrateWorkspaceTint()`. Add a DCHECK in the owner: the override after `Update()` equals what it set (leak detector, as G14) | `src/` (#62 rebased on #159) | no |
| T2 | **Decide what the seed reaches** *(TT3 first)*. Option A: no `user_color_override`. Set only `color_mode_override`, and pass the Space accent through Koan's own ids (`AddSpaceTintColors`), so Chromium surfaces keep Zen's fixed greys. Option B: keep the override but have the Koan core mixer pin the sys/surface tokens to Zen values for dialog, bubble, menu and omnibox popup. A is simpler, matches Zen, and also removes T-e's theme suppression | `src/browser/ui/color/*` | no |
| T3 | **Scheme = `kBrowserColorScheme`** (reuse). The scheme buttons, the settings "Mode" select and a new View > Appearance menu (T9) all write it | `src/` | no |
| T4 | **Redirect Customize Chrome.** Re-point `kActionSidePanelShowCustomizeChrome`, `…Toolbar`, `…Footer` and `IDC_SHOW_CUSTOMIZE_CHROME_*` to the picker, as `koan_url_bar_controller.cc` `InstallNewTabAction` does. Turn off the NTP auto-open (`CustomizeChromeAutoOpened…` path; only reachable with Chromium's NTP). Hide the settings Theme row (koan settings rows or a `pageVisibility` override) | `src/`, feature overrides | no |
| T5 | **Lock extension themes** (Zen `disable-lightweight`). In Koan windows return no custom theme (`GetCustomTheme`), giving glass and Space themes regardless of `UsingExtensionTheme()`. Hide the Web Store theme link. Leave the theme installed (no data loss). Alternative: keep Chromium's behaviour and document it as a divergence | hook in `browser_widget.cc` (new patch) or `AdjustColorProviderKey` + glass eligibility hunk | 1 small |
| T6 | The root scrollbar scheme follows the website appearance in the same hook as `PrefersDarkWebColorScheme` | existing `chrome_content_browser_client.cc.patch` (#62) | no |
| T7 | `kColorToolbar` luminance must equal Zen's `shouldBeDarkMode` result for the Space (mixer unit test). Never transparent | `src/` + unit test | no |
| T8 | Bound accent keys: quantise the seed (for example to 8-bit OKLCH steps), and never call the override setter per frame | `src/` | no |
| T9 | View > Appearance (Auto/Light/Dark) in the Mac menu bar, as Zen's `ZenMenubar.mjs:5-45` | `browser_native_widget_mac.mm.patch` hunk or main-menu builder hook | hunk |

### 1.4 Risks in the open PRs

**#62 (theme picker):**
- **Crash:** the one-paint CHECK window. `PaintBackground` posts `InvalidateBackground` when it finds an opaque layer, but `CustomCornersBackground::Paint` already CHECKed on that paint (`custom_corners_background.cc:330-331`). Make layers translucent where they are created or recreated (§0.2), not lazily during paint. Also handle `MultiContentsBackgroundView` (its `ThemeChanged` recreation makes a new *textured* layer, opaque by default) and `BrowserFrameViewMac` (it sets `SetFillsBoundsOpaquely(true)` when glass becomes ineligible: fullscreen, Reduce Transparency).
- **Looks like Chromium:** T2. The primary colour becomes the GM3 seed, so the Material You palette spreads everywhere.
- **Looks like Chromium:** T5. An extension theme turns Space themes off.
- **Looks like Chromium:** scrollbars (T6).
- **Conflict with #159:** T1, two override writers and a duplicate migration.
- **Perf:** T8, and the cost of the cross-fade repaint (S7 in its own review, not measured).
- **Must test:** a themed Space whose decision is Light under Dark appearance, for WebUI pages: `colors.css` comes from the window provider and `prefers-color-scheme` from the hook. Its own review flagged this.

**#159** (not in scope, but it touches colour): `ApplySpaceColor` must skip OTR windows and must become T1's client when #62 lands.

### 1.5 Mac tests (themes)

- **TT1.** Two windows on Spaces with different colours. Each window's accent differs. Switch Spaces 10 times quickly: no flash of the user colour or of the previous Space (T1).
- **TT2.** A themed Space with the picker open: drag the dot for 5 s. The `ColorProviderManager` provider count (DevTools-free: a `chrome://koan-internals` counter, or a `DLOG` count) grows by at most the number of drops, not by the number of moves (T8).
- **TT3.** A blue Space. Open the page-info bubble, the download bubble, a JS `alert`, the find bar, a context menu, and chrome://settings. Capture and compare with Zen (`rgb(31,31,31)` panels, native menus). Record whether the Material You tint appears (T2).
- **TT4.** Install a Web Store theme. Spaces windows keep Koan's look and glass (T5), and settings has no Web Store theme path.
- **TT5.** Under Dark appearance, a light Space: a web page has `prefers-color-scheme: dark` **and** dark root scrollbars. chrome://settings follows the window (T6).
- **TT6.** Settings > Appearance > Mode = Light: the picker's scheme button shows Light, and vice versa (T3).
- **TT7.** App-menu path (enter `kNone` mode) and the chrome://settings "Theme" row: both open Koan's picker, never the Customize Chrome panel (T4).
- **TT8.** `colors.mjs` in every look × scheme × {no theme, 1-dot, 3-dot, black-and-white preset} Space. No undefined, placeholder or cyclic colour, and chrome://settings renders.
- **TT9.** An incognito window from a themed window stays Chromium's dark incognito look, and its pages follow the device.
- **TT10.** macOS appearance toggled in System Settings with the scheme on Auto: the window, menus and traffic lights follow within 1 s.

---

## 2. Glass / translucent window on macOS vs Koan's glass

### 2.1 How it works upstream (the model)

- **The feature and the service.**
  - `features::kGlassFrame` (`ui_base_features.cc:500`) is off by default. `IsGlassFrameEnabled()` is true only on **macOS ≥ 26** (`:526-533`).
  - `GlassFrameService` (`glass_frame_service.cc`) uses the local-state pref `glass_frame.enabled` (`pref_names.h:2280`). That pref defaults to **false on x86** (`:102-106`); on arm64 its default comes from `kGlassFrameEnabledByDefault` (`:120-124`).
  - The service allows glass only with the pref on, **Battery Saver** off and **no window capture** (`:198-201`).
  - A window is eligible only if it is a normal window, tracked (**after its first activation**, `:209-218`), not fullscreen (`:278-284`), and not using an **extension theme** (`:285-290`).
  - At most **one** glass window (`kMaxGlassWindows = 1`, `.h:55`; enforced at `:159`, `:307-313`). There are opt-in and opt-out IPH promos.
- **The Mac window.** `BrowserNativeWidgetMac::UpdateGlassEligibility` (`browser_native_widget_mac.mm:1039-1095`):
  - CHECKs that eligibility actually changed;
  - makes the layer, compositor and NSWindow non-opaque, with a background of `windowBackgroundColor` at alpha **0.001** (pure `clearColor` makes AppKit invalidate continuously: CPU and energy, `:1074-1079`);
  - adds an `NSGlassEffectView` subclass with `hitTest:` → nil (`:155-166`) and a tint `NSView`;
  - adds an **`OpaqueFrameBackgroundView`** (`:168-178`, identifier `kOpaqueFrameBackgroundViewIdentifier`) under the non-glass part.

  Geometry: the glass covers the tab strip and toolbar, or the vertical tab column (`:297-345, 1098-1170`). The tint is `kColorFrameActive`/`Inactive` at a fixed opacity (`:114-140, 1172-1222`).

  Going back to opaque waits for the compositor's next successful presentation before `RemoveGlassBackground` (`:995-1028`), which avoids a white flash.
- **AppKit integration.** `IsBackgroundEffectView` (`native_widget_ns_window_bridge.mm:114-130`) treats `NSVisualEffectView`, `NSGlassEffectView` **and** the opaque-identifier view as background views. `SubviewSorter` (`:298`) keeps them below the compositor view.

  The web-contents occlusion checker (`web_contents_occlusion_checker_mac.mm:503-527`) lets a non-opaque window occlude others only if its background alpha is ≥ 1 **or** it contains the opaque-identifier view. Manual occlusion is shut off on macOS 26 by default (`kShutoffMacOS26Entirely`, `:41-71`).
- **The views.**
  - `BrowserFrameViewMac` sets `layer()->SetFillsBoundsOpaquely(!glass_eligible)` (`browser_frame_view_mac.mm:90-101, 615-620`) and skips the inactive repaint in glass mode (`:337-342`).
  - The tabbed layout's glass mode paints the frame and vertical-tab region at alpha 0, the corner pieces through `CustomCornersBackground::SetAlpha`, and a background blur for the expand-on-hover strip (`browser_view_tabbed_layout_impl.cc:1694-1700, 1750-1765, 1840`).
  - `CustomCornersBackground` CHECKs `!layer->fills_bounds_opaquely()` in `SetUseBackgroundBlur` (`custom_corners_background.cc:154-160`, plus `CHECK(!use_background_blur)` without a layer) and in `Paint` (`:330-331`).
  - `MultiContentsBackgroundView` (the gutter) switches between a solid-colour and a textured layer on `OnThemeChanged` (`multi_contents_background_view.cc:12-60`), with `CHECK(AsTextured())` and `CHECK(AsSolidColor())`.
  - `ContentsWebView::UpdateBackgroundColor` requires an **opaque** `kColorWebContentsBackground` (`contents_web_view.cc:210-232`, `DumpWithoutCrashing`, a future CHECK).
- **Label DCHECK.** `views::Label::OnPaint` (`label.cc:973-1008`, DCHECK builds, not ChromeOS) walks up the parents: the first parent with an opaque `background()` is accepted; otherwise the first parent **layer must fill its bounds opaquely**. On glass, every label on a translucent surface trips it unless it calls `SetSubpixelRenderingEnabled(false)` or `SetSkipSubpixelRenderingOpacityCheck(true)` (upstream does this in `browser_view_app_layout_impl.cc:399-405`, `multi_contents_view_mini_toolbar.cc:106`).
- **Fullscreen and the menu bar.** Fullscreen windows are not eligible, so glass goes opaque on entry. The immersive toolbar lives in a separate overlay widget (`fullscreen_toolbar_controller`, audit 2 §3). `BrowserFrameViewMac::ShouldHideTopUIInFullscreen` follows the toolbar style; Koan already forces `TOOLBAR_NONE` where its toolbar is hidden (`browser_frame_view_mac.mm.patch`).
- **Zen.** `ZenWindowMaterialView : NSVisualEffectView` wraps the content view (`src/widget/cocoa/nsCocoaWindow-mm.patch:21-127`):
  - material from `zen.widget.macos.window-material` (1 = HUDWindow; 2..7 others), behind-window, `appearance=nil`, not emphasized;
  - the state follows the window's active state unless `zen.view.grey-out-inactive-windows=false` (`VibrancyManager-mm.patch:18`);
  - the master switch is `zen.widget.macos.window-vibrancy=true`, a live callback;
  - the chrome document is transparent on macOS (`zen-theme.css:309-320`).

  Zen has **no** macOS-version gate, battery rule, window cap or capture rule. The page card is opaque for real pages and transparent for the empty tab. The acrylic URL bar and sidebar are prefs, off in release (FJS:1797-1798).

### 2.2 Inventory

| # | Upstream item | Does | Conflicts with Zen? | Recommendation | Koan / PR |
|---|---|---|---|---|---|
| GL-a | `kGlassFrame` + macOS ≥ 26 gate (`ui_base_features.cc:500, 526-533`) | glass exists at all | **Yes** for macOS 13-15 (Zen has vibrancy there) | **Reuse** for 26+. Document that <26 stays opaque, or **override** later (the compositor non-opaque path was never shipped < 26; risky) | #103 enables it on Mac |
| GL-b | `glass_frame.enabled` pref; default off on x86 (`glass_frame_service.cc:102-124`) | the user toggle | Zen's equivalent is `window-vibrancy` | **Reuse** as "Translucent window". Set the default to true on all archs only after a perf pass on Intel (GL6) | #103 reuses it |
| GL-c | `kMaxGlassWindows = 1` (`.h:55`) | one glass window | **Yes** (Zen: every window) | **Override** (#103) + perf budget (GL7) | #103 patches it |
| GL-d | Battery Saver disables glass (`:198-201, 232-236`) | energy | Zen has none | **Override** (#103). Keep in mind under GL7 | #103 |
| GL-e | Window capture disables glass (`:249-258`) | avoids capture artefacts | Zen has none | **Reuse** (invisible to the user, safe) | kept |
| GL-f | Fullscreen not eligible (`:278-284`) | opaque in fullscreen | Zen in native fullscreen *(live check GT6)* | **Reuse** unless GT6 shows Zen keeps vibrancy | kept |
| GL-g | Extension theme not eligible (`:285-290`) | | Zen ignores themes | **Lock** via T5 (or patch out this rule together with T5) | kept: a Web Store theme kills glass |
| GL-h | Eligible only after first activation (`tracked_browsers_`, `:209-218`) | | Zen: all windows | **Override**: track on creation for TYPE_NORMAL | not handled: restored background windows open opaque *(live check GT7)* |
| GL-i | `NSGlassEffectView` (Liquid Glass) + tint at fixed opacity (`browser_native_widget_mac.mm:114-140, 1083-1095, 1172-1222`) | look | **Yes** (Zen: NSVisualEffectView HUD material) | **Override** (#103: `CreateGlassBackgroundView`, tint alpha from `kColorKoanGlassTint`) | #103 |
| GL-j | `OpaqueFrameBackgroundView` + identifier (`:168-178, 1101-1113`; bridge `:114-130, 298`; occlusion `:78-88, 524-527`) | opaque backing under non-glass parts; marks the window as occluding | Zen has none (but its page card is opaque) | **Keep (reuse) an identifier view**, a zero-area or card-sized one, so occlusion and sorting stay right (GL4) | **#103 deletes it** |
| GL-k | 0.001-alpha window background (`:1074-1079`) | avoids continuous invalidation | — | **Reuse. Never** `clearColor` | #103 keeps it (good) |
| GL-l | `RemoveGlassBackground` deferred to presentation (`:995-1028, 1054-1066`) | no white flash | — | **Reuse** | #103 keeps it |
| GL-m | `CHECK_NE(last_is_glass_eligible_, is_glass_eligible)` (`:1049`) | | — | Never call `UpdateGlassEligibility` from Koan except through the service | #103 OK (it goes through `GlassFrameService::OnThemeChanged`) |
| GL-n | `BrowserFrameViewMac` layer opaque when not eligible (`browser_frame_view_mac.mm:95, 620`) | | — | **Reuse**, but #62 must not paint translucent onto it while ineligible; or make Koan's painter's layer translucent after this flip (§0.2) | #62 relies on a posted fix |
| GL-o | `CustomCornersBackground` CHECKs (`custom_corners_background.cc:154-160, 330-331`) | | — | **Reuse. Never patch them out.** Views with that background never get an opaque layer (GL3) | #103 notes the rule; #62 implements it lazily |
| GL-p | `Label` subpixel DCHECK (`label.cc:973-1008`) | | — | **Reuse**: every Koan label on glass calls `SetSubpixelRenderingEnabled(false)`. Never "fix" it with an opaque layer (that conflicts with GL-o) | #62 does it for picker labels; audit the sidebar rows, the URL pill, toasts (GT9) |
| GL-q | `MultiContentsBackgroundView` layer-type switch (`:31-60`) | | — | **Reuse**: after `PaintsWindowBackground` flips, `ThemeChanged` must run (#62 does). The new textured layer must be made translucent at once | #62 posted fix |
| GL-r | `ContentsWebView` opaque background requirement (`contents_web_view.cc:210-232`) | | Zen's empty tab is transparent | **Reuse** (opaque) for now. A transparent empty tab needs `background_visible_=false` plus a transparent page (N1, later) | #96 opaque |
| GL-s | Tabbed-layout glass mode; `kGlassExpandOnHoverEnabled` (`ui_base_features.cc:501`) | expand-on-hover strip with blur | Koan has its own sidebar | **Lock** (#103 sets the param false) | #103 |
| GL-t | Glass IPH (`kIPHGlassFrameOptIn/OptOut`) | promos | Zen has none | **Lock** (#103) | #103 |
| GL-u | Reduce Transparency | Chromium: nothing in `GlassFrameService` | Zen: the system effect view goes opaque; the acrylic options obey it | **Override** (#103 `IsWindowGlassAllowed` + notification) | #103 |
| GL-v | Fullscreen toolbar overlay widget / menu-bar reveal (`BrowserFrameViewMac` fullscreen controller) | the toolbar slides in a separate widget under the menu bar | Zen drops the toolbox shift (`browser-fullScreenAndPointerLock-js.patch:9-11`) | Covered by F1-F3 (#157). The overlay widget is never glass; #62 paints the gradient there through screen coordinates | #157, #62 |
| GL-w | `NativeWidgetMac::SetBackgroundColor` → NSAppearance (T-j) | | the effect view with `appearance=nil` inherits it | **Reuse**: correct only if `kColorToolbar` luminance = Zen's decision (T7) | — |

### 2.3 Changes

| ID | Change | Where | Patch? |
|---|---|---|---|
| GL1 | Land #103's model (every window, whole-window `NSVisualEffectView` HUD, `kColorKoanGlassTint`, Reduce Transparency) **with GL4** | #103 | existing hunks |
| GL2 | Toolbar and top container clear via `CustomCornersBackground` alpha, never via a transparent `kColorToolbar` (T-j) | #103 (already) | — |
| GL3 | **Creation-site translucency** (§0.2). A Koan helper `koan::MakeTranslucentLayer(view)` is called right after every `SetPaintToLayer` Koan does, plus synchronous fix-ups from the two upstream re-creation points, in `MultiContentsBackgroundView::OnThemeChanged` (hook or observer) and the `BrowserFrameViewMac` eligibility callback (Koan subscribes *after* it, via `GlassFrameService::RegisterGlassFrameEligibilityChangedCallback`). Keep #62's lazy path as a DCHECK-only detector | `src/` + possibly 1 hunk in `multi_contents_background_view.cc` | maybe 1 small |
| GL4 | **Keep an opaque-identifier view.** Instead of deleting `opaque_background_view_`, keep it with `identifier = kOpaqueFrameBackgroundViewIdentifier`, sized to the page card (or 1×1 and hidden). Then `IsBackgroundEffectView` sorting and `windowCanOccludeOtherWindows` behave as upstream. *(If occlusion is wanted off, i.e. windows behind a glass window keep rendering because they show through the blur, then say so and test energy, GT8)* | existing `browser_native_widget_mac.mm.patch` | hunk change |
| GL5 | **Decide macOS < 26.** Either document "glass needs macOS 26" in ZEN_PARITY, or later port the NSVisualEffectView path without the `@available(macOS 26)` block. The compositor's non-opaque path below 26 is not used by Chromium: high risk | docs | — |
| GL6 | Intel Macs: `glass_frame.enabled` defaults false (`:102-106`). Decide after GT8 on an Intel Mac or a Rosetta-free x86 build. Until then, document it | docs / pref default | — |
| GL7 | **Perf budget** for "every window" + no Battery Saver rule: GT8. If it fails, keep glass on the key window only, while the others are flat. Zen already makes inactive windows flat, so the cost is only the active window: verify that `NSVisualEffectStateFollowsWindowActiveState` really stops the blur | measurement | — |
| GL8 | **#105: restyle per window, not process-wide.** Replace `NotifyOnNativeThemeUpdated()` with `browser_view_->GetWidget()->ThemeChanged()` on that window (or set the card's colours directly as values on the card's views). Make `SetFloatingCardGlass` per controller, not global | #105 `src/` | no |
| GL9 | Track windows for glass at creation (GL-h) | existing `glass_frame_service.cc.patch` | hunk |

### 2.4 Risks in the open PRs

**#103:**
- **Correctness:** GL4. Deleting the opaque view changes occlusion: a Koan window no longer occludes windows beneath it, so their tabs keep painting (that matters only where manual occlusion is on; it is off on macOS 26 by default), and it drops the identifier's sort rule. Keep a marked view.
- **Crash:** none new in #103 itself; the CHECK risk comes from #62 (GL3).
- **Looks like Chromium:**
  - opaque windows after relaunch until each is activated (GL-h, GT7);
  - opaque with any Web Store theme (GL-g);
  - opaque on Intel by default (GL6);
  - opaque on macOS 15 (GL5).
- **Perf:** every window non-opaque and Battery Saver ignored (GL7). Upstream's cap of one window was deliberate.

**#105:**
- **Perf / flicker:** GL8. `NotifyOnNativeThemeUpdated()` on each open/close of the Cmd+T card re-keys and regenerates frames in every window. With several windows that is visible jank, and it also re-runs `GlassFrameService::OnThemeChanged` eligibility.
- **Global state:** `SetFloatingCardGlass` turns the other windows' cards glass too.
- **Blur:** `top_container->layer()->SetBackgroundBlur` is set outside `CustomCornersBackground`'s own blur bookkeeping (`backdrop_filter_bounds_`). Today nothing upstream calls `SetUseBackgroundBlur` on the top container (`browser_view_tabbed_layout_impl.cc:1761, 1840` touch other views), but a rebase could change that. Add a REBASE row.
- **Looks like Chromium:** none. It is off by default, as in Zen.

**#62:** see §1.4. On the glass side: the lazy opaque-layer fix (GL3), and painting the gradient onto a frame layer upstream has just made opaque (GL-n).

### 2.5 Mac tests (glass)

DCHECK build, so the Label DCHECK fires.
- **GT1.** A fresh window over a high-detail backdrop (spec 15 method): the sidebar, top row and gutter blur the backdrop. The page card is opaque, the window is active, and the tint is `rgba(0,0,0,.4)` dark / none light.
- **GT2.** Deactivate (click another app): flat, no blur, no tint. Reactivate: glass returns with no white flash.
- **GT3.** Toggle Reduce Transparency in System Settings: opaque within 1 s, glass back after. No crash. Repeat with compact mode on and with the theme picker open (GL3 CHECKs).
- **GT4.** Light Space under Dark appearance, then the reverse: traffic lights visible when inactive, context-menu appearance matches the window (T7).
- **GT5.** Enter and exit native fullscreen 10 times, with compact mode on and off, and with the content card on a page and on an empty tab. No CHECK, no black panel, and glass is back after exit.
- **GT6.** Zen reference: is native fullscreen vibrant or flat? Decide GL-f.
- **GT7.** Quit with 3 windows open. Relaunch with session restore: all 3 are glass without clicking each one (GL9).
- **GT8.** Energy: 3 glass windows, a YouTube video in the front one, 5 min. Compare Activity Monitor "Energy Impact" and WindowServer CPU with glass off, with upstream's one-window cap, and with Zen. Repeat on battery with Low Power Mode (GL7).
- **GT9.** DCHECK build: hover every sidebar row, open the URL bar, a toast, the Library and the welcome with glass on. No `Label` subpixel DCHECK and no `CustomCornersBackground` CHECK.
- **GT10.** #105 on, 3 windows: Cmd+T/Esc 20 times in one window. The other windows don't repaint (Quartz Debug flash screen updates) and frame time stays < 16 ms (GL8).
- **GT11.** Install a Web Store theme: glass stays (if T5) or the divergence is documented. Remove the theme: glass returns.
- **GT12.** Share the window in a Meet tab (window capture): glass off during the capture, back after, no crash.

---

## 3. New Tab Page and first run vs Koan's empty NTP and Zen's onboarding

### 3.1 How it works upstream (the model)

- **NTP choice.** `chrome://newtab` is rewritten by the browser URL handlers (`chrome_content_browser_client.cc:5231-5258`), in this order:
  1. extension `chrome_url_overrides`;
  2. **`HandleNewTabPageLocationOverride`** (`:945-970`: the `NewTabPageLocation` policy pref `kNewTabPageLocationOverride`, non-OTR);
  3. **`search::HandleNewTabURLRewrite`** (`search.cc:410-430`) → `NewTabURLDetails::ForProfile` (`:166-231`).

  `ForProfile` picks:
  - OTR → none (incognito / guest NTP);
  - Google DSE → **WebUI NTP** `chrome://new-tab-page/`;
  - otherwise the **DSE's `new_tab_url`** if valid, HTTPS and allowed (a **remote third-party NTP**, for example Bing's);
  - else `chrome://new-tab-page-third-party/` (the "local" NTP).

  `GetNewTabPageURL` (`:348`) feeds `IsNTPOrRelatedURL`, `IsInstantNTPURL` and the instant-process assignment (chrome:// is excluded, `:350-357`). `IsNTPURL` (`:274-291`) only knows the two WebUI NTP origins.

  NTP prefs: `NewTabPage.ModulesVisible`, `.FooterVisible`, the custom background, shortcuts (`pref_names.h:1647-1745`). Customize Chrome auto-opens from the NTP (`new_tab_page_feature_promo_helper.cc`). Split view's "new tab" is a tab-search WebUI (`kChromeUISplitViewNewTabPageURL`, `webui_url_constants.h:323-324`). `!defined(OFFICIAL_BUILD)` only adds the NTP's "foo" dev module binding (`new_tab_page_ui.cc:158, 1172`).
- **First run on Mac.**
  - `first_run::IsChromeFirstRun()` (`first_run.cc:352-362`): sentinel absent, or `--force-first-run`; **false with `--no-first-run`**.
  - The old first-run dialog (metrics plus default browser) is **branding-only** (`first_run_internal_posix.cc:47-66`): not in Koan.
  - The **DICE FRE** is not gated by branding (`enable_dice_support` includes Mac, `components/signin/features.gni:13`). `StartupBrowserCreator` (`:768-779`) calls `FirstRunService::OpenFirstRunIfNeeded` before any browser window when `ShouldOpenFirstRun` holds (`first_run_service.cc:298-301`: first run, not `--no-first-run`, a regular profile, `kFirstRunFinished` unset). Skip reasons are force-signin, already signed in, or disabled by policy (`profile_picker.cc:242-264`). This is the profile-picker window with the intro ("Make Chromium your own" / sign in / "Don't sign in"); its later steps can include the search choice and default browser *(live check NT1)*.
  - The **profile picker at startup** appears with several profiles (`kBrowserShowProfilePickerOnStartup`).
- **Search engine choice.** It is compiled in on all desktops (`CHOICE_SCREEN_IN_CHROME = !(android||fuchsia)`, `buildflags.gni:11`), not branding-gated. `SearchEngineChoiceService::GetStaticChoiceScreenConditions` (`search_engine_choice_service.cc:576-640`) checks, in order:
  1. the `--disable-search-engine-choice-screen` switch;
  2. the region (EEA);
  3. a choice already made, managed, overrides, or policy;
  4. the location;
  5. the account.

  `--force-search-engine-choice-screen` wipes the choice (`:1021`). The dialog itself comes from `SearchEngineChoiceDialogService` in the browser window. Koan's welcome sets the DSE via `SetUserSelectedDefaultSearchProviderByPrepopulateId` (`koan_welcome.cc:1095-1127`); whether that counts as a completed choice is O10.
- **Startup infobars and bubbles** (`AddInfoBarsIfNecessary`, `infobar_utils.cc:160-364`), in order:
  1. bad flags;
  2. automation;
  3. the **session-crashed bubble** ("Restore pages?", `:216-224`, B16);
  4. a `--test-type` / automation return (`:229-232`);
  5. the obsolete-system infobar;
  6. **OSCrypt availability** (`:288` → `:118-158`). It is feature-on on Mac (`ui_features.cc:593-597`) and runs **before** the `--no-default-browser-check` / `--no-first-run` return;
  7. that return (`:306-311`);
  8. the **session-restore infobar** (`kSessionRestoreInfobar` on, `ui_features.cc:392`; `session_restore_infobar_controller.cc:34-67`: shown when `kRestoreOnStartup` is at its **default value** and is LAST or NTP, cookies allowed, under the max count);
  9. after the first run, **default browser → pin → PDF** (`:323-361`; `kPdfInfoBar` on, `ui_features.cc:191`; `kSeparateDefaultAndPinPrompt` arms, `:193-227`).

  The Google API-keys infobar is already gated by Koan (`infobar_utils.cc.patch`).
- **OSCrypt on Mac.** `KeychainKeyProvider::GetKeyTask` (`keychain_key_provider.mm:40-72`) reads the "Koan Safe Storage" generic password (Koan renames it in `keychain_password_mac.mm.patch`). An empty result (access denied, the prompt not answered, or an ACL bound to a different code signature) becomes `kTemporarilyUnavailable`, so encryption is unavailable and the infobar shows. `--use-mock-keychain` uses a fake keychain, which is why the Mac loop never sees it.
- **What's New** is branding-only (`whats_new_util.cc:35-43`; `kForceEnabled` off). Not in Koan.
- **Zen.**
  - **New tab and welcome.** `zen.urlbar.replace-newtab=true` (FJS:1817): Cmd+T opens the floating URL bar, and the last tab closed selects an empty `about:blank` tab (`Tabbrowser-sys-mjs.patch:845-852`). The NTP content prefs are all off, and the Home settings category is hidden. `about:welcome`, pre-onboarding and UITour are off; Zen's own welcome runs on `zen.welcome-screen.seen=false`.
  - **Session restore.** It always restores (`browser.startup.page=3`, and `SessionStartup` forced to restore). There is no crash prompt and no restore infobar.
  - **Prompts.** The default browser is asked in the welcome. `browser.shell.checkDefaultBrowser` stays true (a Zen gap; whether Firefox's own prompt ever shows on Zen is a *live check*). The profile chooser is off (`browser.profiles.enabled=false`).

### 3.2 Inventory

| # | Upstream item | Does | Conflicts with Zen? | Recommendation | Koan / PR |
|---|---|---|---|---|---|
| N-a | `NewTabURLDetails::ForProfile` (`search.cc:166-231`) | picks WebUI / third-party / remote NTP | **Yes** (Zen: empty tab) | **Redirect** at the top, after OTR (#96 hook) | #96 |
| N-b | Remote DSE `new_tab_url` (`:209-230`) | a non-Google engine's own page, from the network | Yes | Suppressed by N-a for Koan's page. With "Chromium's new tab page" selected it comes back: document it or always use the local third-party page there (N4) | #96 |
| N-c | `NewTabPageLocation` policy (`chrome_content_browser_client.cc:945-970`) | admin URL | n/a | **Reuse** (it wins; it runs before search's rewrite) | #96 OK |
| N-d | Extension NTP override | | Zen: Cmd+T never shows a page; an extension's newtab shows on... *(live check)* | **Reuse** (wins). Cmd+T stays the floating bar (#36) | #96 OK |
| N-e | WebUI NTP + Customize Chrome auto-open + footer + modules + `foo` (non-official) | Google NTP | Yes (B6) | **Redirect** (N-a). Keep `chrome://new-tab-page` reachable only by typing; turn off the auto-open (T4) | #96 |
| N-f | Incognito / guest NTP (OTR branch `:173-177`) | "You've gone Incognito" | Zen's private tab is empty *(live check NT9)* | **Reuse** for now; **override** later if NT9 shows Zen's empty page | #96 keeps Chromium's |
| N-g | `IsNTPURL` / `IsInstantNTPURL` / instant process | NTP-specific behaviour in the omnibox, tabs, metrics | — | **Reuse.** `chrome://koan-newtab` is chrome:// so it is never an instant renderer. Check the omnibox's "is NTP" for focus and placeholder (`chrome_location_bar_model_delegate.cc:127`) | #96 says the URL is hidden (rewrite) *(NT7)* |
| N-h | `kColorNewTabPageBackground` | NTP colour | Zen's empty tab shows the window theme or glass | **Override** with a live colour (N1) | #96 bakes it in at request time |
| N-i | Split-view "new tab" picker (`kChromeUISplitViewNewTabPageURL`) | tab-search WebUI | Koan has its own split view | **Lock** (not reached by Koan's split, verify) | — |
| N-j | DICE FRE (`startup_browser_creator.cc:768-779`, `first_run_service.cc`) | intro / sign-in window before the browser | **Yes** (Zen: welcome only) | **Lock**: mark `kFirstRunFinished` before startup, from Koan's first-run hook (local state), or override `ShouldOpenFirstRun`. The welcome replaces it | **none**: it shows on every real first run *(NT1)* |
| N-k | Branding-only first-run dialog, What's New | — | — | nothing to do (off in Chromium builds) | — |
| N-l | Search choice dialog (EEA) | modal in the browser | Zen: the welcome's search page | **Redirect**: the welcome records a valid choice (O10) | #158 |
| N-m | Session-crashed bubble (`infobar_utils.cc:216-224`) | "Restore pages?" | **Yes** (B16; Zen restores silently) | **Override**: restore automatically (Zen) when `kRestoreOnStartup=LAST`. Suppress the bubble (hook in `ShowSessionCrashedBubbleIfNotOffTheRecordProfile` or the call site) | none (B16 open) |
| N-n | Session-restore infobar (`kSessionRestoreInfobar`) | "Koan will restore your tabs… turn off" | Yes | **Lock**: `DisableFeature(kSessionRestoreInfobar)` (N6) | none |
| N-o | Default browser prompt + pin + PDF infobars (`infobar_utils.cc:323-361`) | nag from the second launch | Yes (Zen: the welcome's page) | **Lock**: disable `kPdfInfoBar` and `kSeparateDefaultAndPinPrompt`, and skip `ShowDefaultBrowserPrompt` when the welcome has asked (`koan.welcome_screen.seen`). Keep the settings button (N7) | none |
| N-p | OSCrypt availability infobar (`infobar_utils.cc:118-158, 288`) | relaunch to fix keychain | Zen never uses the keychain (NSS) | **Root-cause** (N8); do not just hide it | #141 open |
| N-q | Profile picker on startup | | Zen: profiles off | **Reuse** (only with several profiles); low priority | — |
| N-r | `kRestoreOnStartup` default LAST (Koan patch) | | matches `browser.startup.page=3` | **Reuse**. It is what triggers N-n | done |

### 3.3 Changes

| ID | Change | Where | Patch? |
|---|---|---|---|
| N1 | **#96: a live NTP colour.** Load `chrome://theme/colors.css?sets=chrome` plus upstream's `colors_css_updater.js` and use `var(--color-new-tab-page-background)`. The window's provider (Space override included) feeds it, and it updates on theme change without a reload. Or make the document transparent and show the gradient (later, GL-r). Localise the `<title>` | #96 `koan_new_tab_ui.cc` | no |
| N2 | **Lock the DICE FRE:** set `prefs::kFirstRunFinished` in local state before `StartupBrowserCreator` runs (a Koan early-startup hook that already exists for prefs?), or a 1-line hook in `ShouldOpenFirstRun`. Koan's welcome stays gated on `IsChromeFirstRun()` | `src/` or `first_run_service.cc` hunk | maybe 1 |
| N3 | **#96 settings choice "Chromium's new tab page":** either keep it as a power-user switch (and accept N-b's remote NTP), or drop it (Zen has no such option; `zen.urlbar.replace-newtab` is hidden). Recommended: drop it, keep `chrome://flags/#koan-new-tab` | #96 | — |
| N4 | If N3 keeps the switch: document that a non-Google DSE opens the engine's own remote NTP | docs | — |
| N5 | Incognito NTP: after NT9, either keep Chromium's or serve Koan's page for OTR | `search.cc.patch` (#96) | hunk |
| N6 | `DisableFeature(features::kSessionRestoreInfobar)` | `koan_feature_overrides.cc` | no |
| N7 | Default browser: disable `kPdfInfoBar` and `kSeparateDefaultAndPinPrompt`. Skip `ShowDefaultBrowserPrompt` after the welcome has run (or set `kDefaultBrowserLastDeclined*` when the welcome's "NO" is picked) | feature overrides + `infobar_utils.cc.patch` (existing) | existing patch |
| N8 | **#141 root cause.** Reproduce on the Mac without `--use-mock-keychain`, with a fresh `--user-data-dir`, on a build signed (a) ad hoc and (b) with a stable identity. Expected: each ad-hoc build has a new code signature, the "Koan Safe Storage" ACL no longer matches, the keychain prompt appears or is denied, and an empty password follows. Fix: sign release and preview builds with one stable identity (even a self-signed certificate in CI). Only then consider `DisableFeature(kOSCryptAsyncAvailabilityInfoBar)` as polish, because hiding it with encryption really unavailable means cookies and passwords silently fail to decrypt | CI signing (#70), docs | no |
| N9 | Session-crashed bubble → silent restore when `kRestoreOnStartup=LAST` (B16) | `infobar_utils.cc.patch` (existing) + `src/` | existing patch |
| N10 | The welcome's search page records a completed choice (O10), and its default-browser page records a decline (N7) | `koan_welcome.cc` | no |

### 3.4 Risks in the open PR

**#96 (NTP):**
- **Looks wrong after #62:** the colour is fixed when the document is served (`BuildStyle`). Switching Space or editing a theme leaves the previous colour until a reload, and in Zen-look windows the colour is GM3's `kColorNewTabPageBackground` under the Space seed, not Zen's gradient or glass (N1).
- **Looks like Chromium:**
  - the incognito NTP (N-f);
  - "Chromium's new tab page" with Bing gives a remote Bing page (N-b, N3);
  - `chrome://new-tab-page` is still served for `IsNTPURL` users and by typing; check that nothing in Koan (for example the empty-tab placeholder) keys on `IsNTPURL`, which doesn't know `koan-newtab` (NT7).
- **Crash:** none seen. The page is static, with no Mojo. `search.cc` runs on hot paths (`IsNTPOrRelatedURL` for every navigation's process assignment): `koan::GetNewTabPageURL` must stay a cheap pref read (it does).
- **Not built or run** (draft). It also bundles omnibox changes (`zero_suggest_provider.cc.patch`, `chrome_omnibox_client.cc.patch`) that overlap #158; split before merge.

### 3.5 Mac tests (NTP / first run)

Run the first-run tests **without** `--no-first-run`, `--no-default-browser-check` or `--use-mock-keychain`, with a fresh `--user-data-dir`.
- **NT1.** First launch: record every window and prompt in order. Expected (Zen): one browser window at 1200×720 with the welcome, no profile-picker window, no infobar, no keychain dialog (N2, N8).
- **NT2.** The same, built signed ad hoc vs with a stable identity: does the keychain prompt or #141's infobar appear? Then relaunch: does it appear again? Run with `KOAN_REAL_KEYCHAIN=1` too (N8).
- **NT3.** Finish the welcome (any choices), quit, relaunch twice: no session-restore infobar, no default-browser/pin/PDF infobar, no welcome (N6, N7).
- **NT4.** `--search-engine-choice-country=DE`, fresh profile: the welcome's search page is the only engine choice. No modal after the welcome, and none on the next launch (O10, N10).
- **NT5.** Kill -9 Koan with 3 Spaces and 6 tabs, relaunch: silent restore, one window, 3 Spaces, no "Restore pages?" bubble (N9, B15/B16).
- **NT6.** Pick "NO, still exploring" on the default-browser page. Relaunch 3 times: no default-browser infobar (N7).
- **NT7.** #96: Cmd+N, the tab menu's "New tab below", `chrome.tabs.create({})`, and closing the last tab. Each shows Koan's empty page; the URL bar shows no address, the placeholder is Zen's, and nothing Google loads (check the network log).
- **NT8.** #96 + #62: open an empty tab, switch to a differently coloured Space and back, edit the theme with the picker open. The empty tab's colour follows live, with no reload (N1).
- **NT9.** Zen reference vs Koan: a private window's new tab (N-f).
- **NT10.** Policy `NewTabPageLocation=https://example.com` and an extension with `chrome_url_overrides.newtab`: both win over Koan's page; Cmd+T is still the floating bar.
- **NT11.** Settings "New tab page" = Chromium's with Bing as DSE: document what loads (N3/N4).

---

## 4. What Zen suppresses in Firefox → Chromium equivalent → Koan status

From Zen 1.24b source (patches, `prefs/*.yaml`, CSS, JS overrides), with effective values from FJS. **done** means Koan already matches; **missing** means nothing in Koan yet; **PR #n** means an open PR covers it; **n/a** means there is no Chromium counterpart or it is already off in a Chromium build.

### 4.1 Themes and colours

| Firefox feature | How Zen suppresses it | Chromium equivalent | Koan status |
|---|---|---|---|
| Lightweight (add-on) themes | `zen.theme.disable-lightweight=true` (FJS:1802); `LightweightThemeConsumer-sys-mjs.patch:19` forces the default theme | ThemeService extension themes (`SetTheme`, `GetCustomTheme`, `ThemedBackground` images) | **missing** (T5); glass and #62 step *aside* instead |
| about:addons Themes category, customise "Manage themes", settings Browser theme group, web-appearance footer | CSS `display:none` (`aboutaddons-css.patch:9-12`, `zen-single-components.css:27-31`, `zen-preferences.css:413-426`) | chrome://settings/appearance Theme row (Web Store / Customize Chrome), chrome://extensions themes | **missing** (T4, T5) |
| Dark private-window theme | moot (LWT off); `[privatebrowsingmode]` background | incognito key (dark, grayscale) | **done**: Chromium's incognito look kept (T-g) |
| Toolbar/content colour scheme (`browser.theme.toolbar-theme`) | replaced by `zen.view.window.scheme` 0/1/2 (FJS:1864) via `@media -moz-pref` (`zen-theme.css:329-341`) | `kBrowserColorScheme` | **PR #62** (reuse) |
| Website colour scheme follows the window | `<browser>` color-scheme inherit (`zen-browser-container.css:29-33`); migration `ZenUIMigration.sys.mjs:111-115` | `GetPreferredColorScheme` from the window key | **PR #62** (hook); scrollbars **missing** (T6) |
| View > Appearance menu, URL-bar dark/light/auto commands | `ZenMenubar.mjs:5-45`; `ZenUBGlobalActions.sys.mjs:310-326` | Mac main menu; command palette | **missing** (T9); palette in **PR #62** |
| LWT frame colours → per-Space gradient | `ZenGradientGenerator.mjs` (`dark-mode-bias=0.3`) | widget colour override + Koan painter | **PR #62** (with T1/T2) |
| Accent colour | `zen.theme.accent-color="AccentColor"` → `--zen-primary-color` (`zenThemeModifier.js:204-211`) | macOS `controlAccentColor` | **done** (`koan_system_accent_mac.mm`) |
| Native widget accent | `widget.non-native-theme.use-theme-accent=true` | `kColorAccent` from the system | done (Chromium follows the system) |
| Scrollbar style | `widget.non-native-theme.scrollbar.style=2` | overlay scrollbars | **done** (`native_theme_features.cc.patch`) |
| Firefox View / AI toggle / private label / FxA CTAs | CSS `display:none` (`zen-single-components.css:33-53`) | Glic button, incognito badge, sign-in promos | partly (audit 2 O5: Glic); avatar/sign-in **missing** in `kNone` mode |
| Content colour remap (Zen Boosts) | `Theme-cpp.patch`, `PresShell-cpp.patch` | — | n/a |
| Mozilla promos in settings | CSS + `browser.preferences.moreFromMozilla=false` locked | Chrome promos in settings | out of scope |

### 4.2 Glass, vibrancy and window

| Firefox feature | How Zen suppresses it | Chromium equivalent | Koan status |
|---|---|---|---|
| Opaque window / sidebar-only vibrancy | `ZenWindowMaterialView : NSVisualEffectView`, behind-window, `appearance=nil` (`nsCocoaWindow-mm.patch:21-127`) | `GlassFrameService` + `BrowserNativeWidgetMac` glass (`NSGlassEffectView`) | **PR #103** |
| Material choice | `zen.widget.macos.window-material=1` (HUD; 2-7 others) | `NSVisualEffectMaterial` | **PR #103** (HUD fixed; no pref) |
| Vibrancy master switch | `zen.widget.macos.window-vibrancy=true`, live | `glass_frame.enabled` local state | **PR #103** (reuse) |
| Inactive greying | `zen.view.grey-out-inactive-windows=true` (`VibrancyManager-mm.patch:18`; `zen-browser-ui.css:130-140`) | effect-view `FollowsWindowActiveState`; Chromium tint inactive | **PR #103** + **PR #62** (InactiveCaption paint) |
| Legacy sidebar blend pref migration | `ZenUIMigration` v7 | — | n/a |
| Firefox's own vertical-tab sidebar | `sidebar.revamp=false`, `sidebar.verticalTabs=false` locked | Chromium vertical tab strip | done via the vertical-tabs audit (A-layer, **PR #155**) |
| Transparent chrome document | `zen-theme.css:309-320`, `zen-browser-ui.css:7-15` | frame/toolbar/top container alpha 0 | **PR #103** (`OnGlassLayout`) |
| Acrylic URL bar / sidebar | `zen.theme.acrylic-urlbar/sidebar=false` in release; off under software WR | layer background blur | **PR #105** (URL bar); sidebar **missing** (deliberately: off in Zen release) |
| Native popovers for panels | `widget.macos.native-popovers=true`; `native_macos_popovers_fix.patch` | NSPopover; Views bubbles | **PR #62** (picker gets `NSVisualEffectMaterialPopover`); other bubbles stay Views (T2 decides their colour) |
| Traffic lights mono / hidden | `zen.widget.mac.mono-window-controls=true`, `zen.view.hide-window-controls=true` | caption buttons, `koan_traffic_lights*` | done / PR #144 |
| Three-dot menu, menubar toolbar | `zen.view.mac.show-three-dot-menu=false`; `#toolbar-menubar{display:none}` | app menu button | done (Koan hides the toolbar); `kNone` mode leaks (audit 1 A10) |
| Fullscreen toolbox shift under the menu bar | `browser-fullScreenAndPointerLock-js.patch:9-11` | immersive reveal | done (#157 F1-F3) |
| Borderless fullscreen | `zen.view.borderless-fullscreen=true` | card insets in fullscreen | done (#128/#157) |
| Toolbox hidden in DOM fullscreen | `zen-browser-ui.css:23-25` | content fullscreen | done (#157 F1/F2) |
| Swipe-gesture override | `zen.widget.macos.override-system-swipe-gestures=true` | overscroll history navigation | done (`koan_swipe_tracker`) |
| Liquid-glass app icon | `no_liquid_glass_icon.patch` (drops `CFBundleIconName`) | Chromium's Info.plist / Assets.car | **missing**: check that Koan's app icon is not given Tahoe's glass treatment *(live check)* |
| Pre-XUL skeleton UI | `browser.startup.preXulSkeletonUI=false` | — | n/a |

### 4.3 NTP, first run and startup

| Firefox feature | How Zen suppresses it | Chromium equivalent | Koan status |
|---|---|---|---|
| about:welcome / pre-onboarding / UITour | `browser.aboutwelcome.enabled=false`, `browser.preonboarding.enabled=false`, `browser.uitour.enabled=false` | DICE FRE (intro), IPH tours | welcome **done** (#66); DICE FRE **missing** (N2); IPH partly (feature overrides) |
| Zen welcome | `zen.welcome-screen.seen` (`ZenStartup.mjs:172-204`) | `koan.welcome_screen.seen` + `IsChromeFirstRun()` | **done** (#66); never tested on a real first run (NT1) |
| First-run import | the welcome's import page → `MigrationUtils.showMigrationWizard` (`ZenWelcome.mjs:661-708`); `content-modal.import-all=false` | `chrome://settings/importData`, importer | done in the welcome (choice); import itself untested |
| Default-browser prompt | the welcome's page (`ZenWelcome.mjs:646-655`); `skipDefaultBrowserCheckOnFirstRun=true`; `checkDefaultBrowser` left true (gap) | default-browser + pin + PDF infobars (`infobar_utils.cc:323-361`) | welcome page **done**; infobars **missing** (N7) |
| Search engine choice | the welcome's page (`SearchService.setDefault`) | EEA choice dialog | welcome **done**; double prompt **PR #158** (O10) |
| New tab page | `zen.urlbar.replace-newtab=true` → `handleNewTab`; Home category hidden | `chrome://newtab` → WebUI NTP | **PR #96** (N1-N5); Cmd+T done (#36) |
| NTP content (top sites, stories, weather, sponsored) | `browser.newtabpage.activity-stream.*=false` | NTP modules, shortcuts, Discover, footer | moot once **PR #96** lands; until then B6 |
| Last tab closed → empty tab | `Tabbrowser-sys-mjs.patch:845-852` → `selectEmptyTab()` | close-last-tab → NTP | done (`close-last-tab-and-empty-tab.md`, #96 empty-tab work) |
| Startup page = resume | `browser.startup.page=3` | `kRestoreOnStartup=LAST` | **done** (`session_startup_pref.cc.patch`) |
| Crash / restore decision always auto | `SessionStartup-sys-mjs.patch:50,57`, `SessionStore-sys-mjs.patch:61` | session-crashed bubble | **missing** (N9, B16) |
| Restore-on-startup nag | n/a in Firefox | session-restore infobar | **missing** (N6) |
| Recently closed windows menus | hidden with window sync | History menu "Recently closed" | n/a until window sync (#75) |
| What's New / upgrade dialog | Zen update toast; `upgradeDialog.enabled=false` | What's New (branding-only) | n/a (off in Chromium builds) |
| Profile chooser | `browser.profiles.enabled=false` | profile picker at startup / avatar menu | low (N-q); avatar menu leaks in `kNone` mode |
| Bookmarks toolbar on NTP | `browser.toolbars.bookmarks.visibility="never"` | bookmark bar shown on NTP | **check**: Chromium shows the bar on the NTP when "show on NTP only" applies *(live check NT7)* |
| Search one-offs, suggestions | `zen.urlbar.hide-one-offs=true`, `browser.search.suggest.enabled=false` | omnibox suggest prefs | **PR #158** (O7) |
| Keychain | not used (NSS key db) | OSCrypt keychain + availability infobar | **missing** (N8, #141) |
| Mozilla / Firefox account promos | locked prefs, CSS | Chrome sign-in promos, IPH | partly (feature overrides) |

**Correction to audit 2:** in `tab-groups-omnibox-fullscreen.md` §1.2.1, "Firefox themes keep working" (Zen column) is wrong for 1.24b. Zen disables lightweight themes (`zen.theme.disable-lightweight=true`).

---

## Appendix: key references (pristine 158.0.8127.2)

- **Themes:**
  - `chrome/browser/themes/theme_service.cc:310-333` (prefs), `:383-409` (observers), `:683-703` (scheme), `:705-786` (key), `:791-845` (user colour);
  - `chrome/common/pref_names.h:682-703`;
  - `chrome/browser/themes/theme_syncable_service.cc:228-546`.
- **Window key:** `chrome/browser/ui/views/frame/browser_widget.cc:263-305, 326-360, 474-526`; `chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.cc:625-647`.
- **Colours:**
  - `ui/color/color_provider_manager.cc:101-126`;
  - `ui/color/mac/native_color_mixers_mac.mm:62`;
  - `chrome/browser/ui/webui/theme_source.cc:136, 272-…`;
  - `chrome/browser/ui/webui/theme_colors_source_manager.cc:60-110`;
  - `chrome/browser/ui/color/chrome_color_id.h:1244`.
- **Content scheme:** `chrome/browser/chrome_content_browser_client.cc:4094-4145` (scheme), `:945-970` (NTP policy), `:5231-5258` (URL handlers).
- **Customize Chrome:**
  - `chrome/browser/ui/browser_command_controller.cc:1592-1602, 1951-1954`;
  - `chrome/browser/ui/toolbar/app_menu_model.cc:1099-1107`;
  - `chrome/browser/ui/browser_actions.cc:494, 2268, 2388`;
  - `chrome/browser/ui/webui/side_panel/customize_chrome/customize_chrome_page_handler.cc:226-240`;
  - `chrome/browser/ui/webui/cr_components/theme_color_picker/theme_color_picker_handler.cc:96-111`;
  - `chrome/browser/resources/settings/appearance_page/appearance_page.html.ts:14-80`.
- **macOS appearance:**
  - `ui/views/widget/native_widget_mac.mm:607-621`;
  - `chrome/browser/ui/views/frame/browser_view.cc:6113`;
  - `components/remote_cocoa/app_shim/native_widget_ns_window_bridge.mm:114-130, 298, 1387-1394`;
  - `ui/native_theme/os_settings_provider_mac.mm:31, 131`.
- **Glass:**
  - `ui/base/ui_base_features.cc:498-533`;
  - `chrome/browser/ui/views/frame/glass_frame_service.{h:55, cc:102-124, 155-201, 209-290, 300-315}`;
  - `chrome/browser/ui/views/frame/browser_native_widget_mac.mm:114-178, 297-345, 995-1222`;
  - `chrome/browser/ui/views/frame/browser_frame_view_mac.mm:90-101, 337-342, 615-620`;
  - `content/app_shim_remote_cocoa/web_contents_occlusion_checker_mac.mm:41-88, 503-527`;
  - `chrome/browser/ui/views/frame/custom_corners_background.cc:154-180, 285-331`;
  - `ui/views/controls/label.cc:973-1008`;
  - `chrome/browser/ui/views/frame/multi_contents_background_view.cc:12-60`;
  - `chrome/browser/ui/views/frame/contents_web_view.cc:210-232`;
  - `chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc:1694-1700, 1750-1765, 1840`.
- **NTP:** `chrome/browser/search/search.cc:166-231, 263-357, 410-430`; `chrome/common/webui_url_constants.h:228-232, 323-324`; `chrome/browser/ui/webui/new_tab_page/new_tab_page_ui.cc:158, 1172`.
- **First run:**
  - `chrome/browser/first_run/first_run.cc:352-367`;
  - `chrome/browser/first_run/first_run_internal_posix.cc:47-66`;
  - `chrome/browser/ui/startup/first_run_service.cc:35-60, 91-130, 298-301`;
  - `chrome/browser/ui/startup/startup_browser_creator.cc:768-779`;
  - `chrome/browser/ui/profiles/profile_picker.cc:242-264`;
  - `components/signin/features.gni:13`.
- **Search choice:** `components/search_engines/search_engine_choice/search_engine_choice_service.cc:576-640, 1021`; `components/search_engines/search_engine_choice/buildflags.gni:11`.
- **Startup UI:**
  - `chrome/browser/ui/startup/infobar_utils.cc:114-364`;
  - `chrome/browser/ui/ui_features.cc:191-227, 392, 593-597`;
  - `chrome/browser/ui/views/session_restore_infobar/session_restore_infobar_controller.cc:34-90`;
  - `chrome/browser/ui/views/session_restore_infobar/session_restore_infobar_model.cc` (`ShouldShowOnStartup`, `IsDefaultSessionRestorePref`);
  - `components/os_crypt/async/browser/keychain_key_provider.mm:40-72`;
  - `chrome/browser/ui/webui/whats_new/whats_new_util.cc:35-43`.
