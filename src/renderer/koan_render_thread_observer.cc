// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/renderer/koan_render_thread_observer.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "content/public/renderer/render_thread.h"
#include "content/public/renderer/render_thread_observer.h"
#include "koan/common/koan_glance_trigger.h"
#include "koan/common/koan_renderer_settings.mojom.h"
#include "mojo/public/cpp/bindings/associated_receiver_set.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"

namespace koan {

namespace {

// Lives as long as the process (RenderThreadImpl keeps its observers until
// it is destroyed, at exit).
class KoanRenderThreadObserver : public content::RenderThreadObserver,
                                 public mojom::RendererSettings {
 public:
  KoanRenderThreadObserver() = default;
  KoanRenderThreadObserver(const KoanRenderThreadObserver&) = delete;
  KoanRenderThreadObserver& operator=(const KoanRenderThreadObserver&) = delete;
  ~KoanRenderThreadObserver() override = default;

  // content::RenderThreadObserver:
  void RegisterMojoInterfaces(
      blink::AssociatedInterfaceRegistry* associated_interfaces) override {
    associated_interfaces->AddInterface<mojom::RendererSettings>(
        base::BindRepeating(&KoanRenderThreadObserver::Bind,
                            base::Unretained(this)));
  }
  void UnregisterMojoInterfaces(
      blink::AssociatedInterfaceRegistry* associated_interfaces) override {
    associated_interfaces->RemoveInterface(mojom::RendererSettings::Name_);
  }

  // mojom::RendererSettings:
  void SetGlanceActivationMethod(const std::string& method) override {
    SetGlanceTrigger(ParseGlanceTrigger(method));
  }

 private:
  // The browser binds a new remote for each push
  // (koan::UpdateRendererSettings()), dropped once sent.
  void Bind(mojo::PendingAssociatedReceiver<mojom::RendererSettings> receiver) {
    receivers_.Add(this, std::move(receiver));
  }

  mojo::AssociatedReceiverSet<mojom::RendererSettings> receivers_;
};

}  // namespace

void AddRenderThreadObservers(content::RenderThread* thread) {
  static base::NoDestructor<KoanRenderThreadObserver> observer;
  thread->AddObserver(observer.get());
}

}  // namespace koan
