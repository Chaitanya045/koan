// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_EMOJI_PICKER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_EMOJI_PICKER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace views {
class BubbleDialogDelegate;
class Button;
class Textfield;
class Widget;
}  // namespace views

namespace koan {

// Zen's emoji picker (ZenEmojiPicker): a 250px panel below the anchor with
// a search field and a grid of emoji, plus a None button that removes the
// icon. Like Zen's Space icon pickers it stays open while you pick, so you
// can try icons. Koan searches its own set of emoji by keyword; Zen's
// second page of SVG icons isn't there.
class KoanEmojiPicker : public views::View, public views::TextfieldController {
  METADATA_HEADER(KoanEmojiPicker, views::View)

 public:
  // Runs with the picked emoji, or an empty string for None.
  using PickedCallback = base::RepeatingCallback<void(const std::u16string&)>;

  // An open picker; destroying it closes the picker.
  class Bubble {
   public:
    Bubble(views::View* anchor, bool allow_none, PickedCallback on_pick);
    Bubble(const Bubble&) = delete;
    Bubble& operator=(const Bubble&) = delete;
    ~Bubble();

   private:
    void OnClosed();
    void Drop();

    std::unique_ptr<views::BubbleDialogDelegate> delegate_;
    std::unique_ptr<views::Widget> widget_;
    base::WeakPtrFactory<Bubble> weak_factory_{this};
  };

  // Opens the picker below `anchor`. `allow_none` shows the None button (Zen
  // offers it while there is an icon to remove).
  static std::unique_ptr<Bubble> Show(views::View* anchor,
                                      bool allow_none,
                                      PickedCallback on_pick);

  KoanEmojiPicker(bool allow_none, PickedCallback on_pick);
  KoanEmojiPicker(const KoanEmojiPicker&) = delete;
  KoanEmojiPicker& operator=(const KoanEmojiPicker&) = delete;
  ~KoanEmojiPicker() override;

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;

 private:
  void Pick(const std::u16string& emoji);

  const PickedCallback on_pick_;
  raw_ptr<views::Textfield> search_ = nullptr;
  raw_ptr<views::Button> none_ = nullptr;
  raw_ptr<views::View> grid_ = nullptr;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_EMOJI_PICKER_H_
