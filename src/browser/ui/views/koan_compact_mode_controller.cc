// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_compact_mode_controller.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout_params.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/infobars/infobar_container_view.h"
#include "components/prefs/pref_service.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_content_card.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/animation/tween.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/window/vector_icons/vector_icons.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

namespace koan {

namespace {

constexpr base::TimeDelta kToolbarRevealDuration = base::Milliseconds(160);
constexpr base::TimeDelta kToolbarHideDuration = base::Milliseconds(200);
// How long the pointer must be away before the toolbar hides again.
constexpr base::TimeDelta kToolbarHideDelay = base::Milliseconds(400);
constexpr base::TimeDelta kToolbarHideCheckInterval = base::Milliseconds(100);

// Returns the BrowserView's top-container overlay view: a child of the
// widget's NonClientView, drawn above the client view (and so above the
// page). BrowserView only exposes it on macOS, and its class is private to
// browser_view.cc, so find it by its metadata class name.
views::View* GetTopContainerOverlayView(BrowserView* browser_view) {
  views::Widget* widget = browser_view->GetWidget();
  if (!widget || !widget->non_client_view()) {
    return nullptr;
  }
  for (views::View* child : widget->non_client_view()->children()) {
    if (child->GetClassName() == "TopContainerOverlayView") {
      return child;
    }
  }
  return nullptr;
}

}  // namespace

// An invisible strip along a window edge that reports pointer enter/exit.
// It sits in the content card's gutter, where no native web contents window
// can swallow the events.
class KoanHotZone : public views::View {
  METADATA_HEADER(KoanHotZone, views::View)

 public:
  explicit KoanHotZone(base::RepeatingCallback<void(bool)> on_hover)
      : on_hover_(std::move(on_hover)) {
    GetViewAccessibility().SetIsIgnored(true);
  }

  // views::View:
  void OnMouseEntered(const ui::MouseEvent& event) override {
    on_hover_.Run(true);
  }
  void OnMouseExited(const ui::MouseEvent& event) override {
    on_hover_.Run(false);
  }

 private:
  base::RepeatingCallback<void(bool)> on_hover_;
};

BEGIN_METADATA(KoanHotZone)
END_METADATA

KoanCompactModeController::KoanCompactModeController(BrowserView* browser_view)
    : browser_view_(browser_view),
      toggle_accelerator_(ui::VKEY_C, ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN) {
  pref_change_registrar_.Init(browser_view_->GetProfile()->GetPrefs());
  pref_change_registrar_.Add(
      prefs::kCompactModeEnabled,
      base::BindRepeating(&KoanCompactModeController::UpdateActiveSoon,
                          base::Unretained(this)));
}

KoanCompactModeController::~KoanCompactModeController() {
  if (focus_manager_) {
    focus_manager_->UnregisterAccelerators(this);
    focus_manager_->RemoveFocusChangeListener(this);
  }
}

void KoanCompactModeController::OnAddedToWidget() {
  if (!focus_manager_ && browser_view_->GetFocusManager()) {
    focus_manager_ = browser_view_->GetFocusManager();
    focus_manager_->RegisterAccelerator(
        toggle_accelerator_, ui::AcceleratorManager::kNormalPriority, this);
    focus_manager_->AddFocusChangeListener(this);
  }
  UpdateActiveSoon();
}

void KoanCompactModeController::OnBrowserViewLayout() {
  // Fullscreen and tab strip mode changes show up as layouts. Re-evaluate
  // outside of layout, since (de)activation reparents views.
  if (active_ != ShouldBeActive()) {
    UpdateActiveSoon();
  }
  if (active_) {
    LayoutHotZones();
    UpdateWindowControls();
    // The top container's height may have changed (e.g. bookmarks bar).
    if (!toolbar_revealed_) {
      ApplyToolbarTransform(/*animate=*/false);
    }
  }
}

void KoanCompactModeController::OnBrowserViewDestroying() {
  if (active_) {
    Deactivate();
  }
  if (focus_manager_) {
    focus_manager_->UnregisterAccelerators(this);
    focus_manager_->RemoveFocusChangeListener(this);
    focus_manager_ = nullptr;
  }
}

void KoanCompactModeController::Toggle() {
  PrefService* prefs = browser_view_->GetProfile()->GetPrefs();
  prefs->SetBoolean(prefs::kCompactModeEnabled,
                    !prefs->GetBoolean(prefs::kCompactModeEnabled));
}

bool KoanCompactModeController::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (accelerator != toggle_accelerator_) {
    return false;
  }
  Toggle();
  return true;
}

bool KoanCompactModeController::CanHandleAccelerators() const {
  return features::IsCompactModeEnabled() && browser_view_->GetIsNormalType();
}

void KoanCompactModeController::OnDidChangeFocus(views::View* focused_before,
                                                 views::View* focused_now) {
  // Focusing anything in the toolbar (e.g. Ctrl+L) reveals it; the hide
  // timer keeps it revealed until focus leaves.
  if (active_ && focused_now &&
      browser_view_->top_container()->Contains(focused_now)) {
    SetToolbarRevealed(true, /*animate=*/true);
  }
}

bool KoanCompactModeController::ShouldBeActive() const {
  return features::IsCompactModeEnabled() &&
         browser_view_->GetProfile()->GetPrefs()->GetBoolean(
             prefs::kCompactModeEnabled) &&
         GetTopContainerOverlayView(browser_view_) &&
         UseContentCard(browser_view_);
}

void KoanCompactModeController::UpdateActiveSoon() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanCompactModeController::UpdateActive,
                                weak_factory_.GetWeakPtr()));
}

void KoanCompactModeController::UpdateActive() {
  const bool should_be_active = ShouldBeActive();
  if (should_be_active == active_) {
    return;
  }
  if (should_be_active) {
    Activate();
  } else {
    Deactivate();
  }
}

void KoanCompactModeController::Activate() {
  active_ = true;
  VLOG(1) << "Koan: compact mode on";

  // Sidebar: collapse it; the layout then gives it zero width and upstream's
  // expand-on-hover draws it over the page.
  if (auto* state = tabs::VerticalTabStripStateController::From(
          browser_view_->browser())) {
    collapsed_before_ = state->IsCollapsed();
    if (!state->IsCollapsed()) {
      state->RequestCollapse(true);
    }
  }

  // Toolbar: move the top container into the overlay view, above the page.
  // Mirrors BrowserView::ReparentTopContainerForStartOfImmersive(); the
  // layout positions a top container that isn't parented to the BrowserView.
  views::View* top_container = browser_view_->top_container();
  views::View* overlay = GetTopContainerOverlayView(browser_view_);
  if (top_container->parent() == browser_view_) {
    top_container_index_ = browser_view_->GetIndexOf(top_container);
    top_container->SetPaintToLayer();
    top_container->layer()->SetFillsBoundsOpaquely(false);
    top_container->SetProperty(views::kViewDoesNotLayOutChildren, false);
    overlay->AddChildView(top_container);
    overlay->SetVisible(true);
  }
  SetToolbarRevealed(false, /*animate=*/false);

  // Edge hot zones, below the tab strip so the expanded strip gets the
  // pointer, above the page.
  std::optional<size_t> index;
  if (auto* tab_strip = browser_view_->tab_strip_view()) {
    index = browser_view_->GetIndexOf(tab_strip);
  }
  const size_t insert_at = index.value_or(browser_view_->children().size());
  sidebar_hot_zone_ = browser_view_->AddChildViewAt(
      std::make_unique<KoanHotZone>(
          base::BindRepeating(&KoanCompactModeController::OnSidebarHotZoneHovered,
                              base::Unretained(this))),
      insert_at);
  toolbar_hot_zone_ = browser_view_->AddChildViewAt(
      std::make_unique<KoanHotZone>(
          base::BindRepeating(&KoanCompactModeController::OnToolbarHotZoneHovered,
                              base::Unretained(this))),
      insert_at);

  browser_view_->InvalidateLayout();
  LayoutHotZones();
  UpdateWindowControls();
}

void KoanCompactModeController::UpdateWindowControls() {
  views::View* top_container = browser_view_->top_container();
  const BrowserFrameView* frame =
      browser_view_->browser_widget()
          ? browser_view_->browser_widget()->GetFrameView()
          : nullptr;
  const BrowserLayoutParams params =
      frame ? frame->GetBrowserLayoutParams() : BrowserLayoutParams();
  const gfx::SizeF area = params.trailing_exclusion.content;
  if (!active_ || area.IsEmpty()) {
    if (window_controls_) {
      top_container->RemoveChildViewT(std::exchange(window_controls_, nullptr));
    }
    return;
  }
  if (!window_controls_) {
    auto controls = std::make_unique<views::BoxLayoutView>();
    controls->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    views::Widget* widget = browser_view_->GetWidget();
    const auto add = [&](const gfx::VectorIcon& icon, std::u16string name,
                         base::RepeatingClosure action) {
      auto button = views::CreateVectorImageButtonWithNativeTheme(
          base::BindRepeating(
              [](base::RepeatingClosure action, const ui::Event&) {
                action.Run();
              },
              std::move(action)),
          icon, /*dip_size=*/16);
      button->SetTooltipText(name);
      button->GetViewAccessibility().SetName(name);
      controls->AddChildView(std::move(button));
    };
    add(views::kChromeMinimizeIcon, u"Minimize",
        base::BindRepeating(&views::Widget::Minimize,
                            base::Unretained(widget)));
    add(views::kChromeMaximizeIcon, u"Maximize",
        base::BindRepeating(
            [](views::Widget* w) {
              w->IsMaximized() ? w->Restore() : w->Maximize();
            },
            base::Unretained(widget)));
    add(vector_icons::kCloseIcon, u"Close",
        base::BindRepeating(&views::Widget::Close, base::Unretained(widget)));
    window_controls_ = top_container->AddChildView(std::move(controls));
  }
  const int width = static_cast<int>(area.width());
  const int height = static_cast<int>(area.height());
  for (views::View* button : window_controls_->children()) {
    button->SetPreferredSize(
        gfx::Size(width / static_cast<int>(window_controls_->children().size()),
                  height));
  }
  window_controls_->SetBounds(top_container->width() - width, 0, width, height);
}

void KoanCompactModeController::Deactivate() {
  active_ = false;
  VLOG(1) << "Koan: compact mode off";

  toolbar_hide_timer_.Stop();
  toolbar_unwanted_since_.reset();
  sidebar_lock_.reset();
  if (window_controls_) {
    browser_view_->top_container()->RemoveChildViewT(
        std::exchange(window_controls_, nullptr));
  }
  if (sidebar_hot_zone_) {
    browser_view_->RemoveChildViewT(std::exchange(sidebar_hot_zone_, nullptr));
  }
  if (toolbar_hot_zone_) {
    browser_view_->RemoveChildViewT(std::exchange(toolbar_hot_zone_, nullptr));
  }

  // Mirrors BrowserView::ReparentTopContainerForEndOfImmersive().
  views::View* top_container = browser_view_->top_container();
  if (top_container->parent() != browser_view_ && top_container_index_) {
    if (top_container->layer()) {
      top_container->layer()->GetAnimator()->AbortAllAnimations();
    }
    if (views::View* overlay = GetTopContainerOverlayView(browser_view_)) {
      overlay->SetVisible(false);
    }
    top_container->SetCanProcessEventsWithinSubtree(true);
    top_container->DestroyLayer();
    browser_view_->AddChildViewAt(
        top_container,
        std::min(*top_container_index_, browser_view_->children().size()));
    browser_view_->infobar_container()->InsertAfterInFocusList(top_container);
    top_container->SetProperty(views::kViewDoesNotLayOutChildren, true);
  }
  top_container_index_.reset();
  toolbar_revealed_ = false;

  if (auto* state = tabs::VerticalTabStripStateController::From(
          browser_view_->browser())) {
    if (collapsed_before_.has_value() && !*collapsed_before_ &&
        state->IsCollapsed()) {
      state->RequestCollapse(false);
    }
  }
  collapsed_before_.reset();

  browser_view_->InvalidateLayout();
}

void KoanCompactModeController::SetToolbarRevealed(bool revealed,
                                                   bool animate) {
  if (!active_) {
    return;
  }
  const bool changed = toolbar_revealed_ != revealed;
  toolbar_revealed_ = revealed;
  browser_view_->top_container()->SetCanProcessEventsWithinSubtree(revealed);
  if (changed || !animate) {
    ApplyToolbarTransform(animate);
  }
  if (revealed) {
    toolbar_unwanted_since_.reset();
    if (!toolbar_hide_timer_.IsRunning()) {
      toolbar_hide_timer_.Start(FROM_HERE, kToolbarHideCheckInterval, this,
                                &KoanCompactModeController::CheckToolbarHide);
    }
  } else {
    toolbar_hide_timer_.Stop();
  }
}

void KoanCompactModeController::ApplyToolbarTransform(bool animate) {
  views::View* top_container = browser_view_->top_container();
  if (!top_container->layer()) {
    return;
  }
  gfx::Transform target;
  if (!toolbar_revealed_) {
    // Slide fully above the window, including any drop shadow.
    target.Translate(0, -top_container->height());
  }
  if (!animate) {
    if (top_container->layer()->GetAnimator()->is_animating()) {
      return;
    }
    top_container->layer()->SetTransform(target);
    return;
  }
  // Compositor-driven, so the slide stays smooth even if the UI thread is
  // busy.
  views::AnimationBuilder()
      .SetPreemptionStrategy(
          ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET)
      .Once()
      .SetDuration(toolbar_revealed_ ? kToolbarRevealDuration
                                     : kToolbarHideDuration)
      .SetTransform(top_container, target,
                    toolbar_revealed_ ? gfx::Tween::EASE_OUT
                                      : gfx::Tween::EASE_IN);
}

bool KoanCompactModeController::ShouldKeepToolbarRevealed() const {
  views::View* top_container = browser_view_->top_container();
  if (top_container->IsMouseHovered() ||
      (toolbar_hot_zone_ && toolbar_hot_zone_->IsMouseHovered())) {
    return true;
  }
  // Keep it while something in it has focus (omnibox editing) or a menu
  // (app menu, omnibox context menu, ...) is open.
  if (focus_manager_ && focus_manager_->GetFocusedView() &&
      top_container->Contains(focus_manager_->GetFocusedView())) {
    return true;
  }
  return views::MenuController::GetActiveInstance() != nullptr;
}

void KoanCompactModeController::CheckToolbarHide() {
  if (ShouldKeepToolbarRevealed()) {
    toolbar_unwanted_since_.reset();
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  if (!toolbar_unwanted_since_) {
    toolbar_unwanted_since_ = now;
    return;
  }
  if (now - *toolbar_unwanted_since_ >= kToolbarHideDelay) {
    SetToolbarRevealed(false, /*animate=*/true);
  }
}

void KoanCompactModeController::OnSidebarHotZoneHovered(bool hovered) {
  if (!active_) {
    return;
  }
  if (hovered) {
    if (!sidebar_lock_) {
      if (auto* tab_strip = browser_view_->tab_strip_view()) {
        sidebar_lock_ =
            tab_strip->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepExpanded);
      }
    }
    return;
  }
  // Once the strip has slid over the hot zone the pointer is inside the
  // strip, and upstream's hover tracking keeps it open until it leaves.
  sidebar_lock_.reset();
}

void KoanCompactModeController::OnToolbarHotZoneHovered(bool hovered) {
  if (hovered) {
    SetToolbarRevealed(true, /*animate=*/true);
  }
}

void KoanCompactModeController::LayoutHotZones() {
  if (!sidebar_hot_zone_ || !toolbar_hot_zone_) {
    return;
  }
  const gfx::Rect bounds = browser_view_->GetLocalBounds();
  const int gutter = kContentCardMargin;
  sidebar_hot_zone_->SetBoundsRect(
      gfx::Rect(bounds.x(), bounds.y(), gutter, bounds.height()));
  toolbar_hot_zone_->SetBoundsRect(
      gfx::Rect(bounds.x(), bounds.y(), bounds.width(), gutter));
}

}  // namespace koan
