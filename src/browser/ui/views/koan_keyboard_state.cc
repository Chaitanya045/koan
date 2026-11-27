// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_keyboard_state.h"

#include "base/scoped_observation.h"
#include "ui/aura/env.h"
#include "ui/events/event.h"
#include "ui/events/event_handler.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/types/event_type.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace koan {

namespace {

// Off macOS there is no hardware state to ask for, so the key events tell:
// aura::Env is the parent target of every root window, so its pre-target
// handlers see each key event of every Koan window before it reaches the
// focused window (a page's RenderWidgetHostViewAura, the URL bar, a
// bubble). Upstream's Ctrl+Tab MRU (chrome::CycleToMruTab) keeps no walk, so
// it has nothing like this.
class AuraControlKeyState : public ControlKeyState,
                            public ui::EventHandler,
                            public views::WidgetObserver {
 public:
  explicit AuraControlKeyState(views::Widget* widget) {
    // System priority: before the default handlers, one of which may stop
    // the event.
    aura::Env::GetInstance()->AddPreTargetHandler(
        this, ui::EventTarget::Priority::kSystem);
    if (widget) {
      widget_observation_.Observe(widget);
    }
  }
  AuraControlKeyState(const AuraControlKeyState&) = delete;
  AuraControlKeyState& operator=(const AuraControlKeyState&) = delete;
  ~AuraControlKeyState() override {
    if (aura::Env::HasInstance()) {
      aura::Env::GetInstance()->RemovePreTargetHandler(this);
    }
  }

  // ControlKeyState:
  bool IsDown() const override { return down_; }

  // ui::EventHandler:
  void OnKeyEvent(ui::KeyEvent* event) override {
    switch (event->key_code()) {
      case ui::VKEY_CONTROL:
      case ui::VKEY_LCONTROL:
      case ui::VKEY_RCONTROL:
        // The Control key's own release may still carry its flag.
        down_ = event->type() == ui::EventType::kKeyPressed;
        break;
      default:
        // Any other key says whether Control is held, also after the window
        // was activated with it already down.
        down_ = event->IsControlDown();
        break;
    }
  }

  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override {
    // Focus went to another app (or window), which gets the release.
    if (!active) {
      down_ = false;
    }
  }
  void OnWidgetDestroying(views::Widget* widget) override {
    widget_observation_.Reset();
  }

 private:
  bool down_ = false;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
};

}  // namespace

// static
std::unique_ptr<ControlKeyState> ControlKeyState::Create(
    views::Widget* widget) {
  return std::make_unique<AuraControlKeyState>(widget);
}

}  // namespace koan
