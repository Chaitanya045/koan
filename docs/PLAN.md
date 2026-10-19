# Koan plan

Chromium fork. Hot-path UI in native C++ Views (GPU-composited ui::Layer
animations); rarely used screens (settings, workspace editor, theme picker) as
lightweight WebUI (Lit, no React).

Koan code lives in `chromium/src/koan/`. Upstream files are touched Brave-style:
a `koan/chromium_src/` shadow tree first, minimal `patches/` as a last resort,
so each 4-weekly Chromium rebase stays mechanical. Every Koan surface sits
behind a Koan-owned `base::Feature`. See `research-upstream.md` for sources.

## What upstream already gives us

- **Vertical tabs** — launched (`VerticalTabStripRegionView`,
  `tabs::VerticalTabStripStateController`): collapse, expand-on-hover, resize,
  session persistence. Koan reskins/extends it.
- **Split view** — launched (`MultiContentsView`,
  `TabStripModel::AddToNewSplit`). Koan consumes it.
- **Toolbar auto-hide** — Linux immersive mode is a stub; Koan writes its own
  controller on the `ImmersiveModeController` pattern.
- **Glance, workspaces, Essentials** — no upstream equivalent; Koan-native.

## Phases

0. **Bootstrap** — depot_tools, shallow checkout, hooks, first build of stock
   Chromium as `out/Koan`; `koan/` source tree and `chromium_src` override
   mechanism wired into the build.
1. **Sidebar + compact mode** — Zen-style skin and behaviour on the upstream
   vertical tab strip; compact mode auto-hiding sidebar and toolbar with slide
   animations (Koan immersive controller on Linux).
2. **Workspaces + Essentials** — tab-collection layer over `TabStripModel`
   with swipe switching; pinned Essentials grid at the top of the sidebar.
3. **Split view + Glance** — Zen UX on upstream split view; Glance overlay
   hosting a `WebContents` over the contents area.
4. **Theming + polish** — gradient themes, branding (name, icons, about page),
   rebase tooling (`check_chromium_src`-style validator).

## Status

- [x] Phase 0 — checkout (Chromium 158.0.8111.0, main @ 67e29b8) and first stock build (~15 h, finished 2026-10-19)
- [x] Upstream research (`research-upstream.md`)
- [x] Phase 1 — sidebar look (vertical tabs default, frame-coloured toolbar,
      floating content card), compact mode (zero-width sidebar overlay on the
      leading edge, toolbar in the NonClientView overlay sliding on a
      compositor transform, Ctrl+S like Zen, persisted). Design: `phase1-design.md`.
- [x] Phase 1b — single toolbar (Ctrl+Alt+B): no top toolbar, navigation
      and a URL pill in the sidebar, floating URL bar on focus.
- [x] Phase 2 — workspaces on upstream tab group focus mode (switcher, keys,
      new tabs join the active workspace, restored on restart, per-workspace
      tint); Essentials = upstream pinned-tab grid. Research:
      `phase2-research.md`.
- [x] Phase 3 — Glance (Shift+click; expand to tab / split / close),
      upstream split view kept. Research: `phase3-research.md`.
- [x] Phase 4 — branding (name, logos, profile dir, desktop id, launcher),
      stable feature defaults, rebase tooling (`REBASE.md`,
      `update_chromium.sh`).

Deviations from the original plan, and why, are in
`.agent/notes/decisions.md`: hook patches instead of a `chromium_src`
shadow tree (no global include changes, no full rebuilds); Glance on
Shift+click instead of Alt+click (Alt+click is a renderer-side download);
workspace themes via Chromium's user colour instead of custom gradients.

Known gaps are tracked in `.agent/notes/status.md`.
