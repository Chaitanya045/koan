// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_HEADER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_HEADER_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

namespace views {
class Button;
class Label;
class Textfield;
}  // namespace views

namespace koan {

class KoanWorkspaceController;

// The Space header above the Space's tabs (Zen's current-workspace
// indicator): the active Space's icon and name. Double-click the name to
// rename the Space in place, the icon to change it; right-click (or the
// "..." button shown on hover) opens the Space menu.
class KoanSpaceHeaderView : public views::View,
                            public views::ContextMenuController,
                            public views::TextfieldController,
                            public views::ViewObserver {
  METADATA_HEADER(KoanSpaceHeaderView, views::View)

 public:
  explicit KoanSpaceHeaderView(KoanWorkspaceController* controller);
  KoanSpaceHeaderView(const KoanSpaceHeaderView&) = delete;
  KoanSpaceHeaderView& operator=(const KoanSpaceHeaderView&) = delete;
  ~KoanSpaceHeaderView() override;

  // Shows a Space (`icon` empty: none), or nothing while no Space is active.
  void Update(const std::u16string& icon, const std::u16string& name);
  void Clear();
  bool has_space() const { return has_space_; }

  // In-place rename, like Zen's (Enter or leaving the field keeps the name,
  // Escape cancels).
  void StartRename();
  void CancelRename();
  bool IsRenaming() const;

  views::View* icon_view();
  std::u16string GetName() const;
  std::u16string GetIcon() const;

  // views::View:
  void Layout(PassKey) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseWheel(const ui::MouseWheelEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // views::ViewObserver:
  void OnViewBlurred(views::View* observed_view) override;

 private:
  void FinishRename(bool keep);
  void ShowMenu(const gfx::Point& screen_point);
  void UpdateActionsVisibility();

  const raw_ptr<KoanWorkspaceController> controller_;
  raw_ptr<views::Label> icon_ = nullptr;
  raw_ptr<views::Label> name_ = nullptr;
  raw_ptr<views::Textfield> rename_field_ = nullptr;
  raw_ptr<views::Button> actions_ = nullptr;
  bool has_space_ = false;
  bool hovered_ = false;

  base::ScopedObservation<views::View, views::ViewObserver> rename_observation_{
      this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_HEADER_H_
