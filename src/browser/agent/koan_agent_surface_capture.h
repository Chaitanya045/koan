// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_SURFACE_CAPTURE_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_SURFACE_CAPTURE_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "base/time/time.h"
#include "koan/browser/agent/koan_agent_capture.h"

class SkBitmap;

namespace content {
class WebContents;
}

// Koan.captureScaled's browser side (koan_agent_capture.h): copies the frame
// the compositor already drew for a tab, scaled on the GPU to the image's
// size (RenderWidgetHostView::CopyFromSurface, the path tab thumbnails and
// previews use: a viz CopyOutputRequest with a scale ratio), paints the
// masks over it and encodes it on a worker thread. No device emulation, no
// new layout or raster, nothing the page can see.
//
// A hidden or covered agent tab renders only while its lease holds the
// capturer (.agent/notes/agent-control.md section 8): the copy runs as a
// command does (AgentLeaseManager::RunCommand), and if the tab was not
// drawing, it waits for a fresh frame (RenderFrameHost::
// InsertVisualStateCallback) before it copies. With no surface within
// kSurfaceWait the copy fails (SurfaceStatus::kNoSurface) and the facade
// decides what to do (ChooseFallback()).
namespace koan::agent {

// How long a copy waits for the tab to have a surface and a fresh frame, and
// for the copy itself.
inline constexpr base::TimeDelta kSurfaceWait = base::Milliseconds(1000);
inline constexpr base::TimeDelta kCopyTimeout = base::Milliseconds(1500);

// The view's size and scale; nullopt if the tab has no view.
std::optional<SurfaceGeometry> SurfaceGeometryOf(
    content::WebContents* contents);

// Copies and encodes; `done` runs once, on the UI thread, also when the tab
// goes away meanwhile.
void CaptureSurface(content::WebContents* contents,
                    SurfaceCaptureJob job,
                    SurfaceCaptureCallback done);

// The worker thread's part: the masks painted (opaque) over a copy of
// `bitmap`, then encoded. nullopt if it could not be.
std::optional<std::vector<uint8_t>> EncodeCapture(
    const SkBitmap& bitmap,
    ImageFormat format,
    int quality,
    const std::vector<CaptureRect>& masks);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_SURFACE_CAPTURE_H_
