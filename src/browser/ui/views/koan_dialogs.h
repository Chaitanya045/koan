// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_DIALOGS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_DIALOGS_H_

namespace views {
class DialogDelegate;
class MessageBoxView;
}  // namespace views

// Zen's layout for a page's alert(), confirm() and prompt(), Firefox's
// commonDialog (commonDialog-css.patch, zen-panels/dialog.css): 30px above
// and below, 26px at the sides, 6px between the rows, at least 32em wide
// and an 18px radius. Called from JavaScriptTabModalDialogViewViews'
// constructor
// (chrome/browser/ui/views/javascript_tab_modal_dialog_view_views.cc).
namespace koan {

// Before the widget exists: the title's and the message's margins, the
// width and, in the Zen look, the radius.
void StyleJavaScriptDialog(views::DialogDelegate& dialog,
                           views::MessageBoxView& message_box);

// Once it is shown: the button row's insets (the client view is made with
// the widget), and the widget resized to fit.
void StyleJavaScriptDialogButtons(views::DialogDelegate& dialog);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_DIALOGS_H_
