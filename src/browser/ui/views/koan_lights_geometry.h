// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_LIGHTS_GEOMETRY_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_LIGHTS_GEOMETRY_H_

#include <vector>

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

// What the mono window controls show: AppKit's coloured buttons (opaque) or
// the three dots in their place, never both. AppKit's buttons come back while
// the pointer is over them or the window has a sheet (Zen: not with
// window-modal-open); with the buttons hidden (compact mode), neither shows.
// Only a change of an input changes the result: the view that tracks the
// pointer stays shown whatever this says (it only stops drawing), since hiding
// it would end its own hover and bring the dots straight back.
struct MonoLightsState {
  bool buttons_opaque = false;
  bool dots_drawn = false;
  bool operator==(const MonoLightsState&) const = default;
};
MonoLightsState GetMonoLightsState(bool pointer_over_buttons,
                                   bool has_sheet,
                                   bool buttons_hidden);

// The other platforms' window buttons are the frame's caption buttons, in
// the window's top corners, on the side the desktop's button layout puts them
// (GNOME's default: a close button on the right; KDE's: minimise, maximise and
// close on the right). Where Koan hides the toolbar, the ones on the sidebar's
// side sit in its top row, which keeps room for them, as the macOS traffic
// lights do.

// Whether the caption button at `button` (in a frame `frame_width` wide) is on
// the sidebar's side of the window.
bool IsCaptionButtonOnSidebarSide(const gfx::Rect& button,
                                  int frame_width,
                                  bool sidebar_on_right);

// Whether the strip above the page that holds the caption buttons stays open
// where Koan hides the toolbar, rather than collapsing to the page's gutter
// until the pointer reaches the window's top edge: while one of `buttons` (the
// visible caption buttons, in a frame `frame_width` wide) is on the page's side
// of a sidebar that is shown, so that the desktop's window buttons are always
// there, as the macOS traffic lights are with tabs on the right. A hidden
// sidebar (compact mode) has no top row: then the strip collapses and the top
// edge reveals all of them. Buttons not laid out yet (empty) don't count.
bool KeepsCaptionStripOpen(const std::vector<gfx::Rect>& buttons,
                           int frame_width,
                           bool sidebar_on_right,
                           bool sidebar_hidden);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_LIGHTS_GEOMETRY_H_
