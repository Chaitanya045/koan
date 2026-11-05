// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/browser/koan_vertical_tabs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include "base/auto_reset.h"
#include "base/check.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/i18n/break_iterator.h"
#include "base/i18n/char_iterator.h"
#include "base/i18n/rtl.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/sessions/exit_type_service.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/tab_group_sync/tab_group_sync_service_factory.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/simple_message_box.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/frame/base_tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/groups/tab_group_editor_bubble_view.h"
#include "chrome/browser/ui/views/tabs/shared/tab_strip_types.h"
#include "chrome/common/webui_url_constants.h"
#include "components/bookmarks/common/bookmark_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/saved_tab_groups/public/tab_group_sync_service.h"
#include "components/saved_tab_groups/public/types.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_emoji_picker.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "koan/browser/ui/views/koan_library.h"
#include "koan/browser/ui/views/koan_sidebar_animation.h"
#include "koan/browser/ui/views/koan_sidebar_tabs.h"
#include "koan/browser/ui/views/koan_space_bar.h"
#include "koan/browser/ui/views/koan_space_creation.h"
#include "koan/browser/ui/views/koan_space_header.h"
#include "koan/browser/ui/views/koan_space_menu.h"
#include "koan/browser/ui/views/koan_space_swipe.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_tab_unload.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "koan/browser/ui/views/koan_zen_toasts.h"
#include "third_party/icu/source/common/unicode/uchar.h"
#include "ui/actions/actions.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/layer_owner.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/events/event.h"
#include "ui/events/event_handler.h"
#include "ui/events/event_target.h"
#include "ui/events/event_utils.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace koan {

namespace {

// zen.workspaces.switch-animation-duration.
constexpr base::TimeDelta kSwitchAnimationDuration = base::Milliseconds(250);

// zen.workspaces wheel switching: one switch per 200 ms.
constexpr base::TimeDelta kWheelSwitchCooldown = base::Milliseconds(200);

// A swipe whose events stop for this long is abandoned: its end may never
// reach the sidebar (it went away, the window lost focus), which would leave
// the Space dragged aside. Fingers resting on the touchpad send nothing, so
// this is generous.
constexpr base::TimeDelta kSwipeIdleTimeout = base::Milliseconds(2000);

// The name Zen gives a Space it creates by itself.
constexpr char16_t kDefaultSpaceName[] = u"Space";

// A Space with Zen's default theme keeps the user's browser colour; Koan
// stores it as the grey tab group colour.
constexpr tab_groups::TabGroupColorId kDefaultThemeColor =
    tab_groups::TabGroupColorId::kGrey;

// Whether `grapheme` (one grapheme cluster) is an emoji: a flag, a
// pictograph shown as an emoji by default, or one made an emoji with the
// variation selector or in a ZWJ sequence. Symbols that are text by default,
// like (c), (R), TM and arrows, start plain names.
bool IsEmoji(const std::u16string& grapheme) {
  if (grapheme.empty()) {
    return false;
  }
  const int32_t first = base::i18n::UTF16CharIterator(grapheme).get();
  if (u_hasBinaryProperty(first, UCHAR_REGIONAL_INDICATOR) ||
      u_hasBinaryProperty(first, UCHAR_EMOJI_PRESENTATION)) {
    return true;
  }
  return u_hasBinaryProperty(first, UCHAR_EXTENDED_PICTOGRAPHIC) &&
         (grapheme.find(u'\uFE0F') != std::u16string::npos ||
          grapheme.find(u'\u200D') != std::u16string::npos);
}

// Seed colours for Space tints, one per tab group colour. Fixed values (not
// looked up from the current theme, which the tint itself changes).
SkColor TintSeed(tab_groups::TabGroupColorId color) {
  switch (color) {
    case tab_groups::TabGroupColorId::kGrey:
      return SkColorSetRGB(0x5F, 0x63, 0x68);
    case tab_groups::TabGroupColorId::kBlue:
      return SkColorSetRGB(0x1A, 0x73, 0xE8);
    case tab_groups::TabGroupColorId::kRed:
      return SkColorSetRGB(0xD9, 0x30, 0x25);
    case tab_groups::TabGroupColorId::kYellow:
      return SkColorSetRGB(0xF9, 0xAB, 0x00);
    case tab_groups::TabGroupColorId::kGreen:
      return SkColorSetRGB(0x1E, 0x8E, 0x3E);
    case tab_groups::TabGroupColorId::kPink:
      return SkColorSetRGB(0xD0, 0x18, 0x84);
    case tab_groups::TabGroupColorId::kPurple:
      return SkColorSetRGB(0x93, 0x34, 0xE6);
    case tab_groups::TabGroupColorId::kCyan:
      return SkColorSetRGB(0x12, 0xB5, 0xCB);
    case tab_groups::TabGroupColorId::kOrange:
      return SkColorSetRGB(0xFA, 0x90, 0x3E);
    case tab_groups::TabGroupColorId::kNumEntries:
      break;
  }
  return SkColorSetRGB(0x1A, 0x73, 0xE8);
}

// The ids of the Spaces deleted (Delete Space) since the browser started, in
// any window: a tab reopened from one doesn't bring it back
// (AdoptResurrectedSpaces()). Group ids are unique, so one set does.
std::set<tab_groups::TabGroupId>& DeletedSpaces() {
  static base::NoDestructor<std::set<tab_groups::TabGroupId>> deleted;
  return *deleted;
}

// Before the tint was per window, Koan made the Space's colour the profile's
// user colour and kept the user's own in prefs. A profile still showing that
// tint gets its own colour back, once.
void MigrateWorkspaceTint(Profile* profile) {
  PrefService* prefs = profile->GetPrefs();
  if (!prefs->HasPrefPath(prefs::kWorkspaceTintApplied)) {
    return;
  }
  const SkColor applied =
      static_cast<SkColor>(prefs->GetInteger(prefs::kWorkspaceTintApplied));
  ThemeService* theme = ThemeServiceFactory::GetForProfile(profile);
  if (theme && applied && theme->GetUserColor() == applied) {
    const SkColor saved = static_cast<SkColor>(
        prefs->GetInteger(prefs::kWorkspaceTintSavedColor));
    const auto variant = static_cast<ui::mojom::BrowserColorVariant>(
        prefs->GetInteger(prefs::kWorkspaceTintSavedVariant));
    if (saved) {
      theme->SetUserColorAndBrowserColorVariant(saved, variant);
    } else {
      theme->SetUserColor(std::nullopt);
      theme->SetBrowserColorVariant(variant);
    }
  }
  prefs->ClearPref(prefs::kWorkspaceTintApplied);
  prefs->ClearPref(prefs::kWorkspaceTintSavedColor);
  prefs->ClearPref(prefs::kWorkspaceTintSavedVariant);
}

// The Spaces of `browser`'s window, if it has them.
KoanWorkspaceController* SpacesOf(const BrowserWindowInterface* browser) {
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  KoanWorkspaceController* spaces =
      controller ? controller->workspaces() : nullptr;
  return spaces && spaces->IsEnabled() ? spaces : nullptr;
}

// Gives `to_clone`'s children (recursively) new layers and puts the old ones
// under `parent`, which keeps showing them as they were.
void RecreateChildLayers(ui::Layer* to_clone, ui::Layer* parent) {
  const std::vector<raw_ptr<ui::Layer, VectorExperimental>> children(
      to_clone->children());
  for (ui::Layer* child : children) {
    ui::LayerOwner* owner = child->owner();
    if (!owner) {
      continue;
    }
    ui::Layer* old_layer = owner->RecreateLayer().release();
    parent->Add(old_layer);
    RecreateChildLayers(owner->layer(), old_layer);
  }
}

// Recreates the layers of `root` and its subtree and returns the old ones,
// which keep showing the old content (see wm::RecreateLayers(), Aura-only).
std::unique_ptr<ui::LayerTreeOwner> RecreateLayers(ui::LayerOwner* root) {
  auto old_layers = std::make_unique<ui::LayerTreeOwner>(root->RecreateLayer());
  RecreateChildLayers(root->layer(), old_layers->root());
  return old_layers;
}

// Shows or hides the Space's tabs and the rows above them (New Tab, Clear).
void SetSpaceRowsVisible(TabStripView* tab_strip, bool visible) {
  tab_strip->unpinned_tabs_scroll_view()->SetVisible(visible);
  for (views::View* child : tab_strip->children()) {
    if (IsSidebarTabRowsView(child)) {
      child->SetVisible(visible);
    }
  }
}

// Zen's mouse wheel over the sidebar (ZenSpaceManager's toolbox "wheel"
// listener): a horizontal wheel, or a vertical one with Ctrl
// (zen.workspaces.scroll-modifier-key), switches Space; on macOS also Zen's
// two-finger swipe. Runs before upstream's swipe controller on the same view.
class SpaceWheelHandler : public ui::EventHandler {
 public:
  explicit SpaceWheelHandler(KoanWorkspaceController* controller)
      : controller_(controller) {}

  // ui::EventHandler:
#if BUILDFLAG(IS_MAC)
  // macOS sends the mouse wheel as scroll events, like touchpad scrolling;
  // only a wheel has ticks. The touchpad's horizontal swipes are
  // KoanSpaceSwipe's (through the controller).
  void OnScrollEvent(ui::ScrollEvent* event) override {
    if (event->type() == ui::EventType::kScroll && event->HasNativeEvent() &&
        !ui::GetMouseWheelTick120ths(event->native_event()).IsZero()) {
      Handle(event->x_offset(), event->y_offset(), event);
      return;
    }
    if (controller_->OnSwipeScroll(*event)) {
      event->SetHandled();
      event->StopPropagation();
    }
  }
#endif

  void OnMouseEvent(ui::MouseEvent* event) override {
#if BUILDFLAG(IS_MAC)
    // Wheels are handled above; what comes here is touchpad scrolling,
    // which Zen leaves alone (only line scrolling switches).
#else
    // A horizontal wheel (or touchpad scroll) is upstream's swipe, which
    // switches Space too (through PickSpaceTab() and WrapsSpaces()); Ctrl
    // with a vertical wheel is Zen's. Not touchpad scrolling.
    if (!event->IsMouseWheelEvent() || !event->IsControlDown() ||
        (event->flags() & ui::EF_PRECISION_SCROLLING_DELTA)) {
      return;
    }
    const ui::MouseWheelEvent* wheel = event->AsMouseWheelEvent();
    Handle(wheel->x_offset(), wheel->y_offset(), event);
#endif
  }

 private:
  void Handle(float x, float y, ui::Event* event) {
    float delta = 0;
    if (x != 0 && y == 0) {
      delta = -x;
    } else if (y != 0 && x == 0 && event->IsControlDown()) {
      delta = -y;
    }
    if (delta == 0) {
      return;
    }
    // Wheel down or right: the next Space (Zen's natural-scroll off).
    controller_->OnSpaceWheel((delta > 0) != base::i18n::IsRTL());
    event->SetHandled();
    event->StopPropagation();
  }

  const raw_ptr<KoanWorkspaceController> controller_;
};

}  // namespace

// Keeps the old Space's layers alive while they slide out.
class SlideOutLayers : public ui::ImplicitAnimationObserver {
 public:
  using DoneCallback = base::OnceCallback<void(SlideOutLayers*)>;

  SlideOutLayers(std::unique_ptr<ui::LayerTreeOwner> layers, DoneCallback done)
      : layers_(std::move(layers)), done_(std::move(done)) {}
  // Deleting animating layers aborts their animations, which would call
  // back here.
  ~SlideOutLayers() override { StopObservingImplicitAnimations(); }

  ui::Layer* root() { return layers_->root(); }

  // ui::ImplicitAnimationObserver:
  void OnImplicitAnimationsCompleted() override {
    // Deletes this.
    std::move(done_).Run(this);
  }

 private:
  std::unique_ptr<ui::LayerTreeOwner> layers_;
  DoneCallback done_;
};

KoanWorkspaceController::CreateMenuEntry::CreateMenuEntry() = default;
KoanWorkspaceController::CreateMenuEntry::CreateMenuEntry(
    const CreateMenuEntry&) = default;
KoanWorkspaceController::CreateMenuEntry&
KoanWorkspaceController::CreateMenuEntry::operator=(const CreateMenuEntry&) =
    default;
KoanWorkspaceController::CreateMenuEntry::~CreateMenuEntry() = default;

KoanWorkspaceController::KoanWorkspaceController(BrowserView* browser_view)
    : browser_view_(browser_view),
      swipe_(std::make_unique<KoanSpaceSwipe>(this)) {
  if (IsEnabled()) {
    tab_strip_model()->AddObserver(this);
    session_restored_subscription_ =
        SessionRestore::RegisterOnSessionRestoredCallback(
            base::BindRepeating(&KoanWorkspaceController::OnSessionRestored,
                                base::Unretained(this)));
  }
  AddDefaultCreateMenuEntries();
}

KoanWorkspaceController::~KoanWorkspaceController() {
  slide_outs_.clear();
}

// static
KoanWorkspaceController* KoanWorkspaceController::FromTabStrip(
    TabStripModel* tab_strip) {
  return SpacesOf(tab_strip && tab_strip->delegate()
                      ? tab_strip->delegate()->GetBrowserWindowInterface()
                      : nullptr);
}

bool KoanWorkspaceController::IsEnabled() const {
  // Like Zen, private windows have no Spaces.
  return features::IsWorkspacesEnabled() &&
         !browser_view_->GetProfile()->IsOffTheRecord() &&
         base::FeatureList::IsEnabled(::features::kTabGroupsFocusing) &&
         browser_view_->GetIsNormalType() &&
         browser_view_->browser()->GetTabStripModel()->SupportsTabGroups();
}

base::WeakPtr<KoanWorkspaceController> KoanWorkspaceController::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

TabStripModel* KoanWorkspaceController::tab_strip_model() const {
  return browser_view_->browser()->GetTabStripModel();
}

void KoanWorkspaceController::OnAddedToWidget() {
  if (!IsEnabled()) {
#if BUILDFLAG(IS_MAC)
    // Only for the menu bar, which each window sets as it becomes key
    // (OnWidgetActivationChanged()).
    widget_observation_.Observe(browser_view_->GetWidget());
#endif
    return;
  }
  InstallViews();
  InstallGroupActions();
  ScheduleUpdate();
  widget_observation_.Observe(browser_view_->GetWidget());
  Profile* profile = browser_view_->GetProfile()->GetOriginalProfile();
  MigrateWorkspaceTint(profile);
  // Spaces aren't saved tab groups (koan::SavesTabGroups()): the bookmarks
  // bar doesn't list Chrome's saved groups (an old profile may have some)
  // unless the user turns that on. (The pref is registered after Koan's
  // prefs, so its default changes here.)
  profile->GetPrefs()->SetDefaultPrefValue(
      bookmarks::prefs::kShowTabGroupsInBookmarkBar, base::Value(false));
  // The window takes its Space's colour from the start (upstream may have
  // given it the group's before Koan was there).
  ApplySpaceColor();
  // The tint can be switched in chrome://koan-settings.
  pref_change_registrar_.Init(profile->GetPrefs());
  pref_change_registrar_.Add(
      prefs::kWorkspaceTint,
      base::BindRepeating(&KoanWorkspaceController::OnTintPrefChanged,
                          base::Unretained(this)));
}

void KoanWorkspaceController::OnTintPrefChanged() {
  ApplySpaceColor();
}

void KoanWorkspaceController::OnWidgetActivationChanged(views::Widget* widget,
                                                        bool active) {
#if BUILDFLAG(IS_MAC)
  // The menu bar is shared: the key window's kind decides Tab > Group
  // Selected Tab's state (SetTabGroupMenuAlternate()).
  if (active) {
    SetTabGroupMenuAlternate(/*alternate=*/!IsEnabled());
  }
#endif
  if (!IsEnabled()) {
    return;
  }
  // The window the user comes to stores its Spaces (StoreSpaces()).
  if (active) {
    stored_spaces_.clear();
    ScheduleUpdate();
  } else {
    // A swipe's end may not reach a window that isn't key.
    CancelSwipe(/*animate=*/true);
  }
}

void KoanWorkspaceController::OnWidgetDestroying(views::Widget* widget) {
  widget_observation_.Reset();
}

void KoanWorkspaceController::OnBrowserViewDestroying() {
  session_restored_subscription_ = base::CallbackListSubscription();
  destroying_ = true;
  CancelSwipe(/*animate=*/false);
  pref_change_registrar_.RemoveAll();
  widget_observation_.Reset();
  slide_outs_.clear();
  switch_layered_views_.clear();
  // The window is closing: the browser's animation controller is gone (the
  // lock's release would collapse the sidebar with an animation), and this
  // runs after KoanCompactModeController's teardown. Same for the menus' holds,
  // which release their locks the same way when they are destroyed below.
  ReleaseExpandOnHoverLockSoon(create_form_lock_);
  if (region_) {
    region_->RemovePreTargetHandler(wheel_handler_.get());
    region_ = nullptr;
  }
  space_menu_.reset();
  create_menu_.reset();
  theme_menu_.reset();
  icon_picker_.reset();
  tab_menu_submenu_.reset();
  view_observations_.RemoveAllObservations();
  // Null the pointers first, so none dangles while the views go.
  const std::array<views::View*, 3> views = {creation_form_.get(),
                                             header_.get(), bar_.get()};
  creation_form_ = nullptr;
  header_ = nullptr;
  bar_ = nullptr;
  for (views::View* view : views) {
    if (view) {
      view->parent()->RemoveChildViewT(view);
    }
  }
  // Unconditional: removing an observer that was never added is a no-op.
  tab_strip_model()->RemoveObserver(this);
}

void KoanWorkspaceController::InstallViews() {
  if (header_) {
    return;
  }
  views::View* region = browser_view_->tab_strip_view();
  if (!region || region->GetClassName() != "VerticalTabStripRegionView") {
    return;
  }
  // The wheel switches Spaces anywhere over the sidebar, like Zen's
  // toolbox. Before upstream's swipe controller (default priority).
  if (region_ != region) {
    if (region_) {
      region_->RemovePreTargetHandler(wheel_handler_.get());
      view_observations_.RemoveObservation(region_);
    }
    region_ = region;
    view_observations_.AddObservation(region_);
    if (!wheel_handler_) {
      wheel_handler_ = std::make_unique<SpaceWheelHandler>(this);
    }
    region_->AddPreTargetHandler(wheel_handler_.get(),
                                 ui::EventTarget::Priority::kSystem);
  }
  // The header goes into the tab strip, between the pinned tabs (Essentials)
  // and the Space's tabs; TabStripViewLayout places it (see
  // PlaceSpaceHeader()).
  auto* tab_strip = views::AsViewClass<TabStripView>(
      static_cast<BaseTabStripRegionView*>(region)->GetTabStripView());
  if (!tab_strip) {
    return;
  }
  header_ =
      tab_strip->AddChildViewAt(std::make_unique<KoanSpaceHeaderView>(this), 1);
  view_observations_.AddObservation(header_);
  // The bottom bar goes at the bottom of the vertical tab strip's content area
  // (a FlexLayoutView whose new-tab container flexes to fill the space, so
  // anything appended after it sits at the bottom of the sidebar).
  for (views::View* child : region->children()) {
    if (child->GetClassName() == "FlexLayoutView") {
      bar_ = child->AddChildView(std::make_unique<KoanSpaceBarView>(this));
      // The Library's button, in the room Zen keeps on the bar's leading
      // side.
      bar_->SetLeadingButton(CreateLibraryButton(browser_view_));
      view_observations_.AddObservation(bar_);
      return;
    }
  }
}

bool KoanWorkspaceController::RunGroupCommand(int command_id) {
  switch (command_id) {
    // ⌘⌃P "New tab group": Zen's Create Space form.
    case IDC_CREATE_NEW_TAB_GROUP:
      ShowCreateForm();
      return true;
    // ⌘⌃W "Close group": Delete Space, which asks first (Zen's).
    case IDC_CLOSE_TAB_GROUP:
      if (std::optional<tab_groups::TabGroupId> active = GetActiveSpace()) {
        DeleteSpace(*active);
      }
      return true;
    // ⌘⌃X / ⌘⌃Z: the next or previous Space, wrapping around.
    case IDC_FOCUS_NEXT_TAB_GROUP:
      Cycle(/*forward=*/true);
      return true;
    case IDC_FOCUS_PREV_TAB_GROUP:
      Cycle(/*forward=*/false);
      return true;
    // ⌘⌃C "New tab in group": a new tab is always in the Space, so Koan's new
    // tab (the URL bar, as ⌘T).
    case IDC_ADD_NEW_TAB_TO_GROUP:
    case IDC_ADD_NEW_TAB_RECENT_GROUP:
      chrome::ExecuteCommand(browser_view_->browser(), IDC_NEW_TAB);
      return true;
    // Tab > Group Tab (and its ⌥ alternate), "Group ungrouped tabs": a tab is
    // always in a Space; "Move To" moves it to another.
    case IDC_WINDOW_GROUP_TAB:
    case IDC_GROUP_TARGET_TAB:
    case IDC_GROUP_UNGROUPED_TABS:
      return true;
    default:
      return false;
  }
}

void KoanWorkspaceController::InstallGroupActions() {
  BrowserActions* actions = BrowserActions::From(browser_view_->browser());
  if (!actions || !actions->root_action_item()) {
    return;
  }
  // The actions are the window's own (BrowserActions), like the new-tab one
  // the URL bar takes (KoanUrlBarController::InstallNewTabAction()). They run
  // Chrome's ⌘⌃ group keys, the Tab menu and the app menu's "Tab groups"
  // (kUseActionsForBrowserCommands); in a Spaces window each does what
  // RunGroupCommand() does for its command, also when it reaches the action
  // while disabled. (Without that feature, the browser command reaches
  // RunGroupCommand() through koan::HandleSpacesCommand().)
  const auto install = [&](actions::ActionId id, std::optional<int> command,
                           bool visible) {
    actions::ActionItem* item = actions::ActionManager::Get().FindAction(
        id, actions->root_action_item());
    if (!item) {
      return;
    }
    item->SetInvokeActionCallback(base::BindRepeating(
        [](base::WeakPtr<KoanWorkspaceController> controller,
           std::optional<int> command, actions::ActionItem* item,
           actions::ActionInvocationContext) {
          if (controller && command) {
            controller->RunGroupCommand(*command);
          }
        },
        weak_factory_.GetWeakPtr(), command));
    // (Not disabled: the command controller sets the enabled state again;
    // the callback is what decides.)
    if (!visible) {
      item->SetVisible(false);
    }
  };
  install(kActionCreateNewTabGroup, IDC_CREATE_NEW_TAB_GROUP, /*visible=*/true);
  install(kActionCloseTabGroup, IDC_CLOSE_TAB_GROUP, /*visible=*/true);
  install(kActionFocusNextTabGroup, IDC_FOCUS_NEXT_TAB_GROUP,
          /*visible=*/true);
  install(kActionFocusPrevTabGroup, IDC_FOCUS_PREV_TAB_GROUP,
          /*visible=*/true);
  install(kActionAddNewTabToGroup, IDC_ADD_NEW_TAB_TO_GROUP, /*visible=*/true);
  install(kActionAddNewTabRecentGroup, IDC_ADD_NEW_TAB_RECENT_GROUP,
          /*visible=*/true);
  install(kActionWindowGroupTab, IDC_WINDOW_GROUP_TAB, /*visible=*/false);
  install(kActionGroupTargetTab, IDC_GROUP_TARGET_TAB, /*visible=*/false);
  install(kActionGroupUngroupedTabs, IDC_GROUP_UNGROUPED_TABS,
          /*visible=*/false);
  // The app menu's saved "Tab groups" (Spaces aren't saved groups).
  install(kActionTabGroupsMenu, std::nullopt, /*visible=*/false);
  install(kActionSavedTabGroupsSubmenu, std::nullopt, /*visible=*/false);
}

// Spaces.

std::pair<std::u16string, std::u16string> KoanWorkspaceController::SplitTitle(
    const std::u16string& raw_title) {
  std::u16string title;
  base::TrimWhitespace(raw_title, base::TRIM_ALL, &title);
  base::i18n::BreakIterator iter(title,
                                 base::i18n::BreakIterator::BREAK_CHARACTER);
  if (title.empty() || !iter.Init() || !iter.Advance()) {
    return {std::u16string(), title};
  }
  std::u16string first(iter.GetString());
  if (!IsEmoji(first)) {
    return {std::u16string(), title};
  }
  std::u16string name;
  base::TrimWhitespace(title.substr(first.size()), base::TRIM_LEADING, &name);
  return {first, name};
}

std::u16string KoanWorkspaceController::JoinTitle(const std::u16string& icon,
                                                  const std::u16string& name) {
  if (icon.empty() || name.empty()) {
    return icon + name;
  }
  return base::StrCat({icon, u" ", name});
}

std::vector<KoanWorkspaceController::SpaceInfo>
KoanWorkspaceController::GetSpaces() const {
  TabStripModel* model = tab_strip_model();
  if (!IsEnabled() || !model->group_model()) {
    return {};
  }
  std::vector<tab_groups::TabGroupId> groups =
      model->group_model()->ListTabGroups();
  // Upstream's ephemeral groups (e.g. a focused tab's temporary group)
  // aren't Spaces.
  std::erase_if(groups, [model](const tab_groups::TabGroupId& id) {
    return model->IsEphemeralTabGroup(id);
  });
  std::ranges::sort(groups, {}, [model](const tab_groups::TabGroupId& id) {
    return model->group_model()->GetTabGroup(id)->ListTabs().start();
  });
  std::vector<SpaceInfo> spaces;
  for (const tab_groups::TabGroupId& id : groups) {
    spaces.push_back(*GetSpace(id));
  }
  return spaces;
}

std::optional<tab_groups::TabGroupId> KoanWorkspaceController::GetActiveSpace()
    const {
  return tab_strip_model()->GetFocusedGroup();
}

std::optional<KoanWorkspaceController::SpaceInfo>
KoanWorkspaceController::GetSpace(const tab_groups::TabGroupId& space) const {
  std::optional<SpaceData> data = ReadSpace(space);
  if (!data) {
    return std::nullopt;
  }
  return SpaceInfo{.id = space,
                   .name = data->name.empty() ? kDefaultSpaceName : data->name,
                   .icon = data->icon,
                   .theme = data->theme,
                   .active = GetActiveSpace() == space};
}

base::CallbackListSubscription
KoanWorkspaceController::AddSpacesChangedCallback(
    base::RepeatingClosure callback) {
  return spaces_changed_callbacks_.Add(std::move(callback));
}

void KoanWorkspaceController::SwitchTo(const tab_groups::TabGroupId& space) {
  TabStripModel* model = tab_strip_model();
  if (!GetSpace(space) || model->GetFocusedGroup() == space) {
    return;
  }
  // Upstream activates the Space's last active tab (PickSpaceTab()).
  model->EnterFocusMode(space);
}

bool KoanWorkspaceController::SwitchToNumber(int number) {
  const std::vector<SpaceInfo> spaces = GetSpaces();
  if (number < 1 || number > static_cast<int>(spaces.size())) {
    return false;
  }
  SwitchTo(spaces[number - 1].id);
  return true;
}

tabs::TabInterface* KoanWorkspaceController::GetLastActiveTab(
    const tab_groups::TabGroupId& space) {
  TabStripModel* model = tab_strip_model();
  auto it = last_active_tabs_.find(space);
  tabs::TabInterface* last =
      it == last_active_tabs_.end() ? nullptr : it->second.Get();
  if (last && last->GetGroup() == space &&
      model->GetIndexOfTab(last) != TabStripModel::kNoTab) {
    return last;
  }
  // (It moved to another Space or window.)
  last = nullptr;
  // Not left since the window opened: the tab shown last (session restore
  // keeps when each tab was).
  if (!model->group_model() || !model->group_model()->ContainsTabGroup(space)) {
    return nullptr;
  }
  const gfx::Range range = model->group_model()->GetTabGroup(space)->ListTabs();
  for (size_t i = range.start(); i < range.end(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(static_cast<int>(i));
    content::WebContents* contents = tab->GetContents();
    if (!last || contents->GetLastActiveTime() >
                     last->GetContents()->GetLastActiveTime()) {
      last = tab;
    }
  }
  return last;
}

void KoanWorkspaceController::Cycle(bool forward) {
  const std::vector<SpaceInfo> spaces = GetSpaces();
  if (spaces.size() < 2) {
    return;
  }
  auto it = std::ranges::find(spaces, true, &SpaceInfo::active);
  const size_t index =
      it == spaces.end() ? 0 : static_cast<size_t>(it - spaces.begin());
  // zen.workspaces.wrap-around-navigation (on by default).
  const size_t next = forward ? (index + 1) % spaces.size()
                              : (index + spaces.size() - 1) % spaces.size();
  SwitchTo(spaces[next].id);
}

void KoanWorkspaceController::CreateSpace(const std::u16string& name,
                                          const std::u16string& icon,
                                          const SpaceTheme& theme) {
  TabStripModel* model = tab_strip_model();
  // Zen puts a new Space right after the active one. (Creating a group
  // leaves focus mode, so remember which one that is.)
  const std::optional<tab_groups::TabGroupId> active = GetActiveSpace();
  content::WebContents* contents = chrome::AddAndReturnTabAt(
      browser_view_->browser(), GURL(), InsertionIndexAfter(active),
      /*foreground=*/false);
  const int index = model->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab) {
    return;
  }
  const tabs::TabHandle tab = model->GetTabAtIndex(index)->GetHandle();
  const tab_groups::TabGroupId space = NewSpaceGroup({index});
  WriteSpace(space, {.name = name.empty() ? kDefaultSpaceName : name,
                     .icon = icon,
                     .theme = theme});
  // The tab opened in the active Space, and grouping it may have moved it
  // past the next one.
  if (active && GetSpace(*active)) {
    const int after_active = InsertionIndexAfter(active);
    if (static_cast<int>(
            model->group_model()->GetTabGroup(space)->ListTabs().start()) !=
        after_active) {
      model->MoveGroupTo(space, after_active);
    }
  }
  last_active_tabs_[space] = tab;
  SwitchTo(space);
}

tab_groups::TabGroupId KoanWorkspaceController::NewSpaceGroup(
    const std::vector<int>& indices) {
  // (Not a foreign group, see OnTabGroupAdded().)
  base::AutoReset<bool> creating(&creating_space_, true);
  return tab_strip_model()->AddToNewGroup(indices);
}

int KoanWorkspaceController::InsertionIndexAfter(
    std::optional<tab_groups::TabGroupId> after) const {
  TabStripModel* model = tab_strip_model();
  if (after && model->group_model()->ContainsTabGroup(*after)) {
    return static_cast<int>(
        model->group_model()->GetTabGroup(*after)->ListTabs().end());
  }
  return model->count();
}

std::optional<KoanWorkspaceController::SpaceData>
KoanWorkspaceController::ReadSpace(const tab_groups::TabGroupId& space) const {
  TabGroupModel* groups = tab_strip_model()->group_model();
  if (!groups || !groups->ContainsTabGroup(space)) {
    return std::nullopt;
  }
  const tab_groups::TabGroupVisualData* visuals =
      groups->GetTabGroup(space)->visual_data();
  auto [icon, name] = SplitTitle(visuals->title());
  return SpaceData{
      .name = std::move(name),
      .icon = std::move(icon),
      .theme = SpaceTheme{.color = visuals->color() == kDefaultThemeColor
                                       ? std::nullopt
                                       : std::make_optional(visuals->color())}};
}

void KoanWorkspaceController::WriteSpace(const tab_groups::TabGroupId& space,
                                         const SpaceData& data) {
  TabGroupModel* groups = tab_strip_model()->group_model();
  if (!groups || !groups->ContainsTabGroup(space)) {
    return;
  }
  base::AutoReset<bool> writing(&writing_space_, true);
  // A Space is never collapsed (NormalizeSpaces()).
  tab_strip_model()->ChangeTabGroupVisuals(
      space, tab_groups::TabGroupVisualData(
                 JoinTitle(data.icon, data.name),
                 data.theme.color.value_or(kDefaultThemeColor),
                 /*is_collapsed=*/false));
}

void KoanWorkspaceController::RenameSpace(const tab_groups::TabGroupId& space,
                                          const std::u16string& name) {
  std::optional<SpaceData> data = ReadSpace(space);
  std::u16string trimmed;
  base::TrimWhitespace(name, base::TRIM_ALL, &trimmed);
  // Zen ignores an empty name.
  if (!data || trimmed.empty() || trimmed == data->name) {
    return;
  }
  data->name = trimmed;
  WriteSpace(space, *data);
  // Zen's toast when a rename in the header finishes.
  if (KoanBrowserViewController* controller =
          KoanBrowserViewController::From(browser_view_)) {
    controller->toast()->Show(kWorkspaceRenamedToast);
  }
}

void KoanWorkspaceController::SetSpaceIcon(const tab_groups::TabGroupId& space,
                                           const std::u16string& icon) {
  std::optional<SpaceData> data = ReadSpace(space);
  if (!data) {
    return;
  }
  // A Space without a name shows (and keeps) Zen's default one.
  if (data->name.empty()) {
    data->name = kDefaultSpaceName;
  }
  data->icon = icon;
  WriteSpace(space, *data);
}

KoanWorkspaceController::SpaceTheme KoanWorkspaceController::GetSpaceTheme(
    const tab_groups::TabGroupId& space) const {
  std::optional<SpaceData> data = ReadSpace(space);
  return data ? data->theme : SpaceTheme();
}

void KoanWorkspaceController::SetSpaceTheme(const tab_groups::TabGroupId& space,
                                            const SpaceTheme& theme) {
  std::optional<SpaceData> data = ReadSpace(space);
  if (!data) {
    return;
  }
  data->theme = theme;
  WriteSpace(space, *data);
}

void KoanWorkspaceController::UnloadSpace(const tab_groups::TabGroupId& space) {
  ShowKeptLoadedToast(DiscardSpaceTabs(space));
}

int KoanWorkspaceController::DiscardSpaceTabs(
    const tab_groups::TabGroupId& space) {
  TabStripModel* model = tab_strip_model();
  // Handles: discarding swaps a tab's WebContents.
  std::vector<tabs::TabHandle> tabs;
  for (int i = 0; i < model->count(); ++i) {
    if (model->GetTabGroupForTab(i) == space) {
      tabs.push_back(model->GetTabAtIndex(i)->GetHandle());
    }
  }
  int kept = 0;
  for (const tabs::TabHandle& handle : tabs) {
    tabs::TabInterface* tab = handle.Get();
    if (!tab) {
      continue;
    }
    content::WebContents* contents = tab->GetContents();
    // Like Zen, skip tabs that aren't loaded.
    if (IsTabUnloaded(contents)) {
      continue;
    }
    // The tabs on screen (the active one and its split view partner), and
    // tabs Memory Saver keeps (sound, capture, form input, ...).
    if (model->IsTabSelected(model->GetIndexOfTab(tab)) ||
        IsTabInUse(contents)) {
      ++kept;
      continue;
    }
    UnloadTab(contents);
  }
  return kept;
}

void KoanWorkspaceController::UnloadAllOtherSpaces(
    const tab_groups::TabGroupId& space) {
  int kept = 0;
  for (const SpaceInfo& other : GetSpaces()) {
    if (other.id != space) {
      kept += DiscardSpaceTabs(other.id);
    }
  }
  ShowKeptLoadedToast(kept);
}

bool KoanWorkspaceController::CanDeleteSpace() const {
  return GetSpaces().size() > 1;
}

void KoanWorkspaceController::DeleteSpace(const tab_groups::TabGroupId& space) {
  std::optional<SpaceInfo> info = GetSpace(space);
  if (!info || !CanDeleteSpace()) {
    return;
  }
  chrome::ShowQuestionMessageBoxAsync(
      browser_view_->GetWidget()->GetNativeWindow(), u"Delete Space?",
      base::StrCat({u"Are you sure you want to delete ", info->name,
                    u"? This action cannot be undone."}),
      base::BindOnce(
          [](base::WeakPtr<KoanWorkspaceController> controller,
             tab_groups::TabGroupId space, chrome::MessageBoxResult result) {
            if (controller) {
              controller->OnDeleteConfirmed(
                  space, result == chrome::MESSAGE_BOX_RESULT_YES);
            }
          },
          weak_factory_.GetWeakPtr(), space));
}

void KoanWorkspaceController::OnDeleteConfirmed(
    const tab_groups::TabGroupId& space,
    bool confirmed) {
  if (!confirmed || !GetSpace(space) || !CanDeleteSpace()) {
    return;
  }
  TabStripModel* model = tab_strip_model();
  if (GetActiveSpace() == space) {
    Cycle(/*forward=*/false);
  }
  deleting_.insert(space);
  last_active_tabs_.erase(space);
  for (int i = model->count() - 1; i >= 0; --i) {
    if (model->GetTabGroupForTab(i) == space) {
      model->CloseWebContentsAt(i, TabCloseTypes::CLOSE_USER_GESTURE);
    }
  }
}

void KoanWorkspaceController::MoveSpace(const tab_groups::TabGroupId& space,
                                        int to) {
  TabStripModel* model = tab_strip_model();
  std::vector<tab_groups::TabGroupId> others;
  for (const SpaceInfo& info : GetSpaces()) {
    if (info.id != space) {
      others.push_back(info.id);
    }
  }
  if (!GetSpace(space) || others.empty() || to < 0) {
    return;
  }
  const auto range_of = [model](const tab_groups::TabGroupId& id) {
    return model->group_model()->GetTabGroup(id)->ListTabs();
  };
  const gfx::Range moved = range_of(space);
  // TabStripModel::MoveGroupTo() takes where the group's first tab ends up,
  // counted without the group's own tabs.
  const auto without_moved = [&moved](int index) {
    return index > static_cast<int>(moved.start())
               ? index - static_cast<int>(moved.length())
               : index;
  };
  const int index =
      to < static_cast<int>(others.size())
          ? without_moved(static_cast<int>(range_of(others[to]).start()))
          : without_moved(static_cast<int>(range_of(others.back()).end()));
  model->MoveGroupTo(space, index);
}

void KoanWorkspaceController::MoveTabsToSpace(
    const std::vector<tabs::TabInterface*>& tabs,
    const tab_groups::TabGroupId& space) {
  TabStripModel* model = tab_strip_model();
  if (tabs.empty() || !GetSpace(space)) {
    return;
  }
  std::vector<tabs::TabHandle> handles;
  for (tabs::TabInterface* tab : tabs) {
    handles.push_back(tab->GetHandle());
  }
  std::vector<int> indices;
  for (const tabs::TabHandle& handle : handles) {
    // Essentials belong to no Space.
    if (tabs::TabInterface* tab = handle.Get(); tab && !tab->IsPinned()) {
      indices.push_back(model->GetIndexOfTab(tab));
    }
  }
  if (indices.empty()) {
    return;
  }
  std::ranges::sort(indices);
  model->AddToExistingGroup(indices, space, /*add_to_end=*/true);
  // (A tab pinned to its Space stays pinned, among the new Space's pinned
  // tabs: KoanPinnedTabController sees it change group.)
  // Zen shows the Space on the last moved tab. Activated first, so the
  // Space's remembered tab never shows (and loads, if it was unloaded).
  if (tabs::TabInterface* last = handles.back().Get();
      last && last->GetGroup() == space) {
    model->ActivateTabAt(model->GetIndexOfTab(last));
  }
  SwitchTo(space);
}

void KoanWorkspaceController::ShowKeptLoadedToast(int kept) {
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  if (kept == 0 || !controller) {
    return;
  }
  // Zen unloads without a word; Koan says why some tabs are still loaded.
  controller->toast()->Show(
      kept == 1 ? u"1 tab in use stayed loaded"
                : base::StrCat({base::NumberToString16(kept),
                                u" tabs in use stayed loaded"}));
}

// Zen's UI.

void KoanWorkspaceController::ShowCreateForm() {
  if (creation_form_ || !header_) {
    return;
  }
  auto* tab_strip = views::AsViewClass<TabStripView>(header_->parent());
  // A collapsed sidebar (icons only, or compact mode's hidden one) has no
  // room for the form: keep it expanded, over the page, while it's open.
  auto* state =
      tabs::VerticalTabStripStateController::From(browser_view_->browser());
  if (state && state->IsCollapsed()) {
    create_form_lock_ =
        static_cast<BaseTabStripRegionView*>(browser_view_->tab_strip_view())
            ->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepExpanded);
  }
  creation_form_ = tab_strip->AddChildViewAt(
      std::make_unique<KoanSpaceCreationView>(this), 2);
  view_observations_.AddObservation(creation_form_);
  // The form takes the place of the Space (header, tab rows and tabs), like
  // Zen's.
  SetSpaceRowsVisible(tab_strip, false);
  tab_strip->InvalidateLayout();
  creation_form_->FocusName();
}

void KoanWorkspaceController::CloseCreateForm() {
  if (!creation_form_) {
    return;
  }
  auto* tab_strip = views::AsViewClass<TabStripView>(creation_form_->parent());
  KoanSpaceCreationView* form = creation_form_.get();
  creation_form_ = nullptr;
  tab_strip->RemoveChildViewT(form);
  SetSpaceRowsVisible(tab_strip, true);
  tab_strip->InvalidateLayout();
  create_form_lock_.reset();
}

bool KoanWorkspaceController::IsCreateFormOpen() const {
  return creation_form_ != nullptr;
}

void KoanWorkspaceController::KeepTabMenuSubmenu(
    std::unique_ptr<ui::SimpleMenuModel> submenu) {
  tab_menu_submenu_ = std::move(submenu);
}

void KoanWorkspaceController::AddCreateMenuEntry(CreateMenuEntry entry) {
  create_menu_entries_.push_back(std::move(entry));
}

void KoanWorkspaceController::AddDefaultCreateMenuEntries() {
  // The browser view outlives this controller.
  BrowserWindowInterface* browser = browser_view_->browser();
  CreateMenuEntry create_space;
  create_space.label = u"Create Space";
  create_space.icon = &vector_icons::kContentCopyIcon;
  create_space.run = base::BindRepeating(
      &KoanWorkspaceController::ShowCreateForm, base::Unretained(this));
  AddCreateMenuEntry(std::move(create_space));
  // (Folders add "New Folder" here.)
  const auto command = [&](std::u16string label, const gfx::VectorIcon& icon,
                           int id) {
    CreateMenuEntry entry;
    entry.label = std::move(label);
    entry.icon = &icon;
    entry.section = CreateMenuEntry::Section::kTabs;
    entry.is_enabled = base::BindRepeating(
        [](BrowserWindowInterface* browser, int id) {
          return chrome::IsCommandEnabled(browser, id);
        },
        base::Unretained(browser), id);
    entry.run =
        base::BindRepeating([](BrowserWindowInterface* browser,
                               int id) { chrome::ExecuteCommand(browser, id); },
                            base::Unretained(browser), id);
    AddCreateMenuEntry(std::move(entry));
  };
  command(u"New Split", kSplitSceneIcon, IDC_NEW_SPLIT_TAB);
  command(u"New Tab", kAddIcon, IDC_NEW_TAB);
}

void KoanWorkspaceController::ShowCreateMenu(views::View* anchor) {
  create_menu_ = std::make_unique<KoanCreateMenu>(this);
  create_menu_->RunMenuAt(anchor);
}

void KoanWorkspaceController::ShowSpaceMenu(
    std::optional<tab_groups::TabGroupId> space,
    views::View* anchor,
    const gfx::Point& screen_point) {
  if (!space && !GetActiveSpace()) {
    return;
  }
  space_menu_ = std::make_unique<KoanSpaceMenu>(this, space);
  space_menu_->RunMenuAt(anchor, screen_point);
}

void KoanWorkspaceController::StartRename() {
  if (header_) {
    header_->StartRename();
  }
}

void KoanWorkspaceController::ShowIconPicker(
    const tab_groups::TabGroupId& space,
    views::View* anchor) {
  std::optional<SpaceInfo> info = GetSpace(space);
  if (!info || !anchor) {
    return;
  }
  icon_picker_ = KoanEmojiPicker::Show(
      anchor, /*allow_none=*/!info->icon.empty(),
      base::BindRepeating(&KoanWorkspaceController::SetSpaceIcon,
                          weak_factory_.GetWeakPtr(), space));
}

void KoanWorkspaceController::EditSpaceTheme(
    const tab_groups::TabGroupId& space,
    views::View* anchor) {
  if (!GetSpace(space) || !anchor) {
    return;
  }
  theme_menu_ = std::make_unique<KoanThemeMenu>(
      GetSpaceTheme(space),
      base::BindRepeating(&KoanWorkspaceController::SetSpaceTheme,
                          weak_factory_.GetWeakPtr(), space));
  theme_menu_->RunMenuAt(anchor);
}

void KoanWorkspaceController::ShowIconPickerForActiveSpace() {
  const std::optional<tab_groups::TabGroupId> active = GetActiveSpace();
  if (active && header_ && header_->GetVisible()) {
    ShowIconPicker(*active, header_);
  }
}

void KoanWorkspaceController::EditActiveSpaceTheme() {
  const std::optional<tab_groups::TabGroupId> active = GetActiveSpace();
  if (active && header_ && header_->GetVisible()) {
    EditSpaceTheme(*active, header_);
  }
}

void KoanWorkspaceController::OnSpaceWheel(bool forward) {
  const base::TimeTicks now = base::TimeTicks::Now();
  if (now - last_wheel_switch_ >= kWheelSwitchCooldown) {
    last_wheel_switch_ = now;
    Cycle(forward);
  }
}

// Keeping one Space shown.

void KoanWorkspaceController::ScheduleUpdate() {
  if (update_pending_) {
    return;
  }
  update_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanWorkspaceController::Update,
                                weak_factory_.GetWeakPtr()));
}

void KoanWorkspaceController::Update() {
  update_pending_ = false;
  if (!IsEnabled() || destroying_) {
    return;
  }
  // The views go away with the vertical tab strip (switching to horizontal
  // tabs) and come back with it.
  InstallViews();
  EnsureOneSpaceShown();
  DisconnectSavedGroups();
  KeepStartupEssential();
  SnapshotSpaces();
  StoreSpaces();
  CloseGroupEditors();
  // Also where upstream set the group's colour without asking Koan (the
  // horizontal tab strip's controller has no hook).
  ApplySpaceColor();
  UpdateViews();
  spaces_changed_callbacks_.Notify();
  CheckSpaces();
}

void KoanWorkspaceController::KeepStartupEssential() {
  TabStripModel* model = tab_strip_model();
  if (started_ || model->empty()) {
    return;
  }
  started_ = true;
  // Like Zen when a window opens: an Essential that was selected stays
  // selected, although showing a Space (session restore's, or the one
  // above) selected one of its tabs.
  tabs::TabInterface* essential =
      std::exchange(startup_essential_, tabs::TabHandle()).Get();
  if (essential && essential->IsPinned() &&
      model->GetActiveTab() != essential) {
    model->ActivateTabAt(model->GetIndexOfTab(essential));
  }
}

void KoanWorkspaceController::OnSessionRestored(Profile* profile,
                                                int tab_count) {
  if (restore_started_ && profile == browser_view_->GetProfile()) {
    restore_started_ = false;
    drop_startup_space_ = pre_restore_space_.has_value();
    ScheduleUpdate();
  }
}

bool KoanWorkspaceController::IsUntouchedStartupSpace(
    const tab_groups::TabGroupId& space) const {
  const std::optional<SpaceInfo> info = GetSpace(space);
  if (!info || info->name != kDefaultSpaceName || !info->icon.empty() ||
      info->theme.color) {
    return false;
  }
  TabStripModel* model = tab_strip_model();
  const gfx::Range tabs = model->group_model()->GetTabGroup(space)->ListTabs();
  if (tabs.length() != 1) {
    return false;
  }
  // Upstream's HasSingleNewTabPage(), which decides whether a restore after a
  // crash replaces the tab the window started on.
  const int index = static_cast<int>(tabs.start());
  content::WebContents* contents = model->GetWebContentsAt(index);
  return !model->IsTabPinned(index) &&
         (contents->GetURL() == chrome::ChromeUINewTabURLAsGURL() ||
          search::IsInstantNTP(contents));
}

void KoanWorkspaceController::DropStartupSpaceAfterRestore() {
  const std::optional<tab_groups::TabGroupId> space =
      std::exchange(pre_restore_space_, std::nullopt);
  if (!space || GetSpaces().size() < 2 || !IsUntouchedStartupSpace(*space)) {
    return;
  }
  TabStripModel* model = tab_strip_model();
  if (GetActiveSpace() == space) {
    Cycle(/*forward=*/true);
  }
  deleting_.insert(*space);
  last_active_tabs_.erase(*space);
  model->CloseWebContentsAt(
      static_cast<int>(
          model->group_model()->GetTabGroup(*space)->ListTabs().start()),
      TabCloseTypes::CLOSE_USER_GESTURE);
}

void KoanWorkspaceController::EnsureOneSpaceShown() {
  TabStripModel* model = tab_strip_model();
  if (model->empty() || model->closing_all() ||
      browser_view_->browser()->IsDeleteScheduled()) {
    closed_spaces_.clear();
    return;
  }
  // A Space whose tabs didn't all close (a page's "Leave site?" was
  // cancelled, which clears its close gesture) stays a normal Space.
  std::erase_if(deleting_, [model](const tab_groups::TabGroupId& id) {
    if (!model->group_model()->ContainsTabGroup(id)) {
      return false;
    }
    const gfx::Range tabs = model->group_model()->GetTabGroup(id)->ListTabs();
    for (size_t i = tabs.start(); i < tabs.end(); ++i) {
      if (model->GetWebContentsAt(static_cast<int>(i))
              ->GetClosedByUserGesture()) {
        return false;
      }
    }
    return true;
  });
  AdoptResurrectedSpaces();
  NormalizeSpaces();

  // A Space whose last tab closed comes back with a new tab: Zen keeps
  // empty Spaces.
  for (const ClosedSpace& closed : std::exchange(closed_spaces_, {})) {
    if (model->group_model()->ContainsTabGroup(closed.id)) {
      continue;
    }
    int index = model->count();
    if (closed.next && model->group_model()->ContainsTabGroup(*closed.next)) {
      index = static_cast<int>(
          model->group_model()->GetTabGroup(*closed.next)->ListTabs().start());
    }
    content::WebContents* contents = chrome::AddAndReturnTabAt(
        browser_view_->browser(), GURL(), index, /*foreground=*/false);
    const int new_index = model->GetIndexOfWebContents(contents);
    if (new_index == TabStripModel::kNoTab) {
      continue;
    }
    // Same id, so whatever refers to the Space still does.
    model->AddToGroupForRestore({new_index}, closed.id);
    WriteSpace(closed.id, closed.data);
    if (closed.was_active && !model->GetFocusedGroup()) {
      // Zen stays on an Essential that was selected when the Space emptied.
      tabs::TabInterface* active_tab = model->GetActiveTab();
      const bool on_essential = active_tab && active_tab->IsPinned();
      SwitchTo(closed.id);
      if (on_essential) {
        model->ActivateTabAt(model->GetIndexOfTab(active_tab));
      }
    }
  }

  // Loose tabs (dragged in from another window, unpinned, from an older
  // Koan) join the active Space, or a new one.
  std::vector<int> loose;
  for (int i = 0; i < model->count(); ++i) {
    if (!model->IsTabPinned(i) && !model->GetTabGroupForTab(i)) {
      loose.push_back(i);
    }
  }
  std::vector<SpaceInfo> spaces = GetSpaces();
  if (spaces.empty() && loose.empty()) {
    // Only Essentials: the window still has a Space, with a new tab.
    chrome::AddTabAt(browser_view_->browser(), GURL(), model->count(),
                     /*foreground=*/false);
    loose.push_back(model->count() - 1);
  }
  // A window that opened with no Spaces (none restored) while the profile has
  // no other: it takes the stored ones.
  if (!started_ && spaces.empty() && RecreateStoredSpaces(loose)) {
    loose.clear();
    spaces = GetSpaces();
  }
  if (!loose.empty()) {
    std::optional<tab_groups::TabGroupId> target = model->GetFocusedGroup();
    if (!target && last_space_ && GetSpace(*last_space_)) {
      target = last_space_;
    }
    if (!target && !spaces.empty()) {
      target = spaces.front().id;
    }
    if (target) {
      model->AddToExistingGroup(loose, *target, /*add_to_end=*/true);
    } else {
      WriteSpace(NewSpaceGroup(loose), {.name = kDefaultSpaceName});
    }
    spaces = GetSpaces();
  }
  if (std::exchange(drop_startup_space_, false)) {
    DropStartupSpaceAfterRestore();
    spaces = GetSpaces();
  }

  if (model->GetFocusedGroup() || spaces.empty()) {
    return;
  }
  // Upstream left focus mode because a tab of another Space was activated
  // (switch-to-tab, tab search, an extension, back to the opener): show
  // that Space, on that tab. Otherwise the Space shown last.
  tabs::TabInterface* active = model->GetActiveTab();
  std::optional<tab_groups::TabGroupId> target =
      active ? active->GetGroup() : std::nullopt;
  if (!target && last_space_ && GetSpace(*last_space_)) {
    target = last_space_;
  }
  SwitchTo(target.value_or(spaces.front().id));
}

void KoanWorkspaceController::SnapshotSpaces() {
  spaces_snapshot_.clear();
  for (const SpaceInfo& space : GetSpaces()) {
    spaces_snapshot_.emplace_back(space.id, *ReadSpace(space.id));
  }
}

void KoanWorkspaceController::NormalizeSpaces() {
  TabGroupModel* groups = tab_strip_model()->group_model();
  for (const SpaceInfo& space : GetSpaces()) {
    const bool collapsed =
        groups->GetTabGroup(space.id)->visual_data()->is_collapsed();
    const bool foreign = foreign_groups_.contains(space.id);
    if (!collapsed && !foreign) {
      continue;
    }
    // Upstream collapses a group from its header (hidden here), an old
    // profile's session or an extension (tabGroups.update()): expanded, as
    // WriteSpace() writes it. A group from outside Koan gets Zen's default
    // theme, not Chrome's next group colour.
    SpaceData data = *ReadSpace(space.id);
    if (foreign) {
      data.theme = SpaceTheme();
      if (data.name.empty()) {
        data.name = kDefaultSpaceName;
      }
    }
    WriteSpace(space.id, data);
  }
  foreign_groups_.clear();
}

void KoanWorkspaceController::AdoptResurrectedSpaces() {
  TabStripModel* model = tab_strip_model();
  Profile* profile = browser_view_->GetProfile();
  const auto in_another_window = [&](const tab_groups::TabGroupId& id) {
    for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
      TabStripModel* other = browser->GetTabStripModel();
      if (browser != browser_view_->browser() &&
          browser->GetProfile() == profile && other && other->group_model() &&
          other->group_model()->ContainsTabGroup(id)) {
        return true;
      }
    }
    return false;
  };
  for (const tab_groups::TabGroupId& id : std::exchange(resurrected_, {})) {
    if (!model->group_model()->ContainsTabGroup(id) ||
        (!DeletedSpaces().contains(id) && !in_another_window(id))) {
      continue;
    }
    // Tab restore (⌘⇧T, History) put the tab back in its Space's group, by
    // id, with the Space's old looks. Zen reopens a tab in its Space if
    // that still exists, else in the Space shown.
    std::optional<tab_groups::TabGroupId> target =
        last_space_ != id ? last_space_ : std::nullopt;
    if (!target || !GetSpace(*target)) {
      target.reset();
      for (const SpaceInfo& space : GetSpaces()) {
        if (space.id != id) {
          target = space.id;
          break;
        }
      }
    }
    if (!target) {
      // The window has no other Space: this one stays, as a new Space.
      WriteSpace(id, {.name = kDefaultSpaceName});
      continue;
    }
    model->AddToExistingGroup(
        model->group_model()->GetTabGroup(id)->ListTabs().ToIntVector(),
        *target, /*add_to_end=*/true);
  }
}

bool KoanWorkspaceController::RecreateStoredSpaces(
    const std::vector<int>& loose) {
  Profile* profile = browser_view_->GetProfile();
  // Not while a session is restored (its windows have their Spaces), nor
  // after a crash (the user may still pick Restore, into this window).
  if (SessionRestore::IsRestoring(profile) ||
      ExitTypeService::GetLastSessionExitType(profile) == ExitType::kCrashed) {
    return false;
  }
  // Only the profile's one window: a start that restored no session (startup
  // set to the New Tab page or to pages, or a session with no window), or
  // macOS's new window after the last one closed. A window opened next to
  // another (⌘N, a tab torn off or moved to a new window) starts with one
  // Space, as before (Zen would sync the other window's; Koan's Spaces are
  // per window).
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    if (browser != browser_view_->browser() &&
        browser->GetProfile() == profile && SpacesOf(browser)) {
      return false;
    }
  }
  std::vector<std::pair<SpaceData, bool>> stored;
  for (const base::Value& entry :
       profile->GetPrefs()->GetList(prefs::kWorkspaceList)) {
    const base::DictValue* dict = entry.GetIfDict();
    const std::string* title = dict ? dict->FindString("title") : nullptr;
    const std::optional<int> color =
        dict ? dict->FindInt("color") : std::nullopt;
    if (!title || !color || *color < 0 ||
        *color >= static_cast<int>(tab_groups::TabGroupColorId::kNumEntries)) {
      continue;
    }
    auto [icon, name] = SplitTitle(base::UTF8ToUTF16(*title));
    const auto color_id = static_cast<tab_groups::TabGroupColorId>(*color);
    stored.emplace_back(
        SpaceData{.name = std::move(name),
                  .icon = std::move(icon),
                  .theme = {.color = color_id == kDefaultThemeColor
                                         ? std::nullopt
                                         : std::make_optional(color_id)}},
        dict->FindBool("active").value_or(false));
  }
  if (stored.empty()) {
    return false;
  }
  TabStripModel* model = tab_strip_model();
  size_t active = 0;
  for (size_t i = 0; i < stored.size(); ++i) {
    if (stored[i].second) {
      active = i;
    }
  }
  // The startup tabs (the New Tab page, or the startup pages) go to the
  // Space that was active, which is shown; every other Space gets a new tab,
  // as Zen's empty Spaces have.
  std::vector<tab_groups::TabGroupId> ids;
  for (size_t i = 0; i < stored.size(); ++i) {
    std::vector<int> indices = i == active ? loose : std::vector<int>();
    if (indices.empty()) {
      content::WebContents* contents = chrome::AddAndReturnTabAt(
          browser_view_->browser(), GURL(), model->count(),
          /*foreground=*/false);
      const int index = model->GetIndexOfWebContents(contents);
      if (index == TabStripModel::kNoTab) {
        return !ids.empty();
      }
      indices = {index};
    }
    const tab_groups::TabGroupId id = NewSpaceGroup(indices);
    WriteSpace(id, stored[i].first);
    ids.push_back(id);
  }
  // In the stored order (the startup tabs' Space was made where they were).
  for (size_t i = 0; i < ids.size(); ++i) {
    MoveSpace(ids[i], static_cast<int>(i));
  }
  SwitchTo(ids[std::min(active, ids.size() - 1)]);
  return true;
}

void KoanWorkspaceController::StoreSpaces() {
  TabStripModel* model = tab_strip_model();
  if (model->empty() || model->closing_all() ||
      browser_view_->browser()->IsDeleteScheduled()) {
    return;
  }
  base::ListValue list;
  for (const auto& [id, data] : spaces_snapshot_) {
    list.Append(base::DictValue()
                    .Set("title", JoinTitle(data.icon, data.name))
                    .Set("color", static_cast<int>(data.theme.color.value_or(
                                      kDefaultThemeColor)))
                    .Set("active", GetActiveSpace() == id));
  }
  if (list.empty()) {
    return;
  }
  // After a crash the session may still be restored (into this window): the
  // stored Spaces stay until this window's change.
  if (!first_spaces_) {
    first_spaces_ = list.Clone();
    may_store_spaces_ = ExitTypeService::GetLastSessionExitType(
                            browser_view_->GetProfile()) != ExitType::kCrashed;
  }
  if (!may_store_spaces_) {
    if (list == *first_spaces_) {
      return;
    }
    may_store_spaces_ = true;
  }
  // The window the user is in, or was in last.
  const views::Widget* widget = browser_view_->GetWidget();
  const BrowserWindowInterface* last_active =
      GetLastActiveBrowserWindowInterfaceWithAnyProfile();
  if (list == stored_spaces_ ||
      !((widget && widget->IsActive()) || !last_active ||
        last_active == browser_view_->browser())) {
    return;
  }
  stored_spaces_ = list.Clone();
  browser_view_->GetProfile()->GetPrefs()->SetList(prefs::kWorkspaceList,
                                                   std::move(list));
}

void KoanWorkspaceController::DisconnectSavedGroups() {
  // Spaces aren't saved groups (koan::SavesTabGroups()), but an old profile
  // may have saved groups already, and opening one (the everything menu, a
  // bookmarks bar chip) makes a Space connected to it. Connected, its
  // renames sync, a remote delete closes its tabs and closing it asks
  // Chrome's "Delete group?". Koan drops the connection (the saved group
  // stays, as closed): upstream's dialog only asks for a group the service
  // maps (SavedTabGroupUtils::MaybeShowSavedTabGroupDeletionDialog()), and a
  // sync change only reaches a mapped one.
  tab_groups::TabGroupSyncService* service =
      tab_groups::TabGroupSyncServiceFactory::GetForProfile(
          browser_view_->GetProfile());
  if (!service) {
    return;
  }
  for (const SpaceInfo& space : GetSpaces()) {
    if (service->GetGroup(space.id)) {
      service->RemoveLocalTabGroupMapping(space.id,
                                          tab_groups::ClosingSource::kUnknown);
    }
  }
}

void KoanWorkspaceController::CloseGroupEditors() {
  views::Widget* widget = browser_view_->GetWidget();
  if (!widget) {
    return;
  }
  // Upstream opens its group editor for a new group (⌘⌃P, Group Tab) and
  // from the group header; neither runs here (InstallGroupActions(), the
  // hidden header), so this is a backstop for any other way.
  for (views::View* view :
       views::ElementTrackerViews::GetInstance()->GetAllMatchingViews(
           TabGroupEditorBubbleView::kTabGroupEditorBubbleViewId,
           views::ElementTrackerViews::GetContextForWidget(widget))) {
    if (views::Widget* bubble = view->GetWidget();
        bubble && !bubble->IsClosed()) {
      bubble->CloseWithReason(views::Widget::ClosedReason::kUnspecified);
    }
  }
}

void KoanWorkspaceController::CheckSpaces() {
#if DCHECK_IS_ON()
  TabStripModel* model = tab_strip_model();
  BrowserWidget* widget = browser_view_->browser_widget();
  // Only a window at rest. Every state a valid window passes through on its
  // way somewhere else is skipped, so the check never fires falsely:
  // - another update is due: this one changed the model, or something
  //   changed it after (a new Space's group leaves focus mode until the next
  //   update: NewSpaceGroup(); loose tabs, a closed Space coming back);
  // - the window goes away (destroying, closed, delete scheduled, closing
  //   all tabs), or has no tabs or no Space yet (first update, Essentials
  //   only);
  // - tabs of a Space being deleted are closing (a "Leave site?" may hold
  //   them);
  // - a tab is dragged: dragged tabs leave and rejoin Spaces;
  // - a session is being restored, into this window or another;
  // - the Create-a-Space form is open: the Space rows are hidden and the new
  //   Space is made when the form is done;
  // - Glance is open or closing: its page isn't a tab until it expands, and
  //   expanding or splitting it inserts a tab next to its source (an
  //   Essential's is loose until the next update);
  // - a split is being made: its tabs may be in different groups, or one
  //   pinned, until the split settles;
  // - upstream's ephemeral group ("Focus tabs", a flag) is focused.
  // The Space switch animation, the swipe and the sidebar's collapse change
  // no model state. Popups and Incognito have no Spaces, so no Update().
  const KoanBrowserViewController* koan =
      KoanBrowserViewController::From(browser_view_);
  const KoanGlanceController* glance = koan ? koan->glance() : nullptr;
  const auto split_settling = [model] {
    for (int i = 0; i < model->count(); ++i) {
      tabs::TabInterface* tab = model->GetTabAtIndex(i);
      if (!tab->IsSplit()) {
        continue;
      }
      for (tabs::TabInterface* other :
           model->GetSplitData(*tab->GetSplit())->ListTabs()) {
        if (other->GetGroup() != tab->GetGroup() ||
            other->IsPinned() != tab->IsPinned()) {
          return true;
        }
      }
    }
    return false;
  };
  if (destroying_ || update_pending_ || !widget || widget->IsClosed() ||
      model->empty() || model->closing_all() || !deleting_.empty() ||
      browser_view_->browser()->IsDeleteScheduled() ||
      widget->tab_drag_kind() != TabDragKind::kNone ||
      SessionRestore::IsRestoring(browser_view_->GetProfile()) ||
      GetSpaces().empty() || creation_form_ ||
      (glance && (glance->is_open() || glance->is_closing())) ||
      split_settling() ||
      (model->GetFocusedGroup() &&
       model->IsEphemeralTabGroup(*model->GetFocusedGroup()))) {
    return;
  }
  std::string leak;
  if (!model->GetFocusedGroup()) {
    leak += " no Space is shown (focus mode is off);";
  }
  for (int i = 0; i < model->count(); ++i) {
    if (!model->IsTabPinned(i) && !model->GetTabGroupForTab(i)) {
      leak += " a tab is in no Space;";
      break;
    }
  }
  for (const SpaceInfo& space : GetSpaces()) {
    if (model->group_model()
            ->GetTabGroup(space.id)
            ->visual_data()
            ->is_collapsed()) {
      leak += " a Space is collapsed;";
    }
  }
  for (views::View* view :
       views::ElementTrackerViews::GetInstance()->GetAllMatchingViews(
           TabGroupEditorBubbleView::kTabGroupEditorBubbleViewId,
           views::ElementTrackerViews::GetContextForWidget(widget))) {
    if (view->GetWidget() && !view->GetWidget()->IsClosed()) {
      leak += " upstream's group editor is open;";
    }
  }
  if (widget->user_color_override() != GetSpaceColor()) {
    leak += " the window has a colour Koan didn't set;";
  }
  if (TabStripRegionView* region = browser_view_->tab_strip_view()) {
    for (int i = 0; i < model->count(); ++i) {
      auto* tab_view = views::AsViewClass<TabView>(
          region->GetTabAnchorView(model->GetTabAtIndex(i)->GetHandle()));
      if (tab_view &&
          tab_view->HasFreezingVote(FreezingVoteReason::kFocusedGroup)) {
        leak += " a tab of another Space is frozen by focus mode;";
        break;
      }
    }
  }
  // Upstream's tab group state leaked into the Spaces (a new upstream entry
  // point, or a hook that no longer runs after a Chromium update): see
  // .agent/notes/upstream/tab-groups-omnibox-fullscreen.md.
  DCHECK(leak.empty()) << "Spaces:" << leak;
#endif  // DCHECK_IS_ON()
}

void KoanWorkspaceController::UpdateViews() {
  const std::optional<tab_groups::TabGroupId> active = GetActiveSpace();
  if (header_) {
    if (std::optional<SpaceInfo> info =
            active ? GetSpace(*active) : std::nullopt) {
      header_->Update(info->icon, info->name);
    } else {
      header_->Clear();
    }
  }
  if (bar_) {
    bar_->Update(GetSpaces());
  }
}

void KoanWorkspaceController::PlaySwitchAnimation(
    const tab_groups::TabGroupId& from,
    const tab_groups::TabGroupId& to) {
  if (!header_ || !header_->GetVisible() || !header_->has_space() ||
      !gfx::Animation::ShouldRenderRichAnimation()) {
    return;
  }
  auto* tab_strip = views::AsViewClass<TabStripView>(header_->parent());
  if (!tab_strip->GetWidget() || tab_strip->width() == 0) {
    return;
  }
  // Like Zen, the shorter way round: with wrap-around, going from the last
  // Space to the first moves forward.
  const std::vector<SpaceInfo> spaces = GetSpaces();
  const auto index_of = [&spaces](const tab_groups::TabGroupId& id) {
    return static_cast<int>(std::ranges::find(spaces, id, &SpaceInfo::id) -
                            spaces.begin());
  };
  const int count = static_cast<int>(spaces.size());
  int diff = index_of(to) - index_of(from);
  if (diff > count / 2) {
    diff -= count;
  } else if (diff < -(count / 2)) {
    diff += count;
  }
  // Moving forward, the new Space comes in from the right (in LTR).
  const float distance = static_cast<float>(tab_strip->width()) *
                         ((diff > 0) != base::i18n::IsRTL() ? 1.f : -1.f);
  // A swipe that was dragging the Space: both slide on from where it is. (0
  // for any other switch, and for a swipe that was abandoned.)
  const float dragged = drag_.TakeSlideOffset();
  swipe_timer_.Stop();

  // TabStripView clips its layer to its bounds, so the Spaces slide
  // within the sidebar.
  for (views::View* view : GetSlidingViews()) {
    // Switching again mid-slide: the old Space slides on from where it is.
    view->layer()->GetAnimator()->AbortAllAnimations();
    // The old layers keep showing the old Space while it slides out.
    std::unique_ptr<ui::LayerTreeOwner> old_layers = RecreateLayers(view);
    if (!old_layers) {
      continue;
    }
    auto slide_out = std::make_unique<SlideOutLayers>(
        std::move(old_layers),
        base::BindOnce(&KoanWorkspaceController::OnSlideOutDone,
                       weak_factory_.GetWeakPtr()));
    ui::Layer* old_root = slide_out->root();
    {
      ui::ScopedLayerAnimationSettings settings(old_root->GetAnimator());
      settings.SetTransitionDuration(kSwitchAnimationDuration);
      settings.SetTweenType(gfx::Tween::EASE_OUT);
      settings.AddObserver(slide_out.get());
      gfx::Transform out;
      out.Translate(-distance, 0);
      old_root->SetTransform(out);
    }
    slide_outs_.push_back(std::move(slide_out));
    ui::Layer* layer = view->layer();
    gfx::Transform in;
    in.Translate(distance + dragged, 0);
    layer->SetTransform(in);
    ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
    settings.SetTransitionDuration(kSwitchAnimationDuration);
    settings.SetTweenType(gfx::Tween::EASE_OUT);
    layer->SetTransform(gfx::Transform());
  }
}

void KoanWorkspaceController::OnSlideOutDone(SlideOutLayers* slide_out) {
  std::erase_if(slide_outs_, [slide_out](const auto& entry) {
    return entry.get() == slide_out;
  });
  if (slide_outs_.empty()) {
    // Not from inside the animation step that just ended.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanWorkspaceController::DropSwitchLayers,
                                  weak_factory_.GetWeakPtr()));
  }
}

std::vector<views::View*> KoanWorkspaceController::GetSlidingViews() {
  auto* tab_strip = views::AsViewClass<TabStripView>(header_->parent());
  const std::vector<views::View*> views = {
      header_.get(), tab_strip->unpinned_tabs_scroll_view()};
  for (views::View* view : views) {
    if (!view->layer()) {
      view->SetPaintToLayer();
      view->layer()->SetFillsBoundsOpaquely(false);
      // Only for the animation (DropSwitchLayers()).
      switch_layered_views_.push_back(
          std::make_unique<views::ViewTracker>(view));
    }
  }
  return views;
}

bool KoanWorkspaceController::CanSwipe() const {
  return header_ && header_->GetVisible() && header_->has_space() &&
         !creation_form_ && slide_outs_.empty() && GetSpaces().size() >= 2 &&
         header_->parent() && header_->parent()->width() > 0;
}

bool KoanWorkspaceController::OnSwipeScroll(const ui::ScrollEvent& event) {
  return swipe_->OnScrollEvent(event);
}

void KoanWorkspaceController::UpdateSwipe(float amount) {
  if (!CanSwipe()) {
    // Whatever was dragged lets go (another Space is sliding in, the form
    // opened...).
    if (drag_.live()) {
      CancelSwipe(/*animate=*/true);
    }
    return;
  }
  const float width = static_cast<float>(header_->parent()->width());
  const float offset = drag_.Update(amount, width);
  swipe_timer_.Start(FROM_HERE, kSwipeIdleTimeout, this,
                     &KoanWorkspaceController::OnSwipeTimeout);
  if (!gfx::Animation::ShouldRenderRichAnimation()) {
    return;
  }
  gfx::Transform transform;
  transform.Translate(offset, 0);
  for (views::View* view : GetSlidingViews()) {
    view->layer()->GetAnimator()->AbortAllAnimations();
    view->layer()->SetTransform(transform);
  }
}

void KoanWorkspaceController::EndSwipe(float amount, float velocity) {
  swipe_timer_.Stop();
  if (!drag_.live()) {
    return;
  }
  drag_.Release();
  if (!header_ || !header_->parent()) {
    drag_.Reset();
    return;
  }
  if (SwipeSucceeds(amount, velocity)) {
    // Fingers moving right (a negative amount) go to the next Space, the
    // strip moving left, as Zen's does (ZenSpacesSwipe #handleSwipeEnd()).
    // The slide takes the dragged offset (PlaySwitchAnimation()).
    Cycle((amount < 0) != base::i18n::IsRTL());
    // If there was no slide (no animation, no other Space), the drag is over.
    drag_.Reset();
    DropSwitchLayers();
    return;
  }
  drag_.Reset();
  SpringBackSwipe();
}

void KoanWorkspaceController::CancelSwipe(bool animate) {
  swipe_timer_.Stop();
  swipe_->Cancel();
  // (Only a drag leaves the views off their place; a slide in progress isn't
  // this swipe's to stop.)
  const bool was_dragging = drag_.live();
  drag_.Reset();
  if (animate && was_dragging && header_ &&
      gfx::Animation::ShouldRenderRichAnimation()) {
    SpringBackSwipe();
  } else {
    DropSwitchLayers();
  }
}

void KoanWorkspaceController::OnSwipeTimeout() {
  CancelSwipe(/*animate=*/true);
}

void KoanWorkspaceController::SpringBackSwipe() {
  if (switch_layered_views_.empty()) {
    return;
  }
  views::AnimationBuilder builder;
  builder.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  builder.OnEnded(base::BindOnce(&KoanWorkspaceController::DropSwitchLayers,
                                 weak_factory_.GetWeakPtr()));
  auto& rest = builder.Once().SetDuration(kSwitchAnimationDuration);
  for (const std::unique_ptr<views::ViewTracker>& tracker :
       switch_layered_views_) {
    if (views::View* view = tracker->view(); view && view->layer()) {
      rest.SetTransform(view, gfx::Transform(), gfx::Tween::EASE_OUT);
    }
  }
}

void KoanWorkspaceController::DropSwitchLayers() {
  if (!slide_outs_.empty() || drag_.live()) {
    return;
  }
  for (const std::unique_ptr<views::ViewTracker>& tracker :
       std::exchange(switch_layered_views_, {})) {
    if (views::View* view = tracker->view(); view && view->layer()) {
      view->DestroyLayer();
    }
  }
}

// TabStripModelObserver.

void KoanWorkspaceController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // The Essential a restored window opened on (see KeepStartupEssential()).
  if (!started_ && selection.active_tab_changed() && selection.new_tab &&
      selection.new_tab->IsPinned()) {
    startup_essential_ = selection.new_tab->GetHandle();
  }
  // Each Space remembers its last active tab: the one it had when the window
  // left it.
  if (selection.active_tab_changed() && selection.old_tab &&
      change.type() != TabStripModelChange::kRemoved) {
    const std::optional<tab_groups::TabGroupId> old_space =
        selection.old_tab->GetGroup();
    const std::optional<tab_groups::TabGroupId> new_space =
        selection.new_tab ? selection.new_tab->GetGroup() : std::nullopt;
    if (old_space && old_space != new_space) {
      last_active_tabs_[*old_space] = selection.old_tab->GetHandle();
    }
  }
  if (change.type() != TabStripModelChange::kSelectionOnly) {
    ScheduleUpdate();
  }
}

void KoanWorkspaceController::OnTabGroupChanged(const TabGroupChange& change) {
  if (change.type == TabGroupChange::kCreated && !restore_started_ &&
      SessionRestore::IsRestoring(browser_view_->GetProfile())) {
    // The first Space a restore adds: the window's only Space until now is
    // the one it started with (a window the restore creates has none, and
    // Spaces the user made are never replaced).
    restore_started_ = true;
    std::vector<SpaceInfo> before = GetSpaces();
    std::erase_if(
        before, [&](const SpaceInfo& info) { return info.id == change.group; });
    pre_restore_space_ = before.size() == 1
                             ? std::make_optional(before.front().id)
                             : std::nullopt;
  }
  if (change.type == TabGroupChange::kCreated) {
    // Not a group moved here from another window (a whole Space, which
    // leaves that one): one restored or recreated with a known id (tab
    // restore, Koan's own empty Space), checked in the next update.
    const TabGroupChange::CreateChange* create = change.GetCreateChange();
    if (!create || create->reason() != TabGroupChange::TabGroupCreationReason::
                                           kInsertedFromAnotherTabstrip) {
      resurrected_.insert(change.group);
    }
  }
  if (change.type == TabGroupChange::kVisualsChanged && !writing_space_) {
    // A foreign group whose creator set its looks itself (an extension's
    // tabGroups.update() after tabs.group()) keeps them.
    foreign_groups_.erase(change.group);
  }
  if (change.type == TabGroupChange::kClosed) {
    const bool deleted = deleting_.erase(change.group);
    if (deleted) {
      DeletedSpaces().insert(change.group);
    }
    const TabGroupChange::CloseChange* close = change.GetCloseChange();
    const bool moved_away =
        close &&
        close->reason() ==
            TabGroupChange::TabGroupClosureReason::kDetachedToAnotherTabstrip;
    auto it = std::ranges::find(spaces_snapshot_, change.group,
                                &decltype(spaces_snapshot_)::value_type::first);
    // A session restored into this window (Restore after a crash) closes the
    // tab the window started on: that Space goes, the restored Spaces stay.
    // Zen's restore never adds a Space.
    const bool replaced_by_restore =
        restore_started_ && pre_restore_space_ == change.group;
    if (!deleted && !moved_away && !replaced_by_restore &&
        it != spaces_snapshot_.end()) {
      std::optional<tab_groups::TabGroupId> next;
      if (it + 1 != spaces_snapshot_.end()) {
        next = (it + 1)->first;
      }
      closed_spaces_.push_back(
          {.id = change.group,
           .data = it->second,
           .next = next,
           .was_active = change.model->GetFocusedGroup() == change.group ||
                         (last_space_ == change.group &&
                          !change.model->GetFocusedGroup())});
    }
    last_active_tabs_.erase(change.group);
  }
  if (change.type == TabGroupChange::kVisualsChanged &&
      change.group == GetActiveSpace()) {
    if (header_) {
      std::optional<SpaceInfo> info = GetSpace(change.group);
      header_->Update(info->icon, info->name);
    }
    ApplySpaceColor();
  }
  ScheduleUpdate();
}

void KoanWorkspaceController::OnTabGroupAdded(
    const tab_groups::TabGroupId& group_id) {
  // Upstream's AddToNewGroup() from outside Koan: Chrome's group shortcuts
  // and menus, an extension's tabs.group(), a promoted ephemeral group. It
  // becomes a Space (NormalizeSpaces()).
  if (!creating_space_ && !tab_strip_model()->IsEphemeralTabGroup(group_id)) {
    foreign_groups_.insert(group_id);
    ScheduleUpdate();
  }
}

void KoanWorkspaceController::OnTabGroupFocusChanged(
    std::optional<tab_groups::TabGroupId> new_focused_group_id,
    std::optional<tab_groups::TabGroupId> old_focused_group_id) {
  if (!new_focused_group_id) {
    if (old_focused_group_id) {
      last_space_ = old_focused_group_id;
    }
    ScheduleUpdate();
    return;
  }
  const std::optional<tab_groups::TabGroupId> from =
      old_focused_group_id ? old_focused_group_id : last_space_;
  last_space_ = new_focused_group_id;
  // The header shows the new Space right away, so that it slides in. A
  // rename in progress keeps the name typed so far, as leaving the field
  // does.
  std::optional<SpaceInfo> info = GetSpace(*new_focused_group_id);
  if (header_ && info) {
    header_->CommitRename();
    header_->Update(info->icon, info->name);
  }
  if (from && *from != *new_focused_group_id && GetSpace(*from)) {
    PlaySwitchAnimation(*from, *new_focused_group_id);
    // Screen readers hear where they are.
    if (header_ && info) {
      header_->GetViewAccessibility().AnnouncePolitely(
          base::StrCat({info->name, u" Space"}));
    }
  }
  // (Upstream's own focus-mode colour comes through ApplySpaceColor() too;
  // this covers the order the observers run in.)
  ApplySpaceColor();
  ScheduleUpdate();
}

void KoanWorkspaceController::OnViewIsDeleting(views::View* observed_view) {
  view_observations_.RemoveObservation(observed_view);
  if (observed_view == header_) {
    CancelSwipe(/*animate=*/false);
    header_ = nullptr;
  } else if (observed_view == bar_) {
    bar_ = nullptr;
  } else if (observed_view == creation_form_) {
    creation_form_ = nullptr;
  } else if (observed_view == region_) {
    CancelSwipe(/*animate=*/false);
    region_->RemovePreTargetHandler(wheel_handler_.get());
    region_ = nullptr;
    create_form_lock_.reset();
  }
}

// Tint.

std::optional<SkColor> KoanWorkspaceController::GetSpaceColor() const {
  // With the tint switched off, as for the default theme: the user's colour.
  if (!browser_view_->GetProfile()->GetPrefs()->GetBoolean(
          prefs::kWorkspaceTint)) {
    return std::nullopt;
  }
  std::optional<tab_groups::TabGroupId> space = GetActiveSpace();
  if (!space && last_space_ && GetSpace(*last_space_)) {
    space = last_space_;
  }
  const std::optional<tab_groups::TabGroupColorId> color =
      space ? GetSpaceTheme(*space).color : std::nullopt;
  return color ? std::make_optional(TintSeed(*color)) : std::nullopt;
}

void KoanWorkspaceController::ApplySpaceColor() {
  // A per-window user colour, as upstream's focus mode sets for a focused
  // group: the window's Space tints only this window (Zen's per-window
  // gradient), and the profile's colour and theme stay the user's. None for
  // Zen's default theme, so the user's colour, and an extension theme, show
  // (an override suppresses the theme, BrowserWidget::GetCustomTheme()).
  // (As upstream: browser_widget(), which is null once the window is being
  // deleted, and not a closed one; the hook can run during window close.)
  BrowserWidget* widget = browser_view_->browser_widget();
  if (!widget || widget->IsClosed() || destroying_ || !IsEnabled()) {
    return;
  }
  const std::optional<SkColor> color = GetSpaceColor();
  if (widget->user_color_override() == color) {
    return;
  }
  // The window re-themes itself (Widget::SetUserColorOverride()). Its child
  // widgets take the colour through their parent's key but aren't told
  // (upstream re-themes them only with kThemeChangeOptimization): on macOS
  // the fullscreen overlay with the sidebar and toolbar, compact mode's,
  // Glance, the floating URL bar. A profile colour change used to reach them
  // all through the native theme.
  widget->SetUserColorOverride(color);
  for (views::Widget* child :
       views::Widget::GetAllChildWidgets(widget->GetNativeView())) {
    if (child != widget && !child->IsClosed()) {
      child->ThemeChanged();
    }
  }
}

// Hooks (koan_spaces.h).

tabs::TabInterface* PickSpaceTab(TabStripModel* tab_strip,
                                 const tab_groups::TabGroupId& group) {
  KoanWorkspaceController* spaces =
      KoanWorkspaceController::FromTabStrip(tab_strip);
  tabs::TabInterface* tab = spaces ? spaces->GetLastActiveTab(group) : nullptr;
  // Only one of the group's own tabs (upstream's selection must stay valid).
  return tab && tab->GetGroup() == group &&
                 tab_strip->GetIndexOfTab(tab) != TabStripModel::kNoTab
             ? tab
             : nullptr;
}

bool WrapsSpaces(TabStripModel* tab_strip) {
  return KoanWorkspaceController::FromTabStrip(tab_strip) != nullptr;
}

bool ShowsSpaceHeader(const views::View* tab_group_view) {
  const views::Widget* widget = tab_group_view->GetWidget();
  BrowserView* browser_view = widget
                                  ? BrowserView::GetBrowserViewForNativeWindow(
                                        widget->GetNativeWindow())
                                  : nullptr;
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  return controller && controller->workspaces()->IsEnabled();
}

bool ApplySpaceColor(TabStripModel* tab_strip) {
  KoanWorkspaceController* spaces =
      KoanWorkspaceController::FromTabStrip(tab_strip);
  if (!spaces) {
    return false;
  }
  spaces->ApplySpaceColor();
  return true;
}

bool HasSpaces(TabStripModel* tab_strip) {
  return KoanWorkspaceController::FromTabStrip(tab_strip) != nullptr;
}

bool HandleSpacesCommand(BrowserWindowInterface* browser, int command_id) {
  KoanWorkspaceController* spaces = SpacesOf(browser);
  return spaces && spaces->RunGroupCommand(command_id);
}

bool KeepsOtherSpacesRunning(const tabs::TabInterface* tab) {
  return tab && SpacesOf(tab->GetBrowserWindowInterface()) != nullptr;
}

std::optional<bool> HidesMenuCommand(BrowserView* browser_view,
                                     int command_id) {
  // Every item Koan owns gets an answer both ways: the menu bar is shared by
  // the windows, and AppKit keeps a hidden item hidden until told otherwise.
  switch (command_id) {
    // Upstream's tab group items (Tab > Group Tab, its Option alternate): a
    // tab is always in a Space, "Move To" moves it to another.
    case IDC_WINDOW_GROUP_TAB:
    case IDC_GROUP_TARGET_TAB:
      return browser_view && SpacesOf(browser_view->browser()) != nullptr;
    // View > Always Show Toolbar in Full Screen: with Koan's toolbar hidden
    // (single toolbar) fullscreen reveals nothing at the top edge, whatever
    // the setting (Chromium's, synced) says, so it isn't offered, neither
    // greyed out nor there. (The command itself is ignored there:
    // IgnoresFullscreenToolbarToggle().)
    case IDC_TOGGLE_FULLSCREEN_TOOLBAR:
      return browser_view && IsToolbarHidden(browser_view);
    // View > "Show Tabs Vertically": Koan owns the sidebar, which is always
    // vertical (#154). Upstream's validation sets this item's hidden state
    // itself (hidden only in immersive fullscreen), so a hide at menu build
    // time doesn't hold.
    // (Not IDC_TOGGLE_VERTICAL_TABS_COLLAPSE: AppKit doesn't fire the key
    // equivalent of a hidden item, enabled or not, so hiding it would kill
    // Shift+Cmd+L, which is the sidebar button's shortcut. It stays, and its
    // check mark follows the collapse state, i.e. compact mode hiding the
    // sidebar. Its title is the menu bar's to change.)
    case IDC_TOGGLE_VERTICAL_TABS:
      return OwnsSidebar();
    default:
      return std::nullopt;
  }
}

}  // namespace koan
