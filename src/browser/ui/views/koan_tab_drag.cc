// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_tab_drag.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_data.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/pinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/common/split_tab_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_drag_handler.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/common/unpinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_context.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h"
#include "chrome/browser/ui/views/tabs/tab/tab_icon.h"
#include "chrome/browser/ui/views/tabs/tab/tab_title.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_group.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_content_card.h"
#include "koan/browser/ui/views/koan_essentials_promo.h"
#include "koan/browser/ui/views/koan_folder_controller.h"
#include "koan/browser/ui/views/koan_folder_views.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "koan/browser/ui/views/koan_haptics.h"
#include "koan/browser/ui/views/koan_pinned.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_sidebar_animation.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_sidebar_tabs.h"
#include "koan/browser/ui/views/koan_space_bar.h"
#include "koan/browser/ui/views/koan_space_drag_logic.h"
#include "koan/browser/ui/views/koan_space_header.h"
#include "koan/browser/ui/views/koan_space_navigation.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_split_view.h"
#include "koan/browser/ui/views/koan_tab_drag_hooks.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/list_selection_model.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/range/range.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/event_monitor.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// Zen's failsafe: a dropped tab hidden for its landing shows again after
// this, landed or not.
constexpr base::TimeDelta kLandingFailsafe = base::Milliseconds(1000);

// Half the gap between two Essentials tiles (Zen's 4px), where the line goes
// between them.
constexpr int kTileGapHalf = 2;

// #zen-dragover-background's margin above and below the row it lights.
constexpr int kDragOverMargin = 2;

// --border-radius-medium, the drag image's ([drag-image]).
constexpr int kDragImageRadius = 8;

gfx::Point InView(const views::View* from,
                  const views::View* to,
                  const gfx::Point& point) {
  gfx::Point converted = point;
  views::View::ConvertPointToTarget(from, to, &converted);
  return converted;
}

// A drop place in the Essentials grid: a tile (a split view's two tabs as
// one), in the region's coordinates.
struct DropUnit {
  tabs::TabHandle first;
  gfx::Rect bounds;
};

std::vector<DropUnit> DropUnits(TabStripModel* model,
                                TabDragContext* context,
                                const views::View* region,
                                const gfx::Range& indices) {
  std::vector<DropUnit> units;
  std::optional<split_tabs::SplitTabId> last_split;
  for (size_t index = indices.start(); index < indices.end(); ++index) {
    tabs::TabInterface* tab = model->GetTabAtIndex(static_cast<int>(index));
    views::View* view =
        tab ? context->GetTabForContents(tab->GetContents()) : nullptr;
    if (!view || !view->IsDrawn()) {
      continue;
    }
    const gfx::Rect bounds =
        views::View::ConvertRectToTarget(view, region, view->GetLocalBounds());
    if (tab->GetSplit() && tab->GetSplit() == last_split && !units.empty()) {
      units.back().bounds.Union(bounds);
      continue;
    }
    last_split = tab->GetSplit();
    units.push_back({tab->GetHandle(), bounds});
  }
  return units;
}

std::vector<gfx::Rect> UnitBounds(const std::vector<DropUnit>& units) {
  std::vector<gfx::Rect> bounds;
  for (const DropUnit& unit : units) {
    bounds.push_back(unit.bounds);
  }
  return bounds;
}

// The tab whose row `target` (a view in `region`) is in, unless it is a
// button of the row (its close button, the reset button): those keep their
// presses, as upstream's drag doesn't start from them either.
TabView* PressedTabView(views::View* region, views::View* target) {
  for (views::View* view = target; view && view != region;
       view = view->parent()) {
    if (auto* tab_view = views::AsViewClass<TabView>(view)) {
      return tab_view;
    }
    if (views::Button::AsButton(view)) {
      return nullptr;
    }
  }
  return nullptr;
}

// The folder's row `target` (a view in `region`) is in, unless it is being
// renamed or the press is on another button inside it.
KoanFolderRowView* PressedFolderRow(views::View* region, views::View* target) {
  for (views::View* view = target; view && view != region;
       view = view->parent()) {
    if (auto* row = views::AsViewClass<KoanFolderRowView>(view)) {
      return row->GetRenaming() || row->removing() ? nullptr : row;
    }
    if (views::Button::AsButton(view)) {
      return nullptr;
    }
  }
  return nullptr;
}

// What the row (or tile) `row_view` looks like, for the drag image: the icon
// and title of each tab it shows (a split view's two).
KoanTabDragImage::Row ImageRow(BrowserView* browser_view,
                               views::View* row_view) {
  KoanTabDragImage::Row row;
  row.size = row_view->size();
  row.corner_radius =
      static_cast<float>(look::StyleRadius(kDragImageRadius, look::Step::kMd));
  std::vector<TabView*> tab_views;
  if (auto* tab_view = views::AsViewClass<TabView>(row_view)) {
    tab_views.push_back(tab_view);
  } else {
    for (views::View* child : row_view->children()) {
      if (auto* half = views::AsViewClass<TabView>(child);
          half && half->GetVisible()) {
        tab_views.push_back(half);
      }
    }
  }
  TabStripRegionView* strip = browser_view->tab_strip_view();
  for (TabView* tab_view : tab_views) {
    KoanTabDragImage::Label label;
    for (views::View* child : tab_view->children()) {
      if (!child->GetVisible()) {
        continue;
      }
      if (views::IsViewClass<TabIcon>(child)) {
        const gfx::Rect icon = views::View::ConvertRectToTarget(
            child, row_view, child->GetLocalBounds());
        label.icon_bounds =
            gfx::Rect(icon.CenterPoint().x() - kPinnedIconSize / 2,
                      icon.CenterPoint().y() - kPinnedIconSize / 2,
                      kPinnedIconSize, kPinnedIconSize);
      } else if (auto* title = views::AsViewClass<TabTitle>(child)) {
        label.title = std::u16string(title->GetText());
        label.font = title->font_list();
        label.title_bounds = views::View::ConvertRectToTarget(
            child, row_view, child->GetLocalBounds());
      }
    }
    if (const tabs::TabInterface* tab = tab_view->GetTabInterface();
        tab && strip) {
      label.icon = strip->GetTabData(tab->GetHandle()).favicon;
    }
    row.labels.push_back(std::move(label));
  }
  return row;
}

}  // namespace

bool IsUpstreamTabDragRunning() {
  if (!TabDragController::IsActive()) {
    return false;
  }
  TabDragContext* source = TabDragController::GetSourceContext();
  const TabDragController* controller =
      source ? source->GetDragController() : nullptr;
  // (A drag that moved on to another window's strip is owned there.)
  return !controller || controller->started_drag();
}

bool KeepsTabDragInWindow(views::Widget* widget) {
  if (!features::IsSidebarEnabled() || !widget ||
      !widget->GetTopLevelWidget()) {
    return false;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForNativeWindow(
      widget->GetTopLevelWidget()->GetNativeWindow());
  return browser_view && browser_view->ShouldDrawVerticalTabStrip();
}

// Holds the mouse capture during Koan's drag, so the drag gets the pointer's
// moves and its release wherever the pointer goes (and whatever happens to
// the pressed row: a Space switch hides it). An empty child of the sidebar's
// region, left out of its layout.
class KoanTabDragCaptureView : public views::View {
  METADATA_HEADER(KoanTabDragCaptureView, views::View)

 public:
  explicit KoanTabDragCaptureView(KoanTabDrag* drag) : drag_(drag) {
    SetProperty(views::kViewIgnoredByLayoutKey, true);
    GetViewAccessibility().SetIsIgnored(true);
  }
  KoanTabDragCaptureView(const KoanTabDragCaptureView&) = delete;
  KoanTabDragCaptureView& operator=(const KoanTabDragCaptureView&) = delete;
  ~KoanTabDragCaptureView() override = default;

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override { return true; }
  bool OnMouseDragged(const ui::MouseEvent& event) override {
    drag_->OnCaptureDragged(
        views::View::ConvertPointToScreen(this, event.location()), event);
    return true;
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
    drag_->OnCaptureReleased();
  }
  void OnMouseCaptureLost() override { drag_->OnCaptureLost(); }

 private:
  const raw_ptr<KoanTabDrag> drag_;
};

BEGIN_METADATA(KoanTabDragCaptureView)
END_METADATA

KoanTabDrag::KoanTabDrag(KoanWorkspaceController* controller,
                         BrowserView* browser_view)
    : controller_(controller),
      browser_view_(browser_view),
      image_(browser_view) {}

KoanTabDrag::~KoanTabDrag() = default;

void KoanTabDrag::OnRegionMouseEvent(views::View* region,
                                     ui::MouseEvent* event) {
  if (event->target() == capture_.view()) {
    return;
  }
  switch (event->type()) {
    case ui::EventType::kMousePressed: {
      if (state_ == State::kDragging) {
        return;
      }
      state_ = State::kIdle;
      pressed_.SetView(nullptr);
      if (!event->IsOnlyLeftMouseButton() || !features::IsSidebarEnabled()) {
        return;
      }
      auto* target = static_cast<views::View*>(event->target());
      views::View* pressed = PressedTabView(region, target);
      if (!pressed ||
          !views::AsViewClass<TabView>(pressed)->GetTabInterface()) {
        // A folder's row drags its folder.
        pressed = folders() ? PressedFolderRow(region, target) : nullptr;
      }
      if (!pressed) {
        return;
      }
      region_.SetView(region);
      pressed_.SetView(pressed);
      press_point_ = views::View::ConvertPointToScreen(
          static_cast<views::View*>(event->target()), event->location());
      // (Before the press selects the tab: upstream's handler comes after.)
      tabs::TabInterface* shown =
          controller_->tab_strip_model()->GetActiveTab();
      shown_before_press_ = shown ? shown->GetHandle() : tabs::TabHandle();
      state_ = State::kPressed;
      return;
    }
    case ui::EventType::kMouseDragged: {
      if (state_ != State::kPressed) {
        return;
      }
      // Upstream's drag waits on a tab's press (TabView::OnMousePressed()
      // starts one only for a press that drags): its moves are Koan's from
      // here. A folder's row has none.
      auto* folder_row = views::AsViewClass<KoanFolderRowView>(pressed_.view());
      if (!pressed_.view() || (!folder_row && !GetWaitingUpstreamDrag())) {
        state_ = State::kIdle;
        return;
      }
      event->StopPropagation();
      const gfx::Point point = views::View::ConvertPointToScreen(
          static_cast<views::View*>(event->target()), event->location());
      if (!views::View::ExceededDragThreshold(point - press_point_)) {
        return;
      }
      if (folder_row ? !StartFolder(folder_row, /*for_testing=*/false)
                     : !Start(pressed_.view(), /*for_testing=*/false)) {
        state_ = State::kIdle;
        return;
      }
      DragTo(point, nullptr);
      return;
    }
    case ui::EventType::kMouseReleased:
      if (state_ == State::kPressed) {
        state_ = State::kIdle;
      }
      return;
    default:
      return;
  }
}

void KoanTabDrag::Reset() {
  // The window's views go: nothing moves, nothing lands.
  EndPaneDrag();
  StopFeedback();
  dragged_folder_ = 0;
  folder_closed_by_drag_ = false;
  image_.Hide();
  ShowHiddenViews();
  landing_failsafe_.Stop();
  landing_ = false;
  hold_.reset();
  state_ = State::kIdle;
  pressed_.SetView(nullptr);
  capture_.SetView(nullptr);
  region_.SetView(nullptr);
}

bool KoanTabDrag::IsDragging() const {
  return state_ == State::kDragging || landing_;
}

void KoanTabDrag::OnEvent(const ui::Event& event) {
  if (state_ != State::kDragging) {
    return;
  }
  // Esc cancels (Zen's mozUserCancelled). A release the capture didn't get
  // (the sidebar went from under it) can't drop anywhere either. Not from
  // inside the monitor's dispatch, which the cancel ends.
  const bool escape =
      event.IsKeyEvent() && event.AsKeyEvent()->key_code() == ui::VKEY_ESCAPE;
  const bool lost_release = event.type() == ui::EventType::kMouseReleased &&
                            (!capture_.view() || !capture_.view()->IsDrawn());
  if (escape || lost_release) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&KoanTabDrag::Cancel, weak_factory_.GetWeakPtr()));
  }
}

bool KoanTabDrag::StartForTesting(tabs::TabInterface* tab) {
  views::View* region = browser_view_->tab_strip_view();
  views::View* view = RowViewFor(tab);
  if (!region || !view) {
    return false;
  }
  region_.SetView(region);
  press_point_ = view->GetBoundsInScreen().CenterPoint();
  return Start(view, /*for_testing=*/true);
}

bool KoanTabDrag::StartFolderForTesting(int id) {
  views::View* region = browser_view_->tab_strip_view();
  KoanFolderRowView* row = FolderRowFor(id);
  if (!region || !row) {
    return false;
  }
  region_.SetView(region);
  press_point_ = row->GetBoundsInScreen().CenterPoint();
  return StartFolder(row, /*for_testing=*/true);
}

void KoanTabDrag::SwitchSpaceForTesting(const tab_groups::TabGroupId& space) {
  SwitchSpace(space);
}

void KoanTabDrag::CancelForTesting() {
  Cancel();
}

void KoanTabDrag::OnCaptureDragged(const gfx::Point& screen_point,
                                   const ui::MouseEvent& event) {
  if (state_ == State::kDragging) {
    DragTo(screen_point, &event);
  }
}

void KoanTabDrag::OnCaptureReleased() {
  if (state_ == State::kDragging) {
    Drop();
  }
}

void KoanTabDrag::OnCaptureLost() {
  if (state_ == State::kDragging) {
    Cancel();
  }
}

bool KoanTabDrag::Start(views::View* pressed, bool for_testing) {
  views::View* region = this->region();
  views::Widget* widget = region ? region->GetWidget() : nullptr;
  TabStripModel* model = controller_->tab_strip_model();
  const tabs::TabInterface* pressed_tab =
      views::IsViewClass<TabView>(pressed)
          ? views::AsViewClass<TabView>(pressed)->GetTabInterface()
          : nullptr;
  if (!pressed_tab && views::IsViewClass<SplitTabView>(pressed) &&
      !pressed->children().empty()) {
    if (auto* first =
            views::AsViewClass<TabView>(pressed->children().front())) {
      pressed_tab = first->GetTabInterface();
    }
  }
  const int source_index =
      pressed_tab ? model->GetIndexOfTab(pressed_tab) : TabStripModel::kNoTab;
  if (!widget || source_index == TabStripModel::kNoTab) {
    return false;
  }
  tabs::TabInterface* source = model->GetTabAtIndex(source_index);
  // The selection, as upstream's drag takes it: the tabs that are Essentials
  // or not like the pressed one.
  dragged_.clear();
  dragged_folder_ = 0;
  const ui::ListSelectionModel selection =
      model->selection_model().GetListSelectionModel();
  for (size_t index : selection.selected_indices()) {
    tabs::TabInterface* tab = model->GetTabAtIndex(static_cast<int>(index));
    if (tab && tab->IsPinned() == source->IsPinned()) {
      dragged_.push_back(tab->GetHandle());
    }
  }
  if (!std::ranges::contains(dragged_, source->GetHandle())) {
    dragged_.push_back(source->GetHandle());
  }
  // A tab of a split view takes its whole split along, one row (Zen's
  // elementToMove() moves the split's group).
  for (size_t i = 0, count = dragged_.size(); i < count; ++i) {
    tabs::TabInterface* tab = dragged_[i].Get();
    if (!tab || !tab->GetSplit()) {
      continue;
    }
    for (tabs::TabInterface* other :
         model->GetSplitData(*tab->GetSplit())->ListTabs()) {
      if (!std::ranges::contains(dragged_, other->GetHandle())) {
        dragged_.push_back(other->GetHandle());
      }
    }
  }
  source_ = source->GetHandle();
  RememberOrigin();
  if (!TakeCapture(widget, for_testing)) {
    return false;
  }

  // The image: the pressed tab's row, then the others' behind it (a split
  // view's row with both its tabs).
  const std::vector<tabs::TabInterface*> tabs = DraggedTabs();
  std::vector<tabs::TabInterface*> order = {source};
  for (tabs::TabInterface* tab : tabs) {
    if (tab != source &&
        (!tab->GetSplit() || tab->GetSplit() != source->GetSplit())) {
      if (std::ranges::none_of(order, [tab](const tabs::TabInterface* other) {
            return other->GetSplit() && other->GetSplit() == tab->GetSplit();
          })) {
        order.push_back(tab);
      }
    }
  }
  std::vector<KoanTabDragImage::Row> rows;
  TabDragContext* context = GetDragContext();
  views::View* source_row = RowViewFor(source);
  // The tile the image becomes over the Essentials, and the row an Essential
  // becomes out of them (Zen sizes it like the first normal tab): a Space
  // row's look, with the Essential's icon and title.
  views::View* first_tile = model->IndexOfFirstNonPinnedTab() > 0
                                ? RowViewFor(model->GetTabAtIndex(0))
                                : nullptr;
  tile_size_ = EssentialDragTileSize(
      first_tile ? std::make_optional(first_tile->size()) : std::nullopt,
      region->width(), source_row ? source_row->height() : 0);
  std::optional<KoanTabDragImage::Row> normal_row;
  if (source->IsPinned()) {
    std::vector<tabs::TabHandle> firsts;
    const std::vector<SpaceListRow> space_rows = SpaceRows(firsts);
    for (size_t i = 0; i < space_rows.size() && !normal_row; ++i) {
      tabs::TabInterface* tab = firsts[i].Get();
      views::View* view = tab && !tab->IsSplit() && context
                              ? context->GetTabForContents(tab->GetContents())
                              : nullptr;
      if (view) {
        normal_row = ImageRow(browser_view_, view);
      }
    }
  }
  for (tabs::TabInterface* tab : order) {
    views::View* row_view = RowViewFor(tab);
    if (!row_view || rows.size() >= kDragImageMaxRows) {
      continue;
    }
    KoanTabDragImage::Row row = ImageRow(browser_view_, row_view);
    if (tab->IsPinned() && normal_row && !normal_row->labels.empty()) {
      // Its tile shows no title: the row's place for it, with its own.
      KoanTabDragImage::Label label = normal_row->labels.front();
      label.icon =
          row.labels.empty() ? ui::ImageModel() : row.labels.front().icon;
      if (TabStripRegionView* strip = browser_view_->tab_strip_view()) {
        label.title = strip->GetTabData(tab->GetHandle()).title;
      }
      row = *normal_row;
      row.labels = {std::move(label)};
    }
    rows.push_back(std::move(row));
  }
  // Held where it was grabbed (Zen's offset from the row's top-left).
  const gfx::Vector2d grab =
      source_row ? press_point_ - source_row->GetBoundsInScreen().origin()
                 : gfx::Vector2d();
  grab_ = DragImageGrab(order.size(), grab);
  image_.Show(std::move(rows), order.size(), grab_, press_point_);

  // Zen hides a dragged Essential's tile; a row stays.
  if (source->IsPinned()) {
    for (tabs::TabInterface* tab : tabs) {
      HideView(RowViewFor(tab));
    }
  }
  BeginDragging(widget);
  return true;
}

bool KoanTabDrag::StartFolder(KoanFolderRowView* row, bool for_testing) {
  views::View* region = this->region();
  views::Widget* widget = region ? region->GetWidget() : nullptr;
  KoanFolderController* folders = this->folders();
  const KoanFolder* folder =
      folders && row ? folders->GetFolder(row->folder_id()) : nullptr;
  if (!widget || !folder) {
    return false;
  }
  dragged_.clear();
  dragged_folder_ = folder->id;
  source_ = tabs::TabHandle();
  RememberOrigin();
  if (!TakeCapture(widget, for_testing)) {
    dragged_folder_ = 0;
    return false;
  }
  // The pressed row gets no release (the capture has the pointer).
  row->SetState(views::Button::STATE_NORMAL);
  KoanTabDragImage::Row image_row;
  image_row.size = row->size();
  image_row.corner_radius =
      static_cast<float>(look::StyleRadius(kDragImageRadius, look::Step::kMd));
  image_row.labels = {row->GetDragLabel()};
  grab_ = DragImageGrab(1, press_point_ - row->GetBoundsInScreen().origin());
  image_.Show({std::move(image_row)}, 1, grab_, press_point_);
  // Zen closes an open folder for the drag (collapsedByDrag), its rows
  // folding away.
  folder_closed_by_drag_ = !folder->collapsed;
  if (folder_closed_by_drag_) {
    folders->SetCollapsed(folder->id, true);
  }
  BeginDragging(widget);
  return true;
}

void KoanTabDrag::RememberOrigin() {
  TabStripModel* model = controller_->tab_strip_model();
  const ui::ListSelectionModel selection =
      model->selection_model().GetListSelectionModel();
  origin_space_ = controller_->GetActiveSpace();
  origin_active_ = model->GetActiveTab() ? model->GetActiveTab()->GetHandle()
                                         : tabs::TabHandle();
  origin_selection_.clear();
  for (size_t index : selection.selected_indices()) {
    origin_selection_.push_back(
        model->GetTabAtIndex(static_cast<int>(index))->GetHandle());
  }
  origin_anchor_ =
      selection.anchor()
          ? model->GetTabAtIndex(static_cast<int>(*selection.anchor()))
                ->GetHandle()
          : tabs::TabHandle();
}

bool KoanTabDrag::TakeCapture(views::Widget* widget, bool for_testing) {
  if (for_testing) {
    return true;
  }
  views::View* region = this->region();
  views::View* capture = capture_.view();
  if (!capture || capture->parent() != region) {
    capture =
        region->AddChildView(std::make_unique<KoanTabDragCaptureView>(this));
    capture_.SetView(capture);
  }
  widget->SetCapture(capture);
  return widget->HasCapture();
}

void KoanTabDrag::BeginDragging(views::Widget* widget) {
  hold_ = HoldSidebarOpen(browser_view_);
  escape_monitor_ = views::EventMonitor::CreateWindowMonitor(
      this, widget->GetNativeWindow(),
      {ui::EventType::kKeyPressed, ui::EventType::kMouseReleased});
  target_ = DropTarget();
  haptic_skipped_ = false;
  limit_toast_shown_ = false;
  edge_ = 0;
  state_ = State::kDragging;
}

void KoanTabDrag::DragTo(const gfx::Point& screen_point,
                         const ui::MouseEvent* event) {
  image_.MoveTo(screen_point);
  if (event && MaybeHandOff(screen_point, *event)) {
    return;
  }
  views::View* region = this->region();
  if (!region) {
    return;
  }
  gfx::Point point = screen_point;
  views::View::ConvertPointFromScreen(region, &point);
  last_point_ = point;
  const std::vector<KoanWorkspaceController::SpaceInfo> spaces =
      controller_->GetSpaces();
  const bool essentials = DragsEssentials();

  // Over a Space button: that Space, at once (not for Essentials, which
  // every Space shows).
  if (KoanSpaceBarView* bar = controller_->space_bar();
      bar && bar->GetVisible() && !essentials && spaces.size() > 1) {
    if (const std::optional<tab_groups::TabGroupId> space =
            bar->SpaceAt(InView(region, bar, point));
        space && space != controller_->GetActiveSpace()) {
      switch_timer_.Stop();
      edge_ = 0;
      SwitchSpace(*space);
    }
  }

  // Over the Essentials: a tile, else a row (Zen's
  // #makeDragImageEssential() / #makeDragImageNonEssential(), at once).
  {
    KoanPinnedTabController* pinned = pinned_tabs();
    const bool over_grid =
        !IsSidebarCollapsed() && !DragsFolder() && IsOverEssentials(point) &&
        (essentials || (pinned && pinned->CanAddToEssentials()));
    image_.SetTile(over_grid ? tile_size_ : gfx::Size(),
                   essentials ? grab_ : EssentialTileGrab(grab_));
  }
  // Over the page's edge: Zen's split there.
  if (UpdatePageSplit(screen_point)) {
    switch_timer_.Stop();
    edge_ = 0;
    return;
  }
  UpdatePromo(point);
  UpdateTarget(point);

  // Near an edge of an expanded sidebar, away from the Essentials: the
  // previous or next Space after the delay.
  const int edge =
      spaces.size() > 1 && !IsSidebarCollapsed() && !IsOverEssentials(point)
          ? SpaceDragEdge(point.x(), region->width())
          : 0;
  if (edge == 0) {
    switch_timer_.Stop();
    edge_ = 0;
    return;
  }
  if (edge != edge_ || !switch_timer_.IsRunning()) {
    edge_ = edge;
    switch_timer_.Start(FROM_HERE, kSpaceDragSwitchDelay, this,
                        &KoanTabDrag::OnSwitchTimer);
  }
}

void KoanTabDrag::Drop() {
  const DropTarget target = target_;
  const std::vector<tabs::TabInterface*> tabs = DraggedTabs();
  const bool essentials = DragsEssentials();
  StopFeedback();
  ReleaseUpstreamAndCapture();
  if (DragsFolder()) {
    DropFolder(target);
    return;
  }
  KoanPinnedTabController* pinned = pinned_tabs();
  views::View* region = this->region();
  const std::optional<tab_groups::TabGroupId> active =
      controller_->GetActiveSpace();
  if (target.kind == DropTarget::Kind::kNone && !essentials && !tabs.empty() &&
      region && region->GetLocalBounds().Contains(last_point_) && active &&
      tabs.front()->GetGroup() != active) {
    // Elsewhere on the sidebar (a Space button, the bottom bar) after a
    // switch: the tabs join the Space shown, at its end
    // (#handle_dropSwitchSpace()).
    controller_->MoveDraggedTabsToSpace(tabs, *active);
    Land(source_);
    return;
  }
  if (target.kind == DropTarget::Kind::kNone || tabs.empty() || !pinned) {
    // Not on the sidebar (the page, out of the window): back where it was.
    RestoreOrigin();
    Land(source_);
    return;
  }
  if (target.kind == DropTarget::Kind::kSplitTab ||
      target.kind == DropTarget::Kind::kSplitPage) {
    // Zen: a drop into a split takes the image away at once.
    image_.Hide();
    tabs::TabInterface* dragged = tabs.size() == 1 ? tabs.front() : nullptr;
    tabs::TabInterface* other = target.split_with.Get();
    const split_tabs::SplitTabLayout layout =
        target.split_stacked ? split_tabs::SplitTabLayout::kStacked
                             : split_tabs::SplitTabLayout::kSideBySide;
    if (dragged && other == dragged) {
      // The page showed the dragged tab: Zen's createEmptySplit(), the new
      // pane on the side it was dropped on.
      OpenEmptySplit(browser_view_->browser(), layout,
                     split_tabs::SplitTabCreatedSource::kDragAndDropTab);
      TabStripModel* model = controller_->tab_strip_model();
      if (target.split_first && dragged->IsSplit() &&
          model->GetSplitData(*dragged->GetSplit())->ListTabs().front() ==
              dragged) {
        model->ReverseTabsInSplit(*dragged->GetSplit());
      }
    } else {
      SplitByDrag(browser_view_, dragged, other, layout, target.split_first);
    }
    OnLanded();
    return;
  }
  tabs::TabInterface* before = target.before.Get();
  if (target.kind == DropTarget::Kind::kEssentials) {
    pinned->DropInEssentials(tabs, before);
  } else {
    if (essentials) {
      for (tabs::TabInterface* tab : tabs) {
        pinned->DropEssentialInSpace(tab, before, target.pinned);
      }
    } else {
      // Zen: the tabs join the Space shown (and the dragged one shows).
      if (active && tabs.front()->GetGroup() != active) {
        controller_->MoveDraggedTabsToSpace(tabs, *active);
      }
      pinned->DropTabsInSpace(tabs, before, target.pinned);
    }
    if (KoanBrowserViewController* koan =
            KoanBrowserViewController::From(browser_view_);
        koan && koan->folders()) {
      koan->folders()->DropInFolder(tabs, target.pinned ? target.folder : 0);
    }
  }
  Land(source_);
}

void KoanTabDrag::Cancel() {
  if (state_ != State::kDragging) {
    return;
  }
  StopFeedback();
  ReleaseUpstreamAndCapture();
  RestoreOrigin();
  if (DragsFolder()) {
    const int folder = dragged_folder_;
    ReopenFolder();
    Land(tabs::TabHandle(), folder);
    return;
  }
  Land(source_);
}

void KoanTabDrag::DropFolder(const DropTarget& target) {
  const int folder = dragged_folder_;
  KoanFolderController* folders = this->folders();
  const KoanFolder* dragged = folders ? folders->GetFolder(folder) : nullptr;
  views::View* region = this->region();
  const std::optional<tab_groups::TabGroupId> active =
      controller_->GetActiveSpace();
  if (dragged && target.kind == DropTarget::Kind::kFolder) {
    folders->DropFolder(folder, target.folder, target.before_folder);
  } else if (dragged && region &&
             region->GetLocalBounds().Contains(last_point_) && active &&
             dragged->space != active) {
    // Elsewhere on the sidebar after a switch: into the Space shown, as tabs
    // go (#handle_dropSwitchSpace()).
    folders->ChangeSpace(folder, *active);
  } else {
    RestoreOrigin();
  }
  ReopenFolder();
  Land(tabs::TabHandle(), folder);
}

void KoanTabDrag::ReopenFolder() {
  const int folder = std::exchange(dragged_folder_, 0);
  KoanFolderController* folders = this->folders();
  // Zen's _expandGroupOnDrop(): open again, unless it holds the active tab
  // (which a closed folder shows).
  if (std::exchange(folder_closed_by_drag_, false) && folders &&
      folders->GetFolder(folder) && !folders->HasActiveTab(folder)) {
    folders->SetCollapsed(folder, false);
  }
}

void KoanTabDrag::StopFeedback() {
  state_ = State::kIdle;
  switch_timer_.Stop();
  edge_ = 0;
  escape_monitor_.reset();
  target_ = DropTarget();
  // Zen: the drop is done, so its line goes before the tabs land.
  ShowFeedback(gfx::Rect(), KoanTabDropIndicatorView::Style::kAcross,
               gfx::Rect());
  RemovePromo();
  ClearRowSplit();
  SetLitFolder(0);
  if (page_edge_) {
    page_edge_.reset();
    image_.SetCard(false);
    image_.ShowFakePane(gfx::Rect(), 0);
  }
}

void KoanTabDrag::ReleaseUpstreamAndCapture() {
  // Upstream's drag waited on the press all along: it goes, so the strip is
  // editable again and Koan's moves are no drag of upstream's.
  if (TabDragContext* context = GetDragContext();
      context && GetWaitingUpstreamDrag()) {
    context->DestroyDragController();
  }
  views::Widget* widget = browser_view_->GetWidget();
  if (widget && capture_.view() && widget->HasCapture()) {
    widget->ReleaseCapture();
  }
}

void KoanTabDrag::Land(tabs::TabHandle tab, int folder) {
  ShowHiddenViews();
  landing_ = true;
  views::Widget* widget = browser_view_->GetWidget();
  views::View* row = nullptr;
  if (widget && gfx::Animation::ShouldRenderRichAnimation() &&
      image_.IsShowing()) {
    // Where the drop put it, once the sidebar has laid it out.
    widget->LayoutRootViewIfNecessary();
    row = folder ? FolderRowFor(folder) : RowViewFor(tab.Get());
  }
  if (!row || !row->IsDrawn()) {
    OnLanded();
    return;
  }
  // Hidden until the image is on it (Zen's landing), and at the latest after
  // the failsafe.
  HideView(row);
  landing_failsafe_.Start(FROM_HERE, kLandingFailsafe, this,
                          &KoanTabDrag::OnLanded);
  // (`image_` is a member: the callback never outlives this.)
  image_.Land(
      base::BindRepeating(&KoanTabDrag::LandingTarget, base::Unretained(this),
                          tab, folder),
      base::BindOnce(&KoanTabDrag::OnLanded, weak_factory_.GetWeakPtr()));
}

std::optional<gfx::Rect> KoanTabDrag::LandingTarget(tabs::TabHandle tab,
                                                    int folder) const {
  views::View* row = folder ? FolderRowFor(folder) : RowViewFor(tab.Get());
  if (!row || !row->IsDrawn()) {
    return std::nullopt;
  }
  return row->GetBoundsInScreen();
}

void KoanTabDrag::OnLanded() {
  landing_failsafe_.Stop();
  image_.Hide();
  ShowHiddenViews();
  landing_ = false;
  // Zen's [movingtab] lasts until the drag is over: the sidebar may hide.
  hold_.reset();
}

views::View* KoanTabDrag::RowViewFor(const tabs::TabInterface* tab) const {
  if (!tab) {
    return nullptr;
  }
  TabDragContext* context = GetDragContext();
  views::View* view =
      context ? context->GetTabForContents(tab->GetContents()) : nullptr;
  if (view && view->parent() &&
      views::IsViewClass<SplitTabView>(view->parent())) {
    return view->parent();
  }
  return view;
}

std::vector<tabs::TabInterface*> KoanTabDrag::DraggedTabs() const {
  TabStripModel* model = controller_->tab_strip_model();
  std::vector<tabs::TabInterface*> tabs;
  for (const tabs::TabHandle& handle : dragged_) {
    if (tabs::TabInterface* tab = handle.Get();
        tab && model->GetIndexOfTab(tab) != TabStripModel::kNoTab) {
      tabs.push_back(tab);
    }
  }
  std::ranges::sort(tabs, {}, [model](const tabs::TabInterface* tab) {
    return model->GetIndexOfTab(tab);
  });
  return tabs;
}

bool KoanTabDrag::DragsEssentials() const {
  const std::vector<tabs::TabInterface*> tabs = DraggedTabs();
  return !tabs.empty() &&
         std::ranges::all_of(tabs, &tabs::TabInterface::IsPinned);
}

void KoanTabDrag::SwitchSpace(const tab_groups::TabGroupId& space) {
  // Only the Space shown changes: the tabs stay in theirs until the drop,
  // so a cancel has nothing to undo but the switch.
  controller_->SwitchTo(space);
}

void KoanTabDrag::OnSwitchTimer() {
  if (state_ != State::kDragging || edge_ == 0) {
    return;
  }
  const std::vector<KoanWorkspaceController::SpaceInfo> spaces =
      controller_->GetSpaces();
  const auto active = std::ranges::find(
      spaces, true, &KoanWorkspaceController::SpaceInfo::active);
  if (active == spaces.end()) {
    return;
  }
  // As the Space shortcuts: wrapping around only if the setting says so.
  const std::optional<size_t> target =
      NextSpaceIndex(static_cast<size_t>(active - spaces.begin()),
                     spaces.size(), edge_ > 0, controller_->WrapsAround());
  if (!target) {
    return;
  }
  SwitchSpace(spaces[*target].id);
  // Zen's dragover keeps coming while the tab stays at the edge, and each
  // one starts the delay again: so does this, until the pointer moves away.
  switch_timer_.Start(FROM_HERE, kSpaceDragSwitchDelay, this,
                      &KoanTabDrag::OnSwitchTimer);
}

void KoanTabDrag::RestoreOrigin() {
  TabStripModel* model = controller_->tab_strip_model();
  tabs::TabInterface* active = origin_active_.Get();
  const bool switched =
      origin_space_ && controller_->GetActiveSpace() != origin_space_;
  if (!switched && model->GetActiveTab() == active) {
    return;
  }
  if (switched) {
    controller_->SwitchTo(*origin_space_);
  }
  // The tab and the selection the drag started with (a switch showed the
  // Space's remembered tab, a page split the tab shown before).
  const int active_index =
      active ? model->GetIndexOfTab(active) : TabStripModel::kNoTab;
  if (active_index == TabStripModel::kNoTab) {
    return;
  }
  ui::ListSelectionModel selection;
  for (const tabs::TabHandle& handle : origin_selection_) {
    if (tabs::TabInterface* tab = handle.Get();
        tab && model->GetIndexOfTab(tab) != TabStripModel::kNoTab) {
      selection.AddIndexToSelection(
          static_cast<size_t>(model->GetIndexOfTab(tab)));
    }
  }
  selection.AddIndexToSelection(static_cast<size_t>(active_index));
  selection.set_active(static_cast<size_t>(active_index));
  const tabs::TabInterface* anchor = origin_anchor_.Get();
  const int anchor_index =
      anchor ? model->GetIndexOfTab(anchor) : TabStripModel::kNoTab;
  selection.set_anchor(static_cast<size_t>(
      anchor_index != TabStripModel::kNoTab &&
              selection.IsSelected(static_cast<size_t>(anchor_index))
          ? anchor_index
          : active_index));
  model->SetSelectionFromModel(std::move(selection));
}

bool KoanTabDrag::MaybeHandOff(const gfx::Point& screen_point,
                               const ui::MouseEvent& event) {
  views::Widget* widget = browser_view_->GetWidget();
  if (!widget || widget->GetWindowBoundsInScreen().Contains(screen_point)) {
    return false;
  }
  // Another window's sidebar, of the same profile, under the pointer.
  bool over_other = false;
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    BrowserView* other = BrowserView::GetBrowserViewForBrowser(browser);
    if (!other || other == browser_view_ ||
        browser->GetProfile() != browser_view_->GetProfile() ||
        !other->GetWidget() || !other->GetWidget()->IsVisible() ||
        !other->ShouldDrawVerticalTabStrip()) {
      continue;
    }
    TabStripRegionView* strip = other->tab_strip_view();
    if (strip && strip->IsDrawn() &&
        strip->GetBoundsInScreen().Contains(screen_point)) {
      over_other = true;
      break;
    }
  }
  TabDragContext* context = GetDragContext();
  auto* handler =
      context ? views::AsViewClass<TabDragHandler>(context) : nullptr;
  views::View* capture = capture_.view();
  // Upstream would take a Space's group along whole (tab_drag_handler.cc's
  // fully selected groups): not that.
  if (!over_other || !handler || !capture || !GetWaitingUpstreamDrag() ||
      DragsWholeSpace()) {
    return false;
  }
  // Back as it was here; Koan's image and feedback go, and upstream's drag
  // starts from the press, takes the capture and the tabs (to that window,
  // as before Koan's drag).
  StopFeedback();
  RestoreOrigin();
  image_.Hide();
  ShowHiddenViews();
  hold_.reset();
  handler->ContinueDrag(*capture, event);
  return true;
}

bool KoanTabDrag::DragsWholeSpace() const {
  TabStripModel* model = controller_->tab_strip_model();
  std::map<tab_groups::TabGroupId, int> counts;
  for (tabs::TabInterface* tab : DraggedTabs()) {
    if (const std::optional<tab_groups::TabGroupId> group = tab->GetGroup()) {
      ++counts[*group];
    }
  }
  return std::ranges::any_of(counts, [model](const auto& entry) {
    const TabGroup* group = model->group_model()->GetTabGroup(entry.first);
    return group && group->tab_count() == entry.second;
  });
}

tabs::TabInterface* KoanTabDrag::SplittableTab() const {
  const std::vector<tabs::TabInterface*> tabs = DraggedTabs();
  // Zen: one tab, not an Essential, not in a split.
  return tabs.size() == 1 && !tabs.front()->IsPinned() &&
                 !tabs.front()->IsSplit()
             ? tabs.front()
             : nullptr;
}

bool KoanTabDrag::UpdatePageSplit(const gfx::Point& screen_point) {
  TabStripModel* model = controller_->tab_strip_model();
  tabs::TabInterface* dragged = SplittableTab();
  MultiContentsView* contents = browser_view_->multi_contents_view();
  KoanBrowserViewController* koan =
      KoanBrowserViewController::From(browser_view_);
  const bool glance = koan && koan->glance() && koan->glance()->is_open();
  tabs::TabInterface* shown = shown_before_press_.Get();
  if (!shown || model->GetIndexOfTab(shown) == TabStripModel::kNoTab) {
    shown = model->GetActiveTab();
  }
  std::optional<SplitEdge> edge;
  gfx::Rect panels;
  if (dragged && contents && contents->IsDrawn() && !glance && shown &&
      !shown->IsPinned() && !IsEmptyTab(shown)) {
    panels = contents->GetBoundsInScreen();
    edge = PageSplitEdge(panels, screen_point);
    if (edge && shown->IsSplit()) {
      // Koan's splits take two panes: Zen's toast for a third, once.
      if (!limit_toast_shown_) {
        limit_toast_shown_ = true;
        ShowSplitLimitToast(browser_view_);
      }
      edge.reset();
    }
  }
  if (!edge) {
    if (page_edge_) {
      page_edge_.reset();
      image_.SetCard(false);
      image_.ShowFakePane(gfx::Rect(), 0);
    }
    return false;
  }
  const int edge_value = static_cast<int>(*edge);
  if (page_edge_ != edge_value) {
    if (!page_edge_) {
      // Zen: the card, a tick, and the page the tab was dragged from.
      image_.SetCard(true);
      PlayHapticFeedback();
      if (shown != model->GetActiveTab()) {
        model->ActivateTabAt(model->GetIndexOfTab(shown));
      }
    }
    page_edge_ = edge_value;
    image_.ShowFakePane(FakePaneBounds(panels, *edge, 2, kContentCardMargin),
                        edge_value);
  }
  ClearRowSplit();
  RemovePromo();
  ShowFeedback(gfx::Rect(), KoanTabDropIndicatorView::Style::kAcross,
               gfx::Rect());
  DropTarget target;
  target.kind = DropTarget::Kind::kSplitPage;
  target.split_with = shown->GetHandle();
  target.split_first = *edge == SplitEdge::kLeft || *edge == SplitEdge::kTop;
  target.split_stacked =
      *edge == SplitEdge::kTop || *edge == SplitEdge::kBottom;
  target_ = target;
  return true;
}

bool KoanTabDrag::UpdateRowSplit(const SpaceListRow& row,
                                 tabs::TabInterface* tab,
                                 const gfx::Point& point) {
  tabs::TabInterface* dragged = SplittableTab();
  const std::optional<bool> first =
      dragged && tab && tab != dragged && !tab->IsSplit() && !tab->IsPinned() &&
              !IsEmptyTab(tab)
          ? SplitBandSide(row.bounds, point, base::i18n::IsRTL())
          : std::nullopt;
  if (!first) {
    ClearRowSplit();
    return false;
  }
  if (split_row_.Get() == tab && split_row_first_ == *first) {
    return true;
  }
  if (split_candidate_.Get() == tab && split_candidate_first_ == *first &&
      split_timer_.IsRunning()) {
    return false;
  }
  // A new row or side: Zen's delay again.
  ClearRowSplit();
  split_candidate_ = tab->GetHandle();
  split_candidate_first_ = *first;
  split_timer_.Start(FROM_HERE, kSplitDragDelay, this,
                     &KoanTabDrag::OnSplitTimer);
  return false;
}

void KoanTabDrag::OnSplitTimer() {
  if (state_ != State::kDragging) {
    return;
  }
  split_row_ = split_candidate_;
  split_row_first_ = split_candidate_first_;
  UpdateTarget(last_point_);
}

void KoanTabDrag::ClearRowSplit() {
  split_timer_.Stop();
  split_candidate_ = tabs::TabHandle();
  split_row_ = tabs::TabHandle();
}

void KoanTabDrag::UpdateTarget(const gfx::Point& point) {
  DropTarget target;
  gfx::Rect line;
  gfx::Rect bar;
  KoanTabDropIndicatorView::Style line_style =
      KoanTabDropIndicatorView::Style::kAcross;
  bool on_row = false;
  int lit_folder = 0;
  views::View* region = this->region();
  KoanPinnedTabController* pinned = pinned_tabs();
  TabDragContext* context = GetDragContext();
  TabStripModel* model = controller_->tab_strip_model();
  const bool essentials = DragsEssentials();
  if (region && pinned && context && !IsSidebarCollapsed()) {
    // Zen: tabs over the grid only while one more fits
    // (canEssentialBeAdded()); a tab over a full grid drops back into the
    // Space. Not a folder, nor a split pane from its grip.
    if (IsOverEssentials(point) && !DragsFolder() && !pane_drag_ &&
        (essentials || pinned->CanAddToEssentials())) {
      target.kind = DropTarget::Kind::kEssentials;
      const std::vector<DropUnit> tiles =
          DropUnits(model, context, region,
                    gfx::Range(0, model->IndexOfFirstNonPinnedTab()));
      if (!tiles.empty()) {
        const bool rtl = base::i18n::IsRTL();
        const size_t slot = EssentialsDropSlot(UnitBounds(tiles), point, rtl);
        // Upright between two tiles: before the slot's tile, or after the
        // last one.
        const bool after = slot == tiles.size();
        const gfx::Rect& tile = tiles[after ? slot - 1 : slot].bounds;
        const int edge = after != rtl ? tile.right() + kTileGapHalf
                                      : tile.x() - kTileGapHalf;
        line =
            gfx::Rect(edge - KoanTabDropIndicatorView::kThickness / 2, tile.y(),
                      KoanTabDropIndicatorView::kThickness, tile.height());
        line_style = KoanTabDropIndicatorView::Style::kUpright;
        if (!after) {
          target.before = tiles[slot].first;
        }
      }
      target.feedback = line.IsEmpty() && promo_
                            ? views::View::ConvertRectToTarget(
                                  promo_, region, promo_->GetLocalBounds())
                            : line;
    } else if (IsOverSpace(point)) {
      std::vector<tabs::TabHandle> firsts;
      const std::vector<SpaceListRow> rows = SpaceRows(firsts);
      const SpaceListLayout layout = SpaceLayout(rows);
      const SpaceListDrop drop = PlaceInSpaceList(rows, layout, point.y());
      target.kind = DropTarget::Kind::kSpace;
      target.pinned = drop.pinned;
      target.folder = drop.folder;
      // Before the first tab from the slot on, in the section, that isn't
      // dragged; none: the section's end.
      for (size_t i = drop.slot;
           i < rows.size() && rows[i].pinned == drop.pinned; ++i) {
        if (firsts[i].Get() && !std::ranges::contains(dragged_, firsts[i])) {
          target.before = firsts[i];
          break;
        }
      }
      // A folder dragged by its row: among the pinned tabs and folders
      // only, where it lands.
      std::optional<FolderDrop> folder_drop;
      if (KoanFolderController* folders = this->folders();
          folders && DragsFolder()) {
        folder_drop = PlaceFolderInSpaceList(
            rows, layout, drop, dragged_folder_, [&](int parent) {
              return folders->CanHoldFolder(parent, dragged_folder_);
            });
        target = DropTarget();
        if (folder_drop) {
          target.kind = DropTarget::Kind::kFolder;
          target.pinned = true;
          target.folder = folder_drop->parent;
          target.before_folder = folder_drop->before;
        }
      }
      // Held over the middle of a tab's row: Zen's split, after its delay.
      for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].kind != SpaceListRow::Kind::kTab ||
            !rows[i].bounds.Contains(point)) {
          continue;
        }
        on_row = true;
        if (UpdateRowSplit(rows[i], firsts[i].Get(), point)) {
          target = DropTarget();
          target.kind = DropTarget::Kind::kSplitTab;
          target.split_with = split_row_;
          target.split_first = split_row_first_;
          line = SplitFakeTabBounds(rows[i].bounds, split_row_first_,
                                    base::i18n::IsRTL());
          line_style = KoanTabDropIndicatorView::Style::kSplitTab;
          target.feedback = line;
        }
        break;
      }
      if (target.kind == DropTarget::Kind::kSplitTab ||
          target.kind == DropTarget::Kind::kNone) {
        // (The fake tab instead of the line; or nowhere for a folder.)
      } else if (drop.lit_header || drop.lit_row) {
        const gfx::Rect lit =
            drop.lit_row ? rows[*drop.lit_row].bounds : layout.header;
        bar = gfx::Rect(0, lit.y() + kDragOverMargin, region->width(),
                        std::max(0, lit.height() - 2 * kDragOverMargin));
        if (drop.lit_row) {
          lit_folder = rows[*drop.lit_row].folder_id;
        }
      } else if (folder_drop) {
        line = DropLineBounds(folder_drop->line_row, folder_drop->line_y);
      } else {
        line = DropLineBounds(drop.line_row, drop.line_y);
      }
      if (target.kind == DropTarget::Kind::kSpace ||
          target.kind == DropTarget::Kind::kFolder) {
        target.feedback = bar.IsEmpty() ? line : bar;
      }
    }
  }
  if (!on_row) {
    ClearRowSplit();
  }
  ShowFeedback(line, line_style, bar);
  SetLitFolder(lit_folder);
  // A tick for each new place, but the first (Zen finds it too much).
  if (!target.feedback.IsEmpty() && target.feedback != target_.feedback &&
      !target_.feedback.IsEmpty()) {
    if (haptic_skipped_) {
      PlayHapticFeedback();
    }
    haptic_skipped_ = true;
  }
  target_ = target;
}

std::vector<SpaceListRow> KoanTabDrag::SpaceRows(
    std::vector<tabs::TabHandle>& firsts) {
  struct Item {
    SpaceListRow row;
    tabs::TabHandle first;
  };
  std::vector<Item> items;
  views::View* region = this->region();
  TabStripView* strip = GetStrip();
  views::View* container = strip ? strip->GetUnpinnedTabsContainer() : nullptr;
  KoanBrowserViewController* koan =
      KoanBrowserViewController::From(browser_view_);
  KoanFolderController* folders =
      koan && koan->folders() && koan->folders()->IsEnabled() ? koan->folders()
                                                              : nullptr;
  const views::View* group = nullptr;
  for (const views::View* child :
       container ? container->children() : views::View::Views()) {
    if (const auto* group_view = views::AsViewClass<TabGroupView>(child);
        group_view && group_view->IsGroupFocused()) {
      group = group_view;
      break;
    }
  }
  for (views::View* child : group ? group->children() : views::View::Views()) {
    if (!child->IsDrawn() || child->bounds().IsEmpty()) {
      continue;
    }
    Item item;
    item.row.bounds = views::View::ConvertRectToTarget(child, region,
                                                       child->GetLocalBounds());
    const TabView* tab_view = views::AsViewClass<TabView>(child);
    if (!tab_view && views::IsViewClass<SplitTabView>(child) &&
        !child->children().empty()) {
      tab_view = views::AsViewClass<TabView>(child->children().front());
    }
    if (tab_view) {
      const tabs::TabInterface* tab = tab_view->GetTabInterface();
      if (!tab || IsEmptyTab(tab)) {
        continue;
      }
      item.row.pinned = IsPinnedForUser(tab);
      item.row.folder = folders ? folders->GetFolderOf(tab) : 0;
      item.first = tab->GetHandle();
    } else if (const auto* folder_row =
                   views::AsViewClass<KoanFolderRowView>(child);
               folder_row && folders && !folder_row->removing()) {
      const KoanFolder* folder = folders->GetFolder(folder_row->folder_id());
      if (!folder) {
        continue;
      }
      item.row.kind = SpaceListRow::Kind::kFolder;
      item.row.pinned = true;
      item.row.folder_id = folder->id;
      item.row.folder = folder->parent;
      item.row.collapsed = folder->collapsed;
      item.row.items = folders->GetTabs(folder->id, /*recursive=*/false).size();
      for (const KoanFolderController::Entry& entry :
           folders->GetFolders(folder->space)) {
        const KoanFolder* other = folders->GetFolder(entry.id);
        if (other && other->parent == folder->id) {
          ++item.row.items;
        }
      }
    } else {
      continue;
    }
    items.push_back(std::move(item));
  }
  std::ranges::stable_sort(
      items, {}, [](const Item& item) { return item.row.bounds.y(); });
  // Each folder's row ends where the rows inside it do.
  const auto inside = [folders](int folder, int id) {
    for (int depth = 0; folder && depth <= KoanFolderController::kMaxDepth;
         ++depth) {
      if (folder == id) {
        return true;
      }
      const KoanFolder* parent = folders->GetFolder(folder);
      folder = parent ? parent->parent : 0;
    }
    return false;
  };
  for (size_t i = 0; i < items.size(); ++i) {
    SpaceListRow& row = items[i].row;
    if (row.kind != SpaceListRow::Kind::kFolder) {
      continue;
    }
    size_t end = i + 1;
    while (end < items.size() && inside(items[end].row.folder, row.folder_id)) {
      ++end;
    }
    row.end = end;
  }
  std::vector<SpaceListRow> rows;
  firsts.clear();
  for (Item& item : items) {
    rows.push_back(item.row);
    firsts.push_back(item.first);
  }
  return rows;
}

SpaceListLayout KoanTabDrag::SpaceLayout(
    const std::vector<SpaceListRow>& rows) const {
  SpaceListLayout layout;
  views::View* region = this->region();
  if (KoanSpaceHeaderView* header = controller_->space_header();
      header && header->IsDrawn()) {
    layout.header = views::View::ConvertRectToTarget(header, region,
                                                     header->GetLocalBounds());
  }
  // The separator's bottom ends the pinned section; without the rows (none
  // drawn yet), the last pinned row's does.
  if (const std::optional<SpaceSeparatorRows> separator_rows =
          GetStrip() ? GetSpaceSeparatorRows(GetStrip(), region)
                     : std::nullopt) {
    layout.separator = separator_rows->rows;
    layout.separator.set_height(separator_rows->separator_height);
    layout.normal_start = separator_rows->rows.bottom();
    return layout;
  }
  const auto first_normal =
      std::ranges::find_if_not(rows, &SpaceListRow::pinned);
  const int y = first_normal != rows.begin()
                    ? std::prev(first_normal)->bounds.bottom()
                : rows.empty() ? 0
                               : rows.front().bounds.y();
  layout.separator = gfx::Rect(0, y, region ? region->width() : 0, 0);
  layout.normal_start = y;
  return layout;
}

void KoanTabDrag::ShowFeedback(const gfx::Rect& line,
                               KoanTabDropIndicatorView::Style line_style,
                               const gfx::Rect& bar) {
  TabStripView* strip = GetStrip();
  views::View* region = this->region();
  const auto show = [&](raw_ptr<KoanTabDropIndicatorView>& view,
                        const gfx::Rect& bounds, bool background,
                        KoanTabDropIndicatorView::Style style) {
    if (bounds.IsEmpty() || !strip || !region) {
      if (KoanTabDropIndicatorView* old = std::exchange(view, nullptr)) {
        views::View* parent = old->parent();
        parent->RemoveChildViewT(old);
        parent->InvalidateLayout();
      }
      return;
    }
    if (!view) {
      // The bar under the rows (the strip's first child), the line over them.
      auto indicator = std::make_unique<KoanTabDropIndicatorView>(background);
      view = background ? strip->AddChildViewAt(std::move(indicator), 0)
                        : strip->AddChildView(std::move(indicator));
    }
    view->SetSlot(views::View::ConvertRectToTarget(region, strip, bounds),
                  style);
  };
  show(line_, line, /*background=*/false, line_style);
  show(bar_, bar, /*background=*/true,
       KoanTabDropIndicatorView::Style::kBackground);
}

void KoanTabDrag::SetLitFolder(int id) {
  if (id == lit_folder_) {
    return;
  }
  if (KoanFolderRowView* old = FolderRowFor(lit_folder_)) {
    old->SetDragOver(false);
  }
  lit_folder_ = id;
  if (KoanFolderRowView* row = FolderRowFor(id)) {
    row->SetDragOver(true);
  }
}

KoanFolderRowView* KoanTabDrag::FolderRowFor(int id) const {
  TabStripView* strip = id ? GetStrip() : nullptr;
  views::View* container = strip ? strip->GetUnpinnedTabsContainer() : nullptr;
  for (views::View* child :
       container ? container->children() : views::View::Views()) {
    const auto* group = views::AsViewClass<TabGroupView>(child);
    if (!group || !group->IsGroupFocused()) {
      continue;
    }
    for (views::View* row : child->children()) {
      if (auto* folder_row = views::AsViewClass<KoanFolderRowView>(row);
          folder_row && folder_row->folder_id() == id &&
          !folder_row->removing()) {
        return folder_row;
      }
    }
  }
  return nullptr;
}

bool KoanTabDrag::UpdatePaneDrag(tabs::TabInterface* tab,
                                 const gfx::Point& screen_point) {
  views::View* region = browser_view_->tab_strip_view();
  if (state_ == State::kDragging || landing_ || !tab || !tab->IsSplit() ||
      !region || !region->IsDrawn() || IsSidebarCollapsed() ||
      !region->GetBoundsInScreen().Contains(screen_point)) {
    EndPaneDrag();
    return false;
  }
  region_.SetView(region);
  gfx::Point point = screen_point;
  views::View::ConvertPointFromScreen(region, &point);
  if (!IsOverSpace(point)) {
    EndPaneDrag();
    return false;
  }
  if (!pane_drag_) {
    pane_drag_ = true;
    dragged_ = {tab->GetHandle()};
    dragged_folder_ = 0;
    target_ = DropTarget();
    haptic_skipped_ = false;
  }
  last_point_ = point;
  UpdateTarget(point);
  return target_.kind == DropTarget::Kind::kSpace;
}

void KoanTabDrag::DropPane(tabs::TabInterface* tab, bool keep_selection) {
  const DropTarget target = target_;
  const bool over_sidebar = pane_drag_;
  EndPaneDrag();
  KoanPinnedTabController* pinned = pinned_tabs();
  TabStripModel* model = controller_->tab_strip_model();
  if (!over_sidebar || target.kind != DropTarget::Kind::kSpace || !pinned ||
      !tab || !tab->IsSplit() ||
      model->GetIndexOfTab(tab) == TabStripModel::kNoTab) {
    return;
  }
  // Zen's removeTabFromSplit(), then the tab where the line showed.
  model->RemoveSplit(*tab->GetSplit());
  pinned->DropTabsInSpace({tab}, target.before.Get(), target.pinned);
  if (KoanFolderController* folders = this->folders()) {
    folders->DropInFolder({tab}, target.pinned ? target.folder : 0);
  }
  if (!keep_selection) {
    model->ActivateTabAt(model->GetIndexOfTab(tab));
  }
}

void KoanTabDrag::EndPaneDrag() {
  if (!std::exchange(pane_drag_, false)) {
    return;
  }
  dragged_.clear();
  target_ = DropTarget();
  ShowFeedback(gfx::Rect(), KoanTabDropIndicatorView::Style::kAcross,
               gfx::Rect());
  SetLitFolder(0);
}

void KoanTabDrag::HideView(views::View* view) {
  if (!view) {
    return;
  }
  HiddenView hidden;
  hidden.view = std::make_unique<views::ViewTracker>(view);
  hidden.own_layer = !view->layer();
  if (hidden.own_layer) {
    view->SetPaintToLayer();
    view->layer()->SetFillsBoundsOpaquely(false);
  }
  view->layer()->SetOpacity(0);
  hidden_.push_back(std::move(hidden));
}

void KoanTabDrag::ShowHiddenViews() {
  for (HiddenView& hidden : std::exchange(hidden_, {})) {
    views::View* view = hidden.view->view();
    if (!view || !view->layer()) {
      continue;
    }
    view->layer()->SetOpacity(1);
    if (hidden.own_layer) {
      view->DestroyLayer();
    }
  }
}

gfx::Rect KoanTabDrag::EmptyEssentialsZone() const {
  KoanSpaceHeaderView* header = controller_->space_header();
  views::View* region = this->region();
  if (!header || !header->GetVisible() || !region) {
    return gfx::Rect();
  }
  std::optional<gfx::Rect> promo;
  if (promo_) {
    promo = views::View::ConvertRectToTarget(promo_, region,
                                             promo_->GetLocalBounds());
  }
  return EmptyEssentialsDropZone(region->size(),
                                 views::View::ConvertRectToTarget(
                                     header, region, header->GetLocalBounds()),
                                 promo);
}

bool KoanTabDrag::IsOverEssentials(const gfx::Point& point) const {
  views::View* region = this->region();
  // With the promo: it, or the sidebar above it.
  if (promo_ && promo_->GetVisible() && EmptyEssentialsZone().Contains(point)) {
    return true;
  }
  TabStripView* strip = GetStrip();
  if (!strip || !region) {
    return false;
  }
  views::View* essentials = strip->pinned_tabs_scroll_view();
  if (!essentials) {
    essentials = strip->GetPinnedTabsContainer();
  }
  if (!essentials || !essentials->GetVisible()) {
    return false;
  }
  // The grid, or the sidebar above it (a tab dragged straight up, over the
  // URL bar).
  return essentials->HitTestPoint(InView(region, essentials, point)) ||
         EssentialsTopDropZone(
             region->size(),
             views::View::ConvertRectToTarget(essentials, region,
                                              essentials->GetLocalBounds()))
             .Contains(point);
}

bool KoanTabDrag::IsOverSpace(const gfx::Point& point) const {
  views::View* region = this->region();
  // The Space header counts as its pinned section (Zen's
  // .zen-current-workspace-indicator).
  if (KoanSpaceHeaderView* header = controller_->space_header();
      header && region && header->IsDrawn() &&
      header->HitTestPoint(InView(region, header, point))) {
    return true;
  }
  TabStripView* strip = GetStrip();
  views::ScrollView* list =
      strip ? strip->unpinned_tabs_scroll_view() : nullptr;
  return list && region && list->IsDrawn() &&
         list->HitTestPoint(InView(region, list, point));
}

void KoanTabDrag::UpdatePromo(const gfx::Point& point) {
  TabStripView* strip = GetStrip();
  KoanSpaceHeaderView* header = controller_->space_header();
  KoanPinnedTabController* pinned = pinned_tabs();
  // Zen: only while there are no Essentials, for a tab that can become one,
  // in an expanded sidebar.
  const bool can_promo = strip && header && pinned &&
                         pinned->EssentialsCount() == 0 && !DragsEssentials() &&
                         !DragsFolder() && !pane_drag_ && !IsSidebarCollapsed();
  if (!can_promo) {
    RemovePromo();
    return;
  }
  if (!promo_) {
    // Zen shows it once the tab is over the (empty) Essentials: here, the
    // top of the sidebar, down to the Space header's middle.
    if (!EmptyEssentialsZone().Contains(point)) {
      return;
    }
    // Before the header, which PlaceSpaceHeader() puts below it.
    promo_ = strip->AddChildViewAt(std::make_unique<KoanEssentialsPromoView>(),
                                   strip->GetIndexOf(header).value_or(0));
    strip->InvalidateLayout();
  }
  // Like Zen's, it stays for the rest of the drag; lit while a drop would
  // land in it (over it, or the sidebar above it).
  promo_->SetDragOver(IsOverEssentials(point));
}

void KoanTabDrag::RemovePromo() {
  KoanEssentialsPromoView* promo = std::exchange(promo_, nullptr);
  if (!promo) {
    return;
  }
  views::View* strip = promo->parent();
  strip->RemoveChildViewT(promo);
  strip->InvalidateLayout();
}

TabStripView* KoanTabDrag::GetStrip() const {
  KoanSpaceHeaderView* header = controller_->space_header();
  return header ? views::AsViewClass<TabStripView>(header->parent()) : nullptr;
}

TabDragContext* KoanTabDrag::GetDragContext() const {
  TabStripRegionView* strip = browser_view_->tab_strip_view();
  return strip ? strip->GetDragContext() : nullptr;
}

TabDragController* KoanTabDrag::GetWaitingUpstreamDrag() const {
  TabDragContext* context = GetDragContext();
  TabDragController* controller =
      context ? context->GetDragController() : nullptr;
  return controller && !controller->started_drag() ? controller : nullptr;
}

views::View* KoanTabDrag::region() const {
  return region_.view();
}

KoanFolderController* KoanTabDrag::folders() const {
  KoanBrowserViewController* koan =
      KoanBrowserViewController::From(browser_view_);
  return koan && koan->folders() && koan->folders()->IsEnabled()
             ? koan->folders()
             : nullptr;
}

KoanPinnedTabController* KoanTabDrag::pinned_tabs() const {
  KoanBrowserViewController* koan =
      KoanBrowserViewController::From(browser_view_);
  return koan ? koan->pinned_tabs() : nullptr;
}

bool KoanTabDrag::IsSidebarCollapsed() const {
  // Zen's _prefsSidebarExpanded.
  const auto* state =
      tabs::VerticalTabStripStateController::From(browser_view_->browser());
  return state && state->IsCollapsed();
}

}  // namespace koan
