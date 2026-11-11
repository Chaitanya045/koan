// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_bar.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/vector_icons/koan_icon_sizes.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_haptics.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_space_drag_logic.h"
#include "koan/browser/ui/views/koan_theme_controller.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation.h"
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
// While a Space button is dragged to reorder it, the others (reorder-mode).
constexpr float kReorderOtherOpacity = 0.2f;
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
        (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED ||
         bar_->IsReordered(this))) {
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
    // layer would clip it: the Space switcher paints it (PaintIcon()), under
    // it.
    if (!bar_->compact()) {
      PaintIcon(canvas, GetLocalBounds());
    }
  }

  // Whether the switcher paints this button's emoji (in the row, while it
  // shows).
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
    // Zen scrolls the hovered Space into view (not while reordering).
    if (GetState() == STATE_HOVERED && !bar_->IsReordering()) {
      bar_->ScrollButtonIntoView(this);
    }
  }

  void AddedToWidget() override {
    views::Button::AddedToWidget();
    UpdateLook(/*animate=*/false);
  }

  // Zen's reorder (ZenSpaceIcons.mjs): a plain left press may become a drag
  // along the bar.
  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (event.IsOnlyLeftMouseButton() && !event.IsControlDown() &&
        !event.IsShiftDown() && !event.IsAltDown()) {
      bar_->StartReorder(this, InBar(event.location()));
    }
    return views::Button::OnMousePressed(event);
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    const bool handled = views::Button::OnMouseDragged(event);
    bar_->UpdateReorder(this, InBar(event.location()));
    return handled;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    // The order first; then the click, as in Zen (mouseup, then click).
    bar_->EndReorder(this);
    views::Button::OnMouseReleased(event);
  }

  void OnMouseCaptureLost() override {
    bar_->CancelReorder();
    views::Button::OnMouseCaptureLost();
  }

  void OnFocus() override {
    views::Button::OnFocus();
    bar_->OnButtonStateChanged();
    bar_->ScrollButtonIntoView(this);
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
    if (bar_->IsReordering()) {
      return bar_->IsReordered(this);
    }
    return active_ || GetState() == STATE_HOVERED ||
           GetState() == STATE_PRESSED;
  }

  gfx::Point InBar(const gfx::Point& point) const {
    gfx::Point in_bar = point;
    views::View::ConvertPointToTarget(this, bar_, &in_bar);
    return in_bar;
  }

  void UpdateLook(bool animate) {
    const bool lit = IsLit();
    // A shadcn dot is already the muted colour: dimming it again makes it
    // vanish on a near-black sidebar.
    const bool dimmed =
        !lit && !(look::IsShadcnStyle() && (GetCollapsed() || icon_.empty()));
    const bool reorder_faded = bar_->IsReordering() && !lit;
    std::unique_ptr<ui::ScopedLayerAnimationSettings> settings;
    if (animate) {
      settings = std::make_unique<ui::ScopedLayerAnimationSettings>(
          layer()->GetAnimator());
      settings->SetTransitionDuration(kTransitionDuration);
    }
    layer()->SetLayerGrayscale(lit ? 0.f : 1.f);
    layer()->SetOpacity(reorder_faded ? kReorderOtherOpacity
                        : dimmed      ? kInactiveOpacity
                                      : 1.f);
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

// The Space switcher (Zen's #zen-workspaces-button): holds the Space
// buttons on a layer that clips theirs to its bounds, so however many
// Spaces there are they stay between the leading button and "+", and paints
// their emoji under them.
class KoanSpaceStripView : public views::View {
  METADATA_HEADER(KoanSpaceStripView, views::View)

 public:
  explicit KoanSpaceStripView(KoanSpaceBarView* bar) : bar_(bar) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetMasksToBounds(true);
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    bar_->PaintStripIcons(canvas);
  }

 private:
  const raw_ptr<KoanSpaceBarView> bar_;
};

BEGIN_METADATA(KoanSpaceStripView)
END_METADATA

KoanSpaceBarView::KoanSpaceBarView(KoanWorkspaceController* controller)
    : controller_(controller) {
  // In the shadcn style the + is `muted-foreground`, `foreground` on hover.
  const bool shadcn = look::IsShadcnStyle();
  auto create = views::CreateVectorImageButtonWithNativeTheme(
      views::Button::PressedCallback(), kAddIcon, kCreateIconSize,
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
  strip_ = AddChildView(std::make_unique<KoanSpaceStripView>(this));
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
  std::optional<tab_groups::TabGroupId> active;
  for (const KoanWorkspaceController::SpaceInfo& space : spaces) {
    auto it = std::ranges::find(buttons_, space.id, &KoanSpaceButton::space);
    KoanSpaceButton* button = nullptr;
    if (it != buttons_.end()) {
      button = *it;
      buttons_.erase(it);
    } else {
      button = strip_->AddChildView(
          std::make_unique<KoanSpaceButton>(controller_, this, space.id));
    }
    button->Set(space);
    if (space.active) {
      active = space.id;
    }
    // In the Spaces' order, for focus and accessibility too.
    strip_->ReorderChildView(button, buttons.size());
    buttons.push_back(button);
  }
  // Zen scrolls the current Space into view when it changes.
  if (active != active_space_) {
    active_space_ = active;
    scroll_to_active_ = true;
  }
  // The buttons left over are for Spaces that are gone.
  std::vector<raw_ptr<KoanSpaceButton>> stale =
      std::exchange(buttons_, std::move(buttons));
  for (raw_ptr<KoanSpaceButton>& button : stale) {
    KoanSpaceButton* gone = button.get();
    button = nullptr;
    if (gone == reorder_button_) {
      reorder_button_ = nullptr;
      reordering_ = false;
    }
    strip_->RemoveChildViewT(gone);
  }
  strip_->SchedulePaint();
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
  // The emoji the switcher paints for them.
  strip_->SchedulePaint();
}

std::optional<tab_groups::TabGroupId> KoanSpaceBarView::SpaceAt(
    const gfx::Point& point) const {
  // Not a button the switcher scrolled out of sight.
  const gfx::Point in_strip = ToStrip(point);
  if (!strip_->GetLocalBounds().Contains(in_strip)) {
    return std::nullopt;
  }
  for (const KoanSpaceButton* button : buttons_) {
    if (button->GetVisible() && button->bounds().Contains(in_strip)) {
      return button->space();
    }
  }
  return std::nullopt;
}

bool KoanSpaceBarView::ScrollStrip(const views::View* target,
                                   float x,
                                   float y,
                                   bool precise) {
  if (!compact_ || max_scroll_ == 0 || !target || !strip_->Contains(target)) {
    return false;
  }
  // Along the touchpad's main direction; a mouse wheel's sideways turn is
  // Zen's Space switch, not a scroll.
  const bool sideways = std::abs(x) >= std::abs(y);
  if (!precise && sideways) {
    return false;
  }
  // The offsets move the content: right (or down) scrolls towards the start,
  // which is on the right in a right-to-left row.
  const float delta = sideways ? (base::i18n::IsRTL() ? x : -x) : -y;
  if (delta == 0) {
    return false;
  }
  // A wheel scrolls smoothly, as in Firefox, on from where it is heading;
  // the touchpad follows the fingers from where the strip is.
  const int from =
      !precise && scroll_animation_.is_animating() ? scroll_to_ : scroll_;
  ScrollTo(from + static_cast<int>(std::lround(delta)), /*animate=*/!precise);
  return true;
}

void KoanSpaceBarView::ScrollButtonIntoView(const KoanSpaceButton* button) {
  if (!compact_ || max_scroll_ == 0) {
    return;
  }
  int index = 0;
  for (const KoanSpaceButton* each : buttons_) {
    if (!each->GetVisible()) {
      continue;
    }
    if (each == button) {
      const int start =
          strip_layout_.start + index * strip_layout_.button_width;
      ScrollTo(ScrollSpaceIntoView(
                   scroll_animation_.is_animating() ? scroll_to_ : scroll_,
                   strip_->width(), max_scroll_, start,
                   start + strip_layout_.button_width),
               /*animate=*/true);
      return;
    }
    ++index;
  }
}

void KoanSpaceBarView::ScrollTo(int scroll, bool animate) {
  scroll = std::clamp(scroll, 0, max_scroll_);
  if (animate && GetWidget() && IsDrawn() &&
      gfx::Animation::ShouldRenderRichAnimation()) {
    if (scroll_animation_.is_animating() ? scroll == scroll_to_
                                         : scroll == scroll_) {
      return;
    }
    scroll_animation_.Stop();
    scroll_from_ = scroll_;
    scroll_to_ = scroll;
    scroll_animation_.SetDuration(kSpaceStripScrollDuration);
    scroll_animation_.Start();
    return;
  }
  scroll_animation_.Stop();
  if (scroll != scroll_) {
    scroll_ = scroll;
    InvalidateLayout();
  }
}

void KoanSpaceBarView::AnimationProgressed(const gfx::Animation* animation) {
  const double progress = SpaceStripScrollProgress(
      scroll_animation_.GetCurrentValue() * kSpaceStripScrollDuration);
  scroll_ =
      scroll_from_ +
      static_cast<int>(std::lround((scroll_to_ - scroll_from_) * progress));
  InvalidateLayout();
}

gfx::Point KoanSpaceBarView::ToStrip(const gfx::Point& point) const {
  gfx::Point in_strip = point;
  views::View::ConvertPointToTarget(this, strip_, &in_strip);
  return in_strip;
}

void KoanSpaceBarView::StartReorder(KoanSpaceButton* button,
                                    const gfx::Point& point) {
  CancelReorder();
  reorder_button_ = button;
  // Along the bar: y in a column (Zen: the sidebar not expanded), else x.
  reorder_start_ = compact_ ? point.x() : point.y();
}

void KoanSpaceBarView::UpdateReorder(KoanSpaceButton* button,
                                     const gfx::Point& point) {
  if (button != reorder_button_ || buttons_.size() < 2) {
    return;
  }
  const int along = compact_ ? point.x() : point.y();
  if (!reordering_) {
    if (std::abs(along - reorder_start_) <= kSpaceReorderThreshold) {
      return;
    }
    reordering_ = true;
    OnButtonStateChanged();
  }
  // In the switcher's logical order (a row is mirrored right-to-left).
  std::vector<SpaceButtonSpan> spans;
  for (const KoanSpaceButton* each : buttons_) {
    const gfx::Rect bounds = each->GetMirroredBounds();
    spans.push_back(compact_ ? SpaceButtonSpan{bounds.x(), bounds.right()}
                             : SpaceButtonSpan{bounds.y(), bounds.bottom()});
  }
  const gfx::Point in_strip = ToStrip(point);
  const int pointer =
      compact_ ? strip_->GetMirroredXInView(in_strip.x()) : in_strip.y();
  const auto it = std::ranges::find(buttons_, button);
  const size_t from = static_cast<size_t>(it - buttons_.begin());
  const size_t to = ReorderedSpaceIndex(spans, from, pointer);
  if (to == from) {
    return;
  }
  raw_ptr<KoanSpaceButton> moved = *it;
  buttons_.erase(it);
  buttons_.insert(buttons_.begin() + to, moved);
  strip_->ReorderChildView(button, to);
  InvalidateLayout();
  PlayHapticFeedback();
}

void KoanSpaceBarView::EndReorder(KoanSpaceButton* button) {
  if (button != reorder_button_) {
    return;
  }
  const bool reordered = reordering_;
  reorder_button_ = nullptr;
  reordering_ = false;
  if (!reordered) {
    return;
  }
  OnButtonStateChanged();
  const auto it = std::ranges::find(buttons_, button);
  // Saved with the Spaces (Zen's reorderWorkspace()); the bar follows the
  // new order through Update().
  controller_->MoveSpace(button->space(),
                         static_cast<int>(it - buttons_.begin()));
}

void KoanSpaceBarView::CancelReorder() {
  const bool reordered = reordering_;
  reorder_button_ = nullptr;
  reordering_ = false;
  if (reordered) {
    // Back to the Spaces' order.
    Update(controller_->GetSpaces());
    OnButtonStateChanged();
  }
}

void KoanSpaceBarView::PaintStripIcons(gfx::Canvas* canvas) const {
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
  const bool compact = !IsNarrow(area.width());
  if (compact != compact_) {
    compact_ = compact;
    scroll_to_active_ = true;
    OnButtonStateChanged();
  }
  if (compact) {
    LayoutRow(area);
    return;
  }
  // A column, centred: the leading button, the Spaces, "+". (In LTR terms;
  // mirrored for right-to-left.)
  max_scroll_ = 0;
  const int x = (area.width() - kSpaceButtonSize) / 2;
  int y = kSpaceBarPadding;
  if (leading_) {
    leading_->SetBoundsRect(
        GetMirroredRect(gfx::Rect(x, y, kSpaceButtonSize, kSpaceButtonSize)));
    y += kSpaceButtonSize + kSpaceButtonGap;
  }
  const int count = static_cast<int>(std::ranges::count_if(
      buttons_,
      [](const KoanSpaceButton* button) { return button->GetVisible(); }));
  strip_->SetBoundsRect(GetMirroredRect(gfx::Rect(
      0, y, area.width(),
      std::max(
          0, count * (kSpaceButtonSize + kSpaceButtonGap) - kSpaceButtonGap))));
  int strip_y = 0;
  for (KoanSpaceButton* button : buttons_) {
    if (button->GetVisible()) {
      button->SetBoundsRect(strip_->GetMirroredRect(
          gfx::Rect(x, strip_y, kSpaceButtonSize, kSpaceButtonSize)));
      strip_y += kSpaceButtonSize + kSpaceButtonGap;
    }
  }
  create_button_->SetBoundsRect(GetMirroredRect(
      gfx::Rect(x, y + strip_y, kSpaceButtonSize, kSpaceButtonSize)));
}

void KoanSpaceBarView::LayoutRow(const gfx::Rect& area) {
  // (In LTR terms; mirrored for right-to-left.)
  const auto place = [this](views::View* view, const gfx::Rect& rect) {
    view->SetBoundsRect(GetMirroredRect(rect));
  };
  // Zen's row: the leading button at the start, "+" at the end, the Space
  // switcher between them (4px from each), its buttons centred in it, or
  // scrolling in it when they don't fit.
  const int y = (area.height() - kSpaceButtonSize) / 2;
  const int end = area.right() - kSpaceBarPadding - kSpaceButtonSize;
  place(create_button_, gfx::Rect(end, y, kSpaceButtonSize, kSpaceButtonSize));
  if (leading_) {
    place(leading_,
          gfx::Rect(kSpaceBarPadding, y, kSpaceButtonSize, kSpaceButtonSize));
  }
  const int start = kSpaceBarPadding + kSpaceButtonSize + kSpaceBarGap;
  const int room = std::max(0, end - kSpaceBarGap - start);
  // A smooth scroll only once the switcher is on screen at this size.
  const bool settled = IsDrawn() && strip_->width() == room &&
                       gfx::Animation::ShouldRenderRichAnimation();
  place(strip_, gfx::Rect(start, y, room, kSpaceButtonSize));
  std::vector<KoanSpaceButton*> shown;
  for (KoanSpaceButton* button : buttons_) {
    if (button->GetVisible()) {
      shown.push_back(button);
    }
  }
  strip_layout_ =
      LayoutSpaceStrip(room, static_cast<int>(shown.size()),
                       kSpaceButtonMinWidth, kSpaceButtonCompactWidth);
  max_scroll_ = strip_layout_.max_scroll;
  const int width = strip_layout_.button_width;
  if (std::exchange(scroll_to_active_, false)) {
    const auto active = std::ranges::find_if(
        shown,
        [](const KoanSpaceButton* button) { return button->GetActive(); });
    if (active != shown.end()) {
      const int index = static_cast<int>(active - shown.begin());
      const int button_start = strip_layout_.start + index * width;
      const int target = ScrollSpaceIntoView(
          scroll_animation_.is_animating() ? scroll_to_ : scroll_, room,
          max_scroll_, button_start, button_start + width);
      if (settled) {
        ScrollTo(target, /*animate=*/true);
      } else {
        scroll_animation_.Stop();
        scroll_ = target;
      }
    }
  }
  scroll_ = std::clamp(scroll_, 0, max_scroll_);
  int x = strip_layout_.start - scroll_;
  for (KoanSpaceButton* button : shown) {
    button->SetBoundsRect(
        strip_->GetMirroredRect(gfx::Rect(x, 0, width, kSpaceButtonSize)));
    x += width;
  }
  // The emoji it paints under the buttons moved with them.
  strip_->SchedulePaint();
}

void KoanSpaceBarView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  // Between a row and a column (the sidebar collapsed or expanded): the
  // height changes too.
  if (IsNarrow(width()) != IsNarrow(previous_bounds.width())) {
    PreferredSizeChanged();
  }
}

gfx::Rect KoanSpaceBarView::GetStripBoundsForTesting() const {
  return strip_->bounds();
}

std::optional<gfx::Rect> KoanSpaceBarView::GetSpaceButtonBoundsForTesting(
    const tab_groups::TabGroupId& space) const {
  const auto it = std::ranges::find(buttons_, space, &KoanSpaceButton::space);
  if (it == buttons_.end() || !(*it)->GetVisible()) {
    return std::nullopt;
  }
  gfx::Rect bounds = (*it)->bounds();
  bounds.Offset(strip_->bounds().OffsetFromOrigin());
  return bounds;
}

gfx::Rect KoanSpaceBarView::GetCreateButtonBoundsForTesting() const {
  return create_button_->bounds();
}

bool KoanSpaceBarView::StripClipsForTesting() const {
  return strip_->layer() && strip_->layer()->GetMasksToBounds() &&
         std::ranges::all_of(buttons_, [this](const KoanSpaceButton* button) {
           return button->parent() == strip_;
         });
}

int KoanSpaceBarView::GetWindowTint() const {
  // The accent the window's Space theme puts in its colour key
  // (KoanThemeController), 0 for none.
  const views::Widget* widget =
      GetWidget() ? GetWidget()->GetTopLevelWidget() : nullptr;
  BrowserView* browser_view = widget
                                  ? BrowserView::GetBrowserViewForNativeWindow(
                                        widget->GetNativeWindow())
                                  : nullptr;
  const KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  const std::optional<SkColor> accent =
      controller ? controller->theme()->window_accent() : std::nullopt;
  return accent ? static_cast<int>(*accent) : 0;
}

BEGIN_METADATA(KoanSpaceBarView)
ADD_READONLY_PROPERTY_METADATA(int, WindowTint)
END_METADATA

}  // namespace koan
