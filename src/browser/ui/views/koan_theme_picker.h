// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_THEME_PICKER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_THEME_PICKER_H_

#include <memory>
#include <optional>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "koan/browser/ui/views/koan_space_theme.h"
#include "koan/browser/ui/views/koan_theme_controller.h"
#include "ui/gfx/geometry/rect.h"

namespace views {
class BubbleDialogDelegate;
class Widget;
}  // namespace views

namespace koan {

class KoanThemePickerView;

// Zen's theme picker panel (PanelUI-zen-gradient-generator): the colour
// field with up to three dots, the scheme buttons, the colour actions, the
// preset pages, the wave-shaped opacity slider and the grain dial, plus the
// optional custom colour list. Every change previews on the window at once;
// the controller saves the theme when the panel closes, like Zen.
class KoanThemePicker {
 public:
  KoanThemePicker(KoanThemeController* controller,
                  KoanThemeController::Space space,
                  const theme::SpaceTheme& theme,
                  const gfx::Rect& anchor,
                  bool left_of_anchor,
                  base::OnceClosure on_closed);
  KoanThemePicker(const KoanThemePicker&) = delete;
  KoanThemePicker& operator=(const KoanThemePicker&) = delete;
  ~KoanThemePicker();

  // Shows the panel beside `anchor` (to its left with `left_of_anchor`),
  // over `parent`.
  void Show(views::Widget* parent);

  // The theme as edited so far.
  theme::SpaceTheme GetTheme() const;

  // The window's scheme or theme changed: recolour the panel.
  void OnEnvironmentChanged();

  views::Widget* widget() const { return widget_.get(); }

 private:
  void OnWidgetClosed();

  const raw_ptr<KoanThemeController> controller_;
  base::OnceClosure on_closed_;
  // The window scheme the panel shows.
  std::optional<KoanThemeController::Scheme> shown_scheme_;
  bool shown_dark_ = false;
  std::unique_ptr<views::BubbleDialogDelegate> delegate_;
  raw_ptr<KoanThemePickerView> view_ = nullptr;
  std::unique_ptr<views::Widget> widget_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_THEME_PICKER_H_
