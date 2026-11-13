// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_TAB_DRAG_IMAGE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_TAB_DRAG_IMAGE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/vector2d.h"

class BrowserView;

namespace views {
class View;
class Widget;
}  // namespace views

namespace koan {

class KoanTabDragImageView;

// The image of Koan's tab drag (koan_tab_drag.h), Zen's drag image
// (ZenDragAndDrop.js #createDragImageForTabs()): an opaque copy of the
// dragged tab's row, the label cut short, with up to two more rows stacked
// behind it and the count of dragged tabs on a dot when there are several.
// The drag never leaves the window, so no OS drag session draws it: it is a
// view on a layer in a child widget over the whole window (above the page,
// which on macOS is a view of its own), moved by the layer's transform, and
// it glides into the dropped tab's slot (Zen's landing on macOS,
// nsZenDropLandingCocoa.mm).
class KoanTabDragImage : public gfx::AnimationDelegate {
 public:
  // One row of the image: what the tab's row shows, in its own coordinates.
  struct Row {
    Row();
    Row(const Row&);
    Row& operator=(const Row&);
    ~Row();

    gfx::Size size;
    ui::ImageModel icon;
    gfx::Rect icon_bounds;
    std::u16string title;
    gfx::Rect title_bounds;
    gfx::FontList font;
    float corner_radius = 0;
  };

  // How long the image takes to glide into its slot, easing out. Zen leaves
  // this to AppKit's drag session (animatesToDestination), so it has no value
  // of its own: a Mac step compares the two.
  static constexpr int kLandingMs = 200;

  explicit KoanTabDragImage(BrowserView* browser_view);
  KoanTabDragImage(const KoanTabDragImage&) = delete;
  KoanTabDragImage& operator=(const KoanTabDragImage&) = delete;
  ~KoanTabDragImage() override;

  // Shows `rows` (the grabbed tab's first) for `count` dragged tabs, held at
  // `grab` in the first row (DragImageGrab()), the pointer at `screen_point`.
  void Show(std::vector<Row> rows,
            size_t count,
            const gfx::Vector2d& grab,
            const gfx::Point& screen_point);
  void MoveTo(const gfx::Point& screen_point);
  // Over the page's split edge the image is Zen's card (the first row's icon
  // and title, #zen-split-view-drag-image), at once; else the rows.
  void SetCard(bool card);
  // Zen's fake pane (#zen-split-view-fake-browser) at `bounds` (screen
  // coordinates), opening from `edge` (its SplitEdge, koan_space_drag_logic.h:
  // 0 left, 1 right, 2 top, 3 bottom) in 100 ms; empty bounds close it the
  // same way.
  void ShowFakePane(const gfx::Rect& bounds, int edge);
  // Glides the first row onto `target` (the tab's row in screen coordinates,
  // asked again on every frame as the sidebar settles; nullopt: it went), then
  // hides and runs `done`. Without animations (reduced motion) it hides at
  // once.
  using TargetCallback = base::RepeatingCallback<std::optional<gfx::Rect>()>;
  void Land(TargetCallback target, base::OnceClosure done);
  void Hide();
  bool IsShowing() const;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

 private:
  // Places the image's first row's origin at `origin`, in screen coordinates.
  void PlaceAt(const gfx::Point& origin);
  void FinishLanding();
  // The fake pane's bounds `value` of the way open.
  void LayoutFakePane(double value);

  const raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<KoanTabDragImageView> view_ = nullptr;
  gfx::Vector2d grab_;
  // The first row's origin, in screen coordinates.
  gfx::Point origin_;
  gfx::Point landing_from_;
  TargetCallback landing_target_;
  base::OnceClosure landing_done_;
  gfx::SlideAnimation landing_{this};
  raw_ptr<views::View> fake_pane_ = nullptr;
  gfx::Rect fake_pane_bounds_;
  int fake_pane_edge_ = 0;
  gfx::SlideAnimation fake_pane_animation_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_TAB_DRAG_IMAGE_H_
