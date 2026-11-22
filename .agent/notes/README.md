# Notes

Koan's working notes: plans, designs, research, decisions and review
records. They're written mainly for the AI agents and maintainers who work on
Koan. Read the relevant ones before starting work, and update them when you
learn something non-obvious. One topic per file. User and contributor
documentation lives in [`docs/`](../../docs) and
[CONTRIBUTING.md](../../CONTRIBUTING.md).

## Project state

- [status.md](status.md): where things stand, what's in flight, next steps
- [plan.md](plan.md): the original plan and phases
- [decisions.md](decisions.md): architecture decisions and why
- [architecture-review.md](architecture-review.md): the 2026-11-02 review
  (keep Views; fix the Chromium base, the Mac loop and the tests)

## How to work on Koan

- [build.md](build.md): build setup, machine limits, rebuild-cost traps,
  off-screen testing tricks
- [mac.md](mac.md): the macOS build: the Mac's setup, the one-folder
  layout, and consent before anything runs there
- [chromium-releases.md](chromium-releases.md): following Chromium's releases
  (#86): the M158 move, the checkout switch plan, the routine
- [macos-ci.md](macos-ci.md): the nightly macOS build on GitHub Actions:
  the job chain, how the source is fetched, measurements
- [linux-ci.md](linux-ci.md): the nightly Linux build and its Flatpak (a
  proposal from a fork): the chain, the runner, the Flatpak's launcher
- [agent-brief.md](agent-brief.md): the brief every implementation agent gets
- [parallel-agents.md](parallel-agents.md): one thread per issue, the
  shared build lock, memory limits and recovery
- [subagents.md](subagents.md): model choice and provider quirks

## Design and research

- [design-phase1-sidebar-compact.md](design-phase1-sidebar-compact.md):
  sidebar, content card and compact mode
- [research-upstream-chromium.md](research-upstream-chromium.md): what
  upstream Chromium already provides (vertical tabs, groups, split view)
- [research-phase2-workspaces-essentials.md](research-phase2-workspaces-essentials.md):
  workspaces and Essentials
- [research-phase3-glance-split.md](research-phase3-glance-split.md): Glance
  and split view
- [zen-mac-spec.md](zen-mac-spec.md): what Zen 1.24b looks like and does on
  macOS (measured), with screenshots and where Koan differs
- [zen-onboarding-port.md](zen-onboarding-port.md): reusing Zen's welcome as
  a `chrome://koan-welcome` WebUI: API mapping, CSS changes, phases

## Records

- [license-audit.md](license-audit.md): where Koan's code comes from, under which licence
- Early per-PR reviews (#11–#93) were removed on 2026-11-20; they're at [reviews/](https://github.com/Chaitanya045/koan/tree/ac644383c4884c21aaa7a2a7fc23694e5a5859e0/.agent/notes/reviews). Reviews now live as comments on each PR
- [screenshots/](https://github.com/Chaitanya045/koan/tree/c6593d31481c0f789c0574d7a4d2376f98a40644/.agent/notes/screenshots): screenshots from development steps and the Zen
  reference captures, removed from the tree on 2026-11-20 and kept at this
  permalink (the README and docs images are in `docs/screenshots/`)
