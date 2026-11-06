# ego(lite) / ego-browser: public-source research

**Use rules.** This is a record of what the vendor publishes (an MIT repo, public docs, a public changelog, plain-text
files from the public DMG). It is for understanding only. Koan reimplements independently: no code, scripts, prompts,
skill text or snapshot syntax from ego is copied. The vendor's Terms forbid reverse engineering the app, so no
executable was analysed; those questions are listed as unknown. Nothing from the DMG is stored in this repository.
Hands-on, black-box findings from the Mac are in `.agent/notes/ego-lite-teardown.md` (zen-ref's note).

Date of research: 2026-11-07. Everything below comes from public sources. No login, no account, no form was used, and nothing from the app bundle was executed.

Labels: CONFIRMED = quoted or directly read, with URL or file path. INFERRED = my reading, not stated by the vendor.

Path shorthand:
- `REPO` = `<scratchpad>/repo/ego-lite` (shallow clone of https://github.com/citrolabs/ego-lite at commit `bacaece43900`, 2026-11-04, MIT, about 21.9k stars). `EB` = `REPO/package/ego-browser`.
- `EXT` = `.../scratchpad/ego-dl/extract/ego lite.app/Contents` (plain-text files read out of the DMG).

Source caveat: the vendor's web docs (lite.ego.app/document/...) describe an older v1 helper API (`useOrCreateTaskSpace`, `snapshotText`). The GitHub repo and the in-bundle skill are v2.2.0 (`taskSpace`, `page.snapshot`). Where the two disagree, the repo is newer and more detailed, and I say so.

---

## 1. Skill / CLI

### 1.1 Where the code lives (CONFIRMED)
- The vendor publishes the agent-side runtime and the skill under MIT at https://github.com/citrolabs/ego-lite. Source: `llms-full.txt`, "Repository: https://github.com/citrolabs/ego-lite (MIT)", and the GitHub API.
- `REPO/AGENTS.md` says what is and isn't open: "This repo contains the open-source harness and the agent skill package, **not** the browser itself. The ego lite app bundles its own `ego-browser` binary that embeds this runtime." It also says the `globalThis.ego` bindings are "provided by the closed-source ego lite app".
- Related repos in the org: `pi-chrome-use`, `safari-browser-use`, `ego-browser-benchmark-framework`.
- GitHub Releases carry `ego-browser-v2.2.0.zip` (and older zips). I did not open them.
- npm: `REPO/plugins/ego/package.json` names the package `@citrolabs/ego` v2.2.0, and the changelog mentions an `@ego-lite` npm scope. `registry.npmjs.org` returned "Not found" for both `@citrolabs/ego` and `@ego-lite/ego`. Not found does not mean the package doesn't exist under another name. No PyPI, crates or Homebrew package found.
- Skill install without the app: `npx skills add github:CitroLabs/ego-lite/skills/ego-browser` (docs/space).
- Plugin manifests for Claude Code, Codex, Cursor, CodeBuddy, Qoder and OpenCode are in `REPO/plugins/ego/` and `REPO/.claude-plugin/marketplace.json`.

### 1.2 Two-layer skill (CONFIRMED, `REPO/docs/ego-browser-skill-command.md`)
- Entry skill, `skills/ego-browser/SKILL.md`: 1,733 bytes. It only says "Before any browser task, run: `ego-browser skill` and follow its output."
- It also carries `references/install*.md`, `references/clearing-state.md`, `scripts/install.sh` and `learnings/{github,google,x-com}`.
- The real usage guide is `EB/skill-body/SKILL.md` (24,570 bytes, v2.2.0, 2026-10-16). It is printed by `ego-browser skill`, so it always matches the installed app. It ends with the line `[ego-browser:skill] end of guide v<version>`. Per-OS variants are rendered from `<!-- platform: windows -->` blocks.
- Install location, quoted from the doc: "On every launch, check `~/.agents/skills/ego-browser`: Missing: copy `agent-skills/ego-browser/` there as real files … Anything else (installed by the user or a package manager): leave it untouched." The older design used a symlink to `~/.local/share/ego/ego-skills`.
- The docs say onboarding also writes `~/.claude/skills/` (lite.ego.app/document/en/docs/claude-code: "Onboarding writes the ego-browser skill to ~/.claude/skills/").
- Inside the app bundle the entry skill sits at `.../Resources/ego-skills/ego-browser/`. `SKILL.md` and `scripts/install.sh` there are byte-identical to the repo copies (checked with diff).
- The Claude Code guide tells users to enable "Bypass permissions" / `claude --dangerously-skip-permissions`. The reason it gives is that Claude must run the shell commands that start and drive ego without a confirmation per step.

### 1.3 What the skill runs (CONFIRMED)
- Binary: `ego-browser`, a native launcher inside the app, at `ego Framework.framework/Versions/<ver>/Helpers/ego-browser` (path from `scripts/install.sh`, `find_ego_browser_in_app`). Onboarding "registers the `ego-browser` command on the PATH, usually under `~/.local/bin`" (`references/install-macos.md`).
- Script language: JavaScript (ESM) in Node.js. `skill-body/SKILL.md`: "Pass the script to `ego-browser nodejs`" via heredoc or `-e`. "The script always runs in Node.js, not in the web Page."
- Node is built into the app. The docs say "we embedded Node.js into ego(lite) (it adds only 10 MB to the install)" (docs/skills). The changelog says "The built-in Node.js stays at v24.22.1" (0.6.2.5).
- The script does not run in the shell's working directory. It also does not inherit the shell environment (`process.env` won't contain your variables), so inputs must be written into the script.
- Helpers come from the open-source bundle (`EB/src/run.ts`, `index.ts`). Code is run as `new AsyncFunction(...helperNames, '"use strict";\n' + code)`. Top-level await works. The helpers `taskSpace`, `claimTaskSpace`, `takeOverTaskSpace`, `listTaskSpaces`, `help`, `cliLog`, `console` are injected as lexical parameters.
- Entry points (`AGENTS.md`):
  - run as a CLI: `runMain()` reads stdin.
  - imported by the app: `installEgoSdk(globalThis)`. This is the path the real product uses.
  - `--sdk-path <index.js>` lets the installed CLI load a local build of the bundle (`docs/local-runtime-development.md`).
  - A debug override exists at `~/Library/Application Support/Citro Labs/debug/index.js`.
- Per-user state is kept in `~/.ego-browser/state` (`EB/src/env.ts`, `EGO_BROWSER_STATE_DIR`). It holds the Page ledgers and the shared download dir.
- The embedded host runs all scripts in one long-lived Node process. `EB/src/driver/observe.ts` comment: "The embedded host runs every script in one long-lived Node process, giving each run its own `globalThis` but sharing `process.env`, `os.tmpdir()` and the pid."

### 1.4 CLI subcommands seen in public text
CONFIRMED to exist:
- `ego-browser nodejs [-e CODE] [--sdk-path P]` (reads stdin).
- `ego-browser skill` (prints the guide).
- `ego-browser upgrade` (skill: "run `ego-browser upgrade` only with their approval").
- `ego-browser import --data-path <dir>` (changelog 0.6.2.7).
- `ego-browser help` (`docs/ego-browser-skill-command.md`).

Also seen, but not found in current docs:
- `ego-browser open <url>` and `ego-browser click @e3` appear in `llms-full.txt` and the blog. These look like marketing or legacy forms and conflict with the current heredoc design.
- Changelog: "CLI task-spaces output now includes Profile ID" (0.4.6.12) and "The CLI can now connect to a specific browser instance by server name" / "you can specify the ego server name at startup" (0.4.5.x). These imply a task-spaces listing command and a server-name option. I could not find their flag names.

The full flag list is **not** public. Only the commands above are verifiable.

### 1.5 Transport between CLI and browser
CONFIRMED:
- Inside the script, CDP is spoken over native bindings, not a socket the script opens. `EB/src/browser-runtime.ts` builds `JSON.stringify({id, method, params, sessionId?})` and calls `runtime.sendCDPMessage(payload)`. Replies arrive through `runtime.onCDPMessage = dispatchCdpMessage`. Send failures arrive through `onSendCDPMessageError(message, error_code)`.
- `docs/native-bindings-api.md`: "the low-level `globalThis.ego` API injected into the `ego-browser nodejs` environment by the installed Ego Lite application." "Most methods return a Promise and route work to the browser-side `NodeClient`. Promise results are converted from browser-side `base::Value` objects."
- `ego.sendCDPMessage`: "Sends a raw CDP message through the Node CDP channel … the current task must be selected with `ego.useTaskSpace(id)`, and its ownership must be `agent`." It throws `EGO_CDP_CHANNEL_UNAVAILABLE` if the channel is missing.
- The docs call the protocol "the Chrome DevTools Protocol to the real Chromium session" (docs/ego-browser).
- A custom CDP domain exists: `egoAutomated.enable` / `egoAutomated.disable` (`browser-runtime.ts`, `enableEgoAutomation`; SKILL.md "Page sessions hide page console messages from CDP so sites cannot detect the automation … `page.cdp("egoAutomated.disable")`").
- The changelog mentions a "CLI CDP bridge" that blocks `Target.createTarget` (0.4.5.x) and a crash on CDP messages over 64 KB.

NOT CONFIRMED, and deliberately not pursued:
- How the outer `ego-browser` process hands the heredoc to the browser. It could be a unix socket, a named pipe, XPC or something else. The repo says only that the app "dispatches" the heredoc: `EB/README.md`, "The ego-browser browser dispatches `ego-browser nodejs <<'EOF' ... EOF` heredocs to that bundle."
- The earlier thread's claim of "no TCP/remote-debugging port" is plausible given the above but is **not stated anywhere I read**.
- The repo never mentions `--remote-debugging-port`, `--remote-allow-origins` or an `ws://` endpoint. The only place `ws` appears is bundled Puppeteer transport code in the `local-ego-daemon` web client (see section 2), which is a different component.
- Whether there is one CDP agent host per task space: the bindings doc speaks of "the browser-side CDP agent host", with failures "the selected task space has not been claimed by the agent" and "the browser-side CDP agent host is no longer available". Per message checks on ownership are CONFIRMED. "One per task space" is INFERRED.

### 1.6 Command set (CONFIRMED, `EB/skill-body/SKILL.md`)
TaskSpace and global helpers:
- `taskSpace(nameOrId, {profileId?})`: creates, or resumes your own space of that name. If the name belongs to a user-owned space it fails with `EGO_TASK_SPACE_NAME_IN_USE`.
- `claimTaskSpace(id)`, `takeOverTaskSpace(id)`, `listTaskSpaces()` (entries have `id`, `name`, `ownership`, `profileId`, `profileName`, `recentTabTitles`).
- `TaskSpace` members: `spaceId`, `name`, `ownership`, `page(label)`, `userPage()`, `pages()`, `tabs()`, `newPage()`, `adopt(page,{as?})`, `release(label)`, `waitForControl()`, `handOff()`, `finish({keep})`, `cdp(method, params, options)`.
- `tabs()` returns `{label?, page, targetId, title, url, active, openedBy}`.
- Pages get permanent labels `p1`, `p2`, ... A new space starts with `p1`. The page budget defaults to 8 (`EGO_BROWSER_PAGE_BUDGET`, `page-model.ts:4405`).

Page API:
- Observe: `label`, `spaceId`, `openedBy`, `targetId`, `url()`, `title()`, `info()`, `snapshot()`, `screenshot()`.
- Navigate and wait: `goto()`, `reload()`, `waitForURL()`, `waitForEvent()`, `waitForSelector()`, `waitForLoadState()`, `waitForFunction()`, `waitForTimeout()`.
- Elements: `click()`, `dblclick()`, `hover()`, `dragAndDrop()`, `fill()`, `selectOption()`, `focus()`, `press()`, `setInputFiles()`, `waitForFileChooser()`, `close()`.
- Dialogs: `acceptDialog(text?)`, `dismissDialog()`.
- Input: `mouse.{click,move,down,up,wheel}`, `keyboard.{down,up,press,type,insertText,paste}`.
- Code and protocol: `evaluate(fnOrString, arg)`, `fetch(url, opts)`, `cdp(method, params, opts)`, `events()`.
- Quote: "Ego-browser deliberately exposes a small custom API. It is not Playwright, even where method names and options look similar." Do not infer `locator()`, `getByRole()`, etc.
- Selector forms for element actions: `@21` / `ref=21`, `text=...`, `loc=css:` / `loc=role:` / `loc=href:`, `xpath=...`, raw CSS, plus a small Playwright subset (`css=`, `:has-text()`, `:text-is()`, `>> nth=N`).
- Pointer actions accept `label` (3-6 words) that is shown beside the visible agent cursor (via `ego.setAgentTaskState` and `ego.animationHighlightMouseToPosition`).
- `help()` / `help("page.click")` prints signatures.

How a multi-step snippet is submitted:
- One heredoc per round: `ego-browser nodejs <<'EOF' ... EOF`.
- Every invocation is a fresh Node process in the CLI sense. Task spaces, tabs and Page labels persist (ledger on disk). JS variables do not.
- Guidance: do all planned actions on a page, wait for the final state, then print `await page.snapshot()` last so the next round starts from it.
- Output is buffered and flushed when the script settles. A thrown error drops the buffer.
- Windows PowerShell can't use heredocs, so scripts go in a `.mjs` file loaded by `-e "await import('file:///...')"` or `< file.mjs` in cmd.

Native bindings (`globalThis.ego`, CONFIRMED, `docs/native-bindings-api.md`):
- Task spaces: `getBrowserVersion`, `listProfiles`, `createTaskSpace(name, profileId?)`, `claimTaskSpace(id, name?)`, `listTaskSpaces`, `useTaskSpace(id)` (local selection only), `closeTaskSpace`, `completeTaskSpace`.
- Control and UI: `handOffTaskSpace`, `takeOverTaskSpace`, `setAgentTaskState(text)`, `animationHighlightMouseToPosition(x,y)`.
- Tabs and snapshot: `createTab(url)`, `listTabs()`, `snapshot(opts)`.
- CDP: `sendCDPMessage`, `onCDPMessage`, `onSendCDPMessageError`.
- Error codes: `EGO_TASK_SPACE_USER_IN_CONTROL`, `EGO_TASK_SPACE_NOT_SELECTED`, `EGO_CDP_CHANNEL_UNAVAILABLE`, `EGO_SNAPSHOT_FAILED`, `EGO_PROFILE_NOT_FOUND` and others.

### 1.7 Waiting and timeouts (CONFIRMED, `EB/src/page-model.ts` lines 609-658, `driver/page-waits.ts`, `driver/waits.ts`)
- The earlier thread's numbers hold up:
  - `DEFAULT_PAGE_ACTION_TIMEOUT_MS = 3_000`.
  - `PAGE_ACTION_RESOLUTION_RETRY_MS = 50` for a blocked target.
  - `MISSING_TARGET_IDLE_MS = 500`. Source comment: "A missing action target stops being retried once the page has shown no DOM mutation and no network activity for this long."
- Other defaults:
  - `waitForSelector` 10 s (`page-waits.ts:154`), default state `visible`. Several other waits default to 10 s (`page-model.ts` 2117, 2293, 2724).
  - `goto` / `reload` 15 s (`page-model.ts` 1883/1904). `goto()` stops an uncommitted navigation at timeout, leaving the Page on its previous document.
  - Selector poll interval 100 ms.
  - `waitForLoadState` default `load`.
  - `waitForNetworkIdle` idle window 500 ms (v1 helper, `driver/waits.ts`).
  - `page.evaluate`: 14 s execution limit, 15 s transport limit.
  - `waitForControl`: 20 s poll, 10 min total.
  - CDP request timeout 15 s; session TTL 2 s; session setup wait 2 s.
  - Page budget 8.
  - A missing-target action "fails faster when the page is idle" (changelog 0.6.2.5).
- Guidance in SKILL.md: "Avoid fixed delays when an observable condition exists. … A snapshot captures the current moment; it does not wait for the page to become stable."
- Dialogs: a blocked JS dialog stalls all renderer commands, so the runtime fails other methods fast with `EGO_PAGE_DIALOG_OPENED`.
- `Page` action errors: `EGO_PAGE_UNRESPONSIVE` is raised when a navigation is pending and the page does not answer.

### 1.8 Take-over / hand-over (CONFIRMED)
- Ownership is `agent` or `user` per task space. The user-in-control state blocks `createTab`, `listTabs`, `snapshot`, `closeTaskSpace`, `completeTaskSpace`, highlight, `setAgentTaskState` and raw CDP sends (bindings doc).
- Handoff flow in the skill: `await task.handOff()`, end the round, tell the user what to do. Afterward `takeOverTaskSpace(id)` and `task.userPage()`, then `adopt()` it. `waitForControl()` polls instead.
- `claimTaskSpace(id)` takes a user-owned or inactive space and needs explicit user approval: "The caller must have the user's approval before taking ownership."
- Automatic transfer to the user happens on browser-owned prompts. The reason keys are: `notifications`, `location`, `camera`, `microphone`, `pan_tilt_zoom_microphone`, `midi`, `bluetooth`, `usb`, `serial`, `hid`, `protocol_handler`, `manual_takeover`. The 0.6.2.5 changelog adds that clipboard, local-network, storage-access, window-management and local-fonts prompts now report specific reasons.
- Page-authored `alert` / `confirm` / `prompt` stay under agent control (`docs/native-javascript-dialog-requirement.md`). That doc states `beforeunload` is not yet handled, and a dialog left open when a script exits can make the next invocation time out (in 0.5.2.2).
- Completion: `task.finish({keep: []})` closes the space. `keep: ["p2"]` leaves chosen Pages. `completeTaskSpace` leaves the space open and user-owned.
- Idle cleanup: "An Agent task Space closes automatically after 30 minutes without a command" (changelog 0.6.0.12).
- User-visible behaviours from the changelog:
  - the address bar is read-only while an agent controls a Space;
  - an "Agent control overlay" and a "task border glow";
  - agent task tabs are muted by default;
  - the system won't sleep during an agent task;
  - while an agent controls a Space, Space shortcuts and browser UI that would disrupt it (beforeunload prompts, file pickers, credential dialogs, external protocol handlers) are suppressed;
  - Settings pages opened meanwhile go to a normal Space.
- Permissions the user sees: the web docs list "captcha, QR-code login, payment confirmation" as pause points. There is no OS-level consent prompt documented for the CLI itself. The only gate described is the host agent's own permission mode (Bypass permissions for Claude Code).
- I found no source describing authentication between the CLI and the app (token, user prompt, socket permissions). Unknown.

---

## 2. Bundle (macOS DMG)

Downloaded to `.../scratchpad/ego-dl/`, never executed.

### 2.1 Download facts
- CONFIRMED public: `https://cdn.ego.app/setup/macos/arm64/egolite-fHoqgZ74bOEM.dmg`. The URL is hard-coded in `REPO/skills/ego-browser/scripts/install.sh`, with an x64 sibling. No login was needed.
- The arm64 file is 149,608,943 bytes. HTTP headers: `x-amz-meta-sha256: e891aed9...605c1`, which matches my `sha256sum`. `x-amz-meta-omaha-version: 0.6.2.9`.
- x64 DMG: 168,283,951 bytes, same version. I did not download it.
- The file is an xz stream containing a raw APM + HFS+ image (452,741,632 bytes). `xz -d` reported "Compressed data is corrupt" at the end, but the full image came out and the HFS+ alternate volume header was present.
- No 7z, hfsplus, dmg2img or xar tools were installed. I used my own read-only Python HFS+ lister (`.../scratchpad/tools/hfs.py`, run with `python3 -I`), with the output in `.../scratchpad/tools/listing.tsv` (1,363 entries). No pip install.
- I extracted only plain-text shipped files into `ego-dl/extract/`: Info.plists, skill files, small JSON/MJS/HTML, the `install.sh` scripts, the `.keystone_install` script and SQL migrations.

### 2.2 Scope limit (ToS)
The vendor's Terms say: "Reverse engineering, decompiling, disassembling, modifying, creating derivative works of the Services, or attempting to obtain, derive, or enabling any third party to obtain or derive the source code of the Services, except as expressly permitted by applicable law" (https://lite.ego.app/terms). On the coordinator's instruction I did **no** `strings`, disassembly or other analysis of any executable, framework or compiled file.

Binary-level questions were deliberately left unanswered. These are: remote-debugging flags, unix socket paths, `ws://` / `127.0.0.1` use, IPC or mojo service names, feature-flag names, what the `ego-browser` launcher is written in, and how it reaches the browser. They are listed in section 5.

### 2.3 What the plain-text/directory evidence shows
- Bundle: `ego lite.app`, `CFBundleIdentifier = com.citrolabs.ego.lite`, `CFBundleShortVersionString` / `KSVersion` 0.6.2.9, `CFBundleVersion` 6.2.9, `LSMinimumSystemVersion` 13.0, `KSUpdateURL = https://update.citrolabs.ai/service/update2`, `KSChannelID = lite`, `CrProductDirName = Citro Labs/ego lite`, `NSPrincipalClass = BrowserCrApplication`, `NSAppleScriptEnabled` true, `OSAScriptingDefinition = scripting.sdef`, built with macosx27.2 SDK.
- URL schemes: `http`, `https`, `file`, and `ego` ("Direct launch URL"). `LSUIElement` is not set on the main app. It is "1" on all helper apps.
- Framework: `Contents/Frameworks/ego Framework.framework/Versions/0.6.2.9/`. Its Info.plist id is `com.citrolabs.ego.framework`.
- Chromium version: found in plain text in `app_mode-Info.plist` and `EgoUpdater.app/Contents/Info.plist` as `CFBundleShortVersionString = 156.0.8078.74`, and in the public changelog (0.6.2.5): "Upgraded the browser engine from Chromium 156.0.8078.48 to 156.0.8078.74. The built-in Node.js stays at v24.22.1." The changelog is CONFIRMED. The plists agree. Calling the plist value "the Chromium version" is INFERRED from the filename and the match with the changelog.
- Helper apps (directory names; the plists give ids `com.citrolabs.ego.helper*`):
  - `ego Helper`
  - `ego Helper (Alerts)`
  - `ego Helper (GPU)`
  - `ego Helper (Renderer)`
  - `ego Helper (Components)`
  - `ego Helper (Node)`, id `com.citrolabs.ego.helper.node`
  - `ego Helper (Aperitif)`, `(Aperitif Alerts)`, `(Aperitif GPU)`, `(Aperitif Renderer)`
  - `EgoUpdater.app`, a Chromium-updater fork. It also has `Library/LaunchServices/com.citrolabs.ego.UpdaterPrivilegedHelper` (1,997,168 bytes).
  - `app_mode_loader`, `chrome_crashpad_handler`, `web_app_shortcut_copier`.
  - `Libraries/libaperitif.dylib` (87,632 bytes).
- File sizes: `ego-browser` is 1,992,608 bytes at `Versions/0.6.2.9/Helpers/ego-browser`, and the main executable `MacOS/ego lite` is 72,256 bytes. The framework binary `ego Framework` is 270,131,312 bytes.
- INFERRED from directory names only:
  - `ego Helper (Node)` is the separate, UI-less process that hosts the embedded Node scripts. This agrees with "built-in Node.js" in the docs, the bindings doc and the changelog line "Fixed leftover CDP callbacks when a Node runtime ends … could abort the utility process". It does not prove how the CLI talks to that helper.
  - "Aperitif" is some internal component of the product. Its function is unknown.
- Bundled skill: `Resources/ego-skills/ego-browser/` (CONFIRMED identical to the repo's entry skill for `SKILL.md` and `install.sh`). The `local-ego-daemon` below is not the skill.
- Extra plain-text resource tree, `Resources/local-ego-daemon/`: a bundled Node daemon with a web UI.
  - `index.mjs` (`#!/usr/bin/env node; import "./runtime/index.mjs"`) and `runtime/*.mjs` (Effect, `acp`, `langfuse`, `sentry`, `sdk`, `run-*.mjs`). The largest is 5.2 MB.
  - `client/index.html` (title "ego home") plus 525 asset files: KaTeX fonts, syntax highlighters, a `chatbot-layout` bundle, Puppeteer transport stubs.
  - `db-migrations/` with three SQL migrations dated 2026-09-09 and 2026-09-20.
  - So the app ships a local daemon with its own chat/agent UI beside the CLI path. I did not read these minified bundles. The `NodeWebSocketTransport` / `BrowserWebSocketTransport` stubs are Puppeteer's. Their presence says nothing about how `ego-browser nodejs` connects.
- Not found: no `asar`, no `SKILL.md` for the usage guide (the guide is printed from the SDK `dist/out/ego-browser/SKILL*.md`, whose location inside the app I did not locate in plain text), no node binary exposed as a file other than the helper app.
- `.keystone_install` and `Resources/install.sh` are Chromium's standard Keystone/updater scripts (refcode xattr `com.citrolabs.refcode` is written from the DMG file name).
- Entitlements and code-signing blobs: not read, since they sit in signed binaries/`_CodeSignature`. `embedded.provisionprofile` is a binary file; I did not open it.

---

## 3. Space isolation

CONFIRMED, vendor claims:
- Product doc (https://lite.ego.app/document/en/docs/space): "A Space is: A parallel workspace inside the same ego(lite) browser process. One isolated BrowserContext per task, with its own cookies and storage." "A Space is not: Not a new browser window or a new Chrome profile. Not headless Chrome or offscreen rendering."
- Same page: "every agent task shares ego(lite)'s main process and gets its own native BrowserContext for cookies and storage".
- Resource figure from the same page: about +0.9 GB and +6 processes for 6 concurrent Spaces, versus about 15 GB and 84 processes for separate browser instances with profile copies. It also says "the numbers exist to show the resource model, not to be a precise benchmark."
- `llms-full.txt`: "A Space is a workspace partition inside the same browser process."
- Product intro (docs/product-introduce): "A Space is not a new window, a second Chrome profile, or a headless browser … agents can use the logins you already have there."

CONFIRMED, vendor's own engineering docs contradict "own cookies and storage":
- `REPO/skills/ego-browser/references/clearing-state.md`: "A task space picks a browser profile when it is created, and that profile's cookie jar and HTTP cache are shared with the user's own tabs and with every other task space on the same profile. Measured from inside an agent space on a default profile: the current page saw 1 cookie, the profile held 3478 across 1161 domains."
- It also says `Network.clearBrowserCookies`, `Network.clearBrowserCache` and `Storage.clearCookies` without a `browserContextId` reach "the whole profile".
- Bindings doc: "Profiles determine which browser identity, cookies, storage, and login state a new task space inherits." `createTaskSpace(name, profileId?)` takes the profile when the Space is created. Omitting it picks the "currently active/last-used regular profile".
- So agent Spaces on the same profile **share cookies and logins with the user's tabs**. That is the product's purpose ("the agent inherits the same logged-in state"). The "own cookies and storage" wording on the Space page is not literally true for the default case. I would treat the two statements as describing different things: per-Space tab sets with a shared profile jar.
- The reconciling hypothesis is INFERRED: the "BrowserContext" is the profile's context. A Space is a grouping of tabs and windows inside that context, owned by `agent` or `user`.

CONFIRMED, what a Space is in the browser UI (changelog, https://lite.ego.app/changelog):
- Spaces are first-class user objects, like Arc/Zen workspaces. There are a "Spaces Overview", pinned Spaces, switching shortcuts, a Space "shelf" and per-profile restore. User-created Spaces have `ownership: 'user'` and agent ones `'agent'`.
- Examples: "Open a Space in a new window from the overview while preserving tabs, title, Agent task ownership", "Incognito mode disables Space-switching controls".
- Spaces belong to a Profile: "Agent tasks can now run with a specified browser Profile", and the bindings doc lists `profileId`, `profileName` on each entry.
- There is hidden-window evidence: "Fixed a hidden window appearing when an Agent creates a task tab" and "task windows staying hidden and unable to reopen from the Dock". INFERRED: agent Space tabs live in a window or group that is not the user's visible tab strip, which the user can open from the Overview.
- Other behaviour that follows: popups outside a task open in a regular window, not in a running Task Space ("URLs opened outside an Agent task now open in a regular browser window"). Task tabs are muted by default.

NOT determined:
- Whether the engine enforces any isolation of the user's tabs from the agent's CDP session beyond per-Space target filtering. The CDP bridge visibly restricts targets (`Target.createTarget` blocked) and checks ownership, but I could not see how targets are scoped.
- Whether two agent Spaces on the same profile are cookie-isolated from each other. The clearing-state doc says no ("shared … with every other task space on the same profile").

---

## 4. Snapshot

### 4.1 Vendor claims about "inside the engine"
- docs/product-introduce: "ego(lite) builds Snapshots inside its modified Chromium engine, not with a script added to the page. Because of this, Snapshots include … nested cross-origin iframes, shadow DOM, and third-party components such as Stripe checkout, Salesforce embeds, and Intercom panels."
- docs/skills: "A deeply customized Chromium kernel produces snapshots … Deeply nested iframes … are handled here."
- docs/snapshot: "built from the browser's accessibility tree".
- Changelog 0.4.x: "Page snapshots include more precise page-structure information so Agents can identify page content and elements more accurately."
- So the mechanism is an accessibility-tree-derived serialization done in the browser process or renderer, exposed as native `ego.snapshot()`. Which Blink or AX code it patches is **not stated**.

### 4.2 What the open repo shows about the native API (CONFIRMED)
- `ego.snapshot(options)` options: `scope` (`full_page` default natively, `only_within_viewport`, `subtree`), `root` (a `backendNodeId`), `includeActionMarks`, `interactiveOnly`, `includeStableLocator`, `maxResultLength`. Result: `{content, refs:[{backendNodeId, role, name, loc?}]}` (bindings doc).
- Subtree scope: "In Ego Lite 0.5.0.19, subtree scope requires launching the browser with `--enable-features=SnapshotSubtree`." This is the only browser flag name in the public docs.
- Native requirements doc (`docs/native-snapshot-ref-requirement.md`) is the vendor writing to its own browser team. Quotes:
  - "`ego.snapshot()` already includes text from ordinary iframes and OOPIFs, but actionable nodes inside those frames are currently omitted from `refs`."
  - Asks for `refId` unique across renderer processes, `backendNodeId` renderer-local, and `frameId`. Also: "Capturing the snapshot must not activate, focus, or reload a frame."
  - Asks for a compact tree with "Omit text nodes that are empty … Replace an unnamed `container` with its only remaining child", and keeping `image`, `svg_root`, `canvas`, `iframe`, `root`.
  - The SDK backfills a missing `frameId` from the frame AX tree (`Accessibility.getFullAXTree`) when one backend node id has only one possible owner.
- That tells us the native snapshot is a renderer-assisted AX tree walk covering OOPIFs, with refs derived from backend node ids. This is INFERRED. The SDK also uses `Accessibility.getFullAXTree`, `DOM.getFrameOwner`, `DOM.getBoxModel` over CDP for provenance, and `Target.setAutoAttach` with `flatten:true` plus `filter:[{}]` to attach OOPIFs (`browser-runtime.ts`).

### 4.3 Output format (CONFIRMED from tests, not from a live capture)
There is no real captured snapshot in public docs. The following are from the repo's unit-test fixtures (`EB/src/snapshot-result.test.mjs`, `page-model.test.mjs`), so they show the syntax but are synthetic:

```
[p1 | space "research"(7) ...]            <- header added by the SDK (snapshotSourceHeader)
root
  button "Host action" [ref=1]
  iframe [ref=11]
    root
      button "Frame action" [ref=21]
      iframe [ref=31]
        root
          button "Nested OOPIF action" [ref=41]
  text "Host sibling"
textfield "Prompt" [ref=10, loc=css:#prompt, url=https://example.com/prompt]
button "Save" [ref=11, loc=role:button[name="Save"]]
anchor [ref=6, url=https://example.com/item]
[more content below the viewport: scrollY=0, viewport 778 / page 880 px; scroll or use snapshot({ scope: "full_page" })]
```

- Syntax: two-space indentation encodes the tree. Each line is `role "name" [ref=N, loc=<locator>, url=<href>]`. Roles seen: `root`, `container`, `button`, `textfield`, `link`/`anchor`, `heading`, `text`, `iframe`, `image`, `svg_root`, `canvas`.
- A nested document appears as `iframe [ref=N]` with a child `root`. In the tests this includes cross-origin frames (OOPIF).
- Refs are numbers. The agent uses them as `@21` / `ref=21`. The SDK maps them to a frame + document + backend node (`AGENTS.md`: "v2 Page refs (`@21`, not `@e21`) are SDK-assigned ids bound to one frame/document/backend node").
- `loc=` is a stable locator hint, validated and dropped by the SDK if it is not unique (`unstable` / `ambiguous` are removed).
- Viewport snapshots end with `[more content below the viewport: ...]` or `above` / `above and below`.
- Not diffs: `page.snapshot({diff:true})` throws "page.snapshot received unknown option: diff" (test in `page-model.test.mjs`). So there are no diff snapshots.
- Shadow DOM: the e2e test `page-shadow-dom.mjs` asserts the snapshot contains an input inside an open shadow root. Locators are resolved with "CSS searches nested open shadow roots". Closed shadow roots: not addressed anywhere I read.
- Refs persist across rounds through an on-disk Page ledger; partial snapshots merge by node identity, full snapshots replace the active set. A navigation or changed document invalidates the refs (`Stale ref: @N; take a new snapshot`).
- Sizes: docs say "a typical page fits in 200 to 400 tokens". The docs/snapshot page says an admin page may be 30k tokens in raw HTML. These are marketing figures, not measured.
- The marketing format in docs/product-introduce (`@1 [heading] "Log in"`, `@3 [input type="email"] placeholder="Email"`) differs from the repo format above. The docs/snapshot page uses a third format (`@1 [input] "Search"`). They are older illustrations, the repo is newer. Also `llms-full.txt` says `@e1`, while the repo says "`@21`, not `@e21`".
- `snapshotText()` in the v1 docs prints `[ref=N, loc=..., url=...]` trees, which match the repo.

---

## 5. Unknowns (not determined)

1. How the `ego-browser` launcher reaches the browser (unix socket, named pipe, XPC, local HTTP, other), the socket path, and any auth. Docs say only that the app "dispatches" the heredoc.
2. Whether any remote-debugging port, `--remote-debugging-*` flag, `--remote-allow-origins` or `ws://127.0.0.1` listener exists. The "no TCP port" claim from the earlier thread is unverified by me. Binary analysis was off-limits (ToS).
3. The language and framework of the native `ego-browser` launcher (Mach-O, 1,992,608 bytes). Not inspected.
4. IPC/mojo service names, feature names (beyond the documented `SnapshotSubtree`), Info.plist entitlements, and strings in the `Aperitif` helpers and `libaperitif.dylib`. Not inspected.
5. What "Aperitif" is.
6. The exact engine mechanism of the snapshot (which Blink/AX code is patched, whether the serializer runs in the browser process or the renderer). Only behaviour is known.
7. Whether the app mutates the DOM or accessibility state for the snapshot (docs say it only reads).
8. Full CLI flags and subcommands beyond `nodejs`, `skill`, `upgrade`, `import`, `help` and the server-name option. A task-spaces listing command probably exists but I could not find its name.
9. The npm package name actually published for the skill (`@citrolabs/ego` / `@ego-lite/*` returned 404 on the public registry).
10. A real, captured snapshot from the shipping app. All examples are test fixtures or marketing.
11. How Spaces are rendered: separate hidden window per task or tab grouping inside the user's window. There is hidden-window evidence in the changelog only.
12. Whether two agent Spaces on one profile can see each other's cookies at the engine level (the vendor doc says they share the jar).
13. The x64 DMG and the Windows build were not downloaded or inspected.
14. The contents and role of the 5 MB `local-ego-daemon/runtime/run-*.mjs` and the web client were not read. Their relationship to `ego-browser nodejs` is unknown.
