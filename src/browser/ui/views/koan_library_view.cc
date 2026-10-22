// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_library_view.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_library.h"
#include "koan/browser/ui/views/koan_library_icons.h"
#include "koan/browser/ui/views/koan_library_sections.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/menu_separator_types.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace koan {

using library::CreateStateBackground;
using library::Font;
using library::LibraryLabel;

namespace {

// Zen: SEARCH_DEBOUNCE_MS.
constexpr base::TimeDelta kSearchDebounce = base::Milliseconds(250);
// Loads more once the end of a list is this close (Zen's rootMargin).
constexpr int kLoadMoreMargin = 200;
// Below this window height the tab labels go (Zen's max-height: 575px).
constexpr int kTabLabelMinHeight = 575;

// Zen's .zen-library-tab: margin 8px, padding 16px 0, a 28px icon, a 4px
// gap and the label; the tabs column has another 8px gap.
constexpr int kTabMargin = 8;
constexpr int kTabGap = 8;
constexpr int kTabPadding = 16;
constexpr int kTabIconSize = 28;
// Zen's #zen-library-footer: padding 8px.
constexpr int kFooterPadding = 8;

std::u16string SectionLabel(LibrarySection section) {
  switch (section) {
    case LibrarySection::kDownloads:
      return u"Downloads";
    case LibrarySection::kSpaces:
      return u"Spaces";
    case LibrarySection::kHistory:
      return u"History";
  }
  return std::u16string();
}

size_t Index(LibrarySection section) {
  return static_cast<size_t>(section);
}

// Lays out children left to right, wrapping into rows, like
// .zen-library-filter-options (flex-wrap: wrap; gap: 8px).
class WrapView : public views::View {
  METADATA_HEADER(WrapView, views::View)

 public:
  explicit WrapView(int width) : width_(width) {}

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(width_, LayoutChildren(/*apply=*/false));
  }
  void Layout(PassKey) override { LayoutChildren(/*apply=*/true); }

 private:
  static constexpr int kGap = 8;

  int LayoutChildren(bool apply) const {
    int x = 0;
    int y = 0;
    int row_height = 0;
    for (views::View* child : children()) {
      if (!child->GetVisible()) {
        continue;
      }
      const gfx::Size size = child->GetPreferredSize();
      if (x > 0 && x + size.width() > width_) {
        x = 0;
        y += row_height + kGap;
        row_height = 0;
      }
      if (apply) {
        child->SetBounds(x, y, size.width(), size.height());
      }
      x += size.width() + kGap;
      row_height = std::max(row_height, size.height());
    }
    return y + row_height;
  }

  const int width_;
};

BEGIN_METADATA(WrapView)
END_METADATA

}  // namespace

// A section tab in the side column: Zen's animated sprite (here its resting
// frame) over the section's name.
class KoanLibraryTab : public views::Button,
                       public views::ContextMenuController {
  METADATA_HEADER(KoanLibraryTab, views::Button)

 public:
  KoanLibraryTab(KoanLibraryView* panel, LibrarySection section)
      : views::Button(base::BindRepeating(&KoanLibraryView::ShowSection,
                                          base::Unretained(panel),
                                          section)),
        panel_(panel),
        section_(section) {
    label_ = AddChildView(std::make_unique<LibraryLabel>(
        SectionLabel(section), 13, gfx::Font::Weight::SEMIBOLD));
    label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    label_->SetCanProcessEventsWithinSubtree(false);
    SetTooltipText(SectionLabel(section));
    GetViewAccessibility().SetName(SectionLabel(section));
    set_context_menu_controller(this);
  }

  void SetActive(bool active) {
    if (active_ != active) {
      active_ = active;
      SchedulePaint();
    }
  }
  bool GetActive() const { return active_; }
  std::u16string GetLabel() const { return std::u16string(label_->GetText()); }

  void SetLabelVisible(bool visible) {
    if (label_->GetVisible() != visible) {
      label_->SetVisible(visible);
      PreferredSizeChanged();
    }
  }

  void SetProgress(float progress) {
    if (progress_ != progress) {
      progress_ = progress;
      SchedulePaint();
    }
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    int height = 2 * kTabPadding + kTabIconSize;
    if (label_->GetVisible()) {
      height += 4 + label_->GetPreferredSize().height();
    }
    return gfx::Size(library::kSideWidth - 2 * kTabMargin, height);
  }

  void Layout(PassKey) override {
    const int y = kTabPadding + kTabIconSize + 4;
    label_->SetBounds(0, y, width(), label_->GetPreferredSize().height());
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const ui::ColorProvider* provider = GetColorProvider();
    std::optional<ui::ColorId> background;
    if (active_) {
      background = kColorKoanLibraryTabSelected;
    } else if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      background = kColorKoanLibraryTabHovered;
    }
    if (background) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(provider->GetColor(*background));
      canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), library::kTabRadius,
                            flags);
    }
    const gfx::RectF icon((width() - kTabIconSize) / 2.f, kTabPadding,
                          kTabIconSize, kTabIconSize);
    // The Downloads icon is filled like the selected one while anything
    // downloads (zen-library-downloading).
    library::PaintSectionIcon(canvas, icon, section_, provider,
                              active_ || progress_ >= 0.f, progress_);
  }

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override {
    // Zen's tabMenu: what the section offers for all it lists.
    panel_->ShowTabMenu(section_, point);
  }

 private:
  const raw_ptr<KoanLibraryView> panel_;
  const LibrarySection section_;
  raw_ptr<views::Label> label_ = nullptr;
  bool active_ = false;
  float progress_ = -1.f;
};

BEGIN_METADATA(KoanLibraryTab)
ADD_READONLY_PROPERTY_METADATA(bool, Active)
ADD_READONLY_PROPERTY_METADATA(std::u16string, Label)
END_METADATA

void ShowLibraryConfirmation(BrowserView* browser_view,
                             std::u16string title,
                             std::u16string message,
                             std::u16string accept,
                             base::OnceClosure on_accept) {
  constrained_window::ShowBrowserModal(
      ui::DialogModel::Builder(std::make_unique<ui::DialogModelDelegate>())
          .SetTitle(std::move(title))
          .AddParagraph(ui::DialogModelLabel(std::move(message)))
          .AddOkButton(
              std::move(on_accept),
              ui::DialogModel::Button::Params().SetLabel(std::move(accept)))
          .AddCancelButton(base::DoNothing())
          .Build(),
      browser_view->GetNativeWindow());
}

// KoanLibraryMenu -----------------------------------------------------------

KoanLibraryMenu::KoanLibraryMenu() = default;
KoanLibraryMenu::~KoanLibraryMenu() = default;

void KoanLibraryMenu::AddItem(std::u16string label,
                              base::RepeatingClosure action,
                              bool enabled) {
  model_.AddItem(static_cast<int>(actions_.size()), label);
  actions_.push_back(std::move(action));
  enabled_.push_back(enabled);
}

void KoanLibraryMenu::AddItemWithIcon(std::u16string label,
                                      const ui::ImageModel& icon,
                                      base::RepeatingClosure action) {
  model_.AddItemWithIcon(static_cast<int>(actions_.size()), label, icon);
  actions_.push_back(std::move(action));
  enabled_.push_back(true);
}

void KoanLibraryMenu::AddSeparator() {
  model_.AddSeparator(ui::NORMAL_SEPARATOR);
}

KoanLibraryMenu* KoanLibraryMenu::AddSubMenu(std::u16string label) {
  submenus_.push_back(std::make_unique<KoanLibraryMenu>());
  KoanLibraryMenu* submenu = submenus_.back().get();
  model_.AddSubMenu(static_cast<int>(actions_.size()), label, &submenu->model_);
  actions_.push_back(base::DoNothing());
  enabled_.push_back(true);
  return submenu;
}

void KoanLibraryMenu::Run(views::View* source,
                          const gfx::Rect& bounds,
                          views::MenuAnchorPosition anchor,
                          ui::mojom::MenuSourceType source_type,
                          base::RepeatingClosure on_closed) {
  runner_ = std::make_unique<views::MenuRunner>(
      &model_, views::MenuRunner::CONTEXT_MENU, std::move(on_closed));
  runner_->RunMenuAt(source->GetWidget(), nullptr, bounds, anchor, source_type);
}

bool KoanLibraryMenu::IsRunning() const {
  return runner_ && runner_->IsRunning();
}

void KoanLibraryMenu::ExecuteCommand(int command_id, int event_flags) {
  if (command_id >= 0 && static_cast<size_t>(command_id) < actions_.size()) {
    actions_[command_id].Run();
  }
}

bool KoanLibraryMenu::IsCommandIdEnabled(int command_id) const {
  return command_id >= 0 && static_cast<size_t>(command_id) < enabled_.size()
             ? enabled_[command_id]
             : false;
}

// KoanLibraryPill -----------------------------------------------------------

KoanLibraryPill::KoanLibraryPill(PressedCallback callback,
                                 std::u16string text,
                                 gfx::Font::Weight weight,
                                 const gfx::Insets& padding)
    : views::LabelButton(std::move(callback), std::move(text)) {
  label()->SetFontList(Font(14, weight));
  // On the library's translucent layers.
  SetTextSubpixelRenderingEnabled(false);
  SetEnabledTextColors(kColorKoanLibraryForeground);
  SetBorder(views::CreateEmptyBorder(padding));
  SetImageLabelSpacing(8);
  SetActive(false);
}

KoanLibraryPill::~KoanLibraryPill() = default;

void KoanLibraryPill::SetActive(bool active) {
  active_ = active;
  SetBackground(CreateStateBackground(
      active ? kColorKoanLibraryChipActive : kColorKoanLibraryPill,
      active ? kColorKoanLibraryChipActive : kColorKoanLibraryPillHovered,
      library::kPillRadius));
  SchedulePaint();
}

void KoanLibraryPill::SetUnavailable(bool unavailable) {
  SetEnabled(!unavailable);
  if (!layer()) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }
  layer()->SetOpacity(unavailable ? library::kUnavailableOpacity : 1.f);
}

BEGIN_METADATA(KoanLibraryPill)
ADD_READONLY_PROPERTY_METADATA(bool, Active)
END_METADATA

// KoanLibraryRow ------------------------------------------------------------

KoanLibraryRow::KoanLibraryRow(PressedCallback callback)
    : views::Button(std::move(callback)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(10, 12), 14));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  icon_box_ = AddChildView(std::make_unique<views::View>());
  icon_box_->SetLayoutManager(std::make_unique<views::BoxLayout>());
  icon_box_->SetCanProcessEventsWithinSubtree(false);

  auto* text = AddChildView(std::make_unique<views::BoxLayoutView>());
  text->SetOrientation(views::BoxLayout::Orientation::kVertical);
  text->SetBetweenChildSpacing(2);
  text->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  text->SetCanProcessEventsWithinSubtree(false);
  title_ = text->AddChildView(std::make_unique<LibraryLabel>(
      std::u16string(), 14, gfx::Font::Weight::MEDIUM));
  subtitle_ = text->AddChildView(std::make_unique<LibraryLabel>(
      std::u16string(), 12, gfx::Font::Weight::NORMAL,
      kColorKoanLibraryForegroundSecondary));
  layout->SetFlexForView(text, 1);

  auto* actions = AddChildView(std::make_unique<views::BoxLayoutView>());
  actions->SetBetweenChildSpacing(4);
  actions->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  actions->SetVisible(false);
  actions_ = actions;

  // Hovering an action button keeps the row hovered.
  SetNotifyEnterExitOnChild(true);
  SetTriggerableEventFlags(ui::EF_LEFT_MOUSE_BUTTON |
                           ui::EF_MIDDLE_MOUSE_BUTTON);
  set_context_menu_controller(this);
}

KoanLibraryRow::~KoanLibraryRow() = default;

void KoanLibraryRow::SetIcon(const ui::ImageModel& icon, int size) {
  icon_box_->RemoveAllChildViews();
  auto* image = icon_box_->AddChildView(std::make_unique<views::ImageView>());
  image->SetImage(icon);
  image->SetImageSize(gfx::Size(size, size));
}

void KoanLibraryRow::SetIconView(std::unique_ptr<views::View> view) {
  icon_box_->RemoveAllChildViews();
  icon_box_->AddChildView(std::move(view));
}

void KoanLibraryRow::SetTitle(std::u16string title) {
  GetViewAccessibility().SetName(title);
  title_->SetText(std::move(title));
}

std::u16string KoanLibraryRow::GetTitle() const {
  return std::u16string(title_->GetText());
}

void KoanLibraryRow::SetSubtitle(std::u16string normal,
                                 std::u16string hovered) {
  subtitle_normal_ = std::move(normal);
  subtitle_hovered_ = std::move(hovered);
  UpdateHover();
}

std::u16string KoanLibraryRow::GetSubtitle() const {
  return std::u16string(subtitle_->GetText());
}

views::View* KoanLibraryRow::AddAction(std::unique_ptr<views::View> action) {
  return actions_->AddChildView(std::move(action));
}

void KoanLibraryRow::ClearActions() {
  actions_->RemoveAllChildViews();
}

void KoanLibraryRow::SetMenuCallback(MenuCallback callback) {
  menu_callback_ = std::move(callback);
}

void KoanLibraryRow::SetMenuOpen(bool open) {
  menu_open_ = open;
  UpdateHover();
}

void KoanLibraryRow::SetDimmed(bool dimmed) {
  if (!layer()) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }
  layer()->SetOpacity(dimmed ? library::kMissingOpacity : 1.f);
}

bool KoanLibraryRow::GetHovered() const {
  return hovered_;
}

void KoanLibraryRow::StateChanged(ButtonState old_state) {
  views::Button::StateChanged(old_state);
  UpdateHover();
}

void KoanLibraryRow::OnPaintBackground(gfx::Canvas* canvas) {
  if (!hovered_) {
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(GetColorProvider()->GetColor(kColorKoanLibraryRowHovered));
  canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), library::kRowRadius,
                        flags);
}

void KoanLibraryRow::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (menu_callback_) {
    menu_callback_.Run(this, point, source_type);
  }
}

void KoanLibraryRow::UpdateHover() {
  const ButtonState state = GetState();
  hovered_ = menu_open_ || state == STATE_HOVERED || state == STATE_PRESSED;
  actions_->SetVisible(hovered_ && !actions_->children().empty());
  const std::u16string& subtitle = hovered_ && !subtitle_hovered_.empty()
                                       ? subtitle_hovered_
                                       : subtitle_normal_;
  subtitle_->SetText(subtitle);
  subtitle_->SetVisible(!subtitle.empty());
  SchedulePaint();
}

BEGIN_METADATA(KoanLibraryRow)
ADD_READONLY_PROPERTY_METADATA(std::u16string, Title)
ADD_READONLY_PROPERTY_METADATA(std::u16string, Subtitle)
ADD_READONLY_PROPERTY_METADATA(bool, Hovered)
END_METADATA

// KoanLibrarySection --------------------------------------------------------

KoanLibrarySection::KoanLibrarySection(KoanLibraryController* library,
                                       LibrarySection id)
    : library_(library), id_(id) {}

KoanLibrarySection::~KoanLibrarySection() = default;

int KoanLibrarySection::GetContentWidth() const {
  return library::kSectionWidth;
}

bool KoanLibrarySection::HandleEscape() {
  return false;
}

void KoanLibrarySection::RunMenu(std::unique_ptr<KoanLibraryMenu> menu,
                                 views::View* source,
                                 const gfx::Rect& screen_bounds,
                                 views::MenuAnchorPosition anchor,
                                 ui::mojom::MenuSourceType source_type,
                                 base::RepeatingClosure on_closed) {
  menu_ = std::move(menu);
  menu_->Run(source, screen_bounds, anchor, source_type, std::move(on_closed));
}

BEGIN_METADATA(KoanLibrarySection)
END_METADATA

// KoanLibrarySearchSection --------------------------------------------------

KoanLibrarySearchSection::FilterOption::FilterOption(std::string id,
                                                     std::u16string label,
                                                     bool disabled)
    : id(std::move(id)), label(std::move(label)), disabled(disabled) {}
KoanLibrarySearchSection::FilterOption::FilterOption(const FilterOption&) =
    default;
KoanLibrarySearchSection::FilterOption::~FilterOption() = default;

KoanLibrarySearchSection::FilterGroup::FilterGroup() = default;
KoanLibrarySearchSection::FilterGroup::FilterGroup(
    std::string id,
    std::u16string title,
    bool exclusive,
    std::vector<FilterOption> options)
    : id(std::move(id)),
      title(std::move(title)),
      exclusive(exclusive),
      options(std::move(options)) {}
KoanLibrarySearchSection::FilterGroup::FilterGroup(const FilterGroup&) =
    default;
KoanLibrarySearchSection::FilterGroup::~FilterGroup() = default;

KoanLibrarySearchSection::KoanLibrarySearchSection(
    KoanLibraryController* library,
    LibrarySection id,
    std::u16string placeholder,
    std::u16string filter_title)
    : KoanLibrarySection(library, id) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // .zen-library-search-top: padding 12px 14px 8px.
  auto* top = AddChildView(std::make_unique<views::BoxLayoutView>());
  top->SetOrientation(views::BoxLayout::Orientation::kVertical);
  top->SetInsideBorderInsets(gfx::Insets::TLBR(12, 14, 8, 14));
  top->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);

  // The search box and the Filter button.
  auto* header = top->AddChildView(std::make_unique<views::BoxLayoutView>());
  header->SetBetweenChildSpacing(8);
  header->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  search_header_ = header;

  auto* box = header->AddChildView(std::make_unique<views::BoxLayoutView>());
  box->SetInsideBorderInsets(gfx::Insets::VH(0, 12));
  box->SetBetweenChildSpacing(8);
  box->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  box->SetBackground(CreateStateBackground(
      kColorKoanLibraryPill, kColorKoanLibraryPill, library::kPillRadius));
  box->SetPreferredSize(gfx::Size(0, 36));
  header->SetFlexForView(box, 1);
  box->AddChildView(
      std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
          vector_icons::kSearchIcon, kColorKoanLibraryForegroundDim, 16)));
  search_field_ = box->AddChildView(std::make_unique<views::Textfield>());
  search_field_->SetBackgroundEnabled(false);
  search_field_->SetBorder(views::NullBorder());
  search_field_->SetFontList(Font(14));
  search_field_->SetTextColorId(kColorKoanLibraryForeground);
  search_field_->SetPlaceholderText(placeholder);
  search_field_->GetViewAccessibility().SetName(placeholder);
  search_field_->set_controller(this);
  box->SetFlexForView(search_field_, 1);

  // Filled in by InitFilters(), once the subclass can say what it filters.
  filter_header_ = top->AddChildView(std::make_unique<views::BoxLayoutView>());
  filter_header_->SetVisible(false);
  filter_panel_ = top->AddChildView(std::make_unique<views::View>());
  filter_panel_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  filter_panel_->SetVisible(false);

  // The header row of the open filter panel: its title and "Done".
  auto* filter_header =
      static_cast<views::BoxLayoutView*>(filter_header_.get());
  filter_header->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  filter_header->SetMinimumCrossAxisSize(36);
  auto* title = filter_header->AddChildView(std::make_unique<LibraryLabel>(
      std::move(filter_title), 14, gfx::Font::Weight::BOLD));
  filter_header->SetFlexForView(title, 1);
  filter_header->AddChildView(std::make_unique<KoanLibraryPill>(
      base::BindRepeating(&KoanLibrarySearchSection::SetFiltersOpen,
                          base::Unretained(this), false),
      u"Done", gfx::Font::Weight::SEMIBOLD, gfx::Insets::VH(8, 14)));

  scroll_ = AddChildView(std::make_unique<views::ScrollView>(
      views::ScrollView::ScrollWithLayers::kEnabled));
  scroll_->SetBackgroundColor(std::nullopt);
  scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_->SetDrawOverflowIndicator(false);
  auto* list = scroll_->SetContents(std::make_unique<views::BoxLayoutView>());
  list->SetOrientation(views::BoxLayout::Orientation::kVertical);
  list->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  list->SetInsideBorderInsets(gfx::Insets::TLBR(0, 10, 10, 10));
  list_ = list;
  layout->SetFlexForView(scroll_, 1);
  scroll_subscription_ =
      scroll_->AddContentsScrolledCallback(base::BindRepeating(
          &KoanLibrarySearchSection::OnScrolled, base::Unretained(this)));
}

KoanLibrarySearchSection::~KoanLibrarySearchSection() {
  search_field_->set_controller(nullptr);
}

// static
KoanLibrarySearchSection::FilterGroup KoanLibrarySearchSection::WhenFilterGroup(
    std::u16string title) {
  return FilterGroup(
      "when", std::move(title), /*exclusive=*/true,
      {{"today", u"Today"}, {"week", u"This Week"}, {"month", u"This Month"}});
}

void KoanLibrarySearchSection::InitFilters() {
  // The Filter button next to the search box.
  auto filter = std::make_unique<KoanLibraryPill>(
      base::BindRepeating(&KoanLibrarySearchSection::SetFiltersOpen,
                          base::Unretained(this), true),
      u"Filter", gfx::Font::Weight::MEDIUM, gfx::Insets::VH(0, 12));
  filter->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(vector_icons::kTuneIcon,
                                     kColorKoanLibraryForeground, 16));
  search_header_->AddChildView(std::move(filter));
  UpdateFilterPanel();
}

void KoanLibrarySearchSection::UpdateFilterPanel() {
  filter_panel_->RemoveAllChildViews();
  // The panel spans the section less its 14px padding.
  const int width = GetContentWidth() - 28;
  for (const FilterGroup& group : GetFilterGroups()) {
    auto* title = filter_panel_->AddChildView(std::make_unique<LibraryLabel>(
        group.title, 13, gfx::Font::Weight::SEMIBOLD));
    title->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(16, 0, 10, 0)));
    auto* options =
        filter_panel_->AddChildView(std::make_unique<WrapView>(width));
    options->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, 0, 6, 0)));
    for (const FilterOption& option : group.options) {
      auto* chip = options->AddChildView(std::make_unique<KoanLibraryPill>(
          base::BindRepeating(&KoanLibrarySearchSection::ToggleFilter,
                              base::Unretained(this), group, option.id),
          option.label, gfx::Font::Weight::NORMAL, gfx::Insets::VH(9, 14)));
      chip->SetActive(IsFilterActive(group.id, option.id));
      if (option.disabled) {
        chip->SetUnavailable(true);
      }
    }
  }
  InvalidateLayout();
}

bool KoanLibrarySearchSection::IsFilterActive(const std::string& group,
                                              const std::string& option) const {
  return active_filters_.contains(base::StrCat({group, ":", option}));
}

void KoanLibrarySearchSection::SetFilterActive(const std::string& group,
                                               const std::string& option,
                                               bool active) {
  const std::string key = base::StrCat({group, ":", option});
  if (active) {
    active_filters_.insert(key);
  } else {
    active_filters_.erase(key);
  }
}

std::optional<int> KoanLibrarySearchSection::GetWhenDays() const {
  if (IsFilterActive("when", "today")) {
    return 1;
  }
  if (IsFilterActive("when", "week")) {
    return 7;
  }
  if (IsFilterActive("when", "month")) {
    return 30;
  }
  return std::nullopt;
}

void KoanLibrarySearchSection::SetFiltersOpen(bool open) {
  filters_open_ = open;
  search_header_->SetVisible(!open);
  filter_header_->SetVisible(open);
  filter_panel_->SetVisible(open);
  if (!open && GetWidget()) {
    search_field_->RequestFocus();
  }
  InvalidateLayout();
}

void KoanLibrarySearchSection::ToggleFilter(const FilterGroup& group,
                                            const std::string& option) {
  const bool was_active = IsFilterActive(group.id, option);
  if (group.exclusive) {
    for (const FilterOption& other : group.options) {
      SetFilterActive(group.id, other.id, false);
    }
  }
  SetFilterActive(group.id, option, !was_active);
  OnFiltersChanged();
  // Rebuilding destroys the chip that was just pressed; do it once the
  // click is over.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanLibrarySearchSection::UpdateFilterPanel,
                                weak_factory_.GetWeakPtr()));
}

void KoanLibrarySearchSection::OnShown() {
  // Zen puts the keyboard in the search field, which is what it is for.
  if (GetWidget() && !filters_open_) {
    search_field_->RequestFocus();
  }
}

void KoanLibrarySearchSection::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  search_timer_.Start(FROM_HERE, kSearchDebounce,
                      base::BindOnce(&KoanLibrarySearchSection::OnSearchTimer,
                                     base::Unretained(this)));
}

void KoanLibrarySearchSection::OnSearchTimer() {
  std::u16string query;
  base::TrimWhitespace(search_field_->GetText(), base::TRIM_ALL, &query);
  if (query != query_) {
    query_ = std::move(query);
    OnSearchChanged();
  }
}

void KoanLibrarySearchSection::ClearList() {
  list_->RemoveAllChildViews();
}

views::View* KoanLibrarySearchSection::AddGroup(const std::u16string& header) {
  auto* group = list_->AddChildView(std::make_unique<views::BoxLayoutView>());
  group->SetOrientation(views::BoxLayout::Orientation::kVertical);
  group->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  if (!header.empty()) {
    // .zen-library-group h3: 12px bold at 60%, margin 10px 8px 5px.
    auto* label = group->AddChildView(
        std::make_unique<LibraryLabel>(header, 12, gfx::Font::Weight::BOLD,
                                       kColorKoanLibraryForegroundSecondary));
    label->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(10, 8, 5, 8)));
  }
  return group;
}

void KoanLibrarySearchSection::ShowEmpty(std::u16string text) {
  auto* label = list_->AddChildView(std::make_unique<LibraryLabel>(
      std::move(text), 14, gfx::Font::Weight::NORMAL,
      kColorKoanLibraryForegroundSecondary));
  label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  label->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(24, 0)));
}

void KoanLibrarySearchSection::OnScrolled() {
  const gfx::Rect visible = scroll_->GetVisibleRect();
  if (visible.bottom() + kLoadMoreMargin >= list_->height()) {
    OnScrolledToEnd();
  }
}

BEGIN_METADATA(KoanLibrarySearchSection)
ADD_READONLY_PROPERTY_METADATA(bool, FiltersOpen)
END_METADATA

// KoanLibraryView -----------------------------------------------------------

KoanLibraryView::KoanLibraryView(KoanLibraryController* library)
    : library_(library) {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  side_ = AddChildView(std::make_unique<views::View>());
  for (LibrarySection section :
       {LibrarySection::kDownloads, LibrarySection::kSpaces,
        LibrarySection::kHistory}) {
    std::unique_ptr<KoanLibrarySection> view;
    switch (section) {
      case LibrarySection::kDownloads:
        view = CreateLibraryDownloadsSection(library);
        break;
      case LibrarySection::kSpaces:
        view = CreateLibrarySpacesSection(library);
        break;
      case LibrarySection::kHistory:
        view = CreateLibraryHistorySection(library);
        break;
    }
    // Spaces exist only where workspaces do (Zen leaves the tab out too).
    if (!view) {
      continue;
    }
    view->SetVisible(false);
    sections_[Index(section)] = AddChildView(std::move(view));
    tabs_[Index(section)] =
        side_->AddChildView(std::make_unique<KoanLibraryTab>(this, section));
  }

  // Zen's footer: the back button that closes the library.
  footer_ = side_->AddChildView(library::CreateIconButton(
      base::BindRepeating(&KoanLibraryController::Close,
                          base::Unretained(library_)),
      vector_icons::kArrowBackIcon, u"Close library",
      library::kSmallButtonIconSize, kFooterPadding, library::kSmallRadius));
}

KoanLibraryView::~KoanLibraryView() = default;

KoanLibrarySection* KoanLibraryView::GetSection(LibrarySection section) const {
  return sections_[Index(section)];
}

KoanLibrarySection* KoanLibraryView::active_section() const {
  return shown_ ? GetSection(*shown_) : nullptr;
}

void KoanLibraryView::ShowSection(LibrarySection section) {
  if (!GetSection(section)) {
    section = LibrarySection::kHistory;
  }
  if (shown_ == section) {
    return;
  }
  if (library_->section() != section) {
    // A click on a tab; the controller remembers it and calls back.
    library_->ShowSection(section);
    return;
  }
  shown_ = section;
  for (size_t i = 0; i < sections_.size(); ++i) {
    if (sections_[i]) {
      sections_[i]->SetVisible(i == Index(section));
      tabs_[i]->SetActive(i == Index(section));
    }
  }
  InvalidateLayout();
  GetSection(section)->OnShown();
  library_->OnPreferredWidthChanged();
}

int KoanLibraryView::GetPreferredWidth() const {
  const KoanLibrarySection* section = active_section();
  return library::kSideWidth +
         (section ? section->GetContentWidth() : library::kSectionWidth);
}

bool KoanLibraryView::GetOpen() const {
  return library_->is_open();
}

void KoanLibraryView::OnOpened() {
  if (KoanLibrarySection* section = active_section()) {
    section->OnShown();
  }
}

bool KoanLibraryView::HandleEscape() {
  KoanLibrarySection* section = active_section();
  return section && section->HandleEscape();
}

void KoanLibraryView::SetDownloadProgress(float progress) {
  if (KoanLibraryTab* tab = tabs_[Index(LibrarySection::kDownloads)]) {
    tab->SetProgress(progress);
  }
}

void KoanLibraryView::ShowTabMenu(LibrarySection section,
                                  const gfx::Point& screen_point) {
  KoanLibrarySection* view = GetSection(section);
  if (!view) {
    return;
  }
  auto menu = std::make_unique<KoanLibraryMenu>();
  view->FillTabMenu(menu.get());
  tab_menu_ = std::move(menu);
  tab_menu_->Run(tabs_[Index(section)], gfx::Rect(screen_point, gfx::Size()),
                 views::MenuAnchorPosition::kTopLeft,
                 ui::mojom::MenuSourceType::kMouse, base::DoNothing());
}

void KoanLibraryView::SetOnRight(bool on_right) {
  if (on_right_ == on_right) {
    return;
  }
  on_right_ = on_right;
  // Zen turns the back arrow round with the panel.
  footer_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(on_right ? vector_icons::kArrowForwardIcon
                                              : vector_icons::kArrowBackIcon,
                                     kColorKoanLibraryForeground,
                                     library::kSmallButtonIconSize));
  InvalidateLayout();
  SchedulePaint();
}

void KoanLibraryView::Layout(PassKey) {
  const gfx::Rect bounds = GetLocalBounds();
  // With tabs on the right the panel is mirrored: side column on the right
  // (Zen's flex-direction: row-reverse).
  side_->SetBounds(on_right_ ? bounds.width() - library::kSideWidth : 0, 0,
                   library::kSideWidth, bounds.height());

  // The back button at the foot of the side column; the tabs centred in the
  // space above it (Zen's side column: justify-content: space-between).
  const gfx::Size footer = footer_->GetPreferredSize();
  const int footer_top = bounds.height() - kFooterPadding - footer.height();
  footer_->SetBounds(on_right_
                         ? library::kSideWidth - kFooterPadding - footer.width()
                         : kFooterPadding,
                     footer_top, footer.width(), footer.height());
  const bool labels = bounds.height() >= kTabLabelMinHeight;
  int tabs_height = 0;
  for (KoanLibraryTab* tab : tabs_) {
    if (tab) {
      tab->SetLabelVisible(labels);
      tabs_height +=
          tab->GetPreferredSize().height() + 2 * kTabMargin + kTabGap;
    }
  }
  tabs_height -= kTabGap;
  int y =
      std::max(0, (footer_top - kFooterPadding - tabs_height) / 2) + kTabMargin;
  for (KoanLibraryTab* tab : tabs_) {
    if (!tab) {
      continue;
    }
    const gfx::Size size = tab->GetPreferredSize();
    tab->SetBounds(kTabMargin, y, size.width(), size.height());
    y += size.height() + 2 * kTabMargin + kTabGap;
  }

  const gfx::Rect content(on_right_ ? 0 : library::kSideWidth, 0,
                          bounds.width() - library::kSideWidth,
                          bounds.height());
  for (KoanLibrarySection* section : sections_) {
    if (section) {
      // Next to the side column.
      const int width = std::min(content.width(), section->GetContentWidth());
      section->SetBounds(on_right_ ? content.right() - width : content.x(),
                         content.y(), width, content.height());
    }
  }
}

void KoanLibraryView::OnPaintBackground(gfx::Canvas* canvas) {
  const ui::ColorProvider* provider = GetColorProvider();
  // The library sits on the window's own surface, like the sidebar.
  canvas->DrawColor(provider->GetColor(kColorKoanLibraryBackground));
  // The side column lightens towards the content (Zen: a gradient from
  // transparent at the window's edge).
  const gfx::Rect side = side_->bounds();
  const SkPoint points[] = {
      {static_cast<float>(on_right_ ? side.right() : side.x()), 0},
      {static_cast<float>(on_right_ ? side.x() : side.right()), 0}};
  const SkColor4f colors[] = {
      SkColors::kTransparent,
      SkColor4f::FromColor(provider->GetColor(kColorKoanLibrarySideHighlight))};
  cc::PaintFlags flags;
  flags.setShader(cc::PaintShader::MakeLinearGradient(points, colors, nullptr,
                                                      2, SkTileMode::kClamp));
  canvas->DrawRect(side, flags);
}

BEGIN_METADATA(KoanLibraryView)
ADD_READONLY_PROPERTY_METADATA(bool, Open)
END_METADATA

}  // namespace koan
