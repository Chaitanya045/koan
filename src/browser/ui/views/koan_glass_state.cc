// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_glass_state.h"

#include <map>
#include <memory>
#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/glass_frame_service.h"
#include "koan/browser/koan_switches.h"
#include "ui/compositor/layer.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace koan {

namespace {

// Per window: calls back when IsWindowGlassEnabled() may have changed (the
// glass frame service changes the window's eligibility) or the window goes
// active or inactive.
class WindowGlassObserver : public views::WidgetObserver {
 public:
  WindowGlassObserver(BrowserView* browser_view,
                      views::Widget* widget,
                      GlassFrameService* service)
      : widget_(widget) {
    widget_observation_.Observe(widget);
    active_subscription_ =
        widget->RegisterPaintAsActiveChangedCallback(base::BindRepeating(
            &WindowGlassObserver::Notify, base::Unretained(this)));
    if (service) {
      eligible_subscription_ =
          service->RegisterGlassFrameEligibilityChangedCallback(
              browser_view->browser(),
              base::BindRepeating(
                  [](WindowGlassObserver* self, bool) { self->Notify(); },
                  base::Unretained(this)));
    }
  }

  base::CallbackListSubscription Add(base::RepeatingClosure callback) {
    return callbacks_.Add(std::move(callback));
  }

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  void Notify() { callbacks_.Notify(); }

  raw_ptr<views::Widget> widget_;
  base::RepeatingClosureList callbacks_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
  base::CallbackListSubscription active_subscription_;
  base::CallbackListSubscription eligible_subscription_;
};

std::map<views::Widget*, std::unique_ptr<WindowGlassObserver>>& GetObservers() {
  static base::NoDestructor<
      std::map<views::Widget*, std::unique_ptr<WindowGlassObserver>>>
      observers;
  return *observers;
}

void WindowGlassObserver::OnWidgetDestroying(views::Widget* widget) {
  // Destroys this.
  GetObservers().erase(widget_);
}

}  // namespace

bool IsWindowGlassEnabled(const BrowserView* browser_view) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kTestWindowGlass)) {
    return true;
  }
  // The service exists only where the glass frame is on.
  GlassFrameService* service = GlassFrameService::GetInstance();
  // The service takes a non-const browser; it only reads it.
  return service && browser_view &&
         service->IsBrowserWindowEligible(
             const_cast<BrowserView*>(browser_view)->browser());
}

base::CallbackListSubscription AddWindowGlassObserver(
    BrowserView* browser_view,
    base::RepeatingClosure callback) {
  views::Widget* widget = browser_view->GetWidget();
  if (!widget) {
    return {};
  }
  std::unique_ptr<WindowGlassObserver>& observer = GetObservers()[widget];
  if (!observer) {
    observer = std::make_unique<WindowGlassObserver>(
        browser_view, widget, GlassFrameService::GetInstance());
  }
  return observer->Add(std::move(callback));
}

void MakeTranslucentLayer(views::View* view) {
  if (view && view->layer() && view->layer()->fills_bounds_opaquely()) {
    view->layer()->SetFillsBoundsOpaquely(false);
    view->SchedulePaint();
  }
}

}  // namespace koan
