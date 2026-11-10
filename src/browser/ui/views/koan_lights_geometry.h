// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_LIGHTS_GEOMETRY_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_LIGHTS_GEOMETRY_H_

#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rect.h"

namespace koan {

// `bounds` moved clear of `obstacle` (both in the coordinates of a container
// `container_width` wide): where it doesn't touch it, itself; else to the
// right of it where the container has the room, else below it. For the
// sidebar's top row beside the macOS traffic lights (AvoidTrafficLights()).
gfx::Rect MoveClearOf(const gfx::Rect& bounds,
                      const gfx::Rect& obstacle,
                      int container_width);

// The colour of the three dots that stand for the traffic lights while the
// pointer is away from them (Zen's zen.widget.mac.mono-window-controls, on by
// default on macOS): --zen-toolbar-element-bg, the toolbox's text colour at
// 8% over a light toolbox and 15% over a dark one (light text).
SkColor MonoTrafficLightColor(SkColor toolbox_text);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_LIGHTS_GEOMETRY_H_
