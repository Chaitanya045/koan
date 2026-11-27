// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_quick_window_controller.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/auto_reset.h"
#include "base/callback_list.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/startup/startup_tab.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/webui_url_constants.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_group.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_quick_window.h"
#include "koan/browser/ui/views/koan_quick_window_rules.h"
#include "koan/browser/ui/views/koan_space_routes.h"
#include "koan/browser/ui/views/koan_space_routing.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_url_bar_controller.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/class_property.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/display/screen.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "url/origin.h"

#if BUILDFLAG(IS_LINUX)
#include "base/nix/xdg_util.h"
#endif

namespace koan {

namespace {

DEFINE_UI_CLASS_PROPERTY_KEY(bool, kKoanQuickWindowKey, false)

// Set while OpenQuickWindow() creates its window (synchronously).
bool g_creating_quick_window = false;

// Zen's Glance buttons (koan_glance_controller.cc), a little smaller to sit
// in the popup's address row.
constexpr int kButtonSize = 28;
constexpr int kButtonIconSize = 14;
constexpr int kButtonGap = 6;
constexpr int kTrailingInset = 8;

#if BUILDFLAG(IS_MAC)
// Room for the traffic lights at the row's start: they end at x = 74 on every
// macOS version, plus the gap upstream keeps after them on macOS 26
// (BrowserFrameViewMac::GetCaptionButtonBounds(), MacTabStripInsets, which
// popups don't get).
constexpr int kRowLeadingInset = 86;
#else
constexpr int kRowLeadingInset = 0;
#endif

constexpr char16_t kMoveLabel[] = u"Move to Space";
#if !BUILDFLAG(IS_MAC)
constexpr char16_t kCloseLabel[] = u"Close (Esc)";
#endif

// Menu commands.
constexpr int kMoveToWindowCommand = 1;
constexpr int kMoveToNewWindowCommand = 2;
constexpr int kFirstSpaceCommand = 100;

KoanBrowserViewController* ControllerOf(BrowserWindowInterface* browser) {
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  return browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
}

KoanWorkspaceController* SpacesOf(BrowserWindowInterface* browser) {
  KoanBrowserViewController* controller = ControllerOf(browser);
  KoanWorkspaceController* spaces =
      controller ? controller->workspaces() : nullptr;
  return spaces && spaces->IsEnabled() ? spaces : nullptr;
}

// The window that has `space`, among `profile`'s normal windows.
BrowserWindowInterface* WindowWithSpace(Profile* profile,
                                        const tab_groups::TabGroupId& space) {
  BrowserWindowInterface* found = nullptr;
  ProfileBrowserCollection::GetForProfile(profile)->ForEach(
      [&](BrowserWindowInterface* browser) {
        if (browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
          return true;
        }
        const KoanWorkspaceController* spaces = SpacesOf(browser);
        if (spaces && spaces->GetSpace(space)) {
          found = browser;
          return false;
        }
        return true;
      },
      BrowserCollection::Order::kActivation);
  return found;
}

bool IsNewTabPage(const GURL& url) {
  return url == GURL(chrome::kChromeUINewTabURL) ||
         url == GURL(chrome::kChromeUINewTabPageURL);
}

// A Glance button (KoanGlanceView::AddButton()): a round disc in Zen's
// Glance colours, no ink drop.
views::ImageButton* AddButton(
    views::View* parent,
    const gfx::VectorIcon& icon,
    const std::u16string& label,
    views::Button::PressedCallback callback,
    std::vector<base::CallbackListSubscription>& subscriptions) {
  views::ImageButton* button =
      parent->AddChildView(views::CreateVectorImageButton(std::move(callback)));
  button->SetTooltipText(label);
  button->GetViewAccessibility().SetName(label);
  button->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
  views::InkDrop::Get(button)->SetMode(views::InkDropHost::InkDropMode::OFF);
  views::SetImageFromVectorIconWithColor(
      button, icon, kButtonIconSize,
      views::IconColors(kColorKoanGlanceButtonIcon, kColorKoanGlanceButtonIcon,
                        kColorKoanGlanceButtonIcon));
  const float radius = views::LayoutProvider::Get()->GetCornerRadiusMetric(
      views::Emphasis::kMaximum, gfx::Size(kButtonSize, kButtonSize));
  const auto update = [](views::ImageButton* button, float radius) {
    const views::Button::ButtonState state = button->GetState();
    const bool hovered = state == views::Button::STATE_HOVERED ||
                         state == views::Button::STATE_PRESSED;
    button->SetBackground(views::CreateRoundedRectBackground(
        hovered ? kColorKoanGlanceButtonHover : kColorKoanGlanceButton,
        radius));
  };
  update(button, radius);
  subscriptions.push_back(button->AddStateChangedCallback(
      base::BindRepeating(update, base::Unretained(button), radius)));
  return button;
}

}  // namespace

int QuickWindowRowHeight(const BrowserView* browser_view) {
  const ToolbarView* toolbar = browser_view ? browser_view->toolbar() : nullptr;
  return toolbar ? toolbar->GetPreferredSize().height() : 0;
}

bool IsQuickWindow(const BrowserView* browser_view) {
  return browser_view && browser_view->GetProperty(kKoanQuickWindowKey);
}

bool IsQuickWindow(BrowserWindowInterface* browser) {
  return browser &&
         IsQuickWindow(BrowserView::GetBrowserViewForBrowser(browser));
}

void MarkQuickWindowIfRequested(BrowserView* browser_view) {
  if (g_creating_quick_window && !browser_view->GetIsNormalType()) {
    browser_view->SetProperty(kKoanQuickWindowKey, true);
    // Before the frame exists: no window icon in the frame either (it would
    // keep room for itself above the row, as an app icon does), like the
    // title (OwnsWindowTopRow()).
    browser_view->SetShowIcon(false);
  }
}

BrowserWindowInterface* QuickWindowHome(Profile* profile) {
  return profile ? ProfileBrowserCollection::GetForProfile(profile)
                       ->FindTabbedBrowser()
                 : nullptr;
}

std::string QuickWindowContainer(const KoanWorkspaceController* spaces,
                                 const GURL& url) {
  if (!spaces) {
    return std::string();
  }
  // No route takes the link here, so this is the default route for
  // external links, if it names a Space of the window.
  std::optional<tab_groups::TabGroupId> space =
      spaces->space_routing()->RouteFor(url, /*from_external=*/true);
  if (!space) {
    space = spaces->GetActiveSpace();
  }
  return space ? spaces->GetSpaceContainer(*space) : std::string();
}

BrowserWindowInterface* OpenQuickWindow(Profile* profile,
                                        const GURL& url,
                                        const std::string& container,
                                        const QuickWindowLoad& load) {
  if (!profile || profile->IsOffTheRecord() ||
      GetBrowserWindowCreationStatusForProfile(*profile) !=
          BrowserWindowInterface::CreationStatus::kOk) {
    return nullptr;
  }
  BrowserWindowCreateParams params(BrowserWindowInterface::TYPE_POPUP, profile,
                                   /*from_user_gesture=*/true);
  // Not restored after a restart (Arc's Little Arc holds nothing lasting).
  params.omit_from_session_restore = true;
  params.initial_bounds = quick_window::QuickWindowBounds(
      display::Screen::Get()->GetDisplayForNewWindows().work_area());
  params.initial_origin_specified =
      BrowserWindowCreateParams::ValueSpecified::kSpecified;
#if BUILDFLAG(IS_LINUX)
  params.startup_id = load.startup_id;
#endif
  BrowserWindowInterface* browser = nullptr;
  {
    base::AutoReset<bool> creating(&g_creating_quick_window, true);
    browser = CreateBrowserWindow(std::move(params));
  }
  if (!browser) {
    return nullptr;
  }
  // The page in the profile of the Space the link would have gone to (#318).
  const std::optional<containers::ContainerId> id =
      containers::SpaceContainerFromString(container);
  content::WebContents::CreateParams create(
      profile,
      id ? containers::CreateSiteInstance(profile, *id, url) : nullptr);
#if defined(USE_AURA)
  // As CreateTargetContents() (browser_navigator.cc).
  if (browser->GetWindow() && browser->GetWindow()->GetNativeWindow()) {
    create.context = browser->GetWindow()->GetNativeWindow();
  }
#endif
  // As Navigate(): the tab (its features and helpers) before the load, which
  // its navigation observers expect.
  auto tab = std::make_unique<tabs::TabModel>(
      content::WebContents::Create(create), browser->GetTabStripModel());
  content::NavigationController::LoadURLParams load_params(url);
  load_params.transition_type = load.transition;
  load_params.extra_headers = load.extra_headers;
  if (load.opaque_initiator) {
    load_params.initiator_origin = url::Origin();
  }
  tab->GetContents()->GetController().LoadURLWithParams(load_params);
  browser->GetTabStripModel()->AppendTab(std::move(tab), /*foreground=*/true);
  // A launcher's --open-in-background (upstream's OpenTabsInBrowser()).
  if (load.in_background) {
    browser->GetWindow()->ShowInactive();
  } else {
    browser->GetWindow()->Show();
  }
  return browser;
}

bool OpenEmptyQuickWindow(Profile* profile) {
  BrowserWindowInterface* home =
      profile ? QuickWindowHome(profile->GetOriginalProfile()) : nullptr;
  KoanBrowserViewController* controller = ControllerOf(home);
  if (!controller) {
    return false;
  }
  // From a quick window, or the menu bar with another window in front.
  home->GetWindow()->Activate();
  return controller->url_bar()->OpenForQuickWindow();
}

bool OpenQuickWindowFromUrlBar(BrowserWindowInterface* browser,
                               const GURL& url,
                               ui::PageTransition transition,
                               const std::string& extra_headers) {
  QuickWindowLoad load;
  load.transition = transition;
  load.extra_headers = extra_headers;
  return OpenQuickWindow(browser->GetProfile(), url,
                         QuickWindowContainer(SpacesOf(browser), url),
                         load) != nullptr;
}

bool TakeQuickWindowLinks(const std::vector<StartupTab>& tabs,
                          const base::CommandLine& command_line,
                          Profile* profile,
                          BrowserWindowInterface*& browser,
                          std::vector<StartupTab>& rest) {
  if (!ScopedExternalLinks::IsActive() || !browser) {
    return false;
  }
  if (!profile) {
    profile = browser->GetProfile();
  }
  // The window to come back to; Linux hands the most recently active one,
  // which may be a quick window.
  BrowserWindowInterface* home =
      IsQuickWindow(browser) ? QuickWindowHome(profile) : browser;
  KoanWorkspaceController* spaces = SpacesOf(home);
  TabStripModel* home_tabs = home ? home->GetTabStripModel() : nullptr;
  content::WebContents* home_active =
      home_tabs ? home_tabs->GetActiveWebContents() : nullptr;

  quick_window::LinkFacts facts;
  facts.quick_window_chosen =
      profile->GetPrefs()->GetString(prefs::kExternalLinksOpenIn) ==
      prefs::kExternalLinksInQuickWindow;
  facts.off_the_record = profile->IsOffTheRecord();
  facts.has_window = home_tabs && !home_tabs->empty();
  facts.window_is_only_new_tab = home_tabs && home_tabs->count() == 1 &&
                                 home_active &&
                                 IsNewTabPage(home_active->GetVisibleURL());

  const std::vector<SpaceRoute> routes =
      ParseSpaceRoutes(profile->GetPrefs()->GetList(prefs::kSpaceRoutes));

  QuickWindowLoad load;
  load.in_background = command_line.HasSwitch(switches::kOpenInBackground);
#if BUILDFLAG(IS_LINUX)
  // Upstream reads it the same way for its own window (and still can).
  load.startup_id =
      base::nix::GetXdgActivationTokenFromCommandLine(command_line)
          .value_or(std::string());
#endif
  bool took = false;
  std::vector<StartupTab> left;
  for (const StartupTab& tab : tabs) {
    facts.web_url =
        tab.type == StartupTab::Type::kNormal && tab.url.SchemeIsHTTPOrHTTPS();
    // Any matching route, also one to "Most recent space" or to a Space the
    // window lacks: the link goes where routing takes it today.
    facts.routed = std::ranges::any_of(routes, [&](const SpaceRoute& route) {
      return IsSpaceRouteMatching(tab.url.spec(), route);
    });
    if (quick_window::OpensInQuickWindow(facts)) {
      load.opaque_initiator = tab.is_untrusted_launch;
      if (OpenQuickWindow(profile, tab.url,
                          QuickWindowContainer(spaces, tab.url), load)) {
        took = true;
        continue;
      }
    }
    left.push_back(tab);
  }
  if (!took && home == browser) {
    return false;
  }
  browser = home;
  rest = std::move(left);
  return true;
}

// KoanQuickWindowController.

KoanQuickWindowController::KoanQuickWindowController(BrowserView* browser_view)
    : browser_view_(browser_view) {}

KoanQuickWindowController::~KoanQuickWindowController() {
  OnBrowserViewDestroying();
}

void KoanQuickWindowController::OnAddedToWidget() {
  if (destroying_ || focus_manager_ || !browser_view_->GetFocusManager()) {
    return;
  }
  focus_manager_ = browser_view_->GetFocusManager();
  // After BrowserView's own: normal priority targets are tried most recently
  // registered first. A page that takes the key (preventDefault) keeps it.
  focus_manager_->RegisterAccelerator(
      ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE),
      ui::AcceleratorManager::kNormalPriority, this);
  focus_manager_->RegisterAccelerator(
      ui::Accelerator(ui::VKEY_O, ui::EF_PLATFORM_ACCELERATOR),
      ui::AcceleratorManager::kNormalPriority, this);
  CreateButtons();
  StyleRow();
  OnBrowserViewLayout();
}

void KoanQuickWindowController::StyleRow() {
  ToolbarView* toolbar = browser_view_->toolbar();
  if (!toolbar || !buttons_) {
    return;
  }
  // The window's colour, as the main window's sidebar around its card; the
  // address takes it too (AdjustLocationBarBackground()).
  toolbar->SetBackground(views::CreateSolidBackground(kColorToolbar));
  // The popup row is a FlexLayout, which keeps to its host's insets: the
  // address ends where the buttons start, so they cover neither a long
  // address nor its page actions, and on macOS it starts after the lights.
  toolbar->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(
      0, kRowLeadingInset, 0,
      buttons_->GetContentsView()->GetPreferredSize().width())));
}

bool KoanQuickWindowController::IsCaptionAt(const gfx::Point& point) {
  ToolbarView* toolbar = browser_view_->toolbar();
  if (destroying_ || !toolbar || !toolbar->GetVisible() ||
      !browser_view_->parent()) {
    return false;
  }
  gfx::Point in_toolbar(point);
  views::View::ConvertPointToTarget(browser_view_->parent(), toolbar,
                                    &in_toolbar);
  if (!toolbar->HitTestPoint(in_toolbar)) {
    return false;
  }
  const views::View* target = toolbar->GetEventHandlerForPoint(in_toolbar);
  if (target == toolbar) {
    return true;
  }
  // The address is read-only in a popup: it drags the window, as a title
  // would. Its security chip and page actions keep their clicks.
  LocationBarView* location_bar = toolbar->location_bar_view();
  return location_bar && (target == location_bar ||
                          (location_bar->omnibox_view() &&
                           location_bar->omnibox_view()->Contains(target)));
}

void KoanQuickWindowController::CreateButtons() {
  views::Widget* browser_widget = browser_view_->GetWidget();
  if (buttons_ || !browser_widget) {
    return;
  }
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  params.parent = browser_widget->GetNativeView();
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  params.name = "KoanQuickWindowButtons";
  buttons_ = std::make_unique<views::Widget>();
  buttons_->Init(std::move(params));
  auto row = std::make_unique<views::BoxLayoutView>();
  row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  row->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  row->SetMainAxisAlignment(views::BoxLayout::MainAxisAlignment::kEnd);
  row->SetBetweenChildSpacing(kButtonGap);
  row->SetInsideBorderInsets(
      gfx::Insets::TLBR(0, kButtonGap, 0, kTrailingInset));
  // At the trailing end of the row, in its colour (StyleRow()).
  row->SetBackground(views::CreateSolidBackground(kColorToolbar));
  // Posted: the menu and the move outlive the press that starts them.
  move_button_ = AddButton(
      row.get(), kKoanFullscreenIcon, kMoveLabel,
      base::BindRepeating(
          [](base::WeakPtr<KoanQuickWindowController> controller,
             const ui::Event& event) {
            if (controller) {
              controller->ShowMoveMenu(
                  event.IsKeyEvent() ? ui::mojom::MenuSourceType::kKeyboard
                                     : ui::mojom::MenuSourceType::kMouse);
            }
          },
          weak_factory_.GetWeakPtr()),
      button_subscriptions_);
#if !BUILDFLAG(IS_MAC)
  // The window's only close: the frame shows none of its buttons
  // (OwnsWindowTopRow()). On macOS the red traffic light is the close.
  AddButton(row.get(), vector_icons::kCloseIcon, kCloseLabel,
            base::BindRepeating(&KoanQuickWindowController::Close,
                                weak_factory_.GetWeakPtr()),
            button_subscriptions_);
#endif
  buttons_->SetContentsView(std::move(row));
  buttons_->ShowInactive();
}

void KoanQuickWindowController::OnBrowserViewLayout() {
  if (destroying_ || !buttons_) {
    return;
  }
  ToolbarView* toolbar = browser_view_->toolbar();
  if (!toolbar || !toolbar->GetVisible() || toolbar->height() == 0 ||
      !toolbar->GetWidget()) {
    // The page's own fullscreen hides the row.
    buttons_->Hide();
    return;
  }
  const gfx::Rect row = toolbar->ConvertRectToWidget(toolbar->GetLocalBounds());
  const int width = buttons_->GetContentsView()->GetPreferredSize().width();
  buttons_->SetBounds(
      gfx::Rect(row.right() - width, row.y(), width, row.height()));
  if (!buttons_->IsVisible()) {
    buttons_->ShowInactive();
  }
}

void KoanQuickWindowController::OnBrowserViewDestroying() {
  if (destroying_) {
    return;
  }
  destroying_ = true;
  weak_factory_.InvalidateWeakPtrs();
  menu_runner_.reset();
  menu_model_.reset();
  if (focus_manager_) {
    focus_manager_->UnregisterAccelerators(this);
    focus_manager_ = nullptr;
  }
  move_button_ = nullptr;
  // Before the buttons they watch.
  button_subscriptions_.clear();
  buttons_.reset();
}

void KoanQuickWindowController::ShowMoveMenu(
    ui::mojom::MenuSourceType source_type) {
  if (destroying_ || !buttons_ || !move_button_ ||
      (menu_runner_ && menu_runner_->IsRunning())) {
    return;
  }
  Profile* profile = browser_view_->GetProfile();
  BrowserWindowInterface* home = QuickWindowHome(profile);
  const KoanWorkspaceController* spaces = SpacesOf(home);
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_spaces_.clear();
  if (spaces) {
    menu_model_->AddTitle(kMoveLabel);
    const std::vector<KoanWorkspaceController::SpaceInfo> infos =
        spaces->GetSpaces();
    std::optional<size_t> active;
    for (size_t i = 0; i < infos.size(); ++i) {
      if (infos[i].active) {
        active = i;
      }
    }
    for (size_t index : quick_window::MoveMenuOrder(infos.size(), active)) {
      const KoanWorkspaceController::SpaceInfo& info = infos[index];
      menu_model_->AddItem(
          kFirstSpaceCommand + static_cast<int>(menu_spaces_.size()),
          info.icon.empty() ? info.name
                            : base::StrCat({info.icon, u"  ", info.name}));
      menu_spaces_.push_back(info.id);
    }
  } else if (home) {
    menu_model_->AddItem(kMoveToWindowCommand, u"Move to Window");
  } else {
    menu_model_->AddItem(kMoveToNewWindowCommand, u"Move to New Window");
  }
  menu_runner_ =
      std::make_unique<views::MenuRunner>(menu_model_.get(), /*run_types=*/0);
  gfx::Rect bounds = move_button_->GetLocalBounds();
  views::View::ConvertRectToScreen(move_button_, &bounds);
  menu_runner_->RunMenuAt(browser_view_->GetWidget(), nullptr, bounds,
                          views::MenuAnchorPosition::kTopRight, source_type);
}

void KoanQuickWindowController::ExecuteCommand(int command_id,
                                               int event_flags) {
  std::optional<tab_groups::TabGroupId> space;
  if (command_id == kMoveToNewWindowCommand) {
    // No normal window left: upstream's own popup-to-tab move.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<KoanQuickWindowController> controller) {
                         if (controller && !controller->destroying_) {
                           chrome::ConvertPopupToTabbedBrowser(
                               controller->browser_view_->browser());
                         }
                       },
                       weak_factory_.GetWeakPtr()));
    return;
  }
  if (command_id >= kFirstSpaceCommand &&
      static_cast<size_t>(command_id - kFirstSpaceCommand) <
          menu_spaces_.size()) {
    space = menu_spaces_[command_id - kFirstSpaceCommand];
  }
  // After the menu has gone.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanQuickWindowController::MoveToSpace,
                                weak_factory_.GetWeakPtr(), space));
}

void KoanQuickWindowController::MoveToSpace(
    std::optional<tab_groups::TabGroupId> space) {
  if (destroying_) {
    return;
  }
  Profile* profile = browser_view_->GetProfile();
  BrowserWindowInterface* home =
      space ? WindowWithSpace(profile, *space) : QuickWindowHome(profile);
  TabStripModel* from = browser_view_->browser()->GetTabStripModel();
  if (!home || from->empty()) {
    return;
  }
  KoanWorkspaceController* spaces = SpacesOf(home);
  if (!space && spaces) {
    space = spaces->GetActiveSpace();
  }
  TabStripModel* to = home->GetTabStripModel();
  int index = to->count();
  if (space && spaces && to->group_model()->ContainsTabGroup(*space)) {
    // Shown first, so the tab lands in the Space in front.
    if (spaces->GetActiveSpace() != space) {
      spaces->SwitchTo(*space);
    }
    index = static_cast<int>(
        to->group_model()->GetTabGroup(*space)->ListTabs().end());
  } else {
    space.reset();
  }
  // As upstream's ConvertPopupToTabbedBrowser(): the page itself moves (a
  // TabModel can't move between window types, so its tab is made anew), so
  // it neither reloads nor loses its state. It keeps its partition (#318):
  // in a Space with another profile its colour line shows, as for any moved
  // tab. This window closes once its tab strip is empty.
  std::unique_ptr<content::WebContents> contents =
      from->DetachWebContentsAtForInsertion(from->active_index());
  to->InsertWebContentsAt(index, std::move(contents), AddTabTypes::ADD_ACTIVE,
                          space);
  home->GetWindow()->Show();
  home->GetWindow()->Activate();
}

void KoanQuickWindowController::Close() {
  if (destroying_) {
    return;
  }
  // As its close button.
  browser_view_->browser()->GetWindow()->Close();
}

bool KoanQuickWindowController::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (destroying_) {
    return false;
  }
  if (accelerator.key_code() == ui::VKEY_ESCAPE) {
    // In fullscreen Esc is upstream's, to leave it (BrowserView's own Esc
    // accelerator, ExclusiveAccessManager::HandleUserKeyEvent()).
    if (browser_view_->IsFullscreen()) {
      return false;
    }
    Close();
    return true;
  }
  if (accelerator.key_code() == ui::VKEY_O) {
    // Arc's Cmd+O ("Open in"): to the most recent Space at once.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanQuickWindowController::MoveToSpace,
                                  weak_factory_.GetWeakPtr(), std::nullopt));
    return true;
  }
  return false;
}

bool KoanQuickWindowController::CanHandleAccelerators() const {
  return !destroying_;
}

}  // namespace koan
