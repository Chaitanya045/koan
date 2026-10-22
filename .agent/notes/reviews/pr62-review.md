# Review of PR #62: theme picker like Zen's (per-Space gradient themes, dark styles), fixes #41

Branch `feat/zen-theme-picker`, 18 commits, `git diff origin/develop...HEAD` (50 files). Reviewed read-only against
Chromium 158.0.8111.0 (`~/work/koan/chromium/src`), Zen 1.23.3b (`~/work/koan/.cache/zen-src`) and the open #57 branch
(`origin/feat/zen-spaces`). Nothing was built or run. `clang-format` (CI's pinned 22.1.8) and `node --check` pass on
the changed files. Each finding says whether it was verified by reading code (V) or is uncertain (U).

Checked and fine:
- The four patches (`chrome/browser/BUILD.gn`, `chrome_content_browser_client.cc`, `frame/BUILD.gn`,
  `frame/themed_background.cc`) have pre-image blob hashes equal to pristine 158 (`HEAD:` blobs) and
  `git apply --check` succeeds on pristine copies. They are minimal.
- The colour maths matches `ZenGradientGenerator.mjs`: `hslToRgb`, `rgbToHsl`, `hueToRgb`, `getColorFromPosition`
  (padding 30, half-dot 29, `-0.2`), `calculateCompliments` and `getColorHarmonyType` (including the dead
  `singleAnalogous` branch and the `numDots` quirk), `handleColorPositions`, `getGradient` layers, `shouldBeDarkMode`
  (opaque path), `getAccentColorForUI`, `blendColors`, `luminance`, `contrastRatio`, `#interpolateWavePath` (27.3
  reference), the texture 16 steps. The 41 presets, their `parseInt`ed positions and `data-type` match `browser.xhtml`.
  `grain-bg.png` is byte-identical to Zen's (sha256 checked). Strings match the `.ftl` files.
- Idle behaviour (#47): the 250 ms `gfx::LinearAnimation`, the 400 ms dot animation and the 250 ms page animation all
  end by themselves; there is no repeating timer, and `Update()` returns early when the tokens are unchanged.
- Lifetimes: `KoanThemePicker` uses `CreateBubble(delegate*, on_close)` as documented (delegate outlives the widget; the
  widget is deleted from a posted task, not inside the close callback). `OnBrowserViewDestroying` resets everything.

---

## Blockers

### B1. Machine-local absolute symlinks `chromium` and `depot_tools` are committed
- `chromium:1`, `depot_tools:1` (mode 120000, added in 75c81cb "WIP"): targets `/home/chaitanya/work/koan/chromium` and
  `/home/chaitanya/work/koan/depot_tools`. (V)
- `.gitignore` lists `/chromium/` and `/depot_tools/`; a trailing slash matches directories only, so the symlinks
  were not ignored and got added.
- Failure scenario: on any other checkout (CI, another developer, the Mac) the repo root holds dangling symlinks.
  `scripts/bootstrap.sh` / `build.sh` / `apply_patches.sh` then fail or write through the link into the author's
  home directory. They also pollute every worktree and are not mentioned in the PR.
- Fix: `git rm chromium depot_tools` (history can stay if squashed). Change `.gitignore` to `/chromium` and
  `/depot_tools` (no trailing slash) so a worktree symlink can never be added again.

### B2. Per-Space persistence is keyed on saved tab-group GUIDs, which #57 stops creating; the PR also collides with #57 in `koan_workspaces.*`
- `src/browser/ui/views/koan_space_theme_store.cc:27-50` (`SpaceKey`: `TabGroupSyncService::GetGroup(*space)` ->
  `saved_guid()`, else `"session:<token>"` kept only in the per-window `session_themes_`). (V)
- #57's PR text and branch: "Spaces are not Chromium *saved* tab groups ... new groups aren't saved while Spaces are
  on (`koan::SavesTabGroups`)" (`saved_tab_group_model_listener.cc` patch on `origin/feat/zen-spaces`). #57 also
  removes the "all tabs" Space (`nullopt`), keeps name, icon and theme in the group's title and colour via
  `ReadSpace()`/`WriteSpace()`, adds `SpaceInfo.theme` / `GetSpaceTheme()` / `SetSpaceTheme()` / `EditSpaceTheme()`, its
  own Space menu with "Edit Theme…", and still has `UpdateTint()` and `kWorkspaceTint*`.
- Failure scenario: once #57 is in, `GetGroup()` is empty for every Space, so every theme lands in `session_themes_`.
  Themes reset on restart, and they are not shared between windows because the store lives in each
  `KoanThemeController`. The e2e "Space kept its theme" check would still pass because it never restarts.
  Separately, this PR rewrites `koan_workspaces.{cc,h}` (removes `UpdateTint`, adds the context-menu controller and
  `ShowSpaceMenu`) in the same regions #57 rewrites (1466 changed lines), so the second PR to land has a large
  conflict. The PR text says the store "mirrors #57's" API, but nothing wires them.
- Fix: land one of the two first and rebase. The store is the right seam: key by #57's stable Space id (or store the
  `SpaceTheme` dict through #57's `WriteSpace()`), and drop the saved-group lookup and the `"default"` key. Drop this
  PR's `ShowSpaceMenu` in favour of #57's menu item that calls `ShowThemePicker()`. Re-add the persistence e2e
  after a restart (see S6).

### B3. The colour field, opacity slider and grain dial cannot be used from the keyboard or by assistive tech
- `koan_theme_picker.cc:346-` (`KoanThemeGradientField`), `:828-` (`KoanThemeOpacitySlider`), `:974-`
  (`KoanThemeGrainDial`). There is no `OnKeyPressed`, `SetFocusBehavior`, accessible role, value or action anywhere
  in the file (`grep -n "OnKeyPressed\|SetFocusBehavior\|SetRole" koan_theme_picker.cc` is empty). Only
  `views::Button`s (scheme, actions, arrows, swatches) get default focus. (V)
- Failure scenario: a keyboard or VoiceOver user can tab through the 41 swatches and the buttons but cannot place or
  move a dot (the primary feature), change opacity or grain. The empty-field state ("Click to add a color")
  cannot be left except through a preset. Zen's panel is mouse-first too (its range input is keyboard-operable
  natively), but the brief asks for a fully keyboard-operable panel.
- Fix: make the three views focusable with a focus ring. Field: arrow keys move the big dot (Shift = larger step),
  Enter/Space adds the first dot at the centre, Delete removes it. Slider: role `kSlider` with min/max/value and
  Left/Right/Home/End in 0.001 steps (or 0.05). Dial: role `kSlider`, Left/Right step 1/16 and wrap. Each key calls the
  same model call as the mouse path and then `OnModelChanged()`. Set accessible names and values ("Opacity 40%",
  "Grain 25%"). Consider one tab stop for the 41 swatches (arrows inside) so the panel is not 45+ tab stops.

---

## Should fix

### S1. The system accent is never read, so the macOS accent never reaches Zen's maths (default-Space Colorful tint and custom-colour primary)
- `koan_theme_controller.cc:200-203`: `NativeTheme::GetInstanceForNativeUi()->user_color()` is used for
  `Environment::system_accent`. In Chromium 158 only the renderer (`render_thread_impl.cc:1505`) and a test
  call `set_user_color`; nothing in the browser process sets it on the native-UI instance (`grep -rn set_user_color
  chrome ui components content`). (V)
- Failure scenario: `system_accent` is always the hard-coded GNOME blue `{53,132,228}` (`koan_space_theme.h:95`), so
  on a Mac with a green or pink accent, Colorful on a default Space is still blue and a custom primary colour
  yields blue. Zen resolves CSS `AccentColor`.
- Fix: fill it from the platform: `+[NSColor controlAccentColor]` in a small `.mm` (guarded by `BUILDFLAG(IS_MAC)`),
  or the GTK accent on Linux, falling back to the blue. Re-evaluate on `OnNativeThemeUpdated`. Add a
  `Environment` unit test.

### S2. Setting the Space's accent through `Widget::SetUserColorOverride` silently disables the user's Chrome theme in that window
- `koan_theme_controller.cc:258`. Upstream `BrowserWidget::GetThemeProvider()` (`browser_widget.cc:326-336`) returns
  the default theme provider and `GetCustomTheme()` (`:351-358`) returns `nullptr` whenever `user_color_override()`
  has a value (that is the Focus Mode contract). Also `GetColorProviderKey()` forces
  `user_color_source = kAccent`. (V)
- Failure scenario: a user with a Chrome Web Store theme or device theme who gives any Space a colour loses the theme
  (images and colours) in that window and gets it back on a default Space. `docs/ZEN_PARITY.md` says "Chrome themes from
  the Chrome Web Store work". Window colour also differs by the user's chosen colour variant.
- Fix: do not piggy-back on Focus Mode's override. Either plumb the accent into the key some other way (a
  `ColorProviderKey::ThemeInitializerSupplier`/`user_color` set in a Koan-owned key hook), or skip the accent
  override when `ThemeService::UsingExtensionTheme()` and document it. At least add the assumption to `REBASE.md` (S8).

### S3. #48 can't plug in without reworking the model: no family discriminator, `WindowTokens` is too thin, window colours bypass the colour IDs
- `koan_space_theme.h:44-60` (`SpaceTheme`) and `.cc:176-205` (`ToDict`/`FromDict`): the stored dict has no
  `type`/family field (Zen's `getTheme()` has `type: "gradient"`; Koan drops it) and no schema version. A shadcn
  theme stored under `koan.spaces.themes` would be read by `FromDict` as an empty gradient theme (the default), and
  nothing says which family a Space uses.
- `WindowTokens` (`koan_space_theme.h:197-216`) is `{background, optional dark, optional accent}`. The theme reaches
  Chromium/Koan colours only through `Widget::SetColorModeOverride`/`SetUserColorOverride`
  (`koan_theme_controller.cc:249-259`); `koan_color_mixer.cc` only sees `ColorProviderKey` (mode + user colour), not
  the controller. A shadcn family has independent background/card/popover/border/input/primary/muted/ring tokens
  that cannot be expressed as one accent seed.
- The default/Night/Colorful bases and window colours (`kDarkDefault` #131313, `kDarkWindow` #171717, `kLightDefault`,
  `kLightWindow`, `ToolbarBase`, ...) are baked `SkColor`s computed in `GetBackground()` (`koan_space_theme.cc:34-45,
  538-560`) at `Update()` time. None are Koan colour IDs, so they do not follow the pipeline "all UI colours flow
  through `koan_color_id.h`/mixer". `KoanThemeController::Update()` hard-codes `theme::GetZenTokens`.
- Fix before #48: add a `family` (and `version`) to the stored dict and a `ThemeFamily` interface
  (`GetTokens(env, stored)`), with a token set rich enough for the mixer (a `std::map<ColorId, SkColor>` or named token
  struct) delivered to the mixer through the colour key (custom `ThemeInitializerSupplier`) or a per-window provider.
  Add Koan colour IDs for the window background base/fill so `PaintBackground` resolves them from the
  `ColorProvider`. `GetZenTokens` becomes the first implementation.

### S4. macOS panel metrics are the Linux ones; Zen has macOS-specific rules and no macOS check was done
- `koan_theme_picker.cc:93,95` (`kDialSize = 73`, `kControlsGap = 22`). `zen-gradient-generator.css:73-75` sets
  `gap: 2rem` on macOS for the controls row, `:86-88` bigger labels, and `:376-379` makes the texture wrapper `6rem`
  (not `5rem`) on macOS. (V from CSS)
- Failure scenario: on the product platform the dial is about 18% smaller than Zen's and the slider-dial gap
  is about 7px short, so the panel is not "exactly like Zen's". All screenshots and the "Same" rows in `ZEN_PARITY.md` were
  taken on Linux; PR text says nothing ran on a Mac. Apply the Mac sizes behind `BUILDFLAG(IS_MAC)` (as #57 does for
  sidebar metrics) and mark the rows as "Linux only checked" until a Mac comparison exists.

### S5. `zen.theme.dark-mode-bias` and Zen's macOS (translucent) maths are not implemented (documented decision, needs sign-off)
- `koan_space_theme.cc:458-468` (`ShouldBeDarkMode`) implements only the `!canBeTransparent` branch.
  `ZenGradientGenerator.mjs` `canBeTransparent` is true on macOS (`-moz-platform: macos`), where Zen skips the
  base pre-blend, subtracts `darkModeBias` (default 0.3) from the light-text alpha and blends the white overlay
  (`blendWithWhiteOverlay`, 0.6). `ZEN_PARITY.md` explains the choice ("Koan's window is opaque on every platform").
  (V)
- Failure scenario: for mid-luminance colours the light/dark decision, the accent and the tint strength differ from Zen
  on macOS, which is the product, and the rows are labelled "Same". The Linux screenshots cannot show it.
- Fix: either label those rows "Different on macOS" or implement Zen's translucent formulas as an opaque
  approximation on macOS (bias included). Needs a maintainer decision, not necessarily a code change.

### S6. The e2e tests do not prove several claims
`scripts/e2e/theme.mjs`:
- "A themed window keeps websites on the appearance setting" (end of the Space-switch section) is not
  discriminating: the profile is Dark and the themed Space is dark, so `prefers-color-scheme: dark` is true with or
  without the `GetPreferredColorScheme` hook. Test it with a Space whose theme decides Light while the appearance is Dark
  (or the reverse) and check the page's value.
- No restart test: "saved per Space" only switches Spaces in one session (reads the pref back through the store).
  Persistence across restart (and after #57, B2) is untested. The harness supports restarts (`KOAN_CHROME_ARGS`).
- Not covered: dot drag (only the dial is dragged, with a single synthetic `mouseDragged`), the Appearance submenu,
  custom colours, keyboard operation (only Esc), the animation, `kSpaceThemes` content, Chrome-theme interaction (S2).
  Most checks compare strings produced by the same `DescribeBackground()` code; only the preset check is anchored to a
  Zen measurement (the dark-window gradient from Zen 1.23.3b), which is good.
- `scripts/e2e/settings.mjs`: the tint check was reduced to "the pref flips after clicking the checkbox", which is
  tautological. The old test checked the effect. Check that the window background reverts
  (`WindowBackground`/pref-free) when "Show the active Space's theme" is off.
- There are no unit tests for the pure maths (`:space_theme` has no views dependency and is exactly the kind of
  code that is easy to cover: round trips, harmonies, `getColorFromPosition` samples, `ShouldBeDarkMode`).

### S7. Dragging repaints the whole panel and the whole window on every mouse event (U: not measured)
- `koan_theme_picker.cc:1321-1337` (`OnModelChanged` -> `UpdateControls`, `:1339-` -> `pages->SetPalette()` calls
  `SchedulePaint()` on all 41 swatches (3-layer gradients with a clip path each) and on each button) and again from
  `KoanThemeController::Update()` -> `picker_->OnEnvironmentChanged()` -> `UpdateControls()` (`:1437`), so twice per
  event. `koan_theme_controller.cc:270-286` (`InvalidateBackground`) walks the whole window view tree comparing
  `GetClassName()` strings and schedules a paint on every view that has any `background()`, on each drag event and each
  of the ~15 animation frames of a Space switch.
- Failure scenario: dot or slider drags, which Zen does with one `style` write, repaint dozens of views plus
  full-window gradient shaders per event. `SchedulePaint` coalesces per frame, so it is not a storm, but it adds
  avoidable work with many tabs.
- Fix: only call `SetPalette`/`SchedulePaint` on a swatch when the palette actually changed (store the last palette and
  compare), skip `UpdateControls` from `OnEnvironmentChanged` when the scheme is unchanged, and invalidate a cached list
  of the few views that paint the background (`ThemedBackground` users and the gutter) instead of walking the tree.
  Measure with `scripts/perf` first.

### S8. `docs/REBASE.md` misses the assumptions this PR adds
- New rows cover the two patches but not: the `BUILD.gn` deps added to `chrome/browser:core` and
  `frame:themed_background_impl` (other rows say "+ its BUILD.gn"); `BrowserWidget::GetThemeProvider/GetCustomTheme/
  GetColorProviderKey` honouring `user_color_override` and `color_mode_override` (S2) and
  `Widget::SetColorModeOverride/SetUserColorOverride`; the `MultiContentsBackgroundView` class-name lookup and "a view with
  a `background()` paints the window" in `InvalidateBackground`; `TabStripModelObserver::OnTabGroupFocusChanged` and
  `TabGroupSyncService::Observer::OnTabGroupLocalIdChanged`; `ThemeService::BrowserUsesDarkColors()`.
- Add rows (marked "no patch") for each, as the file does for other unhooked assumptions.

### S9. `GetPreferredColorScheme` hook also changes chrome:// (WebUI) pages (U)
- `patches/chrome/browser/chrome_content_browser_client.cc.patch`: the hook sits in the branch "WebUI and regular
  pages follow the browser theme colour mode", so `chrome://` pages (settings, NTP, `chrome://koan-settings`) get the
  profile's scheme in a themed window whose colour provider (and `colors.css`, which is generated from the window's
  provider with no media query: `theme_colors_source_manager.cc`) is in the Space's mode.
- Failure scenario (uncertain, needs a run): a Space whose theme decides Light while the appearance is Dark gives
  WebUI pages dark `prefers-color-scheme` with light `colors.css` values. Zen's website-appearance override only affects
  content, not chrome pages.
- Fix: skip the hook for `security_principal.IsWebUI()`, as upstream's incognito branch does.

### S10. macOS fullscreen / overlay widgets: the gradient is sized to the view's own widget (U)
- `koan_theme_controller.cc:~160-190` (`PaintBackground`) sizes the gradient to `view->GetWidget()->GetRootView()`. In
  macOS immersive fullscreen (and any compact-mode overlay that is a separate widget; REBASE.md lists the
  `overlay_widget()` case) the toolbar paints in another widget, so its gradient would be cut to that widget's size and
  not line up with the sidebar. Not verifiable here (no Mac, no build). Check, and if true size the gradient to
  the browser widget's root view via `browser_view->GetWidget()`.

---

## Nits

- N1. `koan_theme_controller.cc:239-243`: if Spaces change during a cross-fade, `previous_ = std::move(*current_)` is the
  running animation's target, not what is on screen, so it jumps. Remember the displayed blend or finish first.
- N2. The 250 ms cross-fade, the 400 ms dot spring and the page scroll ignore reduced motion
  (`gfx::Animation::ShouldRenderRichAnimation()` / macOS "Reduce motion"). Zero the duration then.
- N3. `koan_theme_picker.cc:545` (`OnMouseCaptureLost`): a lost capture ends the drag without `OnModelChanged(true)`, so
  the window's mode and accent stay at the pre-drag values until the panel closes (the close path fixes it).
- N4. Stale entries of deleted Spaces stay in `koan.spaces.themes` forever; `session_themes_` is per window, so a theme
  set before a group is saved is not shared and is lost when the key switches to the saved GUID (no migration in
  `OnTabGroupLocalIdChanged`). Moot after B2, otherwise prune on group removal.
- N5. Night and Colorful are Koan inventions (Zen 1.23 has only the strings; `ZEN_PARITY.md` says so). `NativeAccentColor`
  uses `kDarkBase` even for Night/Colorful, unlike the surface colours. Fine, but call it out in the settings note.
- N6. Screenshots are Linux-only and not like-for-like: in `theme-picker-panels-zen-vs-koan.png` the light pair shows
  Zen on preset page 1 and Koan on page 2 (gradient swatches) with the right arrow hovered; the window shot compares
  `example.com` against Koan's NTP, and the sidebar content differs. The issue asks for Mac screenshots eventually.
- N7. `src/browser/ui/views/BUILD.gn`: `"//base:i18n"` and `"//ui/color"` moved out of their sorted positions in
  `impl` deps; `gn format` would put them back.
- N8. The palette entry is Zen's "Open Theme Picker" (matches `ZenUBGlobalActions`), and the Space menu entry is
  "Edit Theme…"; fine. Zen's macOS menu bar also has Spaces > Edit Theme… and View > Appearance
  (`ZenMenubar.mjs`); the PR documents only the Appearance gap. On macOS those menus are the natural entry points.
- N9. The picker's first paint can use the Space-themed colours before `SetColorModeOverride` applies
  (`KoanThemePicker::Show` creates the bubble, then overrides) (U: `ScheduleThemeChanged` may hide it).
- N10. `ShowThemePicker()` does `picker_.reset()` (no save) if a second request arrives while open; the open
  bubble normally closes (and saves) on deactivation first, but a programmatic call would drop unsaved edits. Close
  through the widget so `OnPickerClosed` saves.
- N11. `KoanThemePickerView::OnModelChanged` rebuilds the whole custom-colour list on every drag event when the list is shown.
- N12. Patch size: the `chrome_content_browser_client.cc` hook carries a 7-line if/assign. A single call
  (`koan::AdjustPreferredColorScheme(web_contents, &scheme)`) keeps the patch to one line like the others.
