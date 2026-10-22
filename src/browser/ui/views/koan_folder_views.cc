// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_folder_views.h"

#include <algorithm>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

#include "base/functional/callback_helpers.h"
#include "base/i18n/case_conversion.h"
#include "base/memory/ptr_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_folder_controller.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/text_constants.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace koan {

namespace {

// Zen's folder icon is drawn from a 27-unit SVG.
constexpr float kIconScale = kFolderIconSize / 27.f;
// zen.folders.search.hover-delay.
constexpr base::TimeDelta kSearchHoverDelay = base::Milliseconds(500);

// Zen's folder colours (koan_color_id.h).
struct FolderColors {
  SkColor behind;
  SkColor front;
  SkColor stroke;
};

// The transform Zen animates a folder half with while it opens: skewX,
// translate, scale (CSS transform-origin 0 0 of the SVG).
SkMatrix OpenTransform(const SkMatrix& base,
                       float open,
                       float skew_degrees,
                       float dx,
                       float dy) {
  SkMatrix m = base;
  m.preSkew(std::tan(skew_degrees * open * std::numbers::pi_v<float> / 180.f),
            0);
  m.preTranslate(dx * open, dy * open);
  m.preScale(1.f - 0.15f * open, 1.f - 0.15f * open);
  return m;
}

// Paints Zen's folder SVG with its top-left at `origin`.
void PaintFolderIcon(gfx::Canvas* canvas,
                     const gfx::PointF& origin,
                     float open,
                     bool dots,
                     const FolderColors& colors) {
  // The SVG is shifted by (-1, -1) in Zen.
  SkMatrix base = SkMatrix::Translate(origin.x() - 1, origin.y() - 1);
  base.preScale(kIconScale, kIconScale);
  const float stroke_width = 1.5f * kIconScale * (1.f - 0.15f * open);

  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setStyle(cc::PaintFlags::kFill_Style);
  cc::PaintFlags stroke;
  stroke.setAntiAlias(true);
  stroke.setStyle(cc::PaintFlags::kStroke_Style);
  stroke.setStrokeWidth(stroke_width);
  stroke.setColor(colors.stroke);

  // Back: the folder with its tab.
  const SkMatrix back = OpenTransform(base, open, 16.f, -4.f, 2.f);
  SkPathBuilder back_path;
  back_path.moveTo(8, 5.625f)
      .lineTo(11.9473f, 5.625f)
      .cubicTo(12.4866f, 5.625f, 13.0105f, 5.80861f, 13.4316f, 6.14551f)
      .lineTo(14.2881f, 6.83105f)
      .cubicTo(14.9308f, 7.34508f, 15.7298f, 7.625f, 16.5527f, 7.625f)
      .lineTo(20, 7.625f)
      .cubicTo(21.3117f, 7.625f, 22.375f, 8.68832f, 22.375f, 10)
      .lineTo(22.375f, 20)
      .cubicTo(22.375f, 21.3117f, 21.3117f, 22.375f, 20, 22.375f)
      .lineTo(8, 22.375f)
      .cubicTo(6.68832f, 22.375f, 5.625f, 21.3117f, 5.625f, 20)
      .lineTo(5.625f, 8)
      .cubicTo(5.625f, 6.68832f, 6.68832f, 5.625f, 8, 5.625f)
      .close();
  const SkPath back_shape = back_path.detach(&back);
  fill.setColor(colors.behind);
  canvas->DrawPath(back_shape, fill);
  canvas->DrawPath(back_shape, stroke);

  // Front: the folder's cover, with three dots while a collapsed folder
  // shows its active tab.
  const SkMatrix front = OpenTransform(base, open, -16.f, 8.f, 2.f);
  SkPathBuilder front_path;
  front_path.addRRect(SkRRect::MakeRectXY(
      SkRect::MakeXYWH(5.625f, 9.625f, 16.75f, 12.75f), 2.375f, 2.375f));
  const SkPath front_shape = front_path.detach(&front);
  fill.setColor(colors.front);
  canvas->DrawPath(front_shape, fill);
  canvas->DrawPath(front_shape, stroke);
  if (dots) {
    SkPathBuilder dots_path;
    for (float x : {10.f, 14.f, 18.f}) {
      dots_path.addCircle(x, 16, 1.25f);
    }
    fill.setColor(colors.stroke);
    canvas->DrawPath(dots_path.detach(&front), fill);
  }
}

// Zen's formatRelativeTime().
std::u16string FormatRelativeTime(base::Time time) {
  const int64_t seconds = (base::Time::Now() - time).InSeconds();
  const auto plural = [](int64_t n, const char* unit) {
    return base::UTF8ToUTF16(base::StrCat(
        {base::NumberToString(n), " ", unit, n == 1 ? "" : "s", " ago"}));
  };
  if (seconds < 60) {
    return u"Just now";
  }
  const int64_t minutes = seconds / 60;
  if (minutes < 60) {
    return plural(minutes, "minute");
  }
  const int64_t hours = minutes / 60;
  if (hours < 24) {
    return plural(hours, "hour");
  }
  const int64_t days = hours / 24;
  if (days < 30) {
    return plural(days, "day");
  }
  return plural(days / 30, "month");
}

// The rename field: commits when it loses focus, like Zen's tab renamer.
class KoanFolderNameField : public views::Textfield {
  METADATA_HEADER(KoanFolderNameField, views::Textfield)

 public:
  explicit KoanFolderNameField(base::RepeatingClosure on_blur)
      : on_blur_(std::move(on_blur)) {}

  // views::Textfield:
  void OnBlur() override {
    views::Textfield::OnBlur();
    if (GetVisible()) {
      on_blur_.Run();
    }
  }

 private:
  base::RepeatingClosure on_blur_;
};

BEGIN_METADATA(KoanFolderNameField)
END_METADATA

}  // namespace

KoanFolderRowView::KoanFolderRowView(KoanFolderController* controller,
                                     int folder_id)
    : views::Button(base::BindRepeating(&KoanFolderRowView::OnPressed,
                                        base::Unretained(this))),
      controller_(controller->GetWeakPtr()),
      folder_id_(folder_id) {
  set_context_menu_controller(this);
  open_animation_.SetSlideDuration(base::Milliseconds(300));
  open_animation_.SetTweenType(gfx::Tween::ACCEL_40_DECEL_100_3);
  name_field_ = AddChildView(std::make_unique<KoanFolderNameField>(
      base::BindRepeating(&KoanFolderRowView::FinishRename,
                          weak_factory_.GetWeakPtr(), /*commit=*/true)));
  name_field_->SetVisible(false);
  name_field_->set_controller(this);
  name_field_->SetFontList(
      gfx::FontList().DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));
  name_field_->GetViewAccessibility().SetName(u"Folder name");
  Update();
  open_animation_.Reset(collapsed_ ? 0 : 1);
}

KoanFolderRowView::~KoanFolderRowView() {
  // The field may still have focus; it blurs while ~View removes it, after
  // this row is gone (the blur callback is weak too).
  name_field_->set_controller(nullptr);
}

void KoanFolderRowView::Update() {
  const KoanFolder* folder =
      controller_ ? controller_->GetFolder(folder_id_) : nullptr;
  if (!folder) {
    return;
  }
  name_ = folder->name;
  collapsed_ = folder->collapsed;
  has_active_ = collapsed_ && controller_->HasActiveTab(folder_id_);
  tab_count_ = static_cast<int>(
      controller_->GetTabs(folder_id_, /*recursive=*/true).size());
  const std::u16string count_text =
      base::StrCat({base::NumberToString16(tab_count_),
                    tab_count_ == 1 ? u" tab" : u" tabs"});
  SetTooltipText(base::StrCat({name_, u" (", count_text, u")"}));
  // The icon's dots (a collapsed folder showing its active tab) are said too.
  GetViewAccessibility().SetName(
      base::StrCat({u"Folder ", name_, u", ", count_text,
                    has_active_ ? u", showing the active tab" : u""}));
  if (collapsed_) {
    GetViewAccessibility().SetIsCollapsed();
    open_animation_.Hide();
  } else {
    GetViewAccessibility().SetIsExpanded();
    open_animation_.Show();
  }
  SchedulePaint();
}

void KoanFolderRowView::StartRename() {
  name_field_->SetText(name_);
  name_field_->SetVisible(true);
  name_field_->SelectAll(/*reversed=*/false);
  name_field_->RequestFocus();
  InvalidateLayout();
  SchedulePaint();
}

bool KoanFolderRowView::GetRenaming() const {
  return name_field_->GetVisible();
}

void KoanFolderRowView::FinishRename(bool commit) {
  if (!name_field_->GetVisible()) {
    return;
  }
  name_field_->SetVisible(false);
  if (commit) {
    // Posted: renaming rebuilds rows, and this may run inside the field's
    // own event handling.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&KoanFolderController::Rename, controller_, folder_id_,
                       std::u16string(name_field_->GetText())));
  }
  SchedulePaint();
}

void KoanFolderRowView::OnPressed() {
  if (GetRenaming()) {
    return;
  }
  search_timer_.Stop();
  if (controller_) {
    controller_->SetCollapsed(folder_id_, !collapsed_);
  }
}

bool KoanFolderRowView::IsNarrow() const {
  return width() < kMinExpandedRowWidth;
}

gfx::Size KoanFolderRowView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(kMinExpandedRowWidth, SidebarRowHeight());
}

void KoanFolderRowView::Layout(PassKey) {
  const int field_height = std::min(height(), 24);
  name_field_->SetBounds(
      kFolderLabelX - 4, (height() - field_height) / 2,
      std::max(0, width() - kFolderLabelX - kFolderTrailingPadding + 4),
      field_height);
}

bool KoanFolderRowView::OnMousePressed(const ui::MouseEvent& event) {
  // Double-click renames, like Zen's folder label (the first click already
  // toggled the folder).
  if (event.IsOnlyLeftMouseButton() && event.GetClickCount() == 2 &&
      !IsNarrow()) {
    StartRename();
    return false;
  }
  return views::Button::OnMousePressed(event);
}

void KoanFolderRowView::OnMouseEntered(const ui::MouseEvent& event) {
  views::Button::OnMouseEntered(event);
  if (!collapsed_ || GetRenaming()) {
    return;
  }
  search_timer_.Start(
      FROM_HERE, kSearchHoverDelay,
      base::BindOnce(&KoanFolderController::ShowSearch, controller_, folder_id_,
                     base::Unretained(this)));
}

void KoanFolderRowView::OnMouseExited(const ui::MouseEvent& event) {
  views::Button::OnMouseExited(event);
  search_timer_.Stop();
}

void KoanFolderRowView::OnPaintBackground(gfx::Canvas* canvas) {
  if (GetState() != STATE_HOVERED && GetState() != STATE_PRESSED) {
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  // Like the New Tab row: Zen's hover, a little stronger while pressed.
  flags.setColor(GetColorProvider()->GetColor(
      GetState() == STATE_PRESSED ? kColorKoanSidebarRowPressed
                                  : kColorKoanSidebarTabHovered));
  gfx::RectF bounds(GetLocalBounds());
  bounds.Inset(2);
  canvas->DrawRoundRect(bounds, SidebarRowCornerRadius(), flags);
}

void KoanFolderRowView::PaintButtonContents(gfx::Canvas* canvas) {
  const ui::ColorProvider* provider = GetColorProvider();
  const KoanFolder* folder =
      controller_ ? controller_->GetFolder(folder_id_) : nullptr;
  if (!folder) {
    return;
  }
  const int icon_x =
      IsNarrow() ? (width() - kFolderIconSize) / 2 : kFolderIconX;
  PaintFolderIcon(canvas,
                  gfx::PointF(icon_x, (height() - kFolderIconSize) / 2.f),
                  open_animation_.GetCurrentValue(), has_active_,
                  {provider->GetColor(kColorKoanFolderIconBack),
                   provider->GetColor(kColorKoanFolderIconFront),
                   provider->GetColor(kColorKoanFolderIconStroke)});

  if (IsNarrow() || GetRenaming()) {
    return;
  }
  int right = width() - kFolderTrailingPadding;
  if (collapsed_) {
    // The tab count, muted, while the folder's tabs are hidden.
    const gfx::FontList count_font;
    const std::u16string count = base::NumberToString16(tab_count_);
    const int count_width = gfx::GetStringWidth(count, count_font);
    canvas->DrawStringRectWithFlags(
        count, count_font, provider->GetColor(kColorKoanFolderCount),
        gfx::Rect(right - count_width, 0, count_width, height()),
        gfx::Canvas::TEXT_ALIGN_RIGHT);
    right -= count_width + kFolderCountGap;
  }
  canvas->DrawStringRectWithFlags(
      name_, gfx::FontList().DeriveWithWeight(gfx::Font::Weight::SEMIBOLD),
      provider->GetColor(kColorToolbarText),
      gfx::Rect(kFolderLabelX, 0, std::max(0, right - kFolderLabelX), height()),
      gfx::Canvas::TEXT_ALIGN_LEFT);
}

void KoanFolderRowView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  search_timer_.Stop();
  if (controller_) {
    controller_->ShowFolderMenu(folder_id_, this, point, source_type);
  }
}

bool KoanFolderRowView::HandleKeyEvent(views::Textfield* sender,
                                       const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    FinishRename(/*commit=*/true);
    return true;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    FinishRename(/*commit=*/false);
    return true;
  }
  return false;
}

void KoanFolderRowView::AnimationProgressed(const gfx::Animation* animation) {
  if (animation == &open_animation_) {
    SchedulePaint();
    return;
  }
  views::Button::AnimationProgressed(animation);
}

BEGIN_METADATA(KoanFolderRowView)
ADD_READONLY_PROPERTY_METADATA(std::u16string, FolderName)
ADD_READONLY_PROPERTY_METADATA(bool, Collapsed)
ADD_READONLY_PROPERTY_METADATA(int, TabCount)
ADD_READONLY_PROPERTY_METADATA(bool, Renaming)
END_METADATA

namespace {

// A tab in the folder search list: favicon, title, and "<time> • <folder>".
class KoanFolderSearchItem : public views::Button {
  METADATA_HEADER(KoanFolderSearchItem, views::Button)

 public:
  KoanFolderSearchItem(PressedCallback callback,
                       tabs::TabInterface* tab,
                       const std::u16string& folder_name)
      : views::Button(std::move(callback)), handle_(tab->GetHandle()) {
    TabUIHelper* helper = TabUIHelper::From(tab);
    title_ = helper->GetTitle();
    content::WebContents* contents = tab->GetContents();
    filter_text_ = base::i18n::ToLower(base::StrCat(
        {title_, u" ", base::UTF8ToUTF16(contents->GetVisibleURL().host())}));

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 8), 10));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    AddChildView(std::make_unique<views::ImageView>(helper->GetFavicon()))
        ->SetImageSize(gfx::Size(16, 16));
    auto* labels = AddChildView(std::make_unique<views::BoxLayoutView>());
    labels->SetOrientation(views::BoxLayout::Orientation::kVertical);
    labels->SetMainAxisAlignment(views::BoxLayout::MainAxisAlignment::kCenter);
    layout->SetFlexForView(labels, 1);
    auto* title = labels->AddChildView(std::make_unique<views::Label>(title_));
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title->SetElideBehavior(gfx::ELIDE_TAIL);
    auto* secondary = labels->AddChildView(std::make_unique<views::Label>(
        base::StrCat({FormatRelativeTime(contents->GetLastActiveTime()), u" • ",
                      folder_name})));
    secondary->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    secondary->SetElideBehavior(gfx::ELIDE_TAIL);
    secondary->SetFontList(gfx::FontList().Derive(-2, gfx::Font::NORMAL,
                                                  gfx::Font::Weight::MEDIUM));
    secondary->SetEnabledColor(kColorKoanFolderCount);
    GetViewAccessibility().SetName(title_);
  }

  tabs::TabHandle handle() const { return handle_; }
  const std::u16string& GetTitle() const { return title_; }
  bool Matches(const std::u16string& query) const {
    return filter_text_.find(query) != std::u16string::npos;
  }
  void SetHighlighted(bool highlighted) {
    highlighted_ = highlighted;
    SchedulePaint();
  }
  bool highlighted() const { return highlighted_; }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kFolderSearchWidth, kFolderSearchItemHeight);
  }
  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (!highlighted_ && GetState() != STATE_HOVERED) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(
        GetColorProvider()->GetColor(kColorKoanFolderSearchItemSelected));
    gfx::RectF bounds(GetLocalBounds());
    bounds.Inset(gfx::InsetsF::VH(2, 4));
    canvas->DrawRoundRect(bounds, kFolderSearchItemCornerRadius, flags);
  }

 private:
  const tabs::TabHandle handle_;
  std::u16string title_;
  std::u16string filter_text_;
  bool highlighted_ = false;
};

BEGIN_METADATA(KoanFolderSearchItem)
ADD_READONLY_PROPERTY_METADATA(std::u16string, Title)
END_METADATA

// The contents of Zen's folder search popup (zen-folder-tabs-popup): a
// search field over the folder's hidden tabs. Arrows / Tab move the
// highlight, Enter or a click opens the tab.
class KoanFolderSearchView : public views::View,
                             public views::TextfieldController {
  METADATA_HEADER(KoanFolderSearchView, views::View)

 public:
  KoanFolderSearchView(views::BubbleDialogDelegate* delegate,
                       KoanFolderController* controller,
                       const std::u16string& folder_name,
                       const std::vector<tabs::TabInterface*>& tab_list)
      : delegate_(delegate), controller_(controller->GetWeakPtr()) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));

    // Zen's header: a search icon, then the field.
    auto* header = AddChildView(std::make_unique<views::BoxLayoutView>());
    header->SetInsideBorderInsets(gfx::Insets::VH(0, 12));
    header->SetBetweenChildSpacing(kFolderSearchIconGap);
    header->SetCrossAxisAlignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    header->AddChildView(
        std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
            vector_icons::kSearchIcon, kColorKoanFolderCount,
            kFolderSearchIconSize)));
    search_ = header->AddChildView(std::make_unique<views::Textfield>());
    header->SetFlexForView(search_, 1);
    search_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(10, 0)));
    search_->SetBackgroundEnabled(false);
    search_->SetPlaceholderText(
        base::StrCat({u"Search ", folder_name, u"..."}));
    search_->GetViewAccessibility().SetName(
        base::StrCat({u"Search ", folder_name}));
    search_->set_controller(this);

    auto* scroll = AddChildView(std::make_unique<views::ScrollView>());
    scroll->SetBackgroundColor(std::nullopt);
    scroll->ClipHeightTo(0, kFolderSearchListMaxHeight);
    list_ = scroll->SetContents(std::make_unique<views::BoxLayoutView>());
    list_->SetOrientation(views::BoxLayout::Orientation::kVertical);
    list_->SetInsideBorderInsets(gfx::Insets::VH(0, 4));
    for (tabs::TabInterface* tab : tab_list) {
      list_->AddChildView(std::make_unique<KoanFolderSearchItem>(
          base::BindRepeating(&KoanFolderSearchView::Open,
                              base::Unretained(this), tab->GetHandle()),
          // Zen names each tab's own folder (a subfolder's, say).
          tab, controller->GetFolder(controller->GetFolderOf(tab))->name));
    }
    no_results_ = AddChildView(
        std::make_unique<views::Label>(u"No tabs matching that search 🤔"));
    no_results_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(24, 5)));
    no_results_->SetVisible(false);
    Filter();
  }

  views::Textfield* search() { return search_; }

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override {
    Filter();
  }
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override {
    if (key_event.type() != ui::EventType::kKeyPressed) {
      return false;
    }
    switch (key_event.key_code()) {
      case ui::VKEY_DOWN:
        MoveHighlight(1);
        return true;
      case ui::VKEY_UP:
        MoveHighlight(-1);
        return true;
      case ui::VKEY_TAB:
        MoveHighlight(key_event.IsShiftDown() ? -1 : 1);
        return true;
      case ui::VKEY_RETURN:
        for (views::View* child : list_->children()) {
          auto* item = static_cast<KoanFolderSearchItem*>(child);
          if (item->GetVisible() && item->highlighted()) {
            Open(item->handle());
            return true;
          }
        }
        return true;
      default:
        return false;
    }
  }

 private:
  std::vector<KoanFolderSearchItem*> VisibleItems() {
    std::vector<KoanFolderSearchItem*> items;
    for (views::View* child : list_->children()) {
      if (child->GetVisible()) {
        items.push_back(static_cast<KoanFolderSearchItem*>(child));
      }
    }
    return items;
  }

  void Filter() {
    const std::u16string query = base::i18n::ToLower(search_->GetText());
    for (views::View* child : list_->children()) {
      auto* item = static_cast<KoanFolderSearchItem*>(child);
      item->SetVisible(item->Matches(query));
      item->SetHighlighted(false);
    }
    const std::vector<KoanFolderSearchItem*> items = VisibleItems();
    if (!items.empty()) {
      items.front()->SetHighlighted(true);
    }
    no_results_->SetVisible(items.empty());
    if (GetWidget()) {
      delegate_->SizeToContents();
    }
  }

  void MoveHighlight(int delta) {
    const std::vector<KoanFolderSearchItem*> items = VisibleItems();
    if (items.empty()) {
      return;
    }
    const auto it = std::ranges::find_if(
        items, [](KoanFolderSearchItem* item) { return item->highlighted(); });
    const int size = static_cast<int>(items.size());
    const int index =
        it == items.end()
            ? 0
            : (static_cast<int>(it - items.begin()) + delta + size) % size;
    for (KoanFolderSearchItem* item : items) {
      item->SetHighlighted(false);
    }
    items[index]->SetHighlighted(true);
    items[index]->ScrollViewToVisible();
  }

  void Open(tabs::TabHandle handle) {
    if (controller_ && handle.Get()) {
      controller_->ActivateTab(handle.Get());
    }
    GetWidget()->Close();
  }

  const raw_ptr<views::BubbleDialogDelegate> delegate_;
  base::WeakPtr<KoanFolderController> controller_;
  raw_ptr<views::Textfield> search_ = nullptr;
  raw_ptr<views::BoxLayoutView> list_ = nullptr;
  raw_ptr<views::Label> no_results_ = nullptr;
};

BEGIN_METADATA(KoanFolderSearchView)
END_METADATA

}  // namespace

KoanFolderSearch::KoanFolderSearch() = default;
KoanFolderSearch::~KoanFolderSearch() = default;

// static
std::unique_ptr<KoanFolderSearch> KoanFolderSearch::Show(
    KoanFolderController* controller,
    int folder_id,
    views::View* anchor,
    base::OnceClosure on_closed) {
  const KoanFolder* folder = controller->GetFolder(folder_id);
  if (!folder || !anchor->GetWidget()) {
    return nullptr;
  }
  // The tabs not already showing: Zen leaves out the active tab, which a
  // collapsed folder still shows.
  std::vector<tabs::TabInterface*> tab_list;
  for (tabs::TabInterface* tab :
       controller->GetTabs(folder_id, /*recursive=*/true)) {
    if (!tab->IsActivated()) {
      tab_list.push_back(tab);
    }
  }
  if (tab_list.empty()) {
    return nullptr;
  }
  auto search = base::WrapUnique(new KoanFolderSearch());
  search->delegate_ = std::make_unique<views::BubbleDialogDelegate>(
      anchor, views::BubbleBorder::LEFT_TOP);
  views::BubbleDialogDelegate* delegate = search->delegate_.get();
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_margins(gfx::Insets());
  delegate->set_fixed_width(kFolderSearchWidth);
  auto* contents =
      delegate->SetContentsView(std::make_unique<KoanFolderSearchView>(
          delegate, controller, folder->name, tab_list));
  delegate->SetInitiallyFocusedView(contents->search());
  search->widget_ = views::BubbleDialogDelegate::CreateBubble(
      delegate,
      base::IgnoreArgs<views::Widget::ClosedReason>(std::move(on_closed)));
  search->widget_->Show();
  return search;
}

}  // namespace koan
