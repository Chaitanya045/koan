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
| `src/browser/ui/views/koan_sidebar_curve.{h,cc}` and its unit test | `src/zen/compact-mode/sidebar.inc.css` (compact mode's slide easing table, 250 ms / 150 ms timings) |
| `src/browser/ui/views/koan_commands.cc` | `ZenUBGlobalActions.sys.mjs`, `zen-command-palette.ftl` |
| `src/browser/ui/views/koan_command_palette.cc` | `ZenUBActionsProvider.sys.mjs` (fuzzy scoring) |
| `src/browser/ui/webui/koan_settings_rows.cc` | `zen-preferences.ftl` |
| `src/browser/ui/webui/resources/koan_settings.js` | `zen-preferences.ftl` (strings) |
| `src/browser/ui/views/koan_pinned_tabs.cc` | `ZenPinnedTabManager.mjs`, `zen-general.ftl` (strings) |
| `src/browser/ui/views/koan_sidebar_metrics.h` | Zen's sidebar, toolbar and URL bar stylesheets (sizes, shadows), `zen-workspaces.css` (Space sizes) |
| `src/browser/ui/color/koan_color_mixer.cc` | Zen's theme stylesheets (colour values), `zen-workspaces.css` (Space colours), `zen-library.css` (Library colours), `zen-omnibox.css` and `zen-popup.css` (the floating URL bar card's colours) |
| `src/browser/ui/color/koan_tokens.{h,cc}` | `zen-theme.css` (Zen's colours), `ZenGradientGenerator.mjs`, `zen-buttons.css` (the primary and accent colours' derivation) |
| `src/browser/ui/color/koan_shadcn_color_mixer.cc` | `zen-omnibox.css`, `zen-popup.css` (the floating URL bar card and suggestion colours), `zen-theme.css` (tab colours) |
| `src/browser/ui/views/koan_omnibox_rows.{h,cc}` | `zen-omnibox.css` (the suggestion rows' height, radius and font) |
| `src/browser/ui/views/koan_url_bar_controller.cc` | `zen-omnibox.css`, `ZenUIManager.mjs` (the floating URL bar card: size, position, input, divider), the `zen.urlbar` preferences |
| `src/browser/ui/views/koan_space_menu.cc` | `ZenSpaceManager.mjs` (the Space and Create menus), `zen-workspaces.ftl` (strings) |
| `src/browser/ui/views/koan_space_creation.cc` | `ZenSpaceCreation.mjs`, `zen-workspaces.ftl` (strings) |
| `src/browser/ui/views/koan_library.h`, `koan_library_controller.cc` | `ZenLibrary.mjs` (open/close spring, toggle, unmounting) |
| `src/browser/ui/views/koan_library_view.{h,cc}` | `ZenLibrary.mjs`, `ZenLibrarySearchSection.mjs`, `zen-library.css` (layout), `zen-library.ftl` (strings) |
| `src/browser/ui/views/koan_library_ui.h` | `zen-library.css` (sizes, radii) |
| `src/browser/ui/views/koan_library_history.cc` | `ZenLibraryHistorySection.mjs`, `zen-library.ftl` (strings) |
| `src/browser/ui/views/koan_library_downloads.cc` | `ZenLibraryDownloadsSection.mjs`, `zen-library.ftl` (strings) |
| `src/browser/ui/views/koan_library_spaces.cc` | `ZenLibrarySpacesSection.mjs`, `zen-library.css` (cards), `zen-library.ftl` (strings) |
| `src/browser/ui/views/koan_library_icons.cc` | `zen-icons/library/library-{downloads,history,spaces}-sprite.svg` (section icons) |
| `src/browser/ui/views/koan_zen_welcome.h` | `ZenWelcome.mjs`, `zen-welcome.ftl` (the welcome screen's strings, pages and Essentials) |
| `src/browser/ui/views/koan_zen_window_drag.{h,cc}` | `ZenWindowDragChild.sys.mjs`, `zen-default-prefs.js` (where a press may drag the window, the thresholds) |
| `src/browser/ui/views/koan_native_window_move_mac.mm` | `nsZenNativeWindowMove.mm` (the native window move on macOS), `expose_tiled_attribute_to_all_platforms.patch` (`WindowIsTiled`, the tiling state of a window) |
| `src/browser/ui/views/koan_zen_download_animation.{h,cc}` | `ZenDownloadAnimation.mjs` (the download arc, the tile's timing) |
| `src/browser/ui/views/koan_zen_media.{h,cc}` | `ZenMediaController.mjs`, `zen-media-controls.css` (the media cards' stack, timing and time format) |
| `src/browser/ui/views/koan_zen_toasts.h` | `zen-general.ftl` (the background tab and renamed workspace toasts) |
| `src/browser/ui/views/koan_swipe_tracker.{h,cc}`, `koan_swipe_tracker_unittest.cc` | `ZenSpacesSwipe.mjs` (success threshold, velocity weight, resistance), Gecko's `SwipeTracker.cpp` units as Zen patches it |

New files that port Zen material must carry the same header and be added to
this table (see CONTRIBUTING.md).

The comparison screenshots `docs/screenshots/*-zen-vs-koan*.png` show Zen
Browser's interface, for comparison only.

## shadcn/ui (MIT)

The colour tokens and palettes in `src/browser/ui/color/koan_shadcn_palettes.inc`
(generated by `scripts/gen_shadcn_palettes.py`) are shadcn/ui's, from
https://github.com/shadcn-ui/ui (MIT, Copyright (c) 2023 shadcn).

## Geist (SIL Open Font License 1.1)

`src/browser/ui/fonts/geist/` bundles Geist Sans and Geist Mono, Copyright (c)
2023 Vercel, in collaboration with basement.studio, under the SIL Open Font
License 1.1 (`src/browser/ui/fonts/geist/OFL.txt`).

## Chromium-derived files

`src/browser/ui/vector_icons/*.template` and
`src/browser/ui/vector_icons/{light_mode,automatic_appearance}.icon` are
adapted from Chromium and stay under Chromium's BSD-style license.

## Not affiliated

Koan is not affiliated with Google, the Chromium project, or the Zen browser
project. "Zen" is referenced only to describe the user experience Koan aims
for.
