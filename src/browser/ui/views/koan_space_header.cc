// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_header.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_space_creation.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/proposed_layout.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_utils.h"

namespace koan {

KoanSpaceHeaderView::KoanSpaceHeaderView(KoanWorkspaceController* controller)
    : controller_(controller) {
  set_context_menu_controller(this);
  SetNotifyEnterExitOnChild(true);
  // A button that opens the Space menu, for keyboards and screen readers.
  SetFocusBehavior(FocusBehavior::ALWAYS);
  GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
  GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);
  GetViewAccessibility().SetDescription(
      u"Space. Enter opens its menu, F2 renames it");
  views::InstallRoundRectHighlightPathGenerator(
      this, gfx::Insets::VH(kSpaceHeaderBackgroundInset, SidebarStripInset()),
      SidebarRowCornerRadius());
  views::FocusRing::Install(this);

  icon_ = AddChildView(std::make_unique<views::Label>());
  icon_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kSpaceIconSize - gfx::FontList().GetFontSize()));
  icon_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  icon_->SetElideBehavior(gfx::NO_ELIDE);
  icon_->SetCanProcessEventsWithinSubtree(false);
  // The sidebar paints to a transparent layer.
  icon_->SetSubpixelRenderingEnabled(false);
  icon_->SetEnabledColor(kColorKoanSpaceForeground);

  name_ = AddChildView(std::make_unique<views::Label>());
  name_->SetFontList(
      gfx::FontList().DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));
  name_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  name_->SetElideBehavior(gfx::ELIDE_TAIL);
  name_->SetCanProcessEventsWithinSubtree(false);
  name_->SetSubpixelRenderingEnabled(false);
  name_->SetEnabledColor(kColorKoanSpaceForeground);

  rename_field_ = AddChildView(std::make_unique<views::Textfield>());
  rename_field_->SetFontList(name_->font_list());
  rename_field_->SetController(this);
  rename_field_->GetViewAccessibility().SetName(u"Space Name");
  rename_field_->SetVisible(false);
  child_observations_.AddObservation(rename_field_);

  actions_ = AddChildView(views::CreateVectorImageButtonWithNativeTheme(
      base::BindRepeating(
          [](KoanSpaceHeaderView* header) {
            gfx::Point point(header->actions_->bounds().bottom_left());
            views::View::ConvertPointToScreen(header, &point);
            header->ShowMenu(point);
          },
          base::Unretained(this)),
      views::kMoreHorizIcon, /*dip_size=*/16));
  actions_->SetTooltipText(u"Space options");
  actions_->GetViewAccessibility().SetName(u"Space options");
  // Always in the focus order; drawn only while the header is hovered or
  // focused.
  actions_->SetPaintToLayer();
  actions_->layer()->SetFillsBoundsOpaquely(false);
  actions_->layer()->SetOpacity(0);
  child_observations_.AddObservation(actions_);
}

KoanSpaceHeaderView::~KoanSpaceHeaderView() = default;

void KoanSpaceHeaderView::Update(const std::u16string& icon,
                                 const std::u16string& name) {
  has_space_ = true;
  icon_->SetText(icon);
  icon_->SetVisible(!icon.empty());
  name_->SetText(name);
  SetTooltipText(name);
  GetViewAccessibility().SetName(name);
  InvalidateLayout();
}

void KoanSpaceHeaderView::Clear() {
  has_space_ = false;
  CancelRename();
  InvalidateLayout();
}

std::u16string KoanSpaceHeaderView::GetName() const {
  return std::u16string(name_->GetText());
}

std::u16string KoanSpaceHeaderView::GetIcon() const {
  return std::u16string(icon_->GetText());
}

views::View* KoanSpaceHeaderView::icon_view() {
  return icon_->GetVisible() ? static_cast<views::View*>(icon_) : this;
}

void KoanSpaceHeaderView::StartRename() {
  if (!has_space_ || IsRenaming()) {
    return;
  }
  renaming_ = controller_->GetActiveSpace();
  // From the keyboard (F2, or the menu opened with Enter).
  refocus_after_rename_ = HasFocus();
  rename_field_->SetText(name_->GetText());
  rename_field_->SelectAll(/*reversed=*/false);
  rename_field_->SetVisible(true);
  name_->SetVisible(false);
  UpdateActionsVisibility();
  InvalidateLayout();
  rename_field_->RequestFocus();
}

void KoanSpaceHeaderView::CommitRename() {
  FinishRename(/*keep=*/true);
}

void KoanSpaceHeaderView::CancelRename() {
  FinishRename(/*keep=*/false);
}

bool KoanSpaceHeaderView::IsRenaming() const {
  return rename_field_->GetVisible();
}

void KoanSpaceHeaderView::FinishRename(bool keep) {
  if (!IsRenaming()) {
    return;
  }
  // Hide the field first: losing focus calls back in here.
  const bool had_focus = rename_field_->HasFocus();
  rename_field_->SetVisible(false);
  name_->SetVisible(true);
  UpdateActionsVisibility();
  InvalidateLayout();
  const std::optional<tab_groups::TabGroupId> space =
      std::exchange(renaming_, std::nullopt);
  if (keep && space) {
    // Posted: a Space switch commits the rename from inside a tab strip
    // notification, where the model can't change.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanWorkspaceController::RenameSpace,
                                  controller_->GetWeakPtr(), *space,
                                  std::u16string(rename_field_->GetText())));
  }
  // Enter or Escape after a rename started from the keyboard: the keyboard
  // stays on the header.
  if (had_focus && refocus_after_rename_) {
    RequestFocus();
  }
}

bool KoanSpaceHeaderView::HandleKeyEvent(views::Textfield* sender,
                                         const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    FinishRename(/*keep=*/true);
    return true;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    FinishRename(/*keep=*/false);
    return true;
  }
  return false;
}

void KoanSpaceHeaderView::OnViewFocused(views::View* observed_view) {
  UpdateActionsVisibility();
}

void KoanSpaceHeaderView::OnViewBlurred(views::View* observed_view) {
  if (observed_view == rename_field_) {
    FinishRename(/*keep=*/true);
  }
  UpdateActionsVisibility();
}

void KoanSpaceHeaderView::Layout(PassKey) {
  const gfx::Rect bounds = GetLocalBounds();
  // The icon where the tabs have their favicon.
  int x = SidebarStripInset() + kNewTabIconX;
  // (In LTR terms; mirrored for right-to-left.)
  const auto place = [this](views::View* view, const gfx::Rect& rect) {
    view->SetBoundsRect(GetMirroredRect(rect));
  };
  if (icon_->GetVisible()) {
    // An emoji's glyph is wider than the 16px slot: centre it there.
    place(icon_, gfx::Rect(x - kSpaceIconSize / 4, 0, kSpaceIconSize * 3 / 2,
                           bounds.height()));
    x += kSpaceIconSize + kSpaceIconGap;
  }
  const int actions_x =
      bounds.width() - SidebarStripInset() - kSpaceActionsSize - 4;
  place(actions_,
        gfx::Rect(actions_x, (bounds.height() - kSpaceActionsSize) / 2,
                  kSpaceActionsSize, kSpaceActionsSize));
  const int text_width = std::max(0, actions_x - 4 - x);
  place(name_, gfx::Rect(x, 0, text_width, bounds.height()));
  const int field_height = rename_field_->GetPreferredSize().height();
  place(rename_field_, gfx::Rect(x - 4, (bounds.height() - field_height) / 2,
                                 text_width + 4, field_height));
}

bool KoanSpaceHeaderView::OnMousePressed(const ui::MouseEvent& event) {
  if (!event.IsOnlyLeftMouseButton() || event.GetClickCount() != 2) {
    return event.IsOnlyLeftMouseButton();
  }
  // Zen: double-click the icon to change it, the name to rename the Space.
  if (icon_->GetVisible() && icon_->bounds().Contains(event.location())) {
    if (std::optional<tab_groups::TabGroupId> space =
            controller_->GetActiveSpace()) {
      controller_->ShowIconPicker(*space, icon_);
    }
  } else {
    StartRename();
  }
  return true;
}

bool KoanSpaceHeaderView::OnKeyPressed(const ui::KeyEvent& event) {
  switch (event.key_code()) {
    case ui::VKEY_RETURN:
    case ui::VKEY_SPACE: {
      // Where the "..." button opens it.
      gfx::Point point = actions_->bounds().bottom_left();
      views::View::ConvertPointToScreen(this, &point);
      ShowMenu(point);
      return true;
    }
    case ui::VKEY_F2:
      StartRename();
      return true;
    default:
      return false;
  }
}

void KoanSpaceHeaderView::OnFocus() {
  views::View::OnFocus();
  UpdateActionsVisibility();
  SchedulePaint();
}

void KoanSpaceHeaderView::OnBlur() {
  views::View::OnBlur();
  UpdateActionsVisibility();
  SchedulePaint();
}

void KoanSpaceHeaderView::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  UpdateActionsVisibility();
  SchedulePaint();
}

void KoanSpaceHeaderView::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  UpdateActionsVisibility();
  SchedulePaint();
}

bool KoanSpaceHeaderView::IsHighlighted() const {
  return hovered_ || HasFocus() || actions_->HasFocus();
}

void KoanSpaceHeaderView::UpdateActionsVisibility() {
  actions_->SetVisible(!IsRenaming());
  actions_->layer()->SetOpacity(IsHighlighted() ? 1 : 0);
}

void KoanSpaceHeaderView::OnPaintBackground(gfx::Canvas* canvas) {
  if (!IsHighlighted()) {
    return;
  }
  gfx::RectF bounds(GetLocalBounds());
  bounds.Inset(
      gfx::InsetsF::VH(kSpaceHeaderBackgroundInset, SidebarStripInset()));
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(GetColorProvider()->GetColor(kColorKoanSpaceHeaderHovered));
  canvas->DrawRoundRect(bounds, SidebarRowCornerRadius(), flags);
}

void KoanSpaceHeaderView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  ShowMenu(point);
}

void KoanSpaceHeaderView::ShowMenu(const gfx::Point& screen_point) {
  controller_->ShowSpaceMenu(std::nullopt, this, screen_point);
}

int PlaceSpaceHeader(const views::View* tab_strip_view,
                     bool collapsed,
                     int width,
                     int y,
                     int available_height,
                     views::ProposedLayout& layouts) {
  KoanSpaceHeaderView* header = nullptr;
  KoanSpaceCreationView* form = nullptr;
  for (views::View* child : tab_strip_view->children()) {
    if (auto* header_view = views::AsViewClass<KoanSpaceHeaderView>(child)) {
      header = header_view;
    } else if (auto* form_view =
                   views::AsViewClass<KoanSpaceCreationView>(child)) {
      form = form_view;
    }
  }
  // Expanded on hover (or kept expanded for the form), a collapsed sidebar
  // is wider than its collapsed width: lay it out as expanded, like
  // upstream's TabView does.
  collapsed = collapsed && width <= tabs::kVerticalTabStripCollapsedWidth;
  int height = 0;
  if (form) {
    // Zen's Create-a-Space form takes the Space's place (the caller hides
    // the tabs) and fills the sidebar.
    if (!collapsed) {
      height = std::max(form->GetHeightForWidth(width), available_height);
    }
    layouts.child_layouts.emplace_back(form, !collapsed,
                                       gfx::Rect(0, y, width, height));
  }
  if (header) {
    // A collapsed sidebar has no room for the name (Zen's collapsed sidebar
    // shows the icon alone; Koan leaves the header out).
    const bool visible = header->has_space() && !collapsed && !form;
    const int header_height = visible ? kSpaceHeaderHeight : 0;
    layouts.child_layouts.emplace_back(
        header, visible, gfx::Rect(0, y + height, width, header_height));
    height += header_height;
  }
  return height;
}

BEGIN_METADATA(KoanSpaceHeaderView)
ADD_READONLY_PROPERTY_METADATA(std::u16string, Name)
ADD_READONLY_PROPERTY_METADATA(std::u16string, Icon)
END_METADATA

}  // namespace koan
