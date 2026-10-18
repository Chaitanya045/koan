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

- [ ] Phase 0 — Chromium checkout downloading
- [x] Upstream research (`research-upstream.md`)
