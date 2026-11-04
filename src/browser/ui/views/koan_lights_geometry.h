// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_LIGHTS_GEOMETRY_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_LIGHTS_GEOMETRY_H_

#include "ui/gfx/geometry/rect.h"

namespace koan {

// `bounds` moved clear of `obstacle` (both in the coordinates of a container
// `container_width` wide): where it doesn't touch it, itself; else to the
// right of it where the container has the room, else below it. For the
// sidebar's top row beside the macOS traffic lights (AvoidTrafficLights()).
gfx::Rect MoveClearOf(const gfx::Rect& bounds,
                      const gfx::Rect& obstacle,
                      int container_width);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_LIGHTS_GEOMETRY_H_
