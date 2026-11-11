// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_URL_BAR_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_URL_BAR_H_

#include <string>

#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"

class BrowserWindowInterface;
class GURL;
class LocationBar;

class GURL;

namespace gfx {
class Rect;
}

namespace views {
class View;
}

namespace koan {

// What the URL bar opens from its rows. An open tab's row (`is_open_tab_match`,
// its "Switch to Tab") switches to the tab, wherever the bar was opened from
// (Zen's rows always do), unless that tab is gone: then, like any other row,
// it opens in the current tab, or in a new one while the URL bar is open in
// place of a new tab (Ctrl+T or the sidebar's new-tab button with Zen's
// zen.urlbar.replace-newtab), or when `destination_url` is on another site
// than the active tab's and that tab is pinned or an Essential (Zen's
// zen.tabs.open-pinned-in-new-tab, koan_pinned_site.h). Turns the kCurrentTab
// disposition into kSwitchToTab or kNewForegroundTab. Called from
// ChromeOmniboxClient::OnAutocompleteAccept().
void MaybeAdjustOmniboxDisposition(BrowserWindowInterface* browser,
                                   bool is_open_tab_match,
                                   const GURL& destination_url,
                                   WindowOpenDisposition* disposition);

// Whether a new tab page must not focus the URL bar by itself, as Chromium
// does: with Zen's URL bar the floating card opens only when the user asks
// (Cmd+L, Cmd+T, a click on the pill). Called from
// BrowserWebContentsDelegate::ShouldFocusLocationBarByDefault().
bool SuppressesDefaultUrlBarFocus(BrowserWindowInterface* browser);

// The Focus Location command (Cmd+L, the action and the legacy command both
// end in chrome::FocusLocationBar()): the floating bar opens for the
// keyboard, also when the bar is already open in the pill. Called at the
// start of chrome::FocusLocationBar().
void OnFocusLocationBarCommand(BrowserWindowInterface* browser);

// While the URL bar is open over a Glance card (⌘L: Zen's Glance is the
// selected tab, so the URL bar edits its address), what the user picks loads
// in the Glance page: returns true after loading `url` there, so the caller
// skips its own navigation. False otherwise (no Glance session, a new-tab
// disposition such as Alt+Enter, or not a web URL, which closes the card and
// goes to the tab). Called from ChromeOmniboxClient::OnAutocompleteAccept(),
// after MaybeAdjustOmniboxDisposition().
bool MaybeOpenOmniboxMatchInGlance(BrowserWindowInterface* browser,
                                   const GURL& url,
                                   ui::PageTransition transition,
                                   WindowOpenDisposition disposition,
                                   const std::string& extra_headers);

// Zen's zen.urlbar.replace-newtab for the New Tab command (IDC_NEW_TAB: ⌘T
// on macOS, which runs as a reserved command before any accelerator, and the
// menu bar's File > New Tab): opens the URL bar in place of a new tab, or
// closes the one open for a new tab. Returns false if the command should open
// a tab as usual. Called from the command policy (koan_command_policy.cc).
bool MaybeOpenUrlBarForNewTab(BrowserWindowInterface* browser);

// macOS: whether the reserved commands (Chromium's list, accelerator_table.cc:
// new / close tab and window, restore and switch tabs; ⌘T, ⌘W, ⌘N ...) stay
// the browser's in `browser`'s fullscreen, as in a window, instead of going
// to the page first. Not the others: ⌘L and ⌃⌘F are not reserved in a window
// either (a page can swallow them there, as in Firefox).
// Chromium does that only while the fullscreen window shows its toolbar
// (autohide, revealed); Koan's native fullscreen always has the toolbar
// hidden, yet its shortcuts must work. The page's own fullscreen (a video)
// keeps Chromium's rule (it may hold the keyboard). False elsewhere. Called
// from BrowserCommandController::IsReservedCommandOrKey().
bool KeepsReservedCommandsInFullscreen(BrowserWindowInterface* browser);

// macOS: whether `browser`'s window has no "Always Show Toolbar in Full
// Screen" (View menu, ⇧⌘F, the toolbar action): Koan's toolbar is hidden and
// fullscreen reveals nothing at the top edge, whatever that setting
// (Chromium's, synced) says. Called from the command policy, which then
// ignores the command; the menu item is hidden by its macOS menu table
// (koan_command_policy.h).
bool IgnoresFullscreenToolbarToggle(BrowserWindowInterface* browser);

// The view Chromium's bubbles anchor to instead of the toolbar's, where the
// toolbar is hidden: the URL pill's site icon in Koan's single toolbar (Zen
// anchors permission prompts, page info and the like to the identity box
// there), else the sidebar header's buttons while the sidebar is hidden.
// Null where the toolbar is there (the anchor is upstream's). Where compact
// mode hides just the toolbar (no pill), this slides it in and holds it for
// as long as a bubble anchored to it is open, and returns null (the anchor is
// upstream's, the revealed toolbar). `view` is the BrowserView or any view in
// its window (the location bar, the toolbar). Called from the hook patches in
// ToolbarView::GetBubbleAnchor(), LocationBarView::GetAnchorOrNull() and
// GetChipAnchor(), bubble_anchor_util::GetPageInfoAnchorConfiguration() and
// DownloadToolbarUIController::OnBubbleAnchorAssembled().
views::View* GetBubbleAnchorView(const views::View* view);

// The omnibox text of `location_bar` (a LocationBarView) changed;
// `user_input_in_progress` is whether the user is typing. The bar is placed
// again: a bar edited in the pill widens over the page as the text grows; a
// pill click never lifts it to the floating card (Zen decides once, when it
// opens). Called from LocationBarView::OnChanged().
void OnOmniboxChanged(views::View* location_bar, bool user_input_in_progress);

// The omnibox of `location_bar` (a LocationBarView) is installing its
// placeholder text: Zen's "Search..." in the floating card, and while the URL
// bar edits in the pill (not Chromium's AI Mode hint) unless a keyword mode
// has its own. Called from OmniboxViewViews::InstallPlaceholderText().
void AdjustOmniboxPlaceholder(views::View* location_bar, std::u16string& text);

// Whether the floating command card is open for `location_bar`: its input
// shows the "Search..." placeholder even while focused (Chromium hides it
// then), in a muted colour, and its focus ring and background pill are flat.
// Called from OmniboxViewViews::ShouldShowPlaceholderText().
bool ShowsCardPlaceholder(views::View* location_bar);

// The colour id of the omnibox's placeholder text: the card's own while the
// floating card is open for `location_bar`. Called from
// OmniboxViewViews::UpdatePlaceholderTextColor().
ui::ColorId AdjustOmniboxPlaceholderColorId(views::View* location_bar,
                                            ui::ColorId id);

// Whether the page action icons (site settings, install, bookmark star, ...)
// and content setting icons of `location_bar` (a LocationBarView) are hidden:
// while it is open as Zen's URL bar, over the sidebar's pill or floating, as
// Zen's has none. Called from LocationBarView::ShouldHidePageActionIcons().
bool HidesPageActionIcons(const views::View* location_bar);

// Z15: Zen's URL bar has no bookmark star, in either layout (#star-button-box
// is hidden; ⌘D and the menus still bookmark). Called from
// BookmarkPageActionController::ShouldShowPageAction().
bool HidesBookmarkStar();

// Z15: Zen's identity box has no text (#identity-icon-label is hidden): no
// "Not secure", "Dangerous" or page name chip by the site icon, in either
// layout. Screen readers still hear the security state (the icon's
// description). Called from LocationIconView::GetText().
bool HidesSecurityChipText();

// Whether `view` (the location bar, or a row of its omnibox popup) is in a
// window whose floating URL bar is Zen's card: only then do the suggestion
// rows take Zen's styling (koan_omnibox_rows.h). False for any other omnibox
// popup, in either look.
bool ShowsZenCardRows(const views::View* view);

// Zen's floating URL bar is one card: the input row with the suggestions under
// it, one surface, one shadow (#urlbar[breakout-extend]). Chromium's popup is
// a frame of its own around the location bar (its own surface, corners,
// shadow and location bar cutout); in Zen's card the card paints all of that
// and grows to hold the suggestions, and the popup only lays out its rows.

// Whether the omnibox popup frame being built for `location_bar` leaves its
// surface, shadow and cutout to Zen's card: only while that window's bar is
// open as Zen's card. Decided once per popup; the card closes a popup opened
// before it floated and asks for its suggestions again. Called from
// RoundedOmniboxResultsFrame's constructor.
bool PaintsOmniboxPopupSurface(LocationBar* location_bar);

// The omnibox popup widget's `bounds` (in screen coordinates) for
// `location_bar` (a LocationBarView) and its suggestions, `results_height`
// tall: for a popup in Zen's card (PaintsOmniboxPopupSurface()), exactly the
// part of the card under its input row, as wide as the card; the card grows
// to match. Unchanged otherwise. Called from
// OmniboxPopupViewViews::GetTargetBounds().
void AdjustOmniboxPopupBounds(views::View* location_bar,
                              int results_height,
                              gfx::Rect& bounds);

// The colour the location bar of `location_bar` (a LocationBarView) paints and
// gives its omnibox as its background: clear, while a glass URL bar card (Zen's
// acrylic-urlbar) is open in its window, over the card's translucent fill;
// else `color`. Called from LocationBarView::RefreshBackground().
SkColor AdjustLocationBarBackground(views::View* location_bar, SkColor color);

// Whether `browser_view`'s floating URL bar is open as a glass card: the
// toolbar and the top container behind it paint nothing, so the fill is the
// card's only colour. Called from koan::OnGlassLayout().
bool IsGlassUrlBarOpen(const views::View* browser_view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_URL_BAR_H_
