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

// The omnibox text of `location_bar` (a LocationBarView) changed;
// `user_input_in_progress` is whether the user is typing. With Zen's
// "floating-on-type" URL bar behaviour, the first keystroke lifts the bar
// from the sidebar's URL pill to the floating card. Called from
// LocationBarView::OnChanged().
void OnOmniboxChanged(views::View* location_bar, bool user_input_in_progress);

// The omnibox of `location_bar` (a LocationBarView) is installing its
// placeholder text: in the floating shadcn card it is "Search...". Called
// from OmniboxViewViews::InstallPlaceholderText().
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

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_URL_BAR_H_
