// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_QUICK_WINDOW_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_QUICK_WINDOW_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/tab_groups/tab_group_id.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/mojom/menu_source_type.mojom-shared.h"
#include "ui/base/page_transition_types.h"
#include "ui/menus/simple_menu_model.h"

class BrowserView;
class BrowserWindowInterface;
class GURL;
class Profile;

namespace gfx {
class Point;
}  // namespace gfx

namespace views {
class FocusManager;
class MenuRunner;
class View;
class Widget;
}  // namespace views

namespace koan {

class KoanWorkspaceController;

// Arc's Little Arc (#472): a quick window is a small popup-type window
// (BrowserWindowInterface::TYPE_POPUP) with one page and no sidebar, kept
// out of session restore (BrowserWindowCreateParams::
// omit_from_session_restore) and, being no normal window, out of Spaces,
// window sync and the Library. Upstream's popup sends the new tabs its page
// opens to the most recent normal window (GetBrowserWindowForDisposition in
// browser_navigator.cc).
//
// Its look is one row drawn by Koan, over the page as a card: the frame
// draws no title bar and (Linux) no caption buttons (OwnsWindowTopRow() in
// koan_browser_view_hooks.h); upstream's popup toolbar (the read-only address
// with its security chip) is the row, in the window's colour, and the window's
// caption (IsCaptionAt()); at its end Zen's Glance buttons
// (koan_glance_controller.cc) for Move to Space and, but on macOS, where the
// traffic lights at the row's start close it, Close. The page is the content
// card below it (DrawsContentCard()). Keys: Esc (close) and Accel+O (Arc's
// "Open in", to the most recent Space); Accel+W closes it as upstream's Close
// Tab.
//
// Links from other apps open in one when the user chose it
// (prefs::kExternalLinksOpenIn, TakeQuickWindowLinks() in
// koan_quick_window.h); New Quick Window (Cmd+Option+N on macOS,
// Ctrl+Alt+Shift+N elsewhere) opens the floating URL bar for one
// (OpenEmptyQuickWindow()). The design is in
// .agent/notes/upstream/quick-window.md.

// The height of a quick window's top row (its toolbar), which the macOS
// traffic lights are centred in (GetTrafficLightsTitlebarHeight()).
int QuickWindowRowHeight(const BrowserView* browser_view);

// Whether `browser_view`'s window is a quick window.
bool IsQuickWindow(const BrowserView* browser_view);
bool IsQuickWindow(BrowserWindowInterface* browser);
// KoanBrowserViewController::CreateFor(), before the window's controllers:
// marks the window if OpenQuickWindow() is creating it.
void MarkQuickWindowIfRequested(BrowserView* browser_view);

// The window a quick window of `profile` comes back to: the most recently
// active normal window (on the current workspace), or null.
BrowserWindowInterface* QuickWindowHome(Profile* profile);

// The profile (#318, a Serialize()d user container, empty for the default
// partition) a link from another app for `url` takes in `spaces`' window:
// the one of the Space it would have gone to, the default route for external
// links or else the most recent Space, so the user's logins there apply.
std::string QuickWindowContainer(const KoanWorkspaceController* spaces,
                                 const GURL& url);

// How OpenQuickWindow() loads its page.
struct QuickWindowLoad {
  ui::PageTransition transition = ui::PAGE_TRANSITION_AUTO_TOPLEVEL;
  // An opaque initiator for a link from an untrusted launch (StartupTab::
  // is_untrusted_launch), as upstream's OpenTabsInBrowser().
  bool opaque_initiator = false;
  std::string extra_headers;
  // Linux: the launcher's XDG activation token, so the desktop focuses it.
  std::string startup_id;
  // The launcher asked not to take the focus (--open-in-background).
  bool in_background = false;
};
// Opens `url` in a new quick window of `profile` (not off the record), its
// page in `container`'s partition, on the display for new windows. Returns
// the window, or null if none can be made.
BrowserWindowInterface* OpenQuickWindow(Profile* profile,
                                        const GURL& url,
                                        const std::string& container,
                                        const QuickWindowLoad& load);

// New Quick Window (Arc's Cmd+Option+N): the floating URL bar of `profile`'s
// most recent normal window opens empty, and what the user opens from it
// opens in a quick window (KoanUrlBarController::OpenForQuickWindow()).
// Returns false if there is no window whose bar can open.
bool OpenEmptyQuickWindow(Profile* profile);

// The URL bar's pick from New Quick Window (koan_url_bar.h's
// MaybeOpenOmniboxMatchInGlance()): opens `url` in a quick window, in the
// profile of `browser`'s most recent Space. Returns whether it did.
bool OpenQuickWindowFromUrlBar(BrowserWindowInterface* browser,
                               const GURL& url,
                               ui::PageTransition transition,
                               const std::string& extra_headers);

// One quick window's own UI: the buttons and keys above. Owned by its
// KoanBrowserViewController, made only for quick windows.
class KoanQuickWindowController : public ui::AcceleratorTarget,
                                  public ui::SimpleMenuModel::Delegate {
 public:
  explicit KoanQuickWindowController(BrowserView* browser_view);
  KoanQuickWindowController(const KoanQuickWindowController&) = delete;
  KoanQuickWindowController& operator=(const KoanQuickWindowController&) =
      delete;
  ~KoanQuickWindowController() override;

  void OnAddedToWidget();
  void OnBrowserViewLayout();
  void OnBrowserViewDestroying();

  // Move to Space: the Spaces of the most recent normal window, the most
  // recent first; or, without Spaces there, that window; or a new one.
  void ShowMoveMenu(ui::mojom::MenuSourceType source_type);
  // Moves the page (the same WebContents: no reload, its state kept) to
  // `space` of the window that has it, or to the most recent window's most
  // recent Space for none; shows it there and closes this window.
  void MoveToSpace(std::optional<tab_groups::TabGroupId> space);
  void Close();

  views::Widget* buttons_widget() const { return buttons_.get(); }

  // Whether `point` (in the BrowserView's parent's coordinates, as
  // BrowserView::NonClientHitTest()'s) is on the top row's caption: the row
  // itself or its read-only address, not the security chip, a page action or
  // a button (IsWindowCaptionAt()).
  bool IsCaptionAt(const gfx::Point& point);

  // ui::AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  class RowTargeter;

  void CreateButtons();
  // The top row: the window's colour, and room at its ends for the traffic
  // lights (macOS) and the buttons.
  void StyleRow();

  const raw_ptr<BrowserView> browser_view_;
  bool destroying_ = false;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;
  std::unique_ptr<views::Widget> buttons_;
  std::unique_ptr<RowTargeter> row_targeter_;
  raw_ptr<views::View> move_button_ = nullptr;
  std::vector<base::CallbackListSubscription> button_subscriptions_;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  // The menu's Spaces, by command id (kFirstSpaceCommand + index).
  std::vector<tab_groups::TabGroupId> menu_spaces_;
  base::WeakPtrFactory<KoanQuickWindowController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_QUICK_WINDOW_CONTROLLER_H_
