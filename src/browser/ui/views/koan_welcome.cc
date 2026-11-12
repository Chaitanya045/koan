// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_welcome.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_welcome_state.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/color/koan_tokens.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_vertical_tabs_policy.h"
#include "koan/browser/ui/views/koan_zen_window_drag.h"
#include "koan/browser/ui/webui/koan_welcome_host.h"
#include "koan/common/koan_url_constants.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/display/screen.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/background.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// How long the page may take to load and ask for its data before the welcome
// gives up (a hung or broken page would otherwise sit over the window).
constexpr base::TimeDelta kLoadTimeout = base::Seconds(10);

// Shown at most once per browser session, in the first window.
bool g_shown_this_session = false;

// The page's `--background` (shadcn's look, koan_welcome_ui.cc), so the
// WebView's first frame isn't white.
SkColor PageBackground() {
  look::Look look = look::CurrentLook();
  look.style = look::Style::kShadcn;
  const bool dark =
      look.mode == look::Mode::kDark ||
      (look.mode == look::Mode::kAuto &&
       ui::NativeTheme::GetInstanceForNativeUi()->preferred_color_scheme() ==
           ui::NativeTheme::PreferredColorScheme::kDark);
  return look::ResolveTokens(look, dark)[look::Token::kBackground];
}

ui::Accelerator EscapeAccelerator() {
  return ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE);
}

}  // namespace

KoanWelcome::KoanWelcome(BrowserView* browser_view)
    : browser_view_(browser_view) {}

KoanWelcome::~KoanWelcome() = default;

void KoanWelcome::OnAddedToWidget() {
  if (!ShouldShow()) {
    return;
  }
  // Pending from now: Chromium's search engine choice dialog, which the
  // startup tab's first load could open, waits for it.
  SetPending(true);
  // After the window has been laid out and shown.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&KoanWelcome::MaybeShow, weak_factory_.GetWeakPtr()));
}

void KoanWelcome::OnBrowserViewLayout() {
  LayoutWebView();
}

void KoanWelcome::OnBrowserViewDestroying() {
  // The window takes the page along.
  Close(/*remove_page=*/false);
  SetPending(false);
}

void KoanWelcome::SetPending(bool pending) {
  if (std::exchange(pending_, pending) == pending) {
    return;
  }
  if (pending) {
    SetWelcomeWindow(browser_view_->browser());
  } else {
    ClearWelcomeWindow(browser_view_->browser());
  }
}

bool KoanWelcome::ShouldShow() const {
  Profile* profile = browser_view_->GetProfile();
  // (Not while another window has it pending: the first window gets it.)
  return !g_shown_this_session && !IsWelcomePending() &&
         features::IsWelcomeEnabled() && browser_view_->GetWidget() &&
         !profile->IsOffTheRecord() &&
         browser_view_->browser()->GetType() ==
             BrowserWindowInterface::TYPE_NORMAL &&
         !profile->GetPrefs()->GetBoolean(prefs::kWelcomeScreenSeen) &&
         first_run::IsChromeFirstRun();
}

void KoanWelcome::MaybeShow() {
  if (g_shown_this_session || !browser_view_->GetWidget() ||
      IsWindowClosing()) {
    SetPending(false);
    return;
  }
  // Set only now that it shows. If the window that had it pending closed
  // before this, a window opened after the close can still take it; one
  // opened while it was pending skipped it (ShouldShow) and never gets it,
  // and the next launch is no longer a first run, so it is lost. That needs
  // two first-run windows within one task of each other.
  g_shown_this_session = true;
  Show();
}

void KoanWelcome::Show() {
  auto web_view = std::make_unique<views::WebView>(browser_view_->GetProfile());
  content::WebContents* contents = web_view->GetWebContents();
  // Until the page paints: its background, not a white frame.
  const SkColor background = PageBackground();
  web_view->SetBackground(views::CreateSolidBackground(background));
  contents->SetPageBaseBackgroundColor(background);

  const auto bind = [this](auto method) {
    return base::BindRepeating(method, weak_factory_.GetWeakPtr());
  };
  KoanWelcomeHost::Callbacks callbacks;
  callbacks.import = base::BindRepeating(
      [](base::WeakPtr<KoanWelcome> welcome, bool import) {
        if (welcome) {
          welcome->import_ = import;
        }
      },
      weak_factory_.GetWeakPtr());
  callbacks.finish = bind(&KoanWelcome::Finish);
  callbacks.skip = bind(&KoanWelcome::Skip);
  callbacks.page_ready = base::BindRepeating(
      [](base::WeakPtr<KoanWelcome> welcome) {
        if (welcome) {
          welcome->load_timer_.Stop();
        }
      },
      weak_factory_.GetWeakPtr());
  callbacks.window_drag = bind(&KoanWelcome::StartWindowDrag);
  callbacks.window_drag_end = bind(&KoanWelcome::EndWindowDrag);
  KoanWelcomeHost::CreateForWebContents(contents, std::move(callbacks));
  // A crashed or failed page would leave a dead screen over the window.
  Observe(contents);
  web_view->LoadInitialURL(GURL(kWelcomeURL));
  load_timer_.Start(
      FROM_HERE, kLoadTimeout,
      base::BindOnce(&KoanWelcome::Skip, weak_factory_.GetWeakPtr()));

  // In the window itself, never a child widget. On macOS a child widget is a
  // second NSWindow over the browser window, and its page can't have both
  // the window's keyboard and its clicks: shown inactive (as AppKit shows
  // child windows), the keyboard stayed with the browser window (c174d: no
  // Esc, no Tab); made key (45910310's Activate()), it was key while the
  // browser window stayed main, and the page got no click, hover or key at
  // all (c174e/c174f). In the window, the window's own key and mouse events
  // go to the page like any tab's.
  //
  // The last child, over everything else: macOS stacks the window's native
  // views (the tab's page, this page) in the views' order
  // (NativeWidgetMacNSWindowHost::ReorderChildViews), the traffic lights
  // above them all. It stays the last child (KeepOnTop()), and its area is
  // client area, not the window's caption (IsWindowCoveredAt()).
  web_view_ = browser_view_->AddChildView(std::move(web_view));
  browser_view_observation_.Observe(browser_view_);
  LayoutWebView();
  // The keyboard goes to the page, and stays there while it shows
  // (OnDidChangeFocus()); Esc anywhere else in the window still leaves.
  FocusPage();
  focus_manager_ = browser_view_->GetFocusManager();
  if (focus_manager_) {
    focus_manager_->RegisterAccelerator(
        EscapeAccelerator(), ui::AcceleratorManager::kNormalPriority, this);
    focus_observation_.Observe(focus_manager_);
  }
  // Seen once it is on screen (not merely decided on); a crash in the middle
  // doesn't bring it back.
  browser_view_->GetProfile()->GetPrefs()->SetBoolean(prefs::kWelcomeScreenSeen,
                                                      true);
}

void KoanWelcome::Finish() {
  if (!web_view_ || std::exchange(finishing_, true)) {
    return;
  }
  // Not from inside the page's own message: closing destroys it.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<KoanWelcome> welcome) {
                       // (⌘W or ⌘Q meanwhile: nothing to open in a window
                       // that is going.)
                       if (!welcome || welcome->IsWindowClosing()) {
                         return;
                       }
                       const bool import = welcome->import_;
                       welcome->Close();
                       welcome->FocusBrowser();
                       if (import) {
                         chrome::ExecuteCommand(
                             welcome->browser_view_->browser(),
                             IDC_IMPORT_SETTINGS);
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

void KoanWelcome::Skip() {
  // From the page's own message: close once it has returned.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<KoanWelcome> welcome) {
            // (A renderer gone at quit: the window is going.)
            if (welcome && welcome->web_view_ && !welcome->IsWindowClosing()) {
              welcome->Close();
              welcome->FocusBrowser();
            }
          },
          weak_factory_.GetWeakPtr()));
}

bool KoanWelcome::IsWindowClosing() const {
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  return !controller || controller->vertical_tabs()->IsClosing();
}

void KoanWelcome::FocusBrowser() {
  // The keyboard goes back to the window's page, as after a start; only in
  // the active window (not from behind another app).
  views::Widget* window = browser_view_->GetWidget();
  if (!window || !window->IsActive()) {
    return;
  }
  if (content::WebContents* contents = browser_view_->browser()
                                           ->GetTabStripModel()
                                           ->GetActiveWebContents()) {
    contents->Focus();
  }
}

void KoanWelcome::FocusPage() {
  web_view_->RequestFocus();
  web_view_->GetWebContents()->Focus();
}

void KoanWelcome::OnDidChangeFocus(views::View* focused_before,
                                   views::View* focused_now) {
  if (!OtherPageHasFocus()) {
    return;
  }
  // Not from inside the focus change. Only if another page still has it
  // then (⌘T focuses the new tab's page, then the floating URL bar), and
  // only in the active window (never taking the keyboard from another app).
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<KoanWelcome> welcome) {
                       views::Widget* window =
                           welcome ? welcome->browser_view_->GetWidget()
                                   : nullptr;
                       if (welcome && welcome->OtherPageHasFocus() && window &&
                           window->IsActive() && !welcome->IsWindowClosing()) {
                         welcome->FocusPage();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

bool KoanWelcome::OtherPageHasFocus() const {
  // Only another page (a tab's, under this one): what shows over the page,
  // such as the floating URL bar, keeps the keyboard it takes.
  const views::View* focused =
      focus_manager_ ? focus_manager_->GetFocusedView() : nullptr;
  return web_view_ && focused != web_view_ &&
         views::IsViewClass<views::WebView>(focused);
}

bool KoanWelcome::AcceleratorPressed(const ui::Accelerator& accelerator) {
  if (accelerator != EscapeAccelerator() || !web_view_) {
    return false;
  }
  Skip();
  return true;
}

bool KoanWelcome::CanHandleAccelerators() const {
  return web_view_ != nullptr;
}

bool KoanWelcome::CoversPoint(const gfx::Point& point) const {
  if (!web_view_ || !web_view_->GetVisible() || !browser_view_->parent()) {
    return false;
  }
  gfx::Point in_page(point);
  views::View::ConvertPointToTarget(browser_view_->parent(), web_view_,
                                    &in_page);
  return web_view_->HitTestPoint(in_page);
}

void KoanWelcome::OnChildViewAdded(views::View* observed_view,
                                   views::View* child) {
  if (child != web_view_) {
    KeepOnTop();
  }
}

void KoanWelcome::OnChildViewReordered(views::View* observed_view,
                                       views::View* child) {
  if (child != web_view_) {
    KeepOnTop();
  }
}

void KoanWelcome::KeepOnTop() {
  // (Reordering notifies again, for the page itself: nothing more to do.)
  if (web_view_ && browser_view_->children().back() != web_view_) {
    browser_view_->ReorderChildView(web_view_,
                                    browser_view_->children().size());
  }
}

void KoanWelcome::StartWindowDrag() {
  views::Widget* window = browser_view_->GetWidget();
  if (!window || !web_view_) {
    return;
  }
#if BUILDFLAG(IS_MAC)
  // AppKit moves the window while the button is down, as for a page dragged
  // from its top (koan_window_drag.cc; the page is in the window, so its
  // view gets the release afterwards): edge tiling and the drag to another
  // Space work, and nothing of Chromium's move loop. It does nothing if the
  // button is up already.
  StartNativeWindowMove(browser_view_);
#else
  if (!window->IsMoveLoopSupported()) {
    return;
  }
  const gfx::Vector2d offset = display::Screen::Get()->GetCursorScreenPoint() -
                               window->GetWindowBoundsInScreen().origin();
  // After the page's message has returned.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<views::Widget> window, gfx::Vector2d offset) {
            if (window) {
              window->RunMoveLoop(
                  offset, views::Widget::MoveLoopSource::kMouse,
                  views::Widget::MoveLoopEscapeBehavior::kDontHide);
            }
          },
          window->GetWeakPtr(), offset));
#endif
}

void KoanWelcome::EndWindowDrag() {
#if BUILDFLAG(IS_MAC)
  // The page's view never got the release AppKit's drag took.
  if (browser_view_->GetWidget()) {
    EndNativeWindowMove(browser_view_);
  }
#endif
}

void KoanWelcome::Close(bool remove_page) {
  if (!web_view_) {
    return;
  }
  SetPending(false);
  load_timer_.Stop();
  Observe(nullptr);
  // (Before the page goes: its focus moves on.)
  focus_observation_.Reset();
  if (focus_manager_) {
    focus_manager_->UnregisterAccelerator(EscapeAccelerator(), this);
    focus_manager_ = nullptr;
  }
  browser_view_observation_.Reset();
  views::WebView* web_view = std::exchange(web_view_, nullptr);
  if (remove_page) {
    // Destroys the page (and its KoanWelcomeHost).
    browser_view_->RemoveChildViewT(web_view);
  }
}

void KoanWelcome::PrimaryMainFrameRenderProcessGone(
    base::TerminationStatus status) {
  // The page crashed or was killed: end the welcome (it was seen) rather than
  // leave a dead page over the window.
  Skip();
}

void KoanWelcome::DidFailLoad(content::RenderFrameHost* render_frame_host,
                              const GURL& validated_url,
                              int error_code) {
  // The page itself didn't load: nothing to show, no way out on it.
  if (render_frame_host->IsInPrimaryMainFrame()) {
    Skip();
  }
}

void KoanWelcome::LayoutWebView() {
  if (!web_view_) {
    return;
  }
  // All of the window.
  web_view_->SetBoundsRect(browser_view_->GetLocalBounds());
}

}  // namespace koan
