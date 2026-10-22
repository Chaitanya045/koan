# Third-party notices

Koan's own code is under the MIT license in `LICENSE`, except for the files
listed below: the upstream changes in `patches/`, files ported from Zen
Browser, and a few files derived from Chromium.

Koan is built on **Chromium**, Copyright The Chromium Authors, under a
BSD-style license (https://chromium.googlesource.com/chromium/src/+/main/LICENSE).
The files in `patches/` are modifications of Chromium source files and remain
under Chromium's license. Chromium bundles many third-party components with
their own licenses; a Koan build lists them all at `chrome://credits`.

## Zen Browser (MPL-2.0)

Koan reproduces the user experience of Zen Browser
(https://github.com/zen-browser/desktop), Copyright the Zen Browser
contributors, licensed under the Mozilla Public License 2.0
(https://mozilla.org/MPL/2.0/). Some Koan files port Zen's code, data
tables, strings or style values. Those files are covered by the MPL-2.0 as a
whole, Koan's changes included. Each says so in its header and names the Zen
sources it draws on. The MPL-2.0 applies file by file, so the rest of Koan
remains MIT-licensed. Source for every file is in this repository.

| Koan file | Ported from (Zen) |
| --- | --- |
| `src/browser/koan_shortcut_table.cc` | `ZenKeyboardShortcuts.mjs`, `zen-preferences.ftl` |
| `src/browser/ui/views/koan_commands.cc` | `ZenUBGlobalActions.sys.mjs`, `zen-command-palette.ftl` |
| `src/browser/ui/views/koan_command_palette.cc` | `ZenUBActionsProvider.sys.mjs` (fuzzy scoring) |
| `src/browser/ui/webui/koan_settings_rows.cc` | `zen-preferences.ftl` |
| `src/browser/ui/webui/resources/koan_settings.js` | `zen-preferences.ftl` (strings) |
| `src/browser/ui/views/koan_pinned_tabs.cc` | `ZenPinnedTabManager.mjs`, `zen-general.ftl` (strings) |
| `src/browser/ui/views/koan_sidebar_metrics.h` | Zen's sidebar, toolbar and URL bar stylesheets (sizes, shadows) |
| `src/browser/ui/color/koan_color_mixer.cc` | Zen's theme stylesheets (colour values) |

New files that port Zen material must carry the same header and be added to
this table (see CONTRIBUTING.md).

The comparison screenshots `docs/screenshots/*-zen-vs-koan*.png` show Zen
Browser's interface, for comparison only.

## Chromium-derived files

`src/browser/ui/vector_icons/*.template` and
`src/browser/ui/vector_icons/{light_mode,automatic_appearance}.icon` are
adapted from Chromium and stay under Chromium's BSD-style license.

## Not affiliated

Koan is not affiliated with Google, the Chromium project, or the Zen browser
project. "Zen" is referenced only to describe the user experience Koan aims
for.

## Zen browser assets

Two kinds of files are taken from the Zen browser (https://zen-browser.app,
https://github.com/zen-browser/desktop), which is under the Mozilla Public
License 2.0 (https://mozilla.org/MPL/2.0/); they remain under the MPL-2.0:

- `src/browser/ui/views/resources/grain-bg.png`: Zen's grain texture,
  unmodified.
- `src/browser/ui/vector_icons/koan_theme_*.icon`: Zen's icons
  (`zen-icons/*.svg`), converted to Chromium's vector icon format with
  `scripts/svg_to_icon.py`.
