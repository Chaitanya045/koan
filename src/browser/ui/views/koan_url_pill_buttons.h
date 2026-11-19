// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_URL_PILL_BUTTONS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_URL_PILL_BUTTONS_H_

namespace koan {

// Zen's rules for the buttons at the trailing end of the sidebar's URL pill
// (zen-omnibox.css, icons.css): the copy-URL button (#zen-copy-url-button,
// a .urlbar-page-action) and the Extensions button (#zen-site-data-icon-button,
// an .identity-box-button). Pure, for koan_views_unittests.
struct UrlPillButtonsState {
  // The pill shows a page, not an empty tab (Zen's zen-has-empty-tab).
  bool has_page = false;
  // The URL bar edits over the pill (Zen's #urlbar[breakout-extend]).
  bool editing = false;
  // zen.urlbar.single-toolbar-show-copy-url (koan.urlbar.show_copy_url).
  bool show_copy_url = false;
  // The pointer is over the sidebar (#navigator-toolbox[zen-has-implicit-
  // hover]) or the pill.
  bool sidebar_hovered = false;
  bool pill_hovered = false;
  // One of the buttons has the keyboard focus (Koan: Tab still reaches them).
  bool button_focused = false;
  // The Extensions button's panel is open (Zen's [open]).
  bool menu_open = false;
};

struct UrlPillButtons {
  // Whether each button takes its place in the pill.
  bool copy_url = false;
  bool extensions = false;
  // Whether they are faded in (opacity 1), else they keep their place at
  // opacity 0.
  bool revealed = false;
};

inline UrlPillButtons GetUrlPillButtons(const UrlPillButtonsState& state) {
  UrlPillButtons buttons;
  // Neither on an empty tab nor while the URL bar edits (Zen: display none
  // for zen-has-empty-tab and breakout-extend).
  const bool page_shown = state.has_page && !state.editing;
  buttons.copy_url = page_shown && state.show_copy_url;
  buttons.extensions = page_shown;
  // Faded in while the sidebar is hovered, as Zen's page actions and
  // identity buttons, and while the panel is open or a button is focused.
  buttons.revealed = state.sidebar_hovered || state.pill_hovered ||
                     state.button_focused || state.menu_open;
  return buttons;
}

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_URL_PILL_BUTTONS_H_
