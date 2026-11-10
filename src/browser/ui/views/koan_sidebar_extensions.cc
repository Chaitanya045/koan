// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_sidebar_extensions.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/extensions/extension_context_menu_model.h"
#include "chrome/browser/ui/extensions/extensions_container.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/toolbar_action_view_model.h"
#include "chrome/browser/ui/views/extensions/extension_context_menu_controller.h"
#include "chrome/browser/ui/views/extensions/extensions_container_views.h"
#include "chrome/browser/ui/views/extensions/extensions_menu_coordinator.h"
#include "chrome/browser/ui/views/extensions/extensions_menu_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "extensions/common/extension_features.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_extensions_hooks.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_sidebar_nav_view.h"
#include "koan/browser/ui/views/koan_url_bar.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/event_handler.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/controls/button/menu_button_controller.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace koan {

namespace {

// Zen's zen.view.overflow-webext-toolbar-threshold: the extensions take at
// most this share of the URL bar.
constexpr int kOverflowThresholdPercent = 55;

// Zen's fade (zen-omnibox.css: opacity 0.15s).
constexpr base::TimeDelta kFadeDuration = base::Milliseconds(150);

// After a click, while the popup is on its way (the toolbar container may
// first pop the extension out), the button stays for its popup to point at.
constexpr base::TimeDelta kPressHold = base::Seconds(1);

// How often to look whether the popup that keeps the buttons shown is gone.
constexpr base::TimeDelta kPopupPollInterval = base::Milliseconds(200);

const gfx::Size& ButtonSize() {
  static const gfx::Size size(kUrlPillButtonSize, kUrlPillButtonSize);
  return size;
}

}  // namespace

// One extension's button: upstream's ToolbarActionView in the pill's look,
// driving the same view model.
class KoanSidebarExtensionsView::Button
    : public views::MenuButton,
      public views::ContextMenuController,
      public ExtensionContextMenuController::Observer {
  METADATA_HEADER(Button, views::MenuButton)

 public:
  Button(KoanSidebarExtensionsView* owner,
         ToolbarActionsModel::ActionId action_id)
      : owner_(owner), action_id_(std::move(action_id)) {
    SetCallback(base::BindRepeating(&KoanSidebarExtensionsView::OnButtonPressed,
                                    base::Unretained(owner_),
                                    base::Unretained(this)));
    // Like ToolbarActionView: on release, so a drag doesn't open the popup.
    button_controller()->set_notify_action(
        views::ButtonController::NotifyAction::kOnRelease);
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    SetBorder(views::CreateEmptyBorder(gfx::Insets()));
    SetPreferredSize(ButtonSize());
    SetHasInkDropActionOnClick(true);
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::ON);
    views::InkDrop::Get(this)->SetBaseColor(kColorKoanUrlPillForeground);
    views::InstallCircleHighlightPathGenerator(this);
    set_context_menu_controller(this);
  }
  Button(const Button&) = delete;
  Button& operator=(const Button&) = delete;
  ~Button() override { set_context_menu_controller(nullptr); }

  const ToolbarActionsModel::ActionId& action_id() const { return action_id_; }

  bool IsMenuRunning() const { return menu_ && menu_->IsMenuRunning(); }

  // The extension is going away: its context menu (which its view model
  // owns) closes.
  void ResetMenu() { menu_.reset(); }

  void Update(ToolbarActionViewModel* model, content::WebContents* contents) {
    if (model != observed_model_) {
      observed_model_ = model;
      icon_subscription_ = model->RegisterIconUpdateObserver(
          base::BindRepeating(&KoanSidebarExtensionsView::UpdateButtons,
                              owner_->weak_factory_.GetWeakPtr()));
    }
    SetImageModel(views::Button::STATE_NORMAL,
                  model->GetIcon(contents, ButtonSize()));
    SetTooltipText(model->GetTooltip(contents));
    GetViewAccessibility().SetName(model->GetAccessibleName(contents));
  }

  // views::ContextMenuController: Chromium's menu for the extension.
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override {
    ToolbarActionViewModel* model = owner_->GetModel(action_id_);
    if (!model || IsMenuRunning()) {
      return;
    }
    menu_ = std::make_unique<ExtensionContextMenuController>(
        model, this,
        extensions::ExtensionContextMenuModel::ContextMenuSource::
            kToolbarAction);
    menu_->ShowContextMenuForView(source, point, source_type);
  }

  // ExtensionContextMenuController::Observer:
  void OnContextMenuShown() override { owner_->UpdateReveal(); }
  void OnContextMenuClosed() override { owner_->UpdateReveal(); }

 private:
  const raw_ptr<KoanSidebarExtensionsView> owner_;
  const ToolbarActionsModel::ActionId action_id_;
  // Only to tell whether the subscription is for the current model; never
  // dereferenced (the container owns the models).
  raw_ptr<const ToolbarActionViewModel, DisableDanglingPtrDetection>
      observed_model_ = nullptr;
  base::CallbackListSubscription icon_subscription_;
  std::unique_ptr<ExtensionContextMenuController> menu_;
};

BEGIN_METADATA(KoanSidebarExtensionsView, Button)
END_METADATA

// The sidebar region's pointer enter and exit (they come to the region, not
// its children), like Zen's toolbox hover.
class KoanSidebarExtensionsView::SidebarHoverHandler : public ui::EventHandler {
 public:
  SidebarHoverHandler(KoanSidebarExtensionsView* owner, views::View* region)
      : owner_(owner), region_(region) {
    region_->AddPreTargetHandler(this);
  }
  SidebarHoverHandler(const SidebarHoverHandler&) = delete;
  SidebarHoverHandler& operator=(const SidebarHoverHandler&) = delete;
  ~SidebarHoverHandler() override { region_->RemovePreTargetHandler(this); }

  // ui::EventHandler:
  void OnMouseEvent(ui::MouseEvent* event) override {
    if (event->target() != region_.get()) {
      return;
    }
    if (event->type() == ui::EventType::kMouseEntered) {
      owner_->OnSidebarHovered(true);
    } else if (event->type() == ui::EventType::kMouseExited) {
      owner_->OnSidebarHovered(false);
    }
  }

 private:
  const raw_ptr<KoanSidebarExtensionsView> owner_;
  const raw_ptr<views::View> region_;
};

KoanSidebarExtensionsView::KoanSidebarExtensionsView(BrowserView* browser_view)
    : browser_view_(browser_view) {
  // Hidden until the sidebar is hovered (and there is something to show).
  SetVisible(false);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetOpacity(0.0f);
  if (ToolbarActionsModel* model =
          ToolbarActionsModel::Get(browser_view_->GetProfile())) {
    model_observation_.Observe(model);
  }
  browser_view_->browser()->GetTabStripModel()->AddObserver(this);
  Rebuild();
}

KoanSidebarExtensionsView::~KoanSidebarExtensionsView() {
  browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
  hover_handler_.reset();
}

views::View* KoanSidebarExtensionsView::GetButtonForAction(
    std::string_view action_id) const {
  if (!revealed_) {
    return nullptr;
  }
  for (size_t i = 0; i < buttons_.size() && i < fitting_; ++i) {
    Button* button = buttons_[i];
    if (button->action_id() == action_id && button->IsDrawn() &&
        !button->GetVisibleBounds().IsEmpty()) {
      return button;
    }
  }
  return nullptr;
}

base::WeakPtr<KoanSidebarExtensionsView>
KoanSidebarExtensionsView::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void KoanSidebarExtensionsView::ShowExtensionsMenu() {
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  KoanSidebarNavView* nav =
      controller ? controller->compact_mode()->sidebar_nav() : nullptr;
  views::View* anchor = nav ? nav->bubble_anchor() : nullptr;
  BrowserWindowInterface* browser = browser_view_->browser();
  ExtensionsContainer* container = GetContainer();
  ExtensionsContainerViews* container_views =
      browser_view_->toolbar_button_provider()
          ? browser_view_->toolbar_button_provider()
                ->GetExtensionsContainerViews()
          : nullptr;
  if (!anchor || !anchor->GetWidget() || !container || !container_views) {
    return;
  }
  // The menu points at the peeked sidebar (compact mode): keep it out.
  controller->compact_mode()->HoldSidebarForBubble();
  // Upstream's two menus, as the puzzle button shows them
  // (ExtensionsToolbarButton::ToggleExtensionsMenu()), at `anchor`.
  views::Widget* menu = nullptr;
  if (base::FeatureList::IsEnabled(
          extensions_features::kExtensionsMenuAccessControl)) {
    if (!menu_coordinator_) {
      menu_coordinator_ =
          std::make_unique<ExtensionsMenuCoordinator>(browser, container);
    }
    if (menu_coordinator_->IsShowing()) {
      menu_coordinator_->Hide();
    }
    menu_coordinator_->Show(views::BubbleAnchor(anchor), container_views);
    menu = menu_coordinator_->GetExtensionsMenuWidget();
  } else {
    if (ExtensionsMenuView::IsShowing()) {
      ExtensionsMenuView::Hide();
    }
    menu = ExtensionsMenuView::ShowBubble(views::BubbleAnchor(anchor), browser,
                                          container, container_views);
  }
  // Into the window from the sidebar's edge, not out of it.
  views::BubbleDialogDelegate* bubble =
      menu && menu->widget_delegate()
          ? menu->widget_delegate()->AsBubbleDialogDelegate()
          : nullptr;
  if (bubble) {
    bubble->SetArrow(IsSidebarOnRight(anchor) ? views::BubbleBorder::TOP_RIGHT
                                              : views::BubbleBorder::TOP_LEFT);
  }
}

void KoanSidebarExtensionsView::AddedToWidget() {
  // The sidebar: the vertical tab strip region this view is in. Its pointer
  // enter and exit come to it (not its children), like Zen's toolbox hover.
  views::View* region = browser_view_->tab_strip_view();
  if (region && region->Contains(this) && region_ != region) {
    hover_handler_.reset();
    region_ = region;
    hover_handler_ = std::make_unique<SidebarHoverHandler>(this, region);
    sidebar_hovered_ = region_->IsMouseHovered();
  }
  if (parent()) {
    parent_observation_.Reset();
    parent_observation_.Observe(parent());
  }
  UpdateFit();
}

void KoanSidebarExtensionsView::RemovedFromWidget() {
  hover_handler_.reset();
  region_ = nullptr;
  parent_observation_.Reset();
  sidebar_hovered_ = false;
}

gfx::Size KoanSidebarExtensionsView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(static_cast<int>(fitting_) * ButtonSize().width(),
                   ButtonSize().height());
}

void KoanSidebarExtensionsView::Layout(PassKey) {
  int x = 0;
  for (size_t i = 0; i < buttons_.size(); ++i) {
    Button* button = buttons_[i];
    const bool fits = i < fitting_;
    button->SetVisible(fits);
    if (fits) {
      button->SetBounds(x, (height() - ButtonSize().height()) / 2,
                        ButtonSize().width(), ButtonSize().height());
      x += ButtonSize().width();
    }
  }
}

void KoanSidebarExtensionsView::OnToolbarActionAdded(
    const ToolbarActionsModel::ActionId& action_id) {
  ScheduleRebuild();
}

void KoanSidebarExtensionsView::OnToolbarActionRemoved(
    const ToolbarActionsModel::ActionId& action_id) {
  // Now, before the container lets go of the view model the menu uses.
  for (Button* button : buttons_) {
    if (button->action_id() == action_id) {
      button->ResetMenu();
    }
  }
  ScheduleRebuild();
}

void KoanSidebarExtensionsView::OnToolbarActionUpdated(
    const ToolbarActionsModel::ActionId& action_id) {
  UpdateButtons();
}

void KoanSidebarExtensionsView::OnToolbarModelInitialized() {
  ScheduleRebuild();
}

void KoanSidebarExtensionsView::OnToolbarPinnedActionsChanged() {
  ScheduleRebuild();
}

void KoanSidebarExtensionsView::OnToolbarActionsModelShutdown() {
  model_observation_.Reset();
  for (Button* button : buttons_) {
    button->ResetMenu();
  }
  buttons_.clear();
  RemoveAllChildViews();
  UpdateFit();
}

void KoanSidebarExtensionsView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    UpdateButtons();
  }
}

void KoanSidebarExtensionsView::OnSidebarHovered(bool hovered) {
  sidebar_hovered_ = hovered;
  UpdateReveal();
}

void KoanSidebarExtensionsView::OnViewBoundsChanged(
    views::View* observed_view) {
  UpdateFit();
}

ExtensionsContainer* KoanSidebarExtensionsView::GetContainer() const {
  return ExtensionsContainer::From(*browser_view_->browser());
}

content::WebContents* KoanSidebarExtensionsView::GetActiveContents() const {
  return browser_view_->browser()->GetTabStripModel()->GetActiveWebContents();
}

ToolbarActionViewModel* KoanSidebarExtensionsView::GetModel(
    const std::string& action_id) const {
  ExtensionsContainer* container = GetContainer();
  return container ? container->GetActionForId(action_id) : nullptr;
}

void KoanSidebarExtensionsView::ScheduleRebuild() {
  if (rebuild_pending_) {
    return;
  }
  // The container makes and drops its view models in its own observer of the
  // same change, which may run after this one.
  rebuild_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanSidebarExtensionsView::Rebuild,
                                weak_factory_.GetWeakPtr()));
}

void KoanSidebarExtensionsView::Rebuild() {
  rebuild_pending_ = false;
  for (Button* button : buttons_) {
    button->ResetMenu();
  }
  buttons_.clear();
  RemoveAllChildViews();
  const ToolbarActionsModel* model = model_observation_.IsObserving()
                                         ? model_observation_.GetSource()
                                         : nullptr;
  if (model && model->actions_initialized()) {
    // In the toolbar's order (the user's).
    for (const ToolbarActionsModel::ActionId& id : model->pinned_action_ids()) {
      if (GetModel(id)) {
        buttons_.push_back(AddChildView(std::make_unique<Button>(this, id)));
      }
    }
  }
  UpdateButtons();
  UpdateFit();
}

void KoanSidebarExtensionsView::UpdateButtons() {
  content::WebContents* contents = GetActiveContents();
  for (Button* button : buttons_) {
    if (ToolbarActionViewModel* model = GetModel(button->action_id())) {
      button->Update(model, contents);
    }
  }
}

void KoanSidebarExtensionsView::UpdateFit() {
  const int room =
      parent() ? parent()->width() * kOverflowThresholdPercent / 100 : 0;
  const size_t fitting =
      std::min(buttons_.size(),
               static_cast<size_t>(std::max(0, room / ButtonSize().width())));
  if (fitting != fitting_) {
    fitting_ = fitting;
    PreferredSizeChanged();
    InvalidateLayout();
  }
  UpdateReveal();
}

bool KoanSidebarExtensionsView::ShouldReveal() const {
  if (fitting_ == 0) {
    return false;
  }
  if (sidebar_hovered_ || base::TimeTicks::Now() < press_hold_until_) {
    return true;
  }
  for (Button* button : buttons_) {
    if (button->HasFocus() || button->IsMenuRunning()) {
      return true;
    }
    if (ToolbarActionViewModel* model = GetModel(button->action_id());
        model && model->IsShowingPopup()) {
      return true;
    }
  }
  return false;
}

void KoanSidebarExtensionsView::UpdateReveal() {
  const bool reveal = ShouldReveal();
  // Kept by a popup or a click (not the pointer): look again until it's gone.
  if (reveal && !sidebar_hovered_) {
    if (!popup_timer_.IsRunning()) {
      popup_timer_.Start(FROM_HERE, kPopupPollInterval, this,
                         &KoanSidebarExtensionsView::UpdateReveal);
    }
  } else {
    popup_timer_.Stop();
  }
  if (reveal == revealed_) {
    return;
  }
  revealed_ = reveal;
  layer()->GetAnimator()->AbortAllAnimations();
  if (!gfx::Animation::ShouldRenderRichAnimation()) {
    layer()->SetOpacity(reveal ? 1.0f : 0.0f);
    SetVisible(reveal);
    return;
  }
  if (reveal) {
    SetVisible(true);
    views::AnimationBuilder()
        .Once()
        .SetDuration(kFadeDuration)
        .SetOpacity(this, 1.0f, gfx::Tween::LINEAR);
    return;
  }
  // Zen: the fade, then the room goes.
  views::AnimationBuilder()
      .OnEnded(base::BindOnce(
          [](base::WeakPtr<KoanSidebarExtensionsView> view) {
            if (view && !view->revealed_) {
              view->SetVisible(false);
            }
          },
          weak_factory_.GetWeakPtr()))
      .Once()
      .SetDuration(kFadeDuration)
      .SetOpacity(this, 0.0f, gfx::Tween::LINEAR);
}

void KoanSidebarExtensionsView::OnButtonPressed(Button* button) {
  ToolbarActionViewModel* model = GetModel(button->action_id());
  if (!model) {
    return;
  }
  // The popup points at the button: keep it (and, in compact mode, the
  // peeked sidebar) there while it opens.
  press_hold_until_ = base::TimeTicks::Now() + kPressHold;
  UpdateReveal();
  if (KoanBrowserViewController* controller =
          KoanBrowserViewController::From(browser_view_)) {
    controller->compact_mode()->HoldSidebarForBubble();
  }
  // As ToolbarActionView::ButtonPressed(): an extension that can't run here
  // shows its menu.
  if (model->IsEnabled(GetActiveContents())) {
    model->ExecuteUserAction(
        ToolbarActionViewModel::InvocationSource::kToolbarButton);
  } else {
    button->ShowContextMenuForView(button,
                                   button->GetKeyboardContextMenuLocation(),
                                   ui::mojom::MenuSourceType::kNone);
  }
}

BEGIN_METADATA(KoanSidebarExtensionsView)
END_METADATA

views::View* GetExtensionsAnchorView(const views::View* container,
                                     std::string_view action_id) {
  // Also where the toolbar is there (null), and to have compact mode look
  // at what is open.
  views::View* fallback = GetBubbleAnchorView(container);
  if (!fallback || action_id.empty() || !container->GetWidget()) {
    return fallback;
  }
  auto* browser_view =
      views::AsViewClass<BrowserView>(container->GetWidget()->client_view());
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  KoanSidebarNavView* nav =
      controller ? controller->compact_mode()->sidebar_nav() : nullptr;
  views::View* button = nav && nav->extensions()
                            ? nav->extensions()->GetButtonForAction(action_id)
                            : nullptr;
  return button ? button : fallback;
}

}  // namespace koan
