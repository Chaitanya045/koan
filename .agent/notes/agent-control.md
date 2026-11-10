# Agent control: design (issue #180, phase 1)

Date: 2026-11-07. Status: **design for review, no code.** Chromium 158.0.8127.2. The source audit behind every
claim here is `.agent/notes/upstream/agent-control.md` (parts A to D; cited below as A§n, B§n, C§n, D§n). Where
this note says "verified" it means read in pristine source; "UNVERIFIED" means it needs the Mac (§15).

## 0. Summary

A local agent on the user's Mac (Claude Code, Codex, Cursor, a script, or Vercel's `agent-browser`) drives the
user's **real, signed-in** Koan profile without stealing focus or the mouse, and the user can watch, take over and
hand back at any time. The master switch is **Agent ready, on by default** (a user decision, 2026-11-07, which replaced
"off by default"): the gateway listens, but with no agent paired or attached nothing is kept awake and nothing runs.
It is reachable only through a **unix socket** in a 0700 directory, and every client is **approved by the user in a
native prompt** (a paired key is never accepted silently) and **revocable**.

The five decisions the design stands on:

1. **Koan owns the endpoint; it does not use `--remote-debugging-port`.** A gateway in the browser process listens on a
   unix socket with **raw length-prefixed frames** (no HTTP or WebSocket parser in the browser) and speaks a **filtered
   CDP** with per-method tables where unlisted means deny (§4). Each tab the agent may touch gets an in-process
   **untrusted** `DevToolsAgentHostClient` (the `chrome.debugger` pattern), so the Browser, SystemInfo and Extensions
   domains, the browser target and local-file access do not exist for the agent (C§2c, C§6); cookie-bearing network
   events are scrubbed, key-event `commands` stripped, extension frames and worlds unreachable.
2. **Agents work in their own container** (§7, decided 2026-11-10): a storage partition of its own inside the user's
   profile (its own cookies, storage, cache and processes), shown as a Space in the user's sidebar, one per task,
   Throwaway by default. The agent never sees the user's logins; the user's tabs are invisible to it unless the user
   leases one ("Your tab") or approves a take-over (§9). Agent containers block permissions, passwords, autofill,
   extensions, history and sync (§7.2).
3. **A per-tab keep-alive lease** (visible `IncrementCapturerCount` + `PageLiveStateDecorator::SetIsPinnedTab` and
   `SetGlicActuationState`, both public setters) keeps *only agent tabs* rendering with **no upstream patch and no
   global flag** (§8, A§10, review finding 12). It is light on battery: released 3 s after the last command.
4. **`agent-browser` works through a session-scoped relay, not a weakened endpoint** (§11). It cannot dial a unix
   socket; a loopback relay with an unguessable path, started with and outliving its daemon, hides that. The `koan`
   CLI (C++, built with Chromium's own toolchain) does pairing, lifecycle, take-over and the relay; agent-browser does
   page actions.
5. **Security is a feature, not a flag** (§3): **Agent ready is on by default but inert until a user-approved agent
   attaches** (no listener in Incognito, Guest or a non-default user-data-dir unless turned on there; the indicator
   ships before the default-on gateway is reachable by a real build), keypair pairing with a native prompt naming the
   *verified* executable and a pinned identity, Keychain-bound client records, capabilities, indicator, no incognito,
   no cookie/storage/clipboard access, a size-capped audit log, and any user input in a driven tab pauses the agent.
   The Chrome 136 gap that Koan has open today ships separately as PR #186 (§3.2).

**Speed is phase 3** (§16, §20): phases 2a-2d ship security and background capability and make no speed claim. The
in-browser snapshot/act/run, diffs and event waits are phase 3, and "faster than ego" is claimed only from measured
numbers with ego's run included.

The maintainer's decisions on the first review are recorded in §17. §19 records how ego(lite) works from public sources and
decides, technique by technique, what Koan adopts, improves or rejects; §20 is the plan to be faster than it, with a
benchmark harness spec that can also import externally measured baselines.

## 1. Requirements and how they are met

| Requirement | Mechanism | § |
|---|---|---|
| Agent opens new tabs and works in them while the user works in another tab | `Koan.openTab` creates a background tab in the agent's container and its Space through Koan code (`koan::containers::OpenAgentTab`) (not `Target.createTarget`, which foregrounds, B§5); the lease keeps it rendering | 7, 8 |
| Agent sees the user's current tab, takes it over midway | `Koan.listTabs`/`Koan.read` (capability `observe`/`read`), `Koan.takeOver` (user approves per tab), banner with Take over / Release | 9 |
| Screenshots and animations work while the window is unfocused or covered | lease: visible capturer + priority vote + freeze exemption; a detached/non-active tab renders into its own compositor (A§2) | 8 |
| Agent never steals focus | `Page.bringToFront`/`Target.activateTarget` are no-ops; tabs are created inactive; no `ShowInactive` where avoidable | 4.3 |
| Handoff with an indicator; user input pauses the agent | `InputEventObserver` + the `kFromDebugger` bit; sticky pause until the user hands back | 9 |
| Works on the user's real profile | the gateway runs in the main browser process; no separate profile | 4 |
| Secure; incognito excluded; threat model documented | §3 | 3 |
| agent-browser compatibility (maintainer, 11-06) | §11 | 11 |
| ego(lite) ideas (maintainer, 11-06) | agent Space §7, skill §12, in-engine snapshot §10, watch/take-over §9 | |
| **Agent ready, on by default; free when idle; light on battery** (user decision, 11-07) | a listener with no agent attached costs nothing: no lease, no capturer, nothing unthrottled; off means no listener and nothing scheduled, at once; fewer leases and no long keep-alives on battery in Low Power Mode | 3.3 (T21, T22), 6.1, 8 |

## 2. Architecture

```
 agent (Claude Code / Codex / script)
   |  runs shell commands                      agent-browser CLI --> agent-browser daemon (Rust, raw CDP, up to 1 h idle)
   v                                                                   |  AGENT_BROWSER_CDP=ws://127.0.0.1:P/cdp/<secret>   (env, handed to the daemon at spawn)
 koan (C++, signed, Koan.app/Contents/Resources/bin)  <--- koan-relay (session-scoped; loopback; WebSocket<->frames)
   |  length-prefixed frames over a unix socket   ~/Library/Application Support/Koan/AgentControl/agent.sock (dir 0700, sock 0600)
   v
 KoanAgentGateway (browser process; own accept loop on net::UnixDomainServerSocket; NO HTTP, NO WebSocket parsing here)
   |- pre-auth: peer audit token (LOCAL_PEERTOKEN) -> exe path + code signature + ancestors; 4 KB frame cap; connection cap
   |- KoanAgentSession: Koan.pair / Koan.authenticate (signature over a server nonce), capabilities, audit log, rate limits
   |- KoanAgentTargetRegistry: which tabs this client may see/drive (agent Space + handed-over); rejects incognito/guest/WebUI/extension/DevTools
   |- CDP facade: Target.* emulated, Browser.getVersion, per-method policy tables ("unlisted = deny"), event scrubbing
   |- per tab: KoanAgentClient (IsTrusted=false, no local files, strict frame/URL vetting)
   |        -> DevToolsAgentHost::GetOrCreateFor(contents)->AttachClient
   |- AgentTabLease (WebContentsUserData): keep-alive, take-over state, user-interaction observer
   `- UI: indicator, pairing/take-over/userNeeded prompts, toasts, Settings > Agent control
```

Rationale for rejecting the stock transports is in C§1b, C§2 and rows 18-21 of the decisions table in the upstream
note: the `DevToolsHttpHandler` is a process-wide slot that `chrome://inspect` also uses, its WebSocket client is
trusted (file read and write, browser target), `/json` leaks target ids unauthenticated, and
`--remote-debugging-pipe` needs file descriptors inherited at launch. **Framing (decided 2026-11-07):** only `koan` and
`koan-relay` talk to the socket, so the browser process speaks a minimal length-prefixed protocol and never parses
HTTP or WebSocket from a local process. The relay translates for CDP clients that need WebSocket (§11).

## 3. Threat model

### 3.1 What is being protected, and against whom

**Assets.** (1) The user's logged-in sessions: acting as the user on every site (a page-driving agent *is* the
user as far as sites can tell). (2) Cookies, localStorage, saved passwords/payment data, history, bookmarks.
(3) Local files (upload/download, `file://`). (4) The browser itself (extensions, settings, quitting it).
(5) The user's attention and consent: nothing should happen invisibly.

**Actors.**

| Actor | Capability assumed | In scope? |
|---|---|---|
| Web page / ad / iframe in any tab | JS in the user's tabs; can `fetch` to loopback; can try DNS rebinding and cross-protocol attacks | yes: must never reach the endpoint or inject into the agent channel |
| Local process, **other uid** | can `connect()` to loopback TCP ports, not to a 0700 dir | yes |
| Local process, **same uid, unpaired** (malware, a random dev tool) | can read user files, run `koan`, dial the socket | yes: must not get control without a visible prompt |
| Local process, **same uid, paired** or with a stolen credential | holds a valid token | partly: see 3.5 |
| The **agent itself** (a model that can be prompt-injected by a page it reads) | acts through the allowed methods | yes: capability limits, scope, indicator, pause |
| A user who is not paying attention | approves a prompt without reading | partly: prompts name the executable and ancestors, scopes are narrow, strict mode exists |

### 3.2 Chrome 136+, what it restricts and why, and Koan's gap

Chrome 136 (March 2025; from Chrome's announcement, which is not in this source tree) stopped honouring `--remote-debugging-port` and `--remote-debugging-pipe` when the user
data directory is the **default** one. The attack it closes is info-stealing malware that relaunches the user's
browser with a debugging port against the real profile and then reads cookies and sessions over CDP (a port on
loopback is full, unauthenticated control to every local process, and to web pages through DNS rebinding or
cross-protocol requests). The sanctioned workaround is a **non-default** `--user-data-dir`, which by construction
has none of the user's logins. Chrome later added `chrome://inspect#remote-debugging`: loopback TCP, a modal
Allow/Don't-allow dialog per connection and an infobar while connected. That is the only way to debug the default
profile, and it is on by default in 158 behind a pref that is off (`kDevToolsAcceptDebuggingConnections`, C§1b).

In 158 source: the check is `IsRemoteDebuggingAllowed()` in `chrome/browser/devtools/remote_debugging_server.cc:161-183`,
which compares `DIR_USER_DATA` with `GetDefaultUserDataDirectory()` (`chrome_paths.cc:524`), and it is **compiled
in only under `GOOGLE_CHROME_BRANDING`** (`:169-174`); for other branding it is off unless
`EnableDefaultUserDataDirCheckForTesting()` is called (`:290-295`, a public static, no callers). **Koan is not
Chrome-branded, so `Koan --remote-debugging-port=9222` on `~/Library/Application Support/Koan` works today.** That
is exactly the infostealer pattern; it is a hole in Koan now, independent of agent control. A Koan that is already
running ignores the switch (the second launch forwards to the first), so the exposure is a launch by something else
while Koan is not running: that instance opens the real profile with the debug port on.

**Decision (recommended, small): turn the check on in Koan** by calling
`RemoteDebuggingServer::EnableDefaultUserDataDirCheckForTesting()` at startup (before `GetInstance()`), from a
Koan startup hook. No new patch if a startup hook exists; otherwise a one-line change of the branding condition.
Dev and test flows (`scripts/e2e/run.sh`, `run_profile.sh`, `scripts/mac/*`) already pass a non-default
`--user-data-dir`, so they are unaffected. **It shipped as its own PR, #186** (the knob's initial value, not a startup
call, so ordering does not matter). It is a speed bump, not a boundary: a process running as the user can copy the
profile or start Koan with another `--user-data-dir` (the Keychain ACL admits Koan.app either way); it stops the silent
one-flag attack, not malware that already runs as the user. Agent control does not depend on it: it uses no switch. Keep `chrome://inspect#remote-debugging`
as upstream has it (modal approval, off by default; "lock as is"), but our settings page says which of the two
doors is which.

How agent control compares with Chrome's approval mode:

| | `chrome://inspect#remote-debugging` | Koan agent control |
|---|---|---|
| Transport | loopback TCP, any local uid can connect | unix socket, 0700 dir, uid + pid check; optional loopback relay with a secret path only for clients that cannot do sockets |
| Auth | none; a modal per connection | pairing approval naming the executable, an Ed25519 key per client proved by signature, pinned identity, revocable, optional ask-every-time |
| Privilege | **trusted browser-target session** (every domain, `Extensions.loadUnpacked`, `Browser.close`, cookies of all profiles) | untrusted page-level clients, deny-by-default method table, no browser target |
| Scope | everything incl. incognito | agent Space + handed tabs; incognito excluded |
| Visibility | infobar | indicator, banner, menu, toasts, audit log |

### 3.3 Threats and controls

| # | Threat | Control |
|---|---|---|
| T1 | A web page reaches the endpoint (loopback port, DNS rebinding, `Origin`-less WebSocket) | The browser listens on **no TCP port** and parses **no HTTP or WebSocket**; a browser cannot dial a unix socket. The relay (§11) is a separate process: ephemeral 127.0.0.1 port, one path `/cdp/<128-bit secret>`, any request with an `Origin` header refused, `Host == 127.0.0.1:<port>` required (the stock guard is `devtools_http_handler.cc:129-137,478-484`), a second simultaneous connection refused, session-scoped lifetime, killed on revoke |
| T2 | Another local user connects | 0700 directory + 0600 socket + uid check on accept; the relay's secret path |
| T3 | An unpaired same-uid process drives the browser | The first connection from an unknown client gets **only** `Koan.pair`; the user sees a native prompt whose headline is the **verified executable** (audit token → path, code-signing identity, ancestors), with the client's own label shown as "calls itself". Unanswered prompts time out (60 s); one prompt is open at a time and a user gets 3 a minute by default (a setting, 1 to 10, §6.1), whatever the path of the program (PR #190). With the switch on by default this prompt is the only way in, and it is never skipped: a paired key is never accepted silently. **Client records are encrypted and MACed with Keychain-bound `os_crypt` storage** (§5.4), so editing plaintext Local State while Koan is not running cannot add a client |
| T4 | A stolen credential is replayed | There is no shared secret in the browser: clients hold an Ed25519 private key and sign a single-use nonce (§5.2). Paired identity is **pinned** (path, signature, ancestors) and a change re-prompts (§5.5). Credentials expire (30 days) and are revocable (flushed before returning, §5.4). **Residual**: a same-uid process that reads `~/.koan/clients/<id>/key` and runs from the same chain is the paired client (§3.5), which is why `observe` is off and `drive` is per tab |
| T5 | The agent exfiltrates cookies, storage or credentials | Layered: (1) `Storage.*`, `DOMStorage.*`, `IndexedDB.*`, `CacheStorage.*`, `Network.{getCookies,getAllCookies,setCookie,setCookies,deleteCookies,clearBrowserCookies,clearBrowserCache,loadNetworkResource}` and everything unlisted are **denied** by name (§4.3); `Browser.*` and `SystemInfo.*` do not exist for an untrusted client. (2) **`Network.enable` is required by agent-browser and, upstream, leaks cookies including HttpOnly values and `Authorization` through `requestWillBeSentExtraInfo`/`responseReceivedExtraInfo`** (`network_handler.cc:1113-1135, 4033-4058`; review finding 1): the facade rewrites those events and passes only an allowlist of Network events (§4.5), tested by T-sec-4. (3) `getResponseBody` and `setExtraHTTPHeaders` need capability `network` (not offered in v1). **Honest limit**: the agent can run JS in a page, so it can read what that page's JS can read (`document.cookie` minus HttpOnly, that origin's localStorage, the DOM). It is **not limited to the origin it started on**: it can navigate its tab to any site you are logged into and read that page, which is the shared-login point of decision 15. It cannot read HttpOnly cookies, the password store or the cookie jar |
| T6 | The agent reads passwords / autofill | The `Autofill`, `Security`, `Browser` domains are absent for untrusted clients. Risk: password-manager autofill into a form the agent drives, then JS reads the value. Chrome hides autofilled passwords from script until a user gesture; a CDP-injected click may count as one (UNVERIFIED, test T-sec-1, §15). Mitigation if it does: disable password/autofill UI in agent-owned tabs (`ContentAutofillClient`/`PasswordManagerClient` per WebContents) |
| T7 | The agent reaches privileged or sensitive pages and worlds: `chrome://`, Koan settings, DevTools, `file://`, the Agent-control page, **extension frames injected into ordinary pages and extension content-script worlds** | A strict Koan-only allowlist (§4.4): a frame is reachable only if http(s)/`about:blank`/the unreachable-error URL, non-WebUI, non-privileged, non-guest, non-PDF/MimeHandler, with every ancestor and the embedder passing the same; `chrome-extension://` frames are not attached; execution contexts other than the main world or the agent's own isolated worlds are dropped and their `contextId`s rejected. `MayReadLocalFiles=false` blocks `file://`, `DOM.setFileInputFiles` and `Network.loadNetworkResource`. Network-error pages do not detach the tab (the original draft would have) |
| T8 | The agent raises windows, switches tabs, quits, resizes, loads extensions | `Page.bringToFront`/`Target.activateTarget` → no-op success + log; `Browser.*`, `Extensions.*`, `SystemInfo.*` absent; `Target.createBrowserContext`/`disposeBrowserContext`/`attachToBrowserTarget` denied; window bounds denied |
| T9 | Prompt injection: a page the agent reads tells it to do harm | Cannot be solved in the browser. Reduced by: agent-Space scope, per-tab consent for the user's own tabs, optional per-client **site allow/deny list** (default: deny nothing, but banking/payment categories are a settings suggestion), indicator, pause on user input, audit log, `Fetch`/network interception off by default |
| T10 | Hidden action: the user does not know the agent is active | Persistent indicator (§6.2), banner on a driven visible tab, toast, menu-bar item, agent-Space glow; "Stop all agents" one click away; audit log. **The indicator ships before, or in the same PR as, anything that makes the default-on gateway reachable by a real build** (T21) |
| T11 | Incognito leaks | Excluded by default for every client (§13) |
| T12 | The agent changes the user's data through the page (deletes, buys) | Out of scope for the browser (the same as any extension). Reduced by capability `drive` (take-over of user tabs needs per-tab approval) and the audit log. The agent Space is where the agent acts by default |
| T13 | Downloads / uploads | `Page.setDownloadBehavior` denied; downloads go through the normal download UI and Safe Browsing; uploads need a Koan-mediated, user-approved path (a later `Koan.upload`) |
| T14 | Denial of service: floods, leases, pre-auth bytes, parser bugs | Raw length-prefixed frames, **4 KB cap before authentication**, connection and per-peer caps, 10 s incomplete-frame timeout (§4.1); per-connection rate limits; a lighter lease with a hold-off and a concurrent cap of 4 by default, a setting from 1 to 16 (§6.1, §8); tab caps 8 per task, 12 per client, 16 total |
| T15 | Policy bypass | `RemoteDebuggingAllowed=false` stops the gateway and closes sessions; `DeveloperToolsAvailability` is enforced on every attach (C§1a) |
| T16 | Upgrade drift: a Chromium roll changes which domains or methods an untrusted client gets | The protocol-coverage unit test of §4.3 fails on any unclassified CDP method; REBASE rows name the symbols and the behaviours; a Mac test per denied domain (§14, §15) |
| T17 | The agent reads or writes the OS clipboard (`Input.dispatchKeyEvent {commands:["paste"]}` pastes whatever the user last copied, such as passwords or 2FA codes) | `commands` is stripped from every key event (§4.5); no `clipboard` capability in v1; T-sec-10 |
| T18 | A page the agent drives takes the user's attention or focus: `window.open`/`target=_blank`/popups (CDP clicks carry a user gesture), a `<select>` native menu, a file chooser sheet, HTTP auth, client-cert picker, external-protocol launch, print, permission bubbles | Page-opened tabs open in the background in the agent Space (hook #2, §14); `ForbidExternalPopupMenus()` in the lease; file chooser interception forced on; the others become **`userNeeded` approval prompts** that pause the tab and ask the user, never a silent native sheet (§9.4); T-focus-3..7 |
| T19 | Agent browsing leaks into the user's own surfaces: history, omnibox suggestions, Recently Closed, saved tab groups and sync, session restore, session files | Required. The agent works in a container (§7), so its cookies and storage never mix with the user's; the blocks of §7.2 keep its visits out of History, the omnibox, `@tabs` and sync; the restore hook blanks agent tabs (§7.1); conditional write-side hooks keep them out of the Recently Closed list and the session files (§7.4); T-hist-1, T-sync-1, T-sync-2, T-block-5, T-cont-8 |
| T20 | Audit-log tampering or secret leakage through the log | Best-effort integrity (hash-chained records, chain key Keychain-bound), size-capped, URL paths redacted by default, nothing typed or evaluated stored (§6.3). A same-uid attacker can still delete it; the log is evidence for the user, not a security boundary |

| T21 | **Agent ready is on by default**: every launch has a listener, so a same-user process can reach the socket without the user having done anything | Nothing new is reachable by default compared with "on after the user turned it on", and the controls are the same ones, stated here because they now carry the whole load: (1) **the first-contact native prompt is mandatory**; the only methods before authentication are `Koan.pair` and `Koan.authenticate`, a paired key is never accepted silently, and the prompt limits of #190 stand (one open, which is fixed, and 3 a minute per user by default, a setting that is never 0 or unlimited; a refusal or timeout counts as a failure). (2) 0700 directory, 0600 socket, `flock` ownership, uid check, nothing read until the peer is resolved, 8 connections / 4 unauthenticated / 2 per program, 30 s to authenticate, 4 KB frames (§4.1, §5.2). (3) **No listener in Incognito, Guest or a non-default `--user-data-dir` unless the user turns it on there** (§4.1, §13). (4) The enterprise policy `RemoteDebuggingAllowed=false` still forces it off and greys the row. (5) **The indicator (§6.2) ships before, or in the same PR as, anything that makes the default-on gateway reachable by a real build**; wiring the gateway into `BrowserProcess` stays behind the indicator PR, so no build with a default-on listener can run without a visible "agent connected" state. (6) An idle listener holds no lease and no capturer and unthrottles nothing (§8). Residual: a listener that is always there is attack surface that was optional (a parser bug in the pre-auth frame path is now reachable by default): the pre-auth path is small and fuzzed (`koan_agent_frame_fuzzer`, `koan_agent_session_fuzzer`), and Off removes it entirely |
| T22 | Agent control costs battery or memory for a user who never uses it, or too much for one who does | Idle cost is the listener only (§8). Only tabs an agent is attached to hold a lease, within the caps (capturer hold-off 3 s, **4 leased tabs by default, a user setting from 1 to 16**, 8/12/16 open tabs, fixed for now); **on battery with Low Power Mode on, half the leased-tab setting (2 by default, minimum 1; a setting can turn the halving off), and `live: always` and `keepAlive` over 60 s are refused with a clear error** (§8, §6.1, T-power-2, T-power-3). Off releases everything at once |

### 3.4 Controls the maintainer asked for, mapped

| Control | Where |
|---|---|
| **Agent ready** (master switch; on by default since 2026-11-07, was "off by default") | pref `koan.agent_control.enabled` (local state), **default true** in the default user-data-dir and false in Incognito, Guest and a non-default `--user-data-dir` launch (§4.1). On: the gateway listens; with no agent paired or attached nothing is kept awake. **Off: no listener and nothing scheduled (the old "off" row, with the accept loop sharing the browser's IO thread instead of owning one), leases released, sessions closed, at once and without a restart** (§6.1). The first-contact prompt is mandatory either way |
| Only a unix socket or loopback + token; no unauthenticated port | unix socket only in the browser; relay is a client-side process (§11) |
| User approves each new client, native prompt naming the client, revoke in settings | §5 |
| Visible indicator while connected (must ship before the default-on gateway is reachable by a real build) | §6.2 |
| Incognito excluded by default; no listener in Incognito, Guest or a non-default user-data-dir unless turned on there | §13, §4.1 |
| No access to passwords/cookies/storage beyond what the page exposes unless allowed | T5, T6 and the capability `cookies` (§5.3) |
| Audit log | §6.3 |
| User input in a driven tab pauses the agent | §9 |

### 3.5 What this model does not protect

Any code the user runs as themselves is trusted *as the user* in macOS's model. A paired client's private key on disk
is readable by any same-uid process, and anything that can run the signed `koan` binary from an acceptable chain can
speak as a paired client. We therefore do not claim the key defeats malware that already runs as the user, and review
finding 6 is right that this is the dominant real threat on a developer Mac (another script, an `npm` postinstall).
What we claim: it cannot **start silently** (first use and any change of paired program or chain prompts, naming the
verified executable), it cannot be **invisible** (indicator, live chain, audit log), it cannot reach the cookie jar,
HttpOnly values, the browser, the extensions, the clipboard or incognito, it cannot forge a pairing by editing a file,
and it is revocable and expiring. Hardening kept for later: a Keychain item whose ACL is the signed CLI (UNVERIFIED that
Koan.app's ACL can list a second binary, C§5); Touch ID (LocalAuthentication) for strict mode and for `drive`.

## 4. Transport and protocol

### 4.1 Gateway (framing decided: raw length-prefixed frames on the unix socket)

New `//koan/browser/agent/` target. **Koan's own accept loop** runs on the browser's existing IO thread (a dedicated `Koan_AgentIO` thread would be an idle thread in every default launch; the file-system work of starting, and the signature checks, run on thread-pool tasks that may block, as in PR #191),
on `net::UnixDomainServerSocket(auth_cb, /*use_abstract_namespace=*/false)` using `AcceptSocketDescriptor()`
(`unix_domain_server_socket_posix.h:77`), because the raw descriptor gives the peer's **audit token**
(`getsockopt(LOCAL_PEERTOKEN)`) per connection; `net::HttpServer` hides it, and `UnixDomainServerSocket::Credentials`
carries only uid and gid on macOS (C§2a, C§5). No `net::HttpServer`, no WebSocket and no HTTP parser runs in the
browser process.

* **Frame**: `u32` big-endian length, then a payload of UTF-8 JSON in CDP shape (§4.2). **Before authentication** the
  limit is 4 KB and only `Koan.pair`/`Koan.authenticate` are parsed; **after**, a frame may be up to 32 MB
  (large screenshots and PDFs also travel as `IO` stream reads of ≤ 1 MB chunks), and a connection with an
  unfinished frame for more than 10 s is closed. Per-connection and global caps on bytes buffered; connection cap
  (default 8) and a per-peer failure back-off. Malformed JSON, a negative or oversize length, a second
  `Koan.authenticate` after success: close.
* **Peer check on accept**: `uid == geteuid()`; then the audit token → `SecCodeCopyGuestWithAttributes` → code-signing
  identity and executable path (`proc_pidpath`), and the ancestor chain, pid-reuse-proof because the audit token
  carries the pid version (a bare `LOCAL_PEERPID` is racy, review finding 5). These facts feed pairing (§5), pinning
  and the audit log. **Built (PR 4, `koan_agent_peer.*`, `koan_agent_gateway.*`):** the token is read with
  `getsockopt(LOCAL_PEERTOKEN)` at accept, on the owning sequence (the descriptor could be closed and its number reused
  while a worker runs, so the worker gets the token, not the descriptor). The lookup (signing identity, ancestors)
  runs on a thread-pool task, and **no byte is read from the connection until it has succeeded** (5 s deadline). The
  signing identity is `apple` for a platform binary, `team:<id>`, `cdhash:<hex>` for an ad hoc signature (so a rebuilt
  unsigned tool is "changed"), or empty for unsigned; a signature that does not verify is refused. The lookup fails
  closed. The connection is also refused at accept when the cap is full (below), the uid is not the browser's, the peer
  has no executable path, or the peer is blocked. **Looking again** (`Unchanged`, cheap: the pid's start time from
  `proc_bsdinfo`, which tells a reused pid from the process, and its executable path, which an exec of another program
  changes; the audit token's pid version, which would say "has it exec'd" directly, can only be checked through the
  Security framework, so it is used once, at lookup. Apple's `proc_uniqidentifierinfo` is private and was not usable) happens before a proof is verified and before the user's approval is
  applied, so an `exec` after connecting, or during the 60 s dialog, ends the session (`kUnknownPeer`).
  *What this does not tell:* `LOCAL_PEERTOKEN` is the connector's token. A child that inherited the descriptor is
  indistinguishable from the process that connected (it is the connector's decision to share it, and the connector is
  what the user was shown and the pin names), and the token is fixed at connect time, so "again" means the same pid's
  current state, not a fresh read of the socket.
* **Socket hygiene** (verified, C§2a, C§5): `bind()` leaves a stale file and a 0777-minus-umask mode. Whoever holds an
  exclusive `flock` on a lock file named after the socket (`agent.sock.lock`, `<hash>.sock.lock`: one per socket, since the fallback directory is shared by every long-path profile) owns the path, and the kernel drops it when that browser crashes, so a
  socket found while the lock is free is dead and is `unlink()`ed, and one found while it is held is left alone. (A
  `connect()` probe was the first design and is wrong: a full backlog is `ECONNREFUSED` on macOS, so a busy live gateway
  would look dead, and the probe makes a live gateway accept a connection.) The directory is made with `mkdir(0700)`
  and then only reached through a descriptor opened with `O_NOFOLLOW|O_DIRECTORY` (`fstat` for the owner, `fchmod` to
  tighten), so a symlink swapped in cannot redirect a check or a `chmod`; the socket is `chmod`ed 0600 after `bind()`,
  and `Stop()` unlinks only the file whose device and inode it recorded after the bind, never whatever is at the path.
  `sun_path` allows 103 characters, so a long `--user-data-dir` falls back to `<per-user temp dir>/koan-<uid>/<hash>.sock`
  (`confstr(_CS_DARWIN_USER_TEMP_DIR)`, a 0700 directory, rather than a shared `/tmp` where another user can create
  `koan-<uid>` first), then `/tmp`. Making the directory and taking the lock run on a thread that may block. A discovery file `AgentControl/agent.json` (0600: socket path, pid, version, profile) lets the CLI
  find it; the CLI validates the pid and a `connect()` before trusting it.
* **Threads**: socket I/O on the IO thread; **parsing, scrubbing and serialising of large CDP traffic on a worker
  thread**, with only decisions (attach, dispatch, lease) on the UI thread, because all `DevToolsAgentHost` calls are
  UI-only and the user is working in the same process (review finding 14). Events the policy does not need to inspect
  are forwarded as bytes untouched; evaluate `UsesBinaryProtocol()` on the client to skip content's CBOR-to-JSON
  transcoding (phase-2 measurement).

Lifetime: started when the pref is true (**the default**, "Agent ready") and the first window exists; torn down on pref
false, policy change (`kDevToolsRemoteDebuggingAllowed=false`, which also forces the pref off), revoke-all, and app exit,
**immediately and without a restart** (the listener gone and nothing scheduled, leases released, sessions closed). **Where it is not on
by default:** a browser started in Incognito (`--incognito`) or Guest mode, or with a non-default `--user-data-dir`
(a profile someone points the browser at for a test, a script or a second identity), does not start the gateway unless
the user has turned the switch on for that user-data-dir (local state is per user-data-dir, so an explicit setting
there wins). The default is computed once at pref registration from the launch, so it never changes under a running
browser. **Wiring the gateway into `BrowserProcess` ships in or after the indicator PR (§6.2, T21), not before.** No upstream hook for the lifetime is expected (a pref
observer and the existing per-window controller start it); the exit hook is a phase-2 detail.

### 4.2 Wire protocol

Payload JSON, CDP-shaped: `{id, method, params, sessionId}` / `{id, result|error}` / `{method, params, sessionId}`.
Two namespaces on one connection:

* **`Koan.*`**, handled in the gateway: `pair`, `authenticate`, `status`, `listTabs`, `openTab`, `closeTab`,
  `createTask`/`endTask`, `read`, `takeOver`, `release`, `resume`, `keepAlive`, `watch`, and later `snapshot`, `act`,
  `run` (§10, §20).
* **Standard CDP**, so unmodified CDP clients work through the relay: a **browser-level facade** answers
  `Target.setDiscoverTargets`, `getTargets`, `getTargetInfo`, `attachToTarget {flatten:true}`,
  `setAutoAttach {waitForDebuggerOnStart, flatten}` (with `Runtime.runIfWaitingForDebugger`), `createTarget`,
  `closeTarget`, `Browser.getVersion`, emits the matching events, and forwards session-scoped commands through the
  policy tables to the tab's untrusted client. Target ids in the facade are real ids of tabs in the client's scope and
  **nothing else is listed**, which is what makes `agent-browser`, which attaches to every page target on connect
  (D§3), see only the agent's Space.

**Built (CDP forwarding PR, `koan_agent_facade.*`, `koan_agent_forwarder.*`, `koan_agent_devtools_client.*`):** one facade per
authenticated connection on the UI thread. A tab command passes `Classify()`, `TargetRegistry::MaySend()` and the row's
`ApplyParamFilter()` (with the tab's `ContextTracker` and the session's `StreamRegistry`), is re-serialised with a facade id and
sent through the lease's `RunCommand()`; its answer gets the client's id back after the result hooks (`createIsolatedWorld` →
the tracker, `printToPDF` → the stream registry). Every event passes `FilterEvent()` and, for context events, the tracker. Every
command is audited (denied ones at once, forwarded ones when answered, with the duration). `Target.setAutoAttach` inside a tab
succeeds and attaches nothing: child targets (OOPIFs, workers) are not proxied yet (T-cdp-3). Parsing and scrubbing run on the
UI thread for now; moving them to a worker is the measurement of §4.1 (T-perf-1 in the PR's test plan). `Koan.openTab` and
`Target.createTarget` call the `AgentTabOpener` the agent containers install (#202): every task gets a throwaway container,
and the request carries only the client, the task's name and the URL, so nothing the agent sends can name a saved container. A
null tab (no normal window with Spaces) is `Koan.noWindow`. `containers::EndAgentTask()` runs for a task on `Koan.endTask {task}`,
and for each of a client's tasks when its last connection goes, on Stop all agents and on Agent ready off (not at quit: a throwaway
container is in memory). A one-shot CLI command therefore ends its tasks when it disconnects: the CLI keeps one connection for a
task's life (`koan shell`, the relay).

### 4.3 Method policy: per-method tables, **unlisted = deny**

Rules: (1) no domain wildcards; a method is allowed only by name; (2) the table is data, in one file, and a unit
test (T-ui-3) **enumerates every method of the 158 protocol JSON and fails if any is not classified** allow, gated,
no-op or deny, so a Chromium roll that adds a method cannot slip through; (3) every denial is an explicit CDP error
(never a silent drop) plus an audit entry; (4) param filters are part of the entry: the
table has a **`ParamFilter` column** naming the filter each forwarded row needs (`NavigationUrl`, `StripKeyCommands`,
`IsolatedWorld`, `RuntimeContext`, `Screenshot`, `NetworkEnable`, `OwnStream`, `OwnTabOnly`); `kAllow` means "forward after the
filter", never "forward unchanged"; a unit test pins exactly which rows carry which filter, and nothing routes client traffic
through the table until the filters exist; (5) events pass through an
allowlist and a scrubber, not the reverse.

| Domain | Allowed by name (initial; each verified against the 158 protocol JSON in phase 2) |
|---|---|
| `Page` | `enable`, `disable`, `navigate`, `reload`, `stopLoading`, `getFrameTree`, `getLayoutMetrics`, `getNavigationHistory`, `navigateToHistoryEntry`, `captureScreenshot` (see below), `printToPDF`, `addScriptToEvaluateOnNewDocument`, `removeScriptToEvaluateOnNewDocument`, `handleJavaScriptDialog`, `startScreencast`, `stopScreencast`, `screencastFrameAck`, `createIsolatedWorld`, `setLifecycleEventsEnabled`, `setDocumentContent`. `setInterceptFileChooserDialog` is **forced on by Koan** (§9) |
| `Runtime` | `enable`, `disable`, `evaluate`, `callFunctionOn`, `awaitPromise`, `getProperties`, `releaseObject`, `releaseObjectGroup`, `runIfWaitingForDebugger`, `addBinding`/`removeBinding`, `discardConsoleEntries`; **`contextId`/`uniqueContextId` must name the main world or a world the agent created** (§4.4) |
| `DOM` | `enable`, `disable`, `getDocument`, `describeNode`, `resolveNode`, `requestNode`, `querySelector`, `querySelectorAll`, `getBoxModel`, `getContentQuads`, `getNodeForLocation`, `getFrameOwner`, `getAttributes`, `getOuterHTML`, `focus`, `scrollIntoViewIfNeeded`, `pushNodesByBackendIdsToFrontend`. **Not** `setFileInputFiles`, `getFileInfo`, `setNodeValue`, `setOuterHTML`, `removeNode`: writes go through `Runtime` or `Input` |
| `Accessibility` | `enable`, `disable`, `getFullAXTree`, `getPartialAXTree`, `queryAXTree`, `getChildAXNodes`, `getRootAXNode`. Renderer-side, not trust-gated upstream (review correction), so reachable; OOPIF behaviour still tested (T-cdp-3) |
| `Input` | `dispatchMouseEvent`, `dispatchKeyEvent` (**`commands` stripped**, §4.5), `dispatchTouchEvent`, `insertText`, `imeSetComposition`, `synthesizeScrollGesture`, `synthesizeTapGesture`. **Not** `setIgnoreInputEvents` (would lock out the user), `dispatchDragEvent`, `setInterceptDrags`, `emulateTouchFromMouseEvent`, `cancelDragging` |
| `Emulation` | `setDeviceMetricsOverride`, `clearDeviceMetricsOverride`, `setUserAgentOverride`, `setEmulatedMedia`, `setLocaleOverride`, `setTimezoneOverride`, `setGeolocationOverride`, `clearGeolocationOverride`, `setTouchEmulationEnabled`, `setScrollbarsHidden`, `setDefaultBackgroundColorOverride`. **Not** `setFocusEmulationEnabled` (the lease does it), `setVirtualTimePolicy`, `setCPUThrottlingRate`, `setIdleOverride` |
| `Network` | `enable`, `disable`, `emulateNetworkConditions`, `setCacheDisabled`. **Gated by capability `network`** (off in v1): `setExtraHTTPHeaders`, `getResponseBody`, `getRequestPostData`. Never: `getCookies`, `getAllCookies`, `setCookie`, `setCookies`, `deleteCookies`, `clearBrowserCookies`, `clearBrowserCache`, `loadNetworkResource`, `setRequestInterception`, `takeResponseBodyForInterceptionAsStream` |
| `Fetch` | all methods gated by capability `network` (off in v1) |
| `Target` | the facade of §4.2 only. `createBrowserContext`, `disposeBrowserContext`, `attachToBrowserTarget`, `exposeDevToolsProtocol`, `setRemoteLocations`: deny |
| `Browser` | `getVersion` only (liveness check; answered by the facade) |
| `Log`, `Inspector`, `IO` | `Log.enable/disable/clear`, `Inspector.enable/disable`, `IO.read`/`IO.close` for streams created by the session (`IO.resolveBlob` deny) |
| no-op success + audit | `Page.bringToFront`, `Target.activateTarget` (agent-browser's `tab switch` calls them and ignores errors, D§4; agent tabs are never raised) |
| everything else | **deny**: `Storage.*`, `DOMStorage.*`, `IndexedDB.*`, `CacheStorage.*`, `Security.*`, `Autofill.*`, `Extensions.*`, `SystemInfo.*`, `Debugger.*`, `Profiler.*`, `HeapProfiler.*`, `Tracing.*`, `Memory.*`, `Overlay.*`, `CSS.*`, `Audits.*`, `WebAuthn.*`, `ServiceWorker.*`, `DeviceOrientation.*`, `Page.setDownloadBehavior`, `Page.setWebLifecycleState`, `Page.setBypassCSP`, `Page.getResourceContent`, and every method not named above |

**URL scheme filter.** `Page.navigate`, `Page.navigateToHistoryEntry`'s target, `Target.createTarget`, `Koan.openTab` and
`Koan.run`'s `go` step accept only `http:`, `https:` and `about:blank`. Every other URL (`javascript:`, `file:`, `chrome:`,
`chrome-untrusted:`, `chrome-extension:`, `view-source:`, `data:`, `blob:`, `filesystem:`, `devtools:`, `ws:`, `ftp:`)
is refused **before dispatch** with the CDP error `Koan.urlNotAllowed {scheme}` and an audit line, so a denial is an error the
agent can read and not a surprise force-detach of the tab. `MayAttachToURL` (§4.4) stays as defence in depth for
navigations the agent did not start (redirects, links). Test: T-sec-4 navigates to each scheme.

`Page.captureScreenshot` filters: `fromSurface:false` refused (trusted-only upstream, and it needs a really on-screen
window); `clip`, `captureBeyondViewport` and any emulation allowed only on agent-owned tabs (they resize the real
NSView, B§1); on the user's own tab a plain viewport capture only.

### 4.4 Frames and worlds the agent may reach (review finding 3)

`MayAttachToURL` alone is too weak and `MayAttachToRenderFrameHost` must be a **strict Koan-only allowlist** that
depends on no upstream internals (the equivalent predicates in `debugger_api.cc:320-421` are file-local; duplicating
them would break rule 4):

* a frame is reachable only if it is http(s) (or `about:blank`, or `content::kUnreachableWebDataURL` so a failed
  navigation's error page does **not** force-detach the tab, review finding 3), belongs to a non-WebUI,
  non-privileged, non-guest, non-PDF/MimeHandler, non-interstitial WebContents, **and every ancestor and the embedder
  satisfy the same**; the check covers the last-committed URL and the pending navigation's SiteInstance;
* `chrome-extension://` frames injected into ordinary pages (password managers) are **not attached**: auto-attach
  skips them and the facade never lists them;
* **isolated worlds are never given universal access.** `Page.createIsolatedWorld` has a parameter `grantUniveralAccess`
  (upstream's spelling, `Page.pdl:640`: "a powerful option") that lets the new world bypass the same-origin policy and CORS,
  so an agent that sets it can read any frame and any origin's responses from that world. The `IsolatedWorld` filter **forces it
  to false** (and drops a supplied `contentSecurityPolicy` that would weaken the page's, if the schema allows one), and
  registers the new world with the execution-context rule below. A roll that adds another privilege parameter to this method
  is caught by the parameter baseline (`scripts/agent/policy_baseline.json`, REBASE.md); T-sec-16;
* **execution contexts**: the facade drops every `Runtime.executionContextCreated` that is not the main world or a
  world created by the agent's own `Page.createIsolatedWorld`, and rejects `contextId`/`uniqueContextId` values it did
  not hand out. This closes the content-script route (an extension's isolated world could otherwise be driven with
  its `chrome.runtime` messaging, review finding 3);
* `chrome.debugger` interplay: if an extension already holds a debugger session on the tab, or later attaches, the
  tab is treated as contested: sessions and the lease stay, an audit line and a `Koan.contested` event are emitted,
  and the user can release the tab. Attaching alongside other clients is allowed (content supports several, B§8), but
  `Emulation.*` is single-owner per tab (B§4), so a second `Emulation` writer is refused.

### 4.5 Event scrubbing (review finding 1)

`Network.enable` is required (agent-browser hard-fails without it), and with it content emits
`Network.requestWillBeSent`, `requestWillBeSentExtraInfo` (carries `associatedCookies`, with values and the
HttpOnly flag, and raw request headers), `responseReceived`, `responseReceivedExtraInfo` (raw `Set-Cookie`)
(`network_handler.cc:1113-1135, 4033-4058`, nothing trust-gated). The facade therefore **rewrites** those events:
drop `associatedCookies`, `blockedCookies`, `cookiePartitionKey` and, from `Network.requestWillBeSent`, the **request body**: `request.postData`, `request.hasPostData` and `request.postDataEntries` (a login form's password travels in the body, not in a header); remove the headers `Cookie`, `Set-Cookie`,
`Set-Cookie2`, `Authorization`, `Proxy-Authorization` from request and response header maps (case-insensitive) and
from `headersText`; drop `Network.dataReceived` payload fields if present; pass only an **allowlist of Network
events** (`requestWillBeSent`, `responseReceived`, `loadingFinished`, `loadingFailed`, `requestServedFromCache`,
`webSocketCreated` URL-only, and the scrubbed ExtraInfo pair). The same scrubber applies to `Fetch.requestPaused` if
`network` is ever granted. Test: T-sec-4 asserts a login flow shows no cookie or Authorization value in any frame
received by the client.

`Input.dispatchKeyEvent {commands}` is forwarded unfiltered upstream (`input_handler.cc:790-800`);
`commands:["paste"]` pastes the OS clipboard into a focused field and `copy`/`cut` write it. The facade **strips
`commands`** from every key event. A `clipboard` capability (off) would later admit a Koan-mediated, audited
`Koan.paste {text}` that never reads the OS clipboard. T-sec-10 tests paste.

## 5. Pairing, approval, keys, capabilities

### 5.1 First contact

1. `koan pair` dials the socket. The gateway runs the peer check (§4.1) and accepts only `Koan.pair {publicKey,
   requested capabilities, claimedName}` and `Koan.authenticate`.
2. Koan shows a **native modal** (a `ui::DialogModel`, cloned from `DevToolsConnectionDialog`, C§4) in the most recent
   window and requests user attention (Dock bounce + a macOS notification if Koan is not frontmost). The **headline is
   the verified executable**, not a name the client chose: "*`koan` (signed by <team / 'unsigned'>) started by
   `claude` (signed by Anthropic …), run from `zsh`*", with full paths on expansion, and the client's own label
   shown as "calls itself *Claude Code*". Switches for the requested capabilities (§5.3). Buttons: *Allow*, *Deny*; no
   default button; Esc denies. Unanswered after 60 s: denied. The limits are on the **user**, not the program's path (a
   same-user process can run copies from any number of paths): at most one prompt open in the whole browser (**fixed**) and
   **3 prompts a minute per user by default** (a user setting, 1 to 10, §6.1; a connection from another user is refused
   before it can ask, so that is also the cap over all users), and a denial or a timeout counts as a failure against the
   peer (**5 failures in 5 minutes block it for 5 minutes by default**, a setting, 1 to 20; #190 shipped a 60 s block, and
   the settings PR makes 5 minutes the default). 0 and "unlimited" are never accepted for any of them (§6.1).
   **Accepted risk:** a process of the user's own account can use up that budget with pairing offers it signs itself,
   which delays a legitimate client's prompt (the client gets `Koan.rateLimited` and retries) and shows the user
   nuisance dialogs. It gains no access by it: each dialog names the verified program, each refusal counts against
   that program, and nothing is granted without the user. Without the budget it could ask without end, which is
   worse.
   The offer comes with a **proof of possession**: the client signs `"koan-agent-v1-pair" NUL publicKey NUL label`
   with the key it offers, and the browser checks it **before** any prompt. A key of small order (the identity point and
   the other seven torsion points, in their canonical and non-canonical encodings) is refused too, since a signature for
   it can be forged by anyone and a proof of possession does not help. The label is an **allowlist**: valid UTF-8, 1 to
   64 characters, each printable ASCII, an ASCII space, or beyond ASCII a letter (L*) or decimal digit (Nd) that is not
   a default-ignorable code point (which takes in the Hangul fillers). That leaves out controls, format characters (bidi
   overrides and isolates, zero-width, tags), line and paragraph separators, other spaces, combining marks, private use
   and symbols (the braille blank among them). No leading, trailing or repeated spaces. Look-alike letters from another
   script are not caught; the label is only shown as "calls itself" after the verified program.
3. On Allow the browser stores the **public key** (Ed25519, generated by the CLI), a random 128-bit **client id**,
   the display label, the capabilities, created/last-seen, and the **pinned identity** (§5.5). It stores no shared
   secret, so there is nothing in the browser's data that lets a process authenticate.

### 5.2 Every connection: signature, not shared secret

`Koan.authenticate {clientId}` → server nonce (32 random bytes, single use, 10 s) → client signs
`"koan-agent-v1" NUL clientId NUL nonce` with its private key → the browser verifies with the stored public key. (The
first draft also put an "audit-token identity digest" in the signed bytes. A client cannot know how the browser sees it,
so it cannot sign it; the browser checks the peer's identity from the OS instead, §5.5, and the proof binds the client
id and the connection's nonce. Implemented in `src/browser/agent/koan_agent_auth.cc`.) The failures setting (default five; §6.1) closes the connection and blocks the peer for 5 minutes. The private key lives with the client, in
`~/.koan/clients/<client-id>/key` (0600, the directory is the random id, never the label, which also closes the
`--name ../..` and silent-overwrite holes of review finding 5). A Keychain-held key with an ACL naming the signed CLI
is the later hardening (UNVERIFIED that Koan.app's ACL can list a second binary, C§5). Credentials expire after 30 days
without use. Connections are bound to a browser launch; **strict mode** (off by default) re-prompts on every
connection, optionally behind Touch ID.

**Order: verify, then ask, then pin** (PR 3 review, finding 1). A client id is not a secret (it is a directory name
the same user can read), so a prompt about a changed program or chain is shown only after the signature has verified;
the pin and the accepted-chain list are written only after the user has said yes to it. A same-user process that has
read an id but not the key gets the one error `Koan.authFailed` and no prompt. For the same reason **the nonce is
handed to any well-formed id**, and an unknown id, an expired pairing, a revoked client and a wrong signature all
fail at the proof with the same answer. An authenticated session is **not** a one-time check: every request and the
gateway's one-second tick look the client up again, so a revoked client, an expired pairing or a reduced capability
reaches live sessions at once (`OnClientRevoked` closes immediately), and a session in use keeps its pairing from
expiring.

A peer whose executable path the OS could not name, or whose uid is not the browser's, is refused before anything else
(`kUnknownPeer`). A pin with no agent in it means the chain was unknown at pairing; a connection that has an agent in
its chain is then a chain change, so an empty pin never switches the chain check off.

*What the transcript does not give.* There is no server identity in it, and the client cannot know one: a same-user
process that holds the socket path first can relay a client's nonce to the real browser. What stops it is the pin (the
relay's executable is not the paired one) and the socket's directory permissions; both are the gateway's (PR 4).

**Limits on connections (PR 4, review findings 6 and 11).** The gateway admits at most 8 connections, 4 of them not
yet authenticated (`ConnectionAdmission`); the listener refuses every uid but the browser's, so the total is also the
per-user cap. A connection has 30 s from being made to authenticate, whatever it sends, not counting the time a prompt
to the user was open (`Session::kPreAuthDeadline`); a closing connection has 2 s to flush its last reply; a queue of
replies is capped at 64 KB before authentication and 64 MB after. Malformed JSON, an oversize or pipelined frame, and
every other protocol violation before authentication count as a failure against the peer (a stalled frame, a denied
or unanswered prompt and the deadline already did). After authentication a stalled frame or a violation is not a
guessing failure. The session asks the gateway to look at the peer again before it verifies a proof and before it
applies the user's approval (`SessionDelegate::PeerUnchanged`); a "yes" given after the peer changed is answered
`Koan.peerChanged`, closes the connection and stores nothing. Per program, at most two of the four unauthenticated slots (`Koan.busy` for the third: the CLI retries with a short
back-off). A connection the gateway closes for its own reasons says why in one frame with no id (`Koan.busy`,
`Koan.peerUnresolved`; nothing is said to a foreign user or a blocked peer) and logs it, so that a lookup that is too
slow is a log line and an error, not silence. Memory: 32 MB frames and a 64 MB reply queue per connection times eight
is fine while only `Koan.status` exists; the forwarding PR adds a gateway-wide byte budget and stops reading from a
connection whose queue is high. The dialog's answer may come from any thread (it is posted back to the gateway's
sequence). Nothing starts the gateway yet (no preference, no window hook): that is the settings change.

### 5.3 Capabilities (per client, editable in settings)

| Capability | Default at pairing | Meaning |
|---|---|---|
| `space` | on | open/close tabs in its own agent Space and drive them |
| `observe` | **off** (review finding 6: it hands every open tab's title and URL to a prompt-injectable agent at pairing time); the pairing dialog offers *agent Space only* or *also list my tabs* | list the user's tabs: title, URL origin (paths only if `observe-paths`), Space, which is active |
| `read` | asks on first use | snapshot/screenshot of the user's *current* tab without driving it |
| `drive` | asks on first use, **per tab** | take over a user tab (§9) |
| `clipboard`, `network`, `cookies` | **not offered in v1** | reserved names; v1 ships without them (decisions 11, §4.5) |
| `incognito` | **does not exist in v1** (decision 5) | |
| `keep-awake` | off | `stay_awake` (display-sleep prevention) for unattended runs |

"Asks on first use" shows a bubble anchored to the URL pill of the tab concerned: *Allow once*, *Allow for this
client*, *Deny*. A missing capability returns the CDP error `Koan.notPermitted {capability}`.

### 5.4 Where the records live (review finding 2)

The client list is **not** plaintext Local State. Each client record is encrypted with `os_crypt` (Koan's
Keychain-bound "Safe Storage" key, patched in `keychain_password_mac.mm`) and the list carries a MAC under a key held
the same way, so a same-uid process that edits Local State while Koan is not running can neither add a client nor
re-enable one: a record that fails the MAC is ignored and logged, and `koan.agent_control.enabled` is not a gate on who
may connect (it is on by default; every unknown connection needs a prompt whatever its value). (The `enabled` flag itself
may stay a plain pref.) Revoke
calls `CommitPendingWrite()` and verifies the flush before returning, so a crash cannot resurrect a revoked client.
Reading the Keychain item from outside Koan.app prompts the user, which is the property we rely on.

### 5.5 Pinned identity and re-prompting (review findings 5-6)

At pairing the browser records the peer's **executable path and code-signing identity** (designated requirement or
team id; "unsigned" is recorded as such) and the **ancestor chain** with each ancestor's path and signing identity.
On every connection:

* the peer's executable path and identity must equal the pinned ones, else the connection gets a prompt as for a new
  client ("the paired program changed");
* the agent ancestor the user approved (the first non-shell, non-`koan` ancestor at pairing) must still be in the
  chain; a different chain gets a **soft re-prompt** (once per launch, per new chain), the same dialog, naming what
  changed;
* the live chain is shown in the indicator and written to every audit line.

Limits, stated: the chain comes from the OS but a process can launch `koan` from a chain it likes; pinning makes
the *change* visible, not impossible. That is why `observe` is off, `drive` is per tab, and strict mode exists.

### 5.6 Revocation

Settings > Agent control lists clients (label, verified executable, paired date, last used, capabilities, last chain).
*Revoke* deletes the record (flushed, §5.4), closes live connections, releases their leases and un-hands their tabs;
*Revoke all / Stop all agents* does it for everyone and is also a menu-bar item. Turning the feature off keeps the list
but closes everything.

## 6. Settings, indicator, audit log

### 6.1 Settings

A new "Agent control" group in `chrome://koan-settings` (a `Row` with `local_state = true`, C§4, plus a small
`koan_settings_handler.cc` addition for the client list, which is not expressible as a `Row`): the master switch
**"Agent ready", on by default** (user decision, 2026-11-07), with the one-line copy "Agent ready: lets agents you
approve open and work in background tabs. Tabs stay awake only while an agent is using them." Turning it off closes the
socket and every session and releases every lease at once, with no restart. Under it, the **limits are user settings
with defaults** (user decision, 2026-11-07):

| Setting | Pref (local state) | Default | Range |
|---|---|---|---|
| Awake agent tabs at a time | `koan.agent_control.max_leased_tabs` | 4 | 1 to 16. Above 8 the row shows "each awake tab uses its own memory and power" |
| Fewer awake tabs in Low Power Mode on battery | `koan.agent_control.low_power_halves` | on | when on, half the value above, minimum 1 (§8) |
| Approval requests per minute | `koan.agent_control.max_prompts_per_minute` | 3 | 1 to 10 |
| Failed connection attempts before a 5-minute block | `koan.agent_control.max_auth_failures` | 5 | 1 to 20 |

**Read like this, everywhere:** a limit is read through one function (`ReadLimit(pref, default, min, max)`) that returns
the stored integer if it is an integer in range, and **the default otherwise**: a missing, non-integer, corrupt, out of
range, 0 or negative value is never used, and is not rounded to the nearest edge (a hand-edited 9999 is not "16"). There
is no "unlimited" and no way to enter 0. **A change applies to new connections and new leases only.** It does not kill
live connections, and lowering the tab limit applies as leases are released (the capturer hold-off, 3 s), with no forced
drops: the next lease waits until the count is under the new limit.

**Fixed, not settings (security floors):** only **one approval prompt on screen at a time**; the pre-authentication
limits (8 connections, 4 unauthenticated, 2 per program, 30 s to authenticate, 4 KB frames); the 60 s prompt timeout; and
the open-tab budgets (8 per task, 12 per client, 16 in all), which stay as they are for now. **The open-tab budgets are a
later setting.** In code, `RateLimiter`'s prompt limit and failure threshold (`kMaxPromptsPerMinute`, `kMaxFailures` in
`koan_agent_auth.h`) become constructor parameters defaulting to those constants, so the pure-logic tests do not change;
the browser side reads the prefs and passes them in.

**The switch defaults to on in the default user-data-dir and to off in Incognito, Guest and a non-default
user-data-dir, unless the user turns it on there** (§4.1, §13, T-ready-3); the first-contact prompt is never skipped.

The rest of the group: *Ask every time*, the client list, *Stop all agents*, *Open activity log*, and *Install command line tool* (symlinks
`~/.local/bin/koan`) and *Install agent skill* (decision 10). Prefs (local state): `koan.agent_control.enabled` (default true, see §4.1), `.strict`,
`.live_always` (per client, in its record), `.audit_urls`, `.agent_history`, and the **encrypted, MACed client store**
(`.clients`, §5.4), which is not a plain dict.
The settings page itself is never attachable by an agent (T7). Enterprise: `RemoteDebuggingAllowed=false` forces the switch off and greys the row (it cannot be turned on by the user).

### 6.2 Indicator ("agent connected")

**Ordering rule (user decision, 2026-11-07).** With the gateway on by default, this indicator ships **before, or in the
same PR as, anything that makes the gateway reachable by a real build** (wiring it into `BrowserProcess`, §16). Until
then the gateway is code under `src/browser/agent/` that nothing starts.

Persistent while any client is connected, layered so one survives every layout including compact mode:

* **Window**: a dot + client name chip in the sidebar next to the Space bar; the **agent's Space** gets a glow
  (ego's "blue glow" idea, Koan colour tokens) and an "agent" icon; agent tabs in the strip carry the same mark.
* **Visible driven tab** (the user is looking at a tab the agent drives): an accent border on the content area and
  a banner pill on the page area, not the toolbar, so compact mode still shows it: "*Claude Code is controlling
  this tab* · **Take over** · Release".
* **App level**: a Koan menu-bar item "Agent control ▸ *N clients: Claude Code* · Stop all agents".
* **Events**: a toast on connect, disconnect and pause (`KoanToast`, 2 s).
* The URL pill keeps its in-use dot semantics (#163): a driven tab's pill shows the client name while that tab is
  active.

Gap: with compact mode, the sidebar hidden and the agent in another Space, only the menu-bar item and the Dock
badge show (decision 6; a minimal strip is shown while a driven tab is the visible one).

### 6.3 Audit log

JSON lines in `<user-data-dir>/AgentControl/audit/YYYY-MM-DD.jsonl` (0600): time, client id and label, connection id,
the **peer chain**, tab (id + origin), method, a summary, outcome (`ok|denied|paused|error`), duration. Summaries:
navigate: origin plus a **hash of the path** (full URLs only if the user turns on `audit-urls`, because magic links and
reset tokens live in paths and queries); click/type: ref or coordinates and text **length**, never the text;
evaluate/callFunctionOn: SHA-256 and length of the source; screenshots: size. Pairing, approval, denial, revocation,
take-over, pause, release, `userNeeded` and every denied attempt are events. Never logged: page content, typed text,
cookie values, headers.

* **Size-capped**: 20 MB per day, 100 MB in total, 30 days; the oldest files go first, and when the cap is hit a
  `log-truncated` event is written. Writes happen on a worker thread, never the UI thread.
* **Integrity is best-effort**: records are hash-chained with a key held through `os_crypt`, so deletion of the middle
  or edits are detectable; a same-uid process can still delete the files, and the settings page says the log is
  evidence for the user rather than a security boundary.
* Viewer: the settings page and `koan log --follow`.

## 7. Where agents work: the agent Space, a container

**Decision (user and maintainer, 2026-11-10; replaces "an agent Space is a tab group in the user's profile").** An
agent Space is a **container**: a `StoragePartition` of its own (cookies, local storage, IndexedDB, cache, service
workers and renderer processes) **inside the user's profile**, shown as a Space in the user's own sidebar. The agent's
tabs share none of the user's logins or site data, and the user still watches them where they work: switch to the
Space, open one of its tabs in split view next to their own, pop the Space out into its own window, or drag a tab out
to take it over (§9). After comparing six options (tab groups in the profile, DevTools off-the-record contexts, a
profile per agent, containers, a separate browser instance, an "Agents" profile with containers), containers won on
daily use, on Zen parity (Zen gives each workspace a container) and on cost. The upstream audit behind every line here
is `.agent/notes/upstream/containers.md` (cited as E§n).

The earlier draft of this section (a tab group sharing the user's cookie jar, with copy that said so plainly,
decision 15) is withdrawn: decision 15's warning now applies only to the "Your tab" mode below.

### 7.0 Spawn modes

The default mode is a setting (Settings > Agent control). The pairing and task prompts show the mode, and the user can
change it or refuse the task.

| Mode | What the agent gets | Data after the task | Status |
|---|---|---|---|
| **Throwaway** (default) | a new in-memory container for this task | cleared when the task ends (`StoragePartition::ClearData`, E§1); gone at quit at the latest | core PR |
| **Saved** | a named container on disk that keeps its cookies between tasks ("GitHub bot") | kept; deleted from Settings. Another agent or task gets it **only if the user names it** in the prompt | core PR |
| **Your tab** | a lease on one of the user's own tabs, limited to that tab's site (§8, §9). Not a container: this is the only mode where the agent acts with the user's logins, and its prompt says so | n/a | another PR (lease + gateway) |
| **Strict** | a separate hidden profile: nothing of the user's profile is shared (settings, extensions, history service, permissions), the strongest wall | as Throwaway or Saved, in that profile | seam designed now (`AgentMode::kStrict` returns "not supported"), built last |

There is **no free run in the user's own jar**: an agent never gets the default partition except through a per-tab,
per-site lease the user approved.

### 7.1 How a container works (E§1-E§6)

* **Identity.** A container is a `ContainerId {kind, id, persistent}`. The kind (agent or user) and the id are in the
  partition **domain**: `koanagent<id>` or `koanuser<id>`, with `id` = `t` or `s` (throwaway or saved) then 26 random
  letters, because partition domains are `[a-z]*` (`storage_partition_config.h`). The partition name is empty, so a
  saved container lives at `<profile>/Storage/ext/koanagent<id>/def`. One domain per container is what makes
  `BrowserContext::AsyncObliterateStoragePartition(domain)` delete exactly one container (E§1). Throwaway containers are
  `in_memory`. **Because the kind is in the domain, any code that holds a process, a frame or a partition can tell an
  agent container** (`koan::containers::IsAgentProcess/Frame/Contents`, `//koan/browser/containers:partition`, which
  depends on `//base` and `//content/public/browser` only), so the blocks in `components/` and `extensions/` need no
  service. A user's renaming of a saved container changes only its name in prefs (`koan.containers.saved`), never the
  id or the path.
* **Creating a tab.** `WebContents::Create` with `SiteInstance::CreateForFixedStoragePartition(profile, url, config)`
  (the Android "separate storage partition" tab is the precedent, E§8; `CreateForGuest` is for `<webview>` and is not
  used). The fixed bit lives on the BrowsingInstance and content copies the partition forward at every switch point
  (`NavigationRequest::GetUrlInfo`, `SiteInstanceImpl::DeriveSiteInfo`, `RenderFrameHostManager::ConvertToSiteInstance`,
  E§2), so same-tab links, the omnibox, reload, history, cross-site swaps, subframes and about:blank stay in the
  container with no patch. `ChromeContentBrowserClient::GetStoragePartitionConfigForSite` is never consulted while the
  partition is fixed (E§3).
* **Where content loses the container, and the hooks (§14):**

  | Path | Upstream behaviour | Hook |
  |---|---|---|
  | `window.open` with an opener; drag between windows, detach, Move to new window, split view of open tabs; Duplicate (`WebContents::Clone`) | same BrowsingInstance or the same WebContents: kept | none |
  | `target=_blank` (implicit noopener), `rel=noopener`, `window.open(…, "noopener")` | `WebContentsImpl::CreateNewWindow` makes a default `SiteInstance` (only guests keep their partition) | C1 |
  | Cmd/middle-click, context menu "Open link in new tab/window/split view" | `CreateTargetContents` in `browser_navigator.cc` makes a default one even with a `source_site_instance` | C2 |
  | Tab discard (Mac uses the replace-the-WebContents path; `kWebContentsDiscard` is off outside Android) | `TabLifecycleUnit::FinishDiscard` builds the replacement in the default partition | C3 |
  | Prerender (omnibox, speculation rules) | `PrerenderHost` builds its page in the default partition and swaps it into the tab | C4: no prerender in containers |
  | Saved containers on disk | the web-app `GarbageCollectStoragePartitionsCommand` deletes everything under `Storage/ext/` that is not an extension's or an isolated app's **data-loss risk** | C5: saved containers are loaded before it lists what to keep |
  | Session restore, reopen closed tab or window | `sessions::` stores no partition; `CreateRestoredTab` makes a default `SiteInstance` | no new patch, see below |
  | Glance (Koan) | `KoanGlanceController` made its own `WebContents` in the default partition | Koan code: the Glance opens in the source tab's container |

* **Session restore and Recently Closed.** Every container tab carries `koan.container` in its per-tab session data
  (`koan_session_data.h`; written when the tab enters a window, so a crash cannot restore it without the marker). An
  agent container's tab is **never restored**: a `TabRestoreHook` replaces its pages with one `about:blank` before
  `NavigationController::Restore` runs, and the blank tab is closed once it is in its window (kept, blank, if it is the
  window's only tab). So no agent page ever loads in the user's jar after a restart, a crash or Reopen Closed Tab, and a
  Saved container's tabs do not come back either (its cookies do, for the next task that names it). User containers
  (#111) will restore into their container with one more hook in `CreateRestoredTab` (`create_params.site_instance`);
  until then they are blanked the same way. Brave encodes the partition into the URL so that an older build shows a
  blank page instead of loading in the default partition (`containers.md`, Brave); Koan keeps the marker in its session data, which needs
  Koan's restore hook to run, and the hook fails closed (an unknown or malformed marker blanks the tab too).
* **Spaces.** An agent container's Space is a normal Koan Space (a tab group) in the user's last-active window,
  created with the container's first tab **without switching to it**, named by the agent ("Claude Code · task"), with
  the agent icon. Agent ownership is the **partition**, not Space membership: a tab the user opens in an agent Space
  (Cmd+T) is the user's own, in their jar; a container tab the user drags into one of their Spaces is still in the
  agent's jar and keeps its blocks (taking a tab over gives the user the controls, not their logins; to use their own
  login they open the URL in their own Space). Zen gives each workspace a default container that its new tabs use (`containers.md`, Zen);
  that is #111 and it reuses this core (`ContainerKind::kUser`, the same partition helpers, restore and hooks) with the
  blocks off.
* **End of a task.** `EndAgentContainer` closes the container's tabs in every window (the Space goes with its last tab)
  and, for a Throwaway container, clears all of its data. In-memory partitions are never unloaded before the profile
  shuts down (`browser_context.h`, E§1), so each throwaway task leaves an empty partition and its network context
  behind until quit: a known cost (T-cont-6 measures it). A partition is never reused for another task, because
  `ClearData` does not reset a network context's HTTP auth cache or sockets.
* **What the user sees.** The Space's header and its tabs say "Agent · not your logins". Downloads from an agent tab go
  to `~/Downloads/Koan Agent/` and show in the normal downloads UI (one `DownloadManager` per profile; each item
  remembers its partition, E§7).

### 7.2 What an agent container blocks

Only **agent** containers block; user containers (#111) behave like the default partition. Each block is a few lines in
the upstream decision function calling `koan::containers::IsAgent*` (§14 B-rows):

| Surface | Block | Upstream decision point |
|---|---|---|
| Every permission (location, camera, microphone, notifications, clipboard, MIDI, sensors, storage access, ...) | denied, never prompted | `PermissionUtil::IsPermissionBlockedInPartition` (the one check `PermissionManager` makes for both requests and status, `permission_manager.cc`), true for agent partitions. Upstream already blocks *undelegated* permissions in any non-default partition; Brave overrides the same function the other way, to share (E§5). WebUSB, WebHID and Web Serial are already refused in non-default partitions upstream |
| Choosers and pickers | file and directory pickers, File System Access pickers, Bluetooth chooser and scanning prompt, screen share (`getDisplayMedia`) cancelled (a `Koan.userNeeded` event for them is the gateway's, §9.4) instead of a native sheet | `BrowserWebContentsDelegate::RunFileChooser`, `EnumerateDirectory`; `ChromeFileSystemAccessPermissionContext::CanShowFilePicker`; `ChromeBluetoothDelegateImplClient::RunBluetoothChooser`, `ShowBluetoothScanningPrompt`; `DisplayMediaAccessHandler::HandleRequest` |
| Extensions | no content scripts or extension APIs in agent processes; no webRequest or declarativeNetRequest on agent traffic; no `chrome.debugger` attach; extension APIs cannot address an agent tab by id | `RendererStartupHelper::OnRenderProcessHostCreated` / `OnRenderProcessLaunched` (precedent: `kInitialWebUIWithoutExtensions`), `WebRequestAPI::MaybeProxyURLLoaderFactory`, `ExtensionMayAttachToWebContents` (debugger), `ExtensionTabUtil::GetTabById`. `chrome.cookies` reads only the default jar |
| Passwords | no fill, no save, no Credential Management API | `ChromePasswordManagerClient::IsFillingEnabled` (saving requires it) |
| Autofill | no addresses, cards or autocomplete | `ChromeAutofillClient::IsAutofillEnabled`, `IsAutocompleteEnabled` |
| History and omnibox | agent visits are not added to History, so not to history-based suggestions; `@tabs` and Switch to tab do not offer agent tabs | `HistoryTabHelper::DidFinishNavigation` (before `ShouldUpdateHistory()`); `TabMatcherDesktop::GetOpenWebContents` |
| Downloads | to `~/Downloads/Koan Agent/`, never a Save As prompt, Safe Browsing as usual | `DownloadTargetDeterminer::DoGenerateTargetPath` (where the prompt and the target directory are decided) |
| Sync | agent tabs are not synced as open tabs (history and passwords are already out) | `TabContentsSyncedTabDelegate::ShouldSync` |
| Passkeys / WebAuthn / Touch ID | refused before any UI | `ChromeContentBrowserClient::IsSecurityLevelAcceptableForWebAuthn` (the existing Koan patch) |

**Every per-profile service is decided.** A container shares the profile's `KeyedService`s (that is what makes it
cheap), so a new upstream service could reach agent tabs silently. A unit test lists every factory registered by
`EnsureBrowserContextKeyedServiceFactoriesBuilt()` and requires each to be on `koan_agent_service_policy.cc`'s list as
*agent-safe* (it never sees a tab's content or partition, or it is keyed by partition) or *blocked* (and names the
block above); a factory in neither fails the test, so a Chromium roll that adds a service fails until someone decides
it (§14, T-cont-9).

**Accepted gap (documented in the agent Space's copy and in SECURITY.md):** the user's own extensions with the "tabs"
permission still see agent tabs' URLs and titles in `chrome.tabs.query` and tab events. Chromium has no per-tab hide
flag and the tab model is read in dozens of places; it runs *agent → user's extensions*, never *user's data → agent*.

### 7.3 Hand-over of a user tab

Unchanged in substance: *Hand to agent ▸ [client]* on the tab context menu and the palette (visible only while a
client with `drive` is connected). A user tab is in the user's jar, so it can only be **driven in place** under a lease
(the "Your tab" mode, §8); there is no "move into the agent Space" any more (a WebContents cannot change partition, and
moving it would carry the user's logins into the agent's Space). A container tab dragged out of the agent Space by the
user is *taken over* (§9): the agent loses it, it keeps the agent jar and its blocks.

### 7.4 Kept out of the user's other surfaces

| Surface | Mechanism | Patch |
|---|---|---|
| **History, omnibox, `@tabs`** | §7.2 | B-rows |
| **Session restore, Recently Closed, reopen a window** | the `koan.container` marker and the restore hook blank agent tabs (§7.1) | none |
| **Session files on disk** | they can hold agent-visited URLs until next rewritten, though restore blanks the pages; a tab-level early return in `SessionServiceBase::BuildCommandsForTab` / `UpdateTabNavigation` removes them | **conditional, §14 #6**, decided by T-sync-1 |
| **Recently Closed list** | agent tabs appear there but reopen blank and close; a one-line early return in `CreateHistoricalTab` keeps them out of the list | **conditional, §14 #5**, decided by T-sync-1 |
| **Saved tab groups and tab-group sync** | `koan::SavesTabGroups(context)` already keeps every Space out of saved groups when Spaces are on; agent control requires Spaces (`kNoSpaces`) | none |
| **The user's cookie jar, storage, cache, logins** | the container | C-rows |

## 8. Per-tab keep-alive (the lease)

`AgentTabLease : content::WebContentsUserData`, created when a client attaches to a tab, released on detach, tab close,
revoke, or when idle (below). **Two lifetimes**: everything that is *cheap* is held for the **whole attachment**, from
attach to detach, tab close or revoke, and is never dropped by the hold-off; only the **capturer** (the
visible-compositor cost) follows the hold-off.

| Held for the whole attachment | Follows the hold-off (below) |
|---|---|
| the `SetIsPinnedTab` composition (no freeze, no discard) | the visible `IncrementCapturerCount` capturer |
| the `SetGlicActuationState` vote (renderer priority) | |
| the `ForbidExternalPopupMenus()` `ScopedClosureRunner` | |
| `SetAudioMuted`; the app-suspension blocker while a command is in flight | |

Dropping the first two with the hold-off would let an agent tab be frozen or discarded while the model is thinking, and
the next command would hit a frozen renderer. **No upstream patch is needed for it** (review finding 12): freezing, discarding and
process priority are all driven by public Performance Manager setters Koan already uses.

| Part | Call | Why (audit) |
|---|---|---|
| Render + scheduler | `contents->IncrementCapturerCount(gfx::Size(), /*stay_hidden=*/false, /*stay_awake=*/false, /*is_activity=*/false)`, keep the `ScopedClosureRunner` | RWH stays shown, own compositor on a detached tab, rAF/CSS animations run, Blink treats the page as visible (no 1 s / 1-per-minute timer throttling), explicit freeze cleared (A§7, B§4). Visible rather than hidden-but-painting (decision 7): it unfreezes, and compositor animations are verified only on that path. Cost: `document.visibilityState == "visible"`, so autoplay and ad-viewability logic treat the agent tab as seen |
| Freezing and discarding | `PageLiveStateDecorator::SetIsPinnedTab(contents, IsPinnedForUser(tab) \|\| IsAgentLeased(tab))` inside Koan's existing `UpdateFreezingExemption` (`koan_pinned_tabs.cc:268`) | Koan's own `kPinnedTab` freezing reason and upstream's discard `kPinnedTab` (`discard_eligibility_policy.cc:412`) both read this bit; the only other consumer outside PM is `restartability_monitor.cc`. **Replaces** the freezer patch, the discard patch and the new `CannotFreezeReason` of the first draft. The lease must **compose** with the user-pin value there, never write the bit itself |
| Process priority | `PageLiveStateDecorator::SetGlicActuationState(contents, GlicActuationState::kActuatingOnBackgroundTab)` (public, `page_live_state_decorator.h:111`; reset to `kNone` on release) | Casts the `kUserVisible` PM vote (A§5) so the renderer is not put in the App-Nap-style `kBestEffort` class on macOS; Chromium's Glic helper merely forwards to this setter, so there is **no `//chrome/browser/glic` dependency and no content patch**. The voter is gated by `kGlicActuationPriorityVoter` (on by default; recheck on each roll) |
| Native popups | `contents->ForbidExternalPopupMenus()`, **Mac only**: it exists only under `#if BUILDFLAG(IS_MAC) && BUILDFLAG(USE_EXTERNAL_POPUP_MENU)` (`web_contents.h:940`) and returns a `[[nodiscard]] base::ScopedClosureRunner` | The lease **stores that runner for the whole attachment** (nested calls are allowed; dropping it re-enables native popups). Chrome's own actor takes it behind `kGlicActorInternalPopups` (`actor_task.cc:988-992`); Koan takes it unconditionally. Without it a click on a `<select>` opens a native NSMenu over the user's work (§9.4). The same switch applies to a **user tab driven in place**: while it is driven its `<select>`s render in-page, and the runner is released on hand-back |
| Audible/muted | `SetAudioMuted(true)` on agent tabs unless the task opts in | ego mutes agent tabs (§19.4) |
| `stay_awake` | off | a display-sleep assertion only, never a keep-alive; opt-in `keep-awake` for unattended runs. Separately Koan holds an **app-suspension blocker** (idle-sleep prevention, not display sleep) while a task has a command in flight (T-power-1). System sleep and lid-close are **not** prevented |

Pinned-bit side effects to check (T-keep-5, 5b, 6, 8, plus T-keep-12): Koan's other code that reads `IsPinnedTab` must not
treat an agent tab as a user-pinned one (it reads `koan::IsPinnedForUser`, not the PM bit; the PM bit is for freezing
and discard only).

**A lighter lease for a MacBook Air** (review finding 14). A visible capturer keeps the tab's compositor running at
vsync, which on battery is not free, so the lease is **not held while the agent is thinking**:

| Setting | Default | Why |
|---|---|---|
| Hold-off of the **capturer** after the last command on a tab | **3 s** (release then re-acquire transparently on the next command); the pin bit and the priority vote stay | an LLM turn is 3-20 s; holding the compositor through it burns power for nothing |
| Held for the whole of | a `wait`/program in flight, an open screencast, `Koan.keepAlive {tab, seconds}` (≤ 10 min), an active `record`/`stream` | so waiting for an animation or a timer-driven page works |
| Concurrent leased tabs | **4 by default, a user setting from 1 to 16** (`koan.agent_control.max_leased_tabs`, §6.1; the next lease waits once the limit is reached; commands queue rather than fail); **half of it on battery with Low Power Mode on**, minimum 1, unless the user turned that off (below) | a MacBook Air has 8-16 GB and 4-8 cores; above 8 the settings row warns that each awake tab uses its own memory and power |
| Open tabs per task / per client / total | **8 / 12 / 16**, fixed for now (a later setting) | ego's page budget is 8 (§19) |
| `live: always` (opt-in, per client) | off | hold the lease for as long as a client is attached, for pages that must run between commands |

Consequence, stated plainly: between commands a default-mode agent tab is a normal hidden tab as far as rendering goes
(timers throttled to 1 s, no rAF), **but it is not frozen, not discarded and keeps its renderer at foreground priority**
for the whole attachment, so the next command answers at once. "Animations and timers keep running" holds while the agent acts, observes or waits, and always under
`live: always`; the first frame after re-acquire must be fast (T-keep-10: ≤ 100 ms), and a **never-shown** background
tab (`did_first_set_visible_` false) must produce its first paint under the lease (T-keep-11). A page that needs to
progress while the agent is thinking must say so (`keepAlive`).

**What Agent ready costs when no agent is attached** (user decision, 2026-11-07: the switch is on by default). Effectively
zero: the gateway is a listening socket on the browser's existing IO thread. No tab holds a lease, no capturer runs,
no pin bit or priority vote is set, nothing is unthrottled, and normal tabs throttle, freeze and discard exactly as in
Chrome. A lease exists only for a tab an agent is attached to, within the caps above. With the switch **off** there is no
listener and nothing scheduled, any lease is released and every session is closed, at once (T-ready-2).

**On battery with Low Power Mode on** (user concern, 2026-11-07). While the Mac is on battery power *and* Low Power
Mode is on:

* **concurrent leased tabs drop to half the setting** (4 to 2 by default; minimum 1; the *Fewer awake tabs in Low Power
  Mode* switch, `low_power_halves`, on by default, can turn the halving off). The others queue as with any cap: a
  command waits for a release rather than failing. Like any lowered limit it applies **as leases are released** (the
  capturer hold-off, 3 s), with no forced drop and nothing in flight cut; a toast and a
  `Koan.powerStateChanged {onBattery, lowPowerMode, maxLeases}` event tell the client;
* **`live: always` and `Koan.keepAlive {seconds}` with `seconds > 60` are refused** with a clear error,
  `Koan.lowPower {reason: "battery-low-power-mode", maxLeases: <the halved limit>, maxKeepAliveSeconds: 60}`, so an agent can tell the
  user why and retry shorter or plug in; a `live: always` lease already held is downgraded to the default at the moment
  the state is entered (same event), not dropped; `keepAlive` of 60 s or less, `wait` and in-flight commands are
  unchanged. These two refusals apply whenever the Mac is on battery with Low Power Mode on, whatever `low_power_halves` says (they are the battery protection, not a limit on tabs);
* leaving the state (power connected, or Low Power Mode off) restores the defaults at once; nothing is persisted.

The inputs are `base::PowerMonitor` for battery state and the macOS Low Power Mode setting
(`-[NSProcessInfo isLowPowerModeEnabled]` and `NSProcessInfoPowerStateDidChangeNotification`; UNVERIFIED which
Chromium wrapper exists in 158, check on the Mac before writing a new observer). The tab limit comes from the pref through `ReadLimit` (§6.1); the keep-alive limit is a constant (`kMaxKeepAliveLowPower`,
60 s); the lease cap takes both as inputs, so T-power-2, T-power-3 and T-limits-* can drive them with a fake power source
and fake prefs.

Only agent tabs hold a lease; no global flag changes (A§9 lists the ones never to flip). Expected result for a
leased non-active agent tab in an unfocused or covered window: rAF yes, CSS animations yes, timers unthrottled,
`captureScreenshot` yes (plus its own transient capturer), process foreground, not frozen, not discarded. Minimized
window, other macOS Space and Cmd+H: by code a detached tab owns its compositor (A§2, A§3) but this is UNVERIFIED until
T-keep-1..4. The `kNotifyCompositorOf*OnMacOs` features must stay off, or an agent tab in the *active slot* of a hidden
window needs the window-level video-capture lock, which capture takes automatically (A§3).

## 9. Take-over and hand-back

### 9.1 Tab states (per client, per tab)

`unshared` (not in the facade) → `listed` (title/URL; capability `observe`) → `readable` (snapshot/screenshot, no
input; capability `read`, one approval) → `driven` (full; capability `drive`, **per-tab approval**) ⇄ `paused`.
Agent-Space tabs start `driven`.

### 9.2 Flows

* **Agent asks to take over the user's tab midway.** `Koan.takeOver {tab}` → a bubble on that tab's URL pill:
  "*Claude Code wants to control this tab* · Allow · Deny" (60 s timeout, default deny). Allow → lease + banner +
  indicator; the tab stays in place (or is moved if the user picked *Move into agent Space*).
* **User hands a tab over.** Context menu or palette (§7). Same end state.
* **Agent releases.** `Koan.release {tab}` or `koan hand-back`: lease released, banner removed; a moved tab returns to
  its origin Space and index. **User releases**: banner *Release*, or revoking/Stop all.
* **Pause on user input.** The signal is `WebContentsObserver::DidGetUserInteraction` for the **whole frame tree**
  (mouse-down, key-down, gesture-scroll-begin, touch-start; `WebContentsImpl::HandleUserInteractionForInputEvent`
  covers the primary frame tree including cross-origin child widgets, `web_contents_impl.cc:11307`), filtered by the
  `WebInputEvent::kFromDebugger` bit that every CDP-injected event carries (B§7; Chrome's own
  `extension_actor_tracker.cc:129-138` makes the same distinction). A `RenderWidgetHost::InputEventObserver` is added
  only for wheel (not a user-interaction type), per widget host and re-registered on `RenderFrameHostChanged`, because
  an observer on the root widget alone would miss events routed to a cross-origin iframe, which is exactly the
  payment-widget case. **Switching to the tab is not input**; typing or clicking in the page is. Tab-strip and omnibox
  activity never reaches the widget, so it neither pauses nor needs to. IME text arrives after key events, which are
  seen. **Pause is detect-after-delivery**: the user's key already reached the page and a batch step may be in flight;
  the gateway checks before every step and drains the queue, and the docs say so (T-take-1 includes an OOPIF).
* **Paused**: the gateway rejects mutating methods on that tab with `Koan.paused` (`Input.*`, `Page.navigate/reload`,
  `Runtime.evaluate/callFunctionOn` since arbitrary JS can mutate, `DOM`/`Emulation` writes) and drains queued input;
  reads still work (`captureScreenshot`, `Accessibility`, `DOM` reads, `Page.getFrameTree`). Event
  `Koan.paused {tab, reason:"user-input"}` and a toast. Other agent tabs are unaffected. agent-browser's liveness
  probe (`Runtime.evaluate "1"`, D§3) fails on a paused tab and reports a wedged tab; acceptable.
* **Hand back to the agent**: the banner's *Hand back* button (sticky pause by default so a stray click does not
  release the agent into a half-finished form); optional setting *auto-resume after N seconds of no input* (off).
  The agent cannot resume itself (`Koan.resume` only asks the user via the banner).
* During an atomic multi-step action the gateway may use `WebContents::IgnoreInputEvents(audit_cb)` returning true
  for `kFromDebugger`, false (and pause) otherwise; this also keeps the user's keystrokes out of the page for the
  few milliseconds an injected sequence runs (B§7; note the `ShouldIgnoreInputEvents` effect is Aura-only, so
  Mac relies on the callback).

**Built (take-over PR):** `Koan.takeOver {targetId?}` (capability `drive`; the user's active tab by default) shows a browser-modal
prompt in the tab's window, 60 s, default deny, and grants the tab `driven` **for its site only** (`net::SchemefulSite` at approval).
The agent's own `Page.navigate` off that site is `Koan.offSite`; any committed navigation off it (a link, a redirect, the user) makes
the tab `readable` (`Koan.siteLeft`), where DOM answers and events go through `RedactSecretInputs()` (#196 N1) and `Network.enable`
is refused (N2). The user's own input (`DidGetUserInteraction` without `kFromDebugger`, plus wheel per widget) pauses any registry
tab (`Koan.paused`); commands still waiting for the lease are answered `Koan.paused` instead of sent (`CdpFacade::MayDispatch()`).
The indicator pill shows *Hand back* for a paused tab and *Take back* for a user tab; `Koan.resume` only marks the pill
"asks to continue". `Koan.release` gives a user tab back. The prompt is browser-modal rather than the URL-pill bubble of the
design: a bubble anchored to Koan's URL pill is a UI follow-up.

### 9.3 Seeing the user's tab (`read`)

"Current tab" is C++ knowledge, not CDP: the first window in `ForEachCurrentBrowserWindowInterfaceOrderedByActivation`
and its `tabActive` tab (B§5, B§9). `Koan.listTabs` marks it. `Koan.read` returns a snapshot (§10) and a plain
viewport screenshot of a *readable* tab, which is attached as an observer client with the same untrusted-client
rules and **no keep-alive** (the user is looking at it, or it is not the agent's to keep awake). `watch` streams
tab/URL/title changes, active-tab changes, and (if `read`) navigations. Taking a screenshot of the user's tab while
they are typing is never blocked by pause, and never uses the `clip` path (B§1).

### 9.4 Page-initiated focus and modal surfaces (review finding 9)

The agent controls CDP calls; the page controls what it does next. Everything below pauses the tab and raises a
`Koan.userNeeded {tab, reason}` event, shown as an approval prompt on the tab's URL pill and in the indicator:

| Surface | Behaviour |
|---|---|
| `window.open`, `target=_blank`, popups, a link click that opens a tab | opened **in the background in the agent Space**, owned by the same lease, never activated, no window raised (hook #2, §14); T-focus-3 |
| `<select>` dropdowns, date and colour pickers | `ForbidExternalPopupMenus()` so no native NSMenu opens over the user's work (T-focus-4); the page renders them in-process |
| File chooser | `Page.setInterceptFileChooserDialog` forced on; a `userNeeded {file-chooser}` prompt; a Koan-mediated, user-approved path upload later (T-focus-5) |
| HTTP auth, client-certificate picker, external-protocol launch, print, `beforeunload` | `userNeeded` approval prompts, never a silent native sheet; `beforeunload` is handled explicitly (T-focus-6) |
| Permission prompts (location, camera, microphone, notifications, USB, …) | the tab pauses and the normal permission bubble is anchored in the agent Space; the agent can never grant (`Browser.grantPermissions` does not exist); event reason keys as in §19.4 |
| `alert`/`confirm`/`prompt` | stay with the agent (`Page.handleJavaScriptDialog`) |
| A downloaded file | the normal download UI and Safe Browsing; `Page.setDownloadBehavior` denied |

T-focus-7: nothing in this table changes the active tab, the focused window or the frontmost app.

## 10. Snapshots built in the engine (ego's idea) and the tool surface

ego(lite) claims snapshots built *inside* the engine rather than by injected scripts, reaching cross-origin
iframes, shadow DOM and embedded widgets, plus a multi-step script per call. Options for Koan, in order of cost:

| Option | What | Pros | Cons |
|---|---|---|---|
| **S1 (v1, free): CDP from the client** | agent-browser's own `snapshot` = `Accessibility.getFullAXTree` + `DOM`, flat sessions per frame, refs by `backendDOMNodeId` (D§4) | Works day one through the facade; nothing to build; the same tool agents already know | Needs the `Accessibility` domain to be reachable by an untrusted client (T-cdp-3) and the facade to pass child (OOPIF) target attach events; one CDP round trip per frame |
| **S2: `Koan.snapshot` / `Koan.act` in the browser process** | `WebContents::RequestAXTreeSnapshot(callback, ui::AXMode, max_nodes, timeout, ...)` (public, `web_contents.h:648`) builds a tree across out-of-process iframes and shadow DOM with no script injected; refs `@N` are bound to (frame, AX node id); `Koan.act {ref, click|fill|select|scroll|hover}` resolves bounds in the browser, scrolls into view, dispatches input | Closest to ego; one call returns a compact, token-cheap tree; no page-visible injection; no CDP domain exposure; the engine can drop sensitive nodes (password fields' values) | New code to maintain; we must not enable full platform accessibility in the tab (the snapshot AX mode must be a one-off, UNVERIFIED); Chrome's own actor does the equivalent with annotated page content (`chrome/browser/actor/`, `ToolRequest`s, not audited here: audit before building) |
| S3: scripted batches | `koan run script.js` runs agent JS **in the CLI** (embedded QuickJS), exposing `tab.snapshot()/click()/fill()/wait()/capture()` that call S1/S2 over the socket | Fewer LLM round trips, no code in the page | A JS engine in the CLI; sandboxing it |

Recommendation (updated after the review): **S1 ships in phase 2d** (agent-browser's own client-side AX snapshot through
the facade), because it works on day one, and it is *not* where Koan is faster: through S1 the call pattern is stock CDP
plus a hop (review finding 16). **S2 (`Koan.snapshot`/`act`/`run`, diffs, event waits) is phase 3**, built with
`RequestAXTreeSnapshot(kAll)`, and the benchmark compares S1 and S2 on the same pages, which is how the S1-vs-S2 call
gets made with data. The `Koan.snapshot`/`act` schema is designed now so S2 slots in under the same capabilities and audit
rules. Password-field values are never returned by any snapshot.

## 11. agent-browser integration (maintainer requirement 1, 2026-11-06)

Facts (D, read 2026-11-07, UNVERIFIED where marked): `agent-browser` is a native Rust CLI plus a daemon speaking raw
CDP over its own WebSocket client (no Playwright). `--cdp <port|ws://|wss://|http(s)://>`; a `ws://` URL is **used
verbatim with no `/json/version` request**, query string kept; `--cdp-headers` / `AGENT_BROWSER_CDP_HEADERS` put
headers on the handshake; **no unix-socket dialling and no `ws+unix`**; `DevToolsActivePort` is read only by
`--auto-connect` and only for Chrome/Canary/Chromium/Brave default dirs. It attaches to **every** page target at
connect (`Target.setDiscoverTargets`, `getTargets`, flat `attachToTarget`), creates a tab only if none exist, and
hard-fails the connection if `Page.enable`, `Runtime.enable`, `Network.enable`, `getTargets` or `attachToTarget`
fail. Attached browsers are never closed (`Browser.close` only for browsers it launched). Plugins: a local
executable speaking `agent-browser.plugin.v1` on stdin/stdout can act as a `browser.provider` and return `cdpUrl`.

### 11.1 What works out of the box, what needs an adapter

| Piece | Status |
|---|---|
| A raw-CDP WebSocket endpoint with token in URL or `Authorization` header | works as is (`--cdp ws://…`, `AGENT_BROWSER_CDP_HEADERS`) |
| Our endpoint | **needs an adapter**: it is a unix socket with framed JSON and a signature handshake; agent-browser cannot dial it |
| Adapter | **`koan-relay`** (a C++ helper in the app, started by `koan agent-browser <args>` or the plugin form `agent-browser --provider koan`). **Lifetime (review finding 8):** agent-browser's CLI is thin: the first command spawns a *daemon* that holds the CDP connection (idle timeout 1 h) and receives `AGENT_BROWSER_CDP` only at spawn, so a relay that "exits with the CLI" would leave the daemon holding a dead URL. The relay is therefore **session-scoped**: it is keyed by the agent-browser session name, survives the CLI that started it, and exits after its own idle timeout (15 min without a frame, shorter than the daemon's 1 h so a stale daemon fails fast and is restarted) or on `koan relay stop`/revoke. On the loopback side it serves one path `/cdp/<128-bit secret>`, refuses any `Origin`, requires `Host == 127.0.0.1:<port>`, allows one concurrent connection, and translates WebSocket to the frames of §4.1; it does the signature handshake itself. **Secret hygiene:** env is readable by the same uid (`ps eww`), so the secret is never claimed hidden from a process list; it is **never printed into the agent's context** (`koan cdp-url` does not print it: `koan relay env --session S` writes `AGENT_BROWSER_CDP=…` into a 0600 file that `koan agent-browser` reads and hands to the daemon at spawn); it is valid for the first connection for 30 s, and later reconnects by the same daemon are allowed only while the relay session lives (T-ab-4 decides single-use vs session-lifetime). All relay traffic is audited as the relay's client with an "unknown consumer" note |
| Browser-level `Target.*`, `Browser.getVersion`, `Page/Runtime/Network.enable` | provided by the facade (§4.2); denials of everything else return CDP errors; `bringToFront`/`activateTarget` no-op |
| Tabs | the facade lists only the agent Space (+ handed tabs), so "attach to every page" is safe; with none, it creates `about:blank` through `Koan.openTab` in the agent Space; `window new` (`Target.createBrowserContext`) returns an error that says use `tab new` |
| `snapshot`, `click`, `fill`, `type`, `press`, `scroll`, `eval`, `find`, `get`, `tab`, `frame`, `dialog`, `pdf` | work through the allowed methods (T-ab-1). The facade also handles `Target.setAutoAttach {waitForDebuggerOnStart, flatten}` with `Runtime.runIfWaitingForDebugger`, and tolerates denied `Browser.*`/`Security.*` as non-fatal (D§5); `Security.setIgnoreCertificateErrors` stays denied |
| `screenshot`, `record`, `stream` (`Page.captureScreenshot`, `startScreencast`) | **need the keep-alive lease** (A§7, B§3): a hidden tab produces no frames; `screencast` has no capturer of its own |
| `wait` for animations / `requestAnimationFrame` pages / timers | need the lease (rAF and timer throttling in a background tab) |
| `cookies`, `storage`, `state save/load`, `auth` vault | **denied by default** (T5): `Network.getAllCookies`/`setCookies`, `DOMStorage` fail with `Koan.notPermitted`. Capability `cookies` (later) could allow site-scoped use. This is a deliberate non-feature; the docs say so |
| `network route`, `--headers`, `--allowed-domains`, proxy auth | `Fetch` and `Network.setExtraHTTPHeaders`: need capability `network`, which v1 does not offer, so they fail by design; `agent-browser network requests` loses cookie/Authorization data by design (§4.5). Pin the agent-browser version in T-ab-* (the project moves fast) |
| `--auto-connect` | does not apply (reads Chrome's `DevToolsActivePort`); document `--cdp`/`--provider koan` |
| `--profile` with `--cdp` | do not combine (UNVERIFIED effect, D§2) |

### 11.2 Is the `koan` CLI still needed? (decision 4)

Yes, smaller. agent-browser does not do pairing, status, the Space/task model, take-over/hand-back, the audit log or
unix sockets. **`koan` does lifecycle and the relay; agent-browser does page actions.** v1 `koan`: `pair`, `status`,
`revoke`, `log`, `tabs`, `open`, `task`, `take-over`, `hand-back`, `watch`, `screenshot` (so an agent without
agent-browser can still look), `skill`, and `agent-browser`/`relay`. No `click`/`type`/`eval` of its own in phase 2:
those are agent-browser's, and phase 3's `Koan.run` replaces them rather than duplicating a worse version. agent-browser
compatibility is a test target (§15, T-ab-*) rather than a dependency: we do not bundle or modify it.

## 12. The `koan` CLI and the agent skill

**Built with the Chromium tree's own toolchain** (decision 12) so a friend who builds Koan (#113) installs nothing
extra. **C++, not Rust-via-GN.** Why: the CLI and the gateway share the frame codec, message structs, the method
policy and the signature handshake, and one C++ library under `//koan/browser/agent/common` compiled into both removes a
class of drift bugs and gives one test target; BoringSSL (Ed25519) and `//base` are already in the tree; a Rust target
needs vendored crates for the same things and a second formatting, lint and test story. Two GN executables, copied into
`Koan.app/Contents/Resources/bin/` and signed with the app:

* **`koan`**: small (`//base`, BoringSSL, the shared codec; no `//net`) so that start-up is a few milliseconds.
* **`koan-relay`**: the loopback WebSocket side for agent-browser (`net::HttpServer` + WebSocket framing). Kept out of
  `koan` so the common path stays small, and out of the browser so the browser parses no HTTP.

`koan` is installed to `~/.local/bin/koan` by a settings button, and being inside the app its signature is what the
pairing dialog verifies.

```
koan pair [--label L]            ask the browser for approval; generates the key, stores ~/.koan/clients/<id>/
koan status                      connected? clients, tasks, driven and paused tabs
koan tabs [--space agent|all]    list tabs the client may see (id, label, title, URL, state)
koan open <url> [--task T]       open a background tab in the agent Space; prints the tab id
koan screenshot <tab> [-o f]     plain viewport capture
koan task start|end [name]       name/end the agent Space
koan watch                       stream events (tab/URL/title, pause, userNeeded, release) as JSON lines
koan take-over [tab]             ask to drive the user's current tab (the user approves)
koan hand-back [tab]             release the tab; a moved tab goes home
koan agent-browser <args…>       relay + exec agent-browser against Koan (session-scoped relay)
koan relay env|stop --session S  write the relay env to a 0600 file / stop it (never prints the secret)
koan shell                       (phase 3) one long-lived connection, JSON lines on stdin/stdout
koan skill                       print the usage guide generated from the installed version
koan log [--follow]              the audit log
koan revoke [id|--all]
```

**Skill file** (ego's integration model: shell commands, no SDK, no remote-debugging setup). The installed entry skill
is a few lines ("run `koan skill` and follow it"); `koan skill` prints the guide from the installed version so it is
never stale, written from Koan's own command set. Koan writes `~/.claude/skills/koan-browser/SKILL.md` (and
`~/.agents/skills/koan-browser/`) **only when the user clicks "Install agent skill" in settings** (decision 10), never
silently. The guide tells the agent: run `koan status`; if not paired, `koan pair` and wait for the user to approve;
open tabs with `koan open` (they land in the agent's Space, never the user's); use `koan agent-browser` for page work;
use `koan watch`; if `userNeeded` or `Koan.paused` appears, stop and tell the user; never ask the user for keys, never
read `~/.koan`. `koan` is **one command prefix a user can allow-list**, so the agent works in normal permission modes,
not "bypass permissions". Some agents sandbox shell commands; `koan` needs the home directory and the socket, and the
guide documents the allowances.

## 13. Incognito

**No incognito in v1** (decision 5): the capability does not exist. Every method and every target is closed to it: the
registry resolves each target through `Profile::FromBrowserContext(...)` and rejects `IsOffTheRecord()`, guest and system
profiles, because `Target.getTargets` lists incognito pages and nothing upstream filters them (B§9, C§3b). Agent tabs
are created only in the original profile; there is no `Koan.openTab {incognito}`; a take-over or hand-over of an
incognito tab is refused with a specific error; an incognito window never gets an agent Space (Koan has no Spaces in
incognito windows, `ShowsSpaceHeader`). A later capability would be per tab, with its own prompt and a stronger banner.

**No listener in Incognito, Guest or a non-default `--user-data-dir`** (Agent ready, §4.1): a browser started in
Incognito or Guest mode, or against a user-data-dir other than the default, does not start the gateway, unless the user
turned the switch on for that user-data-dir. The gateway is a browser-process singleton, so this is decided by how the
process was launched, not per window: a normal browser with an incognito window open does have the listener (it is the
same process), and that window's tabs are still unreachable to it, as above.

## 14. Hook patches (for docs/REBASE.md)

Three groups. The gateway and lease rows (#1-#6) are unchanged except where marked; the container rows (C) and the
agent blocks (B) come with the containers (§7, 2026-11-10). Every hook is a few lines that call into `//koan`, except
C1 (in `content/`, which cannot depend on `//koan`), whose logic is generic and Koan-free.

| # | File | Change | Needed for | Status |
|---|---|---|---|---|
| 1 | `chrome/browser/devtools/remote_debugging_server.cc` | the default of `g_enable_default_user_data_dir_check_for_chromium_branding_for_testing` becomes `true` | refuse `--remote-debugging-port`/`-pipe` on the default profile (§3.2) | **its own PR, #186** |
| 2 | `chrome/browser/ui/browser_web_contents_delegate/browser_web_contents_delegate.cc` (the existing Koan patch; `AddNewContents`) | a page-opened tab of an agent container opens **in the background, in the same Space, never activated, never raising a window** (§9.4). *Which partition* it is in is C1/C2's job | page-initiated focus (review finding 9) | phase 2c |
| 3 | `chrome/browser/history/history_tab_helper.cc` | now **B7** | | moved |
| 4 | `chrome/browser/BUILD.gn` / `chrome/browser/ui/BUILD.gn` | deps on `//koan/browser/agent` | build only | phase 2b |
| 5 | **conditional** `chrome/browser/ui/browser_tab_strip_model_delegate.cc` (`CreateHistoricalTab`, `CreateHistoricalGroup`) | early return for agent-container tabs | keep them out of the Recently Closed *list* (they already reopen blank) | decided by T-sync-1 |
| 6 | **conditional** `chrome/browser/sessions/session_service_base.cc` (`BuildCommandsForTab`, `UpdateTabNavigation`; the existing Koan patch) | early return for agent-container tabs | agent-visited URLs out of the session files | decided by T-sync-1 |
| **C1** | `content/browser/web_contents/web_contents_impl.cc` (`CreateNewWindow`, the `opener_suppressed` branch) | a noopener window of a fixed-partition opener gets `SiteInstance::CreateForFixedStoragePartition(…, partition_config)` instead of a default `SiteInstance`, as a guest's keeps its partition | `target=_blank` and noopener popups stay in the container | core PR |
| **C2** | `chrome/browser/ui/navigator/browser_navigator.cc` (`CreateTargetContents`, no opener) | `koan::containers::SiteInstanceForNewTab(profile, params.source_site_instance, url)` when it returns one | Cmd/middle-click and context-menu "Open link in new tab/window/split" stay | core PR |
| **C3** | `chrome/browser/resource_coordinator/tab_lifecycle_unit.cc` (`FinishDiscard`) | the replacement `WebContents` gets `koan::containers::SiteInstanceForReplacement(old_contents)` | a discarded container tab reloads in its container | core PR |
| **C4** | `browser_web_contents_delegate.cc` (`IsPrerender2Supported`, the existing patch) | `kPreloadingDisabled` for container tabs | prerender builds its page in the default partition | core PR |
| **C5** | `chrome/browser/web_applications/commands/garbage_collect_storage_partitions_command.cc` (`DoGarbageCollection`) | `koan::containers::LoadSavedContainers(profile_)` before the allowlist is used (loaded partitions are never collected) | saved containers are not deleted from disk | core PR |
| **B1** | `components/permissions/permission_util.cc` (`IsPermissionBlockedInPartition`) | true for agent partitions | every permission denied | blocks PR |
| **B2** | `browser_web_contents_delegate.cc` (`RunFileChooser`, `EnumerateDirectory`); `chrome/browser/file_system_access/chrome_file_system_access_permission_context.cc` (`CanShowFilePicker`); `chrome/browser/bluetooth/chrome_bluetooth_delegate_impl_client.cc` (`RunBluetoothChooser`, `ShowBluetoothScanningPrompt`); `chrome/browser/media/webrtc/display_media_access_handler.cc` (`HandleRequest`) | cancel for agent tabs | pickers, choosers, screen share | blocks PR |
| **B3** | `extensions/browser/renderer_startup_helper.cc` (`OnRenderProcessHostCreated`, `OnRenderProcessLaunched`), `extensions/browser/api/web_request/web_request_api.cc` (`MaybeProxyURLLoaderFactory`), `chrome/browser/extensions/api/debugger/debugger_api.cc` (`ExtensionMayAttachToWebContents`), `chrome/browser/extensions/extension_tab_util.cc` (`GetTabById`) | skip / refuse for agent processes and tabs | extensions | blocks PR |
| **B4** | `chrome/browser/password_manager/chrome_password_manager_client.cc` (`IsFillingEnabled`) | false for agent tabs | passwords | blocks PR |
| **B5** | `chrome/browser/ui/autofill/chrome_autofill_client.cc` (`IsAutofillEnabled`, `IsAutocompleteEnabled`) | false | autofill | blocks PR |
| **B6** | `chrome/browser/download/download_target_determiner.cc` (`DoGenerateTargetPath`) | agent folder, no prompt | downloads | blocks PR |
| **B7** | `chrome/browser/history/history_tab_helper.cc` (`DidFinishNavigation`, before `ShouldUpdateHistory()`); `chrome/browser/autocomplete/tab_matcher_desktop.cc` (`GetOpenWebContents`) | skip agent tabs | history, omnibox, `@tabs`, Switch to tab (decision 18) | blocks PR |
| **B8** | `chrome/browser/ui/sync/tab_contents_synced_tab_delegate.cc` (`ShouldSync`) | false | open-tab sync | blocks PR |
| **B9** | `chrome/browser/chrome_content_browser_client.cc` (`IsSecurityLevelAcceptableForWebAuthn`, the existing patch) | false for agent frames | passkeys, WebAuthn, Touch ID | blocks PR |

No patch: the container core itself (`//koan/browser/containers`), session restore and Recently Closed (the
`TabRestoreHook` of `koan_session_data.h`), the Glance, and the upgrade test.

**Upgrade guards** (rule 3 of "Upgrade-safe code"):

* **Every per-profile `KeyedService` is decided** (§7.2): `koan_agent_service_policy_unittest` lists the factories from
  `BrowserContextDependencyManager` and fails on one that is neither agent-safe nor blocked. CI cannot build it (it
  needs `//chrome/browser`), so `scripts/ci/check_agent_services.py` checks the same list statically against the
  pinned Chromium's factory names (the PR that adds it says how it gets them without a checkout).
* Behaviour the hooks assume, each named in its REBASE row: the fixed-partition bit is copied forward at the three
  switch points of §7.1 (T-cont-1, T-cont-2); `CreateNewWindow` still has one `opener_suppressed` branch (C1, T-cont-3);
  `FinishDiscard` still builds a new `WebContents` (C3, T-cont-5; if Koan turns on `kWebContentsDiscard` the hook goes);
  `PermissionManager` still asks `IsPermissionBlockedInPartition` for both requests and status (T-block-1); upstream
  still refuses WebUSB/HID/Serial in non-default partitions (T-block-2).

**Dropped from the first draft** (all replaced by `PageLiveStateDecorator::SetIsPinnedTab` and
`SetGlicActuationState`, §8): the freezer hook, the discard-eligibility reason, the new
`CannotFreezeReason::kAgentControlled`, the `//chrome/browser/glic` dependency, the content patch for
`OnBoostForBackgroundExecution`, and the `ActivateContents` skip (the facade's no-op covers CDP, and Koan code never
activates agent tabs).

No-patch pieces (all in `//koan`): the settings row and handler, the tab context menu "Hand to agent" (inside the
existing `AddKoanTabMenuItems`). Everything else uses public `content::`, `performance_manager::` and `chrome::` APIs:
`DevToolsAgentHost`, `DevToolsAgentHostClient`, `WebContents::IncrementCapturerCount`,
`WebContents::ForbidExternalPopupMenus`, `WebContentsObserver::DidGetUserInteraction`, `WebContents::IgnoreInputEvents`,
`PageLiveStateDecorator`, `TabStripModel`, `SiteInstance::CreateForFixedStoragePartition`,
`StoragePartition::ClearData`, `BrowserContext::AsyncObliterateStoragePartition`, `net::UnixDomainServerSocket`.

REBASE rows must reference **symbols**, not lines (rule 3): `SetIsPinnedTab`, `SetGlicActuationState`,
`kGlicActuationPriorityVoter`, `IsDomainAvailableToUntrustedClient` (`devtools_session.h:245-266`,
`chrome_devtools_session.cc:42-48`), `DevToolsAgentHostClient`'s flag defaults, and the protocol-coverage test of §4.3
(a roll that adds a CDP method fails it). Behaviours to name in the rows: freezing/discard/priority stay exempt for a
leased tab (T-keep-5, 6, 8), a `<select>` click opens no native menu (T-focus-4).

**Container Mac tests** (part of the §15 plan, listed here with their hooks):

| ID | Test | Pass condition |
|---|---|---|
| T-cont-1 | sign in to a site in the user's tab; open it in an agent container | the agent tab is signed out; `document.cookie`, localStorage, IndexedDB empty; a cookie set in the agent tab is not in the user's tab |
| T-cont-2 | in an agent tab: same-tab link, cross-site link, typed URL in the omnibox, reload, back/forward, a cross-origin iframe | all stay in the container (the cookie set in step 1 is still visible; the user's is not) |
| T-cont-3 | `target=_blank`, `rel=noopener`, `window.open(…)` with and without `noopener`, Cmd-click, middle-click, context menu "Open link in new tab", "Open link in split view", Glance (Alt-click) | each new tab or Glance is in the container and opens in the background (§9.4) |
| T-cont-4 | drag an agent tab to another window, Move to new window, Duplicate, split view with a user tab | partition kept; the duplicate is a container tab |
| T-cont-5 | discard an agent tab (`chrome://discards`) and reload it | still in the container |
| T-cont-6 | 20 throwaway tasks in a row | each ends with its data cleared (`chrome://serviceworker-internals`, cookies); memory after the 20th recorded |
| T-cont-7 | a Saved container: sign in, end the task, restart Koan, start a task naming it | still signed in; a task that does not name it is not; deleting it in Settings removes `Storage/ext/koanagent<id>` |
| T-cont-8 | quit with agent tabs open (normal quit, then `kill -9`), restart; Reopen Closed Tab on an agent tab; reopen a closed window that had one | no agent page loads anywhere; the tabs come back blank and close |
| T-cont-9 | `koan_agent_service_policy_unittest` and `check_agent_services.py` | pass; a fake new factory fails both |
| T-cont-10 | uninstall an extension so that `kShouldGarbageCollectStoragePartitions` is set, restart | the saved container's data is still there |
| T-block-1 | in an agent tab: geolocation, notifications, camera, microphone, clipboard read, MIDI, storage access, persistent storage | all denied with no prompt; `Permissions.query` says denied |
| T-block-2 | file input, `showOpenFilePicker`, `showDirectoryPicker`, `navigator.bluetooth.requestDevice`, `getDisplayMedia`, WebUSB/HID/Serial | no native UI; each fails; `Koan.userNeeded` raised for the first five |
| T-block-3 | with an ad blocker, a content-script extension and a webRequest logger installed: load pages in an agent tab | no content script runs, nothing blocked or logged, `chrome.debugger.attach` and `chrome.scripting.executeScript` on the agent tab id fail; `chrome.tabs.query` still lists it (the accepted gap) |
| T-block-4 | a login form with a saved password; an address form | no fill, no save prompt, no autofill dropdown |
| T-block-5 | visit pages in an agent tab | not in History, not suggested by the omnibox, not listed by `@tabs`, not in another device's open tabs |
| T-block-6 | a download in an agent tab | lands in `~/Downloads/Koan Agent/` with no prompt; shows in the downloads UI |
| T-block-7 | a passkey sign-in in an agent tab | refused at once, no Touch ID sheet |

## 15. Mac tests (phases 2-3; nothing runs on Linux except the pure-logic unit tests)

All through the Mac loop (`scripts/mac/`, real keys, real pointer, `fv.mjs`, `screencapture -l`), with a **test**
profile, a fake agent client written in Node (a minimal CDP/`Koan.*` client) and, where noted, real `agent-browser`.
Port to a `scripts/mac/agent.sh` driver and `scripts/e2e/agent_*.mjs` specs.

| ID | Test | Pass condition |
|---|---|---|
| **Keep-alive** | | |
| T-keep-1 | rAF page (counter), CSS animation page, 100 ms `setInterval` page in a **non-active tab of an unfocused window**, 60 s | frame/timer rates within 10 % of foreground; the same page without a lease is throttled (control) |
| T-keep-2 | same with the window **covered** by another app's full-screen window, then **minimized**, then on **another Space**, then app **hidden (Cmd+H)** | rates hold; record any case that stalls and which of A§10's UNVERIFIED items it is |
| T-keep-3 | `Page.captureScreenshot` of an animating page in each state of T-keep-2; pixels differ between two shots 500 ms apart | animation advances |
| T-keep-4 | `Page.startScreencast` on the same, with and without the lease | frames only with the lease |
| T-keep-5 | freezing: enable infinite-tabs freezing, 7 tabs; agent tab leased (`keepAlive`) and idle for 3 min; then released and idle again | not frozen while leased (via the `SetIsPinnedTab` composition); a normal tab again after release; others frozen as before (compare `chrome://discards`); the user's own pinned tabs unaffected |
| T-keep-5b | **default mode**: attach, issue a command, let the 3 s hold-off pass, then leave the tab idle for 3 minutes with infinite-tabs freezing and Memory Saver (1 min for the test) on; then send a command | the tab is not frozen and not discarded during the idle time (`chrome://discards`, process state), the renderer is not in the background priority class, the command answers at once, and the time to the first frame after re-acquire is within T-keep-10's bound |
| T-keep-6 | discard: Memory Saver at 1 min for the test; agent tab attached / detached | not discarded while leased |
| T-keep-7 | other tabs unchanged: a normal background tab still throttles and freezes | `power.mjs`-style numbers unchanged |
| T-keep-8 | priority: renderer process `taskpolicy`/`ps` state with and without the Glic vote | not background |
| T-keep-9 | hold-off: no command for 3 s, then a command | lease released after 3 s (tab hidden again, `Koan.status` shows it), re-acquired transparently by the next command; held through a `wait`, an open screencast and `keepAlive` |
| T-keep-10 | time to first frame after a re-acquire, on a hidden tab, 20 runs | the 20 samples' median ≤ 100 ms and p95 ≤ 250 ms (else lengthen the hold-off); a latency check on this test, not a benchmark verdict |
| T-keep-11 | a **new, never-shown** background tab (`did_first_set_visible_` false) under the lease | produces its first paint and answers a screenshot |
| T-keep-12 | lease and pin composition: a user-pinned or Essential tab taken over and released; an agent tab the user pins meanwhile | the PM pinned bit is `user || lease` at every step; Koan's own `IsPinnedForUser` never reads the lease; nothing is left exempt after release |
| T-keep-13 | the popup-menu runner: click a `<select>` in an agent tab while attached, also after a hold-off; then hand a user tab back | no native NSMenu while attached; native popups return after hand-back |
| T-power-1 | battery cost of 4 leased tabs vs 4 normal background tabs for 10 min (`powermetrics` where permitted, otherwise CPU time), and the app-suspension blocker while a task runs | recorded; the blocker is released when the task ends; Mac idle sleep resumes |
| T-power-2 | on battery with Low Power Mode on (a fake power source in the unit test; the real setting on the Mac): attach to 4 tabs, then enter the state with 4 leases held, then leave it | no lease is dropped on entry; as each is released (hold-off) at most 2 hold a capturer, and the next commands wait (none fails); the `Koan.powerStateChanged` event and a toast; leaving restores 4 and the queued commands run. With only one of battery / Low Power Mode, the limit stays 4. With `low_power_halves` off the limit stays 4 in the state; with the tab setting at 1 the halved limit is 1 (not 0) |
| T-power-3 | in that state: `Koan.keepAlive {seconds: 61}` and `Koan.keepAlive {seconds: 60}`, `live: always` set on a client and requested on a tab, and a `live: always` lease already held when the state is entered | 61 and `live: always` return `Koan.lowPower {reason: "battery-low-power-mode", maxLeases: 2, maxKeepAliveSeconds: 60}` (with the default setting; they are refused with `low_power_halves` off too); 60 is accepted; the lease held on entry is downgraded to the default with the event, not dropped; leaving the state allows both again |
| **Focus and creation** | | |
| T-focus-1 | `Koan.openTab` while the user types in another tab | active tab, focus, caret and typed text unchanged (frontmost pid check per `koan-input-grant-and-front-pid`) |
| T-focus-2 | `Page.bringToFront`, `Target.activateTarget` from the client | no-op success; nothing raised |
| T-focus-3 | a page in an agent tab calls `window.open`, has `target=_blank` links and a popup; the user types in another tab | the new tabs open in the background in the agent Space; the active tab, focus and frontmost app are unchanged |
| T-focus-4 | click a `<select>` in an agent tab | no native menu appears over the user's work |
| T-focus-5 | trigger a file chooser | no native sheet; a `userNeeded {file-chooser}` event and prompt; the tab pauses |
| T-focus-6 | HTTP auth, client-certificate request, an external-protocol link, `window.print()`, `beforeunload` | each becomes a `userNeeded` approval prompt, none a silent native sheet |
| T-focus-7 | all of the above while the user is typing in a different window | the frontmost pid and the user's text are unchanged |
| T-win-1 | Koan on another display / Space, `Koan.openTab` | no Space switch, no window raise |
| T-win-2 | no window open (windowless Mac app) then `Koan.openTab` | window appears inactive, does not take focus |
| **Security** | | |
| T-sec-1 | password manager: saved login, agent clicks the username field, reads the password field via `Runtime.evaluate` | value not readable (else disable autofill in agent tabs) |
| T-sec-2 | socket perms: dir 0700, socket 0600; a second uid and an unpaired same-uid client | other uid refused; unpaired gets only `Koan.pair` |
| T-sec-3 | web page attack: page JS `fetch('http://127.0.0.1:*')`, DNS-rebinding page against the relay, `Origin`-bearing WebSocket | no connection |
| T-sec-4 | denied methods: call every row of the §4.3 deny list and `Storage.*`, `Network.{getAllCookies,getCookies,setCookie,deleteCookies,clearBrowserCache,loadNetworkResource}`, `Browser.close`, `Extensions.loadUnpacked`, `SystemInfo.getInfo`, `Page.navigate` to `file:///etc/hosts`, `javascript:`, `view-source:`, `data:` and `chrome://settings` (each must be `Koan.urlNotAllowed`, not a detach), `DOM.setFileInputFiles`, `DOM.getFileInfo`; navigation to `chrome-extension://`; **and log in to a test site (HttpOnly Set-Cookie, an Authorization header, a POST body carrying a password) with `Network.enable` on** | each denied call returns the expected error; no cookie value, `associatedCookies`, `Set-Cookie`, `Authorization`, `Proxy-Authorization`, `postData` or `postDataEntries` (the POSTed password) appears in any frame the client receives; audit entries present |
| T-sec-5 | incognito: open an incognito window with a tab | absent from `Koan.listTabs` and `Target.getTargets`; `attachToTarget` by guessed id refused; `Koan.takeOver` refused |
| T-sec-6 | pairing: a spoofed `--label` (including `../..`), deny, timeout, rate limit, a changed executable path or signature, a new ancestor chain, revoke while connected, revoke then crash, revoke all, feature off while connected | the dialog headline is the verified executable and the label is "calls itself"; a changed pinned identity re-prompts; a revoked client does not return after a crash (flushed); revoke closes sockets and releases leases |
| T-sec-7 | policy: `RemoteDebuggingAllowed=false`, `DeveloperToolsAvailability=2` | gateway stops / attach refused |
| T-sec-8 | `--remote-debugging-port=9222` on the default profile (Koan not running) | ignored with the stderr message, and `curl 127.0.0.1:9222` fails; with a non-default `--user-data-dir` still works |
| T-sec-9 | audit log: content of a typed password and of `Runtime.evaluate` source | neither appears |
| T-sec-10 | clipboard: copy a secret in another app, focus an input in an agent tab, send `Input.dispatchKeyEvent {commands:["paste"]}` and ⌘V | nothing pasted (`commands` stripped); the injected ⌘V does not paste either |
| T-sec-11 | extension reach: a password-manager extension injects a `chrome-extension://` iframe into the page; an extension content script runs in an isolated world | the iframe is not listed or attached; no isolated-world execution context is delivered; `Runtime.evaluate {contextId}` of one is rejected; a failed navigation to an error page does not detach the tab |
| T-ready-1 | the switch on (default), no client paired, the browser used normally for 10 min with several background tabs | no `AgentTabLease`, no capturer, no pin or priority vote from agent control, background tabs throttle, freeze and discard as without the feature; CPU and wakeups equal to the build with the switch off within noise (`powermetrics` where permitted) |
| T-ready-2 | with a paired client attached and a leased tab, turn the switch off in settings (no restart) | the socket file is gone, the listener and its descriptors are closed, every session is closed with a reason, the lease, pin bit, priority vote and popup runner are released, the indicator clears; turning it on again listens again, and the paired clients are still there |
| T-ready-3 | start the browser with `--incognito`, with `--guest`, and with a non-default `--user-data-dir`; then set the switch on explicitly in the last | no socket and no listener in the first two and the third until it is turned on there; with it on there, a listener in that user-data-dir's own `AgentControl/` directory only |
| T-ready-4 | a fresh install with the default switch: a `koan` / raw-socket client connects and sends `Koan.pair`; and a client that sends anything else | the native approval prompt appears and names the verified executable; without an answer (60 s) or on Deny nothing is stored; every other method is refused `Koan.notAuthenticated`; no paired key is accepted without a prompt |
| T-ready-5 | enterprise policy `RemoteDebuggingAllowed=false` on a browser whose switch is on | no listener; the row is greyed and off and cannot be turned on; removing the policy restores the user's setting |
| T-limits-1 | the four limit prefs (unit test, fake prefs): in range, the edges (1 and the maximum), one past each edge, 0, negative, a huge value, a string, a double, a list, a bool, missing | in range and the edges are used as stored; everything else gives the **default**, not an edge; `low_power_halves` that is not a bool gives the default (on); 0 is never returned |
| T-limits-2 | a lease cap of 4 with 4 held; the tab pref is lowered to 2 | the 4 held leases stay; the next lease request queues (its command waits) until the count is under 2, then runs; raising it back lets the queued one run; nothing is dropped at any point |
| T-limits-3 | the prompt and failure prefs changed while connections are live and others are made afterwards | the live connections and the live `RateLimiter` entries keep working; a new gateway session / limiter uses the new numbers: the Nth+1 prompt a minute is `Koan.rateLimited`, the Nth failure blocks for 5 minutes; with the defaults the existing #190 tests pass unchanged |
| T-limits-4 | the floors: with every limit set to its minimum and then its maximum, open two prompts at once, open a 9th connection, an unauthenticated connection idle for 31 s, 17 tabs | still one prompt on screen; the pre-auth limits and the tab budgets hold exactly as without settings |
| T-limits-5 | the settings row for the tab limit at 9 and at 16 (screenshot test); the row at 8 | the note "each awake tab uses its own memory and power" shows at 9 and 16 and not at 8; the rows reject 0 and values out of range |
| T-sec-12 | forged client store: while Koan is not running, add a client record to Local State (and clear `enabled`, or leave it at its default) | the record fails its MAC and is ignored (logged); a connection still gets the pairing prompt; a client that is not in the MACed store is never accepted without the prompt, with the switch on |
| T-sec-13 | pre-auth abuse: a 1 GB length prefix, 100 half-open connections, malformed JSON, a second `Koan.authenticate`, slow-loris | closed within the limits, browser memory flat, UI thread unaffected |
| T-sec-14 | `chrome.debugger` interplay: an extension attaches to an agent tab, then detaches | `Koan.contested` event; a second `Emulation` writer refused; the lease survives |
| T-sec-15 | the protocol-coverage unit test (Linux-runnable): every CDP method in the 158 protocol JSON is classified | fails on any unclassified method |
| T-sec-16 | `Page.createIsolatedWorld {grantUniveralAccess:true}` on a page with a cross-origin iframe, then `Runtime.evaluate` in that world to `fetch` and read the iframe's origin | the world has no universal access (the call succeeds with it forced off; reading the cross-origin frame or response fails) |
| **Take-over** | | |
| T-take-1 | agent drives the user's active tab (approved); user types/clicks in the page | `Koan.paused`; mutating calls rejected; reads allowed; banner shows; no further injected input |
| T-take-1b | as T-take-1 with the input in a **cross-origin iframe** (payment widget) and as a wheel scroll | pauses for both; the in-flight batch step is the documented detect-after-delivery case |
| T-take-2 | CDP-injected input does **not** pause (`kFromDebugger` filter); `Input.insertText` too | agent keeps running |
| T-take-3 | hand back via banner; agent resumes; `Koan.resume` alone does nothing | resumes only after the user's click |
| T-take-4 | move into agent Space and take back; pinned/Essentials refused; user closes the tab mid-run | origin Space/index restored; refusals; `detachedFromTarget` |
| T-take-5 | user switches Space / tab while the agent works | agent unaffected; the agent's tabs keep rendering |
| T-take-6 | two clients, two tasks | two Spaces, isolated registries; revoke one |
| **Compatibility** | | |
| T-ab-1 | `agent-browser` (pinned version) through the relay: `open`, `snapshot -i`, `click @e1`, `fill`, `eval`, `tab new`, `screenshot`, `pdf`, `record` (needs ffmpeg), `wait` on an animation | each works on a **background** tab; failures listed with the denied method |
| T-ab-2 | `cookies`, `state save`, `network route` | fail with `Koan.notPermitted`, no crash, no hang |
| T-ab-3 | agent-browser against a profile with 30 user tabs | it attaches to the agent Space tabs only |
| T-ab-4 | agent-browser CLI exits; its daemon keeps its connection; the daemon later reconnects; the relay's idle timeout passes | the relay is still up while the daemon is; reconnect works or fails fast; a stale daemon fails and restarts cleanly (decides open item O4) |
| T-ab-5 | the relay's secret never appears in the agent's tool output, `koan` stdout or the audit log; a second simultaneous connection is refused | pass |
| T-hist-1 | an agent tab visits pages | not in History or the omnibox suggestions; `@tabs` does not list agent-Space tabs; with the setting on they appear |
| T-sync-1 | an agent Space with tabs; quit and restart; Recently Closed; reopen the closed window; (if sync is on) saved tab groups; grep the profile's `Sessions/` files for the agent-visited URLs | after restart no agent page loads (blank tabs are swept before the window settles); Recently Closed lists nothing (or, without §14 #5, entries that reopen blank and are swept); nothing synced; the grep result decides §14 #6 |
| T-sync-2 | agent control with the Spaces flag off | `Koan.openTab` returns `Koan.unavailable {spaces-disabled}`; no agent group is created, saved or synced |
| T-cdp-1 | Puppeteer `connect({browserWSEndpoint})` through the relay | attaches; limitations documented |
| T-cdp-3 | `Accessibility.getFullAXTree` from an untrusted client, incl. OOPIF and shadow DOM (the domain itself is not trust-gated: review correction) | iframes and shadow roots present, or the facade proxies child targets |
| T-cdp-4 | `Page.captureScreenshot` with `clip`/`captureBeyondViewport` on the user's visible tab | refused or no relayout flicker |
| **UI** | | |
| T-ui-1 | indicator in each layout: normal, compact (sidebar hidden), fullscreen, multiple windows | at least one persistent mark is visible; menu-bar item correct |
| T-ui-2 | settings: toggle, client list, revoke, stop all, install CLI/skill | pref, socket, sessions follow the toggle immediately |
| T-ui-3 | unit tests (Linux-runnable pure logic): the frame codec (fuzzed, incl. oversize and truncated), the method policy table and its protocol-coverage test, frame/world vetting, event scrubbing, signature handshake and nonce reuse, pinned-identity comparison, client-record MAC, rate limiter, socket-path fallback, audit redaction and caps | pass |

Every run also fails on a Koan log `:FATAL:`, a crash report, or a leaked lease (counted at exit).

## 16. Phasing

Implementation is **small PRs, each reviewed and then taken by the Mac loop**, in the maintainer's order (2026-11-07):
the gateway and auth first, then keep-alive, then the relay and agent-browser, then the in-browser snapshot/act/run. Mac
time goes to Zen-parity candidates first, so each PR keeps its build small and batchable (one Mac request per PR, with
its tests listed).

* **2a (done, PR #186):** the default-profile remote-debugging fix.
* **2b, gateway and auth:** the benchmark harness skeleton with the **baseline runs** (stock Chrome + agent-browser,
  scripted and LLM mode, §20.7) as a separate docs/scripts PR; then, in small PRs: the shared frame codec and the policy
  tables with the protocol-coverage test (pure logic, Linux-runnable); the gateway, framing, peer identity, keypair
  pairing and the encrypted client store; the registry and frame/world vetting, event scrubbing and the audit log; the
  indicator, then settings and the wiring of the gateway into `BrowserProcess` (**the indicator comes first or in the
  same PR: with Agent ready on by default, nothing may make the gateway reachable by a real build before the "agent
  connected" state is visible**; PR #190 and #191 are code that nothing starts). Mac: T-sec-*, T-ready-*.
* **2c, containers** (2026-11-10, in this order, each its own PR against `develop`):
  1. this design change (§7, §14, §16; `.agent/notes/upstream/containers.md`);
  2. the container core: `ContainerId` and its partition; the containers implement agent control's
     `koan::agent::AgentTabOpener` (`koan_agent_tab_hooks.h`, #203) and register it with `SetAgentTabOpener()`, one
     container per client and task; `koan_agent_containers.h` keeps the lifecycle (`EndAgentTask`) and the saved
     containers for Settings; a tab a page opens from an agent tab (#203's `OnOpenedFromAgentTab`) inherits the
     partition (C1, C2) and joins the container's Space; the approval dialog's copy says so; the Space,
     Throwaway vs Saved, links and popups (C1, C2), discard (C3), prerender (C4), the GC allowlist (C5), the Glance,
     and session restore (no patch). Mac: T-cont-1..8, 10;
  3. the agent blocks (B1-B9). Mac: T-block-*;
  4. the upgrade test for per-profile services, and its CI check. Mac: T-cont-9;
  5. Strict mode (a hidden profile) behind the same API, last.
  If containers slip past 24 Nov, Agent ready ships **off by default** until they land.
* **2c, keep-alive:** the lease (the whole-attachment part and the capturer hold-off), `Koan.openTab` on top of
  `OpenAgentTab`, the "Your tab" lease, `koan` (C++ in GN), the skill file, page-initiated focus hook (§14 #2) and
  prompts. Mac: T-keep-*, T-focus-*, T-sync-*, T-hist-*.
* **2d, relay and agent-browser:** `koan-relay`, the session-scoped relay and `koan agent-browser`, then take-over/hand-back,
  pause and `userNeeded`. Mac: T-ab-*, T-take-*.
  **2a-2d ship security and background capability. They do not ship a speed advantage**: through agent-browser the
  call pattern is the same CDP as stock plus a facade hop and a relay (review finding 16). No copy, README or release note
  says "faster" until the phase-3 numbers exist.
* **3, the speed phase, last:** `Koan.snapshot` / `act` / `run` in the browser (with an `eval` step, §20.2), incremental
  diffs, the event-driven wait signals, text-first results; re-run the benchmark, including **the ego(lite) run
  (mandatory for the headline)**. The headline is the upper 95 % bound of the paired wall-time ratio Koan / ego being `< 1.0` on the suite (§20.7), not parity.
* **Later:** `network`/`cookies`/`clipboard` capabilities, Keychain-bound client keys, Touch ID strict mode, O2 window mode.

Phase 2 may start now, after #186 (maintainer, 2026-11-07); it is no longer gated on the 24 Nov parity merges, but Zen-parity
candidates keep the Mac first.

## 17. Decisions

### 17.1 Decided by the maintainer (2026-11-07, on the #185 review)

| # | Decision | Where it landed |
|---|---|---|
| 1 | Ship the Chrome 136 default-profile fix first, alone | PR #186; §3.2, §14 #1 |
| 2 | One agent Space per task (O2 window is v2) | §7 |
| 3 | Approval per client until revoked, 30-day expiry, plus strict mode; bound to the paired executable and ancestor | §5 |
| 4 | The CLI does lifecycle and the relay; agent-browser does page actions | §11.2, §12 |
| 5 | No incognito in v1 (the capability does not exist) | §13 |
| 6 | Menu-bar item plus a Dock badge (and a strip on the driven visible tab) | §6.2 |
| 7 | Visible capture for the lease | §8 |
| 8, 9 | No freezer, discard or Glic patch: keep-alive via `SetIsPinnedTab` and `SetGlicActuationState` | §8, §14 |
| 10 | Skill installed from settings only | §12 |
| 11 | No `cookies` or `network` capability in v1 (and the leaks that made "without" false are closed, §4.5) | §4.3, §5.3 |
| 12 | **Maintainer:** build the CLI with the Chromium tree's own toolchain (C++, or Rust via GN) so friends need no extra installs (#113). **Design author's choice, not a maintainer decision: C++** (two GN targets, `koan` and `koan-relay`; rationale in §12), open to override | §12 |
| 13 | `eval`/script step early, not only a DSL | §20.2 |
| 14 | Ghost cursor and progress line, muted agent tabs, an app-suspension blocker (not display sleep) | §8, §19.4 |
| 15 | Say plainly that the agent Space shares the profile's logins. **Superseded 2026-11-10**: agent Spaces are containers and share no logins; the warning now applies to the "Your tab" lease only | §7, §19.4 |
| 16 | Client records Keychain-bound (`os_crypt`) and keypair auth | §5.2, §5.4 |
| 17 | `observe` defaults off | §5.3 |
| 18 | Agent-visited URLs are not added to history or the omnibox by default (a setting) | §7, §14 #3 |
| 19 | Raw length-prefixed framing on the unix socket; the relay translates | §2, §4.1 |
| 20 | The ego(lite) run is mandatory for the "beats ego" headline, with a `< 1.0×` bar | §20.7 |
| 21 | In-browser `snapshot`/`act`/`run`, diffs and event waits move into phase 3; no "faster" claim before phase-3 numbers | §16, §20 |
| 22 | **User, 2026-11-07:** the master switch is **"Agent ready", on by default** (replaces "off by default"); free when idle; the first-contact prompt stays mandatory; no listener in Incognito, Guest or a non-default user-data-dir unless turned on there; the indicator ships before, or with, anything that makes the default-on gateway reachable by a real build; battery: half the leased-tab setting (2 by default) and no `live: always` / long `keepAlive` on battery in Low Power Mode | §0, §1, §3.3 (T21, T22), §3.4, §4.1, §6.1, §6.2, §8, §13, §16, T-ready-*, T-power-2, 3 |
| 23 | **User, 2026-11-07:** the limits are **user settings with defaults**: awake agent tabs (4, 1 to 16, a note above 8), fewer awake tabs in Low Power Mode on battery (on, half, minimum 1), approval requests per minute (3, 1 to 10), failed attempts before a 5-minute block (5, 1 to 20); read with a fallback to the default for anything out of range or corrupt; no "unlimited" and no 0; fixed: one prompt on screen, the pre-auth limits, the open-tab budgets (a later setting); a change applies to new connections and leases only | §6.1, §8, §5.1, §5.2, T-limits-* |

### 17.2 Still open (smaller; proposals in the sections named)

| # | Question | Proposal |
|---|---|---|
| O1 | The power numbers for a MacBook Air: hold-off 3 s, 4 concurrent leases, 8/12/16 tabs per task/client/total | §8 proposes; confirm or change after T-keep-9, 10, 12 |
| O2 | *(resolved in design, §7.4)* The history/omnibox/saved-group/session mechanism | saved groups: already excluded by `SavesTabGroups` when Spaces are on (agent control requires Spaces on); session restore: marker + `TabRestoreHook` + sweeper (no patch); history: §14 #3; Recently Closed and session files: conditional §14 #5, #6, decided by T-sync-1 |
| O3 | How strict ancestor pinning is (soft re-prompt on a new chain, once per launch) | §5.5 proposes; revisit after the first week of use |
| O4 | Whether agent-browser's daemon reconnects with the same relay path after a dropped connection | T-ab-4 tests it; decides single-use vs session-lifetime secret (§11.1) |
| O5 | Keychain-ACL-bound client key (the CLI inside Koan.app) | later hardening; UNVERIFIED feasibility (C§5) |

## 18. Notes and discrepancies found while auditing

* `docs/PERFORMANCE.md` says energy saver "caps frame rates"; the audit found no frame-rate cap code in `cc/`,
  `viz/`, `ui/compositor` or `content/` (A§4). Either the doc is wrong or the cap is elsewhere: worth a check in the
  performance thread; it matters here because it decides whether agent tabs animate at full rate on battery.
* Chromium's own agent (`chrome/browser/actor/`, Glic) uses the same capture + priority recipe; its source is the
  best reference for phase 2 and is not otherwise linked into Koan.
* ego(lite) is closed source; only its public page and quick-start were read (2026-11-07). Its Space/hand-over and
  engine-snapshot ideas are adopted as options above; nothing else is assumed about its implementation.

## 19. How ego(lite) works (public sources, 2026-11-07)

Full record, with quotes and paths: `.agent/notes/upstream/ego-lite-public-research.md`. Hands-on, black-box
findings from the Mac (transport, isolation, real snapshots, baseline timings): zen-ref's
`.agent/notes/ego-lite-teardown.md` (pending; §19.6 says what it will settle). Labels: **C** = confirmed in a public
source, **I** = inferred. Rules: the vendor's Terms forbid reverse engineering, so no binary was analysed; Koan
copies none of their code, scripts, prompts, skill text or snapshot syntax; it is designed independently from the
requirements in this note.

### 19.1 Facts

| Topic | What is published | |
|---|---|---|
| What is open | `github.com/citrolabs/ego-lite` (MIT): the agent-side runtime (`package/ego-browser`) and the skill package. The browser is closed. The `globalThis.ego` bindings come from the app | C |
| Skill | Two layers. The installed entry skill is ~1.7 KB and only says to run `ego-browser skill`, which prints the real ~24 KB guide from the installed app, so the guide always matches the app version. The app copies the entry skill to `~/.agents/skills/ego-browser` (if missing) and, per onboarding, to `~/.claude/skills/` | C |
| Agent interface | `ego-browser nodejs <<'EOF' … EOF`: **arbitrary ESM JavaScript** run in a Node.js (v24) built into the app, with `taskSpace()`, `Page` objects and `ego` bindings in scope. Other subcommands: `skill`, `upgrade`, `import`, `help`. The launcher is a native helper in the app bundle. The script does not inherit the shell cwd or environment | C |
| Per-run model | Every invocation is a fresh script run; task spaces, tabs and permanent page labels (`p1`, `p2`…) persist in the browser and on disk (`~/.ego-browser/state`); JS variables do not. Guidance: do the planned actions, wait for the end state, print `page.snapshot()` last so the next round starts from it | C |
| API | A small custom Page/TaskSpace API ("not Playwright"): `goto`, `click`, `fill`, `press`, `waitFor*`, `snapshot`, `screenshot`, `evaluate`, `cdp`, mouse/keyboard, dialogs, file chooser; locators `@N`, `text=`, `loc=css/role/href`, `xpath=`, CSS, which must match exactly one element | C |
| CDP | Raw CDP goes through native bindings (`ego.sendCDPMessage` / `onCDPMessage`) to a browser-side client. The task must be selected and **owned by the agent**, checked on each send. A custom domain `egoAutomated` hides console messages from the page. A "CDP bridge" blocks `Target.createTarget` | C |
| Transport from the launcher to the browser | **Not published** (socket, pipe, XPC, HTTP unknown). No authentication between CLI and app is described anywhere. The only gate documented is the host agent's own permission mode: the Claude Code guide tells users to enable Bypass permissions | C (absence) |
| Snapshots | Built "inside the modified Chromium engine, not with a script added to the page"; AX-tree derived; include OOPIFs and open shadow roots (e2e test). Format (from the repo's tests, synthetic): indented tree, `role "name" [ref=N, loc=…, url=…]`, `iframe` with nested `root`, a trailing "more content below" marker, subtree scope, **no diffs** (`diff:true` throws). Refs are SDK-assigned per frame/document/backend node and invalidated on navigation | C/I |
| Waits | Action timeout 3 s with a 50 ms re-check; a missing target is abandoned once the page shows no DOM mutation and no network for 500 ms; `waitForSelector` 10 s; `goto` 15 s; CDP request 15 s; page budget 8. Guidance: avoid fixed delays | C |
| Spaces | Product pages: a Space is "one isolated BrowserContext per task, with its own cookies and storage", "not a new window or profile". The vendor's own `clearing-state.md` says a task space's profile cookie jar and cache are **shared with the user's tabs and every other task space on the profile**. Spaces are user-visible objects (overview, pinned, restore); they belong to a profile; a task is created with a profile id. Changelog hints at hidden windows for task tabs. How a Space is rendered is unknown | C/I |
| Hand-over | `ownership` is `agent` or `user`. Browser prompts (location, camera, USB, notifications, …) move the Space to the user with a reason key; `handOff()`, `waitForControl()`, `takeOverTaskSpace()`; claiming a user-owned Space needs the user's approval; page-authored `alert/confirm/prompt` stay with the agent. While an agent controls a Space: read-only address bar, control overlay and glow, labelled agent cursor, tabs muted, the system kept awake, file pickers/credential dialogs/protocol handlers suppressed, auto-close after 30 min idle | C |
| App | Chromium 156.0.8078.74 (changelog), bundle id `com.citrolabs.ego.lite`, a Node helper app, a separate bundled `local-ego-daemon` with its own web UI, an updater fork. Hosted skill/ToS: personal, non-commercial use, no reverse engineering | C |

### 19.2 What stands out

* Their agent API is a **script** (a Node process) that talks to the browser through a native channel, not a wire
  protocol that any program can speak. Agents get one round trip per script, which is where "fewer round trips" comes
  from; the cost is that arbitrary agent JavaScript runs inside a helper process of the browser app.
* **No security layer is published**: no pairing, no per-client prompt, no capabilities, no audit log, no incognito
  rule that we found. Their model is "the agent can always reach your real logins and tabs" (FAQ). That is the
  largest place Koan can differ on purpose (§3).
* **Their Space is not a cookie boundary** in the default case, whatever the marketing page says. Ours also shares the
  user's profile (that is the requirement); we should say so plainly in the UI and docs rather than imply isolation.
* The snapshot has **no incremental mode**, and its token claim ("200 to 400 tokens for a typical page") is
  unmeasured. Diffs are an open lane.
* Waiting is event-driven in spirit (mutation/network idle) but with long defaults (3 s action budget).

### 19.3 Unknowns (not guessed)

The launcher→browser transport and any authentication; whether a remote-debugging port exists; the launcher's
language and IPC names; what "Aperitif" is; the engine mechanism of snapshots (which Blink/AX code, browser vs
renderer, closed shadow roots); how a Space is windowed; whether two agent Spaces on one profile are isolated at all;
the full CLI flag list; the published npm package name; a real captured snapshot (all public examples are fixtures).
Binary-level questions are off the table because of the Terms.

### 19.4 Technique by technique: adopt, improve, reject

| ego technique | Decision | Koan's version, and why |
|---|---|---|
| Agent works in its own Space; user tabs untouched unless handed over | **Adopt** | §7 agent Space. Same promise, built on Koan's tab-group Spaces. Honest copy: it shares the profile's logins |
| One Space per task, named, resumable by name | **Adopt** | `Koan.createTask {name}`; resuming by name returns the same Space and its page labels |
| Stable permanent page labels (`p1`, `p2`) across runs | **Adopt** | per-task tab labels persisted with the session; used by every command (`koan click p2 @12`) |
| Page budget (8) and 30-min idle auto-close | **Adopt, tunable** | per-task page cap (default 8), per-client cap (20), idle end of task with the Space left for the user to dismiss (§8 lease idle is separate) |
| Visible agent affordances: control overlay, glow, read-only address bar, labelled agent cursor, short progress line | **Adopt** | §6.2 indicator + banner; plus an optional per-action label shown beside a ghost cursor and a one-line progress text, because "watch" is a requirement |
| Hand-over on browser permission prompts (location, camera, USB…) with a reason key | **Adopt** | pause the agent's tab, anchor the normal permission bubble, event `Koan.userNeeded {reason}`; same pause machinery as §9 |
| Page-authored dialogs stay with the agent | **Adopt** | `Page.handleJavaScriptDialog` allowed; `beforeunload` handled explicitly (they note it is not) |
| Suppress file pickers, credential dialogs and protocol handlers while the agent drives | **Adopt, but surface them** | do not suppress silently: treat them as `userNeeded` events so a task never hangs without telling anyone |
| System kept awake during an agent task | **Adopt, narrower** | hold an app-suspension (idle-sleep) blocker while a command or task is active; **not** a display-sleep assertion (§8 `stay_awake`). Chromium wake-lock type mapping on macOS to confirm (T-power-1) |
| Tabs muted by default for agent tasks | **Adopt** | `SetAudioMuted` on agent tabs unless the task opts in |
| Agent API = arbitrary JS in a Node process inside the browser app | **Reject for v1** | arbitrary code in a browser-hosted process defeats per-method policy, audit and pause. Use declarative batch programs (§20.2) whose every step is gated. If scripting is wanted later: run it in the **CLI process** (S3) and make each call over the authenticated socket |
| "Fresh process per run, state persists in the browser" | **Adopt** | §20.1 session state in the browser; stateless CLI |
| `ego-browser skill` prints the guide from the installed app (never stale) | **Adopt, independently** | `koan skill` prints a guide generated from the installed version; the installed entry skill stays a few lines. Our text is written from our own command set |
| Auto-install the skill into `~/.agents` and `~/.claude` on first launch | **Reject** | a browser should not write instructions into other tools' directories unprompted. Install on a settings click (§12) |
| Tell users to run the agent in "bypass permissions" mode | **Reject** | `koan` is one command prefix a user can allow-list; the skill documents that |
| `egoAutomated`: hide page console messages from CDP so sites cannot detect automation | **Improve (different goal)** | we do not build a stealth layer. We avoid needing `Runtime.enable` for ordinary flows (so the console-serialization side effect that makes DevTools detectable is not triggered by default) and say in the docs that agent traffic is not hidden from sites |
| `Target.createTarget` blocked in the CDP bridge | **Adopt** | same reason as §4.3: tabs are created by Koan code, in the agent Space |
| Ownership checked on every CDP message | **Adopt, generalise** | per-message check of client, task, capability, tab state and pause (§4, §9) |
| Snapshot built in the engine from the AX tree, OOPIF/shadow included | **Adopt the idea, build with public API** | `RequestAXTreeSnapshot(kAll)` (§20.3). We cannot fork Blink; the benchmark shows whether that suffices |
| `@N` refs unique across frames, stale-ref errors | **Adopt** | §20.3, with re-binding when the same role+name node is re-rendered |
| `loc=` stable locators only when unique | **Adopt** | locator hints in snapshot lines only when they resolve uniquely |
| Snapshot is not a diff; one full snapshot per round | **Improve** | incremental diff since the last snapshot and diff-as-action-result (§20.3) |
| Waits: 3 s action budget, 500 ms idle give-up, 50 ms re-check | **Improve** | event-driven signals first (§20.4), their numbers used as the starting defaults to beat in the benchmark |
| One heredoc per round: act, wait, print the final snapshot | **Improve** | `koan run` batches with conditionals and returns the diff itself, so the agent does not have to remember to print it |
| `evaluate()` and `cdp()` escape hatches | **Adopt, gated** | `Runtime.evaluate` allowed under the policy table; `cdp` passes only allowlisted methods (§4.3) |
| Space = profile-scoped, shared cookies | **Adopt as a stated fact** | same profile is required; copy in settings says so; no isolation promised |
| No pairing, no per-client approval, no capabilities, no audit log, no incognito rule found | **Do better** | §3 and §5 |

### 19.5 Where Koan should win, stated as testable claims

(1) fewer LLM turns per task (batches + diffs as results); (2) fewer tokens per turn (compact snapshot, diffs); (3) no
fixed or generous waits; (4) agent tabs genuinely alive in the background (§8) with screenshots on demand; (5) security
the user can inspect: approval, capabilities, indicator, audit, incognito excluded. §20.7 turns 1-4 into measurements.

### 19.6 What the hands-on teardown must settle before these decisions are final

zen-ref's note is expected to cover: the launcher→browser transport and whether it needs auth; how a Space is rendered
(window, group, hidden); isolation in practice (do two Spaces see each other's cookies?); a real captured snapshot
(size in tokens on a set of pages, cross-origin iframe, closed shadow root, fidelity); and baseline timings for the
task suite in §20.7. Rows above that depend on it are marked in a follow-up commit; none of them is a build-time
dependency, because Koan's design does not rely on ego's internals.

## 20. Faster than ego(lite), and a benchmark that proves it (maintainer, 2026-11-07)

**Premise.** In an agent loop, wall time is `Σ (LLM turn + tool time + waiting)`. A turn costs seconds and grows with the
tokens in context; a browser primitive costs milliseconds; a fixed `sleep(2)` costs 2 s every time it is used. So the
design goal is **fewer turns, fewer tokens per turn, zero idle waiting**. Anything below that does not shorten the
loop is not worth building first. Every claim in this section is a hypothesis the harness (§20.7) tests; none is
measured yet. **This whole section is phase 3** (§16): through the phase-2 agent-browser path Koan's call pattern is stock
CDP plus a hop, so it cannot be faster on turns or tokens, and **no user-facing copy says "faster" until the phase-3
numbers, ego's included, exist.** The defensible gaps against ego are diffs (ego has none), event-driven waits (ego:
3 s action budget, 500 ms idle give-up), background keep-alive, and security; those lead, not per-call connect.

| Cost in the loop | Typical stock-CDP behaviour | Koan lever | § |
|---|---|---|---|
| Per-call connect | stock CDP tools reconnect or hold a daemon; **ego's host is one long-lived process behind a native binding, so it has no per-call connect either** (research note §1.3) | not a lever: Koan matches it (session state in the browser, warm socket) | 20.1 |
| One action per LLM turn | N turns for an N-step form; ego batches through a JavaScript script per round | batched programs with conditionals **and an `eval` step** in one call | 20.2 |
| Re-reading the whole page every turn | 3-30 k tokens per snapshot; **ego has no diffs** (`diff:true` throws) | compact snapshot + incremental diff + action results that carry their own diff | 20.3 |
| `sleep`/polling to wait | 1-3 s per step, flaky | event-driven waits (navigation, network quiet, frame-quiet, ref present) | 20.4 |
| Screenshots as the default observation | 1-2 k tokens each + vision latency | text first, screenshots on request, small and annotated | 20.5 |
| One agent, one tab, serial | serial wall time | parallel agent Spaces, each with its own lease | 20.5 |
| Background tab stalls, so the agent retries or foregrounds it | lost turns, flaky screenshots | keep-alive lease (§8) | 8 |

### 20.1 Persistent connection and session state in the browser

* The gateway already is the daemon: Koan stays running, the socket is warm, and a native CLI start plus `connect()`
  plus the signature handshake is a few milliseconds, not a node/python cold start. **No separate CLI daemon is needed.**
* Per-client **session state is held in the browser**, keyed by `KOAN_SESSION` (env) or `--session`: current tab,
  the ref table, the last snapshot (for diffs), the active task. Consecutive `koan` invocations are therefore
  stateless processes over warm state, which is what a shell-based agent produces anyway. Sessions expire after idle.
* For agents that can hold a process open: `koan shell` (JSON lines on stdin/stdout over one connection; also what the
  agent-browser relay and the benchmark's scripted mode use) and `koan watch`.
* The agent-browser relay (§11) already has its own long-lived per-run connection; agent-browser's own daemon keeps
  its CDP socket, so it pays no per-command connect either.

### 20.2 Batched actions with conditional waits (one round trip)

`koan run` (and `Koan.run` over the wire) takes a small **program** and executes it in the browser process, one pass:

```json
{"tab":"t3","steps":[
  {"go":"https://shop.example/search?q=usb+hub"},
  {"wait":{"any":[{"ref":{"role":"link","name":"~/USB hub/"}},{"text":"No results"}],"timeout":8000}},
  {"if":{"text":"No results"},"then":[{"return":"empty"}]},
  {"click":{"role":"link","name":"~/USB hub/","nth":0}},
  {"wait":{"nav":true}},
  {"extract":{"price":{"role":"text","name":"~/\\$\\d+/"},"title":{"role":"heading","level":1}}}
],"return":"diff","maxSteps":40}
```

* Steps: `go`, `click`, `fill`, `type`, `press`, `select`, `hover`, `scroll`, `wait`, `if/then/else`, `repeat/until`,
  `extract`, `snapshot`, `screenshot`, `tab` (open/switch/close inside the task), `eval`, `return`. **`eval`** (decision 13)
  runs a JavaScript function in the page (main world, or an isolated world the program created) through
  `Runtime.callFunctionOn` under the same gates, with the program's variables passed as arguments and its return value
  usable by later `if`/`repeat`/`extract` steps. It is what gives a declarative program the loops, string handling and
  branching on extracted values that ego gets from running arbitrary JS per call, without running agent code in the
  browser process. Whether the DSL is expressive enough is the first benchmark question (§20.7). Targets are refs (`@12`),
  or role+name locators resolved by the browser against the current tree (a locator survives a re-render; a ref may
  not, §20.3).
* **Every step passes the same gates as a single call**: method policy, URL scope, capability checks, the
  pause check (a user keystroke between two steps stops the program with `Koan.paused` and a partial result), rate
  limits, one audit line per step. Batching must not be a policy bypass.
* Result: per-step status, extracted values, and either the final snapshot, the **diff since the program started**
  (default), or nothing. On a failed step the result carries the failing step, the reason ("covered by dialog
  'Cookie settings'"), and the diff, so the agent can recover in the *same* turn it learns of the failure.
* Bounded: max steps, max wall time, and no unbounded loops (`repeat` needs `until` and a count). A **CLI-side script
  runner** (S3, embedded in `koan`, calling the same gated methods over the socket) follows in phase 3 if the DSL plus
  `eval` loses turns in the benchmark; agent code never runs in the browser process (§19.4).

### 20.3 Compact snapshots built in the browser, with refs and diffs

Built on `WebContents::RequestAXTreeSnapshot(callback, ax_mode, max_nodes, timeout, AXTreeSnapshotPolicy::kAll)`:
public, **does not change the tab's accessibility mode**, and `kAll` includes out-of-process (cross-origin) iframes
(`web_contents.h:633-652`, verified in source). Shadow DOM is part of the AX tree (UNVERIFIED for closed roots; test
T-snap-3). No script is injected into the page.

* **Format** (text, one node per line, indentation = depth; this is the proposal, the benchmark tunes it):
  ```
  page "Cart (2) - Shop" https://shop.example/cart  @1
    nav "Main" @2
      link "Home" @3   link "Search" @4
    main
      heading 1 "Your cart" @9
      list 2 items
        item @12: link "USB hub 7-port" @13  text "$24.99"  spinbutton "Qty" =1 @14  button "Remove" @15
        item @16: link "HDMI cable" @17  text "$9.00"  spinbutton "Qty" =1 @18  button "Remove" @19
      button "Checkout" @21 [primary]
    frame "Payments" (cross-origin pay.example) @30
      textbox "Card number" @31 [required]
  ```
* **What is dropped**: nodes with no name, role or action that only wrap others (flattened), duplicate static text,
  decorative images, nodes outside the viewport collapsed into `… 37 more below` (with a count and a `--scroll`
  hint) unless `--full`; per-node text capped (~80 chars); long lists elided after N with a count; **password-field
  values never returned**; hidden/`aria-hidden` nodes omitted. Interactive nodes always keep a ref; non-interactive
  context stays unreferenced.
* **Stable refs**: a ref is a per-session integer bound to (frame token, backend DOM node id), never reused within
  a session. If the node is replaced but the same role+name sits in the same place, it is re-bound to the old ref
  (cheap heuristic, tested in T-snap-2). A stale ref returns a typed error plus a fresh nearby candidate, not a
  silent mis-click.
* **Incremental diff**: `koan snapshot --since` (and every action's default result) returns only
  `+`/`-`/`~` lines with enough parent context to place them, plus `unchanged: 143 nodes`. This is what keeps context
  small across 20 steps and keeps the prompt prefix stable (good for model prompt caching). A full snapshot is one
  flag away.
* **Actions by ref in the browser** (`Koan.act`): resolve bounds → scroll into view → hit-test the click point (if
  another node covers it, fail with its name rather than click it) → inject input through the browser-side input path
  (marked as injected so the pause logic ignores it, §9) → wait per §20.4 → return the diff. One call replaces
  `snapshot → click → wait → snapshot`.
* Budget targets, to be proven or dropped by the benchmark: p50 snapshot of an average page ≤ 1,500 tokens; p95
  ≤ 6,000; a typical click's result ≤ 200 tokens; snapshot latency ≤ 150 ms for 5,000 AX nodes.
* Fallback and parity: S1 (agent-browser's client-side AX snapshot through the CDP facade) stays supported; the
  benchmark compares S1 and S2 on the same pages, which is also how §10's decision gets made with data.

### 20.4 Event-driven waits (no sleeps)

Every action and `wait` ends on a **condition**, not a delay. Signals, with what is verified:

| Signal | Source | Status |
|---|---|---|
| navigation committed / failed | `WebContentsObserver::DidFinishNavigation` | verified API |
| document ready / load | `DOMContentLoaded`, `DidFinishLoad`, `DidStopLoading` | verified API (`web_contents_observer.h:422,495-520`) |
| network quiet | Blink lifecycle events `networkAlmostIdle`/`networkIdle` via an internal `Page.setLifecycleEventsEnabled` on the tab's own client; `ResourceLoadComplete` only reports completions (`:537`), so it cannot alone count in-flight requests | to verify (T-wait-1) |
| frame-quiet ("DOM settled") | no new compositor frame for *Q* ms while the lease is held (a page that changes also produces frames), plus network quiet | to verify which observer exposes frame submission (T-wait-2); not `AccessibilityEventReceived`, which needs `AXMode::kWebContents` on (`:883`) |
| ref/text/role present or gone | re-resolve against a fresh AX snapshot **when a frame was produced or a load event fired**, not on a timer | by construction |
| URL matches, new tab opened, dialog opened, download started | tab/dialog/download observers | verified APIs exist; wiring in phase 2 |

Defaults after an action: wait for any navigation it started, then network quiet, then frame-quiet (*Q* = 120 ms,
tunable), capped at 5 s; the cap expiring returns the diff and a `settled:false` flag instead of an error. Fixed sleeps
exist only as an explicit `sleep` step.

### 20.5 Keep-alive, parallelism, text-first

* The lease (§8) keeps agent tabs at full frame rate, so frame-quiet and screenshots are reliable and a tab never has to
  be foregrounded.
* **Parallel tasks**: several agent Spaces at once (§7), each with its own lease and session; `koan run` calls are
  independent, so an agent that can run shell commands in parallel (or several agents) overlaps its page loads and
  its LLM turns. Gateway limits: per-client max tabs, a CPU-friendly cap on simultaneous leases (default 4, a setting from 1 to 16, §6.1).
* **Text-first**: every command returns text/JSON by default. `--shot` adds a viewport JPEG (default max 1024 px on
  the long edge, quality 60, ≤ 150 KB) and `--marks` draws ref numbers on it (set-of-marks) for vision models. Full-page
  and `clip` captures are explicit, and never on the user's visible tab (§9.3).

### 20.6 What we expect not to beat ego on, and what ego can do that we cannot yet

* An engine fork can add Blink/AX hooks that a hook-patch browser should not carry; we use only public `content::`
  APIs plus 1-3 tiny patches, which caps how deep our snapshot can go (e.g. a Blink-internal "is clickable" bit).
* ego imports the user's whole Chrome (extensions, passwords). Koan is already the user's browser; no import step.
* §19 lists what the research could and could not confirm; claims about ego are labelled CONFIRMED or INFERRED there.

### 20.7 Benchmark harness: `scripts/agent-bench/` (design; no code in this PR)

**Purpose.** Prove or disprove "faster" with numbers on identical tasks across tools, so decisions (S1 vs S2, batching
shape, diff format, wait defaults) are made on data. It is built before the features it judges: phase 2 begins with
the harness and a baseline run against stock Chrome + agent-browser.

**Targets (adapters).** Each adapter provides: how the agent is told to use the tool (a skill/instructions file), setup
and teardown, and the environment variables.

| Target | What runs | Notes |
|---|---|---|
| `koan` | Koan + `koan` CLI + skill (S2 snapshots, batches, diffs) | the subject |
| `koan-ab` | Koan + `koan agent-browser` relay (S1 client-side snapshots) | compatibility path, §11 |
| `chrome-cdp` | stock Chrome with `--remote-debugging-port` on a **fresh non-default** profile (Chrome 136+ rule, §3.2) + a thin CDP CLI | upper bound for "stock CDP"; no real logins |
| `agent-browser` | agent-browser launching its own Chrome | what most agents use today |
| `ego` | ego(lite) + its published `ego-browser` skill, **unmodified** | **mandatory for the headline claim** (decision 20; the user has consented; zen-ref runs it). Mac only. **Contained**: ego's CLI starts its GUI app through LaunchServices, which `sandbox-exec` does not confine, and in the first teardown attempt it wrote outside any sandbox (shell config, agent skill and rule directories, `~/.local/bin`, a keychain item). So: a dedicated macOS user account or VM, a before/after snapshot of those paths, black-box only (its Terms forbid reverse engineering and limit use to personal, non-commercial use), and nothing deleted or edited in the owner's real home without their say-so. Its login-reuse needs an import, recorded as a deviation |

**Task suite (16 local + 5 live-web, fixed, versioned).** A local deterministic test site (`site/`, Node, no network) on two origins
(`127.0.0.1:A` and `localhost:B`, so frames across them are genuinely cross-origin) records every form submission
and click server-side; **success is decided by the site's recorded state, never by the agent's claim**. Categories and
tasks:

| # | Category | Task (agent is given only the natural-language goal) | Stress |
|---|---|---|---|
| 1-3 | search and extract | find the cheapest in-stock item among 3 pages; extract a table into JSON; follow pagination to find an entry | snapshot size, diffs, extraction |
| 4-5 | form fill | multi-step signup with validation errors; address form with dependent selects | batching, conditional steps |
| 6-7 | multi-tab compare | open 3 product pages in separate tabs, compare, pick one; collect values from 2 tabs into a third | parallel Spaces, tab handling |
| 8 | login-reuse | the profile already holds a session cookie for the site; open the account page and read the balance without logging in | shared profile |
| 9-10 | iframe widget | a cross-origin payment-like iframe with its own inputs; a nested iframe | cross-origin snapshot/actions |
| 11 | shadow DOM | a web component whose controls sit in open and closed shadow roots | snapshot reach |
| 12-13 | async content | content appears after a 1.5 s timer and an XHR; a button enabled only after a CSS transition ends | waits, animations |
| 14 | infinite scroll | load until a target entry appears | scroll + wait |
| 15 | dialogs and popups | `confirm`, a new window, a file-less download trigger | edge handling |
| 16 | **background run** | any of the above run while the browser window is **covered by another app / unfocused / the tab is not active** | keep-alive; screenshots and animation tasks must still pass |
| 17-21 | live web (**part of the headline**) | 5 pinned public pages run in the same interleaved blocks as the local tasks: a docs search, a wiki infobox extraction, a form that is filled but not submitted, a paginated list, a page with an embedded iframe widget. Needs network access; the pinned URLs and expected values live in `tasks/live/` and are re-verified before each run | real network time, real third-party scripts |

Each task is a JSON spec: goal text, start URL/state (seeded cookies, tabs), verifier endpoint, limits
(`maxToolCalls`, `maxSeconds`, `maxTokens`).

**Two modes.**

1. **Scripted-oracle (no LLM; deterministic; cheap).** A canonical minimal plan per task is expressed once in a tool-neutral
   step list and compiled per target to the *calls that target needs* (for `agent-browser`: snapshot, click, wait, snapshot…;
   for `koan`: one `run`). Measures tool-side time, calls required, bytes and estimated tokens returned, and success. This
   isolates **tool efficiency** from model noise, and runs in CI-like fashion on the Mac.
2. **LLM-agent (the real loop).** A fixed agent: the same model id, temperature 0, same system prompt, same task text,
   target-specific skill file, a hard budget. Headless Claude Code (`claude -p` with the skill) or an SDK loop, 5 runs
   per task per target, randomized order, cold browser state per run. Reads usage from the API for exact tokens.

**Metrics (recorded per run; every run is kept):** wall time (total; of which LLM, tool, wait), number of
tool calls (and turns), input/output/cached tokens, tool-result bytes and estimated tokens, **success** (site-verified)
with a failure taxonomy (wrong result, gave up, budget exhausted, stuck on a stall, policy-denied), screenshots taken,
retries, time-to-first-progress, plus environment (versions, machine, power state, window state: foreground vs
covered). Also a **micro layer** over 100 iterations each: connect+auth, `snapshot` (ms, bytes, tokens) on 10 saved real
pages, `click`+settle, `wait nav`, in a foreground and a covered window.
The **summary statistic is the paired ratio with a bootstrap 95 % CI** (defined under "The one statistic", below); medians and IQRs are printed for context only and never decide a verdict.

**External baselines (zen-ref's ego(lite) numbers, and any other hand-measured target).** Not every target can be
driven by an adapter (ego needs the Mac and the user's consent; the Mac thread measures it black-box). The harness
therefore accepts a **baseline file** per target, `baselines/<target>-<date>.json`, written by whoever measured it:

```
{ "target": "ego-lite", "version": "0.6.2.9", "measuredBy": "zen-ref", "date": "...",
  "machine": {"model": "...", "os": "...", "power": "ac", "window": "foreground|covered"},
  "method": "black-box | scripted-oracle | llm-agent", "model": "<model id, if llm>",
  "suite": "agent-bench-v1",
  "tasks": { "t04-signup": { "runs": 5, "success": 5, "wallMs": [..], "toolCalls": [..],
             "inputTokens": [..], "outputTokens": [..], "resultBytes": [..], "notes": "" } },
  "micro": { "snapshotMs": [..], "snapshotBytes": {"pageId": n}, "waitNavMs": [..] } }
```

`report/` merges adapter runs and baseline files into one table and marks each cell **measured here** or
**imported (by whom, when)**; imported cells are never mixed into a pass/fail verdict unless the task spec, the model
and the machine match (the report prints the mismatches). The micro layer reuses the same fixture pages as the Mac
thread's measurements, so zen-ref's page set (saved to `scripts/agent-bench/fixtures/` as static pages, never the
vendor's files) is comparable byte for byte. Vendor claims (e.g. "47-63 % faster on 31 tasks") are listed as
claims, not data.

**Fairness rules.** Same tasks, site, model, budgets and machine; ego and anything on the Mac run only with the user's
OK; no target gets task hints beyond its own skill; login-reuse seeds the same cookie for all (ego via its import
step is recorded as a deviation); the raw transcripts, tool logs and the verifier's records are saved with the report;
a target that cannot do a task (e.g. `chrome-cdp` on #8, or any target on #16 without a keep-alive) is scored as a
failure, not skipped.

**Layout** (all created in phase 2): `scripts/agent-bench/README.md`, `site/`, `tasks/*.json`, `adapters/<target>/`,
`runner/` (spawns target, runs mode 1 or 2, collects metrics), `report/` (CSV + a markdown table + per-task charts),
`results/` (git-ignored). `README.md` is committed with this design as the spec; nothing else.

**The one statistic.** Every bar below uses the same quantity, so nothing in the report can decide a verdict by a
different one:

* **The unit** is a *block*: one task run once by every target back to back in random order (Method, below). For two
  targets and a metric (wall time, tool calls, or tokens sent to the model) each block gives a **paired ratio**
  `r = metric(Koan) / metric(other)`.
* **The summary** is the **geometric mean of the paired ratios** over all blocks (the mean of `log r`, exponentiated), so a
  ratio of 0.5 and one of 2.0 cancel instead of averaging to 1.25.
* **The interval** is a **bootstrap 95 % CI**, percentile method, 10 000 resamples, resampling **tasks first and blocks
  within a task second** (blocks of one task are not independent), with the seed written into the report.
* **A bar is met when the upper bound of that CI is below the bar.** A point estimate under the bar with an interval that
  crosses it is "not shown".
* **Failed and timed-out runs** count at the task's time/call/token budget for the ratio (a failure cannot make a target
  faster), and success is compared separately: the CI of the paired success difference (Koan − other) must not lie entirely
  below zero.
* Medians and IQRs may be printed for context. They never decide a verdict.

**Pass bar for the claim "faster than ego"** (set now so the result cannot be fitted later). The claim holds when, on the
LLM-agent suite including the live-web slice, with ego's run in the same interleaved blocks:

* **headline: the upper 95 % bound of the paired wall-time ratio Koan / ego is `< 1.0`** (the point estimate should be near
  0.8×; the bar is "beats", not parity), and the success difference does not lie entirely below zero;
* **against `agent-browser` on stock Chrome:** the upper 95 % bound of the paired ratio is `< 0.8` for wall time, `< 0.6` for
  tool calls and `< 0.5` for tokens sent to the model, with the same success condition;
* task 16 (the covered/unfocused window) passes for Koan and fails or degrades for `agent-browser`/`chrome-cdp`.

If a threshold is missed, the report names the lever (§20.1-20.5) that underperformed and the claim is not made.
ego's published figures (398 s mean per task, 93.5 % success, $1.64 per task, "47-63 % faster on 31 tasks") are
vendor-reported on an undisclosed task set; they are listed as claims, never as data.

**Method (review finding 19).**

* **Interleaved blocks, paired differences, bootstrap CIs.** For each task, all targets run back to back in a random
  order, repeated; the report shows the paired ratios with bootstrap CIs (the statistic defined above), not medians of five. Temperature 0 does not make
  agent runs deterministic, so n is not 1.
* **Scripted-oracle plans are an upper bound** (authored per target by the Koan team): label them so; claims rest on LLM mode.
* **Skill text is a confound** (length, quality): record the skill's token count per target and run ego's published skill
  unmodified; Koan's own skill is written from Koan's command set (§12).
* **Tokens**: record cached and uncached input tokens and output tokens from the API usage, and the cost.
* **The live-web slice (tasks 17-21) is part of the headline**, not optional, and the task table says so: the local site has near-zero network time and
  overweights tool-side wins. 5 pinned public pages (docs search, a wiki infobox, a form that does not submit, a paginated
  list, a site with an iframe widget), run in the same blocks as the local tasks.
* **Headed vs headless is specified**: every target runs **headed** for the headline (`agent-browser --headed`, headed
  Chrome for `chrome-cdp`), because task 16 depends on a real window being covered; headless is a secondary row.
* **Time at the harness boundary**: a monotonic clock around the whole agent process (LLM latency included, split using
  the API's reported timings), not each tool's self-reported time.
* **Power**: record browser CPU time per process group and, where permitted, `powermetrics` energy, because the lease costs power.
* **Budget**: 21 tasks × 5 targets × 5 runs ≈ **525 agent runs** for the full suite; run a 3-run pilot first, then the full
  suite once per phase-3 candidate, and say what it cost.

**Ordering.** (1) phase 2b: harness + site + oracle mode + `chrome-cdp` and `agent-browser` baselines, and zen-ref's
ego baseline imported; (2) phase 2c-2d: gateway/lease/CLI, then S1 numbers through `koan-ab` (expected: no speed
advantage, which is the point of measuring); (3) phase 3: `Koan.snapshot/act/run`, diffs and wait signals, re-measured,
then LLM mode with ego in the blocks; (4) the claim, or the report of which lever fell short.

## 21. Response to the #185 review (finding → where it landed)

| Finding | Landed in |
|---|---|
| 1 Network leaks cookies/Authorization; `loadNetworkResource`, `deleteCookies`, `clearBrowserCache`; wildcards | §4.3 per-method tables with unlisted = deny and a protocol-coverage test; §4.5 scrubbing; T5; T-sec-4, T-sec-15 |
| 2 forgeable client store; HMAC inconsistency; `CommitPendingWrite()` | §5.2 signatures, §5.4 `os_crypt`-encrypted MACed records, flush on revoke; T-sec-6, T-sec-12 |
| 3 extension worlds, privileged frames, `chrome.debugger`, network-error pages | §4.4; T7; T-sec-11, T-sec-14 |
| 4 clipboard via `commands` | §4.5; T17; T-sec-10 |
| 5 peer identity, claimed names, token file names | §4.1 audit token, §5.1 verified-executable prompt, §5.2 ids not names, §5.5 pinning; T-sec-6 |
| 6 same-uid credential reuse; `observe` default | §5.3, §5.5, §3.5; decision 17 |
| 7 pre-auth surface, HTTP/WS in the browser | §2, §4.1 raw frames; T14; T-sec-13 |
| 8 relay lifetime, secret hygiene | §11.1; T-ab-4, T-ab-5 |
| 9 page-initiated focus and modal surfaces | §9.4, §8 (`ForbidExternalPopupMenus`), §14 #2; T18; T-focus-3..7 |
| 10 audit log integrity, redaction, caps | §6.3; T20 |
| 11 pause on `DidGetUserInteraction`, whole frame tree | §9.2; T-take-1b |
| 12 keep-alive without patches | §8, §14 (freezer, discard and Glic patches dropped); T-keep-5/6/8/12 |
| 13 patch #3 hook location | #186 patches the knob's initial value |
| 14 UI-thread and power cost | §4.1 workers, §8 lighter lease and numbers; T-keep-10/11, T-power-1 |
| 15 agent groups vs sync/history/session | §7.4, §14 B7/#5/#6, T19; T-hist-1, T-sync-1, T-sync-2 |
| 16 the plan contradicts itself on what ships | §16 (speed is phase 3), §10, §20 intro |
| 17 DSL vs JavaScript | §20.2 `eval` step; CLI-side runner in phase 3 |
| 18 strawman lever table | §20 table and intro |
| 19 benchmark gaps | §20.7 method, ego mandatory, `< 1.0×`, interleaving, CIs, headed, live slice |
| 20 agent-browser facade details | §4.2, §11.1 |
| #188 review F2: `ParamFilter` column, `Page.createIsolatedWorld` `grantUniveralAccess` forced false | §4.3 rule 4, §4.4; T-sec-16 |
| Corrections: Accessibility reachable; Glic dependency moot | §4.3, §8 |
