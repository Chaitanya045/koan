# Koan's icons (#106)

Zen 1.24b draws its UI with Nucleo icons, which can't be redistributed (see
`zen-icons-inventory.md` §0). Koan uses a free look-alike set in **Zen's icon
vocabulary**: the same names (`back`, `reload`, `sidebar`, ...), glyph sizes
and tinting, so a surface in the inventory maps to one Koan icon id.

## The set: Lucide (ISC), no second set needed

Compared against Zen's glyphs in `screenshots/zen-mac/` (top row, tab rows,
bottom bar), on stroke weight, caps, grid and coverage:

| Set | Licence | Grid, default stroke | Stroke / grid | Caps, joins | Coverage of Zen's 158 names |
| --- | --- | --- | --- | --- | --- |
| **Lucide** | ISC | 24, 2 | **8.3%** | round, round | **all 158** (each name has a glyph of the same meaning) |
| Tabler (outline) | MIT | 24, 2 (adjustable) | 8.3% | round, round | wide, but squarer glyphs, many near-duplicates |
| Phosphor (regular) | MIT | 256, 16 | 6.3% | round | wide; lighter than Zen's |
| Iconoir | MIT | 24, 1.5 | 6.3% | round | wide; lighter than Zen's |
| Heroicons (outline) | MIT | 24, 1.5 | 6.3% | round | ~300 icons: no panel/sidebar, history, library, ... |

- Zen's strokes in the screenshots are about 1.3-1.5 px at 17 pt, i.e. 8% of the
  glyph box (Nucleo "outline 18": 1.5 on 18 = 8.3%). Lucide's 2 on 24 is the same
  ratio, with rounded caps and joins like Zen's; the lighter sets (6.3%) read as
  clearly thinner next to Zen's text. (Stroke figures for Phosphor, Iconoir and
  Heroicons are the sets' documented defaults; only Lucide and Tabler were
  downloaded and compared glyph by glyph.)
- One set for all 158 names but `library` (Zen's own MPL glyph, see below), so no
  fallback set is vendored. Tabler stays the
  fallback if a future surface needs a glyph Lucide lacks.
- Where Zen's glyph has a distinct shape (Zen's `sidebar` has a small marker in
  the left pane), Lucide's nearest is used; the mapping below is the judgement
  call for each.

## Fit to Zen's size

Nucleo glyphs fill about 91% of their 18-unit box; Lucide's fill 83% of 24
(2 units of padding). So that an icon is as big as Zen's at the same size
(17 pt toolbar, 16 pt favicons, 18 pt app menu, 20 pt bottom bar), the
converter scales the geometry by 1.1 about the centre (`ZEN_FIT` in
`scripts/svg_to_icon.py`) and then strokes it with Lucide's own width. Glyphs
that reach the edge of Lucide's box (key, hammer, dice, brushes, ...) would
then pass the canvas and be clipped, so after stroking and union the converter
measures the outline's points (control points included) and, if any is closer
than `EDGE_PAD` (0.5 unit, 0.35 px at 17 pt) to the canvas edge, shrinks the
whole outline about the centre until it fits, stroke with it. 114 of 148
icons end up a few percent below 1.1 (the worst, `passwords`, at 0.81). The
canvas stays 24, so every call site's `dip_size` still means what it did for
Chromium's icons.

## How it works

1. `third_party/icons/mapping.txt`: Zen name -> Lucide icon (+ `fill` for
   Zen's `-fill` variants where the filled shape keeps its detail).
2. `scripts/gen_koan_icons.py` copies the used SVGs to `third_party/icons/lucide/`
   (with `LICENSE-lucide`) and converts each with `scripts/svg_to_icon.py` to
   `src/browser/ui/vector_icons/koan_<zen_name>.icon`, i.e.
   `koan::kKoan<ZenName>Icon` (header
   `koan/browser/ui/vector_icons/vector_icons.h`). Strokes are turned into
   outlines with Skia's own stroker (round caps and joins, which `.icon`'s
   `STROKE` can't do) and unioned, so an icon is one non-zero path.
3. Monochrome: no colour is baked in. Every call site keeps its colour id and
   opacity (`kColorKoanToolboxText` etc. from #97; `ui::kColorIcon` where Chromium
   chose it), as Zen's `-moz-context-properties: fill`.
4. **Registry** (`src/browser/ui/vector_icons/koan_icons.{h,cc}`):
   `koan::KoanIconFor(icon)` returns the Koan icon for each of 308 Chromium
   Material icons (all variants, old and Chrome Refresh, of one concept; the
   second column of the table). `koan::InstallVectorIconOverride()` makes
   `gfx::CreateVectorIcon` and `gfx::PaintVectorIcon` use it (hook
   `patches/ui/gfx/paint_vector_icon.cc.patch`, 3 small edits in one `.cc`, no
   header). So the toolbar, tabs, location bar, menus, download bubble,
   permission chips, ... that ask for a Material icon paint Zen's glyph with
   no upstream edit and no change in Koan's views. Sizes and colours stay the
   caller's. Koan views that choose an icon of Zen's vocabulary themselves use
   `koan::kKoan...Icon` directly.
5. Sizes: the sizes Zen draws icons at are `koan_icon_sizes.h` (17 toolbar,
   16 favicon, 18 menu, 20 bottom-bar "+"; the sidebar's top row and the Space
   bar use them) and the per-view ones in `koan_sidebar_metrics.h` (Clear 10,
   URL pill 16, New Tab row 16, Library 18, Welcome arrow 17). The hook passes
   the size to the registry, so the trace records each icon with the size it
   was asked for.
6. Tests:
   - `scripts/icon_check.py`: the committed `.icon` files are current; every
     outline point is inside the 24 canvas, and rendering with a margin (an
     unclipped reference) draws nothing outside it, at 16, 17, 18 and 20 pt;
     converter fidelity against the SVG at the same sizes, 1x and 2x (worst
     mean alpha difference 2.3%).
   - `koan_icons_unittests` (`src/browser/ui/vector_icons/koan_icons_unittest.cc`;
     `scripts/build.sh koan_icons_unittests`, run `out/Koan/koan_icons_unittests`):
     every table entry exists, is a Koan icon on the 24 canvas, each Chromium
     icon is mapped once, Koan and unmapped icons are left alone, Zen's sizes,
     the URL bar magnifier's 14 unit ink, and `KoanIconsHookTest` (the gfx
     hook: a Chromium icon paints pixel-equal to its Koan icon through both
     `CreateVectorIcon` and `PaintVectorIcon`). Not in CI: the Mac loop builds
     and runs it (`scripts/build.sh koan_icons_unittests`).
   - `scripts/e2e/icons.mjs`: the live window paints Koan icons, at the sizes
     above, for Back/Forward/Reload, the New Tab row, the bottom bar's "+", the
     Space actions, Clear, Copy URL, site information, Library and the tab close
     button, and no Material variant of Zen's vocabulary is left unmapped.
     `--koan-icon-trace=<file>` is for it; the trace exists in builds with
     DCHECKs only and writes from a thread pool task, not the paint thread.

## Not covered (and why)

- **WebUI pages** (chrome://settings, chrome://koan-settings, history, downloads,
  new tab): they draw their own SVGs/iron-icons, not `gfx::VectorIcon`s. Koan's
  own pages use their own sheet.
- **Native macOS menus and the menu bar** have no icons in Zen either.
- **Library rail sprites, `library-tile.svg`, `urlbar-arrow.svg` and the 87
  `selectable/` Space icons** are Zen's own MPL/MIT files (allowed); they are
  not ported by this change (Koan keeps its drawn Library rail glyphs and emoji
  Space icons). **`library.svg` is ported**: `third_party/icons/zen/library.svg`
  (MPL header kept; its resting state in the sprite's 18-unit frame) is
  `koan::kKoanLibraryIcon`, on an 18 canvas, drawn at 19 dip in the Library
  button (Zen: 18 px frame at 1.08x) and 18 in the download tile. It replaces
  Koan's own `library.icon` (`kLibraryIcon`, removed).
- Zen's icons with no counterpart in Chromium (Boosts, `sparkles`, ...) exist
  as Koan icons for the Koan views that need them; nothing asks for them yet.
- **Note on #62**: the theme-picker branch's `koan_theme_*.icon` files were
  converted from Zen's `zen-icons/*.svg`, which are Nucleo. They are replaced by
  the Koan set (`sparkles`, `face-sun`, `moon-stars`, `plus`, `unpin`,
  `algorithm`, `arrow-left`, `arrow-right`, `paintbrush-fill`: ids
  `koan::kKoan<ZenName>Icon`). Note `paintbrush-fill` is the outline brush
  (Lucide has no filled one that keeps its detail).
- Zen's `signature.svg` and `check.svg` in its "Nucleo" folder are Lucide's own
  paths; Koan's come from Lucide.

## Surfaces swept (every icon of Koan's UI)

Swept on develop aa2ef917 + #110. Every `k...Icon` that a file in `src/` asks
for is either a Koan icon already or in the registry's table (a unit-tested
list: no Koan code asks for a Chromium icon the table lacks). **Koan's own
views:**

| Surface | Where | Icon asked for | Painted as |
| --- | --- | --- | --- |
| Compact mode toast | `koan_compact_mode_controller.cc:630` | `kTabIcon` | `kKoanTabIcon` |
| Download animation | `koan_download_animation.cc:135` | `kKoanLibraryIcon` | same (Koan icon chosen directly) |
| Download animation | `koan_download_animation.cc:107` | `kDownloadIcon` | `kKoanDownloadsIcon` |
| Emoji picker | `koan_emoji_picker.cc:446` | `kDeleteIcon` | `kKoanTrashIcon` |
| Floating URL bar (⌘L / ⌘T) | `koan_url_bar_controller.cc:213` | `kKoanUrlbarSearchGlassIcon` | same (Koan icon chosen directly) |
| Glance | `koan_glance_controller.cc:334` | `kSplitSceneIcon` | `kKoanSplitIcon` |
| Glance | `koan_glance_controller.cc:330` | `kCloseIcon` | `kKoanCloseIcon` |
| Glance | `koan_glance_controller.cc:332` | `kKoanFullscreenIcon` | same (Koan icon chosen directly) |
| Library button | `koan_library_controller.cc:113` | `kKoanLibraryIcon` | same (Koan icon chosen directly) |
| Library panel | `koan_library_view.cc:629` | `kSearchIcon` | `kKoanSearchGlassIcon` |
| Library panel | `koan_library_view.cc:983` | `kArrowForwardIcon` | `kKoanForwardIcon` |
| Library panel | `koan_library_view.cc:893` | `kArrowBackIcon` | `kKoanBackIcon` |
| Library panel | `koan_library_view.cc:699` | `kKoanCircleBarsFilterIcon` | same (Koan icon chosen directly) |
| Library: Downloads | `koan_library_downloads.cc:119` | `kSearchIcon` | `kKoanSearchGlassIcon` |
| Library: Downloads | `koan_library_downloads.cc:718` | `kCloseIcon` | `kKoanCloseIcon` |
| Library: Downloads | `koan_library_downloads.cc:723` | `kReloadChromeRefreshOldIcon` | `kKoanReloadIcon` |
| Library: Downloads | `koan_library_downloads.cc:375` | `kDownloadIcon` | `kKoanDownloadsIcon` |
| Library: Downloads | `koan_library_downloads.cc:125` | `kFolderOpenIcon` | `kKoanFolderIcon` |
| Library: Downloads | `koan_library_downloads.cc:399` | `kInsertDriveFileOutlineOldIcon` | `kKoanPagePortraitIcon` |
| Library: Downloads | `koan_library_downloads.cc:730` | `kMoreHorizIcon` | `kKoanMenuIcon` |
| Library: History | `koan_library_history.cc:589` | `kUndoIcon` | `kKoanUTurnToLeftIcon` |
| Library: History | `koan_library_history.cc:583` | `kDeleteIcon` | `kKoanTrashIcon` |
| Library: History | `koan_library_history.cc:577` | `kGlobeIcon` | `kKoanDefaultFaviconIcon` |
| Library: Spaces | `koan_library_spaces.cc:721` | `kMoreHorizIcon` | `kKoanMenuIcon` |
| Library: Spaces | `koan_library_spaces.cc:700` | `kGlobeIcon` | `kKoanDefaultFaviconIcon` |
| Library: Spaces | `koan_library_spaces.cc:675` | `kKoanPaintbrushFillIcon` | same (Koan icon chosen directly) |
| Library: Spaces | `koan_library_spaces.cc:869` | `kKoanMoveCursorIcon` | same (Koan icon chosen directly) |
| Media controller | `koan_media_controller.cc:430` | `kCloseIcon` | `kKoanCloseIcon` |
| Media controller | `koan_media_controller.cc:475` | `kGlobeIcon` | `kKoanDefaultFaviconIcon` |
| Media controller | `koan_media_controller.cc:399` | `kSkipPreviousIcon` | `kKoanMediaPreviousIcon` |
| Media controller | `koan_media_controller.cc:403` | `kPlayArrowIcon` | `kKoanMediaPlayIcon` |
| Media controller | `koan_media_controller.cc:406` | `kSkipNextIcon` | `kKoanMediaNextIcon` |
| Media controller | `koan_media_controller.cc:409` | `kVolumeUpIcon` | `kKoanMediaUnmuteIcon` |
| Media controller | `koan_media_controller.cc:427` | `kPictureInPictureAltIcon` | `kKoanPictureInPictureIcon` |
| Media controller | `koan_media_controller.cc:493` | `kPauseIcon` | `kKoanMediaPauseIcon` |
| Media controller | `koan_media_controller.cc:501` | `kVolumeOffIcon` | `kKoanMediaMuteIcon` |
| Pinned tab reset button | `koan_pinned_tab_views.cc:319` | `kRemoveIcon` | `kKoanUnpinIcon` |
| Sidebar tab list (New Tab row, Clear) | `koan_sidebar_tabs.cc:232` | `kAdd2Icon` | `kKoanPlusIcon` |
| Sidebar tab list (New Tab row, Clear) | `koan_sidebar_tabs.cc:332` | `kKoanDartDownIcon` | same (Koan icon chosen directly) |
| Sidebar top row, URL pill | `koan_sidebar_nav_view.cc:180` | `kReloadChromeRefreshOldIcon` | `kKoanReloadIcon` |
| Sidebar top row, URL pill | `koan_sidebar_nav_view.cc:176` | `kBackArrowChromeRefreshOldIcon` | `kKoanBackIcon` |
| Sidebar top row, URL pill | `koan_sidebar_nav_view.cc:178` | `kForwardArrowChromeRefreshOldIcon` | `kKoanForwardIcon` |
| Sidebar top row, URL pill | `koan_sidebar_nav_view.cc:308` | `kKoanUrlbarSearchGlassIcon` | same (Koan icon chosen directly) |
| Sidebar top row, URL pill | `koan_sidebar_nav_view.cc:336` | `kKoanLinkIcon` | same (Koan icon chosen directly) |
| Space header | `koan_space_header.cc:96` | `kMoreHorizIcon` | `kKoanMenuIcon` |
| Spaces bottom bar | `koan_space_bar.cc:227` | `kAddIcon` | `kKoanPlusIcon` |
| Spaces: Create menu | `koan_workspaces.cc:1154` | `kSplitSceneIcon` | `kKoanSplitIcon` |
| Spaces: Create menu | `koan_workspaces.cc:1155` | `kAddIcon` | `kKoanPlusIcon` |
| Spaces: Create menu | `koan_workspaces.cc:1132` | `kKoanPlusIcon` | same (Koan icon chosen directly) |
| Welcome | `koan_welcome.cc:270` | `kArrowForwardIcon` | `kKoanForwardIcon` |
| Welcome | `koan_welcome.cc:300` | `kArrowBackIcon` | `kKoanBackIcon` |
| ⌘T overlay (commands) | `koan_command_palette.cc:231` | `kSearchIcon` | `kKoanSearchGlassIcon` |
| ⌘T overlay (commands) | `koan_command_palette.cc:232` | `kSearchChromeRefreshOldIcon` | `kKoanSearchGlassIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:219` | `kDockToLeftIcon` | `kKoanSidebarIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:222` | `kKoanLibraryIcon` | same (Koan icon chosen directly) |
| ⌘T overlay and palette commands | `koan_commands.cc:225` | `kSplitSceneIcon` | `kKoanSplitIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:231` | `kLinkIcon` | `kKoanLinkIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:234` | `kSettingsIcon` | `kKoanSettingsIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:238` | `kIncognitoIcon` | `kKoanPrivateWindowIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:241` | `kNewWindowIcon` | `kKoanWindowIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:243` | `kKeepIcon` | `kKoanPinIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:245` | `kKeepOffIcon` | `kKoanUnpinIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:248` | `kCloseIcon` | `kKoanCloseIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:251` | `kHistoryIcon` | `kKoanHistoryIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:254` | `kContentCopyIcon` | `kKoanEditCopyIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:261` | `kUndoIcon` | `kKoanUTurnToLeftIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:265` | `kReloadChromeRefreshOldIcon` | `kKoanReloadIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:273` | `kArrowForwardIcon` | `kKoanForwardIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:276` | `kArrowBackIcon` | `kKoanBackIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:279` | `kScreenshotRegionIcon` | `kKoanScreenshotIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:288` | `kKoanEssentialAddIcon` | same (Koan icon chosen directly) |
| ⌘T overlay and palette commands | `koan_commands.cc:296` | `kKoanEssentialRemoveIcon` | same (Koan icon chosen directly) |
| ⌘T overlay and palette commands | `koan_commands.cc:304` | `kFindInPageIcon` | `kKoanSearchPageIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:308` | `kChromeExtensionIcon` | `kKoanExtensionIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:314` | `kKoanSparklesIcon` | same (Koan icon chosen directly) |
| ⌘T overlay and palette commands | `koan_commands.cc:317` | `kKoanFaceSunIcon` | same (Koan icon chosen directly) |
| ⌘T overlay and palette commands | `koan_commands.cc:320` | `kKoanMoonStarsIcon` | same (Koan icon chosen directly) |
| ⌘T overlay and palette commands | `koan_commands.cc:322` | `kPrintIcon` | `kKoanPrintIcon` |
| ⌘T overlay and palette commands | `koan_commands.cc:333` | `kAddIcon` | `kKoanPlusIcon` |

**Upstream-owned surfaces** (no Koan code; painted through the gfx hook, all
in the table): the tab close button and favicon fallback (`kCloseIcon`,
`kGlobeIcon`), tab audio / mute indicators (`kVolumeUpIcon`, `kVolumeOffIcon`,
`kSpeakerIcon`), the location bar's page actions (bookmark star, share,
site info `kTuneIcon`, lock / not secure, extensions `kExtensionIcon`) and
permission chips (location, camera, microphone, notifications, cookies,
MIDI, storage), the download bubble and toolbar button, the omnibox rows
(tab `kTabIcon`, history, bookmark, search `kSearchIcon`), the sidebar toggle
(`kMenuCloseCustomIcon` / `kMenuOpenIcon`), the app menu (`kBrowserTools...`).

**Zen-derived, drawn rather than vector icons** (kept): the Library rail's
History / Downloads / Spaces glyphs (`koan_library_icons.cc`, from Zen's
sprites), Space and tab emoji, the download arc.

**Left as Chromium's, because Zen has no such icon:** omnibox answers
(calculator, weather, finance, dictionary), Drive and enterprise rows, the
offline dino, sync / sign-in, cast, Google Lens / AI. None is reachable in a
default Koan window (AI Mode is being removed, #178).

**The URL bar magnifier** is Firefox's own `search-glass.svg` (MPL-2.0), as Zen
draws it in the identity box: a 16 pt box, 14 pt of glyph (1.0 to 15.0),
half opaque (an `alpha` of 128 in the icon file, Zen's `fill-opacity: .5`),
in the floating bar (`kFloatingUrlBarMagnifierSize`) and in the sidebar pill on
an empty tab. It is not Lucide's `search` (14.6 pt at 16).

## Table

Zen icon -> Lucide glyph -> Koan id, the inventory sections where the icon is
used, and (first three of) the Chromium icons that paint as it.

| Zen icon | Glyph | Koan id | Inventory | Chromium icons swapped |
| --- | --- | --- | --- | --- |
| `sidebar` | lucide `panel-left` | `koan::kKoanSidebarIcon` | §1.1, §1.8 | `kMenuCloseCustomIcon`, `kMenuOpenIcon`, `kMenuOpenOldIcon` … |
| `sidebar-right` | lucide `panel-right` | `koan::kKoanSidebarRightIcon` | §1.1 | `kDockToRightIcon`, `kDockToRightOldIcon`, `kRightPanelCloseFlippableIcon` … |
| `expand-sidebar` | lucide `panel-left-open` | `koan::kKoanExpandSidebarIcon` | §1.5 | Koan code only |
| `sidebars-right` | lucide `panel-right-open` | `koan::kKoanSidebarsRightIcon` | icons.css only | Koan code only |
| `back` | lucide `arrow-left` | `koan::kKoanBackIcon` | §1.1, §1.10, §1.12 | `kBackArrowChromeRefreshOldIcon`, `kBackArrowOldIcon`, `kArrowBackIcon` … |
| `forward` | lucide `arrow-right` | `koan::kKoanForwardIcon` | §1.1, §1.12 | `kForwardArrowChromeRefreshOldIcon`, `kForwardArrowOldIcon`, `kArrowForwardIcon` … |
| `arrow-left` | lucide `arrow-left` | `koan::kKoanArrowLeftIcon` | §1.6 | Koan code only |
| `arrow-right` | lucide `arrow-right` | `koan::kKoanArrowRightIcon` | §1.3, §1.6 | `kChevronRightIcon`, `kChevronRightChromeRefreshOldIcon`, `kChevronRightOldIcon` … |
| `arrow-up` | lucide `arrow-up` | `koan::kKoanArrowUpIcon` | icons.css only | Koan code only |
| `arrow-down` | lucide `arrow-down` | `koan::kKoanArrowDownIcon` | icons.css only | Koan code only |
| `arrow-corner-down-right` | lucide `corner-down-right` | `koan::kKoanArrowCornerDownRightIcon` | icons.css only | Koan code only |
| `arrows-collapse` | lucide `minimize-2` | `koan::kKoanArrowsCollapseIcon` | icons.css only | Koan code only |
| `arrows-expand` | lucide `maximize-2` | `koan::kKoanArrowsExpandIcon` | icons.css only | Koan code only |
| `reload` | lucide `rotate-cw` | `koan::kKoanReloadIcon` | §1.1 | `kReloadChromeRefreshOldIcon`, `kReloadIcon`, `kReloadTouchOldIcon` … |
| `arrow-rotate-anticlockwise` | lucide `rotate-ccw` | `koan::kKoanArrowRotateAnticlockwiseIcon` | icons.css only | Koan code only |
| `close` | lucide `x` | `koan::kKoanCloseIcon` | §1.1, §1.3, §1.7, §1.8, §1.13 | `kCloseIcon`, `kCloseChromeRefreshOldIcon`, `kCloseOldIcon` … |
| `close-filled-round` | lucide `circle-x` | `koan::kKoanCloseFilledRoundIcon` | icons.css only | Koan code only |
| `plus` | lucide `plus` | `koan::kKoanPlusIcon` | §1.3, §1.5, §1.6, §1.12 | `kAdd2Icon`, `kAddOldIcon`, `kAddIcon` … |
| `menu` | lucide `ellipsis` | `koan::kKoanMenuIcon` | §1.1, §1.5, §1.6 | `kMoreVertIcon`, `kMoreHorizIcon`, `kBrowserToolsChromeRefreshOldIcon` … |
| `check` | lucide `check` | `koan::kKoanCheckIcon` | icons.css only | `kCheckIcon`, `kCheckOldIcon`, `kCheckSmallIcon` |
| `chevron` | lucide `chevron-down` | `koan::kKoanChevronIcon` | icons.css only | `kKeyboardArrowDownIcon`, `kKeyboardArrowDownChromeRefreshOldIcon`, `kKeyboardArrowDownOldIcon` … |
| `dart-down` | lucide `chevron-down` | `koan::kKoanDartDownIcon` | §1.5 | `kArrowDropDownIcon`, `kArrowDropDownOldIcon` |
| `search-glass` | lucide `search` | `koan::kKoanSearchGlassIcon` | §1.2, §1.10 | `kSearchIcon`, `kSearchChromeRefreshOldIcon`, `kSearchOldIcon` … |
| `search-page` | lucide `file-search` | `koan::kKoanSearchPageIcon` | §1.6 | `kFindInPageIcon`, `kFindInPageChromeRefreshOldIcon` |
| `settings` | lucide `settings` | `koan::kKoanSettingsIcon` | §1.3, §1.6 | `kSettingsIcon`, `kSettingsChromeRefreshOldIcon`, `kSettingsOldIcon` … |
| `settings-fill` | lucide `settings` | `koan::kKoanSettingsFillIcon` | icons.css only | Koan code only |
| `sliders` | lucide `sliders-horizontal` | `koan::kKoanSlidersIcon` | §1.13 | Koan code only |
| `downloads` | lucide `download` | `koan::kKoanDownloadsIcon` | §1.6, §1.9 | `kDownloadIcon`, `kFileDownloadChromeRefreshOldIcon`, `kFileDownloadOldIcon` … |
| `history` | lucide `history` | `koan::kKoanHistoryIcon` | §1.2, §1.6 | `kHistoryIcon`, `kHistoryChromeRefreshOldIcon`, `kHistoryOldIcon` |
| `bookmark-hollow` | lucide `bookmark` | `koan::kKoanBookmarkHollowIcon` | §1.2 | `kStarIcon`, `kStarChromeRefreshOldIcon`, `kBookmarkChromeRefreshOldIcon` … |
| `bookmark` | lucide `bookmark` (filled) | `koan::kKoanBookmarkIcon` | §1.2 | `kStarFilledIcon`, `kStarActiveChromeRefreshOldIcon` |
| `bookmark-star-on-tray` | lucide `book-marked` | `koan::kKoanBookmarkStarOnTrayIcon` | §1.6 | `kBookmarksSidePanelOldIcon`, `kBookmarksSidePanelRefreshOldIcon`, `kBookmarkManagerIcon` … |
| `info` | lucide `info` | `koan::kKoanInfoIcon` | §1.2 | `kInfoIcon`, `kInfoRefreshOldIcon`, `kInfoChromeRefreshOldIcon` … |
| `security` | lucide `lock` | `koan::kKoanSecurityIcon` | §1.2 | `kLockIcon`, `kLockOldIcon`, `kLockFilledIcon` … |
| `security-fill` | lucide `lock` (filled) | `koan::kKoanSecurityFillIcon` | icons.css only | Koan code only |
| `security-broken` | lucide `lock-open` | `koan::kKoanSecurityBrokenIcon` | §1.2 | `kNoEncryptionIcon`, `kNoEncryptionOldIcon`, `kHttpOldIcon` … |
| `security-broken-fill` | lucide `lock-open` (filled) | `koan::kKoanSecurityBrokenFillIcon` | icons.css only | Koan code only |
| `security-warning` | lucide `triangle-alert` | `koan::kKoanSecurityWarningIcon` | icons.css only | `kNotSecureWarningChromeRefreshOldIcon`, `kNotSecureWarningOldIcon` |
| `tracking-protection` | lucide `shield` | `koan::kKoanTrackingProtectionIcon` | icons.css only | `kShieldIcon`, `kGuardianIcon`, `kGuardianOldIcon` … |
| `tracking-protection-fill` | lucide `shield` (filled) | `koan::kKoanTrackingProtectionFillIcon` | icons.css only | Koan code only |
| `extension` | lucide `puzzle` | `koan::kKoanExtensionIcon` | §1.2, §1.3, §1.6 | `kExtensionChromeRefreshOldIcon`, `kExtensionIcon`, `kExtensionOldIcon` … |
| `extension-fill` | lucide `puzzle` (filled) | `koan::kKoanExtensionFillIcon` | icons.css only | Koan code only |
| `extension-blocked` | lucide `puzzle` | `koan::kKoanExtensionBlockedIcon` | icons.css only | Koan code only |
| `page-portrait` | lucide `file` | `koan::kKoanPagePortraitIcon` | §1.2 | `kPageChromeRefreshOldIcon`, `kDescriptionIcon`, `kDescriptionOldIcon` … |
| `link` | lucide `link` | `koan::kKoanLinkIcon` | §1.2 | `kLinkIcon`, `kLinkOldIcon`, `kLinkChromeRefreshOldIcon` |
| `share` | lucide `share` | `koan::kKoanShareIcon` | §1.2, §1.6 | `kShareIcon`, `kShareChromeRefreshOldIcon`, `kShareLinuxChromeRefreshOldIcon` … |
| `reader-mode` | lucide `book-open` | `koan::kKoanReaderModeIcon` | §1.2 | Koan code only |
| `permissions` | lucide `settings-2` | `koan::kKoanPermissionsIcon` | §1.2 | `kTuneIcon`, `kSecurePageInfoChromeRefreshOldIcon` |
| `permissions-fill` | lucide `settings-2` | `koan::kKoanPermissionsFillIcon` | §1.2 | Koan code only |
| `tab` | lucide `app-window` | `koan::kKoanTabIcon` | §1.2 | `kTabIcon`, `kTabOldIcon` |
| `new-tab-image` | lucide `square-plus` | `koan::kKoanNewTabImageIcon` | §1.3, §1.6 | Koan code only |
| `window` | lucide `app-window` | `koan::kKoanWindowIcon` | §0, §1.6, §1.11 | `kNewWindowIcon`, `kNewWindowOldIcon` |
| `private-window` | lucide `venetian-mask` | `koan::kKoanPrivateWindowIcon` | §1.6 | `kIncognitoIcon`, `kIncognitoOldIcon`, `kIncognitoRefreshMenuOldIcon` … |
| `private-window-small` | lucide `venetian-mask` | `koan::kKoanPrivateWindowSmallIcon` | icons.css only | Koan code only |
| `privateBrowsing` | lucide `venetian-mask` | `koan::kKoanPrivatebrowsingIcon` | icons.css only | Koan code only |
| `forget` | lucide `flame` | `koan::kKoanForgetIcon` | icons.css only | Koan code only |
| `passwords` | lucide `key-round` | `koan::kKoanPasswordsIcon` | §1.6 | `kPasswordIcon`, `kPasswordManagerIcon`, `kPasswordManagerOldIcon` … |
| `print` | lucide `printer` | `koan::kKoanPrintIcon` | §1.6 | `kPrintIcon`, `kPrintOldIcon`, `kPrinterOldIcon` … |
| `save` | lucide `file-down` | `koan::kKoanSaveIcon` | §1.6 | `kFileSaveIcon`, `kFileSaveChromeRefreshOldIcon`, `kFileSaveOldIcon` … |
| `fullscreen` | lucide `maximize` | `koan::kKoanFullscreenIcon` | §1.6, §1.7 | `kFullscreenIcon`, `kFullscreenOldIcon`, `kFullscreenRefreshOldIcon` |
| `fullscreen-exit` | lucide `minimize` | `koan::kKoanFullscreenExitIcon` | §1.6 | Koan code only |
| `zoom-out` | lucide `zoom-out` | `koan::kKoanZoomOutIcon` | §1.6 | `kZoomOutIcon`, `kZoomMinusChromeRefreshOldIcon`, `kZoomMinusMenuRefreshOldIcon` … |
| `help` | lucide `circle-question-mark` | `koan::kKoanHelpIcon` | §1.6 | `kHelpIcon`, `kHelpOldIcon`, `kHelpOutlineOldIcon` … |
| `split` | lucide `columns-2` | `koan::kKoanSplitIcon` | §1.7 | `kSplitSceneIcon`, `kSplitSceneOldIcon` |
| `duplicate-tab` | lucide `copy` | `koan::kKoanDuplicateTabIcon` | §1.6 | Koan code only |
| `edit-copy` | lucide `copy` | `koan::kKoanEditCopyIcon` | icons.css only | `kContentCopyIcon`, `kContentCopyOldIcon`, `kCopyOldIcon` … |
| `edit-cut` | lucide `scissors` | `koan::kKoanEditCutIcon` | icons.css only | `kContentCutIcon`, `kCutMenuOldIcon` |
| `edit-paste` | lucide `clipboard-paste` | `koan::kKoanEditPasteIcon` | icons.css only | `kContentPasteIcon`, `kContentPasteOldIcon`, `kPasteMenuOldIcon` |
| `edit-delete` | lucide `delete` | `koan::kKoanEditDeleteIcon` | icons.css only | Koan code only |
| `trash` | lucide `trash-2` | `koan::kKoanTrashIcon` | §1.10 | `kDeleteIcon`, `kDeleteOldIcon`, `kTrashCanRefreshOldIcon` … |
| `u-turn-to-left` | lucide `undo-2` | `koan::kKoanUTurnToLeftIcon` | §1.10 | `kUndoIcon` |
| `unpin` | lucide `minus` | `koan::kKoanUnpinIcon` | §1.3, §1.4, §1.12 | `kKeepOffIcon`, `kKeepOffOldIcon`, `kUnpinOldIcon` … |
| `pin` | lucide `pin` | `koan::kKoanPinIcon` | icons.css only | `kKeepIcon`, `kKeepOldIcon`, `kKeepFilledIcon` … |
| `essential-add` | lucide `star` | `koan::kKoanEssentialAddIcon` | §1.4 | Koan code only |
| `essential-remove` | lucide `star-off` | `koan::kKoanEssentialRemoveIcon` | §1.4 | Koan code only |
| `folder` | lucide `folder` | `koan::kKoanFolderIcon` | §1.3 | `kFolderChromeRefreshOldIcon`, `kFolderOldIcon`, `kFolderFilledIcon` … |
| `open` | lucide `square-arrow-out-up-right` | `koan::kKoanOpenIcon` | icons.css only | `kOpenInNewIcon`, `kOpenInNewOldIcon`, `kOpenInNewChromeRefreshOldIcon` … |
| `drag-indicator` | lucide `grip-vertical` | `koan::kKoanDragIndicatorIcon` | §1.3, §1.5, §1.7 | `kDragGeneralSelectionCustomIcon` |
| `send-to-device` | lucide `monitor-smartphone` | `koan::kKoanSendToDeviceIcon` | §1.6 | Koan code only |
| `screenshot` | lucide `scan` | `koan::kKoanScreenshotIcon` | §1.6 | `kScreenshotRegionIcon`, `kSharingHubScreenshotOldIcon` |
| `camera` | lucide `camera` | `koan::kKoanCameraIcon` | icons.css only | Koan code only |
| `camera-fill` | lucide `camera` | `koan::kKoanCameraFillIcon` | icons.css only | Koan code only |
| `camera-blocked` | lucide `camera-off` | `koan::kKoanCameraBlockedIcon` | icons.css only | Koan code only |
| `microphone` | lucide `mic` | `koan::kKoanMicrophoneIcon` | icons.css only | `kMicIcon`, `kMicOldIcon`, `kMicChromeRefreshOldIcon` … |
| `microphone-fill` | lucide `mic` (filled) | `koan::kKoanMicrophoneFillIcon` | icons.css only | Koan code only |
| `microphone-blocked` | lucide `mic-off` | `koan::kKoanMicrophoneBlockedIcon` | icons.css only | `kMicOffIcon`, `kMicOffChromeRefreshOldIcon` |
| `microphone-blocked-fill` | lucide `mic-off` | `koan::kKoanMicrophoneBlockedFillIcon` | icons.css only | Koan code only |
| `video-fill` | lucide `video` | `koan::kKoanVideoFillIcon` | icons.css only | Koan code only |
| `video-blocked-fill` | lucide `video-off` | `koan::kKoanVideoBlockedFillIcon` | icons.css only | Koan code only |
| `geo` | lucide `map-pin` | `koan::kKoanGeoIcon` | icons.css only | `kLocationOnIcon`, `kLocationOnOldIcon`, `kLocationOnChromeRefreshOldIcon` |
| `geo-fill` | lucide `map-pin` (filled) | `koan::kKoanGeoFillIcon` | icons.css only | Koan code only |
| `geo-blocked` | lucide `map-pin-off` | `koan::kKoanGeoBlockedIcon` | icons.css only | `kLocationOffIcon`, `kLocationOffChromeRefreshOldIcon` |
| `desktop-notification` | lucide `bell` | `koan::kKoanDesktopNotificationIcon` | icons.css only | `kNotificationsIcon`, `kNotificationsOldIcon`, `kNotificationsChromeRefreshOldIcon` … |
| `desktop-notification-fill` | lucide `bell` (filled) | `koan::kKoanDesktopNotificationFillIcon` | icons.css only | Koan code only |
| `desktop-notification-blocked` | lucide `bell-off` | `koan::kKoanDesktopNotificationBlockedIcon` | icons.css only | `kNotificationsOffIcon`, `kNotificationsOffChromeRefreshOldIcon` |
| `popup` | lucide `app-window` | `koan::kKoanPopupIcon` | icons.css only | Koan code only |
| `popup-fill` | lucide `app-window` | `koan::kKoanPopupFillIcon` | icons.css only | Koan code only |
| `autoplay-media` | lucide `circle-play` | `koan::kKoanAutoplayMediaIcon` | icons.css only | Koan code only |
| `autoplay-media-fill` | lucide `circle-play` | `koan::kKoanAutoplayMediaFillIcon` | icons.css only | Koan code only |
| `autoplay-media-blocked` | lucide `circle-pause` | `koan::kKoanAutoplayMediaBlockedIcon` | icons.css only | Koan code only |
| `canvas` | lucide `frame` | `koan::kKoanCanvasIcon` | icons.css only | Koan code only |
| `canvas-blocked` | lucide `frame` | `koan::kKoanCanvasBlockedIcon` | icons.css only | Koan code only |
| `midi` | lucide `piano` | `koan::kKoanMidiIcon` | icons.css only | `kPianoIcon`, `kMidiOldIcon`, `kMidiChromeRefreshOldIcon` |
| `persistent-storage` | lucide `database` | `koan::kKoanPersistentStorageIcon` | icons.css only | `kDatabaseIcon`, `kDatabaseOldIcon` |
| `persistent-storage-fill` | lucide `database` | `koan::kKoanPersistentStorageFillIcon` | icons.css only | Koan code only |
| `persistent-storage-blocked` | lucide `database` | `koan::kKoanPersistentStorageBlockedIcon` | icons.css only | Koan code only |
| `xr` | lucide `glasses` | `koan::kKoanXrIcon` | icons.css only | Koan code only |
| `xr-fill` | lucide `glasses` | `koan::kKoanXrFillIcon` | icons.css only | Koan code only |
| `xr-blocked` | lucide `glasses` | `koan::kKoanXrBlockedIcon` | icons.css only | Koan code only |
| `screen` | lucide `monitor` | `koan::kKoanScreenIcon` | §1.13 | `kScreenShareIcon`, `kScreenShareOldIcon`, `kSelectWindowIcon` … |
| `screen-blocked` | lucide `monitor-off` | `koan::kKoanScreenBlockedIcon` | icons.css only | Koan code only |
| `cookies-fill` | lucide `cookie` | `koan::kKoanCookiesFillIcon` | icons.css only | `kCookieIcon`, `kCookieChromeRefreshOldIcon` |
| `developer` | lucide `code-xml` | `koan::kKoanDeveloperIcon` | icons.css only | Koan code only |
| `tool-profiler` | lucide `gauge` | `koan::kKoanToolProfilerIcon` | icons.css only | Koan code only |
| `translations` | lucide `languages` | `koan::kKoanTranslationsIcon` | icons.css only | `kTranslateIcon`, `kTranslateOldIcon`, `kGTranslateIcon` |
| `spell-check` | lucide `spell-check` | `koan::kKoanSpellCheckIcon` | icons.css only | Koan code only |
| `signature` | lucide `signature` | `koan::kKoanSignatureIcon` | icons.css only | Koan code only |
| `lightbulb` | lucide `lightbulb` | `koan::kKoanLightbulbIcon` | icons.css only | Koan code only |
| `mail` | lucide `mail` | `koan::kKoanMailIcon` | icons.css only | Koan code only |
| `home` | lucide `house` | `koan::kKoanHomeIcon` | icons.css only | `kHomeIcon`, `kHomeOldIcon`, `kNavigateHomeChromeRefreshOldIcon` … |
| `manage` | lucide `wrench` | `koan::kKoanManageIcon` | icons.css only | Koan code only |
| `customize` | lucide `paintbrush` | `koan::kKoanCustomizeIcon` | §0, §1.3 | Koan code only |
| `media-play` | lucide `play` (filled) | `koan::kKoanMediaPlayIcon` | icons.css only | `kPlayArrowIcon`, `kPlayArrowFilledIcon`, `kPlayArrowOldIcon` … |
| `media-pause` | lucide `pause` (filled) | `koan::kKoanMediaPauseIcon` | icons.css only | `kPauseIcon`, `kPauseFilledIcon`, `kPauseOldIcon` … |
| `media-next` | lucide `skip-forward` (filled) | `koan::kKoanMediaNextIcon` | icons.css only | `kSkipNextIcon`, `kSkipNextFilledIcon`, `kSkipNextOldIcon` … |
| `media-previous` | lucide `skip-back` (filled) | `koan::kKoanMediaPreviousIcon` | icons.css only | `kSkipPreviousIcon`, `kSkipPreviousFilledIcon`, `kSkipPreviousOldIcon` … |
| `media-mute` | lucide `volume-x` | `koan::kKoanMediaMuteIcon` | icons.css only | `kVolumeOffIcon` |
| `media-unmute` | lucide `volume-2` | `koan::kKoanMediaUnmuteIcon` | icons.css only | `kVolumeUpIcon` |
| `tab-audio-playing-small` | lucide `volume-2` | `koan::kKoanTabAudioPlayingSmallIcon` | §1.3 | `kSpeakerIcon`, `kSpeakerOldIcon` |
| `tab-audio-muted-small` | lucide `volume-x` | `koan::kKoanTabAudioMutedSmallIcon` | §1.3 | Koan code only |
| `tab-audio-blocked-small` | lucide `volume-off` | `koan::kKoanTabAudioBlockedSmallIcon` | §1.3 | Koan code only |
| `face-sun` | lucide `sun` | `koan::kKoanFaceSunIcon` | §1.12 | Koan code only |
| `moon-stars` | lucide `moon-star` | `koan::kKoanMoonStarsIcon` | §1.12 | Koan code only |
| `sparkles` | lucide `sparkles` | `koan::kKoanSparklesIcon` | §1.12 | Koan code only |
| `algorithm` | lucide `shuffle` | `koan::kKoanAlgorithmIcon` | §1.12 | Koan code only |
| `paintbrush` | lucide `paintbrush` | `koan::kKoanPaintbrushIcon` | icons.css only | Koan code only |
| `paintbrush-fill` | lucide `paintbrush` | `koan::kKoanPaintbrushFillIcon` | §1.10 | Koan code only |
| `palette` | lucide `palette` | `koan::kKoanPaletteIcon` | icons.css only | Koan code only |
| `heart-circle-fill` | lucide `heart` (filled) | `koan::kKoanHeartCircleFillIcon` | §1.10 | Koan code only |
| `wand-sparkle` | lucide `wand-sparkles` | `koan::kKoanWandSparkleIcon` | icons.css only | Koan code only |
| `square-wand-sparkle` | lucide `wand-sparkles` | `koan::kKoanSquareWandSparkleIcon` | icons.css only | Koan code only |
| `edit-theme` | lucide `paintbrush` | `koan::kKoanEditThemeIcon` | icons.css only | Koan code only |
| `boost` | lucide `zap` | `koan::kKoanBoostIcon` | §1.13 | Koan code only |
| `bolt` | lucide `zap` | `koan::kKoanBoltIcon` | §1.13 | Koan code only |
| `eyedropper` | lucide `pipette` | `koan::kKoanEyedropperIcon` | §1.13 | Koan code only |
| `hammer` | lucide `hammer` | `koan::kKoanHammerIcon` | §1.13 | Koan code only |
| `brackets-curly` | lucide `braces` | `koan::kKoanBracketsCurlyIcon` | §1.13 | Koan code only |
| `dice` | lucide `dices` | `koan::kKoanDiceIcon` | §1.13 | Koan code only |
| `block` | lucide `ban` | `koan::kKoanBlockIcon` | §1.13 | Koan code only |
| `blocked-element` | lucide `ban` | `koan::kKoanBlockedElementIcon` | icons.css only | Koan code only |
| `text-lowercase` | lucide `case-lower` | `koan::kKoanTextLowercaseIcon` | icons.css only | Koan code only |
| `text-uppercase` | lucide `case-upper` | `koan::kKoanTextUppercaseIcon` | icons.css only | Koan code only |
| `text-title-case` | lucide `case-sensitive` | `koan::kKoanTextTitleCaseIcon` | icons.css only | Koan code only |
| `text-size` | lucide `a-large-small` | `koan::kKoanTextSizeIcon` | icons.css only | Koan code only |
| `circle-bars-filter` | lucide `list-filter` | `koan::kKoanCircleBarsFilterIcon` | §1.10 | Koan code only |
| `move-cursor` | lucide `move` | `koan::kKoanMoveCursorIcon` | icons.css only | Koan code only |
| `container-tab` | lucide `box` | `koan::kKoanContainerTabIcon` | icons.css only | Koan code only |
| `library` | zen `library` | `koan::kKoanLibraryIcon` | §0 | Koan code only |
| `video` | lucide `video` | `koan::kKoanVideoIcon` | icons.css only | `kVideocamIcon`, `kVideocamOldIcon` |
| `video-blocked` | lucide `video-off` | `koan::kKoanVideoBlockedIcon` | icons.css only | `kVideocamOffIcon` |
| `default-favicon` | lucide `globe` | `koan::kKoanDefaultFaviconIcon` | icons.css only | `kGlobeIcon`, `kGlobeOldIcon` |
