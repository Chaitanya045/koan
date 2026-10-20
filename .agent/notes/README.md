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

## How to work on Koan

- [build.md](build.md): build setup, machine limits, rebuild-cost traps,
  off-screen testing tricks
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

## Records

- [reviews/](reviews/): code reviews, one per PR (`prNN-review.md`)
- [screenshots/](screenshots/): screenshots from development steps (the
  README and docs images are in `docs/screenshots/`)
