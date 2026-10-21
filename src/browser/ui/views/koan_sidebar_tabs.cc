// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_sidebar_tabs.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "components/tabs/public/tab_group.h"
#include "components/vector_icons/vector_icons.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_essentials.h"
#include "koan/browser/ui/views/koan_shortcuts.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_sidebar_tabs_controller.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "koan/browser/ui/views/koan_url_bar_controller.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/proposed_layout.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace koan {

namespace {

gfx::FontList FontOfSize(int size, gfx::Font::Weight weight) {
  const gfx::FontList base;
  return base.DeriveWithSizeDelta(size - base.GetFontSize())
      .DeriveWithWeight(weight);
}

// The platform's text for the shortcut `key` with the platform's
// accelerator modifier (Cmd on macOS, Ctrl elsewhere) and `modifiers`.
std::u16string ShortcutText(ui::KeyboardCode key, int modifiers = 0) {
  return ui::Accelerator(key, ui::EF_PLATFORM_ACCELERATOR | modifiers)
      .GetShortcutText();
}

// Text drawn at a fixed position starts at the leading edge.
int LeadingTextAlignment() {
  return base::i18n::IsRTL() ? gfx::Canvas::TEXT_ALIGN_RIGHT
                             : gfx::Canvas::TEXT_ALIGN_LEFT;
}

// `rect`, in `view`'s left-to-right coordinates, where it paints in the UI's
// direction.
gfx::Rect Mirrored(const views::View* view, const gfx::Rect& rect) {
  return gfx::Rect(view->GetMirroredXWithWidthInView(rect.x(), rect.width()),
                   rect.y(), rect.width(), rect.height());
}

void PaintSeparatorLine(gfx::Canvas* canvas,
                        const views::View* view,
                        int x,
                        int width) {
  cc::PaintFlags flags;
  flags.setColor(
      view->GetColorProvider()->GetColor(kColorKoanSidebarSeparator));
  canvas->DrawRect(
      Mirrored(view, gfx::Rect(x, view->height() / 2, std::max(0, width), 1)),
      flags);
}

}  // namespace

// Zen's "+ New Tab" row.
class KoanNewTabRow : public views::Button {
  METADATA_HEADER(KoanNewTabRow, views::Button)

 public:
  explicit KoanNewTabRow(base::RepeatingClosure open_new_tab)
      : views::Button(std::move(open_new_tab)) {
    SetTooltipText(u"New Tab (" + ShortcutText(ui::VKEY_T) + u")");
    GetViewAccessibility().SetName(u"New Tab");
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  SidebarRowCornerRadius());
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, SidebarRowHeight());
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* provider = GetColorProvider();
    if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(provider->GetColor(GetState() == STATE_PRESSED
                                            ? kColorKoanSidebarRowPressed
                                            : kColorKoanSidebarRowHovered));
      canvas->DrawRoundRect(GetLocalBounds(), SidebarRowCornerRadius(), flags);
    }

    const SkColor color = provider->GetColor(kColorKoanSidebarNewTabForeground);
    const bool expanded = width() >= kMinExpandedRowWidth;
    const gfx::Rect icon(
        expanded ? kNewTabIconX : (width() - kNewTabIconSize) / 2,
        (height() - kNewTabIconSize) / 2, kNewTabIconSize, kNewTabIconSize);
    const gfx::Rect mirrored_icon = Mirrored(this, icon);
    canvas->DrawImageInt(
        gfx::CreateVectorIcon(vector_icons::kAdd2Icon, kNewTabIconSize, color),
        mirrored_icon.x(), mirrored_icon.y());
    if (expanded) {
      canvas->DrawStringRectWithFlags(
          u"New Tab", gfx::FontList(), color,
          Mirrored(this, gfx::Rect(kNewTabLabelX, 0, width() - kNewTabLabelX,
                                   height())),
          LeadingTextAlignment());
    }
  }
};

BEGIN_METADATA(KoanNewTabRow)
END_METADATA

// Zen's "Clear" button, at the trailing end of the separator. Until the
// pointer is over the sidebar (or the button has focus) the separator's line
// runs through it.
class KoanClearButton : public views::Button {
  METADATA_HEADER(KoanClearButton, views::Button)

 public:
  KoanClearButton(base::RepeatingClosure clear,
                  base::RepeatingCallback<std::u16string()> tooltip)
      : views::Button(std::move(clear)),
        tooltip_(std::move(tooltip)),
        font_(FontOfSize(kClearFontSize, gfx::Font::Weight::MEDIUM)) {
    GetViewAccessibility().SetName(u"Clear");
    GetViewAccessibility().SetDescription(u"Close all unpinned tabs");
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  SidebarRowCornerRadius());
  }

  void SetSidebarHovered(bool hovered) {
    if (sidebar_hovered_ != hovered) {
      sidebar_hovered_ = hovered;
      SchedulePaint();
    }
  }

  // Zen shows the arrow only when Clear closes every unpinned tab.
  void SetShowIcon(bool show) {
    if (show_icon_ != show) {
      show_icon_ = show;
      PreferredSizeChanged();
    }
  }

  // Whether the button shows "Clear" rather than the line.
  bool GetRevealed() const {
    return sidebar_hovered_ || HasFocus() || GetState() == STATE_HOVERED ||
           GetState() == STATE_PRESSED;
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    int width = kClearPadding + gfx::GetStringWidth(u"Clear", font_);
    if (show_icon_) {
      width += kClearIconSize + kClearIconGap;
    }
    return gfx::Size(width, kSeparatorRowHeight);
  }

  // The shortcut in it follows the user's binding.
  std::u16string GetRenderedTooltipText(
      const gfx::Point& point) const override {
    return tooltip_.Run();
  }

  void OnFocus() override {
    views::Button::OnFocus();
    SchedulePaint();
  }

  void OnBlur() override {
    views::Button::OnBlur();
    SchedulePaint();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (!GetRevealed()) {
      PaintSeparatorLine(canvas, this, 0, width() - kSeparatorInset);
      return;
    }
    const bool hovered =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    const SkColor color = GetColorProvider()->GetColor(
        hovered ? kColorKoanSidebarClearForegroundHovered
                : kColorKoanSidebarClearForeground);
    int x = kClearPadding;
    if (show_icon_) {
      // Pressing nudges the arrow down, like Zen's.
      const gfx::Rect icon = Mirrored(
          this,
          gfx::Rect(x,
                    (height() - kClearIconSize) / 2 +
                        (GetState() == STATE_PRESSED ? kClearPressedOffset : 0),
                    kClearIconSize, kClearIconSize));
      canvas->DrawImageInt(
          gfx::CreateVectorIcon(kArrowDownwardIcon, kClearIconSize, color),
          icon.x(), icon.y());
      x += kClearIconSize + kClearIconGap;
    }
    canvas->DrawStringRectWithFlags(
        u"Clear", font_, color,
        Mirrored(this, gfx::Rect(x, 0, width() - x, height())),
        LeadingTextAlignment());
  }

 private:
  const base::RepeatingCallback<std::u16string()> tooltip_;
  const gfx::FontList font_;
  bool sidebar_hovered_ = false;
  bool show_icon_ = true;
};

BEGIN_METADATA(KoanClearButton)
ADD_READONLY_PROPERTY_METADATA(bool, Revealed)
END_METADATA

// Zen's separator between the pinned and the unpinned tabs: a line with the
// Clear button (closes the unpinned tabs) at its end.
class KoanClearTabsRow : public views::View {
  METADATA_HEADER(KoanClearTabsRow, views::View)

 public:
  KoanClearTabsRow(base::RepeatingClosure clear,
                   base::RepeatingCallback<std::u16string()> tooltip) {
    button_ = AddChildView(std::make_unique<KoanClearButton>(
        std::move(clear), std::move(tooltip)));
  }

  KoanClearButton* button() { return button_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, kSeparatorRowHeight);
  }

  void Layout(PassKey) override {
    const int button_width = button_->GetPreferredSize().width();
    button_->SetBounds(width() - kSeparatorRowPadding - button_width, 0,
                       button_width, height());
  }

  void ChildPreferredSizeChanged(views::View* child) override {
    InvalidateLayout();
  }

  void ChildVisibilityChanged(views::View* child) override { SchedulePaint(); }

  void OnPaint(gfx::Canvas* canvas) override {
    // Without the button the line runs to the end, short by the inset.
    const int start = kSeparatorRowPadding + kSeparatorInset;
    const int end = button_->GetVisible() ? button_->x() : width() - start;
    PaintSeparatorLine(canvas, this, start, end - start);
  }

 private:
  raw_ptr<KoanClearButton> button_ = nullptr;
};

BEGIN_METADATA(KoanClearTabsRow)
END_METADATA

// The rows Koan puts between the pinned and the unpinned tabs of the vertical
// tab strip, laid out by PlaceSidebarTabRows.
class KoanSidebarTabRowsView : public views::View {
  METADATA_HEADER(KoanSidebarTabRowsView, views::View)

 public:
  KoanSidebarTabRowsView(base::RepeatingClosure open_new_tab,
                         base::RepeatingClosure clear,
                         base::RepeatingCallback<std::u16string()> tooltip) {
    clear_row_ = AddChildView(std::make_unique<KoanClearTabsRow>(
        std::move(clear), std::move(tooltip)));
    AddChildView(std::make_unique<KoanNewTabRow>(std::move(open_new_tab)));
  }

  KoanClearTabsRow* clear_row() { return clear_row_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    int height = 0;
    for (const views::View* row : children()) {
      if (row->GetVisible()) {
        height += row->GetPreferredSize().height();
      }
    }
    return gfx::Size(0, height);
  }

  void Layout(PassKey) override {
    // Inset like the tabs.
    const int inset = SidebarStripInset();
    int y = 0;
    for (views::View* row : children()) {
      if (!row->GetVisible()) {
        continue;
      }
      const int height = row->GetPreferredSize().height();
      row->SetBounds(inset, y, std::max(0, width() - 2 * inset), height);
      y += height;
    }
  }

  void ChildVisibilityChanged(views::View* child) override {
    PreferredSizeChanged();
  }

 private:
  raw_ptr<KoanClearTabsRow> clear_row_ = nullptr;
};

BEGIN_METADATA(KoanSidebarTabRowsView)
END_METADATA

int GetSidebarTabRowsHeight(const TabStripView* tab_strip_view) {
  for (const views::View* child : tab_strip_view->children()) {
    if (IsSidebarTabRowsView(child)) {
      return child->GetPreferredSize().height();
    }
  }
  return 0;
}

int PlaceSidebarTabRows(const TabStripView* tab_strip_view,
                        views::ProposedLayout& layout,
                        int width,
                        int y) {
  for (views::View* child : tab_strip_view->children()) {
    if (IsSidebarTabRowsView(child)) {
      const int height = child->GetPreferredSize().height();
      layout.child_layouts.emplace_back(child, child->GetVisible(),
                                        gfx::Rect(0, y, width, height));
      return height;
    }
  }
  return 0;
}

bool IsSidebarTabRowsView(const views::View* view) {
  return views::IsViewClass<KoanSidebarTabRowsView>(view);
}

bool PaintSidebarTab(gfx::Canvas* canvas,
                     const views::View& tab,
                     bool pinned,
                     bool selected,
                     double hover,
                     float corner_radius) {
  const ui::ColorProvider* provider = tab.GetColorProvider();
  if (!features::IsSidebarEnabled() || !provider) {
    return false;
  }
  if (pinned) {
    PaintEssentialsTile(canvas, tab, selected, hover, corner_radius);
    return true;
  }
  SkColor color = SK_ColorTRANSPARENT;
  if (selected) {
    color = provider->GetColor(kColorKoanSidebarTabSelected);
  } else if (hover > 0) {
    const SkColor hovered = provider->GetColor(kColorKoanSidebarTabHovered);
    color = SkColorSetA(hovered,
                        static_cast<SkAlpha>(SkColorGetA(hovered) * hover));
  }
  if (SkColorGetA(color)) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color);
    canvas->DrawRoundRect(gfx::RectF(tab.GetLocalBounds()), corner_radius,
                          flags);
  }
  return true;
}

int SidebarTabIconGap(int upstream_value) {
  return features::IsSidebarEnabled() ? kSidebarTabIconGap : upstream_value;
}

int SidebarMinWidth(int upstream_value) {
  return features::IsSidebarEnabled() ? kSidebarMinWidth : upstream_value;
}

int SidebarMaxWidth(int upstream_value) {
  return features::IsSidebarEnabled() ? kSidebarMaxWidth : upstream_value;
}

int SidebarCollapseSnapWidth(int upstream_value) {
  // Halfway between the collapsed and the narrowest expanded width, as
  // upstream does with its own minimum.
  return features::IsSidebarEnabled()
             ? (kSidebarMinWidth + tabs::kVerticalTabStripCollapsedWidth) / 2
             : upstream_value;
}

KoanSidebarTabsController::KoanSidebarTabsController(BrowserView* browser_view)
    : browser_view_(browser_view) {}

KoanSidebarTabsController::~KoanSidebarTabsController() = default;

void KoanSidebarTabsController::OnAddedToWidget() {
  if (started_ || !features::IsSidebarEnabled()) {
    return;
  }
  started_ = true;
  browser_view_->browser()->GetTabStripModel()->AddObserver(this);
  if (auto* state = tabs::VerticalTabStripStateController::From(
          browser_view_->browser())) {
    mode_subscription_ = state->RegisterOnModeChanged(base::BindRepeating(
        &KoanSidebarTabsController::OnModeChanged, base::Unretained(this)));
    collapse_subscription_ = state->RegisterOnCollapseChanged(
        base::BindRepeating(&KoanSidebarTabsController::OnCollapseChanged,
                            base::Unretained(this)));
  }
  InstallRows();
}

void KoanSidebarTabsController::OnBrowserViewDestroying() {
  mode_subscription_ = {};
  collapse_subscription_ = {};
  if (started_) {
    browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
    started_ = false;
  }
  if (region_) {
    region_->RemovePreTargetHandler(this);
    region_ = nullptr;
  }
  if (rows_) {
    rows_observation_.Reset();
    rows_->parent()->RemoveChildViewT(std::exchange(rows_, nullptr));
  }
}

void KoanSidebarTabsController::OnModeChanged(
    tabs::VerticalTabStripStateController* state) {
  // BrowserView rebuilds the tab strip in its own callback for this change;
  // add the rows once it has.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanSidebarTabsController::InstallRows,
                                weak_factory_.GetWeakPtr()));
}

void KoanSidebarTabsController::OnCollapseChanged(
    tabs::VerticalTabStripCollapseState state) {
  UpdateClearRow();
}

void KoanSidebarTabsController::InstallRows() {
  if (rows_ || !started_) {
    return;
  }
  TabStripRegionView* region = browser_view_->tab_strip_view();
  if (!region || !views::IsViewClass<VerticalTabStripRegionView>(region)) {
    return;
  }
  auto* strip = views::AsViewClass<TabStripView>(region->GetTabStripView());
  if (!strip) {
    return;
  }
  // After the pinned tabs and their separator, so focus moves through them
  // in order.
  const size_t index = strip->GetIndexOf(strip->GetTabsSeparator())
                           .value_or(strip->children().size() - 1) +
                       1;
  rows_ = strip->AddChildViewAt(
      std::make_unique<KoanSidebarTabRowsView>(
          base::BindRepeating(&KoanSidebarTabsController::OpenNewTab,
                              base::Unretained(this)),
          base::BindRepeating(&KoanSidebarTabsController::ClearUnpinnedTabs,
                              base::Unretained(this)),
          base::BindRepeating(&KoanSidebarTabsController::GetClearTooltip,
                              base::Unretained(this))),
      index);
  // The rows go with the tab strip, which upstream rebuilds when the window
  // switches between vertical and horizontal tabs.
  rows_observation_.Observe(rows_);
  UpdateClearRow();

  // The region outlives its tab strip. It notifies itself (not its children)
  // when the pointer enters or leaves it; Zen reveals Clear while it is
  // inside.
  if (!region_) {
    region_ = region;
    region_->AddPreTargetHandler(this);
  }

  for (views::View* child : strip->parent()->children()) {
    // Zen has no line under the URL bar; the separator above the tabs is
    // the Clear row's.
    if (child->GetClassName() == "FrameSeparator") {
      child->SetVisible(false);
    }
    // Zen has no "+" button below the tabs: the New Tab row takes its place.
    // Keep its container: it is the flexible spacer that pushes the
    // workspace switcher to the bottom of the sidebar.
    if (child->GetClassName() != "VerticalTabStripBottomContainer") {
      continue;
    }
    for (views::View* button : child->children()) {
      if (button->GetClassName() == "NewTabButton") {
        button->SetVisible(false);
      }
    }
  }
}

void KoanSidebarTabsController::UpdateClearRow() {
  if (!rows_) {
    return;
  }
  const TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  const std::vector<int> unpinned =
      KoanShortcutController::UnpinnedTabsForClear(model);
  const size_t closable =
      static_cast<size_t>(std::ranges::count_if(unpinned, [model](int index) {
        return KoanShortcutController::IsClosableByClear(model, index);
      }));
  // The line separates the pinned from the listed unpinned tabs: in the "all
  // tabs" view those include the workspaces' tabs, which Clear leaves alone,
  // so it may have nothing to clear.
  const std::optional<tab_groups::TabGroupId> workspace =
      model->GetFocusedGroup();
  bool listed = false;
  for (int i = 0; i < model->count() && !listed; ++i) {
    listed = !model->IsTabPinned(i) &&
             (!workspace || model->GetTabGroupForTab(i) == workspace);
  }
  const auto* state =
      tabs::VerticalTabStripStateController::From(browser_view_->browser());
  // In the collapsed sidebar upstream's own separator is enough.
  rows_->clear_row()->SetVisible(listed && !(state && state->IsCollapsed()));
  rows_->clear_row()->button()->SetVisible(!unpinned.empty());
  // Zen's arrow: Clear closes every unpinned tab, either because all can be
  // closed or because none can (then it closes them all).
  rows_->clear_row()->button()->SetShowIcon(closable == unpinned.size() ||
                                            closable == 0);
}

void KoanSidebarTabsController::OpenNewTab() {
  // Zen's replace-newtab: the URL bar instead of a tab.
  if (KoanBrowserViewController* controller =
          KoanBrowserViewController::From(browser_view_);
      controller && controller->url_bar()->OpenForNewTab()) {
    return;
  }
  // With the New Tab row on top, Zen puts the tab at the top of the space.
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  const std::optional<tab_groups::TabGroupId> workspace =
      model->GetFocusedGroup();
  int index = model->IndexOfFirstNonPinnedTab();
  if (workspace && model->group_model()->ContainsTabGroup(*workspace)) {
    index = static_cast<int>(
        model->group_model()->GetTabGroup(*workspace)->ListTabs().start());
  }
  chrome::AddTabAt(browser_view_->browser(), GURL(), index,
                   /*foreground=*/true, workspace);
}

std::u16string KoanSidebarTabsController::GetClearTooltip() const {
  std::u16string tooltip = u"Close all unpinned tabs";
  if (KoanBrowserViewController* controller =
          KoanBrowserViewController::From(browser_view_)) {
    if (const std::optional<ui::Accelerator> accelerator =
            controller->shortcuts()->GetAccelerator(
                shortcuts::Action::kCloseUnpinnedTabs)) {
      tooltip += u" (" + accelerator->GetShortcutText() + u")";
    }
  }
  return tooltip;
}

void KoanSidebarTabsController::ClearUnpinnedTabs() {
  if (KoanBrowserViewController* controller =
          KoanBrowserViewController::From(browser_view_)) {
    controller->shortcuts()->CloseUnpinnedTabs();
  }
}

void KoanSidebarTabsController::ShowTabsClosedToast() {
  if (!features::IsSidebarEnabled()) {
    return;
  }
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  if (!controller) {
    return;
  }
  const std::u16string undo = ShortcutText(ui::VKEY_T, ui::EF_SHIFT_DOWN);
  controller->toast()->Show(u"Tabs Closed! Use", undo, u"to undo.",
                            u"Tabs closed. Use " + undo + u" to undo.");
}

void KoanSidebarTabsController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  UpdateClearRow();
}

void KoanSidebarTabsController::OnTabGroupFocusChanged(
    std::optional<tab_groups::TabGroupId> new_focused_group_id,
    std::optional<tab_groups::TabGroupId> old_focused_group_id) {
  UpdateClearRow();
}

void KoanSidebarTabsController::OnTabChangedAt(tabs::TabInterface* tab,
                                               TabChangeType change_type) {
  // A tab starting or stopping to play sound changes what Clear keeps.
  if (change_type == TabChangeType::kAll) {
    UpdateClearRow();
  }
}

void KoanSidebarTabsController::OnTabPinnedStateChanged(tabs::TabInterface* tab,
                                                        int index) {
  UpdateClearRow();
}

void KoanSidebarTabsController::OnMouseEvent(ui::MouseEvent* event) {
  if (!rows_ || event->target() != region_.get()) {
    return;
  }
  if (event->type() == ui::EventType::kMouseEntered) {
    rows_->clear_row()->button()->SetSidebarHovered(true);
  } else if (event->type() == ui::EventType::kMouseExited) {
    rows_->clear_row()->button()->SetSidebarHovered(false);
  }
}

void KoanSidebarTabsController::OnViewIsDeleting(views::View* observed_view) {
  // The tab strip, and the rows with it, is going away.
  rows_observation_.Reset();
  rows_ = nullptr;
}

}  // namespace koan
