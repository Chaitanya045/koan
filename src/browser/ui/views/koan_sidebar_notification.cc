// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_sidebar_notification.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_space_bar.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/browser/ui/views/koan_zen_sidebar_notification.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"

namespace koan {

namespace {

// zen-sidebar-notification.css: --border-radius-medium (the media cards'
// on other platforms, which macOS rounds 2px less).
int NotificationCornerRadius() {
  return look::ScaleRadius(14);
}

gfx::FontList NotificationFont(gfx::Font::Weight weight) {
  const gfx::FontList base;
  return base
      .DeriveWithSizeDelta(kSidebarNotificationFontSize - base.GetFontSize())
      .DeriveWithWeight(weight);
}

SkColor WithOpacity(SkColor color, float opacity) {
  return SkColorSetA(color, static_cast<SkAlpha>(SkColorGetA(color) * opacity));
}

// The close button: the icon at 40 %, a 15 % background on hover.
class CloseButton : public views::Button {
  METADATA_HEADER(CloseButton, views::Button)

 public:
  explicit CloseButton(base::RepeatingClosure close)
      : views::Button(std::move(close)) {
    SetTooltipText(u"Close");
    GetViewAccessibility().SetName(u"Close");
    views::InstallRoundRectHighlightPathGenerator(
        this, gfx::Insets(), kSidebarNotificationCloseRadius);
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kSidebarNotificationCloseSize,
                     kSidebarNotificationCloseSize);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const SkColor color =
        GetColorProvider()->GetColor(ui::kColorLabelForeground);
    if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(WithOpacity(color, kSidebarNotificationCloseHoverOpacity));
      canvas->DrawRoundRect(GetLocalBounds(), kSidebarNotificationCloseRadius,
                            flags);
    }
    const int x = (width() - kSidebarNotificationCloseIconSize) / 2;
    const int y = (height() - kSidebarNotificationCloseIconSize) / 2;
    canvas->DrawImageInt(
        gfx::CreateVectorIcon(
            kKoanCloseIcon, kSidebarNotificationCloseIconSize,
            WithOpacity(color, kSidebarNotificationCloseOpacity)),
        x, y);
  }
};

BEGIN_METADATA(CloseButton)
END_METADATA

// A link row: its icon and label at 70 %, fully opaque on hover (0.2 s), or
// always for the special row, which has the accent colour (Zen animates a
// gradient through its text).
class LinkRow : public views::Button {
  METADATA_HEADER(LinkRow, views::Button)

 public:
  LinkRow(SidebarNotificationLink link, base::RepeatingClosure close)
      : views::Button(
            base::BindRepeating(&LinkRow::OnPressed, base::Unretained(this))),
        icon_(*link.icon),
        label_(std::move(link.label)),
        special_(link.special),
        action_(std::move(link.action)),
        close_(std::move(close)),
        font_(NotificationFont(gfx::Font::Weight::NORMAL)) {
    SetTooltipText(link.tooltip);
    GetViewAccessibility().SetName(label_);
    views::InstallRoundRectHighlightPathGenerator(
        this, gfx::Insets(), kSidebarNotificationCloseRadius);
    hover_.SetSlideDuration(kSidebarNotificationLinkFade);
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(
        0, 2 * kSidebarNotificationLinkPadding +
               std::max(kSidebarNotificationLinkIconSize, font_.GetHeight()));
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      hover_.Show();
    } else {
      hover_.Hide();
    }
  }

  void AnimationProgressed(const gfx::Animation* animation) override {
    SchedulePaint();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    const float opacity = special_ ? 1.0f
                                   : gfx::Tween::FloatValueBetween(
                                         hover_.GetCurrentValue(),
                                         kSidebarNotificationLinkOpacity, 1.0f);
    const SkColor color =
        WithOpacity(colors->GetColor(special_ ? ui::kColorSysPrimary
                                              : ui::kColorLabelForeground),
                    opacity);
    const int icon_y = (height() - kSidebarNotificationLinkIconSize) / 2;
    const gfx::Rect icon(kSidebarNotificationLinkPadding, icon_y,
                         kSidebarNotificationLinkIconSize,
                         kSidebarNotificationLinkIconSize);
    const gfx::Rect mirrored_icon = GetMirroredRect(icon);
    canvas->DrawImageInt(
        gfx::CreateVectorIcon(*icon_, kSidebarNotificationLinkIconSize, color),
        mirrored_icon.x(), mirrored_icon.y());
    const int text_x = icon.right() + kSidebarNotificationLinkIconGap;
    canvas->DrawStringRectWithFlags(
        label_, font_, color,
        GetMirroredRect(gfx::Rect(
            text_x, 0,
            std::max(0, width() - text_x - kSidebarNotificationLinkPadding),
            height())),
        gfx::Canvas::TEXT_ALIGN_TO_HEAD);
  }

 private:
  void OnPressed() {
    // Zen closes the card after a link (not after its own actions; Koan's
    // links are all one-shot).
    if (action_) {
      std::move(action_).Run();
    }
    close_.Run();
  }

  const raw_ref<const gfx::VectorIcon> icon_;
  const std::u16string label_;
  const bool special_;
  base::OnceClosure action_;
  base::RepeatingClosure close_;
  const gfx::FontList font_;
  gfx::SlideAnimation hover_{this};
};

BEGIN_METADATA(LinkRow)
END_METADATA

// The countdown line under the header (1px, scaled from full to empty).
class ProgressLine : public views::View {
  METADATA_HEADER(ProgressLine, views::View)

 public:
  void SetFraction(double fraction) {
    if (fraction_ != fraction) {
      fraction_ = fraction;
      SchedulePaint();
    }
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, 1);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    // Zen's light-dark(primary 50% black, primary 15% #6b6b6b): the themed
    // icon fill the media cards' progress bar has too.
    gfx::Rect bar = GetLocalBounds();
    bar.set_width(static_cast<int>(bar.width() * fraction_));
    canvas->FillRect(GetMirroredRect(bar),
                     GetColorProvider()->GetColor(kColorKoanMediaProgress));
  }

 private:
  double fraction_ = 1.0;
};

BEGIN_METADATA(ProgressLine)
END_METADATA

}  // namespace

SidebarNotificationLink::SidebarNotificationLink(const gfx::VectorIcon& icon,
                                                 std::u16string label,
                                                 std::u16string tooltip,
                                                 base::OnceClosure action,
                                                 bool special)
    : icon(icon),
      label(std::move(label)),
      tooltip(std::move(tooltip)),
      action(std::move(action)),
      special(special) {}
SidebarNotificationLink::SidebarNotificationLink(SidebarNotificationLink&&) =
    default;
SidebarNotificationLink& SidebarNotificationLink::operator=(
    SidebarNotificationLink&&) = default;
SidebarNotificationLink::~SidebarNotificationLink() = default;

SidebarNotificationParams::SidebarNotificationParams() = default;
SidebarNotificationParams::SidebarNotificationParams(
    SidebarNotificationParams&&) = default;
SidebarNotificationParams& SidebarNotificationParams::operator=(
    SidebarNotificationParams&&) = default;
SidebarNotificationParams::~SidebarNotificationParams() = default;

// The card. Owned by the sidebar's column; removes itself when closed.
class KoanSidebarNotification : public views::View,
                                public gfx::AnimationDelegate {
  METADATA_HEADER(KoanSidebarNotification, views::View)

 public:
  explicit KoanSidebarNotification(SidebarNotificationParams params)
      : auto_hide_(params.auto_hide), delay_(params.delay) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetOpacity(0.0f);
    SetNotifyEnterExitOnChild(true);
    GetViewAccessibility().SetRole(ax::mojom::Role::kAlert);
    GetViewAccessibility().SetName(params.heading);

    heading_ = AddChildView(std::make_unique<views::Label>(params.heading));
    heading_->SetFontList(NotificationFont(gfx::Font::Weight::MEDIUM));
    heading_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    heading_->SetElideBehavior(gfx::ELIDE_TAIL);
    close_ = AddChildView(std::make_unique<CloseButton>(base::BindRepeating(
        &KoanSidebarNotification::Close, base::Unretained(this))));
    if (auto_hide_.is_positive()) {
      progress_ = AddChildView(std::make_unique<ProgressLine>());
    }
    for (SidebarNotificationLink& link : params.links) {
      links_.push_back(AddChildView(std::make_unique<LinkRow>(
          std::move(link), base::BindRepeating(&KoanSidebarNotification::Close,
                                               base::Unretained(this)))));
    }
  }

  // Slides in after the delay (Zen's second), then counts down.
  void Start() {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&KoanSidebarNotification::AnimateIn,
                       weak_factory_.GetWeakPtr()),
        delay_);
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    int height = 2 * kMediaCardShadowMargin + HeaderHeight() +
                 2 * kSidebarNotificationBodyPadding;
    for (size_t i = 0; i < links_.size(); ++i) {
      height += links_[i]->GetPreferredSize().height() +
                (i ? kSidebarNotificationLinkGap : 0);
    }
    return gfx::Size(0, height);
  }

  void Layout(PassKey) override {
    const gfx::Rect card = CardBounds();
    const int header = HeaderHeight();
    const int heading_height = heading_->GetPreferredSize().height();
    heading_->SetBounds(
        card.x() + kSidebarNotificationHeadingInset,
        card.y() + (header - heading_height) / 2,
        std::max(0, card.width() - 2 * kSidebarNotificationHeadingInset),
        heading_height);
    close_->SetBounds(card.right() - kSidebarNotificationCloseInset -
                          kSidebarNotificationCloseSize,
                      card.y() + kSidebarNotificationCloseInset,
                      kSidebarNotificationCloseSize,
                      kSidebarNotificationCloseSize);
    if (progress_) {
      // Over the header's bottom line (translateY(-1px)).
      progress_->SetBounds(card.x(), card.y() + header - 1, card.width(), 1);
    }
    int y = card.y() + header + kSidebarNotificationBodyPadding;
    for (LinkRow* link : links_) {
      const int height = link->GetPreferredSize().height();
      link->SetBounds(
          card.x() + kSidebarNotificationBodyPadding, y,
          std::max(0, card.width() - 2 * kSidebarNotificationBodyPadding),
          height);
      y += height + kSidebarNotificationLinkGap;
    }
  }

  void OnPaint(gfx::Canvas* canvas) override {
    // zen-theme.css: --zen-sidebar-notification-bg, -shadow and -outline,
    // as the media cards.
    const ui::ColorProvider* colors = GetColorProvider();
    gfx::RectF card(CardBounds());
    const float radius = NotificationCornerRadius();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kColorKoanMediaCard));
    flags.setLooper(gfx::CreateShadowDrawLooper(
        {gfx::ShadowValue(gfx::Vector2d(), kMediaCardShadowBlur,
                          colors->GetColor(kColorKoanMediaCardShadow))}));
    canvas->DrawRoundRect(card, radius, flags);
    flags.setLooper(nullptr);
    // The header's bottom line: currentColor at 10 %.
    canvas->FillRect(gfx::Rect(static_cast<int>(card.x()),
                               static_cast<int>(card.y()) + HeaderHeight() - 1,
                               static_cast<int>(card.width()), 1),
                     WithOpacity(colors->GetColor(ui::kColorLabelForeground),
                                 kSidebarNotificationBorderOpacity));
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(colors->GetColor(kColorKoanMediaCardOutline));
    card.Inset(0.5f);
    canvas->DrawRoundRect(card, radius - 0.5f, flags);
  }

  // Zen holds the countdown while the pointer is over the card.
  void OnMouseEntered(const ui::MouseEvent& event) override { Pause(); }
  void OnMouseExited(const ui::MouseEvent& event) override { Resume(); }

  // gfx::AnimationDelegate (the countdown):
  void AnimationProgressed(const gfx::Animation* animation) override {
    progress_->SetFraction(1.0 - CountdownDone());
  }
  void AnimationEnded(const gfx::Animation* animation) override { Close(); }
  // (Canceled: paused.)

 private:
  int HeaderHeight() const {
    return 2 * kSidebarNotificationHeaderPadding +
           heading_->GetPreferredSize().height();
  }

  // The card inside the shadow's margin, in line with the tab rows.
  gfx::Rect CardBounds() const {
    gfx::Rect card = GetLocalBounds();
    card.Inset(
        gfx::Insets::VH(kMediaCardShadowMargin,
                        std::max(kMediaCardShadowMargin, SidebarStripInset())));
    return card;
  }

  // How much of the countdown has run, 0..1.
  double CountdownDone() const {
    return paused_done_ +
           (1.0 - paused_done_) *
               (countdown_ ? countdown_->GetCurrentValue() : 0.0);
  }

  void AnimateIn() {
    gfx::Transform offset;
    offset.Translate(0, kSidebarNotificationInOffset);
    layer()->SetTransform(offset);
    views::AnimationBuilder()
        .OnEnded(base::BindOnce(&KoanSidebarNotification::StartCountdown,
                                weak_factory_.GetWeakPtr()))
        .Once()
        .SetDuration(gfx::Animation::ShouldRenderRichAnimation()
                         ? kSidebarNotificationAnimation
                         : base::TimeDelta())
        .SetOpacity(this, 1.0f, gfx::Tween::EASE_OUT)
        .SetTransform(this, gfx::Transform(), gfx::Tween::EASE_OUT);
    GetViewAccessibility().AnnouncePolitely(heading_->GetText());
  }

  void StartCountdown() {
    if (!progress_ || closing_) {
      return;
    }
    countdown_ = std::make_unique<gfx::LinearAnimation>(auto_hide_, 60, this);
    if (IsMouseHovered()) {
      return;
    }
    countdown_->Start();
  }

  void Pause() {
    if (!countdown_ || !countdown_->is_animating()) {
      return;
    }
    paused_done_ = CountdownDone();
    countdown_->Stop();
  }

  void Resume() {
    if (!countdown_ || countdown_->is_animating() || closing_) {
      return;
    }
    countdown_->SetDuration(auto_hide_ * (1.0 - paused_done_));
    countdown_->Start();
  }

  void Close() {
    if (closing_) {
      return;
    }
    closing_ = true;
    countdown_.reset();
    SetCanProcessEventsWithinSubtree(false);
    gfx::Transform offset;
    offset.Translate(0, kSidebarNotificationOutOffset);
    views::AnimationBuilder()
        .OnEnded(base::BindOnce(&KoanSidebarNotification::Remove,
                                weak_factory_.GetWeakPtr()))
        .OnAborted(base::BindOnce(&KoanSidebarNotification::Remove,
                                  weak_factory_.GetWeakPtr()))
        .Once()
        .SetDuration(gfx::Animation::ShouldRenderRichAnimation()
                         ? kSidebarNotificationAnimation
                         : base::TimeDelta())
        .SetOpacity(this, 0.0f)
        .SetTransform(this, offset);
  }

  void Remove() {
    // Not from inside the animator's callback, which runs on this view's
    // layer.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<KoanSidebarNotification> self) {
                         if (self && self->parent()) {
                           self->parent()->RemoveChildViewT(self.get());
                         }
                       },
                       weak_factory_.GetWeakPtr()));
  }

  const base::TimeDelta auto_hide_;
  const base::TimeDelta delay_;
  raw_ptr<views::Label> heading_ = nullptr;
  raw_ptr<CloseButton> close_ = nullptr;
  raw_ptr<ProgressLine> progress_ = nullptr;
  std::vector<raw_ptr<LinkRow>> links_;
  std::unique_ptr<gfx::LinearAnimation> countdown_;
  // The countdown before its last pause.
  double paused_done_ = 0.0;
  bool closing_ = false;
  base::WeakPtrFactory<KoanSidebarNotification> weak_factory_{this};
};

BEGIN_METADATA(KoanSidebarNotification)
END_METADATA

bool ShowSidebarNotification(BrowserView* browser_view,
                             SidebarNotificationParams params) {
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view);
  // Above the Spaces' bottom bar, in the sidebar's column, after the media
  // controls (also installed before the bar) when they are already there.
  views::View* bar = controller && controller->workspaces()
                         ? controller->workspaces()->space_bar()
                         : nullptr;
  views::View* column = bar ? bar->parent() : nullptr;
  const views::View* region = browser_view->tab_strip_view();
  if (!column || !region || region->width() < kMediaMinSidebarWidth) {
    return false;
  }
  auto* notification = column->AddChildViewAt(
      std::make_unique<KoanSidebarNotification>(std::move(params)),
      column->GetIndexOf(bar).value_or(column->children().size()));
  notification->Start();
  return true;
}

}  // namespace koan
