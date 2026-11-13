// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_tab_drag_image.h"

#include <algorithm>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_space_drag_logic.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// The dot's room off the first row's top-left corner.
constexpr gfx::Vector2d kCountDotRoom(-kDragImageCountDotLeft,
                                      -kDragImageCountDotTop);

}  // namespace

// Paints the image: the rows from the back one forward, then the dot.
class KoanTabDragImageView : public views::View {
  METADATA_HEADER(KoanTabDragImageView, views::View)

 public:
  KoanTabDragImageView(std::vector<KoanTabDragImage::Row> rows, size_t count)
      : rows_(std::move(rows)), count_(count) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetCanProcessEventsWithinSubtree(false);
    GetViewAccessibility().SetIsIgnored(true);
    gfx::Size size;
    for (size_t i = 0; i < rows_.size(); ++i) {
      const int offset = static_cast<int>(i) * kDragImageRowOffset;
      size.SetToMax(gfx::Size(rows_[i].size.width() + offset,
                              rows_[i].size.height() + offset));
    }
    size.Enlarge(first_row_offset().x(), first_row_offset().y());
    SetSize(size);
  }
  KoanTabDragImageView(const KoanTabDragImageView&) = delete;
  KoanTabDragImageView& operator=(const KoanTabDragImageView&) = delete;
  ~KoanTabDragImageView() override = default;

  // Where the first row is in the view (the dot needs room above and before
  // it).
  gfx::Vector2d first_row_offset() const {
    return count_ > 1 ? kCountDotRoom : gfx::Vector2d();
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    for (size_t i = rows_.size(); i-- > 0;) {
      const int offset = static_cast<int>(i) * kDragImageRowOffset;
      gfx::ScopedCanvas scoped(canvas);
      canvas->Translate(first_row_offset() + gfx::Vector2d(offset, offset));
      if (i > 0) {
        canvas->SaveLayerAlpha(
            static_cast<uint8_t>(255 * kDragImageBackRowOpacity));
      }
      PaintRow(canvas, colors, rows_[i]);
      if (i > 0) {
        canvas->Restore();
      }
    }
    if (count_ > 1) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(colors->GetColor(kColorKoanTabDragCount));
      const gfx::Rect dot(0, 0, kDragImageCountDotSize, kDragImageCountDotSize);
      canvas->DrawCircle(gfx::RectF(dot).CenterPoint(),
                         kDragImageCountDotSize / 2.f, flags);
      const gfx::FontList font =
          gfx::FontList()
              .DeriveWithSizeDelta(kDragImageCountFontSize -
                                   gfx::FontList().GetFontSize())
              .DeriveWithWeight(gfx::Font::Weight::BOLD);
      canvas->DrawStringRectWithFlags(
          base::NumberToString16(count_), font,
          colors->GetColor(kColorKoanTabDragCountForeground), dot,
          gfx::Canvas::TEXT_ALIGN_CENTER);
    }
  }

 private:
  static void PaintRow(gfx::Canvas* canvas,
                       const ui::ColorProvider* colors,
                       const KoanTabDragImage::Row& row) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kColorKoanTabDragImage));
    canvas->DrawRoundRect(gfx::RectF(gfx::Rect(row.size)), row.corner_radius,
                          flags);
    if (!row.icon.IsEmpty() && !row.icon_bounds.IsEmpty()) {
      const gfx::ImageSkia icon = row.icon.Rasterize(colors);
      canvas->DrawImageInt(icon, 0, 0, icon.width(), icon.height(),
                           row.icon_bounds.x(), row.icon_bounds.y(),
                           row.icon_bounds.width(), row.icon_bounds.height(),
                           /*filter=*/true);
    }
    if (!row.title_bounds.IsEmpty()) {
      canvas->DrawStringRect(
          TruncateDragLabel(row.title, row.title_bounds.width()), row.font,
          colors->GetColor(kColorKoanTabDragImageForeground), row.title_bounds);
    }
  }

  const std::vector<KoanTabDragImage::Row> rows_;
  const size_t count_;
};

BEGIN_METADATA(KoanTabDragImageView)
END_METADATA

KoanTabDragImage::Row::Row() = default;
KoanTabDragImage::Row::Row(const Row&) = default;
KoanTabDragImage::Row& KoanTabDragImage::Row::operator=(const Row&) = default;
KoanTabDragImage::Row::~Row() = default;

KoanTabDragImage::KoanTabDragImage(BrowserView* browser_view)
    : browser_view_(browser_view) {
  landing_.SetSlideDuration(base::Milliseconds(kLandingMs));
  landing_.SetTweenType(gfx::Tween::EASE_OUT);
}

KoanTabDragImage::~KoanTabDragImage() = default;

void KoanTabDragImage::Show(std::vector<Row> rows,
                            size_t count,
                            const gfx::Vector2d& grab,
                            const gfx::Point& screen_point) {
  Hide();
  views::Widget* browser_widget = browser_view_->GetWidget();
  if (rows.empty() || !browser_widget) {
    return;
  }
  rows.resize(std::min(rows.size(), kDragImageMaxRows));
  // A child widget over the whole window, so the image shows over the page
  // too; it takes no events (the drag has the mouse).
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  params.parent = browser_widget->GetNativeView();
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.accept_events = false;
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  params.name = "KoanTabDragImage";
  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(params));
  views::View* root = widget_->SetContentsView(std::make_unique<views::View>());
  root->SetCanProcessEventsWithinSubtree(false);
  view_ = root->AddChildView(
      std::make_unique<KoanTabDragImageView>(std::move(rows), count));
  widget_->SetBounds(
      browser_view_->ConvertRectToWidget(browser_view_->GetLocalBounds()));
  grab_ = grab;
  PlaceAt(screen_point - grab_);
  widget_->ShowInactive();
}

void KoanTabDragImage::MoveTo(const gfx::Point& screen_point) {
  if (view_ && !landing_.is_animating()) {
    PlaceAt(screen_point - grab_);
  }
}

void KoanTabDragImage::Land(TargetCallback target, base::OnceClosure done) {
  if (!view_ || !gfx::Animation::ShouldRenderRichAnimation()) {
    Hide();
    std::move(done).Run();
    return;
  }
  landing_from_ = origin_;
  landing_target_ = std::move(target);
  landing_done_ = std::move(done);
  landing_.Reset(0);
  landing_.Show();
}

void KoanTabDragImage::Hide() {
  landing_.Reset(0);
  landing_target_.Reset();
  view_ = nullptr;
  widget_.reset();
}

bool KoanTabDragImage::IsShowing() const {
  return !!view_;
}

void KoanTabDragImage::AnimationProgressed(const gfx::Animation* animation) {
  const std::optional<gfx::Rect> target =
      landing_target_ ? landing_target_.Run() : std::nullopt;
  if (!target) {
    FinishLanding();
    return;
  }
  const double value = animation->GetCurrentValue();
  PlaceAt(gfx::Point(
      gfx::Tween::IntValueBetween(value, landing_from_.x(), target->x()),
      gfx::Tween::IntValueBetween(value, landing_from_.y(), target->y())));
}

void KoanTabDragImage::AnimationEnded(const gfx::Animation* animation) {
  FinishLanding();
}

void KoanTabDragImage::PlaceAt(const gfx::Point& origin) {
  origin_ = origin;
  gfx::Point position = origin - view_->first_row_offset();
  views::View::ConvertPointFromScreen(widget_->GetRootView(), &position);
  // (Moves the layer; nothing is painted again.)
  view_->SetPosition(position);
}

void KoanTabDragImage::FinishLanding() {
  base::OnceClosure done = std::move(landing_done_);
  Hide();
  if (done) {
    std::move(done).Run();
  }
}

}  // namespace koan
