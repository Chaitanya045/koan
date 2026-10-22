# Koan licence audit: MPL-2.0 material from Zen

Branch: `develop` (HEAD eebe874). Read-only audit; nothing was edited.
Scope: all 94 tracked files under `src/`, `branding/` (SVG, PNG, `.icon`), plus a look at `scripts/`, `docs/` and `patches/` for Zen content.
Zen reference: `.cache/zen-src/` (Zen 1.23.3b, files carry the MPL-2.0 header) and its `omni/` subfolder.

## Method

- Read every file's header.
- Extracted all string literals (`"..."`, `u"..."`, `'...'`, templates) from `src/` and searched them in zen-src (ftl, mjs, css, js, svg).
- Searched every source line of 28 characters or more for exact matches in zen-src: no code line is identical anywhere. The only hits were generic CSS/JS lines.
- Searched the inline SVG `d=` paths in `koan_settings.css` against all Zen SVGs and CSS: no matches.
- Searched the colour hex values in `koan_settings.css` against zen-src: none found.
- Compared the constants and tables in files whose comments cite Zen files, against those files.

Criterion used:

- **High:** a table, algorithm or list that follows the Zen source one-to-one.
- **Medium:** many verbatim Zen strings, including full sentences, or a bulk of exact CSS values.
- **Trivial, not flagged:** a handful of short UI labels, or a few individual numbers.

## 1. Current state

- `THIRD_PARTY_NOTICES.md` mentions only Chromium and says "Koan's own code (everything except `patches/`) is under the MIT license". It has no Zen/MPL entry at all. It also states that Zen is "referenced only to describe the user experience", which is no longer accurate for the files below.
- No file in `src/` or `branding/` has an MPL header. All carry "Copyright 2026 The Koan Authors / MIT", except:
  - `src/browser/ui/vector_icons/automatic_appearance.icon`, `light_mode.icon` and the two `.template` files: Chromium Authors, BSD. These are Chromium files living under `src/`, which the Notices' "everything except patches is MIT" line does not say.
  - `src/browser/ui/webui/resources/koan_settings.html`: no header at all.
- **Files with correct MPL attribution today: none.**
- `.cache/zen-src/README.md` says "do not copy code into Koan". The files below show the data tables and strings were copied.

## 2. Files needing an MPL header and a THIRD_PARTY_NOTICES.md entry

### HIGH confidence

#### 2.1 `src/browser/koan_shortcut_table.cc`

- **Header:** MIT (Koan Authors).
- **Ported from:** `content/zen-components/ZenKeyboardShortcuts.mjs` and `localization/zen-preferences.ftl`.
- **Evidence:**
  - `kGroups` (Koan lines 31-42) reproduces `VALID_SHORTCUT_GROUPS` (Zen 138-145) and the group ids at Zen 133-137. The titles are verbatim: "Other Zen Features", "Window & Tab Management", "Page Operations", "History & Bookmarks", "Media & Display" (`zen-preferences.ftl` ~222-231). The order is Zen's.
  - `kActions` (Koan 45-80) lists Zen's shortcut ids and group assignments: `compact-mode-toggle`, `compact-mode-show-sidebar`, `workspace-forward`/`-backward`/`-create`, `split-view-grid`/`-vertical`/`-horizontal`/`-unsplit`, `new-empty-split-view`, `copy-url`, `copy-url-markdown`, `toggle-pin-tab`, `glance-expand`, `library`. Zen defines them at 754-1334, e.g. `new KeyShortcut("zen-compact-mode-toggle"...` at 754, `zen-workspace-forward` at 793, `zen-split-view-grid` at 817, `zen-new-empty-split-view` at 1163.
  - The labels are verbatim from the ftl: "Toggle Floating Sidebar" (319), "Toggle Compact Mode" (321), "Expand Glance" (322), "Forward Workspace" (333), "Toggle Library" (337), "Toggle Split View Grid" (339), "Close All Unpinned Tabs" (371), "Copy current URL" (247), "Copy current URL as Markdown" (248).
  - `DefaultBindings()` (116-166) is Zen's default key table: Ctrl+S, Ctrl+Alt+S, Ctrl+Alt+Left/Right, Accel+Alt+G/V/H/U, Accel+Shift+C, Accel+O, and others. Its comment at `koan_shortcut_table.h:89` says "Zen's defaults (ZenKeyboardShortcuts.mjs, shortcuts version 21)"; Zen has `LATEST_KBS_VERSION = 21` at line 897.
- **Not ported in this file:** `DisplayText` is independent code. Zen's `toDisplayString` at 207-231 only gives the modifier order, which is not copyable expression.
- **Confidence:** high.

#### 2.2 `src/browser/ui/views/koan_commands.cc`

- **Header:** MIT.
- **Ported from:** `omni/modules/ZenUBGlobalActions.sys.mjs` (`globalActionsTemplate`, lines 36-362) and `localization/zen-command-palette.ftl`.
- **Evidence:**
  - `kZenOrder` (Koan 61-102) reproduces the action ids and their order from `globalActionsTemplate`. The comment says so: "The actions' positions in Zen's list (globalActionsTemplate ...)". Examples:
    - `toggle-compact-mode`=0 maps to Zen 37-41.
    - `open-theme-picker`, `open-library`, `new-split-view`, `unsplit-view`, `new-space`, `new-folder`, `collapse-all-folders`, `expand-all-folders` follow at Zen 42-97.
    - `copy-current-url` is at Zen 98-102 and `settings` at 103-107.
    - The ids are the l10n ids minus the `zen-action-` prefix (`zen-command-palette.ftl` 2-40).
  - The labels at Koan 214-351 are identical to the ftl strings. Examples: "Expand current view", "Copy Current URL", "Duplicate Current Tab", "Reload Tab Without Cache", "Capture Screenshot", "Switch to Automatic Appearance", "Switch to Light Mode", "Switch to Dark Mode", "Open Private Window".
  - The behaviours of `Settings`, `Print`, `Find in Page` and the rest follow the same list.
- **Confidence:** high.

#### 2.3 `src/browser/ui/views/koan_command_palette.cc`

- **Header:** MIT.
- **Ported from:** `omni/modules/ZenUBActionsProvider.sys.mjs`.
- **Evidence:**
  - `KoanCommandPalette::FuzzyScore` (Koan 502-541) is a line-by-line port of Zen's `#calculateFuzzyScore` (Zen 291-345, credited there to @BibekBhusal0). The match is point by point:
    - exact match returns 1,000,000 and prefix match returns 100,000 + length;
    - base bonus 10, +15 at a word start (index 0 or after " ", "-", "_"), +20 times the run length for consecutive matches, minus `min(gap, 10)`.
  - Constants: `kMinimumScore = 92` and `kMinimumPrefixedScore = 30` match Zen 21-22. `kExactMatchScore` and `kPrefixMatchScore` match Zen 28-29, including the same explanatory reasoning.
  - Koan lines 68-80 and 504 carry the comments "Zen (ZenUBActionsProvider)" and "A VS Code-style score, as in Zen".
- **Not verifiable:** `kMaxUsage`/`kMinUsage` (±5) come from `ZenUBResultsLearner.sys.mjs`, which is not in zen-src. They are only constants.
- **Confidence:** high for FuzzyScore.

#### 2.4 `src/browser/ui/webui/koan_settings_rows.cc`

- **Header:** MIT.
- **Ported from:** `localization/zen-preferences.ftl` (also `zen-vertical-tabs.ftl`, `zen-workspaces.ftl`).
- **Evidence:**
  - `kPanes`, `kGroups` and the option tables (Koan 19-103, comment "Zen's groups (zen-preferences.ftl), in Zen's order") are copied title and description pairs. Examples:
    - "Sidebar and tabs layout" (ftl:121), "Choose the layout that suits you best" (9);
    - "Manage your tabs in a vertical layout" (123);
    - "Only show the toolbars you use!" (38);
    - "Customize the dark theme to your liking" (180);
    - "Move the window by dragging empty space at the top of websites, just like the titlebar." (49);
    - "Get a quick overview of your links without opening them in a new tab" (23);
    - "Customize the URL bar to your liking" (192);
    - "With workspaces, you can have multiple browsing sessions at once!" (110);
    - "Manage additional behavior of pinned tabs" (89).
  - The option and checkbox labels (Koan 131-215) are also verbatim: "Briefly make the toolbar popup when switching or opening new tabs in compact mode" (46), "Ignore Pending tabs when cycling with Ctrl+Tab" (78), "Ctrl+Tab cycles within Essential or Workspace tabs only" (76), "When closing a tab, switch to the most recently used tab instead of the next tab" (59), "Reset URL, unload and switch to next tab" (96), "Close Tab Shortcut Behavior" (94), and others.
  - The ordering of the groups follows Zen.
- **Confidence:** high.

### MEDIUM confidence

#### 2.5 `src/browser/ui/webui/resources/koan_settings.js`

- **Header:** MIT.
- **Ported from:** `localization/zen-preferences.ftl`.
- **Evidence:**
  - Koan line 279-280: 'Unsaved shortcut! Please save it by clicking the "Escape" key after retyping it.' is identical to ftl:116.
  - Koan 388-391: 'Customize your keyboard shortcuts' and 'Change the default keyboard shortcuts to your liking and improve your browsing experience' are identical to ftl:210-211.
  - Koan 307: the template `Conflicts with ${group} -> ${label}` follows ftl:117.
  - Koan 401: 'Reset to Default' follows ftl:220.
- **Logic:** independent (see 3). Only the strings are Zen's.
- **Confidence:** medium. The file needs a header only because of these verbatim sentences.

#### 2.6 `src/browser/ui/views/koan_pinned_tabs.cc`

- **Header:** MIT.
- **Ported from:** `localization/zen-general.ftl` and `zen-command-palette.ftl` (strings); behaviour mirrors `content/zen-components/ZenPinnedTabManager.mjs`.
- **Evidence:**
  - Koan 929: "Pinned tab URL has been replaced with the current URL!" (zen-general.ftl:59).
  - Koan 942-944: "Edit Pinned URL" and "Enter the URL this pinned tab should point to:" (ftl:62-63).
  - Koan 963: "That doesn't look like a valid URL." (ftl:61).
  - Koan 970: "Pinned tab URL has been updated!" (ftl:60).
  - Koan 1007: "Tab has been successfully renamed!"
  - Koan 1357-1435: "Remove from Essentials", "Add to Essentials", "Change Label…", "Edit Essential URL", "Reset Essential Tab", "Replace with Current URL".
  - The close-behaviour set (`reset-unload-switch`, `unload-switch`, `reset-switch`, `switch`, `reset`, `close`) matches `zen.pinned-tab-manager.close-shortcut-behavior` (Zen 306-455, `onCloseTabShortcut`). `RunCloseBehaviour` (Koan 1102-1131) uses `behavior.find("reset")` and `find("unload")` where Zen uses `behavior.includes("reset")`. This is the same decision structure but different code (C++, Chromium tab model).
- **Confidence:** medium. The strings are verbatim; the logic is a behaviour re-implementation.

#### 2.7 `src/browser/ui/views/koan_sidebar_metrics.h`

- **Header:** MIT.
- **Ported from:** `content/zen-styles/zen-tabs/vertical-tabs.css`, `zen-omnibox.css`, `zen-popup.css` and `content/ZenUIManager.mjs`. The header comment itself says "Zen's today: zen-tabs/vertical-tabs.css, zen-omnibox.css, zen-popup.css".
- **Evidence (exact value matches):**
  - `kSidebarMinWidth = 163`: vertical-tabs.css:547 (`--zen-toolbox-min-width: 163px`).
  - Pinned slash: `kPinnedSlashWidth = 2.5f`, `kPinnedSlashAngle = 15.f` map to vertical-tabs.css:977-984 (width 2.5px, `rotate(15deg)`).
  - Floating URL bar: `kFloatingUrlBarMinHeight = 333` and `kFloatingUrlBarMaxWidth = 750` are `FLOATING_URLBAR_MIN_HEIGHT` and `_MAX_WIDTH` at ZenUIManager.mjs:10-11. The shadow `0 30px 140px -15px` is zen-omnibox.css:283.
  - Toast: 42px/48px (macOS), padding 6px/8px (macOS), radius 14, 14px font, key cap 12px font, radius 4, padding 5/7: zen-popup.css:327-380.
  - A further 25 or so values are cited in comments.
- **Confidence:** medium. Each value alone is trivial; as a block of CSS design tokens matched to the source, it is the "CSS values copied in bulk" case.

#### 2.8 `src/browser/ui/color/koan_color_mixer.cc`

- **Header:** MIT.
- **Ported from:** `zen-styles/zen-tabs/vertical-tabs.css`, `zen-popup.css`, `zen-glance.css`, `zen-omnibox.css`.
- **Evidence:**
  - `rgba(66, 61, 61, 0.3)` / white 0.3 is vertical-tabs.css:979.
  - Selected-tab alpha 0.85/0.2 is vertical-tabs.css:1181, and the essentials selected values are at 1231-1236. Koan uses 0.8/0.18 for tab rows and 0.85/0.2 for Essentials tiles.
  - `#0f0f0f` at 20% and the "-40%" gradient are zen-popup.css:343.
  - `rgb(220, 53, 69)` is zen-glance.css:81.
  - Floating-bar shadow alphas 0.8/0.6 are zen-omnibox.css:283.
  - Koan's other percentages (11%, 7%, 10%, 70%, 50%, 60%, 8%/15%) are cited in its comments as Zen's.
- **Confidence:** medium (same reasoning as 2.7).

## 3. Checked, below threshold or not ports (for the record)

### Containing only a few Zen strings or numbers (trivial; no header required, optional as a precaution)

- `koan_compact_mode_controller.cc` / `.h`: menu labels "Enable compact mode", "Hide sidebar", "Hide toolbar", "Hide both", "Tabs on the right" (`zen-vertical-tabs.ftl` 3-16) and the toast sentence "Sidebar hidden! Use … to bring the sidebar back." (`zen-general.ftl:68`). A handful of strings; the 1,347 lines of Views/layout logic are independent. Ambiguous: the toast sentence is verbatim.
- `koan_url_bar_controller.cc`: "Copied current URL!" and "Copied current URL as Markdown!" (`zen-general.ftl` 51-52). The 750/333/1.5 floating-bar constants are in the metrics header (2.7). Not a logic port.
- `koan_sidebar_tabs.cc` / `koan_sidebar_tabs_controller.h`: "Clear", "Close all unpinned tabs", "Tabs Closed! Use … to undo." (`zen-workspaces.ftl` 86-88); a few CSS numbers.
- `koan_split_view.cc` / `.h`, `koan_tab_menu.*`: "Add Split View...", "Split Out Tab", "Join N Tabs", "Can't add more panels to the split view!" (`zen-split-view.ftl` 2-6, `zen-general.ftl:67`).
- `koan_glance.h`, `koan_glance_controller.cc/.h`: "Open Link in Glance" (`zen-general.ftl:137`), "Split Link to New Tab" (`zen-split-view.ftl:11`), "Expand Glance"; about ten numbers from `zen-glance.css`/`zen.glance.*` (350 ms, 0.15 s, 80% width, 34px buttons, 12px gap, 3 s confirm, `rgb(220,53,69)` colour). The Glance logic is Chromium-native (own tweens, WebContents handling). Not a port.
- `koan_pinned_tab_views.cc`: "Back to pinned url", "Separate from pinned tab", "Change Icon…", "Change Label…", "Reset and pin tab" (`zen-vertical-tabs.ftl` 43-50, `zen-general.ftl` 33-35). The row, label editor and reset button are Koan Views code. The emoji list (`kIconChoices`) is an independent set of Unicode emoji; Zen's `ZenEmojisData.min.mjs` is a full dataset and was not copied.
- `koan_sidebar_nav_view.cc`: "Copy URL", "Search...".
- `koan_toast.cc` / `.h`: behaviour and look re-implemented from `ZenToasts.mjs` and `zen-popup.css`: 2 s timeout, hover pause, gradient, key cap. Independent Views implementation; the numbers live in `koan_sidebar_metrics.h`.

### Judged not ports

- `src/browser/koan_prefs.cc` / `.h`: pref names are `koan.*`; defaults and value names (`floating-on-type`, `reset-unload-switch`, 800 ms, 45 s) match `zen-default-prefs.js` lines 13-95. Configuration values, not expression. Borderline (the value enumerations), but not flagged.
- `koan_shortcuts.cc` / `.h`, `koan_shortcut_table.h`: Zen's behaviour implemented in C++ over Chromium. No code or table is copied; the data is in `koan_shortcut_table.cc` (2.1). The header's enum and struct types contain no Zen content.
- `koan_workspaces.cc` / `.h`: workspaces on tab groups, own colour and emoji list; "Workspace N" is trivial. The Zen `ZenSpace*.mjs` code was not copied.
- `koan_essentials.cc` / `.h`: independent paint function; the 4-column cap and 12 Essentials are numbers.
- `koan_tab_activation.*`, `koan_tab_hooks.*`, `koan_session_data.*`, `koan_browser_view_controller.*`, `koan_browser_view_hooks.*`, `koan_compact_mode.h`, `koan_content_card.*`, `koan_window_controls.*`, `koan_sidebar_header.h`, `koan_url_bar.h`, `koan_pinned.h`, `koan_pinned_tabs_hooks.h`, `koan_commands.h`, `koan_command_registry.*`, `koan_omnibox_hooks.h`: Koan code that implements Zen's behaviour; no table, string set or code copied.
- `koan_features.*`, `koan_flag_entries.h`, `koan_switches.h`, `koan_renderer_switches.*`, `koan_feature_overrides.*`: original flag text.
- `koan_settings_ui.*`, `koan_settings_handler.*`, `koan_settings_rows.h`, `embed_resources.py`: original.
- `koan_settings.css`: sizes and colours "measured from Zen 1.23" (the file says so). No colour hex value and no inline-SVG `d=` path occurs in zen-src, and no line matches distinctive Zen CSS. The layout picker is drawn in CSS (own gradient, no copy of `layouts/*.png`). Not a port.
- `koan_settings.html`: original (the ensō logo SVG is Koan's own); has no licence header at all (housekeeping).
- `src/common/koan_branding.h`, `koan_url_constants.h`, all `BUILD.gn`: original.
- `src/browser/ui/vector_icons/dark_mode.icon`: a simple crescent made of two arcs, written by Koan; the comment says "Zen's Switch to Dark Mode" but no Zen SVG data was copied (zen-icons are not in zen-src). `light_mode.icon` and `automatic_appearance.icon` are Chromium-copyright files. Not Zen.
- `branding/logo.svg`, `logo_mono.svg`, `wordmark.svg`, `branding/out/*`, `branding/mac/**` (including `AppIcon.icon/icon.json` and `Assets/enso.svg`): Koan's own ensō artwork; "Zen" only refers to the brush-circle concept. No Zen asset.
- Other src files: they contain no Zen strings, tables or assets. In addition, `patches/`, which is excluded from the audit, contains only ~13 small hook diffs with comments mentioning Zen; they are Chromium-licensed modifications.

## 4. Outside the requested scope: worth a look

- `docs/screenshots/*zen-vs-koan*.png` (palette, settings, scrollbars, sidebar-tabs) are screenshots of Zen's own UI, including its layout preview artwork and wallpaper (the `settings-zen-vs-koan.png` left half shows Zen's `layouts/*.png` pictures). These are Zen assets (MPL-2.0 or its own artwork licences). Consider crediting or removing them; they are not source files.
- `docs/ZEN_PARITY.md` and the README quote Zen setting names and labels in prose. This is fine as a comparison, but it should be accompanied by an MPL notice if the strings are treated as copied.
- `scripts/e2e/*.mjs` and `.agent/notes/` were not audited line by line. They mention Zen only in comments and test names.

## 5. Recommended actions

1. Add this header to files 2.1-2.4 (high), then to 2.5-2.8 (medium) at your discretion:

   ```
   // This Source Code Form is subject to the terms of the Mozilla Public
   // License, v. 2.0. If a copy of the MPL was not distributed with this
   // file, You can obtain one at http://mozilla.org/MPL/2.0/.
   //
   // Ported from zen-browser/desktop <path>, Copyright the Zen Browser Authors.
   ```

   Keep the Koan copyright line as well. In the `.js` file use the same text.
2. Add a "Zen Browser (MPL-2.0)" section to `THIRD_PARTY_NOTICES.md`: list the files above with their Zen source paths and link the MPL text. Fix the sentence saying Zen is "referenced only to describe the user experience".
3. In the same file, mention that `src/browser/ui/vector_icons/*.icon` (two) and `*.template` are under Chromium's licence.
4. Remove or alter the instruction in `.cache/zen-src/README.md` once these ports are attributed.
