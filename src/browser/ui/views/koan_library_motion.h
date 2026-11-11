// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenLibrary.mjs (the openProgress setter, animateProgress()'s spring)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_MOTION_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_MOTION_H_

namespace koan {

// How far Zen's open/close spring (stiffness 720, damping 47, mass 1.2) has
// taken the Library from where it started (0) towards where it goes (1) after
// `seconds`: slightly under-damped, settled after about 0.4 s.
double LibrarySpring(double seconds);

// What the Library's open progress (0 closed, 1 open, a little past either
// while a swipe rubber-bands it) does to the panel and the sidebar, as Zen's
// openProgress setter. Everything here is a compositor property (a layer's
// transform or opacity): nothing is laid out again as the progress changes.
struct LibraryMotion {
  // The panel's offset from where it is open, in pixels towards the
  // Library's edge of the window.
  float panel_offset = 0;
  float panel_opacity = 1;
  // The sidebar's offset towards the same edge, as a fraction of its width.
  float sidebar_offset = 0;
  // Its scale about its centre, and its opacity.
  float sidebar_scale = 1;
  float sidebar_opacity = 1;
};

// `panel_width`: the panel's. `sidebar_extent`: how far the sidebar reaches
// in from the Library's edge of the window (0 if it isn't shown).
// `compact`: compact mode hides the sidebar (Zen's #isCompactMode).
//
// The sidebar goes over the first third of the progress (Zen's
// min(1, progress * 3)):
// - in compact mode it slides out to its edge, and the panel slides in from
//   that edge behind it (Zen: translateX(-100% * (1 - progress))): the
//   sidebar collapses first, then the Library follows the progress;
// - otherwise it fades out and shrinks to 0.96 (Zen's scale and opacity)
//   while the panel grows out of it: its far edge goes from the sidebar's
//   edge to its own width with the progress and it fades in over the same
//   first third. Zen gets that look by moving the page aside by the panel's
//   width less the sidebar's (its far edge ends up where Koan's panel edge
//   is); Chromium draws the page in its own native surface, which layer
//   transforms don't move, so Koan's panel edge moves instead. With no
//   sidebar shown, the panel slides in as in compact mode.
LibraryMotion GetLibraryMotion(float progress,
                               float panel_width,
                               float sidebar_extent,
                               bool compact);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_MOTION_H_
