# Agent benchmark (spec only; no harness code yet)

Measures whether agent control is faster than other browser tools for the same tasks: wall time, tool calls, tokens
sent to the model, and success. The full design is `.agent/notes/agent-control.md` section 20.7; this file is the
layout it commits to. Phase 2 builds it, before the features it judges.

```
README.md          this file
site/              local deterministic test site on two origins (127.0.0.1:A, localhost:B); records every
                   submission server-side, so success is decided by the site, never by the agent's claim
tasks/*.json       16 local task specs (+ tasks/live/: 5 pinned live-web tasks, which need network access and are
                   part of the headline): goal text, start state, verifier, limits
fixtures/          saved static pages for the micro layer (our own pages; never a vendor's files)
adapters/<target>/ koan, koan-ab, chrome-cdp, agent-browser, ego (external, opt-in)
runner/            spawns a target, runs scripted-oracle or LLM-agent mode, collects metrics
report/            merges runs and baselines/*.json into a CSV and a markdown table
baselines/         externally measured numbers (for example the Mac thread's ego(lite) black-box results)
results/           git-ignored raw runs, transcripts and verifier records
```

Rules: the ego(lite) run is **mandatory for the "beats ego" headline** (the user has consented; zen-ref runs it on the
Mac, in a dedicated macOS user account or VM with a before/after snapshot of the paths its CLI touches, black-box only).
It runs only with `--target ego` and `EGO_APPROVED=1`, and nothing downloads or runs it by default. Every target runs
headed; targets run interleaved within each task block and the report shows paired differences with bootstrap CIs. No vendor code, scripts, prompts or skill text are copied into this
directory.
