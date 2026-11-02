# Architecture review (2026-11-02)

A review of whether Koan is engineered the right way, done after the user
asked "Did we engineer this correctly?". The verdict: **keep the
architecture; fix the Chromium base, the Mac loop and the tests.**

Product scope, as the user confirmed the same day: **macOS (Apple Silicon)
only, for the user's own use. No official releases.** The goal is an app that
runs on their Mac and stays in sync with Chromium releases.

## What is right

- **Native C++ Views UI on Chromium.** Chrome's own Mac UI is Views, so Views
  is first-class on macOS. Hot-path UI (sidebar, tabs, Essentials, compact
  mode, URL bar, Glance, split, Spaces, toasts) costs no renderer process
  and no idle repaints, which serves the battery and speed goal.
- **Compared with the alternatives:**
  - Vivaldi's React UI (and Zen's HTML/CSS on Gecko) is easy to restyle but
    heavier. Vivaldi users still report UI lag after its 6.2 "Portal
    Windows" refactor.
  - Edge's "WebUI 2.0" moved only flyouts (favourites, Browser Essentials)
    to slimmed-down web UI, because its earlier React UI was slow. Its frame
    and tab strip stay native Views.
  - Arc and Dia wrap Chromium, packaged as an SDK, in a Swift app. That's
    the most Mac-native look, but it took a dedicated team to build and
    maintain the SDK and rebuild Chrome's features. Not viable for Koan.
- **Hybrid rule** (in decisions.md): C++ Views for anything always visible,
  animated, or used per click or keystroke. WebUI is acceptable for
  occasional, form- or list-heavy pages (settings). Customisation comes
  through design tokens and themes (#48), not CSS mods.
- **Fork structure.** Koan code lives in `//koan`, and upstream is changed
  only by small hook patches: 76 patches, 578 changed upstream lines, against
  about 18.6k lines of Koan C++. That's Brave's model (`src/brave` plus
  patches and `chromium_src` overrides) on a smaller scale. A thin patch
  layer is what keeps Chromium updates cheap. Vivaldi reports security-only
  updates take hours, while a full rebase takes days.
- **State** lives in Chromium's own systems (prefs, tab strip and tab
  groups, session restore with `koan_session_data`, ThemeService and colour
  mixers). There is no parallel store, so extensions, sync-shaped data and
  restore keep working.

## What was wrong, and the actions taken

1. **The Chromium base was an unreleased trunk snapshot**
   (`158.0.8111.0`, main @ 67e29b8). It gets no security fixes. On
   2026-11-02 the Mac channels were Stable 156.0.8078.80, Beta 158.0.8127.2
   and Dev 159.0.8130.0. Action: **#86**.
   - Move to the M158 release branch now. Follow it into Stable and take
     every point release.
   - Rebase onto each new milestone within about 2 weeks of it reaching
     Stable.
   - A scheduled job watches chromiumdash and runs the patch check against
     new versions.
2. **Mac-only product, Linux-only testing.** No Koan code had ever run on a
   Mac, and only 8 files had `IS_MAC` code. Reviews had already found
   Mac-only bugs (#54's Aura include, #58's ⌘T). Actions:
   - **#69** (PR #70): a macOS CI build that produces a runnable,
     ad-hoc-signed `.dmg`. A full build is a chain of about 20 jobs on free
     runners, roughly 1–2 days, and it only shows that the code compiles.
   - **#71 and #72:** Mac UI threads, compile-checked by that CI.
   - A fast Mac build-and-test loop (a self-hosted Mac runner or cloud Mac,
     otherwise the user's MacBook overnight with consent) is a hardware
     decision for the user.
3. **No unit or browser tests.** There were only 31 CDP end-to-end scripts.
   Action: **#87**, adding `koan_unittests` and a small `koan_browsertests`
   for the pure logic and the restore paths.

## Also noted

- **Throughput:** one shared Chromium checkout and `out/Koan` behind
  `agent-build.sh`'s lock serialises every agent's builds. It was accepted
  because of the 15.8 GB of RAM. New threads should prefer work that
  rarely needs the lock: Mac code compiled by CI, audits, docs.
- **Patch strategy:** for future large edits to an upstream file,
  Brave-style `chromium_src` overrides may conflict less than patches.
  Re-evaluate during the #86 rebase.
- **No official releases:** PR #13 (Linux packaging) is closed and issue
  #5 is not planned. The app comes from the Mac CI `.dmg` or a local Mac
  build.

## Sources

- Vivaldi Portal Windows: https://www.thurrott.com/?p=287859
- Vivaldi forum on UI lag: https://forum.vivaldi.net/post/585454
- Vivaldi rebase effort: https://vivaldi.com/?p=441493
- Edge WebUI 2.0:
  https://www.neowin.net/news/microsoft-reveals-how-it-is-trying-to-improve-performance-in-its-edge-browser-with-webui-20/
- Arc, written in Swift on Chromium:
  https://en.wikipedia.org/wiki/Arc_(web_browser)
- Dia, Chromium packaged as an SDK (job posting):
  https://www.builtinnyc.com/job/software-engineer-chromium/10194923
- Brave patching: https://github.com/brave/brave-browser/wiki/Patching-Chromium
- Fork maintenance: https://omaha-consulting.com/how-to-fork-chromium
- Mac release channels: https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Mac
