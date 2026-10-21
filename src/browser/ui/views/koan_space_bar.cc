// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_bar.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"

namespace koan {

namespace {

// Inactive Spaces are grey and faded (filter: grayscale(1); opacity: 0.7),
// changing over 200 ms.
constexpr float kInactiveOpacity = 0.7f;
constexpr base::TimeDelta kTransitionDuration = base::Milliseconds(200);

}  // namespace

// One Space in the bottom bar.
class KoanSpaceButton : public views::Button,
                        public views::ContextMenuController {
  METADATA_HEADER(KoanSpaceButton, views::Button)

 public:
  KoanSpaceButton(KoanWorkspaceController* controller,
                  KoanSpaceBarView* bar,
                  const tab_groups::TabGroupId& space)
      : views::Button(base::BindRepeating(&KoanWorkspaceController::SwitchTo,
                                          base::Unretained(controller),
                                          space)),
        controller_(controller),
        bar_(bar),
        space_(space) {
    set_context_menu_controller(this);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }

  const tab_groups::TabGroupId& space() const { return space_; }
  const std::u16string& GetLabel() const { return icon_; }
  bool GetActive() const { return active_; }

  void Set(const KoanWorkspaceController::SpaceInfo& info) {
    const bool changed = icon_ != info.icon || active_ != info.active;
    icon_ = info.icon;
    active_ = info.active;
    SetTooltipText(info.name);
    GetViewAccessibility().SetName(info.name);
    if (changed) {
      UpdateLook(/*animate=*/true);
      SchedulePaint();
    }
  }

  bool IsCollapsed() const {
    return bar_->overflows() && !active_ && GetState() != STATE_HOVERED;
  }

  int GetLayoutWidth() const {
    return IsCollapsed() ? kSpaceButtonCollapsedWidth : kSpaceButtonSize;
  }

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* provider = GetColorProvider();
    const gfx::RectF bounds(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      flags.setColor(provider->GetColor(kColorKoanSpaceButtonHovered));
      canvas->DrawRoundRect(bounds, kSpaceButtonCornerRadius, flags);
    }
    if (IsCollapsed() || icon_.empty()) {
      flags.setColor(provider->GetColor(kColorKoanSpaceDot));
      canvas->DrawCircle(
          bounds.CenterPoint(),
          (IsCollapsed() ? kSpaceCollapsedDotSize : kSpaceDotSize) / 2.f,
          flags);
      return;
    }
    canvas->DrawStringRectWithFlags(
        icon_,
        gfx::FontList().DeriveWithSizeDelta(kSpaceButtonEmojiSize -
                                            gfx::FontList().GetFontSize()),
        provider->GetColor(kColorKoanSpaceForeground), GetLocalBounds(),
        gfx::Canvas::TEXT_ALIGN_CENTER);
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    UpdateLook(/*animate=*/true);
    if (bar_->overflows()) {
      bar_->InvalidateLayout();
    }
  }

  void AddedToWidget() override {
    views::Button::AddedToWidget();
    UpdateLook(/*animate=*/false);
  }

  bool OnMouseWheel(const ui::MouseWheelEvent& event) override {
    return controller_->OnMouseWheel(event);
  }

  // views::ContextMenuController: Zen's Space menu, for this Space.
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override {
    controller_->ShowSpaceMenu(space_, this, point);
  }

 private:
  void UpdateLook(bool animate) {
    const bool lit =
        active_ || GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    std::unique_ptr<ui::ScopedLayerAnimationSettings> settings;
    if (animate) {
      settings = std::make_unique<ui::ScopedLayerAnimationSettings>(
          layer()->GetAnimator());
      settings->SetTransitionDuration(kTransitionDuration);
    }
    layer()->SetLayerGrayscale(lit ? 0.f : 1.f);
    layer()->SetOpacity(lit ? 1.f : kInactiveOpacity);
  }

  const raw_ptr<KoanWorkspaceController> controller_;
  const raw_ptr<KoanSpaceBarView> bar_;
  const tab_groups::TabGroupId space_;
  std::u16string icon_;
  bool active_ = false;
};

BEGIN_METADATA(KoanSpaceButton)
ADD_READONLY_PROPERTY_METADATA(std::u16string, Label)
ADD_READONLY_PROPERTY_METADATA(bool, Active)
END_METADATA

KoanSpaceBarView::KoanSpaceBarView(KoanWorkspaceController* controller)
    : controller_(controller) {
  auto create = views::CreateVectorImageButtonWithNativeTheme(
      views::Button::PressedCallback(), kAddIcon, /*dip_size=*/16);
  create->SetCallback(base::BindRepeating(
      [](KoanWorkspaceController* controller, views::Button* button) {
        controller->ShowCreateMenu(button);
      },
      base::Unretained(controller), base::Unretained(create.get())));
  create->SetTooltipText(u"Create New");
  create->GetViewAccessibility().SetName(u"Create New");
  create_button_ = AddChildView(std::move(create));
}

KoanSpaceBarView::~KoanSpaceBarView() = default;

void KoanSpaceBarView::Update(
    const std::vector<KoanWorkspaceController::SpaceInfo>& spaces) {
  std::vector<raw_ptr<KoanSpaceButton>> buttons;
  for (const KoanWorkspaceController::SpaceInfo& space : spaces) {
    auto it = std::ranges::find(buttons_, space.id, &KoanSpaceButton::space);
    KoanSpaceButton* button = nullptr;
    if (it != buttons_.end()) {
      button = *it;
      buttons_.erase(it);
    } else {
      button = AddChildView(
          std::make_unique<KoanSpaceButton>(controller_, this, space.id));
    }
    button->Set(space);
    // In the Spaces' order, for focus and accessibility too.
    ReorderChildView(button, buttons.size());
    buttons.push_back(button);
  }
  // The buttons left over are for Spaces that are gone.
  std::vector<raw_ptr<KoanSpaceButton>> stale =
      std::exchange(buttons_, std::move(buttons));
  for (raw_ptr<KoanSpaceButton>& button : stale) {
    KoanSpaceButton* gone = button.get();
    button = nullptr;
    RemoveChildViewT(gone);
  }
  ReorderChildView(create_button_, children().size());
  // Zen hides the buttons while there is only one Space.
  for (KoanSpaceButton* button : buttons_) {
    button->SetVisible(buttons_.size() > 1);
  }
  InvalidateLayout();
}

gfx::Size KoanSpaceBarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(0, kSpaceButtonSize + 2 * kSpaceBarPadding);
}

void KoanSpaceBarView::Layout(PassKey) {
  const gfx::Rect area = GetLocalBounds();
  const int y = (area.height() - kSpaceButtonSize) / 2;
  create_button_->SetBounds(
      area.right() - SidebarStripInset() - kSpaceButtonSize, y,
      kSpaceButtonSize, kSpaceButtonSize);
  // The Space buttons are centred in the room left of "+", keeping as much
  // room on the other side (where Zen has its sidebar button).
  const int side = SidebarStripInset() + kSpaceButtonSize + kSpaceButtonGap;
  const int room = std::max(0, area.width() - 2 * side);
  const int count = static_cast<int>(buttons_.size());
  overflows_ = count * kSpaceButtonSize + (count - 1) * kSpaceButtonGap > room;
  const int gap = overflows_ ? 0 : kSpaceButtonGap;
  int total = 0;
  for (KoanSpaceButton* button : buttons_) {
    total += button->GetLayoutWidth() + gap;
  }
  total -= gap;
  int x = side + (room - total) / 2;
  for (KoanSpaceButton* button : buttons_) {
    const int width = button->GetLayoutWidth();
    button->SetBounds(x, y, width, kSpaceButtonSize);
    x += width + gap;
  }
}

bool KoanSpaceBarView::OnMouseWheel(const ui::MouseWheelEvent& event) {
  return controller_->OnMouseWheel(event);
}

BEGIN_METADATA(KoanSpaceBarView)
END_METADATA

}  // namespace koan
