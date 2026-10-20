// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_URL_BAR_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_URL_BAR_H_

#include "ui/base/window_open_disposition.h"

class BrowserWindowInterface;

namespace views {
class View;
}

namespace koan {

// While the URL bar is open in place of a new tab (Ctrl+T or the sidebar's
// new-tab button with Zen's zen.urlbar.replace-newtab), whatever the user
// opens from it goes to a new foreground tab: turns the CURRENT_TAB
// disposition into NEW_FOREGROUND_TAB. Called from
// ChromeOmniboxClient::OnAutocompleteAccept().
void MaybeAdjustOmniboxDisposition(BrowserWindowInterface* browser,
                                   WindowOpenDisposition* disposition);

// The omnibox text of `location_bar` (a LocationBarView) changed;
// `user_input_in_progress` is whether the user is typing. With Zen's
// "floating-on-type" URL bar behaviour, the first keystroke lifts the bar
// from the sidebar's URL pill to the floating card. Called from
// LocationBarView::OnChanged().
void OnOmniboxChanged(views::View* location_bar, bool user_input_in_progress);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_URL_BAR_H_
