// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_COLOR_KOAN_COLOR_ID_H_
#define KOAN_BROWSER_UI_COLOR_KOAN_COLOR_ID_H_

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "ui/color/color_id.h"

namespace koan {

// Colours of Koan's own views, after Chrome's. Defined per look: in the shadcn
// style from shadcn's tokens, in koan_shadcn_color_mixer.cc; in the Zen style
// (the default) with Zen's values, in koan_color_mixer.cc. A view only
// names the id, so a restyle changes it in one place.
enum KoanColorIds : ui::ColorId {
  kKoanColorsStart = kChromeColorsEnd,

  // Sidebar tab rows (koan_sidebar_tabs.cc): the selected tab's background
  // and a hovered tab's.
  kColorKoanSidebarTabSelected = kKoanColorsStart,
  kColorKoanSidebarTabHovered,
  // The toolbar and tab text (Zen's --toolbox-textcolor, derived from the
  // theme's primary colour): the text of the sidebar's rows. kColorToolbarText
  // is the same colour in the Zen look.
  kColorKoanToolboxText,
  // The New Tab row's background
  // while hovered and pressed, and its icon and label.
  kColorKoanSidebarRowHovered,
  kColorKoanSidebarRowPressed,
  kColorKoanSidebarNewTabForeground,
  // The separator above the tabs and its Clear button, shown while the
  // pointer is over the sidebar and over the button itself.
  kColorKoanSidebarSeparator,
  kColorKoanSidebarClearForeground,
  kColorKoanSidebarClearForegroundHovered,

  // A pinned tab away from its pinned URL (koan_pinned_tab_views.cc): the
  // slash after its icon, the reset button's background while hovered and
  // pressed, and the "Back to pinned url" sublabel.
  kColorKoanPinnedTabSlash,
  kColorKoanPinnedTabResetHovered,
  kColorKoanPinnedTabResetPressed,
  kColorKoanPinnedTabSublabel,

  // Essentials tiles (koan_essentials.cc).
  kColorKoanEssentialsTile,
  kColorKoanEssentialsTileHovered,
  kColorKoanEssentialsTileSelected,

  // The URL bar (koan_sidebar_nav_view.cc, koan_url_bar_controller.cc): the
  // sidebar's URL pill and the floating URL bar's shadow.
  kColorKoanUrlPill,
  kColorKoanFloatingUrlBarShadow,
  // The floating URL bar's card (and the location bar's fill): opaque for now;
  // a glass pane makes its alpha < 1 and blurs behind it. Its 1px outline and
  // the divider under the field.
  kColorKoanFloatingUrlBarBackground,
  // The same as a glass pane: Zen's acrylic (zen.theme.acrylic-urlbar) fill,
  // to use with a background blur (kFloatingUrlBarBlurRadius).
  kColorKoanFloatingUrlBarGlass,
  kColorKoanFloatingUrlBarBorder,
  // The card's input row: the location bar's focus ring (none in Zen), the
  // divider under it and the "Search..." placeholder.
  kColorKoanFloatingUrlBarFocusRing,
  kColorKoanFloatingUrlBarDivider,
  kColorKoanFloatingUrlBarPlaceholder,
  // The title of a suggestion row (Zen: white at 70%).
  kColorKoanOmniboxRowTitle,

  // Toasts: a vertical gradient, the text, and the background of the
  // shortcut in it.
  kColorKoanToastBackgroundTop,
  kColorKoanToastBackgroundBottom,
  kColorKoanToastForeground,
  kColorKoanToastKeyBackground,
  // The toast's icon button and its icon.
  kColorKoanToastButton,
  kColorKoanToastButtonIcon,

  // Glance (koan_glance_controller.cc): the window background laid over the
  // page behind the card, the card's shadow, and the buttons beside it
  // (normal, and the close button asking to confirm).
  kColorKoanGlanceBackdrop,
  kColorKoanGlanceCardShadow,
  kColorKoanGlanceButton,
  kColorKoanGlanceButtonIcon,
  kColorKoanGlanceButtonConfirm,
  kColorKoanGlanceButtonConfirmIcon,

  // Spaces (koan_space_*.cc): the header's text and its hover background, a
  // bottom bar button's hover background, the dot of a Space without an
  // icon, the Create-a-Space form's fields (and the Edit Theme button's
  // hover), its description and the dashed box of the icon, and an emoji's
  // hover background in the emoji picker.
  kColorKoanSpaceForeground,
  kColorKoanSpaceHeaderHovered,
  kColorKoanSpaceButtonHovered,
  kColorKoanSpaceDot,
  kColorKoanSpaceFormField,
  kColorKoanSpaceFormFieldHovered,
  kColorKoanSpaceFormDescription,
  kColorKoanSpaceIconBorder,
  kColorKoanEmojiHovered,
  // The Library (koan_library_*.cc): its surface and the side column's
  // highlight; text, secondary text (subtitles, headers) and dim text (the
  // search icon); pills (search box, Filter, chips) normal, hovered and
  // active; a hovered row; side tabs hovered and selected; the section
  // icons' accent and selected fill, and a download's progress track.
  kColorKoanLibraryBackground,
  kColorKoanLibrarySideHighlight,
  kColorKoanLibraryForeground,
  kColorKoanLibraryForegroundSecondary,
  kColorKoanLibraryForegroundDim,
  kColorKoanLibraryPill,
  kColorKoanLibraryPillHovered,
  kColorKoanLibraryChipActive,
  kColorKoanLibraryRowHovered,
  kColorKoanLibraryTabHovered,
  kColorKoanLibraryTabSelected,
  kColorKoanLibraryIcon,
  kColorKoanLibraryIconFill,
  kColorKoanLibraryProgressTrack,
  // Spaces: a card (tinted with the space's colour on top) and its outline,
  // an empty icon slot, and the add button with its "+".
  kColorKoanLibrarySpaceCard,
  kColorKoanLibrarySpaceCardBorder,
  kColorKoanLibrarySpaceIconEmpty,
  kColorKoanLibraryAddSpace,
  kColorKoanLibraryAddSpaceHovered,
  kColorKoanLibraryAddSpaceForeground,
  // A Space theme's colour, as its card in the Library is tinted with (one
  // per tab group colour, the colours a Space's theme can take).
  kColorKoanLibrarySpaceTintGrey,
  kColorKoanLibrarySpaceTintBlue,
  kColorKoanLibrarySpaceTintRed,
  kColorKoanLibrarySpaceTintYellow,
  kColorKoanLibrarySpaceTintGreen,
  kColorKoanLibrarySpaceTintPink,
  kColorKoanLibrarySpaceTintPurple,
  kColorKoanLibrarySpaceTintCyan,
  kColorKoanLibrarySpaceTintOrange,

  // Media cards in the sidebar (koan_media_controller.cc): the card, its
  // shadow and outline, and the progress bar's track and played part.
  kColorKoanMediaCard,
  kColorKoanMediaCardShadow,
  kColorKoanMediaCardOutline,
  kColorKoanMediaProgressTrack,
  kColorKoanMediaProgress,

  // The download animation (koan_download_animation.cc): the flying icon's
  // ring, circle, arrow and shadow; the downloads tile, its icon and shadow.
  kColorKoanDownloadArcRing,
  kColorKoanDownloadArcCircle,
  kColorKoanDownloadArcIcon,
  kColorKoanDownloadArcShadow,
  kColorKoanDownloadTile,
  kColorKoanDownloadTileIcon,
  kColorKoanDownloadTileShadow,

  // The welcome screen (koan_welcome.cc): the backdrop's gradient, the
  // title and the start button; the pages' card (sidebar, content, the
  // surfaces of options and panels, tiles, the mock window), borders,
  // shadows, text, radio circles and the accent buttons.
  kColorKoanWelcomeBackdropTop,
  kColorKoanWelcomeBackdropBottom,
  kColorKoanWelcomeTitle,
  kColorKoanWelcomeStartButton,
  kColorKoanWelcomeStartButtonHovered,
  kColorKoanWelcomeStartButtonIcon,
  kColorKoanWelcomeSidebar,
  kColorKoanWelcomeContent,
  kColorKoanWelcomeSurface,
  kColorKoanWelcomeTile,
  kColorKoanWelcomeMockWindow,
  kColorKoanWelcomeBorder,
  kColorKoanWelcomeShadow,
  kColorKoanWelcomeText,
  kColorKoanWelcomeTextSecondary,
  kColorKoanWelcomeRadio,
  kColorKoanWelcomeAccent,
  kColorKoanWelcomeAccentHovered,
  kColorKoanWelcomeOnAccent,
  // The scrim over the window behind the screen, the fill of an option and
  // the hover of a ghost or outline button.
  kColorKoanWelcomeScrim,
  kColorKoanWelcomeOptionFill,
  kColorKoanWelcomeHover,

  kKoanColorsEnd,
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_COLOR_KOAN_COLOR_ID_H_
