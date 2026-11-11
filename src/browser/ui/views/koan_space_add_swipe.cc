// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenSpaceAddSwipe.mjs, zen-single-components.css
//   (.zen-swipe-add-space-*), zen-animations.css (zen-swipe-add-icon-jump)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_space_add_swipe.h"

#include <algorithm>
#include <cmath>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_swipe_edge_actions.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/effects/SkGradient.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace koan {

namespace {

// .zen-swipe-add-space-container: 100px wide at the edge; it comes in from
// 55% of that further out (ENTRY_TRAVEL).
constexpr float kContainerWidth = 100;
constexpr float kEntryTravel = 0.55f * kContainerWidth;
// .zen-swipe-add-space-progress-badge: 30px round, 10px in from the edge,
// centred vertically, with box-shadow 0 1px 8px #0003.
constexpr float kBadgeSize = 30;
constexpr float kBadgeInset = 10;
constexpr int kBadgeShadowY = 1;
constexpr double kBadgeShadowBlur = 8;
// The plus: 55% of the badge.
constexpr int kIconSize = 17;
// The ring: 2.4px, filling the badge (15px) once ready, in 0.2s.
constexpr float kRingThickness = 2.4f;
constexpr float kRingReadyThickness = 15;
constexpr base::TimeDelta kReadyDuration = base::Milliseconds(200);
// zen-swipe-add-icon-jump: 1, 1.15, 1 over 0.2s.
constexpr float kJumpScale = 0.15f;
// .zen-swipe-add-space-background: 200px wide, 50px past the edge, a
// radial-gradient(circle at the edge) of the accent fading out by 30% (20%
// with the tabs on the right) of the farthest corner, scaled (entry, 3) from
// the edge.
constexpr float kGlowWidth = 200;
constexpr float kGlowOverhang = 50;
constexpr float kGlowStop = 0.3f;
constexpr float kGlowStopOnLeft = 0.2f;
constexpr float kGlowScaleY = 3;

// CSS gradients interpolate in premultiplied colour.
SkGradient::Interpolation CssInterpolation() {
  SkGradient::Interpolation interpolation;
  interpolation.fInPremul = SkGradient::Interpolation::InPremul::kYes;
  return interpolation;
}

}  // namespace

KoanSpaceAddSwipeView::KoanSpaceAddSwipeView() {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetIsIgnored(true);
  ready_animation_.SetDuration(kReadyDuration);
  SetVisible(false);
}

KoanSpaceAddSwipeView::~KoanSpaceAddSwipeView() = default;

void KoanSpaceAddSwipeView::Start(bool on_right) {
  out_animation_.Stop();
  ready_animation_.Stop();
  jumping_ = false;
  shown_ = true;
  on_right_ = on_right;
  progress_ = 0;
  thickness_from_ = thickness_to_ = kRingThickness;
  SetVisible(true);
  SchedulePaint();
}

void KoanSpaceAddSwipeView::SetProgress(float progress) {
  const bool was_ready = ready();
  progress_ = progress;
  if (ready() != was_ready) {
    thickness_from_ = RingThickness();
    thickness_to_ = ready() ? kRingReadyThickness : kRingThickness;
    // The badge jumps as it becomes ready.
    jumping_ = ready();
    ready_animation_.Stop();
    if (gfx::Animation::ShouldRenderRichAnimation()) {
      ready_animation_.Start();
    } else {
      thickness_from_ = thickness_to_;
      jumping_ = false;
    }
  }
  SchedulePaint();
}

void KoanSpaceAddSwipeView::AnimateOut(base::TimeDelta duration) {
  if (!shown_) {
    return;
  }
  if (!gfx::Animation::ShouldRenderRichAnimation()) {
    Stop();
    return;
  }
  out_from_ = progress_;
  out_animation_.SetDuration(duration);
  out_animation_.Start();
}

void KoanSpaceAddSwipeView::Stop() {
  out_animation_.Stop();
  ready_animation_.Stop();
  jumping_ = false;
  progress_ = 0;
  shown_ = false;
  SchedulePaint();
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanSpaceAddSwipeView::HideIfStopped,
                                weak_factory_.GetWeakPtr()));
}

void KoanSpaceAddSwipeView::HideIfStopped() {
  // (Hidden, it is no part of the sidebar's window-drag hit test.)
  if (!shown_) {
    SetVisible(false);
  }
}

bool KoanSpaceAddSwipeView::ready() const {
  return AddSpaceSwipeSucceeds(progress_);
}

float KoanSpaceAddSwipeView::RingThickness() const {
  if (!ready_animation_.is_animating()) {
    return thickness_to_;
  }
  return gfx::Tween::FloatValueBetween(
      gfx::Tween::CalculateValue(gfx::Tween::EASE_IN_OUT,
                                 ready_animation_.GetCurrentValue()),
      thickness_from_, thickness_to_);
}

float KoanSpaceAddSwipeView::JumpScale() const {
  if (!jumping_ || !ready_animation_.is_animating()) {
    return 1;
  }
  // Up to 1.15 half way, and back.
  const double t = ready_animation_.GetCurrentValue();
  const double half = t < 0.5 ? t * 2 : (1 - t) * 2;
  return 1 + kJumpScale * static_cast<float>(gfx::Tween::CalculateValue(
                              gfx::Tween::EASE_IN_OUT, half));
}

void KoanSpaceAddSwipeView::OnPaint(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!shown_ || !colors || width() == 0 || height() == 0) {
    return;
  }
  const float entry = AddSpaceSwipeEntry(progress_);
  // Outward from the edge the badge is on.
  const float outward = on_right_ ? 1.f : -1.f;
  const float edge = on_right_ ? width() : 0.f;
  const float mid_y = height() / 2.f;

  // The glow, centred past the edge.
  if (entry > 0) {
    const SkPoint center = SkPoint::Make(edge + outward * kGlowOverhang, mid_y);
    const float radius = std::hypot(kGlowWidth, height() / 2.f);
    const SkColor glow = colors->GetColor(kColorKoanSpaceAddSwipeGlow);
    const SkColor4f stops[] = {
        SkColor4f::FromColor(
            SkColorSetA(glow, static_cast<U8CPU>(SkColorGetA(glow) * entry))),
        SkColors::kTransparent};
    const SkScalar positions[] = {0, on_right_ ? kGlowStop : kGlowStopOnLeft};
    SkMatrix scale;
    scale.setScale(entry, kGlowScaleY, center.x(), center.y());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setShader(cc::PaintShader::MakeRadialGradient(
        center, radius, stops, positions, 2, SkTileMode::kClamp,
        CssInterpolation(), 0, &scale));
    canvas->DrawRect(gfx::RectF(GetLocalBounds()), flags);
  }

  // The badge, sliding in with the glow.
  const float center_x = edge - outward * (kBadgeInset + kBadgeSize / 2) +
                         outward * (1 - entry) * kEntryTravel;
  const gfx::PointF center(center_x, mid_y);
  const float radius = kBadgeSize / 2;
  canvas->Save();
  canvas->sk_canvas()->translate(center.x(), center.y());
  const float scale = JumpScale();
  canvas->Scale(scale, scale);
  const gfx::PointF origin;

  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(colors->GetColor(kColorKoanSpaceAddSwipeBadge));
  fill.setLooper(gfx::CreateShadowDrawLooper(
      {gfx::ShadowValue(gfx::Vector2d(0, kBadgeShadowY), kBadgeShadowBlur,
                        colors->GetColor(kColorKoanSpaceAddSwipeShadow))}));
  canvas->DrawCircle(origin, radius, fill);

  // The ring: an arc, eased, from the bottom round counter-clockwise (Zen's
  // circle turned by rotate(-90deg) scaleX(-1)).
  const float arc = AddSpaceSwipeArc(progress_);
  if (arc > 0) {
    const float thickness = RingThickness();
    const float ring_radius = radius - thickness / 2;
    SkPathBuilder path;
    path.addArc(
        SkRect::MakeLTRB(-ring_radius, -ring_radius, ring_radius, ring_radius),
        90, -360 * arc);
    cc::PaintFlags ring;
    ring.setAntiAlias(true);
    ring.setStyle(cc::PaintFlags::kStroke_Style);
    ring.setStrokeWidth(thickness);
    ring.setColor(colors->GetColor(kColorKoanSpaceAddSwipeRing));
    canvas->DrawPath(path.detach(), ring);
  }

  const gfx::ImageSkia icon = gfx::CreateVectorIcon(
      kKoanPlusIcon, kIconSize,
      colors->GetColor(ready() ? kColorKoanSpaceAddSwipeIconReady
                               : kColorKoanSpaceAddSwipeIcon));
  canvas->DrawImageInt(icon, -kIconSize / 2, -kIconSize / 2);
  canvas->Restore();
}

void KoanSpaceAddSwipeView::AnimationProgressed(
    const gfx::Animation* animation) {
  if (animation == &out_animation_) {
    progress_ = gfx::Tween::FloatValueBetween(
        gfx::Tween::CalculateValue(gfx::Tween::EASE_OUT,
                                   out_animation_.GetCurrentValue()),
        out_from_, 0.f);
  }
  SchedulePaint();
}

void KoanSpaceAddSwipeView::AnimationEnded(const gfx::Animation* animation) {
  if (animation == &out_animation_) {
    Stop();
    return;
  }
  jumping_ = false;
  SchedulePaint();
}

BEGIN_METADATA(KoanSpaceAddSwipeView)
END_METADATA

}  // namespace koan
