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
