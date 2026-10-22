// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_DOWNLOAD_ANIMATION_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_DOWNLOAD_ANIMATION_H_

#include <memory>
#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "content/public/browser/download_manager.h"
#include "ui/events/event_handler.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

class BrowserView;

namespace views {
class View;
class Widget;
}  // namespace views

namespace koan {

// Zen's download animation (ZenDownloadAnimation.mjs) for one window: when
// a download starts, a round download icon flies along an arc from where
// the pointer was last pressed to the downloads button. When that button
// isn't showing (single toolbar, the default, or a hidden toolbar) a
// downloads tile slides in at the bottom of the sidebar side instead, and
// the icon lands on it.
//
// Both draw in a short-lived child widget over the window that takes no
// input. The tile is on the sidebar's side (right with tabs on the right).
class KoanDownloadAnimation : public content::DownloadManager::Observer,
                              public ui::EventHandler {
 public:
  explicit KoanDownloadAnimation(BrowserView* browser_view);
  KoanDownloadAnimation(const KoanDownloadAnimation&) = delete;
  KoanDownloadAnimation& operator=(const KoanDownloadAnimation&) = delete;
  ~KoanDownloadAnimation() override;

  // Whether downloads animate (Koan's feature and Zen's pref).
  bool IsEnabled() const;

  // Called by KoanBrowserViewController.
  void OnAddedToWidget();
  void OnBrowserViewLayout();
  void OnBrowserViewDestroying();

  // content::DownloadManager::Observer:
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void ManagerGoingDown(content::DownloadManager* manager) override;

  // ui::EventHandler (pre-target on the root view, for the last press on
  // the browser's own views):
  void OnMouseEvent(ui::MouseEvent* event) override;

  // A press on the page (KoanWindowDragController sees those), in screen
  // coordinates.
  void OnPagePressed(const gfx::Point& point_in_screen);

 private:
  // Plays the animation in this window.
  void Play();
  // Whether `item` belongs to this window.
  bool IsForThisWindow(download::DownloadItem* item) const;
  views::View* EnsureOverlay();
  // Where the tile rests, in BrowserView coordinates.
  gfx::Rect TileBounds() const;
  // The tile's horizontal offset from where it rests when it is
  // `edge_distance` from the window's edge (negative: outside).
  float TileOffset(int edge_distance) const;
  void LayoutOverlay();
  void PlayArc(const gfx::Point& start, const gfx::Point& end);
  // The downloads tile at the bottom of the sidebar side.
  void ShowTile();
  void HideTile();
  void OnAnimationDone();

  const raw_ptr<BrowserView> browser_view_;
  base::ScopedObservation<content::DownloadManager,
                          content::DownloadManager::Observer>
      download_manager_observation_{this};
  raw_ptr<views::View> root_view_ = nullptr;
  // Where the pointer was last pressed, in BrowserView coordinates.
  std::optional<gfx::Point> last_press_;

  std::unique_ptr<views::Widget> overlay_;
  raw_ptr<views::View> tile_ = nullptr;
  base::OneShotTimer tile_timer_;
  int running_ = 0;

  base::WeakPtrFactory<KoanDownloadAnimation> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_DOWNLOAD_ANIMATION_H_
