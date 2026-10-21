# Running several agents in parallel

How the issue-per-thread work on Koan is organised, and the traps found
running it on this machine (8 cores, 15.8 GB RAM).

## Setup

- **One issue per thread.** Each issue gets its own top-level T3 Code thread,
  bound to its own git worktree and branch (`~/.t3/worktrees/koan/<branch>`).
  The thread gets [agent-brief.md](agent-brief.md) and the issue number, and
  works until its PR is open.
- **Model:** Claude Opus 5.5 at effort `high` (`claudeAgent` provider). The
  first wave ran on OpenCode GLM 5.3 Flash and was replaced; see
  [subagents.md](subagents.md).
- **The orchestrating thread** reviews each PR, sends the findings back to
  the PR's thread, merges once CI and the e2e tests pass, and tells the other
  threads to rebase. A scheduled check-in every 45 minutes keeps this going.
  Reviews are saved to [reviews/](reviews/).

## One Chromium checkout, one out dir

Every worktree shares `~/work/koan/chromium/src` and `out/Koan`. Builds go
through `~/work/koan/.cache/agent-build.sh <worktree> [e2e tests…]`, which
runs under an exclusive lock and:

1. Resets the upstream files patched by the previous build, applies the
   worktree's `patches/`, and points `chromium/src/koan` at the worktree's
   `src/`.
2. Builds the dev build and runs the given e2e tests (`AGENT_CMD` runs
   extra commands, such as screenshots, under the lock).

Traps, all fixed in the script:

- **Stale objects.** The build decides what to recompile by mtime. After
  `//koan` switches to another worktree, files older than the objects built
  from the previous worktree aren't recompiled. The symptom is bogus
  "undefined symbol" link errors. The script keeps a copy of the last-built
  `src/` (`.cache/last-built-src`) and touches every file that differs, plus
  any file a failed build left uncompiled (`.cache/last-built-dirty`).
- **Starvation.** `flock` isn't fair: waiters race when the lock is
  released, and one waited four hours. The script now queues
  first-come-first-served (`.cache/build.queue`, pruning dead PIDs) before
  taking the lock.
- **Out of memory.** 13 Claude sessions plus a 6-job build plus e2e browsers
  got the machine OOM-killed and rebooted. The rules now:
  - at most **4 active threads**;
  - compile jobs follow free memory (6 jobs from 9 GB available, 4 from
    6 GB, otherwise 2);
  - resume a paused thread only when at least 5 GB is available.
- **Editing the script while builds wait** is safe: running instances keep
  the old, deleted inode. But they run the old logic, so kill them and have
  the threads rerun.
- **Tests, not compiles, hold the lock.** With 6 agents, most lock time went to each agent's 10–17 e2e suites while the CPU idled. Rule: run only your feature's suites while iterating; the full regression set once before pushing for review.
- **Orphans holding the lock.** A child started under the lock inherits its file descriptor. If the child outlives `agent-build.sh` (a hung screenshot browser did), it holds the lock for everyone. Tests and `AGENT_CMD` now run with `8>&- 9>&-`, and `AGENT_CMD` has a 30-minute timeout. Agents must never run Koan in a separate background job.
- **Killing a thread's build wakes the thread.** Its background-task
  notification fires, and it may queue a new build. Tell a paused thread to
  stay paused before you kill its build.

## Losing state

- `/tmp` is wiped on reboot. Agents keep logs and scratch files under
  `~/work/koan/.cache/` or their worktree.
- After a reboot or a network outage (`EAI_AGAIN`), threads that were
  waiting on a background build are never woken: send each one a resume
  message.
- Live orchestration state (thread IDs, the active and paused lists, the
  build queue) stays in `~/work/koan/.cache/` (`threads-opus.txt`,
  `thread-plan.md`). It changes every check-in and is machine-specific, so
  it isn't kept in the repo.
