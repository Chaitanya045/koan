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
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "ui/accessibility/ax_enums.mojom.h"
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
#include "ui/views/widget/widget.h"

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
    // One of a set of choices: screen readers say which Space is active.
    GetViewAccessibility().SetRole(ax::mojom::Role::kRadioButton);
    GetViewAccessibility().SetCheckedState(ax::mojom::CheckedState::kFalse);
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
    GetViewAccessibility().SetCheckedState(
        active_ ? ax::mojom::CheckedState::kTrue
                : ax::mojom::CheckedState::kFalse);
    if (changed) {
      UpdateLook(/*animate=*/true);
      SchedulePaint();
    }
  }

  bool IsHoveredOrFocused() const {
    return GetState() == STATE_HOVERED || GetState() == STATE_PRESSED ||
           HasFocus();
  }

  // Zen's overflow-icons.inc.css: in the row, a Space shows a small dot
  // instead of its icon unless it is hovered, or it is the current one and
  // no other Space button is hovered.
  bool GetCollapsed() const {
    return bar_->compact() && !IsHoveredOrFocused() &&
           (!active_ || bar_->IsAnyButtonHovered());
  }

  // Refreshes how it looks after a button of the bar changed state.
  void OnBarStateChanged() {
    UpdateLook(/*animate=*/true);
    SchedulePaint();
  }

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* provider = GetColorProvider();
    const gfx::RectF bounds(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    // The row's shrunk buttons have no hover background (Zen's
    // icons-overflow).
    if (!bar_->compact() &&
        (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED)) {
      flags.setColor(provider->GetColor(kColorKoanSpaceButtonHovered));
      canvas->DrawRoundRect(bounds, SpaceButtonCornerRadius(), flags);
    }
    const bool collapsed = GetCollapsed();
    if (collapsed || icon_.empty()) {
      // A dot: `muted-foreground`, `foreground` when lit, in the shadcn style
      // (Zen's is one faint colour that the button's opacity dims).
      flags.setColor(provider->GetColor(look::IsShadcnStyle() && IsLit()
                                            ? kColorKoanSpaceForeground
                                            : kColorKoanSpaceDot));
      canvas->DrawCircle(
          bounds.CenterPoint(),
          (collapsed ? kSpaceCollapsedDotSize : kSpaceDotSize) / 2.f, flags);
      return;
    }
    // In the row the emoji is wider than the shrunk button, and this view's
    // layer would clip it: the bar paints it (PaintIcon()), under it.
    if (!bar_->compact()) {
      PaintIcon(canvas, GetLocalBounds());
    }
  }

  // Whether the bar paints this button's emoji (in the row, while it shows).
  bool BarPaintsIcon() const {
    return GetVisible() && bar_->compact() && !GetCollapsed() && !icon_.empty();
  }

  // The emoji, centred on `bounds` and at least a full button wide (Zen's
  // icon is positioned absolutely, overflowing its button).
  void PaintIcon(gfx::Canvas* canvas, gfx::Rect bounds) const {
    const gfx::FontList font = gfx::FontList().DeriveWithSizeDelta(
        kSpaceButtonEmojiSize - gfx::FontList().GetFontSize());
    const int width = std::max(bounds.width(), kSpaceButtonSize);
    bounds.set_x(bounds.x() + (bounds.width() - width) / 2);
    bounds.set_width(width);
    canvas->DrawStringRectWithFlags(
        icon_, font, GetColorProvider()->GetColor(kColorKoanSpaceForeground),
        bounds, gfx::Canvas::TEXT_ALIGN_CENTER | gfx::Canvas::NO_ELLIPSIS);
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    // The current Space's icon depends on whether another one is hovered.
    bar_->OnButtonStateChanged();
  }

  void AddedToWidget() override {
    views::Button::AddedToWidget();
    UpdateLook(/*animate=*/false);
  }

  void OnFocus() override {
    views::Button::OnFocus();
    bar_->OnButtonStateChanged();
  }

  void OnBlur() override {
    views::Button::OnBlur();
    bar_->OnButtonStateChanged();
  }

  // views::ContextMenuController: Zen's Space menu, for this Space.
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override {
    controller_->ShowSpaceMenu(space_, this, point);
  }

 private:
  bool IsLit() const {
    return active_ || GetState() == STATE_HOVERED ||
           GetState() == STATE_PRESSED;
  }

  void UpdateLook(bool animate) {
    const bool lit = IsLit();
    // A shadcn dot is already the muted colour: dimming it again makes it
    // vanish on a near-black sidebar.
    const bool dimmed =
        !lit && !(look::IsShadcnStyle() && (GetCollapsed() || icon_.empty()));
    std::unique_ptr<ui::ScopedLayerAnimationSettings> settings;
    if (animate) {
      settings = std::make_unique<ui::ScopedLayerAnimationSettings>(
          layer()->GetAnimator());
      settings->SetTransitionDuration(kTransitionDuration);
    }
    layer()->SetLayerGrayscale(lit ? 0.f : 1.f);
    layer()->SetOpacity(dimmed ? kInactiveOpacity : 1.f);
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
ADD_READONLY_PROPERTY_METADATA(bool, Collapsed)
END_METADATA

KoanSpaceBarView::KoanSpaceBarView(KoanWorkspaceController* controller)
    : controller_(controller) {
  // In the shadcn style the + is `muted-foreground`, `foreground` on hover.
  const bool shadcn = look::IsShadcnStyle();
  auto create = views::CreateVectorImageButtonWithNativeTheme(
      views::Button::PressedCallback(), kAddIcon, /*dip_size=*/16,
      shadcn ? ui::ColorId(kColorKoanSpaceDot) : ui::kColorIcon,
      ui::kColorIconDisabled,
      shadcn ? ui::ColorId(kColorKoanSpaceForeground) : ui::kColorIconHovered);
  create->SetCallback(base::BindRepeating(
      [](KoanWorkspaceController* controller, views::Button* button) {
        controller->ShowCreateMenu(button);
      },
      base::Unretained(controller), base::Unretained(create.get())));
  create->SetTooltipText(u"Create New");
  create->GetViewAccessibility().SetName(u"Create New");
  create_button_ = AddChildView(std::move(create));
  GetViewAccessibility().SetRole(ax::mojom::Role::kRadioGroup);
  GetViewAccessibility().SetName(u"Spaces");
}

KoanSpaceBarView::~KoanSpaceBarView() = default;

void KoanSpaceBarView::SetLeadingButton(std::unique_ptr<views::View> button) {
  if (leading_) {
    RemoveChildViewT(std::exchange(leading_, nullptr).get());
  }
  if (button) {
    leading_ = AddChildViewAt(std::move(button), 0);
  }
  PreferredSizeChanged();
}

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
    // In the Spaces' order (after the leading button), for focus and
    // accessibility too.
    ReorderChildView(button, buttons.size() + (leading_ ? 1 : 0));
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
  SchedulePaint();
  // Zen hides the buttons while there is only one Space.
  for (KoanSpaceButton* button : buttons_) {
    button->SetVisible(buttons_.size() > 1);
  }
  InvalidateLayout();
}

bool KoanSpaceBarView::IsAnyButtonHovered() const {
  return std::ranges::any_of(buttons_, [](const KoanSpaceButton* button) {
    return button->GetVisible() && button->IsHoveredOrFocused();
  });
}

void KoanSpaceBarView::OnButtonStateChanged() {
  for (KoanSpaceButton* button : buttons_) {
    button->OnBarStateChanged();
  }
  // The emoji it paints for them.
  SchedulePaint();
}

void KoanSpaceBarView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  // The shown emoji of the row's buttons, which their own layers would clip.
  // Only a lit button shows one (the current or hovered Space), so it has
  // full opacity and colour, like the button over it.
  for (const KoanSpaceButton* button : buttons_) {
    if (button->BarPaintsIcon()) {
      button->PaintIcon(canvas, button->bounds());
    }
  }
}

bool KoanSpaceBarView::IsNarrow(int width) const {
  // No room for the leading button, one Space button and "+" in a row.
  return width < 2 * (kSpaceBarPadding + kSpaceButtonSize + kSpaceBarGap) +
                     kSpaceButtonCompactWidth;
}

gfx::Size KoanSpaceBarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int width = available_size.width().is_bounded()
                        ? available_size.width().value()
                        : this->width();
  int rows = 1;
  if (IsNarrow(width)) {
    rows += static_cast<int>(std::ranges::count_if(
        buttons_,
        [](const KoanSpaceButton* button) { return button->GetVisible(); }));
    if (leading_) {
      ++rows;
    }
  }
  return gfx::Size(0, rows * kSpaceButtonSize + (rows - 1) * kSpaceButtonGap +
                          2 * kSpaceBarPadding);
}

void KoanSpaceBarView::Layout(PassKey) {
  const gfx::Rect area = GetLocalBounds();
  // (In LTR terms; mirrored for right-to-left.)
  const auto place = [this](views::View* view, const gfx::Rect& rect) {
    view->SetBoundsRect(GetMirroredRect(rect));
  };
  const bool compact = !IsNarrow(area.width());
  if (compact != compact_) {
    compact_ = compact;
    OnButtonStateChanged();
  }
  if (!compact) {
    // A column, centred: the leading button, the Spaces, "+".
    const int x = (area.width() - kSpaceButtonSize) / 2;
    int y = kSpaceBarPadding;
    if (leading_) {
      place(leading_, gfx::Rect(x, y, kSpaceButtonSize, kSpaceButtonSize));
      y += kSpaceButtonSize + kSpaceButtonGap;
    }
    for (KoanSpaceButton* button : buttons_) {
      if (button->GetVisible()) {
        place(button, gfx::Rect(x, y, kSpaceButtonSize, kSpaceButtonSize));
        y += kSpaceButtonSize + kSpaceButtonGap;
      }
    }
    place(create_button_, gfx::Rect(x, y, kSpaceButtonSize, kSpaceButtonSize));
    return;
  }
  // Zen's row: the leading button at the start, "+" at the end, the Space
  // switcher between them (4px from each), its buttons centred in it.
  const int y = (area.height() - kSpaceButtonSize) / 2;
  const int end = area.right() - kSpaceBarPadding - kSpaceButtonSize;
  place(create_button_, gfx::Rect(end, y, kSpaceButtonSize, kSpaceButtonSize));
  if (leading_) {
    place(leading_,
          gfx::Rect(kSpaceBarPadding, y, kSpaceButtonSize, kSpaceButtonSize));
  }
  const int start = kSpaceBarPadding + kSpaceButtonSize + kSpaceBarGap;
  const int room = std::max(0, end - kSpaceBarGap - start);
  const int count = static_cast<int>(std::ranges::count_if(
      buttons_,
      [](const KoanSpaceButton* button) { return button->GetVisible(); }));
  if (count == 0) {
    return;
  }
  const int width =
      std::clamp(room / count, kSpaceButtonMinWidth, kSpaceButtonCompactWidth);
  int x = start + std::max(0, (room - count * width) / 2);
  for (KoanSpaceButton* button : buttons_) {
    if (button->GetVisible()) {
      place(button, gfx::Rect(x, y, width, kSpaceButtonSize));
      x += width;
    }
  }
}

void KoanSpaceBarView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  // Between a row and a column (the sidebar collapsed or expanded): the
  // height changes too.
  if (IsNarrow(width()) != IsNarrow(previous_bounds.width())) {
    PreferredSizeChanged();
  }
}

int KoanSpaceBarView::GetWindowTint() const {
  const views::Widget* widget =
      GetWidget() ? GetWidget()->GetTopLevelWidget() : nullptr;
  return widget && widget->user_color_override()
             ? static_cast<int>(*widget->user_color_override())
             : 0;
}

BEGIN_METADATA(KoanSpaceBarView)
ADD_READONLY_PROPERTY_METADATA(int, WindowTint)
END_METADATA

}  // namespace koan
