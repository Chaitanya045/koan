// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenLibrarySpacesSection.mjs
//   zen-library.css (card layout)
//   zen-library.ftl (strings)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_change_type.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "koan/browser/koan_library_logic.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_library.h"
#include "koan/browser/ui/views/koan_library_sections.h"
#include "koan/browser/ui/views/koan_library_view.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_utils.h"

namespace koan {

namespace {

using library::CreateStateBackground;
using library::Font;
using library::LibraryLabel;

// Zen's .zen-library-spaces: 242px cards 24px apart, padding 45px 25px.
constexpr int kCardWidth = 242;
constexpr int kCardGap = 24;
constexpr int kPaddingX = 25;
constexpr int kPaddingY = 45;
// The add button after the last card.
constexpr int kPlusSize = 30;
constexpr float kPlusArm = 5.f;
constexpr float kPlusStroke = 1.5f;
// Tabs and titles change often while pages load; redraw at most this often.
constexpr base::TimeDelta kRebuildDelay = base::Milliseconds(150);
// Cards slide aside while one is dragged (translate 0.2s ease).
constexpr base::TimeDelta kShiftDuration = base::Milliseconds(200);

// The colour a card is tinted with for a Space theme's colour.
ui::ColorId TintColorId(tab_groups::TabGroupColorId color) {
  switch (color) {
    case tab_groups::TabGroupColorId::kGrey:
      return kColorKoanLibrarySpaceTintGrey;
    case tab_groups::TabGroupColorId::kBlue:
      return kColorKoanLibrarySpaceTintBlue;
    case tab_groups::TabGroupColorId::kRed:
      return kColorKoanLibrarySpaceTintRed;
    case tab_groups::TabGroupColorId::kYellow:
      return kColorKoanLibrarySpaceTintYellow;
    case tab_groups::TabGroupColorId::kGreen:
      return kColorKoanLibrarySpaceTintGreen;
    case tab_groups::TabGroupColorId::kPink:
      return kColorKoanLibrarySpaceTintPink;
    case tab_groups::TabGroupColorId::kPurple:
      return kColorKoanLibrarySpaceTintPurple;
    case tab_groups::TabGroupColorId::kCyan:
      return kColorKoanLibrarySpaceTintCyan;
    case tab_groups::TabGroupColorId::kOrange:
    case tab_groups::TabGroupColorId::kNumEntries:
      return kColorKoanLibrarySpaceTintOrange;
  }
}

const TabGroup* GroupOf(TabStripModel* model,
                        const tab_groups::TabGroupId& id) {
  return model->group_model() && model->group_model()->ContainsTabGroup(id)
             ? model->group_model()->GetTabGroup(id)
             : nullptr;
}

// A tab of a space, as its card lists it.
class SpaceTab : public views::Button {
  METADATA_HEADER(SpaceTab, views::Button)

 public:
  SpaceTab(PressedCallback callback,
           const ui::ImageModel& favicon,
           std::u16string title,
           bool active)
      : views::Button(std::move(callback)), active_(active) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 10),
        10));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* icon = AddChildView(std::make_unique<views::ImageView>(favicon));
    icon->SetImageSize(gfx::Size(gfx::kFaviconSize, gfx::kFaviconSize));
    auto* label = AddChildView(
        std::make_unique<LibraryLabel>(title, 13, gfx::Font::Weight::NORMAL));
    layout->SetFlexForView(label, 1);
    SetPreferredSize(gfx::Size(0, SidebarRowHeight()));
    SetTooltipText(title);
    GetViewAccessibility().SetName(title);
  }

  // views::Button:
  void OnPaintBackground(gfx::Canvas* canvas) override {
    const ui::ColorProvider* provider = GetColorProvider();
    // Like the sidebar's tab rows.
    std::optional<ui::ColorId> color;
    if (active_) {
      color = kColorKoanSidebarTabSelected;
    } else if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      color = kColorKoanSidebarTabHovered;
    }
    if (color) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(provider->GetColor(*color));
      canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()),
                            SidebarRowCornerRadius(), flags);
    }
  }

 private:
  const bool active_;
};

BEGIN_METADATA(SpaceTab)
END_METADATA

// The name of a space being renamed: commits on Enter or when it loses
// focus, gives up on Esc.
class RenameField : public views::Textfield, public views::TextfieldController {
  METADATA_HEADER(RenameField, views::Textfield)

 public:
  RenameField(std::u16string name,
              base::OnceCallback<void(std::optional<std::u16string>)> done)
      : done_(std::move(done)) {
    SetText(name);
    SetFontList(Font(13, gfx::Font::Weight::MEDIUM));
    SetTextColorId(kColorKoanLibraryForeground);
    SetBackgroundEnabled(false);
    SetBorder(views::NullBorder());
    GetViewAccessibility().SetName(u"Space name");
    set_controller(this);
  }
  ~RenameField() override { set_controller(nullptr); }

  void Finish(bool commit) {
    if (!done_) {
      return;
    }
    std::u16string name;
    base::TrimWhitespace(GetText(), base::TRIM_ALL, &name);
    // The callback rebuilds the card, which destroys this field; not from
    // inside its own event handling.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(done_),
                       commit && !name.empty()
                           ? std::optional<std::u16string>(std::move(name))
                           : std::nullopt));
  }

  // views::Textfield:
  void OnBlur() override {
    views::Textfield::OnBlur();
    Finish(/*commit=*/true);
  }

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& event) override {
    if (event.type() == ui::EventType::kKeyPressed &&
        event.key_code() == ui::VKEY_RETURN) {
      Finish(/*commit=*/true);
      return true;
    }
    return false;
  }

 private:
  base::OnceCallback<void(std::optional<std::u16string>)> done_;
};

BEGIN_METADATA(RenameField)
END_METADATA

// A text button in a given font (the icon, name and emoji buttons).
class SpaceTextButton : public views::LabelButton {
  METADATA_HEADER(SpaceTextButton, views::LabelButton)

 public:
  SpaceTextButton(PressedCallback callback,
                  std::u16string text,
                  const gfx::FontList& font)
      : views::LabelButton(std::move(callback), std::move(text)) {
    label()->SetFontList(font);
    // On the library's translucent layers.
    SetTextSubpixelRenderingEnabled(false);
  }
};

BEGIN_METADATA(SpaceTextButton)
END_METADATA

// Zen's add button after the last card: a "+" on an accent circle.
class PlusButton : public views::Button {
  METADATA_HEADER(PlusButton, views::Button)

 public:
  explicit PlusButton(PressedCallback callback)
      : views::Button(std::move(callback)) {
    SetPreferredSize(gfx::Size(kPlusSize, kPlusSize));
    SetTooltipText(u"Create Space");
    GetViewAccessibility().SetName(u"Create Space");
  }

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* provider = GetColorProvider();
    const bool hovered =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(provider->GetColor(hovered ? kColorKoanLibraryAddSpaceHovered
                                              : kColorKoanLibraryAddSpace));
    const gfx::PointF center = gfx::RectF(GetLocalBounds()).CenterPoint();
    canvas->DrawCircle(center, kPlusSize / 2.f, flags);
    flags.setColor(provider->GetColor(kColorKoanLibraryAddSpaceForeground));
    flags.setStrokeWidth(kPlusStroke);
    flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    canvas->DrawLine(gfx::PointF(center.x() - kPlusArm, center.y()),
                     gfx::PointF(center.x() + kPlusArm, center.y()), flags);
    canvas->DrawLine(gfx::PointF(center.x(), center.y() - kPlusArm),
                     gfx::PointF(center.x(), center.y() + kPlusArm), flags);
  }
};

BEGIN_METADATA(PlusButton)
END_METADATA

class KoanLibrarySpacesSection;

// Zen's "drag to reorder" handle at the foot of a card.
class MoveHandle : public views::ImageButton {
  METADATA_HEADER(MoveHandle, views::ImageButton)

 public:
  MoveHandle(KoanLibrarySpacesSection* section, int index);

  // views::ImageButton:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;

 private:
  const raw_ptr<KoanLibrarySpacesSection> section_;
  const int index_;
};

BEGIN_METADATA(MoveHandle)
END_METADATA

// One space: its icon and name (both editable), its theme, its tabs, and the
// handle and menu at its foot (Zen's .zen-library-space).
class SpaceCard : public views::View {
  METADATA_HEADER(SpaceCard, views::View)

 public:
  // `tint`: the Space theme's colour, none for the default theme.
  explicit SpaceCard(std::optional<ui::ColorId> tint) : tint_(tint) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }

  std::u16string GetName() const { return name_; }
  void set_name(std::u16string name) { name_ = std::move(name); }

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override {
    const ui::ColorProvider* provider = GetColorProvider();
    // The card tinted with the space's colour, standing in for Zen's
    // per-space gradient (the theme is Koan's tint colour until #41).
    SkColor color = provider->GetColor(kColorKoanLibrarySpaceCard);
    if (tint_) {
      color = color_utils::AlphaBlend(provider->GetColor(*tint_), color,
                                      library::kSpaceCardTint);
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color);
    const gfx::RectF bounds(GetLocalBounds());
    canvas->DrawRoundRect(bounds, library::kSpaceCardRadius, flags);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(provider->GetColor(kColorKoanLibrarySpaceCardBorder));
    gfx::RectF outline = bounds;
    outline.Inset(0.5f);
    canvas->DrawRoundRect(outline, library::kSpaceCardRadius, flags);
  }

 private:
  const std::optional<ui::ColorId> tint_;
  std::u16string name_;
};

BEGIN_METADATA(SpaceCard)
ADD_READONLY_PROPERTY_METADATA(std::u16string, Name)
END_METADATA

// The cards side by side; scrolls sideways when they don't fit.
class SpacesStrip : public views::View {
  METADATA_HEADER(SpacesStrip, views::View)

 public:
  SpacesStrip() {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetMasksToBounds(true);
  }

  int GetContentWidth() const {
    int width = 2 * kPaddingX;
    for (const views::View* child : children()) {
      width += child->GetPreferredSize().width() + kCardGap;
    }
    return width - (children().empty() ? 0 : kCardGap);
  }

  // views::View:
  void Layout(PassKey) override {
    offset_ = std::clamp(offset_, 0, std::max(0, GetContentWidth() - width()));
    int x = kPaddingX - offset_;
    const int height = std::max(0, this->height() - 2 * kPaddingY);
    for (views::View* child : children()) {
      const int width = child->GetPreferredSize().width();
      if (views::IsViewClass<SpaceCard>(child)) {
        child->SetBounds(x, kPaddingY, width, height);
      } else {
        const int child_height = child->GetPreferredSize().height();
        child->SetBounds(x, kPaddingY + (height - child_height) / 2, width,
                         child_height);
      }
      x += width + kCardGap;
    }
  }

  bool OnMouseWheel(const ui::MouseWheelEvent& event) override {
    const int delta = event.x_offset() ? event.x_offset() : event.y_offset();
    offset_ -= delta;
    InvalidateLayout();
    return true;
  }

 private:
  int offset_ = 0;
};

BEGIN_METADATA(SpacesStrip)
END_METADATA

// Zen's Spaces section (ZenLibrarySpacesSection.mjs): every space as a card
// to rename, give an icon and a theme, reorder by dragging, open a tab of,
// or delete; and a button to add one. The library grows to fit the cards.
// What a card changes goes through the Spaces' API (KoanWorkspaceController);
// the tab strip is only read, for the tabs a card lists.
class KoanLibrarySpacesSection : public KoanLibrarySection,
                                 public TabStripModelObserver {
  METADATA_HEADER(KoanLibrarySpacesSection, KoanLibrarySection)

 public:
  KoanLibrarySpacesSection(KoanLibraryController* library,
                           KoanWorkspaceController* workspaces)
      : KoanLibrarySection(library, LibrarySection::kSpaces),
        workspaces_(workspaces) {
    SetLayoutManager(std::make_unique<views::FillLayout>());
    strip_ = AddChildView(std::make_unique<SpacesStrip>());
    spaces_changed_ = workspaces_->AddSpacesChangedCallback(base::BindRepeating(
        &KoanLibrarySpacesSection::ScheduleRebuild, base::Unretained(this)));
    model()->AddObserver(this);
  }
  ~KoanLibrarySpacesSection() override { model()->RemoveObserver(this); }

  int GetCardCount() const { return static_cast<int>(cards_.size()); }

  // KoanLibrarySection:
  int GetContentWidth() const override {
    return std::max(library::kSectionWidth, strip_->GetContentWidth());
  }

  void OnShown() override {
    shown_ = true;
    Rebuild();
  }

  bool HandleEscape() override {
    if (!renaming_) {
      return false;
    }
    if (RenameField* field = rename_field_) {
      // Give up on the new name.
      rename_field_ = nullptr;
      field->Finish(/*commit=*/false);
    }
    return true;
  }

  // Dragging a card by its handle (Zen's pointer handlers).
  void StartDrag(int index, const gfx::Point& point_in_strip) {
    if (index < 0 || index >= static_cast<int>(cards_.size())) {
      return;
    }
    drag_index_ = index;
    drop_index_ = index;
    drag_start_x_ = point_in_strip.x();
    gfx::Transform lift;
    lift.Scale(1.03f, 1.03f);
    cards_[index].card->layer()->SetTransform(lift);
    // Over the cards it passes.
    strip_->layer()->StackAtTop(cards_[index].card->layer());
  }

  void Drag(const gfx::Point& point_in_strip) {
    if (drag_index_ < 0) {
      return;
    }
    const int travel = point_in_strip.x() - drag_start_x_;
    SpaceCard* dragged = cards_[drag_index_].card;
    gfx::Transform transform;
    transform.Translate(travel, 0);
    transform.Scale(1.03f, 1.03f);
    dragged->layer()->SetTransform(transform);
    // The slot the card's centre is over.
    const int center = dragged->bounds().CenterPoint().x() + travel;
    std::vector<int> centers;
    for (const Card& card : cards_) {
      centers.push_back(card.card->bounds().CenterPoint().x());
    }
    const int drop = library::DropSlot(centers, drag_index_, center);
    if (drop == drop_index_) {
      return;
    }
    drop_index_ = drop;
    // The cards in between step aside.
    for (int i = 0; i < static_cast<int>(cards_.size()); ++i) {
      if (i == drag_index_) {
        continue;
      }
      const int shift = library::CardShift(i, drag_index_, drop_index_,
                                           kCardWidth + kCardGap);
      ui::ScopedLayerAnimationSettings settings(
          cards_[i].card->layer()->GetAnimator());
      settings.SetTransitionDuration(kShiftDuration);
      gfx::Transform step;
      step.Translate(shift, 0);
      cards_[i].card->layer()->SetTransform(step);
    }
  }

  void EndDrag(bool commit) {
    if (drag_index_ < 0) {
      return;
    }
    const int from = std::exchange(drag_index_, -1);
    const int to = std::exchange(drop_index_, -1);
    for (const Card& card : cards_) {
      card.card->layer()->SetTransform(gfx::Transform());
    }
    if (commit && from != to) {
      workspaces_->MoveSpace(cards_[from].id, to);
    }
    ScheduleRebuild();
  }

  views::View* strip() { return strip_; }

  // TabStripModelObserver (the tabs the cards list; the Spaces themselves
  // come through AddSpacesChangedCallback()):
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override {
    ScheduleRebuild();
  }
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override {
    ScheduleRebuild();
  }

 private:
  // A card, and the buttons that menus and pickers open from.
  struct Card {
    tab_groups::TabGroupId id;
    raw_ptr<SpaceCard> card;
    raw_ptr<views::View> icon;
    raw_ptr<views::View> theme;
  };

  TabStripModel* model() const { return workspaces_->tab_strip_model(); }

  const Card* FindCard(const tab_groups::TabGroupId& id) const {
    auto it = std::ranges::find(cards_, id, &Card::id);
    return it != cards_.end() ? &*it : nullptr;
  }

  void ScheduleRebuild() {
    // A hidden section is rebuilt when it is shown (OnShown()).
    if (!shown_ || !library()->is_open() || !GetVisible() ||
        rebuild_timer_.IsRunning()) {
      return;
    }
    rebuild_timer_.Start(FROM_HERE, kRebuildDelay,
                         base::BindOnce(&KoanLibrarySpacesSection::Rebuild,
                                        base::Unretained(this)));
  }

  void Rebuild() {
    // Not while the user is in the middle of something on a card.
    if (drag_index_ >= 0 || renaming_) {
      return;
    }
    rebuild_timer_.Stop();
    rename_field_ = nullptr;
    cards_.clear();
    strip_->RemoveAllChildViews();
    const std::vector<KoanWorkspaceController::SpaceInfo> spaces =
        workspaces_->GetSpaces();
    for (size_t i = 0; i < spaces.size(); ++i) {
      AddCard(spaces[i], static_cast<int>(i));
    }
    strip_->AddChildView(std::make_unique<PlusButton>(base::BindRepeating(
        &KoanLibrarySpacesSection::CreateSpace, base::Unretained(this))));
    strip_->InvalidateLayout();
    library()->OnPreferredWidthChanged();
  }

  void AddCard(const KoanWorkspaceController::SpaceInfo& space, int index) {
    const TabGroup* group = GroupOf(model(), space.id);
    if (!group) {
      return;
    }
    const tab_groups::TabGroupId id = space.id;
    std::u16string name = space.name;
    if (name.empty()) {
      name = u"Space " + base::NumberToString16(index + 1);
    }

    auto card_view = std::make_unique<SpaceCard>(
        space.theme.color
            ? std::optional<ui::ColorId>(TintColorId(*space.theme.color))
            : std::nullopt);
    SpaceCard* card = card_view.get();
    card->set_name(name);
    card->SetPreferredSize(gfx::Size(kCardWidth, 0));
    auto* layout = card->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    // Header: icon, name, theme (margin 18px 14px 6px, 13px at 85%).
    auto* header = card->AddChildView(std::make_unique<views::BoxLayoutView>());
    header->SetInsideBorderInsets(gfx::Insets::TLBR(18, 14, 6, 14));
    header->SetBetweenChildSpacing(8);
    header->SetCrossAxisAlignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    header->SetMinimumCrossAxisSize(28);

    const bool has_icon = !space.icon.empty();
    auto icon_button = std::make_unique<SpaceTextButton>(
        base::BindRepeating(&KoanLibrarySpacesSection::ShowIconPicker,
                            base::Unretained(this), id),
        space.icon, Font(15));
    icon_button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    icon_button->SetEnabledTextColors(kColorKoanLibraryForeground);
    icon_button->SetBorder(views::NullBorder());
    icon_button->SetMinSize(gfx::Size(20, 20));
    icon_button->SetMaxSize(gfx::Size(24, 24));
    icon_button->SetTooltipText(u"Change icon");
    icon_button->GetViewAccessibility().SetName(u"Change icon");
    // Without an icon, a dashed outline marks where one can go.
    icon_button->SetBackground(
        has_icon
            ? CreateStateBackground(std::nullopt, kColorKoanLibraryPillHovered,
                                    library::kSmallRadius)
            : CreateStateBackground(kColorKoanLibrarySpaceIconEmpty,
                                    kColorKoanLibraryPillHovered,
                                    library::kSmallRadius));
    views::View* icon = header->AddChildView(std::move(icon_button));

    if (renaming_space_ == id) {
      auto field = std::make_unique<RenameField>(
          name, base::BindOnce(&KoanLibrarySpacesSection::OnRenamed,
                               weak_factory_.GetWeakPtr(), id));
      rename_field_ = header->AddChildView(std::move(field));
      header->SetFlexForView(rename_field_, 1);
    } else {
      auto name_button = std::make_unique<SpaceTextButton>(
          base::BindRepeating(&KoanLibrarySpacesSection::StartRename,
                              base::Unretained(this), id),
          name, Font(13, gfx::Font::Weight::MEDIUM));
      name_button->SetEnabledTextColors(kColorKoanLibraryForeground);
      name_button->SetBorder(views::NullBorder());
      name_button->SetTooltipText(u"Rename space");
      name_button->SetElideBehavior(gfx::ELIDE_TAIL);
      views::LabelButton* added = header->AddChildView(std::move(name_button));
      header->SetFlexForView(added, 1);
    }

    views::View* theme = header->AddChildView(library::CreateIconButton(
        base::BindRepeating(&KoanLibrarySpacesSection::EditTheme,
                            base::Unretained(this), id),
        kBrushFilledIcon, u"Change theme"));

    // Body: the space's tabs.
    auto* scroll = card->AddChildView(std::make_unique<views::ScrollView>(
        views::ScrollView::ScrollWithLayers::kEnabled));
    scroll->SetBackgroundColor(std::nullopt);
    scroll->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    scroll->SetDrawOverflowIndicator(false);
    auto* tabs = scroll->SetContents(std::make_unique<views::BoxLayoutView>());
    tabs->SetOrientation(views::BoxLayout::Orientation::kVertical);
    tabs->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
    tabs->SetInsideBorderInsets(gfx::Insets::VH(4, 8));
    tabs->SetBetweenChildSpacing(2);
    layout->SetFlexForView(scroll, 1);
    const gfx::Range range = group->ListTabs();
    const int active_index = model()->active_index();
    for (size_t i = range.start(); i < range.end(); ++i) {
      tabs::TabInterface* tab = model()->GetTabAtIndex(static_cast<int>(i));
      TabUIHelper* helper = TabUIHelper::From(tab);
      ui::ImageModel favicon;
      if (helper && helper->ShouldDisplayFavicon()) {
        favicon = helper->GetFavicon();
      }
      if (favicon.IsEmpty()) {
        favicon = ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon,
                                                 kColorKoanLibraryForeground,
                                                 gfx::kFaviconSize);
      }
      tabs->AddChildView(std::make_unique<SpaceTab>(
          base::BindRepeating(&KoanLibrarySpacesSection::OpenTab,
                              base::Unretained(this), id, tab->GetHandle()),
          favicon, helper ? helper->GetTitle() : std::u16string(),
          static_cast<int>(i) == active_index));
    }

    // Footer: the drag handle and the menu (padding 8px 14px 14px, 70%).
    auto* footer = card->AddChildView(std::make_unique<views::BoxLayoutView>());
    footer->SetInsideBorderInsets(gfx::Insets::TLBR(8, 14, 14, 14));
    footer->SetCrossAxisAlignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    footer->AddChildView(std::make_unique<MoveHandle>(this, index));
    auto* spacer = footer->AddChildView(std::make_unique<views::View>());
    footer->SetFlexForView(spacer, 1);
    auto more =
        library::CreateIconButton(views::Button::PressedCallback(),
                                  views::kMoreHorizIcon, u"More options");
    views::Button* more_button = more.get();
    // (The button owns this callback, so it never outlives the button.)
    more->SetCallback(base::BindRepeating(
        &KoanLibrarySpacesSection::ShowSpaceMenu, base::Unretained(this), id,
        base::Unretained(more_button)));
    footer->AddChildView(std::move(more));

    cards_.push_back(
        {id, strip_->AddChildView(std::move(card_view)), icon, theme});
  }

  void StartRename(const tab_groups::TabGroupId& id) {
    // The rebuild destroys the name button that may have asked for this.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanLibrarySpacesSection::DoStartRename,
                                  weak_factory_.GetWeakPtr(), id));
  }

  void DoStartRename(const tab_groups::TabGroupId& id) {
    if (renaming_) {
      return;
    }
    renaming_space_ = id;
    Rebuild();
    // No field if the Space has gone meanwhile: then nothing is being
    // renamed (Rebuild() and Esc would otherwise wait for it for good).
    if (!rename_field_) {
      renaming_space_.reset();
      return;
    }
    renaming_ = true;
    rename_field_->SelectAll(false);
    rename_field_->RequestFocus();
  }

  void OnRenamed(const tab_groups::TabGroupId& id,
                 std::optional<std::u16string> name) {
    renaming_ = false;
    renaming_space_.reset();
    rename_field_ = nullptr;
    if (name && workspaces_->GetSpace(id)) {
      workspaces_->RenameSpace(id, *name);
    }
    Rebuild();
  }

  // Zen's emoji picker (the Spaces' own), at the card's icon.
  void ShowIconPicker(const tab_groups::TabGroupId& id) {
    if (const Card* card = FindCard(id)) {
      workspaces_->ShowIconPicker(id, card->icon);
    }
  }

  // Edit Theme... (the Spaces' own theme menu), at the card's brush.
  void EditTheme(const tab_groups::TabGroupId& id) {
    if (const Card* card = FindCard(id)) {
      workspaces_->EditSpaceTheme(id, card->theme);
    }
  }

  // Zen's space menu (zenWorkspaceMoreActions), for what Koan's spaces do;
  // the name is changed on the card itself. Its items look the card up
  // when chosen, in case the cards were rebuilt meanwhile.
  void ShowSpaceMenu(const tab_groups::TabGroupId& id, views::Button* source) {
    auto menu = std::make_unique<KoanLibraryMenu>();
    const auto bind = [&](void (KoanLibrarySpacesSection::*action)(
                          const tab_groups::TabGroupId&)) {
      return base::BindRepeating(action, weak_factory_.GetWeakPtr(), id);
    };
    menu->AddItem(u"Change Icon…",
                  bind(&KoanLibrarySpacesSection::ShowIconPicker));
    menu->AddItem(u"Edit Theme…", bind(&KoanLibrarySpacesSection::EditTheme));
    menu->AddSeparator();
    menu->AddItem(u"Unload Space", bind(&KoanLibrarySpacesSection::Unload));
    menu->AddItem(u"Unload All Other Spaces",
                  bind(&KoanLibrarySpacesSection::UnloadOthers));
    menu->AddSeparator();
    menu->AddItem(u"Create Space",
                  base::BindRepeating(&KoanLibrarySpacesSection::CreateSpace,
                                      weak_factory_.GetWeakPtr()));
    // Zen keeps at least one Space.
    menu->AddItem(u"Delete Space", bind(&KoanLibrarySpacesSection::Delete),
                  workspaces_->CanDeleteSpace());
    RunMenu(std::move(menu), source, source->GetBoundsInScreen(),
            views::MenuAnchorPosition::kTopRight,
            ui::mojom::MenuSourceType::kMouse);
  }

  void Unload(const tab_groups::TabGroupId& id) {
    workspaces_->UnloadSpace(id);
  }
  void UnloadOthers(const tab_groups::TabGroupId& id) {
    workspaces_->UnloadAllOtherSpaces(id);
  }
  // Asks first (the Spaces' own confirmation), and never the last Space.
  void Delete(const tab_groups::TabGroupId& id) {
    if (workspaces_->CanDeleteSpace()) {
      workspaces_->DeleteSpace(id);
    }
  }

  // Zen's Create-a-Space form, in the sidebar the Library covers: closes the
  // Library first.
  void CreateSpace() {
    library()->Close();
    workspaces_->ShowCreateForm();
  }

  // Opens a tab of a space: switches to the space, selects the tab and
  // closes the library.
  void OpenTab(const tab_groups::TabGroupId& id, tabs::TabHandle handle) {
    tabs::TabInterface* tab = handle.Get();
    if (!tab) {
      return;
    }
    workspaces_->SwitchTo(id);
    const int index = model()->GetIndexOfTab(tab);
    if (index != TabStripModel::kNoTab) {
      model()->ActivateTabAt(index);
    }
    library()->Close();
  }

  const raw_ptr<KoanWorkspaceController> workspaces_;
  base::CallbackListSubscription spaces_changed_;
  raw_ptr<SpacesStrip> strip_ = nullptr;
  std::vector<Card> cards_;
  raw_ptr<RenameField> rename_field_ = nullptr;
  std::optional<tab_groups::TabGroupId> renaming_space_;
  bool renaming_ = false;
  bool shown_ = false;
  int drag_index_ = -1;
  int drop_index_ = -1;
  int drag_start_x_ = 0;
  base::OneShotTimer rebuild_timer_;
  base::WeakPtrFactory<KoanLibrarySpacesSection> weak_factory_{this};
};

BEGIN_METADATA(KoanLibrarySpacesSection)
ADD_READONLY_PROPERTY_METADATA(int, CardCount)
END_METADATA

MoveHandle::MoveHandle(KoanLibrarySpacesSection* section, int index)
    : views::ImageButton(views::Button::PressedCallback()),
      section_(section),
      index_(index) {
  SetImageModel(STATE_NORMAL, ui::ImageModel::FromVectorIcon(
                                  kMoveIcon, kColorKoanLibraryForeground,
                                  library::kSmallButtonIconSize));
  SetImageHorizontalAlignment(ALIGN_CENTER);
  SetImageVerticalAlignment(ALIGN_MIDDLE);
  constexpr int kSize =
      library::kSmallButtonIconSize + 2 * library::kSmallButtonPadding;
  SetPreferredSize(gfx::Size(kSize, kSize));
  SetBackground(CreateStateBackground(
      std::nullopt, kColorKoanLibraryPillHovered, library::kSmallRadius));
  SetTooltipText(u"Drag to reorder");
  GetViewAccessibility().SetName(u"Drag to reorder");
  // A mouse handle: out of the tab order, since a key on it does nothing.
  SetFocusBehavior(FocusBehavior::NEVER);
}

bool MoveHandle::OnMousePressed(const ui::MouseEvent& event) {
  if (!event.IsOnlyLeftMouseButton()) {
    return false;
  }
  section_->StartDrag(index_, views::View::ConvertPointToTarget(
                                  this, section_->strip(), event.location()));
  return true;
}

bool MoveHandle::OnMouseDragged(const ui::MouseEvent& event) {
  section_->Drag(views::View::ConvertPointToTarget(this, section_->strip(),
                                                   event.location()));
  return true;
}

void MoveHandle::OnMouseReleased(const ui::MouseEvent& event) {
  section_->Drag(views::View::ConvertPointToTarget(this, section_->strip(),
                                                   event.location()));
  section_->EndDrag(/*commit=*/true);
}

void MoveHandle::OnMouseCaptureLost() {
  section_->EndDrag(/*commit=*/false);
}

}  // namespace

std::unique_ptr<KoanLibrarySection> CreateLibrarySpacesSection(
    KoanLibraryController* library) {
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(library->browser_view());
  KoanWorkspaceController* workspaces =
      controller ? controller->workspaces() : nullptr;
  // Like Zen, no Spaces where there are no spaces.
  if (!workspaces || !workspaces->IsEnabled()) {
    return nullptr;
  }
  return std::make_unique<KoanLibrarySpacesSection>(library, workspaces);
}

}  // namespace koan
