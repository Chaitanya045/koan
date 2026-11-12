// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_RENDERER_KOAN_RENDER_THREAD_OBSERVER_H_
#define KOAN_RENDERER_KOAN_RENDER_THREAD_OBSERVER_H_

namespace content {
class RenderThread;
}

namespace koan {

// Adds Koan's observer of the renderer's main thread to `thread`: it takes
// the settings the browser pushes (koan/common/koan_renderer_settings.mojom).
// Called by the hook in ChromeContentRendererClient::RenderThreadStarted().
void AddRenderThreadObservers(content::RenderThread* thread);

}  // namespace koan

#endif  // KOAN_RENDERER_KOAN_RENDER_THREAD_OBSERVER_H_
