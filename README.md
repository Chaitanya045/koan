# Koan

A Chromium-based browser with a Zen-style UX: vertical tabs, workspaces,
compact mode, split view, Glance and Essentials — built as native C++ (Views)
on top of Chromium for maximum UI performance.

## Layout

```
koan/
├── scripts/       bootstrap, sync and build helpers
├── build/         GN args for Koan builds
├── docs/          plan and design notes
├── depot_tools/   (ignored) Chromium tooling
└── chromium/      (ignored) Chromium checkout; Koan changes live under src/koan
```

## Getting started

```sh
./scripts/bootstrap.sh   # depot_tools + Chromium checkout + hooks (long)
./scripts/build.sh       # configure out/Koan and build the browser
```

The first build takes 10–15 hours on an 8-core / 16 GB machine. Incremental
builds take minutes.
