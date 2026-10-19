# Updating Koan to a new Chromium

Chromium ships a milestone every 4 weeks. Koan stays small on purpose so that
moving to a new revision is mechanical:

- All Koan code lives in `src/` (seen by GN as `//koan`).
- Upstream files carry one-to-a-few-line **hook patches**, one file per
  patched upstream file in `patches/`. Each calls into `//koan`.
- Bulk branding (product name strings, `BRANDING`, logos) is applied by
  `scripts/apply_branding.sh` rather than stored as patches.
- Upstream feature defaults Koan relies on are set in one place,
  `src/browser/koan_feature_overrides.cc`.

## Procedure

```sh
scripts/update_chromium.sh --check          # will today's patches apply?
scripts/update_chromium.sh 159.0.8150.0     # un-brand, un-patch, sync, re-apply
scripts/build.sh                            # most of the tree rebuilds
```

`update_chromium.sh` stops if a patch conflicts. Port that hook by hand in
`chromium/src`, then `scripts/apply_patches.sh export` and continue with
`scripts/apply_branding.sh`.

## After every rebase, re-audit the hooks

`update_chromium.sh` prints every `koan::` call site in `chromium/src`.
For each, check the surrounding upstream code still means what the hook
assumes:

| Hook | File | Assumption to re-check |
|---|---|---|
| BrowserView created/destroyed | `chrome/browser/ui/views/frame/browser_view.cc` | end of ctor: children exist, no widget yet; start of dtor: children alive |
| Compact sidebar width | `.../frame/layout/browser_view_tabbed_layout_impl.cc` (`CalculateHorizontalLayout`) | collapsed branch still sizes the strip; expand-on-hover still overlays |
| Content card | `.../frame/multi_contents_view.cc` | single-contents layout, separators and radii code paths unchanged |
| Toolbar overlay (no patch) | `BrowserView::ReparentTopContainerFor{Start,End}OfImmersive` | Koan's compact controller mirrors these; diff them |
| Overlay view lookup (no patch) | `TopContainerOverlayView` class name in `browser_view.cc` | still a NonClientView child with that metadata name |
| Workspace switcher mount (no patch) | `VerticalTabStripRegionView` ctor | content area is still a `FlexLayoutView` child |
| Glance trigger | `chrome/browser/ui/browser_web_contents_delegate/...cc` | Shift+click still arrives as renderer-initiated `NEW_WINDOW` |
| Colour mixer | `chrome/browser/ui/color/chrome_color_mixers.cc` | Koan mixer still runs after Chrome's, before custom themes |
| Feature defaults | `chrome/browser/chrome_browser_field_trials.cc` | `RegisterFeatureOverrides` still exists; `kTabGroupsFocusing` still the focus-mode switch |
| Prefs | `chrome/browser/ui/tabs/tab_strip_prefs.cc` | vertical-tabs pref name and registration |
| Startup, infobar, paths, desktop id | `session_startup_pref.cc`, `infobar_utils.cc`, `chrome_paths_linux.cc`, `channel_info_posix.cc`, `shell_integration_linux.cc` | one-line value swaps |
| Flags | `chrome/browser/about_flags.cc` | `kFeatureEntries` end marker |

Then run the smoke tests (all off-screen, see `.agent/notes/build.md`):

```sh
scripts/shot_ui.sh /tmp/k.png -- https://example.com            # look
PREFS='{"koan":{"compact_mode":{"enabled":true}}}' \
  scripts/shot_ui.sh /tmp/c.png -- https://example.com           # compact
```

and the interactive ones (`ui_session.sh` + `ui_input.mjs`, `run_profile.sh` +
`cdp_click.mjs`) for workspaces, Glance and session restore.
