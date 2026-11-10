// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_CLIENT_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_CLIENT_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "koan/browser/ui/views/koan_window_sync.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;

namespace koan {

// One window's side of Zen's window sync (KoanWindowSync): registers a
// synced window with its profile's sync, reports what the user changes in it
// and reconciles it to the synced state. Owned by KoanBrowserViewController,
// for every window; it does nothing in a window that isn't synced (see
// KoanWindowSync::ShouldSync()).
class KoanWindowSyncClient : public KoanWindowSync::Window,
                             public views::WidgetObserver {
 public:
  explicit KoanWindowSyncClient(BrowserView* browser_view);
  KoanWindowSyncClient(const KoanWindowSyncClient&) = delete;
  KoanWindowSyncClient& operator=(const KoanWindowSyncClient&) = delete;
  ~KoanWindowSyncClient() override;

  // The client of `browser_view`'s window if it is synced, else null.
  static KoanWindowSyncClient* From(const BrowserView* browser_view);

  // After the window's controllers are set up (its Spaces in particular).
  void OnAddedToWidget();
  // Before they go: the window leaves the sync.
  void OnBrowserViewDestroying();

  // Whether the window takes part in the sync.
  bool is_synced() const { return sync_ != nullptr; }
  // Whether the sync is changing this window right now (KoanWindowSync::
  // IsApplying()): its controllers leave what changes alone.
  bool IsApplying() const;

  // KoanWindowSync::Window:
  KoanWindowSync::WindowId window_id() const override;

  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  const raw_ptr<BrowserView> browser_view_;
  // Set while the window is synced.
  raw_ptr<KoanWindowSync> sync_ = nullptr;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_CLIENT_H_
