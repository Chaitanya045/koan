# Koan plan

Chromium fork. Hot-path UI in native C++ Views (GPU-composited ui::Layer
animations); rarely used screens (settings, workspace editor, theme picker) as
lightweight WebUI (Lit, no React).

Koan code lives in `chromium/src/koan/` plus minimal, isolated hooks into
upstream files, so each 4-weekly Chromium rebase stays small. Prefer building
on upstream features (Chrome's split view, vertical tabs experiments) over
replacing them.

## Phases

0. **Bootstrap** — depot_tools, shallow checkout, hooks, first build of stock
   Chromium as `out/Koan`.
1. **Sidebar + compact mode** — vertical tab sidebar replacing the horizontal
   tab strip; compact mode that auto-hides sidebar and toolbar with slide
   animations.
2. **Workspaces + Essentials** — per-workspace tab sets with swipe switching;
   pinned Essentials grid at the top of the sidebar.
3. **Split view + Glance** — multi-pane tab layouts; Glance link preview
   overlay.
4. **Theming + polish** — gradient themes, branding (name, icons, about page),
   rebase tooling.

## Status

- [ ] Phase 0 — in progress
