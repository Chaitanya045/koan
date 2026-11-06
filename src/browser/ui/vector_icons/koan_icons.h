// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VECTOR_ICONS_KOAN_ICONS_H_
#define KOAN_BROWSER_UI_VECTOR_ICONS_KOAN_ICONS_H_

#include "base/containers/span.h"

namespace gfx {
struct VectorIcon;
}

namespace koan {

// Koan's icon registry: Zen's icon vocabulary (`back`, `reload`, `sidebar`,
// ... see .agent/notes/koan-icons.md) in free Lucide glyphs, as
// koan::kKoan<ZenName>Icon (koan/browser/ui/vector_icons/vector_icons.h).
//
// Views code that picks an icon of Zen's vocabulary uses those directly. All
// other code, upstream's included, keeps asking for Chromium's Material
// icons; InstallVectorIconOverride() makes every such icon that has a
// Zen-vocabulary equivalent paint as the Koan icon (a table in
// koan_icons.cc), through a hook in gfx::CreateVectorIcon and
// gfx::PaintVectorIcon (patches/ui/gfx/paint_vector_icon.cc.patch). Sizes
// and colours stay the caller's.

// One row of the table: a Chromium icon and the Koan icon painted for it.
struct KoanIconEntry {
  const gfx::VectorIcon* chromium;
  const gfx::VectorIcon* koan;
};

// The whole table, for tests.
base::span<const KoanIconEntry> KoanIconTable();

// The icon to paint instead of `icon`: its Koan equivalent, or `icon` itself.
const gfx::VectorIcon& KoanIconFor(const gfx::VectorIcon& icon);

// Turns the substitution on. Called once, early in the browser process
// (koan::RegisterFeatureOverrides()). In builds with DCHECKs,
// --koan-icon-trace=<file> appends every icon the UI asks for to that file
// the first time it is seen at a size, as "<icon>\t<status>\t<dip size>":
// status `mapped:<koan icon>`, `koan` (already a Koan icon) or `unmapped`
// (still Chromium's), for the e2e tests.
void InstallVectorIconOverride();

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VECTOR_ICONS_KOAN_ICONS_H_
