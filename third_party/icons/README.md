# Koan's icon set

Koan's UI icons are [Lucide](https://lucide.dev) icons (ISC, see
`LICENSE-lucide`), used in Zen's icon vocabulary: `mapping.txt` says which
Lucide icon stands in for each of Zen's icon names (`back`, `sidebar`,
`reload`, ...), and `scripts/gen_koan_icons.py` converts the vendored SVGs to
Chromium vector icons (`src/browser/ui/vector_icons/koan_<zen name>.icon`,
`koan::kKoan<ZenName>Icon`). Only the SVGs in use are vendored. The choice and
the full table are in `.agent/notes/koan-icons.md`.

Zen's own icons are Nucleo icons, which can't be redistributed. Never copy,
trace or ship one.

Update: `scripts/gen_koan_icons.py --lucide <lucide-static>/icons`, then
`scripts/icon_check.py` (needs `skia-pathops fonttools pycairo pillow`).
