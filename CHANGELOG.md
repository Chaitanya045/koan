# Changelog

All notable changes to Koan. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and Koan uses
[Semantic Versioning](https://semver.org/). Each release notes the Chromium
version it is built on.

## [Unreleased]

## [0.1.0] - 2026-10-19

First release. Built on Chromium 158.0.8111.0.

### Added
- Zen-style sidebar: vertical tabs by default, sidebar and toolbar on one
  surface, the page as a floating rounded card.
- Compact mode (Ctrl+S): sidebar and toolbar hide and slide back in from the
  window edges; Ctrl+Alt+S peeks the sidebar.
- Single-toolbar layout (Ctrl+Alt+B): navigation and the address in the
  sidebar, a floating URL bar on Ctrl+L.
- Workspaces on tab groups: switcher at the bottom of the sidebar,
  Ctrl+Alt+Left/Right, per-workspace colour tint, restored on restart.
  Essentials are the pinned-tab grid shared by all workspaces.
- Glance (Alt+click a link): a floating preview with close, expand to a tab
  (Ctrl+O) and open in split view.
- Zen's default keyboard shortcuts (split view, copy URL, pin, close unpinned
  tabs and more); see docs/USAGE.md.
- Koan branding: name, ensō logo, `~/.config/koan` profile, desktop entry.
- Every Koan feature can be switched off in chrome://flags.
