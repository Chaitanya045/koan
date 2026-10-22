# Round 2 review of PR #62 (Koan theme picker, #41)

Head `374ec42` on `feat/zen-theme-picker`: one commit on top of develop `5ef046e` (50 files). Read-only review of
`git show HEAD:<file>` copies, against Chromium 158 (`~/work/koan/chromium/src`), Zen 1.23.3b (`.cache/zen-src`) and the
round-1 review. Nothing was built or run (no Mac, no build). `node --check` passes on `scripts/e2e/theme.mjs` and
`settings.mjs`; `git apply --check` succeeds for all four patches on pristine 158 blobs (pre-image hashes unchanged).
B2 is deferred until #57 and is not counted. (V) = verified in code.

## Summary

- Blockers: 1 (new). Should-fix: 1 (new). Nits: 7.
- Round 1: B1 FIXED, B3 FIXED. S1 FIXED, S2 FIXED, S3 PARTIAL (as the author says, token delivery is left to #48),
  S4 FIXED, S5 FIXED (documented), S6 FIXED, S7 FIXED (not measured), S8 FIXED, S9 FIXED, S10 FIXED (unverified on Mac).
- Reply draft `.cache/theme-picker/pr62-reply.md` ends with `**Tests:** RESULTS`, a placeholder. Fill it in before posting.

## Round-1 items

| Item | Status | Evidence |
|---|---|---|
| B1 symlinks | FIXED | `git ls-tree -r HEAD \| grep '^120000'` is empty; the branch is one commit; `.gitignore` has `/chromium` and `/depot_tools` (no trailing slash). |
| B3 keyboard / AT | FIXED | Field: `koan_theme_picker.cc:428-432` (focusable, ring, `kGroup`, name "Theme colors"), keys `:618-679` (Enter/Space add in the middle, arrows 4px / Shift 20px, Delete/Backspace remove, window follows on key release), description `:493-502`. Slider: `:1027-1036` (`kSlider`, min/max, increment/decrement), keys `:1148-1164`, `HandleAccessibleAction` `:1166`, value `:1043-1045`. Dial: `:1249-1259`, keys `:1369-1384`, actions `:1386`, value `:1262-1264`. Swatches: one tab stop `:980-988`, arrows `:812-819`, `:992-999`. Lost capture N3 fixed at `:608`, `:1145`, `:1365`. |
| S1 system accent | FIXED | `koan_system_accent.cc:10` (`OsSettingsProvider::Get().AccentColor()`), `koan_system_accent_mac.mm:11-19` (`NSColor.controlAccentColor` to device RGB, `skia::NSDeviceColorToSkColor` exists), read per `Update()` at `koan_theme_controller.cc:218`. BUILD.gn picks the `.mm` only on Mac (`views/BUILD.gn:216-221`). See nit N-3 for accent changes on Mac. Needs a Mac build. |
| S2 Chrome theme | FIXED | `koan_theme_controller.cc:99-104` (`IsEnabled()` is false under `UsingExtensionTheme()`); `Update()` clears both overrides when disabled `:244-256`; `OnThemeChanged` observer `:585`; REBASE row added. |
| S3 #48 seam | PARTIAL | Done: `family`/`version` in the dict (`koan_space_theme.cc:228-229`), `GetTokens(env, stored)` dispatch and unknown family gives default (`:585-595`), `WindowTokens` with `flat_map<Token, SkColor>` (`koan_space_theme.h:206-244`), window base/default/tint colours are colour IDs (`koan_color_mixer.cc:108-117`, resolved at `koan_theme_controller.cc:224-234`). Left for #48 (declared): only `kPrimary` is consumed (`koan_theme_controller.cc:295`); there is no path for the other tokens into the mixer, and `kColorKoanTheme*` are fixed per mode, so a family cannot override them. Acceptable to merge; #48 must add the delivery. |
| S4 Mac metrics | FIXED | `koan_theme_picker.cc:103-109` (78px dial, 26px gap on Mac; Zen CSS 6rem / 2rem with a 13px rem); ZEN_PARITY says Linux-only comparison (`:32`). |
| S5 translucent maths | FIXED | Not implemented by decision; ZEN_PARITY row 36 now reads "Same on Linux, different on macOS" and explains why. Maintainer sign-off still needed on the product platform. |
| S6 e2e | FIXED | `theme.mjs`: light preset under Dark appearance, page stays dark (discriminating); dot drag against a Zen-measured colour (`ZEN_DRAGGED_DOT`); settings switch toggles a chrome:// page's scheme; keyboard checks; restart check (`:441-452`). `settings.mjs` tint check is still only the pref, but `theme.mjs` now covers the effect. No C++ unit tests: the repo has none anywhere and CI does not build gtest targets, so that is a reasonable call. |
| S7 repaint cost | FIXED (not measured) | `ViewTracker` list instead of a tree walk (`koan_theme_controller.cc:336-361`); palettes pushed only in `OnThemeChanged` (`koan_theme_picker.cc:1681-1693`); `OnEnvironmentChanged` returns early (`:1795`); custom list rebuilt only on change (`:1493`). `scripts/perf` not run. |
| S8 REBASE.md | FIXED | Rows added for the two patches with their BUILD.gn deps, the colour-key overrides, scheme/accent inputs, the Space-switch observers (`docs/REBASE.md` +5 lines). Will need an update when B2 lands (the "keys by the saved GUID" wording). |
| S9 WebUI | FIXED | Hook passes `security_principal.IsWebUI()` (`patches/.../chrome_content_browser_client.cc.patch`); `koan_theme_controller.cc:652-654` returns nullopt for WebUI. |
| S10 overlay widgets | FIXED (unverified on Mac) | Gradient is sized to the browser widget's root and offset through screen coordinates (`koan_theme_controller.cc:362-371`). Needs a Mac fullscreen / compact-overlay check. |

Round-1 nits: N1 fixed (`:310-328`), N2 fixed (`:277`, picker `:442`, `:971`), N3 fixed, N7 fixed, N10 fixed
(`:412-415`), N11 fixed (`:1493`); N4 moot after B2; N5, N8 documented; N6, N9 open as stated; N12 declined.

## Blocker

### B4. Null dereference in the `GetPreferredColorScheme` hook when a tab is detached
- `src/browser/ui/views/koan_theme_controller.cc:656-660` (`PrefersDarkWebColorScheme`): 
  `BrowserView::GetBrowserViewForBrowser(tab->GetBrowserWindowInterface())`. (V)
- `TabInterface::GetBrowserWindowInterface()` is documented to return nullptr for a tab that is closing, reparenting, or
  not yet or no longer in a window (`components/tabs/public/tab_interface.h:298-304`; `TabModel::GetBrowserWindowInterface`
  returns nullptr when it has no owning model, `tab_model.cc:308-313`). `BrowserView::GetBrowserViewForBrowser` calls
  `browser->GetWindow()` without a null check (`browser_view.cc:1218-1227`).
- Failure scenario: web preferences are recomputed for a WebContents that is detached (tab dragged out or moved
  between windows) while Space themes are on: the browser process crashes. The old `TabInterface::MaybeGetFromContents`
  null check does not cover this (the tab object exists, its window does not). `GetPreferredColorScheme` runs for every
  regular page whatever the theme, so the exposure is every tab move.
- Fix: `const BrowserWindowInterface* window = tab ? tab->GetBrowserWindowInterface() : nullptr;` and return
  nullopt when it is null; only then call `GetBrowserViewForBrowser(window)`. Optionally add the null case to the e2e
  (drag a tab to a new window with a Space theme set).

## Should-fix

### S11. Zen-derived code is under the MIT header only (maintainer decision)
- `src/browser/ui/views/koan_space_theme.{h,cc}` (header `koan_space_theme.cc:1-3`): the file is a line-by-line port of
  `ZenGradientGenerator.mjs` (its own comments say it "mirrors Zen's formulas and constants", names each Zen function,
  and carries the 41-preset table, the harmony tables and the sine-wave path string). Zen is MPL-2.0, which is file-level
  copyleft. `THIRD_PARTY_NOTICES.md` lists only `grain-bg.png` and the icons as MPL files.
- Fix: decide, then either give those files MPL-2.0 headers (as the `.icon` files have) and list them in
  THIRD_PARTY_NOTICES, or document why a re-implementation of the maths and tables is not a derivative. Cheap to fix now,
  awkward after release.

## Nits

- N-1. `koan_theme_controller.cc:636,646` (`PaintWindowBackground` / `PaintsWindowBackground`) call `IsEnabled()` on each
  paint of each view (and twice per view: `GetBackgroundColor` and `PaintBackground`). `IsEnabled()` does a pref read
  and a `ThemeServiceFactory` lookup. During a 250 ms cross-fade that is several views at frame rate. Use `current_` (set
  exactly when enabled and updated by the observers) or cache the bool in `Update()`.
- N-2. `koan_theme_controller.cc:494`: right-clicking a Space while Space themes are off (setting off, or a Web Store theme
  installed) returns without a menu, so the Appearance submenu (website scheme, independent of Space themes) is gone and
  the user gets no feedback. Show the menu without "Edit Theme…", or disable that item.
- N-3. Mac accent changes are not noticed. Chromium notifies `NativeThemeObserver`s for `NSSystemColorsDidChange` only
  with `kThemeChangeOptimization`, which is disabled by default (`os_settings_provider_mac.mm:~99-106`,
  `ui_base_features.cc:518`). So the Colorful tint and custom primary colour keep the old accent until the next
  `Update()` (Space switch, pref change, restart). Observe `NSSystemColorsDidChangeNotification` in
  `koan_system_accent_mac.mm`, or accept and document.
- N-4. Accessibility leftovers: the selected scheme button is only painted (`koan_theme_picker.cc:260-265`), not exposed
  (checked/pressed state or radio role); the opacity slider is a tab stop while disabled with no colours
  (`:1042` sets the AX enabled flag only, keys then return false, `:1222`); the colour field has role `kGroup` although
  it takes arrow keys, so VoiceOver does not announce it as adjustable. Swatches and icon buttons use the default
  rectangular focus ring (no highlight path generator) around circular / rounded shapes (`:787`, `:234`).
- N-5. `koan_theme_controller.cc:358-360`: `const_cast<views::View*>` to construct a `ViewTracker` inside a const method;
  harmless, but a `ViewObserver` or a tracker taking `const View*` is cleaner. `painters_` also only drops dead entries
  in `InvalidateBackground()`, so it is bounded by the number of live painting views.
- N-6. `SpaceTheme::FromStored` (`koan_space_theme.cc:585`) ignores `version`; a dict written by a newer Koan with the same
  family is read as version 1. Fine for now; reject `version > kThemeVersion` when #48 bumps it.
- N-7. `docs/USAGE.md`, `docs/ZEN_PARITY.md:80` and `docs/REBASE.md` ("keys by the saved GUID") must change with B2. The
  restart check in `theme.mjs` (`:441-452`) depends on the saved-GUID key.

## Fresh review, checked and fine

- Lifetimes: `KoanThemePicker` clears `on_closed_` before deleting the widget (`:1766-1771`), so `ShowThemePicker`'s
  save-and-delete path (`koan_theme_controller.cc:412-415`) cannot call back. `OnPickerClosed` moves the picker to
  `closed_picker_` and deletes it from a weak-bound posted task; `OnBrowserViewDestroying` resets picker, menu runner,
  animation, tab-strip observer, pref registrar, sync/theme/native-theme observations (`:134-147`). `ViewTracker` handles
  deleted painters. No raw `Browser*` is kept (only `raw_ptr<BrowserView>`). `StartSwitchAnimation` moves `fading_` out
  before `Stop()`, which calls `AnimationEnded` and clears it.
- No Aura / X11 / GTK code outside guards; no Ctrl or Cmd handling in the new code (the keys are plain keys; right-click
  removal of a dot also has a Delete-key path, and Ctrl+click is a right-click on Mac). The Mac-only code is the dial
  metrics under `BUILDFLAG(IS_MAC)` and the accent `.mm`. Still unbuilt on Mac: `koan_system_accent_mac.mm`,
  `views::PlatformStyle::kDefaultFocusBehavior` (accessible-only on Mac, so keyboard access needs Full Keyboard Access
  or VoiceOver, as for the other buttons).
- Patches: 4 files, hooks are a call each (the `chrome_content_browser_client.cc` hook is a 7-line statement, declined
  in N12, acceptable), BUILD.gn deps added only where needed. `src/browser/ui/views/BUILD.gn` splits `:space_theme`
  (no views dependency) from `:impl`.
- Stored data: `FromDict` clamps every number and skips malformed entries (`koan_space_theme.cc:236-298`), so a corrupt or
  hand-edited pref cannot crash or make NaNs.
- Easy to move for B2: only `KoanSpaceThemeStore` (`koan_space_theme_store.{h,cc}`, 68 lines) knows the key; the controller
  uses `GetStoredTheme` / `SetSpaceTheme` only. On #57 drop `SpaceKey`'s saved-group lookup and the `session_themes_`
  map, remove `sync_observation_`, `OnInitialized`, `OnTabGroupLocalIdChanged` from the controller, and delete
  `ShowSpaceMenu` plus the `ContextMenuController` in `koan_workspaces.{cc,h}` in favour of #57's menu calling
  `ShowThemePicker()`. The `koan_workspaces.*` conflict with #57 remains the bulk of the merge work.

## Merge recommendation

Fix B4 (a null check), decide S11, rebase onto #57 and do B2 (store key, menu, drop the `koan_workspaces.*` changes, re-run
the restart e2e), then merge. Everything else in round 1 is addressed or consciously deferred (S3's token delivery to #48,
S5 on Mac). A Mac run (build, accent `.mm`, fullscreen / compact overlay gradient, dial sizes) is still owed and should
be noted in the PR rather than gate it.
