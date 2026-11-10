// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_EXTENSIONS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_EXTENSIONS_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/drag_controller.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

class BrowserView;
class ExtensionsContainer;
class ExtensionsMenuCoordinator;
class ToolbarActionViewModel;

namespace content {
class WebContents;
}

namespace koan {

// Zen's extension buttons in the single toolbar's URL bar (issue #73;
// ZenVerticalTabsManager puts the toolbar's extension buttons before the
// URL bar's page actions, zen-omnibox.css shows them while the sidebar is
// hovered or one of their popups is open): one button per extension pinned
// to the toolbar, at the trailing end of the sidebar's URL pill. They fill
// at most 55 % of the pill (zen.view.overflow-webext-toolbar-threshold);
// the others, and every extension, are in Chromium's extensions menu, which
// the site panel opens (Zen's site data panel lists them).
//
// The buttons are views over upstream's models only: which extensions are
// pinned is ToolbarActionsModel's, and each button drives the toolbar
// container's ToolbarActionViewModel (icon, tooltip, click, Chromium's
// context menu), so popups, keyboard shortcuts and chrome.action behave as
// in the toolbar. The popup points at the button (GetExtensionsAnchorView()
// in koan_extensions_hooks.h).
//
// As in Chromium's toolbar, a button can be dragged along the pill to reorder
// the pinned extensions (ToolbarActionsModel::MovePinnedAction(), with the
// toolbar's drag data, BrowserActionDragData); not in Incognito, and not a
// force-pinned extension.
class KoanSidebarExtensionsView : public views::View,
                                  public views::DragController,
                                  public ToolbarActionsModel::Observer,
                                  public TabStripModelObserver,
                                  public views::ViewObserver {
  METADATA_HEADER(KoanSidebarExtensionsView, views::View)

 public:
  explicit KoanSidebarExtensionsView(BrowserView* browser_view);
  KoanSidebarExtensionsView(const KoanSidebarExtensionsView&) = delete;
  KoanSidebarExtensionsView& operator=(const KoanSidebarExtensionsView&) =
      delete;
  ~KoanSidebarExtensionsView() override;

  // The button of the extension `action_id`, if it is on screen.
  views::View* GetButtonForAction(std::string_view action_id) const;

  // Chromium's extensions menu (the puzzle button's: the extensions and
  // their access to the site, Manage Extensions) at the pill's site icon:
  // Zen's site panel's Extensions section, from the site panel's Extensions
  // row.
  void ShowExtensionsMenu();

  base::WeakPtr<KoanSidebarExtensionsView> GetWeakPtr();

  // views::View:
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool AreDropTypesRequired() override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;

  // views::DragController (the buttons'):
  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override;
  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& p) override;
  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& p) override;

  // ToolbarActionsModel::Observer:
  void OnToolbarActionAdded(
      const ToolbarActionsModel::ActionId& action_id) override;
  void OnToolbarActionRemoved(
      const ToolbarActionsModel::ActionId& action_id) override;
  void OnToolbarActionUpdated(
      const ToolbarActionsModel::ActionId& action_id) override;
  void OnToolbarModelInitialized() override;
  void OnToolbarPinnedActionsChanged() override;
  void OnToolbarActionsModelShutdown() override;

  // TabStripModelObserver (the icons are per tab):
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // views::ViewObserver (the pill's width):
  void OnViewBoundsChanged(views::View* observed_view) override;

 private:
  class Button;
  class SidebarHoverHandler;

  // The pointer entered (or left) the sidebar.
  void OnSidebarHovered(bool hovered);

  // The window's extensions container (the toolbar's), which owns the view
  // models. Null while it isn't there (window teardown).
  ExtensionsContainer* GetContainer() const;
  content::WebContents* GetActiveContents() const;
  ToolbarActionViewModel* GetModel(const std::string& action_id) const;

  // Rebuilds the buttons from the pinned extensions, after the container
  // has had its own say about a change (its view models follow the same
  // notifications).
  void ScheduleRebuild();
  void Rebuild();
  void UpdateButtons();
  // How many buttons fit in Zen's share of the pill.
  void UpdateFit();
  // Shown while the sidebar is hovered, a button has focus or its context
  // menu is open, or one of their popups is open; faded like Zen's.
  void UpdateReveal();
  bool ShouldReveal() const;

  void OnButtonPressed(Button* button);

  // A drag of the button of `action_id` is over the pill, showing it in the
  // slot `slot` of the fitting buttons.
  struct DropPreview {
    std::string action_id;
    size_t slot = 0;
  };
  // The index among the fitting buttons of `action_id`'s, if it fits.
  std::optional<size_t> FittingIndexOf(std::string_view action_id) const;
  // The drag from one of the buttons ended (dropped anywhere or cancelled).
  void OnButtonDragDone();
  void ClearDropPreview();
  void MovePinnedAction(std::string action_id,
                        size_t slot,
                        const ui::DropTargetEvent& event,
                        ui::mojom::DragOperation& output_drag_op,
                        std::unique_ptr<ui::LayerTreeOwner> drag_image);

  const raw_ptr<BrowserView> browser_view_;
  std::vector<raw_ptr<Button>> buttons_;
  // How many of `buttons_` fit; the rest are hidden.
  size_t fitting_ = 0;
  bool sidebar_hovered_ = false;
  bool revealed_ = false;
  // The view whose pointer enter and exit is the sidebar's, and what
  // watches it.
  raw_ptr<views::View> region_ = nullptr;
  std::unique_ptr<SidebarHoverHandler> hover_handler_;
  // While a popup of these buttons is open: watches for it to close.
  base::RepeatingTimer popup_timer_;
  // After a click, until the popup is up.
  base::TimeTicks press_hold_until_;
  // The extensions menu where kExtensionsMenuAccessControl is on.
  std::unique_ptr<ExtensionsMenuCoordinator> menu_coordinator_;
  bool rebuild_pending_ = false;
  // The extension whose button is being dragged from here, and where it
  // would drop.
  std::optional<std::string> dragged_id_;
  std::optional<DropPreview> drop_preview_;

  base::ScopedObservation<ToolbarActionsModel, ToolbarActionsModel::Observer>
      model_observation_{this};
  base::ScopedObservation<views::View, views::ViewObserver> parent_observation_{
      this};
  base::WeakPtrFactory<KoanSidebarExtensionsView> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_EXTENSIONS_H_
