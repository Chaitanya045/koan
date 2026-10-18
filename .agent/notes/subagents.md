# Subagent notes

## User's model preference (delegate via T3 `delegate_task`)
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
