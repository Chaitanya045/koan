// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_download_animation.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "components/download/public/common/download_item.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/download_item_utils.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// ZenDownloadAnimation.mjs (sizes in koan_sidebar_metrics.h): 60 keyframes
// over zen.downloads.download-animation-duration (1000 ms), the arc twice as
// high as the distance (at most 1200, at most 80% of the room on that
// side), the icon shrinking to 45% at the end. The tile leaves the arc's
// duration + 200 ms after it is in.
constexpr int kArcViewSize = kDownloadArcSize + 2 * kDownloadShadowMargin;
constexpr int kArcSteps = 60;
constexpr base::TimeDelta kArcDuration = base::Milliseconds(1000);
constexpr float kArcHeightRatio = 2;
constexpr float kMaxArcHeight = 1200;
constexpr float kArcEndScale = 0.45f;
constexpr int kTileViewSize = kDownloadTileSize + 2 * kDownloadShadowMargin;
constexpr base::TimeDelta kTileHold = kArcDuration + base::Milliseconds(200);

// Scales `size` about its centre after moving it by (dx, dy).
gfx::Transform MoveAndScale(float dx, float dy, float scale, int size) {
  gfx::Transform transform;
  transform.Translate(dx + size / 2.0f, dy + size / 2.0f);
  transform.Scale(scale, scale);
  transform.Translate(-size / 2.0f, -size / 2.0f);
  return transform;
}

float EaseInOutQuad(float t) {
  return t < 0.5f ? 2 * t * t : -1 + (4 - 2 * t) * t;
}

// The flying icon: a ring around a filled circle with the download arrow.
class KoanDownloadArcView : public views::View {
  METADATA_HEADER(KoanDownloadArcView, views::View)

 public:
  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    const gfx::PointF center(width() / 2.0f, height() / 2.0f);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kColorKoanDownloadArcRing));
    flags.setLooper(gfx::CreateShadowDrawLooper({gfx::ShadowValue(
        gfx::Vector2d(0, kDownloadArcShadowY), kDownloadArcShadowBlur,
        colors->GetColor(kColorKoanDownloadArcShadow))}));
    canvas->DrawCircle(center, kDownloadArcSize / 2.0f, flags);
    flags.setLooper(nullptr);
    flags.setColor(colors->GetColor(kColorKoanDownloadArcCircle));
    canvas->DrawCircle(center, kDownloadArcSize / 2.0f - kDownloadArcRing,
                       flags);
    const int icon_size = (kDownloadArcSize - 2 * kDownloadArcRing) * 7 / 10;
    canvas->DrawImageInt(
        gfx::CreateVectorIcon(vector_icons::kDownloadIcon, icon_size,
                              colors->GetColor(kColorKoanDownloadArcIcon)),
        (width() - icon_size) / 2, (height() - icon_size) / 2);
  }
};

BEGIN_METADATA(KoanDownloadArcView)
END_METADATA

// The downloads tile shown when there is no downloads button to fly to.
class KoanDownloadTileView : public views::View {
  METADATA_HEADER(KoanDownloadTileView, views::View)

 public:
  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    const gfx::RectF tile(kDownloadShadowMargin, kDownloadShadowMargin,
                          kDownloadTileSize, kDownloadTileSize);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kColorKoanDownloadTile));
    flags.setLooper(gfx::CreateShadowDrawLooper(
        {gfx::ShadowValue(gfx::Vector2d(), kDownloadTileShadowBlur,
                          colors->GetColor(kColorKoanDownloadTileShadow))}));
    canvas->DrawRoundRect(tile, kDownloadTileCornerRadius, flags);
    canvas->DrawImageInt(gfx::CreateVectorIcon(
                             vector_icons::kDownloadIcon, kDownloadTileIconSize,
                             colors->GetColor(kColorKoanDownloadTileIcon)),
                         (width() - kDownloadTileIconSize) / 2,
                         (height() - kDownloadTileIconSize) / 2);
  }
};

BEGIN_METADATA(KoanDownloadTileView)
END_METADATA

}  // namespace

KoanDownloadAnimation::KoanDownloadAnimation(BrowserView* browser_view)
    : browser_view_(browser_view) {
  if (content::DownloadManager* manager =
          browser_view_->GetProfile()->GetDownloadManager()) {
    download_manager_observation_.Observe(manager);
  }
}

KoanDownloadAnimation::~KoanDownloadAnimation() = default;

bool KoanDownloadAnimation::IsEnabled() const {
  return features::IsDownloadAnimationEnabled() &&
         browser_view_->GetProfile()->GetPrefs()->GetBoolean(
             prefs::kDownloadAnimation);
}

void KoanDownloadAnimation::OnAddedToWidget() {
  // Presses on the browser's own views; the page reports its own
  // (OnPagePressed).
  if (!root_view_ && browser_view_->GetWidget()) {
    root_view_ = browser_view_->GetWidget()->GetRootView();
    root_view_->AddPreTargetHandler(this);
  }
}

void KoanDownloadAnimation::OnBrowserViewLayout() {
  LayoutOverlay();
}

void KoanDownloadAnimation::OnBrowserViewDestroying() {
  download_manager_observation_.Reset();
  if (root_view_) {
    std::exchange(root_view_, nullptr)->RemovePreTargetHandler(this);
  }
  tile_timer_.Stop();
  tile_ = nullptr;
  overlay_.reset();
}

void KoanDownloadAnimation::OnDownloadCreated(content::DownloadManager* manager,
                                              download::DownloadItem* item) {
  // Downloads loaded from history arrive here too.
  if (item->GetState() != download::DownloadItem::IN_PROGRESS || !IsEnabled() ||
      !IsForThisWindow(item)) {
    return;
  }
  Play();
}

void KoanDownloadAnimation::ManagerGoingDown(
    content::DownloadManager* manager) {
  download_manager_observation_.Reset();
}

void KoanDownloadAnimation::OnMouseEvent(ui::MouseEvent* event) {
  if (event->type() != ui::EventType::kMousePressed) {
    return;
  }
  // The root view's coordinates are the widget's.
  gfx::Point point = event->root_location();
  views::View::ConvertPointFromWidget(browser_view_, &point);
  last_press_ = point;
}

void KoanDownloadAnimation::OnPagePressed(const gfx::Point& point_in_screen) {
  gfx::Point point = point_in_screen;
  views::View::ConvertPointFromScreen(browser_view_, &point);
  last_press_ = point;
}

bool KoanDownloadAnimation::IsForThisWindow(
    download::DownloadItem* item) const {
  content::WebContents* contents =
      content::DownloadItemUtils::GetWebContents(item);
  if (!contents) {
    return browser_view_->IsActive();
  }
  if (browser_view_->browser()->GetTabStripModel()->GetIndexOfWebContents(
          contents) != TabStripModel::kNoTab) {
    return true;
  }
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  return controller && controller->glance()->contents() == contents;
}

void KoanDownloadAnimation::Play() {
  if (!browser_view_->GetWidget() ||
      browser_view_->GetLocalBounds().IsEmpty()) {
    return;
  }
  // Without a press yet (e.g. a keyboard shortcut), start over the page.
  gfx::Point start = browser_view_->GetLocalBounds().CenterPoint();
  if (last_press_) {
    start = *last_press_;
  } else if (const views::View* page = browser_view_->multi_contents_view()) {
    start = page->bounds().CenterPoint();
  }

  // The downloads button if it shows, else the tile.
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  const bool toolbar_shown = !controller ||
                             !controller->compact_mode()->hides_toolbar() ||
                             controller->compact_mode()->is_toolbar_revealed();
  ToolbarButtonProvider* provider = browser_view_->toolbar_button_provider();
  views::View* button = provider ? provider->GetDownloadButton() : nullptr;
  gfx::Point end;
  if (toolbar_shown && button && button->IsDrawn()) {
    end = button->GetLocalBounds().CenterPoint();
    views::View::ConvertPointToTarget(button, browser_view_, &end);
  } else {
    ShowTile();
    end = TileBounds().CenterPoint();
  }
  PlayArc(start, end);
}

views::View* KoanDownloadAnimation::EnsureOverlay() {
  if (!overlay_) {
    views::Widget::InitParams params(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_CONTROL);
    params.parent = browser_view_->GetWidget()->GetNativeView();
    params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
    params.accept_events = false;
    params.activatable = views::Widget::InitParams::Activatable::kNo;
    params.name = "KoanDownloadAnimation";
    overlay_ = std::make_unique<views::Widget>();
    overlay_->Init(std::move(params));
    overlay_->SetContentsView(std::make_unique<views::View>());
    LayoutOverlay();
    overlay_->ShowInactive();
  }
  return overlay_->GetContentsView();
}

void KoanDownloadAnimation::LayoutOverlay() {
  if (overlay_) {
    overlay_->SetBounds(
        browser_view_->ConvertRectToWidget(browser_view_->GetLocalBounds()));
  }
}

void KoanDownloadAnimation::PlayArc(const gfx::Point& start,
                                    const gfx::Point& end) {
  views::View* overlay = EnsureOverlay();
  views::View* icon =
      overlay->AddChildView(std::make_unique<KoanDownloadArcView>());
  icon->SetBounds(start.x() - kArcViewSize / 2, start.y() - kArcViewSize / 2,
                  kArcViewSize, kArcViewSize);
  icon->SetPaintToLayer();
  icon->layer()->SetFillsBoundsOpaquely(false);

  // Arc towards the side with more room. Layer transforms aren't mirrored
  // in a right-to-left UI, the views' positions are.
  const float dx = (end.x() - start.x()) * (base::i18n::IsRTL() ? -1.0f : 1.0f);
  const float dy = end.y() - start.y();
  const float room_above = std::min(start.y(), end.y());
  const float room_below = overlay->height() - std::max(start.y(), end.y());
  const bool downward = room_below > room_above;
  const float arc_height =
      std::min({std::hypot(dx, dy) * kArcHeightRatio, kMaxArcHeight,
                (downward ? room_below : room_above) * 0.8f});
  const auto keyframe = [&](float progress, gfx::Transform* transform,
                            float* opacity) {
    const float eased = EaseInOutQuad(progress);
    const float x = dx * eased;
    const float y = dy * eased + (downward ? 1 : -1) * arc_height *
                                     (1 - std::pow(2 * eased - 1, 2.0f));
    const float scale = progress < 0.5f ? 0.5f + progress / 0.5f * 1.3f
                                        : 1.8f - (progress - 0.5f) / 0.5f *
                                                     (1.8f - kArcEndScale);
    *transform = MoveAndScale(x, y, scale, kArcViewSize);
    if (progress < 0.3f) {
      *opacity = 0.3f + progress / 0.3f * 0.6f;
    } else if (progress < 0.98f) {
      *opacity = std::min(1.0f, 0.9f + (progress - 0.3f) / 0.6f * 0.1f);
    } else {
      *opacity = std::max(0.0f, 1 - (progress - 0.9f) / 0.1f);
    }
  };
  gfx::Transform transform;
  float opacity = 0;
  keyframe(0, &transform, &opacity);
  icon->layer()->SetTransform(transform);
  icon->layer()->SetOpacity(opacity);

  ++running_;
  views::AnimationBuilder builder;
  builder
      .OnEnded(base::BindOnce(&KoanDownloadAnimation::OnAnimationDone,
                              weak_factory_.GetWeakPtr()))
      .OnAborted(base::BindOnce(&KoanDownloadAnimation::OnAnimationDone,
                                weak_factory_.GetWeakPtr()));
  views::AnimationSequenceBlock* block = &builder.Once();
  for (int step = 1; step <= kArcSteps; ++step) {
    // Zen eases the whole flight with cubic-bezier(0.37, 0, 0.63, 1), a sine.
    keyframe(gfx::Tween::CalculateValue(gfx::Tween::SMOOTH_IN_OUT,
                                        static_cast<double>(step) / kArcSteps),
             &transform, &opacity);
    if (step > 1) {
      block = &block->Then();
    }
    block->SetDuration(kArcDuration / kArcSteps)
        .SetTransform(icon, transform)
        .SetOpacity(icon, opacity);
  }
}

void KoanDownloadAnimation::ShowTile() {
  if (tile_) {
    // Already there: stay a while longer.
    if (tile_timer_.IsRunning()) {
      tile_timer_.Reset();
    }
    return;
  }
  views::View* overlay = EnsureOverlay();
  tile_ = overlay->AddChildView(std::make_unique<KoanDownloadTileView>());
  gfx::Rect bounds = TileBounds();
  bounds.Outset(kDownloadShadowMargin);
  tile_->SetBoundsRect(bounds);
  tile_->SetPaintToLayer();
  tile_->layer()->SetFillsBoundsOpaquely(false);
  tile_->layer()->SetTransform(
      MoveAndScale(TileOffset(kDownloadTileOutside), 0, 0.8f, kTileViewSize));

  ++running_;
  views::AnimationBuilder()
      .OnEnded(base::BindOnce(
          [](base::WeakPtr<KoanDownloadAnimation> self) {
            if (self) {
              self->tile_timer_.Start(FROM_HERE, kTileHold, self.get(),
                                      &KoanDownloadAnimation::HideTile);
            }
          },
          weak_factory_.GetWeakPtr()))
      .OnAborted(base::BindOnce(&KoanDownloadAnimation::OnAnimationDone,
                                weak_factory_.GetWeakPtr()))
      .Once()
      .SetDuration(base::Milliseconds(350))
      .SetTransform(tile_.get(),
                    MoveAndScale(TileOffset(kDownloadTileOvershoot), 0, 1.1f,
                                 kTileViewSize),
                    gfx::Tween::EASE_OUT)
      .Then()
      .SetDuration(base::Milliseconds(200))
      .SetTransform(tile_.get(), MoveAndScale(0, 0, 1, kTileViewSize),
                    gfx::Tween::EASE_IN_OUT);
}

void KoanDownloadAnimation::HideTile() {
  if (!tile_) {
    return;
  }
  views::AnimationBuilder()
      .OnEnded(base::BindOnce(&KoanDownloadAnimation::OnAnimationDone,
                              weak_factory_.GetWeakPtr()))
      .OnAborted(base::BindOnce(&KoanDownloadAnimation::OnAnimationDone,
                                weak_factory_.GetWeakPtr()))
      .Once()
      .SetDuration(base::Milliseconds(150))
      .SetTransform(tile_.get(), MoveAndScale(0, 0, 0.9f, kTileViewSize),
                    gfx::Tween::EASE_IN)
      .Then()
      .SetDuration(base::Milliseconds(300))
      .SetTransform(tile_.get(),
                    MoveAndScale(TileOffset(kDownloadTileOutside), 0, 0.8f,
                                 kTileViewSize),
                    gfx::Tween::EASE_IN_2)
      .SetOpacity(tile_.get(), 0.0f, gfx::Tween::EASE_IN_2);
}

gfx::Rect KoanDownloadAnimation::TileBounds() const {
  // On the sidebar's side (Zen's tabs-on-the-right check).
  const int x =
      IsSidebarOnRight(browser_view_)
          ? browser_view_->width() - kDownloadTileEdge - kDownloadTileSize
          : kDownloadTileEdge;
  return gfx::Rect(
      x, browser_view_->height() - kDownloadTileEdge - kDownloadTileSize,
      kDownloadTileSize, kDownloadTileSize);
}

float KoanDownloadAnimation::TileOffset(int edge_distance) const {
  // Towards the window's edge on screen (layer transforms aren't mirrored).
  const bool screen_right =
      IsSidebarOnRight(browser_view_) != base::i18n::IsRTL();
  return (kDownloadTileEdge - edge_distance) * (screen_right ? 1.0f : -1.0f);
}

void KoanDownloadAnimation::OnAnimationDone() {
  // Called from the compositor animation's own callbacks: tear down once it
  // has unwound.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<KoanDownloadAnimation> self) {
                       if (self && --self->running_ <= 0) {
                         self->running_ = 0;
                         self->tile_timer_.Stop();
                         self->tile_ = nullptr;
                         self->overlay_.reset();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

}  // namespace koan
