# Zen behaviour scripts

Step-by-step interaction scripts for Koan threads (to implement) and the Mac
loop (to test): **input -> exactly what Zen does -> timing / animation -> edge cases**,
plus the prefs involved. One file per feature, in the order of issue #114.

Sources: Zen 1.24b open-source repo (`~/work/koan/.cache/zen-desktop-1.24b/`, paths
quoted as `src/...`), the 1.24b app bundle, and the live Zen measurements in
[../zen-mac-spec.md](../zen-mac-spec.md). Every claim says which one it comes from.
Lines marked **LIVE CHECK** were read from source but not yet observed in the
running app (the screen was not available): the zen-ref thread verifies them at the
next screen slot and updates the file.

Each file ends with a **test script** the Mac loop can run in both Zen and Koan.

| File | Feature |
|---|---|
| [close-last-tab-and-empty-tab.md](close-last-tab-and-empty-tab.md) | closing the last tab, the empty tab |
| [cmd-t-cmd-l-session.md](cmd-t-cmd-l-session.md) | the floating URL bar: Cmd+T / Cmd+L sessions |
| [compact-mode.md](compact-mode.md) | compact mode: toggle, hover reveal, popups, fullscreen |
| [spaces.md](spaces.md) | Spaces: switching, animation, swipe, create/rename/delete, Clear |
| [essentials-and-pinned.md](essentials-and-pinned.md) | Essentials grid, pinned tabs, reset button, close behaviours |
| [folders.md](folders.md) | folders: create, expand/collapse, active tab, reset, search popup, menu, drag/drop |
| [split-view.md](split-view.md) | split view: create (shortcuts, menu, drag), resize, rearrange, unsplit, sidebar row |
| [glance.md](glance.md) | Glance: Option-click activation, 350 ms arc animation, side buttons, expand, split, close/confirm |
| [fullscreen-and-notch.md](fullscreen-and-notch.md) | native macOS fullscreen (notch strip, borderless compact) and DOM fullscreen |
| [theme-picker.md](theme-picker.md) | theme picker: wheel, dots and harmonies, presets, opacity, grain, scheme, saving |
| [library.md](library.md) | Library: slide-over shell, rail and customize, History, Downloads (+ button stack), Spaces cards |
