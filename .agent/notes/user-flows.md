# User flows: test cases for what cycle 19 adds (since cycle 18 at 3ddf93f5)

The user (2026-11-21): "Test it thoroughly like an end user, whatever you have implemented new. Agents' usage also
needs to be tested. Write test cases in the notes for each feature, and automated tests that can satisfy them; for
others do an e2e user flow. For every feature do a user flow. Take screenshots and videos and compress them to 1080
or 720 so that testing doesn't consume more tokens."

This is the catalogue. Every case has an ID, what the person at the Mac does (or tells their agent to do), what they
should see, and what checks it automatically:

- **covered by** names the existing test and its check (the text it prints), or the user flow and its step. A flow's
  step has the same ID as its case. `scripts/mac/video/run_flows.sh` runs the flows and writes `index.md`.
- **needs**: *real* means real input (CGEvent clicks and keys through the bench's gates: the user away 600 s, the
  screen granted, our Koan's pid frontmost, no input but ours). UI DevTools clicks skip the macOS hit test, so they
  never count as proof for a control (the top row's buttons were dead for real clicks while UI DevTools clicks
  passed). *rec* means the case is checked in the recording (run_flows.sh records every flow). *manual* means only a
  person can judge it; those cases point at the `scripts/mac/steps/*.md` step.

## How to run, and what to read

On the Mac bench, the user away, the screen granted (see `scripts/mac/video/README.md` for copying the scripts over):

```sh
scripts/mac/video/run_flows.sh --app ~/work/koan-mac/koan/chromium/src/out/Koan/Koan.app      # all flows
scripts/mac/video/run_flows.sh --only uf_agent_task,uf_resume --app <Koan.app>                    # some
```

It takes the Mac's build/test lock (`$KOAN_MAC_ROOT/tmp/e2e.lock` through `scripts/mac/tools/flock_run.py`: the same
flock(2) lock as the Mac loop's `tmp/stt-probe/flock.py`, `build_official.sh` and the other bench runs) for the
whole run, checks the gates before the run and before each flow, and stops the run when a flow says the user is
back. Each flow runs against its own fresh Koan (launch.sh: a profile under `rec-*`, never the user's), recorded at
30 fps, and quits it by its pid. Out: `$KOAN_MAC_ROOT/uf/<time>/index.md` (read this first), and per flow
`summary.json` (per step: pass/fail, checks, timings, koan calls, screenshots), `flow.log`, `shots/` (within
1920x1080, PNG), `sheets/` (contact sheets at the marks, 1280 wide) and `video.720.mp4`.

A reviewer (or a model) reads `index.md`, then only the screenshots and sheets of the steps that failed or that say
to look. That is what keeps a review cheap: the pictures are compressed with `compress.sh` (below), and the counts
and verdicts are text.

Every flow fails loudly: a step with no check, a check that failed, an exception, a gate that stopped it, or a flow
that died before writing its summary is a FAIL in the index. A case that can't run (the feature isn't in the build,
real input is off) is a SKIP with the reason, never a PASS.

## compress.sh: the review tool

`scripts/mac/video/compress.sh [--720|--1080] [--jpeg] [--sheet] [--check] [-o out] <files>`. Self-test:
`scripts/tests/test_compress.py`.

| | default | option | never |
|---|---|---|---|
| screenshots (png/jpg) | within 1920x1080, PNG | `--720` (1280x720) for overviews only; `--jpeg`: JPEG q90, 4:4:4 | enlarged |
| videos | H.264 within 1280x720, the frame rate kept (30 fps), CRF 23 `-preset medium` (libx264) or the hardware encoder, faststart | `--1080` | enlarged, audio |
| contact sheets | fit 1280 wide, PNG | | enlarged |

The box turns for portrait images. `--check` prints sizes before and after, dimensions, and the SSIM of the output
against the original scaled the same way, and warns below 0.97 (stills) or 0.95 (video), and when a still was made
more than 2x smaller (1 px lines may go). A video's `.rec.json` is copied with its scale adjusted
(`edge_check.mjs` reads the small video; `frames.sh` needs the original, which run_flows.sh cuts before it
compresses).

## Agent onboarding (#397): `uf_onboarding`

| ID | The person / their agent | Expected | Covered by | Needs |
|---|---|---|---|---|
| UF-ONB-1 | The agent runs a plain `koan pair --label "UF agent"`; the person clicks Allow | The dialog shows a check row per capability ("Also list my tabs", ...), all on; pairing prints `allowed: space, observe, read, drive, keep-awake` | `uf_onboarding` UF-ONB-1 (real click on Allow); `agent_cli.mjs` "koan pair (no --capabilities): asked for, and got, everything, drive among it"; unit `PairAsksForEveryCapabilityByDefault` | real |
| UF-ONB-2 | The agent asks `koan status` | `running, Agent ready` and `paired: yes` | UF-ONB-2; `agent_cli.mjs` "koan status: paired: yes (Koan knows the pairing)" | |
| UF-ONB-3 | The profile was replaced, so Koan forgot a pairing; the agent runs `koan status` and `koan tabs` | The stale record is deleted, the good one is used, never "several paired clients"; with only a stale one: `paired: no (Koan forgot this pairing; run koan pair)` | UF-ONB-3; `agent_cli.mjs` "Stale pairing: ..." (3 checks); unit `WhichStalePairingsAreForgotten`, `ChoosingAClient` | |
| UF-ONB-4 | The agent reads `koan skill` before its task | The guide covers the snapshot loop, batches, choose, take-over, `wait --resumed`, screenshot sizes, and ends with `[koan:skill] end of guide vN`; no `[koan:notice]` from the app's own koan | UF-ONB-4; unit `TheGuideCoversPairingTakeOverAndSsh`, `test_agent_skill.py` | |
| UF-ONB-5 | The person opens Settings > Agent Control and clicks **Set Up Agents** | "Set up."; Claude Code's skill in `~/.claude/skills/koan-browser/SKILL.md` (the repo's, byte for byte), Codex's in `~/.agents/skills`, nothing for Cursor (not installed); `~/.local/bin/koan` links to the app's koan; the page lists what it set up. Only under the setup home (`KOAN_AGENT_SETUP_HOME`), never the real `~/.claude` | UF-ONB-5 (real click); `agent_cli.mjs` "Set up agents: ..." (a script click in the page); unit `koan_agent_setup_unittest.cc` | real |
| UF-ONB-6 | A new agent session finds `koan` on the PATH and its skill | `~/.local/bin/koan status` says `paired: yes`; the skill says to run `koan skill` and names the full paths | UF-ONB-6 | |
| UF-ONB-7 | The person clicks **Remove** | Koan's skills and link are gone; the person's own skill in the same folder stays | UF-ONB-7 (real click); `agent_cli.mjs` "Set up agents: Remove takes the skills and the link out" | real |
| UF-ONB-8 | The agent ends | `koan end` exits 0 | UF-ONB-8 | |
| UF-ONB-9 | The person sets up agents on their own profile (their real `~/.claude`) | The found agents are listed; Remove cleans up | manual: PR #397 "Mac steps" 2 (only with the user's OK to write to `~/.claude`) | manual |
| UF-ONB-10 | `npx skills add koan-browser/koan` | The skills CLI finds `skills/koan-browser/SKILL.md` | `test_agent_skill.py` (the repo's only skill, identical to the app's) | |

## Indicator layers (#398): `uf_indicator`

| ID | The person / their agent | Expected | Covered by | Needs |
|---|---|---|---|---|
| UF-IND-1 | An agent connects while the person is on a plain tab | A toast "UF agent connected"; the sidebar chip names the agent; no pill, no border, no corner dot | UF-IND-1; `agent_indicator.mjs` "Connected, on a tab no agent holds: the sidebar chip names the agent" | real |
| UF-IND-2 | The agent takes over the person's tab; the person clicks Allow | On that tab: the pill "UF agent is controlling this tab · Take back · Stop" and a 2 px accent border round the page (the pixels of the page card's left and bottom edges change); the chip stays | UF-IND-2 (real click, pixel check); `agent_indicator.mjs` "Taken-over tab shown: ..." | real, rec |
| UF-IND-3 | The person switches to another tab | No pill and no border there; the chip | UF-IND-3; `agent_indicator.mjs` "Another tab shown: no pill and no border there (the user's complaint)" | |
| UF-IND-4 | The person clicks the chip | A menu: "Go to <title> (<agent>)" per tab an agent holds that isn't shown, a separator, "Stop all agents"; Go to brings the tab (and its Space) forward with its pill; nothing waits (no count on the chip) | UF-IND-4 (real clicks on the chip and on the item, its point read by Accessibility: `native_menu_ax.js point`) | real |
| UF-IND-5 | The agent opens its own tab; the person goes to it from the chip's menu (its own Space) | A dot in the row's top trailing corner (pixels: more off-background pixels in the top corner than in the bottom one); shown, the pill offers **Take over**, not Take back | UF-IND-5 (pixel check); `agent_indicator.mjs` "Agent tab shown: ..." | rec |
| UF-IND-6 | The person turns compact mode on (⌘S) with the sidebar hidden | The corner dot stands in for the chip on a plain tab; the pill (no dot) on the taken-over tab; ⌘S again: chip back, dot gone | UF-IND-6 (real ⌘S); `agent_indicator.mjs` "Compact mode, sidebar hidden, ..." | real |
| UF-IND-7 | The person clicks in the taken-over page | A toast "UF agent paused: you used its tab"; the pill says paused, with Hand back; **Hand back** (real click) gives it back | UF-IND-7 | real |
| UF-IND-8 | The agent releases the tab, then ends | No pill after release, the chip stays; on end a "UF agent disconnected" toast and nothing left | UF-IND-8; `agent_indicator.mjs` "Disconnected: no chip, pill or corner dot" | |
| UF-IND-9 | Both looks (shadcn, Zen), an icons-only sidebar, tabs on the right, full screen, a split, a second window, VoiceOver | The layers look right and read right | manual: `scripts/mac/steps/agent_indicator.md` steps 1-14 | manual |

## Compact mode without a black edge (#399): `uf_compact`

| ID | The person | Expected | Covered by | Needs |
|---|---|---|---|---|
| UF-CMP-1 | Presses ⌘S on a blank white page | Compact mode on; the sidebar's strip changes (it left) | UF-CMP-1.N (real key, pixels) | real, rec |
| UF-CMP-2 | Moves the pointer to the window's left edge, then away (twice a round) | Peeked: the sidebar's strip shows it; away: the strip is as before the peek | UF-CMP-2.N.M (real pointer, pixels) | real, rec |
| UF-CMP-3 | Presses ⌘S again | Compact mode off, the sidebar docked | UF-CMP-3.N | real, rec |
| UF-CMP-4 | | Koan still answers | UF-CMP-4 | |
| UF-CMP-5 | (all the above, frame by frame) | No dark pixel at the window's top rows, left columns or bottom rows in any frame of any transition (edge_check.mjs) | run_flows.sh → `uf_post.mjs edge` → UF-CMP-5; self-test `scripts/tests/test_edge_check.py` (fails on an 8 pt notch for 0.1 s); `flows/compact_edge.mjs` adds native fullscreen. The sidebar's columns follow its measured rect in each frame (the peek starts ~0.6 s late; c19 triage 2), and `edge_check.mjs --self-test` runs first | rec |
| UF-CMP-6 | The same in native fullscreen | No notch at the top on toggle or peek | `flows/compact_edge.mjs` + `edge_check.mjs` (README) | real, rec |

## Top row and URL pill (#400): `uf_toprow`

| ID | The person | Expected | Covered by | Needs |
|---|---|---|---|---|
| UF-TOP-1 | Clicks Back, Forward and Reload in the sidebar's top row | Each navigates; the window does not move (they were caption: a real click dragged the window) | UF-TOP-1 (real clicks); `sidebar_top_row.mjs` "Back (a real click) goes back to page A" etc.; browser test `KoanSidebarTopRowBrowserTest.NavButtonsAreClientArea` | real |
| UF-TOP-2 | Moves the pointer over the sidebar | Copy URL, the pinned extension and Extensions fade in (the pill's trailing pixels change) | UF-TOP-2 (pixels); unit `KoanUrlPillButtonsTest` | real, rec |
| UF-TOP-3 | Clicks the pinned extension (badge "135") | Its popup opens | UF-TOP-3; `sidebar_top_row.mjs` "A click (a real click) on it opens the extension's popup" | real |
| UF-TOP-4 | Clicks Copy URL | Toast "Copied current URL!", the address on the clipboard | UF-TOP-4; `sidebar_top_row.mjs` "Copy URL (a real click) shows Zen's toast" | real |
| UF-TOP-5 | Clicks Extensions, then again | Chromium's extensions menu opens, then closes; the window doesn't move | UF-TOP-5; `sidebar_top_row.mjs` "Extensions (a real click) opens the extensions menu"; browser test `ExtensionsButtonToggles` | real |
| UF-TOP-6 | Clicks the pill to edit the address | No Copy URL or Extensions while editing | UF-TOP-6 | real |
| UF-TOP-7 | Opens an empty tab and hovers the sidebar | No Copy URL or Extensions there | UF-TOP-7 | real |
| UF-TOP-8 | A real uBlock badge; trackpad; compact mode; tabs on the right; dragging the row's empty part moves the window | As Zen | manual: `scripts/mac/steps/sidebar_top_row.md` | manual |

## The agent's task, and the CLI it relies on (#401, #406): `uf_agent_task`

| ID | The person / their agent | Expected | Covered by | Needs |
|---|---|---|---|---|
| UF-AGT-1 | The person pairs the agent | Paired with a real click on Allow | UF-AGT-1 | real |
| UF-AGT-2 | "Sign me up on this site": the agent fills a 3-page form (name, email, a native country select; a custom Experience dropdown and a city; the terms) | Done in at most 3 koan calls: `open --task`, then one batch with locators, `choose` on both dropdowns, `--until url~/signup/done --interactive --screenshot micro`; 10 receipts, "until ...: met"; the server got every field; the snapshot shows "Welcome, Ada"; the micro screenshot (≤ 320 px, ~80 tokens) in the same answer. Calls and wall time in the index | UF-AGT-2; `agent_cli.mjs` "Batch: a 3-page form in at most 3 koan calls, every step one receipt" | |
| UF-AGT-3 | The same form the old way (one verb a call, a snapshot per page) | It works too; its calls and time next to the batch's (INFO) | UF-AGT-3; `agent_cli.mjs` INFO "3-page form: old loop ..." | |
| UF-AGT-4 | The agent opens a custom dropdown (a portal listbox growing in, options that need `buttons: 1`) | The snapshot's first line is `popup of combobox "Experience" @N` with its options; an option takes on the first click; `choose` of a missing option is `Koan.noOption` naming the options | UF-AGT-4; `agent_cli.mjs` "Dropdown: ..." (4 checks) | |
| UF-AGT-5 | The agent submits without a choice, fixes a field, looks at one element | The alert "Choose your experience" is in `--interactive`; `--diff` (in the same mode as the last snapshot, `--interactive`: another mode starts a new baseline) prints only `~ textbox "City" = "Paris"`; `--scope @ref` only that element | UF-AGT-5; `agent_cli.mjs` "Batch: --diff after a change prints only what changed" | |
| UF-AGT-6 | The agent navigates to a slow 504 page and to a server that never answers | `ok (HTTP 504 ...)`, exit 0, under 5 s, the page in the snapshot; `--timeout 3`: `Koan.timeout` in 3-8 s, never "no answer" | UF-AGT-6; `agent_cli.mjs` "Navigate: ..." (3 checks) | |
| UF-AGT-7 | An agent over ssh sends many commands through one `koan shell` | Each plain line ends `--- exit 0`; a JSON line gets a JSON answer; the same N snapshots cost no more a command in one shell than as separate processes (both include starting koan and connecting) | UF-AGT-7; `agent_cli.mjs` "koan shell: ..." | |
| UF-AGT-8 | The person looks at the agent's tab; the agent ends the task | The tab shows the agent's work; `end --task` closes it | UF-AGT-8 (screenshot) | |
| UF-AGT-9 | Stale refs, a batch stopped by a stale ref, locators surviving a re-render, an ambiguous locator | `Koan.staleRef` mid-batch, nothing guessed; a locator finds the new element; `Koan.ambiguous` lists both | `agent_cli.mjs` "Batch: a ref from before a navigation, mid-batch, stops it", "Locators: ..." | |
| UF-AGT-10 | An agent on another machine, over ssh with ControlMaster, `koan shell` | Numbers for the PR | manual: PR #401 "Latency, old vs new" | manual |

## Background rendering and holds (#403, #407): `uf_render`

| ID | The person / their agent | Expected | Covered by | Needs |
|---|---|---|---|---|
| UF-RND-1 | The agent opens an animating chart in its background tab; the person has the same page in a background tab of their own | Both open; no Koan power assertion yet | UF-RND-1 | real |
| UF-RND-2 | Another app's window covers Koan's; the agent takes two mini screenshots, then runs `koan watch <tab>` with no command | The screenshots differ (the chart moved); during the watch the agent tab draws > 25 frames/s, the person's own background copy < 2; `pmset -g assertions` has "Koan: an agent is working" (display sleep; idle sleep only on battery in Low Power Mode); the chip's tooltip says "keeping the display awake"; the watch ends on SIGTERM | UF-RND-2; `agent_lease.mjs` "Covered window ..." cases and its SUMMARY table | real, rec |
| UF-RND-3 | The agent ends that task | The assertion goes within 6 s | UF-RND-3 | |
| UF-RND-4 | The person minimises the window (a real click on its minimise button); the agent waits with `--until` | The tab draws > 25 frames/s while the agent waits; the wait ends with `Koan.untilTimeout` | UF-RND-4; `agent_lease.mjs` "Minimised window" | real |
| UF-RND-5 | Nothing held for 5 s | The agent tab is throttled (< 2 frames/s); no assertion | UF-RND-5; `agent_lease.mjs` "Release (3 s idle)" | |
| UF-RND-6 | A watch runs; the person goes to the agent's tab (the chip's "Go to") and clicks **Stop** on its pill | The assertion goes within 4 s, the watch ends, the agent's tabs close | UF-RND-6 (real click); `agent_lease.mjs` "Stop: ..." | real |
| UF-RND-7 | A screencast over koan-relay (agent-browser) while covered; a killed `koan watch` | Live > 25/s for 11 s with no command; after stopScreencast and after a SIGKILL (keep-alive lapses within 30 s) throttled, assertion gone | `agent_lease.mjs` screencastCase, watchCase | real |
| UF-RND-8 | The cap: 6 agent tabs (4 awake at a time, 2 on battery in Low Power Mode) | Exactly the first N are live; the others' commands wait, then succeed | `agent_lease.mjs` cap case | |
| UF-RND-9 | Another Desktop, a full-screen Space, a locked screen, display sleep with a watch, the lid closed, ⌘H, Activity Monitor | As `scripts/mac/steps/agent_lease.md` says | manual: `agent_lease.md` steps 4, 4b, 4c, 5-9 | manual |

## Auto-resume (#408): `uf_resume`

| ID | The person / their agent | Expected | Covered by | Needs |
|---|---|---|---|---|
| UF-RES-1 | The person pairs; the agent opens a checkout page | | UF-RES-1 | real |
| UF-RES-2 | The person goes to the agent's tab (the chip's "Go to", real clicks), clicks in it and types | Paused: `user-input`, not sticky, with `resumesAt`; the pill offers Keep paused and Hand back | UF-RES-2; `agent_indicator.mjs` "A real click in the agent's page pauses it (user-input, not sticky)" | real |
| UF-RES-3 | The person leaves the tab | `koan wait --resumed` returns `Koan.resumed {reason: user-idle}` 4.5-9 s after the last key, `changed: true`, `navigated: false`, with a fresh snapshot | UF-RES-3; `agent_indicator.mjs` "Left the tab: ..." | real |
| UF-RES-4 | The person clicks in the tab while the agent's batch runs | The batch stops at the click (`stopped at k/13 ... Koan.paused`, `not run: ...`), tells the agent to run `koan wait <tab> --resumed`, and types nothing after | UF-RES-4; `agent_cli.mjs` "Batch: the user taking the tab stops it" (Take over) | real |
| UF-RES-5 | The person stays on the tab, idle | The pill counts down "continues in Ns · Keep paused · Hand back", then the tab resumes; a ref from before is `Koan.staleRef` ("from before the user used this tab") | UF-RES-5; `agent_indicator.mjs` "Idle on the tab: the pill counts down" | real |
| UF-RES-6 | The person clicks **Keep paused**, leaves for 8 s, comes back and clicks **Hand back** | Sticky `keep-paused`; `koan wait` exits 1 at once ("the user is handling this tab (keep-paused)"); still paused after 8 s; Hand back gives it back | UF-RES-6; `agent_indicator.mjs` "A real click on Keep paused" ... | real |
| UF-RES-7 | The person clicks into the password field | Sticky `secret-field`; no resume after 7 s away | UF-RES-7; `agent_indicator.mjs` "A real click into a password field: the pause is sticky (secret-field)" | real |
| UF-RES-8 | The person clicks into the card number field (`autocomplete=cc-number`) | Sticky; no resume | UF-RES-8; browser test `KoanAgentPauseWatchBrowserTest` (cc-number) | real |
| UF-RES-9 | Settings > Agent Control: "Continues after" 2 s; then the switch off | Options 2, 3, 5, 10, 15, 20, 30, 45, 60 (no 0, no unlimited); at 2 s it resumes 1.5-4.5 s after leaving; off: it waits for Hand back | UF-RES-9 (the switch by a real click; the menu by its change event: the native popup can't be driven); `settings_effects_behaviour.mjs` round trips | real |
| UF-RES-10 | The agent ends | | UF-RES-10 | |
| UF-RES-11 | VoiceOver on the countdown; ⌘Q mid-countdown | The countdown is read once; Koan quits cleanly | manual: `agent_indicator.md` steps 15-20 | manual |

## Screenshots (#409, #410): `uf_shots`

| ID | The person / their agent | Expected | Covered by | Needs |
|---|---|---|---|---|
| UF-SHT-1 | The person pairs; the agent opens a page | | UF-SHT-1 | real |
| UF-SHT-2 | The agent takes `--size micro`, `mini`, `normal`, `full` of its own tab | Long edge 320 / 640 / CSS px (≤ 1568) / device px (≤ 2576, ≤ 4,784 tokens); the line `saved ... (W×H, ~N image tokens, 1 px = X CSS px)` matches the PNG; tokens grow with the size, micro ≤ full/10 | UF-SHT-2; `agent_cli.mjs` "koan screenshot --size ...: ..." | |
| UF-SHT-3 | The agent clips the button by locator and by @ref, a rect off screen, and asks for a screenshot with an act | The box plus 8 px, rounded outwards to whole points, at device pixels (CSS px with `--size normal`); `Koan.offScreen`; `act click --screenshot micro`: "ok" and the line in one answer | UF-SHT-3; `agent_cli.mjs` "--clip ...", "act --screenshot ..." | |
| UF-SHT-4 | The agent takes over the person's tab (real click on Allow) and takes every size and clip there | The same sizes as on an agent tab (one capture path, `Koan.captureScaled`); the page saw no resize, devicePixelRatio or media query change | UF-SHT-4; `agent_cli.mjs` "...koan screenshot --size ... of the user's tab", "...the user's page saw no resize ..." | real |
| UF-SHT-4.still | (the recording of UF-SHT-4) | No pixel of the person's page changes in any frame while the agent captures it: no flicker | run_flows.sh → `uf_post.mjs still`; self-test `scripts/tests/test_uf_post.py` (a two-frame flicker fails) | rec |
| UF-SHT-5 | (control) The page changes colour three times on purpose | The still check sees it (UF-SHT-5.still-control), so it can fail | UF-SHT-5 + `uf_post.mjs still` | rec |
| UF-SHT-6 | Pinch and ⌘+ zoom, an external 1× display, another Space; no visible flicker by eye | As `scripts/mac/steps/agent_capture.md` | manual | manual |
| UF-SHT-7 | Old CDP clip+scale against `captureScaled`: ms, bytes, tokens per size | The table for the PR | `agent_capture_bench.mjs`, `agent_shot_bench.mjs` (not in the default list) | |

## The built app (#393, #394, #395, #405): `uf_build`

| ID | The person | Expected | Covered by | Needs |
|---|---|---|---|---|
| UF-BLD-1 | Installs the app | `codesign --verify --deep --strict` passes (SKIP on an ad hoc signed dev build: `run_flows.sh --build-app <signed Koan.app>`) | UF-BLD-1; `test_build_mac.py` (the sign step's order) | |
| UF-BLD-2 | Updates the app | (SKIP on a dev build, as UF-BLD-1) Signed by an identity (not ad hoc), designated requirement on the certificate (not a cdhash), so the Keychain item keeps matching | UF-BLD-2 | |
| UF-BLD-3 | Plays DRM video | `Koan Framework.sig` (the VMP signature) in the bundle; with it the code signature still verifies (VMP first; signed apps only). The CDM is not in the bundle: Chromium can't ship Google's CDM unbranded, the component updater downloads it (`build/koan-mac.gn`) | UF-BLD-3; `widevine.mjs`, `test_widevine.py` | |
| UF-BLD-4 | Opens chrome://version and chrome://components | Names Koan and Chromium 158; the Widevine Content Decryption Module is a component | UF-BLD-4 | |

## Agent autofill (#412, not merged): `uf_autofill` (stub: skips without `koan autofill`)

| ID | The person / their agent | Expected | Covered by | Needs |
|---|---|---|---|---|
| UF-AFL-1 | The person signs in on a site and clicks **Save** in Chrome's password bubble | The password is saved | UF-AFL-1 | real |
| UF-AFL-2 | The agent runs `koan autofill <tab> loc:... --list` on the same site | The login listed as "ada (saved password)"; never the password, nor dots as long as it | UF-AFL-2 | |
| UF-AFL-3 | The agent runs `--pick 1` | Chrome fills the form (the page has the password); the agent's snapshot shows `•••• (filled)`; no snapshot (`--full`, `--json`) carries the secret | UF-AFL-3 | |
| UF-AFL-4 | The agent ends | | UF-AFL-4 | |
| UF-AFL-5 | A card that must be verified; a grouped (affiliated) match; a form posting off-site | `userNeeded: autofill-verify` and the pill's Open; `Koan.offSite` | to write with #412's e2e | real |
| UF-AFL-6 | In a tab of the person's where Chrome filled the saved login on load: the agent runs `koan take-over`, the person clicks into the password field, then **Hand back**; the agent runs script on the page (`koan agent-browser eval ...value`), `koan snapshot`, then `koan act <tab> press Enter` | The eval is refused (Koan.secretField, "…by Koan, the password manager or the user"); the snapshot shows `•••• (filled)`; after sign-in, nothing the agent gets has the password, and a page that echoes it reads `[secret]` | `KoanAgentSecretLockBrowserTest.APasswordFilledOnLoadInAUsersTab` (T-sec-18); UF-AFL-6 to write in `uf_autofill` | real |
| UF-AFL-7 | The same with no saved login: the person types a password into the field themselves, then **Hand back** | As UF-AFL-6 | `KoanAgentSecretLockBrowserTest.APasswordTheUserTypedInAUsersTab` (T-sec-19); UF-AFL-7 to write in `uf_autofill` | real |

## Settings switches and radios (#419): no flow (e2e `settings_controls`)

| ID | The person | Expected | Covered by | Needs |
|---|---|---|---|---|
| UF-SET-1 | Opens chrome://settings/performance in the shadcn look, light then dark, and looks at the switches that are on (Memory Saver, Inactive tabs appearance, Performance issue alerts) | On: the primary track with the thumb at the right edge; off: the muted track with the thumb at the left | `settings_controls.mjs` ("the thumb is at the right edge", "...left edge", "on and off tracks differ"); `test_settings_shell.py` (`SwitchAndRadioStyleTest`) | manual (a look) |
| UF-SET-2 | Clicks a switch | The thumb slides to the other end in about 150 ms, the track changes colour; Tab to it shows a ring | `settings_controls.mjs` ("the knob's transform travels"); the ease and the ring by eye | real, manual |
| UF-SET-3 | Turns Memory Saver on, in dark and light | "Moderate", "Balanced" and "Maximum" each show a visible ring; the selected one is a primary ring with a primary dot | `settings_controls.mjs` ("ring has >= 3:1 contrast", "the selected choice's ring is the primary colour") | manual (a look) |
| UF-SET-4 | Switches to the Zen look | Chromium's own switches, on still at the right | `settings_controls.mjs` (the `zen` checks) | |

## Agent uploads (#420): `uf_upload`

| ID | The person / their agent | Expected | Covered by | Needs |
|---|---|---|---|---|
| UF-UP-1 | The agent pairs and opens a job application in its own tab; the person goes to it from the chip's menu | The form in the agent's Space, the pill on it | UF-UP-1 (real clicks) | real |
| UF-UP-2 | The agent fills the name and runs `act <tab> fill @name … upload @button resume.pdf --snapshot` on a styled button over a hidden input | "attached resume.pdf"; the page itself says "Attached: resume.pdf" and read the same name, size and SHA-256; Koan shows "UF agent attached resume.pdf"; no open panel | UF-UP-2; `agent_upload.mjs` (every chooser kind and refusal) | real, rec |
| UF-UP-3 | The person clicks in the agent's page (takes it over) | The agent's upload is refused (Koan.paused) until **Hand back** (real click); then it attaches again | UF-UP-3; `agent_upload.mjs` "a tab the user took over: Koan.paused" | real |

## Swiping between Spaces (#91, the direction fixed in #436): `uf_swipe`

A trackpad-like gesture (`scripts/mac/tools/swipe.m`: continuous scroll events with Began/Changed/Ended phases at the sidebar), with the deltas a real trackpad gives for the finger direction (`swipe --info`: how AppKit reads them). The strip following the fingers is checked in the contact sheets. A real trackpad, with natural scrolling on and off, stays `full-flow.md` step 7.

| ID | The person | Expected | Covered by | Needs |
|---|---|---|---|---|
| UF-SW-1 | Has three Spaces and is in the middle one | Three Space buttons, the second active | UF-SW-1 | real |
| UF-SW-2 | Swipes right to left over the sidebar | The Space on the right (the next one) comes in, following the fingers | UF-SW-2 (sheets: the strip moves left) | real, rec |
| UF-SW-3 | Swipes left to right | The Space on the left (the previous one) comes back | UF-SW-3 | real, rec |
| UF-SW-4 | Drags 60 pt slowly and lifts | It springs back; the same Space | UF-SW-4 | real |
| UF-SW-5 | On the last Space, swipes on right to left, far | The + badge comes in, then the Create-a-Space form opens; no Space is made by itself; Esc closes it | UF-SW-5 | real, rec |
| UF-SW-6 | On the first Space, swipes left to right (towards the Library on the left), then back | The Library opens, following the fingers; swiping back closes it | UF-SW-6 | real, rec |

## Agent usage across the flows

What an agent does in a real task, and where each part is exercised end to end with the real CLI:

| The agent | Where |
|---|---|
| finds Koan (skill, PATH), pairs, reads the guide | UF-ONB-1..6 |
| opens a task tab, fills a 3-page form in one batch, chooses from a custom dropdown, checks with a micro screenshot | UF-AGT-2, UF-AGT-4 |
| takes over the person's tab (the person approves with a real click) | UF-IND-2, UF-SHT-4 |
| is interrupted by the person, waits with `koan wait --resumed`, carries on from the fresh snapshot | UF-RES-2..5 |
| takes micro screenshots and clips | UF-AGT-2, UF-SHT-2..4 |
| watches its tab with the window covered and minimised | UF-RND-2, UF-RND-4 |
| ends (task, connection), or is stopped by the person | UF-AGT-8, UF-RND-3, UF-RND-6, UF-IND-8 |

## Why a flow fails on the bug its feature fixed

| Feature | The bug | What fails |
|---|---|---|
| #397 | plain pair asked for nothing; status said yes for a forgotten pairing | UF-ONB-1 (`allowed:` line), UF-ONB-3 |
| #398 | the pill on every tab | UF-IND-3 (a pill on the other tab) |
| #399 | a black notch on toggle and peek | UF-CMP-5 (edge_check on every frame) |
| #400 | back/forward/reload dead for real clicks | UF-TOP-1 (real clicks: no navigation, or the window moved) |
| #401 | a 504 took 60 s and said "no answer"; options missed the first click | UF-AGT-6 (< 5 s), UF-AGT-4 (the page has the option) |
| #406 | ~40 calls for a 3-page form | UF-AGT-2 (≤ 3 calls) |
| #403/#407 | a covered agent tab stalled 3 s after the last command | UF-RND-2 (> 25 frames/s during a watch with no command) |
| #408 | a paused tab stayed paused until Hand back | UF-RES-3 (resumed in 4.5-9 s) |
| #409/#410 | user tabs captured at device pixels; emulation flickered the person's page | UF-SHT-4 (sizes, koanSeen), UF-SHT-4.still |
| #393-#405 | ad-hoc signing (a new Keychain prompt per build); VMP after codesign | UF-BLD-1, UF-BLD-2 |
