// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_THEME_PICKER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_THEME_PICKER_H_

#include <memory>
#include <optional>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "koan/browser/ui/views/koan_space_theme.h"
#include "koan/browser/ui/views/koan_theme_controller.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

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
class KoanThemePicker : public gfx::AnimationDelegate,
                        public views::WidgetObserver {
 public:
  KoanThemePicker(KoanThemeController* controller,
                  KoanThemeController::Space space,
                  const theme::SpaceTheme& theme,
                  base::OnceClosure on_closed);
  KoanThemePicker(const KoanThemePicker&) = delete;
  KoanThemePicker& operator=(const KoanThemePicker&) = delete;
  ~KoanThemePicker() override;

  // Where Zen opens the panel: ZenGradientGenerator.mjs openThemePicker()
  // anchors it to #TabsToolbar (`tabs_in_screen`), "topright topleft" with
  // x -10 ("topleft topright" with the tabs on the right, `on_right`). On
  // macOS Firefox shows it as an NSPopover beside that edge (Zen's
  // widget.macos.native-popovers), elsewhere as a XUL popup. Before Show().
  void PlaceBeside(const gfx::Rect& tabs_in_screen, bool on_right);
  // Shows the panel over `parent`.
  void Show(views::Widget* parent);

  // The welcome screen's colours step (Zen's ZenWelcome.mjs): centred over
  // the anchor rect (Zen's openPopup(…, {position: "overlap"})), staying open
  // while the window is used (noautohide, consumeoutsideclicks=false) and on
  // Esc, which the welcome page handles. Before Show().
  void SetWelcomeMode();
  bool welcome_mode() const { return welcome_mode_; }
  // Moves the panel to a new anchor rect, in screen coordinates.
  void SetAnchorRect(const gfx::Rect& anchor);
  // Closes it as a click outside would: the edits are saved. On the welcome
  // screen it fades out first, as Zen's (ZenWelcome.mjs kExit).
  void Close();
  // Whether it is fading out to close.
  bool closing() const { return fade_out_.is_animating(); }
  // Stops the fade-out: the panel stays open.
  void CancelClose();
  // The panel's size on screen (its preferred size until it is shown).
  gfx::Size GetSize() const;
  // The panel's bounds on screen (empty until it is shown).
  gfx::Rect GetBoundsInScreen() const;

  // The theme as edited so far.
  theme::SpaceTheme GetTheme() const;

  // The window's scheme or theme changed: recolour the panel.
  void OnEnvironmentChanged();

  views::Widget* widget() const { return widget_.get(); }

 private:
  void OnWidgetClosed();
  // On macOS: the popover material behind the panel, at its bounds, in its
  // scheme.
  void UpdateMaterial();

  // views::WidgetObserver:
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;
  void OnWidgetDestroying(views::Widget* widget) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;
  void AnimationCanceled(const gfx::Animation* animation) override;

  const raw_ptr<KoanThemeController> controller_;
  base::OnceClosure on_closed_;
  // The window scheme the panel shows.
  std::optional<KoanThemeController::Scheme> shown_scheme_;
  bool shown_dark_ = false;
  std::unique_ptr<views::BubbleDialogDelegate> delegate_;
  raw_ptr<KoanThemePickerView> view_ = nullptr;
  std::unique_ptr<views::Widget> widget_;
  bool welcome_mode_ = false;
  gfx::LinearAnimation fade_out_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_THEME_PICKER_H_
