// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_OMNIBOX_DISPOSITION_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_OMNIBOX_DISPOSITION_H_

#include "ui/base/window_open_disposition.h"

namespace koan {

// What the URL bar does with a row it is asked to open in the current tab
// (MaybeAdjustOmniboxDisposition() in koan_url_bar.h). An open tab's row
// (`is_open_tab_match`, its "Switch to Tab") switches to its tab wherever the
// bar was opened from, as Zen's rows always do, unless that tab is gone
// (`open_tab_exists` false): then it is like any other row. Any other row
// opens a new tab while the bar is open in place of a new tab
// (`new_tab_session`: Ctrl+T), and navigates the current tab otherwise.
// Other dispositions (a modifier was held) stay as they are.
inline WindowOpenDisposition AdjustedOmniboxDisposition(
    WindowOpenDisposition disposition,
    bool is_open_tab_match,
    bool open_tab_exists,
    bool new_tab_session) {
  if (disposition != WindowOpenDisposition::kCurrentTab) {
    return disposition;
  }
  if (is_open_tab_match && open_tab_exists) {
    return WindowOpenDisposition::kSwitchToTab;
  }
  return new_tab_session ? WindowOpenDisposition::kNewForegroundTab
                         : disposition;
}

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_OMNIBOX_DISPOSITION_H_
