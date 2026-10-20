// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
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
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_library.h"
#include "koan/browser/ui/views/koan_library_icons.h"
#include "koan/browser/ui/views/koan_library_sections.h"
#include "koan/browser/ui/views/koan_library_view.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
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
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
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
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

using library::CreateTintBackground;
using library::Font;
using library::TintLabel;

// Zen's .zen-library-spaces: 242px cards 24px apart, padding 45px 25px.
constexpr int kCardWidth = 242;
constexpr int kCardGap = 24;
constexpr int kPaddingX = 25;
constexpr int kPaddingY = 45;
constexpr int kCardRadius = 14;
// The add button after the last card.
constexpr int kPlusSize = 30;
// Tabs and titles change often while pages load; redraw at most this often.
constexpr base::TimeDelta kRebuildDelay = base::Milliseconds(150);
// Cards slide aside while one is dragged (translate 0.2s ease).
constexpr base::TimeDelta kShiftDuration = base::Milliseconds(200);

// Emoji offered for a space's icon (Zen has a full emoji picker).
constexpr auto kIcons = std::to_array<const char16_t*>({
    u"\U0001F30A", u"\U0001F33F", u"\U0001F52E", u"\U0001F34A", u"\U0001F338",
    u"\U0001F4A7", u"\U0001F525", u"⭐",         u"\U0001F4BC", u"\U0001F3E0",
    u"\U0001F3AE", u"\U0001F3B5", u"\U0001F4DA", u"\U0001F3A8", u"✈",
    u"\U0001F6D2", u"\U0001F4A1", u"\U0001F9EA", u"\U0001F4F7", u"\U0001F3AC",
    u"\U0001F355", u"☕",         u"\U0001F3C0", u"⚽",         u"\U0001F319",
    u"☀",          u"\U0001F308", u"❄",          u"\U0001F340", u"\U0001F33B",
    u"\U0001F431", u"\U0001F436", u"\U0001F680", u"\U0001F4BB", u"\U0001F4DD",
    u"\U0001F4C5", u"\U0001F4B0", u"❤",          u"\U0001F3AF", u"\U0001F512",
});

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
        std::make_unique<TintLabel>(title, 13, gfx::Font::Weight::NORMAL));
    layout->SetFlexForView(label, 1);
    SetPreferredSize(gfx::Size(0, 36));
    SetTooltipText(title);
    GetViewAccessibility().SetName(title);
  }

  // views::Button:
  void OnPaintBackground(gfx::Canvas* canvas) override {
    const ui::ColorProvider* provider = GetColorProvider();
    std::optional<SkColor> color;
    if (active_) {
      color = provider->GetColor(kColorTabBackgroundActiveFrameActive);
    } else if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      color = library::TextColor(provider, 0.06f);
    }
    if (color) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(*color);
      canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), 8, flags);
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
    SetTextColorId(library::TextColorId());
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
    const SkColor surface = provider->GetColor(kColorToolbar);
    const bool hovered =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color_utils::AlphaBlend(library::SectionIconStroke(provider),
                                           surface, hovered ? 0.7f : 0.5f));
    const gfx::PointF center = gfx::RectF(GetLocalBounds()).CenterPoint();
    canvas->DrawCircle(center, kPlusSize / 2.f, flags);
    flags.setColor(SK_ColorWHITE);
    flags.setStrokeWidth(1.5f);
    flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    canvas->DrawLine(gfx::PointF(center.x() - 5, center.y()),
                     gfx::PointF(center.x() + 5, center.y()), flags);
    canvas->DrawLine(gfx::PointF(center.x(), center.y() - 5),
                     gfx::PointF(center.x(), center.y() + 5), flags);
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
  explicit SpaceCard(tab_groups::TabGroupColorId color) : color_(color) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }

  std::u16string GetName() const { return name_; }
  void set_name(std::u16string name) { name_ = std::move(name); }

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override {
    const ui::ColorProvider* provider = GetColorProvider();
    // The window's surface tinted with the space's colour, standing in for
    // Zen's per-space gradient.
    const SkColor tint =
        provider->GetColor(GetTabGroupTabStripColorId(color_, true));
    const SkColor base = provider->GetColor(kColorToolbar);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color_utils::AlphaBlend(tint, base, 0.22f));
    const gfx::RectF bounds(GetLocalBounds());
    canvas->DrawRoundRect(bounds, kCardRadius, flags);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(library::TextColor(provider, 0.08f));
    gfx::RectF outline = bounds;
    outline.Inset(0.5f);
    canvas->DrawRoundRect(outline, kCardRadius, flags);
  }

 private:
  const tab_groups::TabGroupColorId color_;
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
    cards_[index]->layer()->SetTransform(lift);
    // Over the cards it passes.
    strip_->layer()->StackAtTop(cards_[index]->layer());
  }

  void Drag(const gfx::Point& point_in_strip) {
    if (drag_index_ < 0) {
      return;
    }
    const int travel = point_in_strip.x() - drag_start_x_;
    SpaceCard* dragged = cards_[drag_index_];
    gfx::Transform transform;
    transform.Translate(travel, 0);
    transform.Scale(1.03f, 1.03f);
    dragged->layer()->SetTransform(transform);
    // The slot the card's centre is over.
    const int center = dragged->bounds().CenterPoint().x() + travel;
    int drop = 0;
    for (int i = 0; i < static_cast<int>(cards_.size()); ++i) {
      if (i != drag_index_ && center > cards_[i]->bounds().CenterPoint().x()) {
        ++drop;
      }
    }
    if (drop == drop_index_) {
      return;
    }
    drop_index_ = drop;
    // The cards in between step aside.
    for (int i = 0; i < static_cast<int>(cards_.size()); ++i) {
      if (i == drag_index_) {
        continue;
      }
      int shift = 0;
      if (drag_index_ < i && i <= drop_index_) {
        shift = -(kCardWidth + kCardGap);
      } else if (drop_index_ <= i && i < drag_index_) {
        shift = kCardWidth + kCardGap;
      }
      ui::ScopedLayerAnimationSettings settings(
          cards_[i]->layer()->GetAnimator());
      settings.SetTransitionDuration(kShiftDuration);
      gfx::Transform step;
      step.Translate(shift, 0);
      cards_[i]->layer()->SetTransform(step);
    }
  }

  void EndDrag(bool commit) {
    if (drag_index_ < 0) {
      return;
    }
    const int from = std::exchange(drag_index_, -1);
    const int to = std::exchange(drop_index_, -1);
    for (SpaceCard* card : cards_) {
      card->layer()->SetTransform(gfx::Transform());
    }
    if (commit && from != to) {
      MoveSpace(from, to);
    }
    ScheduleRebuild();
  }

  views::View* strip() { return strip_; }

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override {
    ScheduleRebuild();
  }
  void OnTabGroupChanged(const TabGroupChange& change) override {
    ScheduleRebuild();
  }
  void OnTabGroupFocusChanged(
      std::optional<tab_groups::TabGroupId> new_focused_group_id,
      std::optional<tab_groups::TabGroupId> old_focused_group_id) override {
    ScheduleRebuild();
  }
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override {
    ScheduleRebuild();
  }

 private:
  TabStripModel* model() const { return workspaces_->tab_strip_model(); }

  void ScheduleRebuild() {
    if (!shown_ || !library()->is_open() || rebuild_timer_.IsRunning()) {
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
    const std::vector<tab_groups::TabGroupId> spaces =
        workspaces_->GetWorkspaces();
    for (size_t i = 0; i < spaces.size(); ++i) {
      AddCard(spaces[i], static_cast<int>(i));
    }
    strip_->AddChildView(std::make_unique<PlusButton>(base::BindRepeating(
        &KoanLibrarySpacesSection::CreateSpace, base::Unretained(this))));
    strip_->InvalidateLayout();
    library()->OnPreferredWidthChanged();
  }

  void AddCard(const tab_groups::TabGroupId& id, int index) {
    const TabGroup* group = GroupOf(model(), id);
    if (!group) {
      return;
    }
    const tab_groups::TabGroupVisualData* visuals = group->visual_data();
    auto [icon, name] = SplitWorkspaceTitle(visuals->title());
    const bool has_icon = WorkspaceTitleHasIcon(visuals->title());
    if (name.empty()) {
      name = u"Space " + base::NumberToString16(index + 1);
    }

    auto card_view = std::make_unique<SpaceCard>(visuals->color());
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

    auto icon_button = std::make_unique<SpaceTextButton>(
        base::BindRepeating(&KoanLibrarySpacesSection::ShowIconPicker,
                            base::Unretained(this), id),
        has_icon ? icon : std::u16string(), Font(15));
    icon_button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    icon_button->SetEnabledTextColors(library::TextColorId());
    icon_button->SetBorder(views::NullBorder());
    icon_button->SetMinSize(gfx::Size(20, 20));
    icon_button->SetMaxSize(gfx::Size(24, 24));
    icon_button->SetTooltipText(u"Change icon");
    icon_button->GetViewAccessibility().SetName(u"Change icon");
    // Without an icon, a dashed outline marks where one can go.
    icon_button->SetBackground(has_icon
                                   ? CreateTintBackground(0.f, 6, 0.14f)
                                   : CreateTintBackground(0.08f, 6, 0.14f));
    header->AddChildView(std::move(icon_button));

    if (renaming_group_ == id) {
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
      name_button->SetEnabledTextColors(library::TextColorId());
      name_button->SetBorder(views::NullBorder());
      name_button->SetTooltipText(u"Rename space");
      name_button->SetElideBehavior(gfx::ELIDE_TAIL);
      views::LabelButton* added = header->AddChildView(std::move(name_button));
      header->SetFlexForView(added, 1);
    }

    auto theme =
        library::CreateIconButton(views::Button::PressedCallback(),
                                  kBrushFilledIcon, u"Change theme", 16, 6, 6);
    views::Button* theme_button = theme.get();
    theme->SetCallback(base::BindRepeating(
        &KoanLibrarySpacesSection::ShowThemeMenu, base::Unretained(this), id,
        base::Unretained(theme_button)));
    header->AddChildView(std::move(theme));

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
                                                 library::TextColorId(),
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
    auto more = library::CreateIconButton(views::Button::PressedCallback(),
                                          views::kMoreHorizIcon,
                                          u"More options", 16, 6, 6);
    views::Button* more_button = more.get();
    more->SetCallback(base::BindRepeating(
        &KoanLibrarySpacesSection::ShowSpaceMenu, base::Unretained(this), id,
        base::Unretained(more_button)));
    footer->AddChildView(std::move(more));

    cards_.push_back(strip_->AddChildView(std::move(card_view)));
  }

  // Moves the space at `from` to sit at `to` among the others.
  void MoveSpace(int from, int to) {
    const std::vector<tab_groups::TabGroupId> spaces =
        workspaces_->GetWorkspaces();
    if (from >= static_cast<int>(spaces.size())) {
      return;
    }
    const tab_groups::TabGroupId moved = spaces[from];
    const TabGroup* group = GroupOf(model(), moved);
    if (!group) {
      return;
    }
    const gfx::Range range = group->ListTabs();
    std::vector<tab_groups::TabGroupId> others = spaces;
    others.erase(others.begin() + from);
    // TabStripModel::MoveGroupTo takes where the group's first tab ends up,
    // counted without the group's own tabs.
    const auto without_group = [&](int index) {
      return index > static_cast<int>(range.start())
                 ? index - static_cast<int>(range.length())
                 : index;
    };
    int to_index;
    if (to < static_cast<int>(others.size())) {
      to_index =
          without_group(GroupOf(model(), others[to])->ListTabs().start());
    } else {
      to_index =
          without_group(GroupOf(model(), others.back())->ListTabs().end());
    }
    model()->MoveGroupTo(moved, to_index);
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
    renaming_group_ = id;
    Rebuild();
    renaming_ = true;
    if (rename_field_) {
      rename_field_->SelectAll(false);
      rename_field_->RequestFocus();
    }
  }

  void OnRenamed(const tab_groups::TabGroupId& id,
                 std::optional<std::u16string> name) {
    renaming_ = false;
    renaming_group_.reset();
    rename_field_ = nullptr;
    const TabGroup* group = GroupOf(model(), id);
    if (name && group) {
      const tab_groups::TabGroupVisualData* visuals = group->visual_data();
      const std::u16string title =
          WorkspaceTitleHasIcon(visuals->title())
              ? base::StrCat(
                    {SplitWorkspaceTitle(visuals->title()).first, u" ", *name})
              : *name;
      SetVisuals(id, title, visuals->color());
    }
    Rebuild();
  }

  void SetIcon(const tab_groups::TabGroupId& id, std::u16string icon) {
    const TabGroup* group = GroupOf(model(), id);
    if (!group) {
      return;
    }
    const tab_groups::TabGroupVisualData* visuals = group->visual_data();
    std::u16string name = SplitWorkspaceTitle(visuals->title()).second;
    if (!WorkspaceTitleHasIcon(visuals->title())) {
      name = visuals->title();
    }
    SetVisuals(id, icon.empty() ? name : base::StrCat({icon, u" ", name}),
               visuals->color());
  }

  void SetColor(const tab_groups::TabGroupId& id,
                tab_groups::TabGroupColorId color) {
    if (const TabGroup* group = GroupOf(model(), id)) {
      SetVisuals(id, group->visual_data()->title(), color);
    }
  }

  void SetVisuals(const tab_groups::TabGroupId& id,
                  const std::u16string& title,
                  tab_groups::TabGroupColorId color) {
    const TabGroup* group = GroupOf(model(), id);
    if (!group) {
      return;
    }
    model()->ChangeTabGroupVisuals(
        id, tab_groups::TabGroupVisualData(
                title, color, group->visual_data()->is_collapsed()));
  }

  // Zen's emoji picker, cut down to a grid of icons.
  void ShowIconPicker(const tab_groups::TabGroupId& id) {
    views::View* anchor = nullptr;
    for (size_t i = 0; i < cards_.size(); ++i) {
      if (workspaces_->GetWorkspaces().size() > i &&
          workspaces_->GetWorkspaces()[i] == id) {
        anchor = cards_[i];
      }
    }
    if (!anchor) {
      return;
    }
    auto delegate = std::make_unique<views::BubbleDialogDelegate>(
        anchor, views::BubbleBorder::TOP_LEFT);
    delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    delegate->SetShowCloseButton(false);
    delegate->SetTitle(u"Change icon");
    delegate->SetShowTitle(false);
    auto grid = std::make_unique<views::BoxLayoutView>();
    grid->SetOrientation(views::BoxLayout::Orientation::kVertical);
    grid->SetBetweenChildSpacing(2);
    constexpr size_t kColumns = 8;
    views::BoxLayoutView* row = nullptr;
    views::BubbleDialogDelegate* bubble = delegate.get();
    const auto pick = [](base::WeakPtr<KoanLibrarySpacesSection> section,
                         views::BubbleDialogDelegate* bubble,
                         tab_groups::TabGroupId id, std::u16string icon) {
      if (section) {
        section->SetIcon(id, icon);
      }
      bubble->GetWidget()->Close();
    };
    size_t column = 0;
    for (const char16_t* icon : kIcons) {
      if (column++ % kColumns == 0) {
        row = grid->AddChildView(std::make_unique<views::BoxLayoutView>());
        row->SetBetweenChildSpacing(2);
      }
      auto button = std::make_unique<SpaceTextButton>(
          base::BindRepeating(pick, weak_factory_.GetWeakPtr(),
                              base::Unretained(bubble), id,
                              std::u16string(icon)),
          icon, Font(18));
      button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
      button->SetPreferredSize(gfx::Size(32, 32));
      button->SetBorder(views::NullBorder());
      button->SetBackground(CreateTintBackground(0.f, 6, 0.12f));
      row->AddChildView(std::move(button));
    }
    auto none = std::make_unique<views::LabelButton>(
        base::BindRepeating(pick, weak_factory_.GetWeakPtr(),
                            base::Unretained(bubble), id, std::u16string()),
        u"No icon");
    none->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(6, 8)));
    grid->AddChildView(std::move(none));
    delegate->SetContentsView(std::move(grid));
    // The section owns the bubble; a new one replaces any still open.
    icon_bubble_.reset();
    icon_bubble_delegate_ = std::move(delegate);
    icon_bubble_ = views::BubbleDialogDelegate::CreateBubble(
        icon_bubble_delegate_.get(),
        base::BindOnce(&KoanLibrarySpacesSection::OnIconBubbleClosed,
                       weak_factory_.GetWeakPtr(),
                       base::Unretained(icon_bubble_delegate_.get())));
    icon_bubble_->Show();
  }

  void OnIconBubbleClosed(views::BubbleDialogDelegate* delegate,
                          views::Widget::ClosedReason reason) {
    // Freed once the closing widget is done with itself, unless a newer
    // picker has replaced it by then.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<KoanLibrarySpacesSection> section,
               views::BubbleDialogDelegate* delegate) {
              if (section && section->icon_bubble_delegate_.get() == delegate) {
                section->icon_bubble_.reset();
                section->icon_bubble_delegate_.reset();
              }
            },
            weak_factory_.GetWeakPtr(), base::Unretained(delegate)));
  }

  void FillThemeMenu(KoanLibraryMenu* menu, const tab_groups::TabGroupId& id) {
    for (const auto& [color, label] : tab_groups::GetTabGroupColorLabelMap()) {
      menu->AddItemWithIcon(
          label, library::ColorSwatch(GetTabGroupContextMenuColorId(color), 16),
          base::BindRepeating(&KoanLibrarySpacesSection::SetColor,
                              weak_factory_.GetWeakPtr(), id, color));
    }
  }

  void ShowThemeMenu(const tab_groups::TabGroupId& id, views::Button* source) {
    auto menu = std::make_unique<KoanLibraryMenu>();
    FillThemeMenu(menu.get(), id);
    RunMenu(std::move(menu), source, source->GetBoundsInScreen(),
            views::MenuAnchorPosition::kTopLeft,
            ui::mojom::MenuSourceType::kMouse);
  }

  // Zen's space menu (zenWorkspaceMoreActions), for what Koan's spaces do.
  void ShowSpaceMenu(const tab_groups::TabGroupId& id, views::Button* source) {
    auto menu = std::make_unique<KoanLibraryMenu>();
    menu->AddItem(u"Change Name…",
                  base::BindRepeating(&KoanLibrarySpacesSection::StartRename,
                                      weak_factory_.GetWeakPtr(), id));
    menu->AddItem(u"Change Icon…",
                  base::BindRepeating(&KoanLibrarySpacesSection::ShowIconPicker,
                                      weak_factory_.GetWeakPtr(), id));
    FillThemeMenu(menu->AddSubMenu(u"Edit Theme…"), id);
    menu->AddSeparator();
    menu->AddItem(u"Create Space",
                  base::BindRepeating(&KoanLibrarySpacesSection::CreateSpace,
                                      weak_factory_.GetWeakPtr()));
    menu->AddItem(u"Delete Space",
                  base::BindRepeating(&KoanLibrarySpacesSection::DeleteSpace,
                                      weak_factory_.GetWeakPtr(), id));
    RunMenu(std::move(menu), source, source->GetBoundsInScreen(),
            views::MenuAnchorPosition::kTopRight,
            ui::mojom::MenuSourceType::kMouse);
  }

  void CreateSpace() {
    // The new space's first tab mustn't close the library.
    library()->KeepOpenWhile(
        base::BindOnce(&KoanWorkspaceController::CreateWorkspace,
                       base::Unretained(workspaces_.get())));
  }

  void DeleteSpace(const tab_groups::TabGroupId& id) {
    ShowLibraryConfirmation(
        library()->browser_view(), u"Delete Space?",
        u"The space and all its tabs will be closed.", u"Delete Space",
        base::BindOnce(
            [](base::WeakPtr<KoanLibrarySpacesSection> section,
               tab_groups::TabGroupId id) {
              if (section && GroupOf(section->model(), id)) {
                section->model()->CloseAllTabsInGroup(id);
              }
            },
            weak_factory_.GetWeakPtr(), id));
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
  raw_ptr<SpacesStrip> strip_ = nullptr;
  std::vector<raw_ptr<SpaceCard>> cards_;
  raw_ptr<RenameField> rename_field_ = nullptr;
  std::optional<tab_groups::TabGroupId> renaming_group_;
  bool renaming_ = false;
  bool shown_ = false;
  int drag_index_ = -1;
  int drop_index_ = -1;
  int drag_start_x_ = 0;
  base::OneShotTimer rebuild_timer_;
  // The icon picker; the delegate outlives its widget.
  std::unique_ptr<views::BubbleDialogDelegate> icon_bubble_delegate_;
  std::unique_ptr<views::Widget> icon_bubble_;
  base::WeakPtrFactory<KoanLibrarySpacesSection> weak_factory_{this};
};

BEGIN_METADATA(KoanLibrarySpacesSection)
ADD_READONLY_PROPERTY_METADATA(int, CardCount)
END_METADATA

MoveHandle::MoveHandle(KoanLibrarySpacesSection* section, int index)
    : views::ImageButton(views::Button::PressedCallback()),
      section_(section),
      index_(index) {
  SetImageModel(STATE_NORMAL, library::MoveGlyph(16));
  SetImageHorizontalAlignment(ALIGN_CENTER);
  SetImageVerticalAlignment(ALIGN_MIDDLE);
  SetPreferredSize(gfx::Size(28, 28));
  SetBackground(CreateTintBackground(0.f, 6, library::kPillHoverAlpha));
  SetTooltipText(u"Drag to reorder");
  GetViewAccessibility().SetName(u"Drag to reorder");
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
