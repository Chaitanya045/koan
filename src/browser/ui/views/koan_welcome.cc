// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_welcome.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_welcome_state.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/color/koan_tokens.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_vertical_tabs_policy.h"
#include "koan/browser/ui/webui/koan_welcome_host.h"
#include "koan/common/koan_url_constants.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/background.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

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
  LayoutWidget();
}

void KoanWelcome::OnBrowserViewDestroying() {
  Close();
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
  views::Widget* window = browser_view_->GetWidget();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  params.parent = window->GetNativeView();
  params.name = "KoanWelcome";
  // On macOS a child widget is a child NSWindow, shown inactive: it must
  // become the key window, or the keyboard (Esc, Tab, the arrows) stays with
  // the browser window under it. It covers the whole window, title bar and
  // traffic lights included; Skip setup and Esc are the way out.
  params.activatable = views::Widget::InitParams::Activatable::kYes;
  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(params));

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
  KoanWelcomeHost::CreateForWebContents(contents, std::move(callbacks));
  // A crashed page would leave a dead screen over the window.
  Observe(contents);
  web_view->LoadInitialURL(GURL(kWelcomeURL));
  web_view_ = widget_->SetContentsView(std::move(web_view));
  LayoutWidget();
  widget_->Show();
  widget_->LayoutRootViewIfNecessary();
  // The keyboard goes to the page (its first button has the focus).
  widget_->Activate();
  web_view_->RequestFocus();
  contents->Focus();
  // Seen once it is actually on screen (not merely decided on); a crash in
  // the middle doesn't bring it back.
  if (widget_->IsVisible()) {
    browser_view_->GetProfile()->GetPrefs()->SetBoolean(
        prefs::kWelcomeScreenSeen, true);
  }
}

void KoanWelcome::Finish() {
  if (!widget_ || std::exchange(finishing_, true)) {
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
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<KoanWelcome> welcome) {
                       // (A renderer gone at quit: the window is going.)
                       if (welcome && !welcome->IsWindowClosing()) {
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
  // The keyboard goes back to the window: to its page, as after a start.
  views::Widget* window = browser_view_->GetWidget();
  if (!window) {
    return;
  }
  window->Activate();
  if (content::WebContents* contents = browser_view_->browser()
                                           ->GetTabStripModel()
                                           ->GetActiveWebContents()) {
    contents->Focus();
  }
}

void KoanWelcome::Close() {
  if (!widget_) {
    return;
  }
  SetPending(false);
  Observe(nullptr);
  web_view_ = nullptr;
  if (!widget_->IsClosed()) {
    widget_->CloseNow();
  }
  widget_.reset();
}

void KoanWelcome::PrimaryMainFrameRenderProcessGone(
    base::TerminationStatus status) {
  // The page crashed or was killed: end the welcome (it was seen) rather than
  // leave a dead page over the window.
  Skip();
}

void KoanWelcome::LayoutWidget() {
  if (!widget_) {
    return;
  }
  // All of the window.
  widget_->SetBounds(
      browser_view_->ConvertRectToWidget(browser_view_->GetLocalBounds()));
}

}  // namespace koan
