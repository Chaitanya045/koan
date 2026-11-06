# Agent control: design (issue #180, phase 1)

Date: 2026-11-07. Status: **design for review, no code.** Chromium 158.0.8127.2. The source audit behind every
claim here is `.agent/notes/upstream/agent-control.md` (parts A to D; cited below as A§n, B§n, C§n, D§n). Where
this note says "verified" it means read in pristine source; "UNVERIFIED" means it needs the Mac (§15).

## 0. Summary

A local agent on the user's Mac (Claude Code, Codex, Cursor, a script, or Vercel's `agent-browser`) drives the
user's **real, signed-in** Koan profile without stealing focus or the mouse, and the user can watch, take over and
hand back at any time. It is **off by default**, reachable only through a **unix socket** in a 0700 directory, and
every client is **approved by the user in a native prompt** and **revocable**.

The five decisions the design stands on:

1. **Koan owns the endpoint; it does not use `--remote-debugging-port`.** A gateway in the browser process listens on a
   unix socket with **raw length-prefixed frames** (no HTTP or WebSocket parser in the browser) and speaks a **filtered
   CDP** with per-method tables where unlisted means deny (§4). Each tab the agent may touch gets an in-process
   **untrusted** `DevToolsAgentHostClient` (the `chrome.debugger` pattern), so the Browser, SystemInfo and Extensions
   domains, the browser target and local-file access do not exist for the agent (C§2c, C§6); cookie-bearing network
   events are scrubbed, key-event `commands` stripped, extension frames and worlds unreachable.
2. **Agents work in their own Koan Space** (a tab group; Koan has Spaces), one per task. The user's tabs are
   invisible to the agent unless the user hands one over or approves a take-over (§7, §9). This is ego(lite)'s
   model, built on what Koan already has.
3. **A per-tab keep-alive lease** (visible `IncrementCapturerCount` + `PageLiveStateDecorator::SetIsPinnedTab` and
   `SetGlicActuationState`, both public setters) keeps *only agent tabs* rendering with **no upstream patch and no
   global flag** (§8, A§10, review finding 12). It is light on battery: released 3 s after the last command.
4. **`agent-browser` works through a session-scoped relay, not a weakened endpoint** (§11). It cannot dial a unix
   socket; a loopback relay with an unguessable path, started with and outliving its daemon, hides that. The `koan`
   CLI (C++, built with Chromium's own toolchain) does pairing, lifecycle, take-over and the relay; agent-browser does
   page actions.
5. **Security is a feature, not a flag** (§3): off by default, keypair pairing with a native prompt naming the
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
| Agent opens new tabs and works in them while the user works in another tab | `Koan.openTab` creates a background tab in the agent's Space through Koan code (not `Target.createTarget`, which foregrounds, B§5); the lease keeps it rendering | 7, 8 |
| Agent sees the user's current tab, takes it over midway | `Koan.listTabs`/`Koan.read` (capability `observe`/`read`), `Koan.takeOver` (user approves per tab), banner with Take over / Release | 9 |
| Screenshots and animations work while the window is unfocused or covered | lease: visible capturer + priority vote + freeze exemption; a detached/non-active tab renders into its own compositor (A§2) | 8 |
| Agent never steals focus | `Page.bringToFront`/`Target.activateTarget` are no-ops; tabs are created inactive; no `ShowInactive` where avoidable | 4.3 |
| Handoff with an indicator; user input pauses the agent | `InputEventObserver` + the `kFromDebugger` bit; sticky pause until the user hands back | 9 |
| Works on the user's real profile | the gateway runs in the main browser process; no separate profile | 4 |
| Secure; incognito excluded; threat model documented | §3 | 3 |
| agent-browser compatibility (maintainer, 11-06) | §11 | 11 |
| ego(lite) ideas (maintainer, 11-06) | agent Space §7, skill §12, in-engine snapshot §10, watch/take-over §9 | |

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
| T3 | An unpaired same-uid process drives the browser | The first connection from an unknown client gets **only** `Koan.pair`; the user sees a native prompt whose headline is the **verified executable** (audit token → path, code-signing identity, ancestors), with the client's own label shown as "calls itself". Unanswered prompts time out (60 s); 3 attempts per minute per peer. **Client records are encrypted and MACed with Keychain-bound `os_crypt` storage** (§5.4), so editing plaintext Local State while Koan is not running cannot add a client |
| T4 | A stolen credential is replayed | There is no shared secret in the browser: clients hold an Ed25519 private key and sign a single-use nonce bound to the peer's identity digest (§5.2). Paired identity is **pinned** (path, signature, ancestors) and a change re-prompts (§5.5). Credentials expire (30 days) and are revocable (flushed before returning, §5.4). **Residual**: a same-uid process that reads `~/.koan/clients/<id>/key` and runs from the same chain is the paired client (§3.5), which is why `observe` is off and `drive` is per tab |
| T5 | The agent exfiltrates cookies, storage or credentials | Layered: (1) `Storage.*`, `DOMStorage.*`, `IndexedDB.*`, `CacheStorage.*`, `Network.{getCookies,getAllCookies,setCookie,setCookies,deleteCookies,clearBrowserCookies,clearBrowserCache,loadNetworkResource}` and everything unlisted are **denied** by name (§4.3); `Browser.*` and `SystemInfo.*` do not exist for an untrusted client. (2) **`Network.enable` is required by agent-browser and, upstream, leaks cookies including HttpOnly values and `Authorization` through `requestWillBeSentExtraInfo`/`responseReceivedExtraInfo`** (`network_handler.cc:1113-1135, 4033-4058`; review finding 1): the facade rewrites those events and passes only an allowlist of Network events (§4.5), tested by T-sec-4. (3) `getResponseBody` and `setExtraHTTPHeaders` need capability `network` (not offered in v1). **Honest limit**: the agent can run JS in a page, so it can read what that page's JS can read (`document.cookie` minus HttpOnly, that origin's localStorage, the DOM). It is **not limited to the origin it started on**: it can navigate its tab to any site you are logged into and read that page, which is the shared-login point of decision 15. It cannot read HttpOnly cookies, the password store or the cookie jar |
| T6 | The agent reads passwords / autofill | The `Autofill`, `Security`, `Browser` domains are absent for untrusted clients. Risk: password-manager autofill into a form the agent drives, then JS reads the value. Chrome hides autofilled passwords from script until a user gesture; a CDP-injected click may count as one (UNVERIFIED, test T-sec-1, §15). Mitigation if it does: disable password/autofill UI in agent-owned tabs (`ContentAutofillClient`/`PasswordManagerClient` per WebContents) |
| T7 | The agent reaches privileged or sensitive pages and worlds: `chrome://`, Koan settings, DevTools, `file://`, the Agent-control page, **extension frames injected into ordinary pages and extension content-script worlds** | A strict Koan-only allowlist (§4.4): a frame is reachable only if http(s)/`about:blank`/the unreachable-error URL, non-WebUI, non-privileged, non-guest, non-PDF/MimeHandler, with every ancestor and the embedder passing the same; `chrome-extension://` frames are not attached; execution contexts other than the main world or the agent's own isolated worlds are dropped and their `contextId`s rejected. `MayReadLocalFiles=false` blocks `file://`, `DOM.setFileInputFiles` and `Network.loadNetworkResource`. Network-error pages do not detach the tab (the original draft would have) |
| T8 | The agent raises windows, switches tabs, quits, resizes, loads extensions | `Page.bringToFront`/`Target.activateTarget` → no-op success + log; `Browser.*`, `Extensions.*`, `SystemInfo.*` absent; `Target.createBrowserContext`/`disposeBrowserContext`/`attachToBrowserTarget` denied; window bounds denied |
| T9 | Prompt injection: a page the agent reads tells it to do harm | Cannot be solved in the browser. Reduced by: agent-Space scope, per-tab consent for the user's own tabs, optional per-client **site allow/deny list** (default: deny nothing, but banking/payment categories are a settings suggestion), indicator, pause on user input, audit log, `Fetch`/network interception off by default |
| T10 | Hidden action: the user does not know the agent is active | Persistent indicator (§6.2), banner on a driven visible tab, toast, menu-bar item, agent-Space glow; "Stop all agents" one click away; audit log |
| T11 | Incognito leaks | Excluded by default for every client (§13) |
| T12 | The agent changes the user's data through the page (deletes, buys) | Out of scope for the browser (the same as any extension). Reduced by capability `drive` (take-over of user tabs needs per-tab approval) and the audit log. The agent Space is where the agent acts by default |
| T13 | Downloads / uploads | `Page.setDownloadBehavior` denied; downloads go through the normal download UI and Safe Browsing; uploads need a Koan-mediated, user-approved path (a later `Koan.upload`) |
| T14 | Denial of service: floods, leases, pre-auth bytes, parser bugs | Raw length-prefixed frames, **4 KB cap before authentication**, connection and per-peer caps, 10 s incomplete-frame timeout (§4.1); per-connection rate limits; a lighter lease with a hold-off and a concurrent cap of 4 (§8); tab caps 8 per task, 12 per client, 16 total |
| T15 | Policy bypass | `RemoteDebuggingAllowed=false` stops the gateway and closes sessions; `DeveloperToolsAvailability` is enforced on every attach (C§1a) |
| T16 | Upgrade drift: a Chromium roll changes which domains or methods an untrusted client gets | The protocol-coverage unit test of §4.3 fails on any unclassified CDP method; REBASE rows name the symbols and the behaviours; a Mac test per denied domain (§14, §15) |
| T17 | The agent reads or writes the OS clipboard (`Input.dispatchKeyEvent {commands:["paste"]}` pastes whatever the user last copied, such as passwords or 2FA codes) | `commands` is stripped from every key event (§4.5); no `clipboard` capability in v1; T-sec-10 |
| T18 | A page the agent drives takes the user's attention or focus: `window.open`/`target=_blank`/popups (CDP clicks carry a user gesture), a `<select>` native menu, a file chooser sheet, HTTP auth, client-cert picker, external-protocol launch, print, permission bubbles | Page-opened tabs open in the background in the agent Space (hook #2, §14); `ForbidExternalPopupMenus()` in the lease; file chooser interception forced on; the others become **`userNeeded` approval prompts** that pause the tab and ask the user, never a silent native sheet (§9.4); T-focus-3..7 |
| T19 | Agent browsing leaks into the user's own surfaces: history, omnibox suggestions, Recently Closed, saved tab groups and sync, session restore, session files | Required, with the mechanism per surface in §7.1: agent-visited URLs are not added to history or the omnibox (decision 18, a setting); agent groups never become saved or synced groups (the existing `SavesTabGroups` hooks; agent control needs Spaces on); a restore-side hook blanks and sweeps agent tabs; conditional write-side hooks keep them out of Recently Closed and the session files; T-hist-1, T-sync-1, T-sync-2 |
| T20 | Audit-log tampering or secret leakage through the log | Best-effort integrity (hash-chained records, chain key Keychain-bound), size-capped, URL paths redacted by default, nothing typed or evaluated stored (§6.3). A same-uid attacker can still delete it; the log is evidence for the user, not a security boundary |

### 3.4 Controls the maintainer asked for, mapped

| Control | Where |
|---|---|
| Off by default | pref `koan.agent_control.enabled` (local state) false; when false, no socket, no thread, no listener, leases released, sessions closed (§6.1) |
| Only a unix socket or loopback + token; no unauthenticated port | unix socket only in the browser; relay is a client-side process (§11) |
| User approves each new client, native prompt naming the client, revoke in settings | §5 |
| Visible indicator while connected | §6.2 |
| Incognito excluded by default | §13 |
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

New `//koan/browser/agent/` target. A `Koan_AgentIO` thread (`MessagePumpType::IO`) runs **Koan's own accept loop**
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
  and the audit log.
* **Socket hygiene** (verified, C§2a, C§5): `bind()` leaves a stale file and a 0777-minus-umask mode, so Koan probes
  with `connect()`, `unlink()`s a dead socket, and `chmod`s 0600 inside the 0700 directory. `sun_path` allows 103
  characters, so a long `--user-data-dir` falls back to `/tmp/koan-<uid>/<hash>.sock` after `lstat` checks (owner,
  not a symlink). A discovery file `AgentControl/agent.json` (0600: socket path, pid, version, profile) lets the CLI
  find it; the CLI validates the pid and a `connect()` before trusting it.
* **Threads**: socket I/O on the IO thread; **parsing, scrubbing and serialising of large CDP traffic on a worker
  thread**, with only decisions (attach, dispatch, lease) on the UI thread, because all `DevToolsAgentHost` calls are
  UI-only and the user is working in the same process (review finding 14). Events the policy does not need to inspect
  are forwarded as bytes untouched; evaluate `UsesBinaryProtocol()` on the client to skip content's CBOR-to-JSON
  transcoding (phase-2 measurement).

Lifetime: started when the pref is true and the first window exists; torn down on pref false, policy change
(`kDevToolsRemoteDebuggingAllowed`), revoke-all, and app exit. No upstream hook for the lifetime is expected (a pref
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

### 4.3 Method policy: per-method tables, **unlisted = deny**

Rules: (1) no domain wildcards; a method is allowed only by name; (2) the table is data, in one file, and a unit
test (T-ui-3) **enumerates every method of the 158 protocol JSON and fails if any is not classified** allow, gated,
no-op or deny, so a Chromium roll that adds a method cannot slip through; (3) every denial is an explicit CDP error
(never a silent drop) plus an audit entry; (4) param filters are part of the entry; (5) events pass through an
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
   default button; Esc denies. Unanswered after 60 s: denied; 3 attempts per minute per peer.
3. On Allow the browser stores the **public key** (Ed25519, generated by the CLI), a random 128-bit **client id**,
   the display label, the capabilities, created/last-seen, and the **pinned identity** (§5.5). It stores no shared
   secret, so there is nothing in the browser's data that lets a process authenticate.

### 5.2 Every connection: signature, not shared secret

`Koan.authenticate {clientId}` → server nonce (32 random bytes, single use, 10 s) → client signs
`"koan-agent-v1" ‖ nonce ‖ audit-token identity digest` with its private key → the browser verifies with the stored
public key. Five failures close the connection and block the peer for 60 s. The private key lives with the client, in
`~/.koan/clients/<client-id>/key` (0600, the directory is the random id, never the label, which also closes the
`--name ../..` and silent-overwrite holes of review finding 5). A Keychain-held key with an ACL naming the signed CLI
is the later hardening (UNVERIFIED that Koan.app's ACL can list a second binary, C§5). Credentials expire after 30 days
without use. Connections are bound to a browser launch; **strict mode** (off by default) re-prompts on every
connection, optionally behind Touch ID.

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
re-enable one: a record that fails the MAC is ignored and logged, and `koan.agent_control.enabled` alone does nothing
because every unknown connection still needs a prompt. (The `enabled` flag itself may stay a plain pref.) Revoke
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
with a one-line warning ("Lets programs you approve open and control tabs in Koan, using your logged-in sessions"),
*Ask every time*, the client list, *Stop all agents*, *Open activity log*, and *Install command line tool* (symlinks
`~/.local/bin/koan`) and *Install agent skill* (decision 10). Prefs (local state): `koan.agent_control.enabled`, `.strict`,
`.live_always` (per client, in its record), `.audit_urls`, `.agent_history`, and the **encrypted, MACed client store**
(`.clients`, §5.4), which is not a plain dict.
The settings page itself is never attachable by an agent (T7). Enterprise: `RemoteDebuggingAllowed` greys the row.

### 6.2 Indicator ("agent connected")

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

## 7. Where agents work: the agent Space

Koan Spaces are tab groups inside a window with an active Space whose tabs are shown (`KoanWorkspaceController`,
`NewSpaceGroup`, `ReplaceTabGroupMenuItems`, `WriteSpace`, `koan_spaces.h`). A tab in a non-active Space is simply a
non-active tab: **hidden**, so it needs the lease (§8) to render, and it is hidden from the user's current view,
which is the point.

| Option | What it is | For | Against |
|---|---|---|---|
| **O1 (recommended v1): one Space per agent task, in the user's last-active window** | Created on the first `Koan.openTab` of a task (`Koan.createTask {name}` optional), named "*Client · task*", agent icon, glow, **ephemeral** (not restored; `koan_session_data.cc` drops it), ended with `Koan.endTask` or by the user | Built on what exists; Zen-like (switch Space to watch, swipe); several tasks = several Spaces; hand-over is "Move To" Space, which exists | The user watches by switching Space, so they cannot see the agent and their own tabs side by side; one window's strip carries agent tabs; Space switching animates |
| O2: a dedicated agent window per task | A real second `Browser` | Side-by-side watching; fully separate from the user's window ops (⌘W, tab strip drags) | New windows must not steal focus or switch macOS Spaces (`ShowInactive` has Space-switching branches, B§5); minimized/other-Space windows need the Mac checks in §15; a window per task is heavy; Koan windows are Space-bound already |
| O3: hidden targets (`Target.createTarget {hidden:true}`) | Detached WebContents, no strip entry | No UI footprint, no focus risk, process boosted by upstream (B§5, `hidden_target_manager.cc`) | The user cannot see or take over; no upstream adoption into a window; "watch" impossible |

Recommend O1 for v1 and O2 as an opt-in "Open this Space in its own window" in v2 (a Space can move to a new
window the way tabs do). O3 stays as an internal "headless task" mode if the user asks for no UI at all.

Mechanics (names to confirm in phase 2): create the tab with `TabStripModel` APIs into the Space's group with
`ADD_NONE` and `initially_hidden`, **not** through `NavigateParams`, so neither the active tab nor the window
changes (`kShowWindowInactive` is what `Target.createTarget {background:true}` adds, B§5). `Koan.openTab` accepts
`{url, task?}`; the agent's tabs are tracked by `AgentTabLease`. Which window: the last-active window of the default
profile; with no window, Koan creates one ordered back/inactive (Mac test T-win-2). Limits: `max_tabs` per client,
never Essentials (pinned across Spaces), never another Space's tabs.

**Kept out of the user's other surfaces** (review finding 15, decision 18): §7.1 gives the mechanism per surface. In
short: an agent Space is ephemeral and never enters saved tab groups or tab-group sync, session restore or Recently Closed;
`open_tab_provider.cc` (an existing Koan patch) skips agent-Space tabs so `@tabs` does not list them; agent-visited URLs are
**not added to history or the omnibox by default** (a setting). The Space's copy says plainly that **it shares your logins
and cookies** (decision 15), as ego's own engineering doc says of theirs, instead of implying isolation.

**Hand-over of a user tab** (ego: "your tabs stay untouched unless you explicitly hand one over"): tab context menu
*Hand to agent ▸ [Claude Code]* (visible only while a client with `drive` is connected) and a palette command. Two
modes:

* **Drive in place** (default for the *active* tab): the tab stays where it is, the lease and banner attach. The user
  keeps seeing it; if they switch away it keeps running.
* **Move into the agent Space**: the tab leaves its Space (origin Space and index remembered in the lease) and
  *Take back* returns it. Refused for Essentials and pinned tabs (drive in place instead). If the user closes it, the
  agent gets `Target.detachedFromTarget`.

### 7.1 Keeping agent groups out of the user's other surfaces (open item O2, designed)

An agent Space is a tab group, so it meets every place Chromium persists or surfaces groups. The audit of Koan's existing hooks
and pristine 158 gives one mechanism per surface, and shows that **most of it already exists**:

| Surface | Mechanism | New upstream patch? |
|---|---|---|
| **Saved tab groups and tab-group sync** | Koan already has `koan::SavesTabGroups(context)` = `!IsWorkspacesEnabled() \|\| context->IsOffTheRecord()` (`koan_tab_hooks.cc:79`), wired into the three places a group becomes a saved group (`saved_tab_group_model_listener.cc` `OnTabGroupAdded`, `session_restore.cc` `ProcessSavedGroup`, `browser_live_tab_context.cc` `AddRestoredTab`'s `SaveGroup`; REBASE row "Spaces aren't saved groups"). With Spaces on, **no group** becomes a saved group, so an agent Space (a Space) is never saved or synced. **Precondition: agent control requires Spaces to be enabled**; with Spaces off (the flag) `Koan.openTab` returns `Koan.unavailable {reason:"spaces-disabled"}` rather than let an agent group be saved or synced (T-sync-2) | none |
| **Session restore** (restart, crash, reopen window) | Persist a marker `koan.agent_tab` in the tab's session extra data with the existing `SetTabSessionData` (`koan_session_data.h`; written by `BuildCommandsForTab`, carried by `GetExtraDataForTab`). A `TabRestoreHook` for that key replaces the restored tab's `entries` with one `about:blank` entry, and an `AgentSpaceSweeper` closes the flagged tabs and the emptied agent groups when the restore completes (the point at which `DropStartupSpaceAfterRestore` already acts). After a restart no agent page loads or shows. This also covers **reopening a closed window**, where `TabRestoreService` returns the window's tabs through `AddRestoredTab` with the same extra data | none |
| **Recently Closed** (reopen closed tab/group) | The two funnels are `BrowserTabStripModelDelegate::CreateHistoricalTab` and `CreateHistoricalGroup` (`browser_tab_strip_model_delegate.cc:227,247`, called from `tab_strip_model.cc:953`). A one-line early return for agent-owned contents/groups keeps them out of the list. Whole-window entries (`BrowserClosing`) are covered by the restore-side hook above | **conditional, §14 #5**: needed to keep agent tabs out of the *list*; without it they appear there but reopen blank and are swept |
| **Session files on disk** | The session files can contain agent-visited URLs until they are next rewritten, even though restore drops the pages. The write side is `SessionServiceBase::BuildCommandsForTab` and the incremental `UpdateTabNavigation`; a tab-level `koan::PersistsInSession(contents)` early return there removes them | **conditional, §14 #6**: decided by T-sync-1, which greps the profile's `Sessions/` files for agent-visited URLs |
| **History and omnibox** | `HistoryTabHelper::DidFinishNavigation` decides on `navigation_handle->ShouldUpdateHistory()` (`history_tab_helper.cc:532`); a hook `koan::SuppressesHistory(web_contents())` ahead of it keeps agent tabs out of History and therefore out of history-based omnibox suggestions. The setting `koan.agent_control.agent_history` turns it off. There is no per-tab knob upstream (searched `HistoryTabHelper`) | **§14 #3** (certain, no longer conditional) |
| **`@tabs` / open-tab suggestions** | the existing `open_tab_provider.cc` Koan patch gains one exclusion | none (existing patch) |

So the new upstream touches for O2 are #3 (history, certain) and #5, #6 (conditional, write-side). Everything restore-side is
`//koan` plus hooks that exist.

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
| Concurrent leased tabs | **4** (a fifth waits for a release; commands queue rather than fail) | a MacBook Air has 8-16 GB and 4-8 cores |
| Open tabs per task / per client / total | **8 / 12 / 16** | ego's page budget is 8 (§19) |
| `live: always` (opt-in, per client) | off | hold the lease for as long as a client is attached, for pages that must run between commands |

Consequence, stated plainly: between commands a default-mode agent tab is a normal hidden tab as far as rendering goes
(timers throttled to 1 s, no rAF), **but it is not frozen, not discarded and keeps its renderer at foreground priority**
for the whole attachment, so the next command answers at once. "Animations and timers keep running" holds while the agent acts, observes or waits, and always under
`live: always`; the first frame after re-acquire must be fast (T-keep-10: ≤ 100 ms), and a **never-shown** background
tab (`did_first_set_visible_` false) must produce its first paint under the lease (T-keep-11). A page that needs to
progress while the agent is thinking must say so (`keepAlive`).

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

## 14. Hook patches (for docs/REBASE.md)

After the review (finding 12) the feature itself needs **two upstream touches (#2, #3) and two conditional ones (#5, #6)**;
everything else is `//koan` plus public setters Koan already calls. (Rows 5 and 6 are conditional write-side hooks for §7.1; the restore side needs none.)

| # | File | Change | Needed for | Status |
|---|---|---|---|---|
| 1 | `chrome/browser/devtools/remote_debugging_server.cc` | the default of `g_enable_default_user_data_dir_check_for_chromium_branding_for_testing` becomes `true` | refuse `--remote-debugging-port`/`-pipe` on the default profile (§3.2) | **its own PR, #186** |
| 2 | `chrome/browser/ui/browser_web_contents_delegate/browser_web_contents_delegate.cc` (the existing Koan patch; `AddNewContents`) | for a tab owned by an agent lease, a page-opened tab (`window.open`, `target=_blank`, a popup) is opened **in the background, in the same agent Space, never activated, never raising a window** (§9.4) | page-initiated focus (review finding 9) | phase 2c, a few lines in a patch that exists |
| 3 | `chrome/browser/history/history_tab_helper.cc` (the `ShouldUpdateHistory()` decision in `DidFinishNavigation`, line 532): `if (koan::SuppressesHistory(web_contents())) return;` ahead of it | skip history, and so history-based omnibox suggestions, for agent-owned tabs | decision 18 (a setting) | phase 2c. **Certain**: §7.1 found no per-tab knob upstream |
| 4 | `chrome/browser/BUILD.gn` / `chrome/browser/ui/BUILD.gn` | deps on `//koan/browser/agent` | build only | phase 2b |
| 5 | **conditional** `chrome/browser/ui/browser_tab_strip_model_delegate.cc` (`CreateHistoricalTab`, `CreateHistoricalGroup`): early return for agent-owned contents and groups | keep agent tabs out of Recently Closed | §7.1; only the *list* needs it, restore already blanks and sweeps | phase 2c; decided by T-sync-1 |
| 6 | **conditional** `chrome/browser/sessions/session_service_base.cc` (`BuildCommandsForTab`, `UpdateTabNavigation`; the existing Koan patch): `koan::PersistsInSession(contents)` early return | keep agent-visited URLs out of the session files | §7.1; the pages are already dropped at restore | phase 2c; decided by the `Sessions/` grep in T-sync-1 |

**Dropped from the first draft** (all replaced by `PageLiveStateDecorator::SetIsPinnedTab` and
`SetGlicActuationState`, §8): the freezer hook, the discard-eligibility reason, the new
`CannotFreezeReason::kAgentControlled`, the `//chrome/browser/glic` dependency, the content patch for
`OnBoostForBackgroundExecution`, and the `ActivateContents` skip (the facade's no-op covers CDP, and Koan code never
activates agent tabs).

No-patch pieces (all in `//koan`): the settings row and handler, the tab context menu "Hand to agent" (inside the
existing `AddKoanTabMenuItems`), the open-tab omnibox provider already has a Koan patch (`open_tab_provider.cc`) and
gets a one-line exclusion of agent-Space tabs. Everything else uses public `content::`, `performance_manager::` and
`chrome::` APIs: `DevToolsAgentHost`, `DevToolsAgentHostClient`, `WebContents::IncrementCapturerCount`,
`WebContents::ForbidExternalPopupMenus`, `WebContentsObserver::DidGetUserInteraction`, `WebContents::IgnoreInputEvents`,
`PageLiveStateDecorator`, `TabStripModel`, `net::UnixDomainServerSocket`.

REBASE rows must reference **symbols**, not lines (rule 3): `SetIsPinnedTab`, `SetGlicActuationState`,
`kGlicActuationPriorityVoter`, `IsDomainAvailableToUntrustedClient` (`devtools_session.h:245-266`,
`chrome_devtools_session.cc:42-48`), `DevToolsAgentHostClient`'s flag defaults, and the protocol-coverage test of §4.3
(a roll that adds a CDP method fails it). Behaviours to name in the rows: freezing/discard/priority stay exempt for a
leased tab (T-keep-5, 6, 8), a `<select>` click opens no native menu (T-focus-4).

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
| T-sec-12 | forged client store: while Koan is not running, add a client record and set `enabled=true` in Local State | the record fails its MAC and is ignored (logged); a connection still gets the pairing prompt |
| T-sec-13 | pre-auth abuse: a 1 GB length prefix, 100 half-open connections, malformed JSON, a second `Koan.authenticate`, slow-loris | closed within the limits, browser memory flat, UI thread unaffected |
| T-sec-14 | `chrome.debugger` interplay: an extension attaches to an agent tab, then detaches | `Koan.contested` event; a second `Emulation` writer refused; the lease survives |
| T-sec-15 | the protocol-coverage unit test (Linux-runnable): every CDP method in the 158 protocol JSON is classified | fails on any unclassified method |
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
  indicator and settings. Mac: T-sec-*.
* **2c, keep-alive:** the lease (the whole-attachment part and the capturer hold-off), agent Space and `Koan.openTab`, the
  history/restore/Recently-Closed hooks of §7.1, `koan` (C++ in GN), the skill file, page-initiated focus hook and
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
| 15 | Say plainly that the agent Space shares the profile's logins | §7, §19.4 |
| 16 | Client records Keychain-bound (`os_crypt`) and keypair auth | §5.2, §5.4 |
| 17 | `observe` defaults off | §5.3 |
| 18 | Agent-visited URLs are not added to history or the omnibox by default (a setting) | §7, §14 #3 |
| 19 | Raw length-prefixed framing on the unix socket; the relay translates | §2, §4.1 |
| 20 | The ego(lite) run is mandatory for the "beats ego" headline, with a `< 1.0×` bar | §20.7 |
| 21 | In-browser `snapshot`/`act`/`run`, diffs and event waits move into phase 3; no "faster" claim before phase-3 numbers | §16, §20 |

### 17.2 Still open (smaller; proposals in the sections named)

| # | Question | Proposal |
|---|---|---|
| O1 | The power numbers for a MacBook Air: hold-off 3 s, 4 concurrent leases, 8/12/16 tabs per task/client/total | §8 proposes; confirm or change after T-keep-9, 10, 12 |
| O2 | *(resolved in design, §7.1)* The history/omnibox/saved-group/session mechanism | saved groups: already excluded by `SavesTabGroups` when Spaces are on (agent control requires Spaces on); session restore: marker + `TabRestoreHook` + sweeper (no patch); history: §14 #3; Recently Closed and session files: conditional §14 #5, #6, decided by T-sync-1 |
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
  its LLM turns. Gateway limits: per-client max tabs, a CPU-friendly cap on simultaneous leases (default 8).
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
| 15 agent groups vs sync/history/session | §7.1, §14 #3/#5/#6, T19; T-hist-1, T-sync-1, T-sync-2 |
| 16 the plan contradicts itself on what ships | §16 (speed is phase 3), §10, §20 intro |
| 17 DSL vs JavaScript | §20.2 `eval` step; CLI-side runner in phase 3 |
| 18 strawman lever table | §20 table and intro |
| 19 benchmark gaps | §20.7 method, ego mandatory, `< 1.0×`, interleaving, CIs, headed, live slice |
| 20 agent-browser facade details | §4.2, §11.1 |
| Corrections: Accessibility reachable; Glic dependency moot | §4.3, §8 |
