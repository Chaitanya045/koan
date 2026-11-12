// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_essentials_audio_badge.h"

#include <memory>
#include <optional>
#include <string>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/tabs/alert/tab_alert_controller.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/tab/tab_icon.h"
#include "components/tabs/public/tab_alert.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_essentials.h"
#include "koan/browser/ui/views/koan_essentials_audio.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_utils.h"

namespace koan {

namespace {

class KoanEssentialsAudioBadge : public views::Button {
  METADATA_HEADER(KoanEssentialsAudioBadge, views::Button)

 public:
  explicit KoanEssentialsAudioBadge(TabView* tab_view)
      : views::Button(base::BindRepeating(&KoanEssentialsAudioBadge::OnPressed,
                                          base::Unretained(this))),
        tab_view_(tab_view) {
    // Like the tab view itself: reachable by screen readers, not a Tab stop.
    SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
    GetViewAccessibility().SetName(std::u16string(
        GetEssentialsAudioBadgeTooltip(EssentialsAudioState::kPlaying)));
    SetVisible(false);
    for (views::View* child : tab_view->children()) {
      if (views::IsViewClass<TabIcon>(child)) {
        icon_ = child;
        icon_watcher_.Watch(child);
        break;
      }
    }
    // The tab's alerts (Chromium's audio-playing and muting states), as the
    // tab's own data has them.
    if (tabs::TabInterface* tab =
            const_cast<tabs::TabInterface*>(tab_view->GetTabInterface())) {
      if (tabs::TabAlertController* controller =
              tabs::TabAlertController::From(tab)) {
        alert_subscription_ = controller->AddAlertToShowChangedCallback(
            base::BindRepeating(&KoanEssentialsAudioBadge::OnAlertChanged,
                                base::Unretained(this)));
      }
    }
  }

  void Update() {
    state_ = ComputeAudioState();
    const bool show = state_ != EssentialsAudioState::kNone && icon_ &&
                      icon_->GetVisible() && !icon_->bounds().IsEmpty();
    if (show && !layer()) {
      // Above the favicon, which may paint to a layer of its own. Only once
      // the badge shows: most tabs never need it.
      SetPaintToLayer();
      layer()->SetFillsBoundsOpaquely(false);
    }
    if (show != GetVisible()) {
      SetVisible(show);
    }
    const std::u16string tooltip(GetEssentialsAudioBadgeTooltip(state_));
    if (tooltip != GetTooltipText()) {
      SetTooltipText(tooltip);
      if (!tooltip.empty()) {
        GetViewAccessibility().SetName(tooltip);
      }
    }
    UpdateBounds();
    SchedulePaint();
  }

  // What the badge shows, for tests: "playing", "muted" or "".
  std::u16string GetAudioState() const {
    switch (state_) {
      case EssentialsAudioState::kNone:
        return std::u16string();
      case EssentialsAudioState::kPlaying:
        return u"playing";
      case EssentialsAudioState::kMuted:
        return u"muted";
    }
  }

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    const gfx::RectF bounds(GetLocalBounds());
    const gfx::PointF center = bounds.CenterPoint();
    const float radius = bounds.width() / 2;
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    // Opaque, like the selected tile on the selected tab.
    flags.setColor(colors->GetColor(tab_view_ && tab_view_->IsActive()
                                        ? kColorKoanEssentialsAudioBadgeSelected
                                        : kColorKoanEssentialsAudioBadge));
    canvas->DrawCircle(center, radius, flags);
    if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      flags.setColor(colors->GetColor(
          GetState() == STATE_PRESSED ? kColorKoanEssentialsAudioBadgePressed
                                      : kColorKoanEssentialsAudioBadgeHovered));
      canvas->DrawCircle(center, radius, flags);
    }
    // The ring: a 1px outline inside the badge (outline-offset: -1px).
    if (const SkColor border =
            colors->GetColor(kColorKoanEssentialsAudioBadgeBorder);
        SkColorGetA(border) != SK_AlphaTRANSPARENT) {
      flags.setColor(border);
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(1);
      canvas->DrawCircle(center, radius - 0.5f, flags);
    }
    const gfx::ImageSkia glyph = gfx::CreateVectorIcon(
        state_ == EssentialsAudioState::kMuted ? kKoanTabAudioMutedSmallIcon
                                               : kKoanTabAudioPlayingSmallIcon,
        kEssentialsAudioBadgeGlyphSize,
        colors->GetColor(kColorKoanEssentialsAudioBadgeForeground));
    canvas->DrawImageInt(glyph, (width() - kEssentialsAudioBadgeGlyphSize) / 2,
                         (height() - kEssentialsAudioBadgeGlyphSize) / 2);
  }

 private:
  // Follows the favicon's place and visibility.
  class IconWatcher : public views::ViewObserver {
   public:
    explicit IconWatcher(KoanEssentialsAudioBadge* badge) : badge_(badge) {}

    void Watch(views::View* icon) { observation_.Observe(icon); }

    // views::ViewObserver:
    void OnViewBoundsChanged(views::View* observed) override {
      badge_->Update();
    }
    void OnViewVisibilityChanged(views::View* observed,
                                 views::View* starting_view,
                                 bool visible) override {
      badge_->Update();
    }
    void OnViewIsDeleting(views::View* observed) override {
      observation_.Reset();
      badge_->icon_ = nullptr;
    }

   private:
    const raw_ptr<KoanEssentialsAudioBadge> badge_;
    base::ScopedObservation<views::View, views::ViewObserver> observation_{
        this};
  };

  EssentialsAudioState ComputeAudioState() const {
    const tabs::TabInterface* tab = tab_view_->GetTabInterface();
    if (!tab || !IsEssentialsTile(tab_view_->pinned())) {
      return EssentialsAudioState::kNone;
    }
    const tabs::TabAlertController* controller =
        tabs::TabAlertController::From(tab);
    if (!controller) {
      return EssentialsAudioState::kNone;
    }
    return GetEssentialsAudioState(
        controller->IsAlertActive(tabs::TabAlert::kAudioPlaying),
        controller->IsAlertActive(tabs::TabAlert::kAudioMuting));
  }

  void OnAlertChanged(std::optional<tabs::TabAlert> alert) { Update(); }

  // Zen's click on the overlay: toggles the tab's mute (Chromium's alert
  // indicator's own action), and doesn't select the tab.
  void OnPressed() {
    const tabs::TabInterface* tab = tab_view_->GetTabInterface();
    content::WebContents* contents = tab ? tab->GetContents() : nullptr;
    if (!contents) {
      return;
    }
    SetTabAudioMuted(contents, !contents->IsAudioMuted(),
                     TabMutedReason::kAudioIndicator, std::string());
  }

  void UpdateBounds() {
    if (!icon_) {
      return;
    }
    // The favicon is drawn at the start of the icon's contents (TabIcon).
    gfx::Rect favicon(icon_->GetContentsBounds().origin(),
                      gfx::Size(gfx::kFaviconSize, gfx::kFaviconSize));
    favicon.Offset(icon_->bounds().OffsetFromOrigin());
    SetBoundsRect(GetEssentialsAudioBadgeBounds(favicon));
  }

  // The tab view owns the badge.
  const raw_ptr<TabView> tab_view_;
  raw_ptr<views::View> icon_ = nullptr;
  EssentialsAudioState state_ = EssentialsAudioState::kNone;
  base::CallbackListSubscription alert_subscription_;
  IconWatcher icon_watcher_{this};
};

BEGIN_METADATA(KoanEssentialsAudioBadge)
ADD_READONLY_PROPERTY_METADATA(std::u16string, AudioState)
END_METADATA

}  // namespace

void AddEssentialsAudioBadge(TabView* tab_view) {
  tab_view->AddChildView(std::make_unique<KoanEssentialsAudioBadge>(tab_view))
      ->Update();
}

void UpdateEssentialsAudioBadge(const TabView* tab_view) {
  for (views::View* child : tab_view->children()) {
    if (auto* badge = views::AsViewClass<KoanEssentialsAudioBadge>(child)) {
      badge->Update();
      return;
    }
  }
}

}  // namespace koan
