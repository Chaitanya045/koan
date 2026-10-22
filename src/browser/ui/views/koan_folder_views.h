// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_FOLDER_VIEWS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_FOLDER_VIEWS_H_

#include <memory>
#include <string>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/textfield/textfield_controller.h"

namespace views {
class BubbleDialogDelegate;
class Textfield;
class Widget;
}  // namespace views

namespace koan {

class KoanFolderController;

// A folder's row in the vertical tab strip, like Zen's folder label: the
// folder icon (open while expanded, with dots while collapsed around the
// active tab), the name in semibold, and the tab count while collapsed.
// Click toggles the folder; double-click renames it; right-click opens the
// folder menu; hovering a collapsed folder opens its search.
class KoanFolderRowView : public views::Button,
                          public views::ContextMenuController,
                          public views::TextfieldController {
  METADATA_HEADER(KoanFolderRowView, views::Button)

 public:
  KoanFolderRowView(KoanFolderController* controller, int folder_id);
  KoanFolderRowView(const KoanFolderRowView&) = delete;
  KoanFolderRowView& operator=(const KoanFolderRowView&) = delete;
  ~KoanFolderRowView() override;

  int folder_id() const { return folder_id_; }

  // Set when the folder is gone and the row animates out.
  bool removing() const { return removing_; }
  void set_removing() { removing_ = true; }

  // Pulls the folder's state from the controller.
  void Update();
  void StartRename();

  // For UI DevTools and tests.
  std::u16string GetFolderName() const { return name_; }
  bool GetCollapsed() const { return collapsed_; }
  int GetTabCount() const { return tab_count_; }
  bool GetRenaming() const;

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void PaintButtonContents(gfx::Canvas* canvas) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // views::Button (an AnimationDelegateViews):
  void AnimationProgressed(const gfx::Animation* animation) override;

 private:
  // Whether the row is too narrow for the name (collapsed sidebar).
  bool IsNarrow() const;
  void FinishRename(bool commit);
  void OnPressed();

  // The controller goes before the window's views do.
  const base::WeakPtr<KoanFolderController> controller_;
  const int folder_id_;
  std::u16string name_;
  bool collapsed_ = false;
  bool has_active_ = false;
  bool removing_ = false;
  int tab_count_ = 0;
  // Opens the folder icon (0: closed, 1: open), like Zen's 0.3s transition.
  gfx::SlideAnimation open_animation_{this};
  raw_ptr<views::Textfield> name_field_ = nullptr;
  base::OneShotTimer search_timer_;
  // Last: the name field's blur callback must not outlive this row.
  base::WeakPtrFactory<KoanFolderRowView> weak_factory_{this};
};

// Zen's folder search popup next to a folder's row.
class KoanFolderSearch {
 public:
  // Shows the popup for `folder_id` next to `anchor`, or returns null if the
  // folder has no tabs to list. `on_closed` runs when it closes; it must
  // destroy the returned object (not synchronously).
  static std::unique_ptr<KoanFolderSearch> Show(
      KoanFolderController* controller,
      int folder_id,
      views::View* anchor,
      base::OnceClosure on_closed);
  KoanFolderSearch(const KoanFolderSearch&) = delete;
  KoanFolderSearch& operator=(const KoanFolderSearch&) = delete;
  ~KoanFolderSearch();

 private:
  KoanFolderSearch();

  // Outlives the widget.
  std::unique_ptr<views::BubbleDialogDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_FOLDER_VIEWS_H_
