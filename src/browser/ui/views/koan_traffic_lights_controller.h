// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_TRAFFIC_LIGHTS_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_TRAFFIC_LIGHTS_CONTROLLER_H_

#include <optional>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "ui/views/view_observer.h"

class BrowserView;

namespace koan {

// Keeps the macOS traffic lights where Koan wants them (koan_traffic_lights.h):
// has AppKit lay them out again when the height they are centred in changes
// (compact mode, single toolbar), since AppKit asks for it only when the title
// bar is laid out; and tells when the sidebar appears or goes, since the
// lights belong to it, and when the lights were laid out again (AppKit may
// show them then).
class KoanTrafficLightsController : public views::ViewObserver {
 public:
  // `on_changed` runs when the sidebar gets or loses its width, and after the
  // lights were laid out again: the caller then shows or hides them.
  KoanTrafficLightsController(BrowserView* browser_view,
                              base::RepeatingClosure on_changed);
  KoanTrafficLightsController(const KoanTrafficLightsController&) = delete;
  KoanTrafficLightsController& operator=(const KoanTrafficLightsController&) =
      delete;
  ~KoanTrafficLightsController() override;

  // After the browser view was laid out.
  void OnBrowserViewLayout();

  // Whether the sidebar takes room (compact mode hides it to nothing).
  bool IsSidebarShown() const;

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewVisibilityChanged(views::View* observed_view,
                               views::View* starting_view,
                               bool visible) override;
  void OnViewIsDeleting(views::View* observed_view) override;

 private:
  void ObserveSidebar();
  void HideStrayOverlays();
  void NotifyIfSidebarChanged();
  void LayOutTrafficLights();

  const raw_ptr<BrowserView> browser_view_;
  const base::RepeatingClosure on_changed_;
  base::ScopedObservation<views::View, views::ViewObserver>
      sidebar_observation_{this};
  bool sidebar_shown_ = false;
  // What AppKit was last told to centre the lights in.
  std::optional<int> applied_height_;
  bool update_pending_ = false;
  base::WeakPtrFactory<KoanTrafficLightsController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_TRAFFIC_LIGHTS_CONTROLLER_H_
