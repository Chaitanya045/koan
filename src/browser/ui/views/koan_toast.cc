// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_toast.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/core/SkTileMode.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/geometry/transform_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// Zen's toast (ZenToasts.mjs): springs in from the top, stays for 2s once in
// (longer while hovered) and fades out shrinking.
constexpr base::TimeDelta kToastShowDuration = base::Milliseconds(400);
constexpr base::TimeDelta kToastHideDuration = base::Milliseconds(200);

gfx::FontList FontOfSize(int size, gfx::Font::Weight weight) {
  const gfx::FontList base;
  return base.DeriveWithSizeDelta(size - base.GetFontSize())
      .DeriveWithWeight(weight);
}

}  // namespace

// Zen's toast: a line of text, optionally with a shortcut in a key cap.
class KoanToastView : public views::View {
  METADATA_HEADER(KoanToastView, views::View)

 public:
  KoanToastView(const std::u16string& text,
                const std::u16string& shortcut,
                const std::u16string& after,
                base::RepeatingCallback<void(bool)> on_hover)
      : on_hover_(std::move(on_hover)) {
    // The labels are event targets too: hovering them counts.
    SetNotifyEnterExitOnChild(true);
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, kToastPadding), kToastGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    const gfx::FontList font =
        FontOfSize(kToastFontSize, gfx::Font::Weight::SEMIBOLD);
    AddChildView(std::make_unique<views::Label>(text))->SetFontList(font);
    if (!shortcut.empty()) {
      views::Label* key =
          AddChildView(std::make_unique<views::Label>(shortcut));
      key->SetFontList(
          FontOfSize(kToastKeyFontSize, gfx::Font::Weight::SEMIBOLD));
      key->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(
          kToastKeyPaddingVertical, kToastKeyPaddingHorizontal)));
      key->SetBackground(views::CreateRoundedRectBackground(
          kColorKoanToastKeyBackground, kToastKeyCornerRadius));
    }
    if (!after.empty()) {
      AddChildView(std::make_unique<views::Label>(after))->SetFontList(font);
    }
    for (views::View* child : children()) {
      auto* label = static_cast<views::Label*>(child);
      label->SetEnabledColor(kColorKoanToastForeground);
      label->SetAutoColorReadabilityEnabled(false);
      label->SetBackgroundColor(SK_ColorTRANSPARENT);
    }
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(
        views::View::CalculatePreferredSize(available_size).width(),
        kToastHeight);
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    // A vertical gradient whose top colour stop sits 40% above the toast,
    // like Zen's linear-gradient(to bottom, primary -40%, ...).
    const ui::ColorProvider* provider = GetColorProvider();
    const SkColor4f colors[] = {
        SkColor4f::FromColor(provider->GetColor(kColorKoanToastBackgroundTop)),
        SkColor4f::FromColor(
            provider->GetColor(kColorKoanToastBackgroundBottom))};
    const SkPoint points[] = {SkPoint::Make(0, -0.4f * height()),
                              SkPoint::Make(0, height())};
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setShader(cc::PaintShader::MakeLinearGradient(points, colors, nullptr,
                                                        2, SkTileMode::kClamp));
    canvas->DrawRoundRect(GetLocalBounds(), kToastCornerRadius, flags);
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    on_hover_.Run(true);
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    on_hover_.Run(false);
  }

 private:
  base::RepeatingCallback<void(bool)> on_hover_;
};

BEGIN_METADATA(KoanToastView)
END_METADATA

KoanToast::KoanToast(BrowserView* browser_view) : browser_view_(browser_view) {}

KoanToast::~KoanToast() {
  Close();
}

void KoanToast::Show(const std::u16string& text,
                     const std::u16string& shortcut,
                     const std::u16string& after,
                     const std::u16string& announcement,
                     base::TimeDelta timeout) {
  Close();
  timeout_ = timeout;
  views::Widget* browser_widget = browser_view_->GetWidget();
  if (!browser_widget) {
    return;
  }

  // A child widget, so the toast sits above the page.
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  params.parent = browser_widget->GetNativeView();
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.name = "KoanToast";
  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(params));
  view_ = widget_->SetContentsView(std::make_unique<KoanToastView>(
      text, shortcut, after,
      base::BindRepeating(&KoanToast::OnHovered, weak_factory_.GetWeakPtr())));
  view_->SetPaintToLayer();
  view_->layer()->SetFillsBoundsOpaquely(false);

  const gfx::Size size = view_->GetPreferredSize();
  OnBrowserViewLayout();
  widget_->Show();
  widget_->LayoutRootViewIfNecessary();
  std::u16string spoken = announcement;
  if (spoken.empty()) {
    spoken = text;
    for (const std::u16string* part : {&shortcut, &after}) {
      if (!part->empty()) {
        spoken += u" " + *part;
      }
    }
  }
  view_->GetViewAccessibility().AnnounceAlert(spoken);

  // Grow from the top centre.
  ui::Layer* layer = view_->layer();
  const gfx::PointF pivot(size.width() / 2.f, 0);
  gfx::Transform start;
  start.Scale(0.01f, 0.01f);
  layer->SetTransform(gfx::TransformAboutPivot(pivot, start));
  {
    ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
    settings.SetTransitionDuration(kToastShowDuration);
    settings.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN_3);
    layer->SetTransform(gfx::Transform());
  }
  // Like Zen, the time on screen counts from when the toast is fully in.
  timer_.Start(FROM_HERE, kToastShowDuration + timeout_,
               base::BindOnce(&KoanToast::Hide, weak_factory_.GetWeakPtr()));
}

void KoanToast::OnBrowserViewLayout() {
  views::Widget* browser_widget = browser_view_->GetWidget();
  if (!widget_ || !browser_widget) {
    return;
  }
  const gfx::Size size = view_->GetPreferredSize();
  const int x = IsSidebarOnRight(browser_view_)
                    ? kToastMargin
                    : browser_widget->GetRootView()->width() - kToastMargin -
                          size.width();
  widget_->SetBounds(gfx::Rect(gfx::Point(x, kToastMargin), size));
}

void KoanToast::OnHovered(bool hovered) {
  // Once it is fading out, the toast goes regardless.
  if (!view_ || hiding_) {
    return;
  }
  if (hovered) {
    timer_.Stop();
  } else {
    timer_.Start(FROM_HERE, timeout_,
                 base::BindOnce(&KoanToast::Hide, weak_factory_.GetWeakPtr()));
  }
}

void KoanToast::Hide() {
  if (!view_) {
    return;
  }
  hiding_ = true;
  ui::Layer* layer = view_->layer();
  const gfx::PointF pivot(view_->width() / 2.f, 0);
  gfx::Transform end;
  end.Scale(0.5f, 0.5f);
  ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
  settings.SetTransitionDuration(kToastHideDuration);
  layer->SetOpacity(0.f);
  layer->SetTransform(gfx::TransformAboutPivot(pivot, end));
  timer_.Start(FROM_HERE, kToastHideDuration,
               base::BindOnce(&KoanToast::Close, weak_factory_.GetWeakPtr()));
}

void KoanToast::Close() {
  timer_.Stop();
  hiding_ = false;
  view_ = nullptr;
  if (widget_) {
    widget_->CloseNow();
    widget_.reset();
  }
}

}  // namespace koan
