// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_welcome.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabrestore.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/history/core/browser/history_service.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "components/sessions/core/serialized_user_agent_override.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_welcome_state.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_system_accent.h"
#include "koan/browser/ui/views/koan_traffic_lights.h"
#include "koan/browser/ui/views/koan_vertical_tabs_policy.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/browser/ui/views/koan_zen_window_drag.h"
#include "koan/browser/ui/webui/koan_welcome_host.h"
#include "koan/common/koan_url_constants.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/page_transition_types.h"
#include "ui/display/screen.h"
#include "ui/views/background.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"

#if __has_include("koan/browser/ui/views/koan_theme_controller.h")
#include "koan/browser/ui/views/koan_theme_controller.h"
#define KOAN_WELCOME_HAS_THEME_PICKER 1
#endif

namespace koan {

namespace {

// Koan's Spaces have one colour each (a tab group colour) until #41's
// gradient themes: the colours page sets it on the active Space. With #41,
// whose SpaceTheme is the picker's own, this does nothing (the picker edits
// the Space itself).
template <typename Controller>
void SetSpaceColor(Controller* spaces,
                   const tab_groups::TabGroupId& space,
                   tab_groups::TabGroupColorId color) {
  using Theme = typename Controller::SpaceTheme;
  if constexpr (requires(Theme theme) { theme.color = color; }) {
    Theme theme;
    theme.color = color;
    spaces->SetSpaceTheme(space, theme);
  }
}

// The intro gradient's base colour (koan_welcome.css, #zen-welcome-video).
constexpr SkColor kWelcomeBaseColor = SkColorSetRGB(165, 196, 130);

// Shown at most once per browser session, in the first window.
bool g_shown_this_session = false;

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
  // The window is going: nothing of it to restore.
  Close(/*restore_window=*/false);
  SetPending(false);
}

void KoanWelcome::SetPending(bool pending) {
  if (std::exchange(pending_, pending) != pending) {
    SetWelcomeWindow(pending ? browser_view_->browser() : nullptr);
  }
}

bool KoanWelcome::ShouldShow() const {
  Profile* profile = browser_view_->GetProfile();
  return !g_shown_this_session && features::IsWelcomeEnabled() &&
         browser_view_->GetWidget() && !profile->IsOffTheRecord() &&
         browser_view_->browser()->GetType() ==
             BrowserWindowInterface::TYPE_NORMAL &&
         !profile->GetPrefs()->GetBoolean(prefs::kWelcomeScreenSeen) &&
         first_run::IsChromeFirstRun();
}

void KoanWelcome::MaybeShow() {
  if (!browser_view_->GetWidget() || IsWindowClosing()) {
    SetPending(false);
    return;
  }
  // (Not before: a window closed before this leaves it to the next one.)
  g_shown_this_session = true;
  Show();
}

void KoanWelcome::Show() {
  views::Widget* window = browser_view_->GetWidget();
  // Zen's first window (ZenWelcome.mjs centerWindowOnScreen): min(1200,
  // available) x min(720, available), centred; it keeps that size after.
  const gfx::Rect work_area =
      display::Screen::Get()
          ->GetDisplayNearestWindow(window->GetNativeWindow())
          .work_area();
  gfx::Rect bounds(work_area);
  bounds.ClampToCenteredSize(
      gfx::Size(std::min(kWelcomeWindowWidth, work_area.width()),
                std::min(kWelcomeWindowHeight, work_area.height())));
  if (window->IsMaximized() || window->IsFullscreen()) {
    window->Restore();
  }
  window->SetBounds(bounds);
  LockWindow(true);
#if BUILDFLAG(IS_MAC)
  // No window buttons while it shows, as in Zen (compact mode leaves them so
  // until it closes).
  SetTrafficLightsShown(browser_view_, false);
#endif

  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  params.parent = window->GetNativeView();
  params.name = "KoanWelcome";
  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(params));

  auto web_view = std::make_unique<views::WebView>(browser_view_->GetProfile());
  content::WebContents* contents = web_view->GetWebContents();
  // Until the page paints: the base of its intro's gradient (koan_welcome.css
  // #zen-welcome-video), not a white frame.
  web_view->SetBackground(views::CreateSolidBackground(kWelcomeBaseColor));
  contents->SetPageBaseBackgroundColor(kWelcomeBaseColor);
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
  callbacks.window_drag = bind(&KoanWelcome::StartWindowDrag);
  callbacks.window_drag_end = bind(&KoanWelcome::EndWindowDrag);
  callbacks.system_accent =
      base::BindRepeating([] { return GetSystemAccentColor(); });
#if defined(KOAN_WELCOME_HAS_THEME_PICKER)
  callbacks.theme_picker_size = base::BindRepeating(
      [](base::WeakPtr<KoanWelcome> welcome) -> std::optional<gfx::Size> {
        KoanThemeController* theme =
            welcome ? welcome->ThemeController() : nullptr;
        // An empty size: no picker here (Space themes off); the page shows
        // its own colours.
        const gfx::Size size = theme ? theme->ThemePickerSize() : gfx::Size();
        return size.IsEmpty() ? std::nullopt : std::optional<gfx::Size>(size);
      },
      weak_factory_.GetWeakPtr());
  callbacks.show_theme_picker = base::BindRepeating(
      [](base::WeakPtr<KoanWelcome> welcome, const gfx::Rect& anchor) {
        KoanThemeController* theme =
            welcome ? welcome->ThemeController() : nullptr;
        if (!theme || !welcome->web_view_) {
          return;
        }
        // The page's CSS pixels are the WebView's DIPs (the page isn't
        // zoomed).
        gfx::Rect in_screen = anchor;
        in_screen.Offset(
            welcome->web_view_->GetBoundsInScreen().OffsetFromOrigin());
        welcome->theme_picker_open_ = true;
        theme->ShowThemePickerForWelcome(in_screen);
      },
      weak_factory_.GetWeakPtr());
  callbacks.close_theme_picker = bind(&KoanWelcome::CloseThemePicker);
#endif
  callbacks.space_color = base::BindRepeating(
      [](base::WeakPtr<KoanWelcome> welcome, int color_id) {
        KoanBrowserViewController* controller =
            welcome ? KoanBrowserViewController::From(welcome->browser_view_)
                    : nullptr;
        KoanWorkspaceController* spaces =
            controller ? controller->workspaces() : nullptr;
        const auto color = static_cast<tab_groups::TabGroupColorId>(color_id);
        if (!spaces || !spaces->IsEnabled() ||
            !tab_groups::GetTabGroupColorLabelMap().contains(color)) {
          return;
        }
        // Zen's picker edits the active Space live (the window behind).
        if (const std::optional<tab_groups::TabGroupId> space =
                spaces->GetActiveSpace()) {
          SetSpaceColor(spaces, *space, color);
        }
      },
      weak_factory_.GetWeakPtr());
  KoanWelcomeHost::CreateForWebContents(contents, std::move(callbacks));
  // A crashed page would leave the window locked under a dead screen.
  Observe(contents);
  web_view->LoadInitialURL(GURL(kWelcomeURL));
  web_view_ = widget_->SetContentsView(std::move(web_view));
  LayoutWidget();
  widget_->Show();
  widget_->LayoutRootViewIfNecessary();
  // The keyboard goes to the page (its start button has the focus).
  web_view_->RequestFocus();
  contents->Focus();
  // Seen once it is actually on screen (not merely decided on); a crash in
  // the middle doesn't bring it back, as in Zen.
  if (widget_->IsVisible()) {
    browser_view_->GetProfile()->GetPrefs()->SetBoolean(
        prefs::kWelcomeScreenSeen, true);
  }
}

void KoanWelcome::LockWindow(bool locked) {
  // Zen sets the window's min and max size to its size (lockWindowSize);
  // Views' equivalent is a window that can't be resized or zoomed.
  if (locked == window_locked_) {
    return;
  }
  window_locked_ = locked;
  if (locked) {
    could_resize_ = browser_view_->WidgetDelegate::CanResize();
    could_maximize_ = browser_view_->WidgetDelegate::CanMaximize();
    browser_view_->SetCanResize(false);
    browser_view_->SetCanMaximize(false);
  } else {
    browser_view_->SetCanResize(could_resize_);
    browser_view_->SetCanMaximize(could_maximize_);
  }
}

void KoanWelcome::Finish(std::vector<GURL> essentials) {
  if (!widget_ || std::exchange(finishing_, true)) {
    return;
  }
  // Not from inside the page's own message: closing destroys it.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<KoanWelcome> welcome, std::vector<GURL> essentials) {
            if (!welcome) {
              return;
            }
            const bool import = welcome->import_;
            // ⌘W or ⌘Q during the page's fade: nothing to restore or add to
            // a window that is going.
            const bool closing = welcome->IsWindowClosing();
            welcome->Close(/*restore_window=*/!closing);
            if (closing) {
              return;
            }
            welcome->AddEssentials(essentials);
            welcome->FocusBrowser();
            if (import) {
              chrome::ExecuteCommand(welcome->browser_view_->browser(),
                                     IDC_IMPORT_SETTINGS);
            }
          },
          weak_factory_.GetWeakPtr(), std::move(essentials)));
}

bool KoanWelcome::IsWindowClosing() const {
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  return !controller || controller->vertical_tabs()->IsClosing();
}

void KoanWelcome::AddEssentials(const std::vector<GURL>& urls) {
  // Zen's #pinEssentials: each app a typed visit in history (so the URL bar
  // suggests it), a background tab that is not loaded until it is selected
  // (Zen's lazy browser; here the way tab restore adds one), and an Essential.
  BrowserWindowInterface* browser = browser_view_->browser();
  Profile* profile = browser_view_->GetProfile();
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  KoanPinnedTabController* pinned =
      controller ? controller->pinned_tabs() : nullptr;
  TabStripModel* tab_strip = browser->GetTabStripModel();
  for (const GURL& url : urls) {
    const WelcomeEssential* essential = FindWelcomeEssential(url);
    if (!essential) {
      continue;
    }
    if (history) {
      history->AddPage(url, base::Time::Now(), /*context_id=*/0,
                       /*nav_entry_id=*/0, GURL(), history::RedirectList(),
                       ui::PAGE_TRANSITION_TYPED, history::SOURCE_BROWSED,
                       history::VisitResponseCodeCategory::kNot404,
                       /*did_replace_entry=*/false);
    }
    sessions::SerializedNavigationEntry entry;
    entry.set_index(0);
    entry.set_virtual_url(url);
    entry.set_title(essential->name);
    entry.set_transition_type(ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    entry.set_timestamp(base::Time::Now());
    const bool as_essential = pinned && pinned->CanAddToEssentials();
    content::WebContents* contents = chrome::AddRestoredTab(
        browser, base::span_from_ref(entry), tab_strip->count(),
        /*selected_navigation=*/0, /*extension_app_id=*/std::string(),
        /*group=*/std::nullopt, /*select=*/false, /*pin=*/!as_essential,
        base::TimeTicks(), base::Time(), /*storage_namespace=*/nullptr,
        sessions::SerializedUserAgentOverride(), /*extra_data=*/{},
        /*from_session_restore=*/true, /*is_active_browser=*/std::nullopt);
    if (tabs::TabInterface* tab =
            contents ? tabs::TabInterface::GetFromContents(contents) : nullptr;
        tab && as_essential) {
      pinned->AddToEssentials(tab);
    }
  }
}

void KoanWelcome::Skip() {
  // From the page's own key handling: close once it has returned.
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

void KoanWelcome::StartWindowDrag() {
  views::Widget* window = browser_view_->GetWidget();
  if (!window || !widget_) {
    return;
  }
#if BUILDFLAG(IS_MAC)
  // AppKit moves the window while the button is down, as for a page dragged
  // from its top (koan_window_drag.cc): edge tiling and the drag to another
  // Space work, and nothing of Chromium's move loop (whose release CHECKs a
  // capture taken in between would trip). It does nothing if the button is
  // up already.
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

void KoanWelcome::CloseThemePicker() {
  if (!std::exchange(theme_picker_open_, false)) {
    return;
  }
#if defined(KOAN_WELCOME_HAS_THEME_PICKER)
  if (KoanThemeController* theme = ThemeController()) {
    theme->CloseThemePickerForWelcome();
  }
#endif
}

#if defined(KOAN_WELCOME_HAS_THEME_PICKER)
KoanThemeController* KoanWelcome::ThemeController() const {
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  return controller ? controller->theme() : nullptr;
}
#endif

void KoanWelcome::Close(bool restore_window) {
  if (!widget_) {
    return;
  }
  SetPending(false);
  // The theme picker the colours page opened goes with the screen (with the
  // window, the theme controller closes it).
  if (restore_window) {
    CloseThemePicker();
  }
  theme_picker_open_ = false;
  Observe(nullptr);
  web_view_ = nullptr;
  if (!widget_->IsClosed()) {
    widget_->CloseNow();
  }
  widget_.reset();
  if (!restore_window) {
    window_locked_ = false;
    return;
  }
  LockWindow(false);
#if BUILDFLAG(IS_MAC)
  // The traffic lights as compact mode wants them now.
  if (KoanBrowserViewController* controller =
          KoanBrowserViewController::From(browser_view_)) {
    controller->compact_mode()->UpdateCaptionButtons();
  }
#endif
}

void KoanWelcome::PrimaryMainFrameRenderProcessGone(
    base::TerminationStatus status) {
  // The page crashed or was killed: end the welcome (it was seen) rather than
  // leave a dead page over a locked window without its buttons.
  Skip();
}

void KoanWelcome::LayoutWidget() {
  if (!widget_) {
    return;
  }
  // All of the window: the traffic lights are hidden while it shows.
  widget_->SetBounds(
      browser_view_->ConvertRectToWidget(browser_view_->GetLocalBounds()));
}

}  // namespace koan
