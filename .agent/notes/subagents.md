# Subagent notes

## Current choice (2026-10-19)
Issue threads run on **Claude Opus 5.5, effort `high`** (T3 provider
`claudeAgent`, model `claude-opus-5-5`, options `[{id: "effort", value:
"high"}]`), as top-level T3 threads; see [parallel-agents.md](parallel-agents.md).
The user switched from GLM after its threads stalled: on permission prompts
nobody could answer, and by ending turns mid-task.

## Earlier preference: free/cheap models (delegate via T3 `delegate_task`)
Provider instance `opencode`, reasoning `variant: high`, in this order:
1. `opencode/deepseek-v4.1-flash-free`
2. `opencode/glm-5.3-flash-free`
3. Only when both are unusable: `opencode-go/glm-5.3-flash` (user's OpenCode Go
   subscription).

## Quirks seen on 2026-10-18
- The free endpoints return `Internal Server Error` once an agent run has a
  few documents of context (~25 KB+), even when one-line prompts work. Three
  design attempts died on the second model call. One research task (web
  research, small context per call) succeeded on DeepSeek free.
  → Give free models small, focused tasks; check for partial output.
- Retest at 08:55 UTC the same day: both free models completed a ~45 KB
  read-and-review task in new top-level T3 threads in ~25 s each, no errors.
  The earlier failures were a temporary provider outage, not a context limit.
  If they fail again, retry once after a few minutes before falling back to Go.
- Both free models gave useful design review (they caught two real gaps in
  .agent/notes/design-phase1-sidebar-compact.md §7). GLM's technical claims need checking: it wrongly
  doubted that ui::Layer transform animations are compositor-threaded.
- **T3 model catalog is cached at app start.** `opencode-go/*` models are
  rejected by `delegate_task` (`model_unavailable`) until T3 Code restarts.
- OpenCode auth: `opencode auth login` saved the user's console key under
  provider `opencode` (Zen, pay-as-you-go). The same key was copied to
  `opencode-go` in `~/.local/share/opencode/auth.json` (mode 600), and
  `opencode run -m opencode-go/glm-5.3-flash` works. Never print the key.
- A subagent's `summary` may be just its first sentence; always read the
  output file it was asked to write, and spot-check its claims against source.
- Tell subagents explicitly: read-only under `chromium/src` while a build runs,
  and write output incrementally.
- 2026-10-19: DeepSeek free (high) reviewed Phase 1 step 1 in ~25 min,
  read-only while a build ran. Good-quality review with file:line evidence
  (claims spot-checked: correct). Found 3 real robustness bugs in
  `apply_patches.sh` export/apply. Its T3 `summary` was only its first
  sentence; the useful output was the requested file in `.agent/notes/reviews/`.
- 2026-10-19: DeepSeek free stalled for ~1 h mid-review (no output file)
  and was cancelled; GLM free redid it in ~40 min with a good, verified
  review. Ask for incremental writes and a time budget; check the child
  thread's `updatedAt` (t3_thread_read) to spot stalls early.
- 2026-10-19 (later): both free models stalled on PR reviews (~50-60 min,
  no output). After the T3 restart `opencode-go/glm-5.3-flash` (the user's
  Go subscription, last in the fallback order) is in the catalog and gave
  two high-quality, verified reviews (PR #11, #12) in ~20 min each. Check a
  child thread's `updatedAt` with t3_thread_read; cancel when it stops
  moving for ~15 min and fall back to the next model.
- Deleting a stacked PR's base branch on merge closes the stacked PR, and
  it can't be reopened after a rebase. Retarget stacked PRs to `develop`
  *before* merging their base (or merge without --delete-branch).
