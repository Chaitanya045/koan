// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_CREATION_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_CREATION_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "koan/browser/ui/views/koan_emoji_picker.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace views {
class Label;
class LabelButton;
class MdTextButton;
class Textfield;
}  // namespace views

namespace koan {

class KoanSpaceIconField;
class KoanThemeMenu;

// Zen's Create-a-Space form (zen-workspace-creation), shown in the sidebar
// in place of the Space: a title, the icon (Zen's emoji picker) and name of
// the new Space, Edit Theme..., and Create Space / Cancel. Create Space is
// enabled once the Space has a name; Enter creates, Escape cancels.
class KoanSpaceCreationView : public views::View,
                              public views::TextfieldController {
  METADATA_HEADER(KoanSpaceCreationView, views::View)

 public:
  explicit KoanSpaceCreationView(KoanWorkspaceController* controller);
  KoanSpaceCreationView(const KoanSpaceCreationView&) = delete;
  KoanSpaceCreationView& operator=(const KoanSpaceCreationView&) = delete;
  ~KoanSpaceCreationView() override;

  // Focuses the name field, as soon as the form is on screen (a collapsed
  // sidebar is still expanding).
  void FocusName();

  // views::View:
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;
  void VisibilityChanged(views::View* starting_from, bool is_visible) override;

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  void PickIcon();
  void SetIcon(const std::u16string& icon);
  void EditTheme();
  void SetTheme(const KoanWorkspaceController::SpaceTheme& theme);
  // Closes the form (soon: it is usually one of its buttons asking) and
  // creates the Space if `create`.
  void Finish(bool create);
  void DoFinish(bool create);
  void MaybeFocusName();

  const raw_ptr<KoanWorkspaceController> controller_;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> description_ = nullptr;
  raw_ptr<KoanSpaceIconField> icon_ = nullptr;
  raw_ptr<views::Textfield> name_ = nullptr;
  raw_ptr<views::LabelButton> theme_button_ = nullptr;
  raw_ptr<views::MdTextButton> create_ = nullptr;
  KoanWorkspaceController::SpaceTheme theme_;
  bool focus_name_pending_ = false;
  std::unique_ptr<KoanThemeMenu> theme_menu_;
  std::unique_ptr<KoanEmojiPicker::Bubble> picker_;
  base::WeakPtrFactory<KoanSpaceCreationView> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_CREATION_H_
