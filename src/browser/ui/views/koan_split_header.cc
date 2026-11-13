// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_split_header.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/cursor/mojom/cursor_type.mojom-shared.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/rrect_f.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/event_monitor.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// zen-split-view.css, in Zen's chrome rem on macOS (11px): two 16 px
// buttons, 0.8rem apart, in 0.4rem 0.6rem 0.2rem of padding (54 x 22.6), the
// grip glyph 14 px and turned a quarter, the unsplit glyph 10 px.
constexpr int kButtonSize = 16;
constexpr int kGripGlyphSize = 14;
constexpr int kUnsplitGlyphSize = 10;
constexpr int kPaddingTop = 4;
constexpr int kPaddingBottom = 3;
constexpr int kPaddingSide = 7;
constexpr int kButtonGap = 8;
constexpr int kHeaderWidth = 2 * kPaddingSide + 2 * kButtonSize + kButtonGap;
constexpr int kHeaderHeight = kPaddingTop + kButtonSize + kPaddingBottom;
// border-radius 6px on the bottom corners only.
constexpr float kHeaderRadius = 6;
// top: calc(var(--zen-split-column-gap) / -2): the header starts in the gap
// above the pane, (8 + 1) / 2 px up.
constexpr int kHeaderRise = 5;
// box-shadow: 0 0 5px 1px: a 5 px blur (gfx's blur is twice CSS's) around
// the header grown by 1 px.
constexpr int kShadowBlur = 10;
constexpr int kShadowSpread = 1;
constexpr int kShadowMargin = kShadowBlur / 2 + kShadowSpread;
// opacity transition 0.1s, and transition-delay 0.1s on hover.
constexpr base::TimeDelta kFadeDuration = base::Milliseconds(100);
constexpr base::TimeDelta kFadeInDelay = base::Milliseconds(100);

// The rearrange view: the browsers fade (transition: opacity 0.2s ease-out)
// to .85, the dragged one to .2; the drop zone moves with
// transition: inset ease-out 0.08s; the drag image is a 160 x 90 thumbnail
// with the pointer 16 px into it.
constexpr base::TimeDelta kDimDuration = base::Milliseconds(200);
constexpr base::TimeDelta kDropZoneDuration = base::Milliseconds(80);
constexpr float kDraggedOpacity = 0.2f;
constexpr float kOtherOpacity = 0.85f;
constexpr gfx::Size kDragImageSize(160, 90);
constexpr int kDragImageOffset = 16;

bool RichAnimation() {
  return gfx::Animation::ShouldRenderRichAnimation();
}

// One of the header's two buttons.
class KoanSplitHeaderButton : public views::View {
  METADATA_HEADER(KoanSplitHeaderButton, views::View)

 public:
  enum class Kind { kRearrange, kUnsplit };

  KoanSplitHeaderButton(KoanSplitHeaderController* controller,
                        int pane,
                        Kind kind)
      : controller_(controller), pane_(pane), kind_(kind) {
    // The page keeps the focus; the buttons are for the pointer, and for
    // screen readers through their actions.
    SetFocusBehavior(FocusBehavior::NEVER);
    GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
    GetViewAccessibility().SetName(
        kind_ == Kind::kRearrange ? u"Rearrange split view" : u"Split Out Tab");
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kButtonSize, kButtonSize);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const SkColor color =
        GetColorProvider()->GetColor(kColorKoanSplitHeaderIcon);
    const gfx::Point center = GetLocalBounds().CenterPoint();
    if (kind_ == Kind::kUnsplit) {
      gfx::ScopedCanvas scoped(canvas);
      canvas->Translate(gfx::Vector2d(center.x() - kUnsplitGlyphSize / 2,
                                      center.y() - kUnsplitGlyphSize / 2));
      gfx::PaintVectorIcon(canvas, kKoanFullscreenIcon, kUnsplitGlyphSize,
                           color);
      return;
    }
    // Zen's drag-indicator turned 90 degrees: the grip lies across.
    gfx::ScopedCanvas scoped(canvas);
    canvas->Translate(gfx::Vector2d(center.x(), center.y()));
    canvas->sk_canvas()->rotate(90);
    canvas->Translate(gfx::Vector2d(-kGripGlyphSize / 2, -kGripGlyphSize / 2));
    gfx::PaintVectorIcon(canvas, kKoanDragIndicatorIcon, kGripGlyphSize, color);
  }

  ui::Cursor GetCursor(const ui::MouseEvent& event) override {
    // cursor: pointer, and move on the grip.
    return ui::Cursor(kind_ == Kind::kRearrange ? ui::mojom::CursorType::kMove
                                                : ui::mojom::CursorType::kHand);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!event.IsOnlyLeftMouseButton()) {
      return false;
    }
    press_ = event.location();
    dragging_ = false;
    return true;
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    if (kind_ != Kind::kRearrange) {
      return true;
    }
    if (!dragging_) {
      if (!ExceededDragThreshold(event.location() - press_)) {
        return true;
      }
      dragging_ = true;
      controller_->StartRearrange(pane_);
    }
    controller_->UpdateRearrange(ScreenPoint(event));
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (kind_ == Kind::kRearrange) {
      if (dragging_) {
        dragging_ = false;
        controller_->FinishRearrange(ScreenPoint(event));
      }
      return;
    }
    if (event.IsLeftMouseButton() && HitTestPoint(event.location())) {
      controller_->OnUnsplitPressed(pane_, event.IsShiftDown());
    }
  }

  void OnMouseCaptureLost() override {
    if (dragging_) {
      dragging_ = false;
      controller_->CancelRearrange();
    }
  }

  bool HandleAccessibleAction(const ui::AXActionData& action_data) override {
    if (action_data.action != ax::mojom::Action::kDoDefault ||
        kind_ != Kind::kUnsplit) {
      return views::View::HandleAccessibleAction(action_data);
    }
    controller_->OnUnsplitPressed(pane_, /*keep_selection=*/false);
    return true;
  }

 private:
  gfx::Point ScreenPoint(const ui::MouseEvent& event) const {
    gfx::Point point = event.location();
    ConvertPointToScreen(this, &point);
    return point;
  }

  const raw_ptr<KoanSplitHeaderController> controller_;
  const int pane_;
  const Kind kind_;
  gfx::Point press_;
  bool dragging_ = false;
};

BEGIN_METADATA(KoanSplitHeaderButton)
END_METADATA

}  // namespace

// The header's widget contents: the shadow, the tab and its buttons.
class KoanSplitHeaderView : public views::View {
  METADATA_HEADER(KoanSplitHeaderView, views::View)

 public:
  KoanSplitHeaderView(KoanSplitHeaderController* controller, int pane)
      : controller_(controller), pane_(pane) {
    SetNotifyEnterExitOnChild(true);
    rearrange_ = AddChildView(std::make_unique<KoanSplitHeaderButton>(
        controller, pane, KoanSplitHeaderButton::Kind::kRearrange));
    unsplit_ = AddChildView(std::make_unique<KoanSplitHeaderButton>(
        controller, pane, KoanSplitHeaderButton::Kind::kUnsplit));
  }

  // The widget's size: the header and its shadow around it.
  static gfx::Size WidgetSize() {
    return gfx::Size(kHeaderWidth + 2 * kShadowMargin,
                     kHeaderHeight + 2 * kShadowMargin);
  }

  // views::View:
  void Layout(PassKey) override {
    const gfx::Rect header = HeaderBounds();
    rearrange_->SetBounds(header.x() + kPaddingSide, header.y() + kPaddingTop,
                          kButtonSize, kButtonSize);
    unsplit_->SetBounds(rearrange_->bounds().right() + kButtonGap,
                        header.y() + kPaddingTop, kButtonSize, kButtonSize);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    const gfx::RoundedCornersF corners(0, 0, kHeaderRadius, kHeaderRadius);
    gfx::RectF shadow_rect(HeaderBounds());
    shadow_rect.Outset(kShadowSpread);
    cc::PaintFlags shadow;
    shadow.setAntiAlias(true);
    shadow.setColor(SK_ColorTRANSPARENT);
    shadow.setLooper(gfx::CreateShadowDrawLooper(
        {gfx::ShadowValue(gfx::Vector2d(), kShadowBlur,
                          colors->GetColor(kColorKoanSplitHeaderShadow))}));
    canvas->sk_canvas()->drawRRect(
        static_cast<SkRRect>(gfx::RRectF(shadow_rect, corners)), shadow);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kColorKoanSplitHeader));
    canvas->sk_canvas()->drawRRect(
        static_cast<SkRRect>(gfx::RRectF(gfx::RectF(HeaderBounds()), corners)),
        flags);
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    controller_->OnHeaderHovered(pane_, true);
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    controller_->OnHeaderHovered(pane_, false);
  }

 private:
  static gfx::Rect HeaderBounds() {
    return gfx::Rect(kShadowMargin, kShadowMargin, kHeaderWidth, kHeaderHeight);
  }

  const raw_ptr<KoanSplitHeaderController> controller_;
  const int pane_;
  raw_ptr<views::View> rearrange_ = nullptr;
  raw_ptr<views::View> unsplit_ = nullptr;
};

BEGIN_METADATA(KoanSplitHeaderView)
END_METADATA

// The rearrange view over the panes: each pane dimmed (the window seen
// through it, as Zen's browsers at lower opacity), the drop zone and the
// drag image. Takes no events: the grip that started the drag keeps the
// pointer.
class KoanSplitRearrangeView : public views::View,
                               public gfx::AnimationDelegate {
  METADATA_HEADER(KoanSplitRearrangeView, views::View)

 public:
  KoanSplitRearrangeView() : zone_animation_(this) {
    zone_animation_.SetSlideDuration(kDropZoneDuration);
    zone_animation_.SetTweenType(gfx::Tween::EASE_OUT);
  }

  // The panes in this view's coordinates.
  void SetPanes(const gfx::Rect& dragged, const gfx::Rect& other) {
    dragged_ = dragged;
    other_ = other;
    SchedulePaint();
  }

  // Shows the drop zone at `zone` (moving there from where it was, if it
  // shows already), or hides it.
  void SetDropZone(const std::optional<gfx::RectF>& zone) {
    if (zone == zone_to_) {
      return;
    }
    if (!zone) {
      zone_to_.reset();
      SchedulePaint();
      return;
    }
    // display: none to shown jumps; a shown zone moves.
    zone_from_ = zone_to_ ? CurrentZone() : *zone;
    zone_to_ = zone;
    zone_animation_.Reset(0);
    if (RichAnimation()) {
      zone_animation_.Show();
    } else {
      zone_animation_.Reset(1);
    }
    SchedulePaint();
  }

  // The drag image's top left corner follows the pointer at `point`.
  void SetPointer(const std::optional<gfx::Point>& point) {
    pointer_ = point;
    SchedulePaint();
  }

  void SetThumbnail(const gfx::ImageSkia& thumbnail) {
    thumbnail_ = thumbnail;
    SchedulePaint();
  }

  void Reset() {
    zone_animation_.Reset(0);
    zone_to_.reset();
    pointer_.reset();
    thumbnail_ = gfx::ImageSkia();
    SchedulePaint();
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    const float radius = MultiContentsView::kSplitViewContentCornerRadius;
    cc::PaintFlags dim;
    dim.setAntiAlias(true);
    dim.setColor(colors->GetColor(kColorKoanSplitRearrangeDim));
    dim.setAlphaf(1 - kOtherOpacity);
    canvas->DrawRoundRect(gfx::RectF(other_), radius, dim);
    dim.setAlphaf(1 - kDraggedOpacity);
    canvas->DrawRoundRect(gfx::RectF(dragged_), radius, dim);
    if (zone_to_) {
      cc::PaintFlags zone;
      zone.setAntiAlias(true);
      zone.setColor(colors->GetColor(kColorKoanSplitDropZone));
      canvas->DrawRoundRect(CurrentZone(), radius, zone);
    }
    if (pointer_) {
      const gfx::Rect image(
          *pointer_ + gfx::Vector2d(kDragImageOffset, kDragImageOffset),
          kDragImageSize);
      canvas->FillRect(image, colors->GetColor(kColorKoanSplitDragImage));
      if (!thumbnail_.isNull()) {
        canvas->DrawImageInt(thumbnail_, 0, 0, thumbnail_.width(),
                             thumbnail_.height(), image.x(), image.y(),
                             image.width(), image.height(),
                             /*filter=*/true);
      }
    }
  }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override {
    SchedulePaint();
  }

 private:
  gfx::RectF CurrentZone() const {
    return gfx::Tween::RectFValueBetween(zone_animation_.GetCurrentValue(),
                                         zone_from_,
                                         zone_to_.value_or(zone_from_));
  }

  gfx::Rect dragged_;
  gfx::Rect other_;
  gfx::SlideAnimation zone_animation_;
  gfx::RectF zone_from_;
  std::optional<gfx::RectF> zone_to_;
  std::optional<gfx::Point> pointer_;
  gfx::ImageSkia thumbnail_;
};

BEGIN_METADATA(KoanSplitRearrangeView)
END_METADATA

void UnsplitFromHeader(TabStripModel* tab_strip,
                       tabs::TabInterface* tab,
                       bool keep_selection) {
  if (!tab || !tab->IsSplit() ||
      tab_strip->GetIndexOfTab(tab) == TabStripModel::kNoTab) {
    return;
  }
  tab_strip->RemoveSplit(*tab->GetSplit());
  if (!keep_selection) {
    tab_strip->ActivateTabAt(tab_strip->GetIndexOfTab(tab));
  }
}

void RearrangeSplitPanes(TabStripModel* tab_strip,
                         tabs::TabInterface* dragged,
                         RearrangeSide side,
                         bool rtl) {
  if (!dragged || !dragged->IsSplit() ||
      tab_strip->GetIndexOfTab(dragged) == TabStripModel::kNoTab) {
    return;
  }
  const split_tabs::SplitTabId split = *dragged->GetSplit();
  split_tabs::SplitTabData* data = tab_strip->GetSplitData(split);
  const std::vector<tabs::TabInterface*> tabs = data->ListTabs();
  if (tabs.size() != 2u) {
    return;
  }
  const RearrangeResult result = RearrangeSplit(
      *data->visual_data(), tabs.front() == dragged ? 0 : 1, side, rtl);
  if (result.reverse) {
    tab_strip->ReverseTabsInSplit(split);
  }
  tab_strip->UpdateSplitLayout(split, result.visual_data.split_layout());
  tab_strip->UpdateSplitRatio(split, result.visual_data.split_ratio());
}

KoanSplitHeaderController::Pane::Pane() = default;
KoanSplitHeaderController::Pane::~Pane() = default;

KoanSplitHeaderController::KoanSplitHeaderController(BrowserView* browser_view)
    : browser_view_(browser_view) {
  browser_view_->browser()->GetTabStripModel()->AddObserver(this);
  if (MultiContentsView* contents = browser_view_->multi_contents_view()) {
    const auto& containers = contents->contents_container_views();
    for (size_t i = 0; i < panes_.size() && i < containers.size(); ++i) {
      panes_[i].container = containers[i].get();
      container_observations_.AddObservation(containers[i].get());
    }
  }
}

KoanSplitHeaderController::~KoanSplitHeaderController() = default;

void KoanSplitHeaderController::OnAddedToWidget() {
  // The page's own fullscreen hides the headers (Zen's inDOMFullscreen).
  if (!fullscreen_subscription_) {
    if (ExclusiveAccessManager* manager =
            ExclusiveAccessManager::From(browser_view_->browser())) {
      fullscreen_subscription_ =
          manager->fullscreen_controller()->RegisterOnFullscreenStateChanged(
              base::BindRepeating(&KoanSplitHeaderController::Update,
                                  base::Unretained(this)));
    }
  }
  Update();
}

void KoanSplitHeaderController::OnBrowserViewLayout() {
  Update();
}

void KoanSplitHeaderController::OnBrowserViewDestroying() {
  destroying_ = true;
  weak_factory_.InvalidateWeakPtrs();
  fullscreen_subscription_ = {};
  browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
  container_observations_.RemoveAllObservations();
  monitor_.reset();
  rearrange_.reset();
  rearrange_hide_timer_.Stop();
  rearrange_view_ = nullptr;
  if (rearrange_widget_) {
    rearrange_widget_->CloseNow();
    rearrange_widget_.reset();
  }
  for (Pane& pane : panes_) {
    pane.timer.Stop();
    pane.view = nullptr;
    pane.container = nullptr;
    if (pane.widget) {
      pane.widget->CloseNow();
      pane.widget.reset();
    }
  }
}

bool KoanSplitHeaderController::OwnsWidget(const views::Widget* widget) const {
  if (widget && widget == rearrange_widget_.get()) {
    return true;
  }
  for (const Pane& pane : panes_) {
    if (widget && widget == pane.widget.get()) {
      return true;
    }
  }
  return false;
}

void KoanSplitHeaderController::OnHeaderHovered(int pane, bool hovered) {
  panes_[pane].header_hovered = hovered;
  // Off the header, the pointer is back over the window's page (Zen's
  // header is inside the pane): its next move says where exactly.
  if (!hovered) {
    if (views::Widget* widget = browser_view_->GetWidget()) {
      pointer_in_window_ = widget->GetWindowBoundsInScreen().Contains(
          display::Screen::Get()->GetCursorScreenPoint());
    }
  }
  UpdateHover();
}

void KoanSplitHeaderController::OnUnsplitPressed(int pane,
                                                 bool keep_selection) {
  if (rearrange_ || !IsActive()) {
    return;
  }
  UnsplitFromHeader(browser_view_->browser()->GetTabStripModel(),
                    TabOf(panes_[pane]), keep_selection);
}

void KoanSplitHeaderController::StartRearrange(int pane) {
  tabs::TabInterface* tab = TabOf(panes_[pane]);
  views::Widget* browser_widget = browser_view_->GetWidget();
  if (rearrange_ || !IsActive() || !tab || !browser_widget) {
    return;
  }
  rearrange_ = Rearrange{.dragged = pane, .tab = tab->GetHandle()};
  ++rearrange_generation_;
  // Only the dragged pane's header stays, under the pointer.
  SetWanted(panes_[1 - pane], false);
  if (!rearrange_widget_) {
    views::Widget::InitParams params(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_CONTROL);
    params.parent = browser_widget->GetNativeView();
    params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
    params.accept_events = false;
    params.activatable = views::Widget::InitParams::Activatable::kNo;
    params.name = "KoanSplitRearrange";
    rearrange_widget_ = std::make_unique<views::Widget>();
    rearrange_widget_->Init(std::move(params));
    rearrange_view_ = rearrange_widget_->SetContentsView(
        std::make_unique<KoanSplitRearrangeView>());
    rearrange_view_->SetPaintToLayer();
    rearrange_view_->layer()->SetFillsBoundsOpaquely(false);
    rearrange_view_->SetVisible(false);
  }
  rearrange_hide_timer_.Stop();
  rearrange_view_->Reset();
  LayoutRearrangeView();
  ui::Layer* layer = rearrange_view_->layer();
  if (!rearrange_widget_->IsVisible()) {
    layer->SetOpacity(0);
    rearrange_view_->SetVisible(true);
    rearrange_widget_->ShowInactive();
  }
  {
    ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
    settings.SetTransitionDuration(RichAnimation() ? kDimDuration
                                                   : base::TimeDelta());
    settings.SetTweenType(gfx::Tween::EASE_OUT);
    layer->SetOpacity(1);
  }
  // The grip being dragged stays above it.
  if (panes_[pane].widget) {
    panes_[pane].widget->StackAtTop();
  }
  CaptureThumbnail(tab->GetContents(), rearrange_generation_);
}

void KoanSplitHeaderController::UpdateRearrange(
    const gfx::Point& screen_point) {
  if (!rearrange_ || !rearrange_view_) {
    return;
  }
  const Pane& other = panes_[1 - rearrange_->dragged];
  rearrange_->side.reset();
  std::optional<gfx::RectF> zone;
  if (other.container) {
    const gfx::Rect bounds = other.container->GetBoundsInScreen();
    // Zen's drop zone is off over the dragged pane, and between the panes.
    if (bounds.Contains(screen_point)) {
      rearrange_->side =
          RearrangeSideAt(gfx::RectF(bounds), gfx::PointF(screen_point));
      gfx::Point origin = bounds.origin();
      views::View::ConvertPointFromScreen(rearrange_view_, &origin);
      zone = RearrangeDropZone(gfx::RectF(gfx::Rect(origin, bounds.size())),
                               *rearrange_->side);
    }
  }
  rearrange_view_->SetDropZone(zone);
  gfx::Point pointer = screen_point;
  views::View::ConvertPointFromScreen(rearrange_view_, &pointer);
  rearrange_view_->SetPointer(pointer);
}

void KoanSplitHeaderController::FinishRearrange(
    const gfx::Point& screen_point) {
  if (!rearrange_) {
    return;
  }
  UpdateRearrange(screen_point);
  EndRearrange(/*drop=*/true);
}

void KoanSplitHeaderController::CancelRearrange() {
  EndRearrange(/*drop=*/false);
}

void KoanSplitHeaderController::EndRearrange(bool drop) {
  if (!rearrange_) {
    return;
  }
  const Rearrange rearrange = *rearrange_;
  rearrange_.reset();
  if (rearrange_view_) {
    rearrange_view_->Reset();
    ui::Layer* layer = rearrange_view_->layer();
    ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
    settings.SetTransitionDuration(RichAnimation() ? kDimDuration
                                                   : base::TimeDelta());
    settings.SetTweenType(gfx::Tween::EASE_OUT);
    layer->SetOpacity(0);
    rearrange_hide_timer_.Start(
        FROM_HERE, RichAnimation() ? kDimDuration : base::TimeDelta(),
        base::BindOnce(
            [](base::WeakPtr<KoanSplitHeaderController> controller) {
              if (controller && !controller->rearrange_ &&
                  controller->rearrange_widget_) {
                controller->rearrange_view_->SetVisible(false);
                controller->rearrange_widget_->Hide();
              }
            },
            weak_factory_.GetWeakPtr()));
  }
  // Zen's onBrowserDrop(): over the other pane only.
  if (drop && rearrange.side && IsActive()) {
    RearrangeSplitPanes(browser_view_->browser()->GetTabStripModel(),
                        rearrange.tab.Get(), *rearrange.side,
                        base::i18n::IsRTL());
  }
  UpdateHover();
}

void KoanSplitHeaderController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // Zen leaves the rearrange view on any tab change (onLocationChange()).
  if (change.type() != TabStripModelChange::kSelectionOnly ||
      selection.active_tab_changed()) {
    CancelRearrange();
  }
  PostUpdate();
}

void KoanSplitHeaderController::OnSplitTabChanged(
    const SplitTabChange& change) {
  CancelRearrange();
  PostUpdate();
}

void KoanSplitHeaderController::OnViewBoundsChanged(
    views::View* observed_view) {
  // A pane resized (the split's ratio or layout) or moved.
  Update();
}

void KoanSplitHeaderController::OnViewIsDeleting(views::View* observed_view) {
  container_observations_.RemoveObservation(observed_view);
  for (Pane& pane : panes_) {
    if (pane.container == observed_view) {
      pane.container = nullptr;
    }
  }
}

void KoanSplitHeaderController::OnEvent(const ui::Event& event) {
  if (destroying_) {
    return;
  }
  if (event.type() == ui::EventType::kKeyPressed) {
    // Zen's Esc leaves the rearrange view, and the drag drops nothing.
    if (rearrange_ &&
        event.AsKeyEvent()->key_code() == ui::KeyboardCode::VKEY_ESCAPE) {
      CancelRearrange();
    }
    return;
  }
  pointer_in_window_ = event.type() != ui::EventType::kMouseExited;
  UpdateHover();
}

void KoanSplitHeaderController::PostUpdate() {
  if (update_posted_ || destroying_) {
    return;
  }
  update_posted_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<KoanSplitHeaderController> controller) {
                       if (controller) {
                         controller->update_posted_ = false;
                         controller->Update();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

bool KoanSplitHeaderController::IsActive() const {
  if (destroying_ || !browser_view_->GetIsNormalType() ||
      !browser_view_->GetWidget()) {
    return false;
  }
  MultiContentsView* contents = browser_view_->multi_contents_view();
  tabs::TabInterface* active =
      browser_view_->browser()->GetTabStripModel()->GetActiveTab();
  if (!contents || !contents->IsInSplitView() || !active ||
      !active->IsSplit()) {
    return false;
  }
  ExclusiveAccessManager* manager =
      ExclusiveAccessManager::From(browser_view_->browser());
  if (manager && manager->fullscreen_controller()->IsTabFullscreen()) {
    return false;
  }
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  return !(controller && controller->glance() &&
           controller->glance()->is_open());
}

void KoanSplitHeaderController::Update() {
  if (destroying_) {
    return;
  }
  const bool active = IsActive();
  if (!active) {
    CancelRearrange();
    monitor_.reset();
    pointer_in_window_ = false;
    for (Pane& pane : panes_) {
      pane.header_hovered = false;
      SetWanted(pane, false);
    }
    return;
  }
  if (!monitor_) {
    monitor_ = views::EventMonitor::CreateWindowMonitor(
        this, browser_view_->GetWidget()->GetNativeWindow(),
        {ui::EventType::kMouseMoved, ui::EventType::kMouseDragged,
         ui::EventType::kMouseEntered, ui::EventType::kMouseExited,
         ui::EventType::kKeyPressed});
  }
  LayoutHeaders();
  if (rearrange_) {
    LayoutRearrangeView();
  }
  UpdateHover();
}

void KoanSplitHeaderController::UpdateHover() {
  if (destroying_) {
    return;
  }
  const bool active = IsActive() && !TabDragController::IsActive();
  const gfx::Point cursor = display::Screen::Get()->GetCursorScreenPoint();
  for (size_t i = 0; i < panes_.size(); ++i) {
    Pane& pane = panes_[i];
    bool wanted;
    if (rearrange_) {
      // The grip being dragged keeps its header; the other has none.
      wanted = static_cast<int>(i) == rearrange_->dragged;
    } else {
      // Zen: .browserSidebarContainer:hover, or the header's own :hover.
      wanted =
          active && (pane.header_hovered ||
                     (pointer_in_window_ && IsPointerOverPane(pane, cursor)));
    }
    SetWanted(pane, wanted);
  }
}

bool KoanSplitHeaderController::IsPointerOverPane(
    const Pane& pane,
    const gfx::Point& screen) const {
  views::Widget* widget = browser_view_->GetWidget();
  if (!pane.container || !pane.container->GetVisible() || !widget) {
    return false;
  }
  // The pane's page or its own views, not what lies over it (a sidebar shown
  // over the page in compact mode, the resize handle between the panes).
  views::View* root = widget->GetRootView();
  gfx::Point point = screen;
  views::View::ConvertPointFromScreen(root, &point);
  if (!root->GetLocalBounds().Contains(point)) {
    return false;
  }
  const views::View* target = root->GetEventHandlerForPoint(point);
  return target && pane.container->Contains(target);
}

void KoanSplitHeaderController::SetWanted(Pane& pane, bool wanted) {
  if (pane.wanted == wanted) {
    return;
  }
  pane.wanted = wanted;
  pane.timer.Stop();
  const int index = &pane == &panes_[0] ? 0 : 1;
  if (wanted) {
    EnsureWidget(index);
    if (!pane.widget) {
      return;
    }
    LayoutHeaders();
    if (!pane.widget->IsVisible()) {
      pane.view->layer()->SetOpacity(0);
      pane.view->SetVisible(true);
      pane.widget->ShowInactive();
      // Above the loading indicator over the active pane.
      pane.widget->StackAtTop();
    }
    if (!RichAnimation()) {
      pane.view->layer()->SetOpacity(1);
      return;
    }
    // Zen's transition-delay: it takes the pointer at once, and shows after
    // 0.1 s.
    pane.timer.Start(FROM_HERE, kFadeInDelay,
                     base::BindOnce(
                         [](KoanSplitHeaderView* view) {
                           ui::ScopedLayerAnimationSettings settings(
                               view->layer()->GetAnimator());
                           settings.SetTransitionDuration(kFadeDuration);
                           settings.SetTweenType(gfx::Tween::EASE_IN_OUT);
                           view->layer()->SetOpacity(1);
                         },
                         base::Unretained(pane.view.get())));
    return;
  }
  if (!pane.widget || !pane.widget->IsVisible()) {
    return;
  }
  if (!RichAnimation()) {
    HideHeader(index);
    return;
  }
  {
    ui::ScopedLayerAnimationSettings settings(
        pane.view->layer()->GetAnimator());
    settings.SetTransitionDuration(kFadeDuration);
    settings.SetTweenType(gfx::Tween::EASE_IN_OUT);
    pane.view->layer()->SetOpacity(0);
  }
  // Zen's pointer-events: none once it is gone. (Unretained: the timer is
  // the pane's.)
  pane.timer.Start(FROM_HERE, kFadeDuration,
                   base::BindOnce(&KoanSplitHeaderController::HideHeader,
                                  base::Unretained(this), index));
}

void KoanSplitHeaderController::HideHeader(int index) {
  Pane& pane = panes_[index];
  if (pane.wanted || !pane.widget) {
    return;
  }
  // (The view too, so that UI DevTools' tree says what shows.)
  pane.view->SetVisible(false);
  pane.widget->Hide();
}

void KoanSplitHeaderController::EnsureWidget(int index) {
  Pane& pane = panes_[index];
  views::Widget* browser_widget = browser_view_->GetWidget();
  if (pane.widget || !browser_widget) {
    return;
  }
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  params.parent = browser_widget->GetNativeView();
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  // The page keeps the focus and the window stays key.
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  params.name = "KoanSplitHeader";
  pane.widget = std::make_unique<views::Widget>();
  pane.widget->Init(std::move(params));
  pane.view = pane.widget->SetContentsView(
      std::make_unique<KoanSplitHeaderView>(this, index));
  pane.view->SetPaintToLayer();
  pane.view->layer()->SetFillsBoundsOpaquely(false);
  pane.view->SetVisible(false);
}

void KoanSplitHeaderController::LayoutHeaders() {
  for (Pane& pane : panes_) {
    if (!pane.widget || !pane.container || !pane.container->GetWidget()) {
      continue;
    }
    // Centred on the pane's top edge, hanging from the gap above it.
    const gfx::Rect bounds =
        pane.container->ConvertRectToWidget(pane.container->GetLocalBounds());
    const gfx::Size size = KoanSplitHeaderView::WidgetSize();
    pane.widget->SetBounds(gfx::Rect(
        bounds.CenterPoint().x() - size.width() / 2,
        bounds.y() - kHeaderRise - kShadowMargin, size.width(), size.height()));
  }
}

void KoanSplitHeaderController::LayoutRearrangeView() {
  MultiContentsView* contents = browser_view_->multi_contents_view();
  if (!rearrange_widget_ || !rearrange_ || !contents ||
      !contents->GetWidget()) {
    return;
  }
  const gfx::Rect area =
      contents->ConvertRectToWidget(contents->GetLocalBounds());
  rearrange_widget_->SetBounds(area);
  // Each pane in the view's coordinates (its origin is the contents area's).
  auto pane_bounds = [&](const Pane& pane) {
    if (!pane.container) {
      return gfx::Rect();
    }
    gfx::Rect bounds =
        pane.container->ConvertRectToWidget(pane.container->GetLocalBounds());
    bounds.Offset(-area.OffsetFromOrigin());
    return bounds;
  };
  rearrange_view_->SetPanes(pane_bounds(panes_[rearrange_->dragged]),
                            pane_bounds(panes_[1 - rearrange_->dragged]));
}

tabs::TabInterface* KoanSplitHeaderController::TabOf(const Pane& pane) const {
  MultiContentsView* contents = browser_view_->multi_contents_view();
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  tabs::TabInterface* active = model->GetActiveTab();
  if (!contents || !pane.container || !active || !active->IsSplit()) {
    return nullptr;
  }
  // The active pane shows the active tab, the other one its split's other
  // tab.
  if (pane.container == contents->GetActiveContentsContainerView()) {
    return active;
  }
  for (tabs::TabInterface* tab :
       model->GetSplitData(*active->GetSplit())->ListTabs()) {
    if (tab != active) {
      return tab;
    }
  }
  return nullptr;
}

void KoanSplitHeaderController::CaptureThumbnail(content::WebContents* contents,
                                                 int generation) {
  content::RenderWidgetHostView* view =
      contents ? contents->GetRenderWidgetHostView() : nullptr;
  if (!view) {
    return;
  }
  // PageThumbs: the top of the page, at the drag image's aspect.
  const gfx::Size size = view->GetViewBounds().size();
  const int height =
      std::min(size.height(),
               size.width() * kDragImageSize.height() / kDragImageSize.width());
  if (size.IsEmpty() || height <= 0) {
    return;
  }
  ui::Compositor* compositor = browser_view_->GetWidget()
                                   ? browser_view_->GetWidget()->GetCompositor()
                                   : nullptr;
  const float scale = compositor && compositor->device_scale_factor() > 0
                          ? compositor->device_scale_factor()
                          : 1.f;
  view->CopyFromSurface(gfx::Rect(size.width(), height),
                        gfx::ScaleToCeiledSize(kDragImageSize, scale),
                        base::Seconds(1),
                        base::BindPostTaskToCurrentDefault(base::BindOnce(
                            &KoanSplitHeaderController::OnThumbnail,
                            weak_factory_.GetWeakPtr(), generation, scale)));
}

void KoanSplitHeaderController::OnThumbnail(
    int generation,
    float scale,
    const content::CopyFromSurfaceResult& result) {
  if (!rearrange_ || generation != rearrange_generation_ || !rearrange_view_ ||
      !result.has_value() || result->bitmap.drawsNothing()) {
    return;
  }
  rearrange_view_->SetThumbnail(
      gfx::ImageSkia::CreateFromBitmap(result->bitmap, scale));
}

}  // namespace koan
