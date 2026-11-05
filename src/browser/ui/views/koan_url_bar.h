// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_URL_BAR_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_URL_BAR_H_

#include <string>

#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"

class BrowserWindowInterface;

namespace views {
class View;
}

namespace koan {

// While the URL bar is open in place of a new tab (Ctrl+T or the sidebar's
// new-tab button with Zen's zen.urlbar.replace-newtab), whatever the user
// opens from it goes to a new foreground tab: turns the kCurrentTab
// disposition into kNewForegroundTab. Called from
// ChromeOmniboxClient::OnAutocompleteAccept().
void MaybeAdjustOmniboxDisposition(BrowserWindowInterface* browser,
                                   WindowOpenDisposition* disposition);

// Zen's zen.urlbar.replace-newtab for the New Tab command (IDC_NEW_TAB: ⌘T
// on macOS, which runs as a reserved command before any accelerator, and the
// menu bar's File > New Tab): opens the URL bar in place of a new tab, or
// closes the one open for a new tab. Returns false if the command should open
// a tab as usual. Called from BrowserCommandController.
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
// (Chromium's, synced) says. Called from BrowserCommandController, which then
// ignores the command; the menu item is hidden by the macOS menu validation.
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
// `user_input_in_progress` is whether the user is typing. With Zen's
// "floating-on-type" URL bar behaviour, the first keystroke lifts the bar
// from the sidebar's URL pill to the floating card. Called from
// LocationBarView::OnChanged().
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

// Whether `view` (the location bar, or a row of its omnibox popup) is in a
// window whose floating URL bar is Zen's card: only then do the suggestion
// rows take Zen's styling (koan_omnibox_rows.h). False for any other omnibox
// popup, in either look.
bool ShowsZenCardRows(const views::View* view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_URL_BAR_H_
