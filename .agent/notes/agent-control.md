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

1. **Koan owns the endpoint; it does not use `--remote-debugging-port`.** A gateway in the browser process
   (`net::HttpServer` on a `net::UnixDomainServerSocket`, WebSocket on top) speaks a **filtered CDP**. Each tab the
   agent may touch gets an in-process **untrusted** `DevToolsAgentHostClient` (the `chrome.debugger` pattern), so
   the Browser, SystemInfo and Extensions domains, the browser target and local-file access do not exist for the
   agent, rather than being blocked by a list that could have a hole (C§2c, C§6).
2. **Agents work in their own Koan Space** (a tab group; Koan has Spaces), one per task. The user's tabs are
   invisible to the agent unless the user hands one over or approves a take-over (§7, §9). This is ego(lite)'s
   model, built on what Koan already has.
3. **A per-tab keep-alive lease** (visible `IncrementCapturerCount` + the Glic "actuating on background tab"
   priority vote + a 3-line freezer hook) keeps *only agent tabs* rendering, with no global flag touched (§8,
   A§10).
4. **`agent-browser` works through a tiny relay/plugin, not a weakened endpoint** (§11). It cannot dial a unix
   socket; a per-run loopback relay with an unguessable path hides that. The `koan` CLI stays small: pair, status,
   take-over/hand-back, log, revoke, the relay, and thin helpers.
5. **Security is a feature, not a flag** (§3): off by default, approval per client, capabilities per client,
   visible indicator, incognito excluded, no cookie/storage CDP, an audit log, and any user input in a driven tab
   pauses the agent. We also close a gap Chrome 136 closed and Koan currently has open (§3.2).

Open decisions for the maintainer are collected in §17.

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
   |  runs shell commands                     agent-browser (Rust, raw CDP)
   v                                             |  --provider koan  or  --cdp ws://127.0.0.1:P/cdp/<secret>
 koan CLI (signed, in Koan.app/Contents/Resources/bin) --- relay (per run, loopback, secret path, exits with child)
   |  WebSocket over unix socket  ~/Library/Application Support/Koan/AgentControl/agent.sock (dir 0700, sock 0600)
   v
 KoanAgentGateway (browser process; IO thread for sockets, hop to UI per frame)
   |- peer check: getpeereid uid + LOCAL_PEERPID  -> exe path, signature, parent chain
   |- KoanAgentSession: Koan.pair / Koan.authenticate (HMAC challenge), capabilities, audit log, rate limits
   |- KoanAgentTargetRegistry: which tabs this client may see/drive (agent Space + handed-over); rejects incognito/guest/WebUI/extension/DevTools
   |- CDP facade: Target.* emulated, Browser.getVersion, method policy table (allow / capability / deny / no-op)
   |- per tab: KoanAgentClient (IsTrusted=false, no local files, MayAttachToURL http(s) only)
   |        -> DevToolsAgentHost::GetOrCreateFor(contents)->AttachClient
   |- AgentTabLease (WebContentsUserData): keep-alive, take-over state, input observer
   `- UI: indicator, pairing/take-over prompts, toasts, Settings > Agent control
```

Rationale for rejecting the stock transports is in C§1b, C§2 and the decisions table (A/B/C rows 18-21): the
`DevToolsHttpHandler` is a process-wide slot that `chrome://inspect` also uses, its WebSocket client is trusted
(file read and write, browser target), `/json` leaks target ids unauthenticated, and `--remote-debugging-pipe` needs
file descriptors inherited at launch (unusable for a running browser).

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
Dev and test flows (`scripts/e2e/run.sh`, `scripts/mac/*`) already pass a non-default `--user-data-dir`, so they
are unaffected. Agent control does not depend on it: it uses no switch. Keep `chrome://inspect#remote-debugging`
as upstream has it (modal approval, off by default; "lock as is"), but our settings page says which of the two
doors is which.

How agent control compares with Chrome's approval mode:

| | `chrome://inspect#remote-debugging` | Koan agent control |
|---|---|---|
| Transport | loopback TCP, any local uid can connect | unix socket, 0700 dir, uid + pid check; optional loopback relay with a secret path only for clients that cannot do sockets |
| Auth | none; a modal per connection | pairing approval naming the executable, long-lived token proved by HMAC, revocable, optional ask-every-time |
| Privilege | **trusted browser-target session** (every domain, `Extensions.loadUnpacked`, `Browser.close`, cookies of all profiles) | untrusted page-level clients, deny-by-default method table, no browser target |
| Scope | everything incl. incognito | agent Space + handed tabs; incognito excluded |
| Visibility | infobar | indicator, banner, menu, toasts, audit log |

### 3.3 Threats and controls

| # | Threat | Control |
|---|---|---|
| T1 | A web page reaches the endpoint (loopback port, DNS rebinding, `Origin`-less WebSocket) | The browser listens on **no TCP port**. The unix socket cannot be dialled by a browser. The optional relay (§11) binds 127.0.0.1 on an ephemeral port, serves only `/cdp/<128-bit secret>`, rejects any request with an `Origin` header, validates `Host == 127.0.0.1:<port>` (the stock handler's own rebinding guard is `devtools_http_handler.cc:129-137,478-484`), and dies with its child |
| T2 | Another local user connects | 0700 directory + 0600 socket + `getpeereid` uid check; relay secret path |
| T3 | An unpaired same-uid process drives the browser | The first connection from an unknown client gets **only** `Koan.pair`; the user sees a native prompt naming the executable path, signing identity and ancestor chain (`LOCAL_PEERPID` + `proc_pidpath`; the CLI-claimed name is shown as "claims to be"). Unanswered prompts time out (60 s) and rate-limit (3 / minute / peer) |
| T4 | A stolen credential is replayed | The token never crosses a socket (HMAC over a server nonce). Credentials expire (30 days) and are revocable. **Residual**: a same-uid process that reads `~/.koan/clients/*.token` can run `koan` as the paired client: that is the same trust as "can run commands as the user" (§3.5) and why the indicator, audit log and strict mode exist |
| T5 | The agent exfiltrates the cookie jar / storage | The CDP domains `Storage.*`, `Network.getAllCookies`/`getCookies`/`setCookie(s)`, `DOMStorage.*`, `IndexedDB.*`, `CacheStorage.*` are **denied** (C§3c); `Browser.*` and `SystemInfo.*` do not exist for an untrusted client. `MayAttachToURL` limits what any cookie output could show to http(s). A per-client capability `cookies` (off) can later enable a Koan method returning cookies **for the tab's own site only**. **Honest limit**: the agent can run JS in a page, so it can read what that page's JS can read (`document.cookie` minus HttpOnly, that origin's localStorage). It cannot read HttpOnly cookies, other origins, the password store or the jar |
| T6 | The agent reads passwords / autofill | The `Autofill`, `Security`, `Browser` domains are absent for untrusted clients. Risk: password-manager autofill into a form the agent drives, then JS reads the value. Chrome hides autofilled passwords from script until a user gesture; a CDP-injected click may count as one (UNVERIFIED, test T-sec-1, §15). Mitigation if it does: disable password/autofill UI in agent-owned tabs (`ContentAutofillClient`/`PasswordManagerClient` per WebContents) |
| T7 | The agent reaches privileged or sensitive pages: `chrome://`, Koan settings, extensions, DevTools, `file://`, the Agent-control settings page itself | The registry hides them (B§9) and `MayAttachToURL` returns false for non-http(s)/about:blank, **including on navigation** (`render_frame_devtools_agent_host.cc:822-833` force-detaches the session on a forbidden URL). `MayReadLocalFiles=false` blocks `file://`, `DOM.setFileInputFiles` and `Network.loadNetworkResource` of files |
| T8 | The agent raises windows, switches tabs, quits, resizes, loads extensions | `Page.bringToFront`/`Target.activateTarget` → no-op success + log; `Browser.*`, `Extensions.*`, `SystemInfo.*` absent; `Target.createBrowserContext`/`disposeBrowserContext`/`attachToBrowserTarget` denied; window bounds denied |
| T9 | Prompt injection: a page the agent reads tells it to do harm | Cannot be solved in the browser. Reduced by: agent-Space scope, per-tab consent for the user's own tabs, optional per-client **site allow/deny list** (default: deny nothing, but banking/payment categories are a settings suggestion), indicator, pause on user input, audit log, `Fetch`/network interception off by default |
| T10 | Hidden action: the user does not know the agent is active | Persistent indicator (§6.2), banner on a driven visible tab, toast, menu-bar item, agent-Space glow; "Stop all agents" one click away; audit log |
| T11 | Incognito leaks | Excluded by default for every client (§13) |
| T12 | The agent changes the user's data through the page (deletes, buys) | Out of scope for the browser (the same as any extension). Reduced by capability `drive` (take-over of user tabs needs per-tab approval) and the audit log. The agent Space is where the agent acts by default |
| T13 | Downloads / uploads | `Page.setDownloadBehavior` denied; downloads go through the normal download UI and Safe Browsing; uploads need a Koan-mediated, user-approved path (a later `Koan.upload`) |
| T14 | Denial of service: the agent holds leases / floods | Rate limits per connection; lease idle timeout (§8); max tabs per client (default 20) |
| T15 | Policy bypass | `RemoteDebuggingAllowed=false` stops the gateway and closes sessions; `DeveloperToolsAvailability` is enforced on every attach (C§1a) |
| T16 | Upgrade drift: a Chromium roll changes which domains an untrusted client gets | REBASE checklist row + a Mac test per denied domain (§14, §15) |

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

Any code the user runs as themselves is trusted *as the user* in macOS's model. A paired client's token on disk
is readable by any same-uid process, and anything that can run the signed `koan` binary can speak as a paired
client. We therefore do not claim the token defeats malware that already runs as the user. We claim: it cannot
start silently (first use needs a prompt that names it), it cannot be invisible (indicator + log), it cannot reach
the cookie jar, the browser, the extensions or incognito, and it is revocable and expiring. Optional hardening,
each a later decision: a Keychain item whose ACL is the signed CLI (UNVERIFIED that Koan.app's ACL can list a second
binary, C§5); checking the peer's code signature (`SecCodeCopyGuestWithAttributes` by pid) so only the signed
`koan` binary or an explicitly approved executable may use a credential; Touch ID (LocalAuthentication) for
`ask every time` strict mode.

## 4. Transport and protocol

### 4.1 Gateway

New `//koan/browser/agent/` target. A `Koan_AgentIO` thread (`MessagePumpType::IO`, as `devtools_http_handler.cc:957-959`)
owns `net::HttpServer(std::make_unique<net::UnixDomainServerSocket>(auth_cb, /*use_abstract_namespace=*/false), delegate)`;
`auth_cb` accepts only `credentials.user_id == geteuid()`. `OnWebSocketRequest` accepts only `/koan/v1`; every HTTP
request gets 404 (no `/json`). 256 MB send / 100 MB receive buffers (C§2a) so screenshots and PDFs do not stall.
Frames are hopped to the UI thread (`AttachClient` and dispatch are UI-only, C§2c). Details Chromium does not do for
us (verified, C§2a, C§5): `bind()` leaves a stale file and a 0777 mode, so Koan probes with `connect()`, `unlink()`s
a dead socket, `chmod`s 0600 inside the 0700 directory; `sun_path` allows 103 characters, so a long
`--user-data-dir` falls back to `/tmp/koan-<uid>/<hash>.sock` after `lstat` checks (owner, not a symlink). A
discovery file `AgentControl/agent.json` (0600: socket path, pid, version, profile) lets the CLI find it; the CLI
validates the pid and a `connect()` before trusting the file.

Lifetime: started when the pref is true and the first window exists, torn down on pref false, policy change
(`kDevToolsRemoteDebuggingAllowed`), and app exit. No upstream hook is expected for the lifetime (a pref observer
and the existing per-window controller start it); the exact exit hook is a phase-2 detail.

### 4.2 Wire protocol

WebSocket text frames, JSON, CDP-shaped: `{id, method, params, sessionId}` / `{id, result|error}` / `{method, params,
sessionId}`. Two namespaces on one connection:

* **`Koan.*`**, handled in the gateway: `pair`, `authenticate`, `status`, `listTabs`, `openTab`, `closeTab`,
  `createTask`/`endTask`, `read` (snapshot/screenshot of a non-driven user tab), `takeOver`, `release`, `resume`,
  `keepAlive`, `watch` (event subscription), `snapshot` and `act` (§10, optional).
* **Standard CDP**, so unmodified CDP clients work: a **browser-level facade** answers `Target.setDiscoverTargets`,
  `getTargets`, `getTargetInfo`, `attachToTarget {flatten:true}`, `setAutoAttach`, `createTarget`, `closeTarget`,
  `Browser.getVersion`, emits the matching events, and forwards session-scoped commands through the method policy
  table to the tab's untrusted client. Target ids in the facade are the real ones for tabs inside the client's
  scope; the facade lists **only** tabs in scope. This is what lets `agent-browser`, which attaches to *every*
  page target on connect (D§3), see only the agent's Space.

### 4.3 Method policy (summary; full table in C§3c and B)

| Class | Methods |
|---|---|
| allow | `Page.{enable,navigate,reload,getFrameTree,captureScreenshot,printToPDF,addScriptToEvaluateOnNewDocument,handleJavaScriptDialog,startScreencast,stopScreencast,screencastFrameAck,createIsolatedWorld}`, `Runtime.*`, `DOM.*` (except `setFileInputFiles`), `Accessibility.*` (UNVERIFIED for untrusted clients, test T-cdp-3), `Input.*`, `Emulation.*` (device metrics, UA, media, locale, timezone, geolocation override), `Log`, `Inspector`, `IO`, `Network.{enable,disable,setExtraHTTPHeaders,emulateNetworkConditions,getResponseBody}` for the tab's own requests |
| capability-gated | `Fetch.*` and `Network.setRequestInterception`: capability `network` (off). `Network.getCookies`-style output: capability `cookies` (off, §5.3) |
| no-op success + log | `Page.bringToFront`, `Target.activateTarget` (agent-browser's `tab switch` calls them and ignores errors, D§4; a no-op keeps other clients working; agent tabs are never raised) |
| deny with a CDP error naming the reason | `Browser.*` except `getVersion`, `Storage.*`, `DOMStorage.*`, `IndexedDB.*`, `CacheStorage.*`, `Network.{getAllCookies,getCookies,setCookie,setCookies,clearBrowserCookies}`, `Page.setDownloadBehavior`, `Page.setWebLifecycleState`, `Page.setBypassCSP`, `Emulation.setVirtualTimePolicy`, `Emulation.setFocusEmulationEnabled` (the lease does it better), `Tracing.*`, `Target.{createBrowserContext,disposeBrowserContext,attachToBrowserTarget,setRemoteLocations,exposeDevToolsProtocol}`, `Extensions.*`, `SystemInfo.*` |

Every denial is an explicit CDP error (never a silent drop) and an audit entry. Screenshots of the **user's own tab**
use a plain viewport capture only: `clip`/`captureBeyondViewport`/emulation resize the real NSView and flicker the
page (B§1); `fromSurface:false` is trusted-only and is refused.

## 5. Pairing, approval, tokens, capabilities

### 5.1 First contact

1. `koan pair --name "Claude Code"` dials the socket. The gateway runs the peer check and accepts only
   `Koan.pair {clientName, requested capabilities}` and `Koan.authenticate`.
2. Koan shows a **native modal** (a `ui::DialogModel`, cloned from `DevToolsConnectionDialog`, C§4) in the most
   recent window and requests user attention (Dock bounce + a macOS notification if Koan is not frontmost):
   "**Claude Code** wants to control tabs in Koan." Below: the verified facts: executable path and ancestors
   (`koan ← node ← claude`), signing identity or "unsigned", and the CLI-claimed name marked "claims to be". Switches
   for the requested capabilities (§5.3). Buttons: *Allow*, *Deny*. No default button; Esc denies.
3. On Allow the browser mints 32 random bytes (`base::RandBytes`), stores **SHA-256(token)**, name, capabilities,
   created/last-seen in the local-state dict `koan.agent_control.clients`, and returns the token once. The CLI writes
   it to `~/.koan/clients/<name>.token` (0600). The browser never stores the token itself.

### 5.2 Every connection

`Koan.authenticate {clientId}` → server nonce → `HMAC-SHA256(token, nonce)` (constant-time compare,
`crypto::SecureMemEqual`). The token never crosses the socket, which matters for the loopback relay. Five failures
close the connection and block the peer for 60 s. Connections are bound to a browser launch: credentials persist
across launches, **sessions do not** (a new launch means a new authenticate). **Strict mode** (setting, off): every
connection re-prompts ("Claude Code is connecting again"), optionally behind Touch ID.

### 5.3 Capabilities (per client, editable in settings)

| Capability | Default at pairing | Meaning |
|---|---|---|
| `space` | on | open/close tabs in its own agent Space and drive them |
| `observe` | on | list the user's tabs: title, URL (origin only when the user hides paths), Space, which is active |
| `read` | asks on first use | snapshot/screenshot of the user's *current* tab without driving it |
| `drive` | asks on first use, **per tab** | take over a user tab (§9) |
| `network` | off | `Fetch`/request interception, extra headers beyond the tab's own |
| `cookies` | off | Koan method returning cookies for the tab's own site only; never `getAllCookies` |
| `incognito` | off | see §13 |
| `keep-awake` | off | `stay_awake` (display sleep prevention) for unattended runs |

"Asks on first use" shows a bubble anchored to the URL pill of the tab concerned: *Allow once*, *Allow for this
client*, *Deny*. Missing capability → CDP error `Koan.notPermitted {capability}`.

### 5.4 Revocation

Settings > Agent control lists clients (name, executable, paired date, last used, capabilities). *Revoke* deletes the
hash, closes live connections, releases their leases and un-hands their tabs; *Revoke all / Stop all agents* does it
for everyone and is also a menu-bar item. Credentials expire after 30 days of non-use. Turning the feature off keeps
the list but closes everything.

## 6. Settings, indicator, audit log

### 6.1 Settings

A new "Agent control" group in `chrome://koan-settings` (a `Row` with `local_state = true`, C§4, plus a small
`koan_settings_handler.cc` addition for the client list, which is not expressible as a `Row`): the master switch
with a one-line warning ("Lets programs you approve open and control tabs in Koan, using your logged-in sessions"),
*Ask every time*, the client list, *Stop all agents*, *Open activity log*, and *Install command line tool* (symlinks
`~/.local/bin/koan`). Prefs: `koan.agent_control.enabled`, `.strict`, `.clients` (dict), `.max_tabs` (all local state).
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
badge show (open decision 6).

### 6.3 Audit log

JSON lines in `<user-data-dir>/AgentControl/audit/YYYY-MM-DD.jsonl` (0600, 30 days): time, client, connection id,
tab (id + origin), method, a summary (navigate: URL without query/fragment; click/type: ref or coordinates and text
**length**, never the text; evaluate/callFunctionOn: SHA-256 and length of the source; screenshots: size), outcome
(`ok|denied|paused|error`), duration. Pairing, approval, revocation, take-over, pause and release are logged as
events. Viewer: settings page and `koan log --follow`. The log never contains page content, typed text or cookie
values.

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

**Hand-over of a user tab** (ego: "your tabs stay untouched unless you explicitly hand one over"): tab context menu
*Hand to agent ▸ [Claude Code]* (visible only while a client with `drive` is connected) and a palette command. Two
modes:

* **Drive in place** (default for the *active* tab): the tab stays where it is, the lease and banner attach. The user
  keeps seeing it; if they switch away it keeps running.
* **Move into the agent Space**: the tab leaves its Space (origin Space and index remembered in the lease) and
  *Take back* returns it. Refused for Essentials and pinned tabs (drive in place instead). If the user closes it, the
  agent gets `Target.detachedFromTarget`.

## 8. Per-tab keep-alive (the lease)

`AgentTabLease : content::WebContentsUserData`, created when a client attaches to a tab, released on detach, tab
close, revoke, or **idle timeout (default 10 min with no command; `Koan.keepAlive {tab, seconds}` extends)**. It holds:

| Part | Call | Why (audit) |
|---|---|---|
| Render + scheduler | `contents->IncrementCapturerCount(gfx::Size(), /*stay_hidden=*/false, /*stay_awake=*/false, /*is_activity=*/false)`, keep the `ScopedClosureRunner` | RWH stays shown, own compositor on a detached tab, rAF/CSS animations run, Blink scheduler treats the page as visible (no 1 s / 1-per-minute timer throttling), explicit freeze cleared (A§7, B§4). Visible rather than hidden-but-painting because it unfreezes and compositor animations are verified only on the visible path; the cost is `document.visibilityState == "visible"` (per-client option `stay_hidden`) |
| Process priority | `glic::GlicPerfTraitsTracker::GetInstance()->NotifyActuationStateChanged(contents, glic::GlicActuationState::kActuatingOnBackgroundTab)`, reset to `kNone` on release | Upstream's own `ActorTask` recipe (A§10 step 2). A captured background tab otherwise votes `kBestEffort`, an App-Nap-style suppression of the renderer on macOS (A§5). Fallback if `//chrome/browser/glic` is unwanted: `RenderProcessHostImpl::OnBoostForBackgroundExecutionAdded()` via a content patch, as `hidden_target_manager.cc` does |
| Freezing | hook in `components/performance_manager/freezing/freezer.cc` `MaybeFreezePageNode`: `if (contents->IsBeingCaptured()) return;` | `Freezer` skips only a really VISIBLE page; Koan turns infinite-tabs freezing **on** (`koan_feature_overrides.cc:52`), so without this an idle agent tab freezes. Side effect: transient captures (thumbnails) also skip one freeze attempt; PM's flag then disagrees for a moment (UNVERIFIED benign, T-keep-5). Precise alternative: a `CannotFreezeReason::kAgentControlled` modelled on Koan's `kPinnedTab` patches (≈5 files) |
| Discarding | nothing while a page-type CDP session is attached (`kDevToolsOpen`, `discard_eligibility_policy.cc:419`) | If the gateway detaches between commands, add one reason beside it (decided after T-keep-6) |
| `stay_awake` | off | A power assertion only, never a keep-alive; opt-in capability `keep-awake` for unattended runs (A§7). System sleep and lid-close are **not** prevented |

Only agent tabs hold a lease; no global flag changes (A§9 lists the ones never to flip). The lease is released while
paused-and-visible is irrelevant (the tab is the user's active tab and visible). Expected result for a non-active
agent tab in an unfocused or covered window: rAF yes, CSS animations yes, timers unthrottled, `captureScreenshot` yes
(plus its own transient capturer), process foreground, not frozen, not discarded. Minimized window, other macOS Space
and Cmd+H: by code a detached tab owns its compositor so it should keep running (A§2, A§3) but this is UNVERIFIED
until T-keep-1..4. The `kNotifyCompositorOf*OnMacOs` features must stay off, or an agent tab in the *active slot* of
a hidden window needs the window-level video-capture lock, which capture takes automatically (A§3).

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
* **Pause on user input.** An `InputEventObserver` on each driven tab's `RenderWidgetHost` (re-registered on
  `RenderFrameHostChanged`) pauses when an event arrives **without** `WebInputEvent::kFromDebugger` (CDP-injected
  input always carries it, B§7; Chrome's own `extension_actor_tracker.cc:129-138` makes the same distinction) and is a
  mouse-down, key-down, wheel or touch-start. **Switching to the tab is not input**; typing or clicking in the page
  is. UI-level activity (tab strip, omnibox) never reaches the widget, so it neither pauses nor needs to. Text from
  `Input.insertText` look-alikes (IME) is preceded by key events, which are seen.
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

## 10. Snapshots built in the engine (ego's idea) and the tool surface

ego(lite) claims snapshots built *inside* the engine rather than by injected scripts, reaching cross-origin
iframes, shadow DOM and embedded widgets, plus a multi-step script per call. Options for Koan, in order of cost:

| Option | What | Pros | Cons |
|---|---|---|---|
| **S1 (v1, free): CDP from the client** | agent-browser's own `snapshot` = `Accessibility.getFullAXTree` + `DOM`, flat sessions per frame, refs by `backendDOMNodeId` (D§4) | Works day one through the facade; nothing to build; the same tool agents already know | Needs the `Accessibility` domain to be reachable by an untrusted client (T-cdp-3) and the facade to pass child (OOPIF) target attach events; one CDP round trip per frame |
| **S2: `Koan.snapshot` / `Koan.act` in the browser process** | `WebContents::RequestAXTreeSnapshot(callback, ui::AXMode, max_nodes, timeout, ...)` (public, `web_contents.h:648`) builds a tree across out-of-process iframes and shadow DOM with no script injected; refs `@N` are bound to (frame, AX node id); `Koan.act {ref, click|fill|select|scroll|hover}` resolves bounds in the browser, scrolls into view, dispatches input | Closest to ego; one call returns a compact, token-cheap tree; no page-visible injection; no CDP domain exposure; the engine can drop sensitive nodes (password fields' values) | New code to maintain; we must not enable full platform accessibility in the tab (the snapshot AX mode must be a one-off, UNVERIFIED); Chrome's own actor does the equivalent with annotated page content (`chrome/browser/actor/`, `ToolRequest`s, not audited here: audit before building) |
| S3: scripted batches | `koan run script.js` runs agent JS **in the CLI** (embedded QuickJS), exposing `tab.snapshot()/click()/fill()/wait()/capture()` that call S1/S2 over the socket | Fewer LLM round trips, no code in the page | A JS engine in the CLI; sandboxing it |

Recommendation: ship S1 via agent-browser first (it is the lower-risk path to a working end-to-end), design the
`Koan.snapshot`/`act` schema now so S2 slots in under the same capability and audit rules, and decide S2/S3 after
Mac measurements of S1's gaps (cross-origin iframes, closed shadow roots, round trips per task). Password-field values
are never returned by any snapshot.

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
| Our endpoint | **needs an adapter**: it is a unix socket with an HMAC handshake; agent-browser cannot dial it |
| Adapter | **`koan` relay**: `koan agent-browser <args>` (or the plugin form `agent-browser --provider koan`) starts a per-run listener on `127.0.0.1:<ephemeral>` that serves only `/cdp/<128-bit secret>`, rejects any `Origin`, validates `Host`, does the HMAC handshake to the gateway itself, then execs `agent-browser` with `AGENT_BROWSER_CDP=ws://127.0.0.1:P/cdp/<secret>` **in the environment, not argv**, and exits with it. The secret never reaches a process list; it protects only a few seconds-to-hours loopback hop on the same uid, and the gateway still applies approval, scope and policy behind it |
| Browser-level `Target.*`, `Browser.getVersion`, `Page/Runtime/Network.enable` | provided by the facade (§4.2); denials of everything else return CDP errors; `bringToFront`/`activateTarget` no-op |
| Tabs | the facade lists only the agent Space (+ handed tabs), so "attach to every page" is safe; with none, it creates `about:blank` through `Koan.openTab` in the agent Space; `window new` (`Target.createBrowserContext`) returns an error that says use `tab new` |
| `snapshot`, `click`, `fill`, `type`, `press`, `scroll`, `eval`, `find`, `get`, `tab`, `frame`, `dialog`, `pdf` | work through the allowed methods (T-ab-1) |
| `screenshot`, `record`, `stream` (`Page.captureScreenshot`, `startScreencast`) | **need the keep-alive lease** (A§7, B§3): a hidden tab produces no frames; `screencast` has no capturer of its own |
| `wait` for animations / `requestAnimationFrame` pages / timers | need the lease (rAF and timer throttling in a background tab) |
| `cookies`, `storage`, `state save/load`, `auth` vault | **denied by default** (T5): `Network.getAllCookies`/`setCookies`, `DOMStorage` fail with `Koan.notPermitted`. Capability `cookies` (later) could allow site-scoped use. This is a deliberate non-feature; the docs say so |
| `network route`, `--headers`, `--allowed-domains`, proxy auth | `Fetch`: capability `network` (off) |
| `--auto-connect` | does not apply (reads Chrome's `DevToolsActivePort`); document `--cdp`/`--provider koan` |
| `--profile` with `--cdp` | do not combine (UNVERIFIED effect, D§2) |

### 11.2 Is the `koan` CLI still needed?

Yes, smaller. agent-browser does not do pairing, status, the Space/task model, take-over/hand-back, the audit log or
unix sockets. v1 `koan`: `pair`, `status`, `revoke`, `log`, `tabs`, `open`, `take-over`, `hand-back`, `watch`,
`screenshot`, the relay (`koan agent-browser`, `koan cdp-url`), and `eval`/`click`/`type` as a minimal built-in
fallback for machines without agent-browser. If the maintainer prefers, ship **only** the lifecycle commands and the
relay, and let agent-browser own page actions (open decision 4). In either case agent-browser compatibility is a
test target (§15, T-ab-*) rather than a dependency: we do not bundle or modify it.

## 12. The `koan` CLI and the agent skill

A single signed binary in `Koan.app/Contents/Resources/bin/koan` (Rust or Go; a static binary because skills run it
without a runtime; language is a phase-2 call), installed to `~/.local/bin/koan` by a settings button. Being inside
the app, its signature identifies it in the pairing dialog.

```
koan pair [--name N]            ask the browser for approval; stores ~/.koan/clients/N.token
koan status                      connected? clients, tasks, driven tabs, paused tabs
koan tabs [--space agent|all]    list tabs the client may see (id, title, URL, state)
koan open <url> [--task T]       open a background tab in the agent Space; prints the tab id
koan screenshot <tab> [-o f]     plain viewport capture
koan eval <tab> '<js>'           Runtime.evaluate in the tab
koan click|type <tab> …          minimal; agent-browser is richer
koan watch                       stream events (tab/URL/title, pause, release) as JSON lines
koan take-over [tab]             ask to drive the user's current tab (user approves)
koan hand-back [tab]             release the tab; a moved tab goes home
koan task start|end [name]       name/end the agent Space
koan agent-browser <args…>       relay + exec agent-browser against Koan
koan cdp-url                     print a one-run relay URL (for other CDP clients)
koan log [--follow]              the audit log
koan revoke [name|--all]
```

**Skill file** (ego's integration model: shell commands, no SDK, no remote-debugging setup). Koan writes
`~/.claude/skills/koan-browser/SKILL.md` (and `~/.agents/skills/koan-browser/` for agents that read it) **only when
the user clicks "Install agent skill" in settings**, never silently. The skill tells the agent: run `koan status`;
if not paired, `koan pair --name "<agent>"` and wait for the user to approve; open tabs with `koan open` (they land
in the agent's Space, never the user's); prefer `koan agent-browser snapshot -i` / `click @e3` for page work; use
`koan watch`; if `Koan.paused` appears, stop and tell the user; never ask the user for tokens; never read
`~/.koan`. A Codex/Cursor variant is the same file in their directories. Some agents sandbox shell commands (ego
notes Codex needs "Full access" to launch the local app, quick-start); `koan` only needs the user's home directory
and the socket, so the skill documents the sandbox allowances.

## 13. Incognito

Excluded by default for every client and every method: the registry resolves each target through
`Profile::FromBrowserContext(...)` and rejects `IsOffTheRecord()`, guest and system profiles, because
`Target.getTargets` lists incognito pages and nothing upstream filters them (B§9, C§3b). Agent tabs are created only
in the original profile; there is no `Koan.openTab {incognito}`; a take-over of an incognito tab is refused with a
specific error. The `incognito` capability (off) lets a client see/drive incognito tabs the user **explicitly**
hands over, each with its own prompt and a stronger banner; open decision 5 is whether to offer it at all in v1
(recommend no). Koan has no Spaces in incognito windows (`ShowsSpaceHeader`), so the agent Space never exists
there.

## 14. Hook patches (for docs/REBASE.md)

| # | File | Change | Needed for | Phase |
|---|---|---|---|---|
| 1 | `components/performance_manager/freezing/freezer.cc` | `MaybeFreezePageNode`: `if (contents->IsBeingCaptured()) return;` (3 lines; `IsBeingCaptured` is public WebContents API; the component can use it as it already uses `SetPageFrozen`) | agent tabs never freeze (§8) | 2 |
| 2 | `chrome/browser/performance_manager/policies/discard_eligibility_policy.cc` | an agent-tab reason beside `kDevToolsOpen` | only if T-keep-6 shows a gap | 2, optional |
| 3 | Startup hook calling `RemoteDebuggingServer::EnableDefaultUserDataDirCheckForTesting()` before `GetInstance()` (or one line in `remote_debugging_server.cc:169-174`) | the Chrome 136 parity fix (§3.2) | 2 (can ship first, alone) |
| 4 | `chrome/browser/BUILD.gn` / `chrome/browser/ui/BUILD.gn` deps on `//koan/browser/agent` and, for the priority vote, `//chrome/browser/glic/public` | build only | 2 |
| 5 | `browser_web_contents_delegate.cc` `ActivateContents`: skip contents flagged agent-owned | only if the facade no-op is not enough (a non-CDP caller) | 2, unlikely |
| 6 | Tab context menu "Hand to agent": a call inside the existing `AddKoanTabMenuItems` (`koan_tab_menu.h`) | no new upstream hook | 2 |
| 7 | Settings row and handler | no upstream hook (`koan_settings_*`) | 2 |
| 8 | `CannotFreezeReason::kAgentControlled` (≈5 files, modelled on `kPinnedTab`) | **alternative to #1**, only if #1's side effect (T-keep-5) is unacceptable | |
| 9 | `RenderProcessHostImpl::OnBoostForBackgroundExecution…` caller (content) | **alternative to the Glic call** if `//chrome/browser/glic` is unavailable in Koan's GN args | |

Everything else is in `//koan` and uses public `content::` and `chrome::` APIs: `DevToolsAgentHost`,
`DevToolsAgentHostClient`, `WebContents::IncrementCapturerCount`, `RenderWidgetHost::InputEventObserver`,
`WebContents::IgnoreInputEvents`, `TabStripModel`, `net::HttpServer`, `net::UnixDomainServerSocket`. Because Koan
adds a `DevToolsAgentHostClient`, the REBASE row must say: re-check `IsDomainAvailableToUntrustedClient`
(`devtools_session.h:245-266`, `chrome_devtools_session.cc:42-48`) and the client flags on every roll.

## 15. Mac tests (phase 2; nothing runs on Linux)

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
| T-keep-5 | freezing: enable infinite-tabs freezing, 7 tabs; agent tab idle 3 min | not frozen; others frozen as before (compare `chrome://discards`) |
| T-keep-6 | discard: Memory Saver at 1 min for the test; agent tab attached / detached | not discarded while leased |
| T-keep-7 | other tabs unchanged: a normal background tab still throttles and freezes | `power.mjs`-style numbers unchanged |
| T-keep-8 | priority: renderer process `taskpolicy`/`ps` state with and without the Glic vote | not background |
| T-keep-9 | idle release: no commands for the timeout | lease released, tab hidden again |
| **Focus and creation** | | |
| T-focus-1 | `Koan.openTab` while the user types in another tab | active tab, focus, caret and typed text unchanged (frontmost pid check per `koan-input-grant-and-front-pid`) |
| T-focus-2 | `Page.bringToFront`, `Target.activateTarget` from the client | no-op success; nothing raised |
| T-win-1 | Koan on another display / Space, `Koan.openTab` | no Space switch, no window raise |
| T-win-2 | no window open (windowless Mac app) then `Koan.openTab` | window appears inactive, does not take focus |
| **Security** | | |
| T-sec-1 | password manager: saved login, agent clicks the username field, reads the password field via `Runtime.evaluate` | value not readable (else disable autofill in agent tabs) |
| T-sec-2 | socket perms: dir 0700, socket 0600; a second uid and an unpaired same-uid client | other uid refused; unpaired gets only `Koan.pair` |
| T-sec-3 | web page attack: page JS `fetch('http://127.0.0.1:*')`, DNS-rebinding page against the relay, `Origin`-bearing WebSocket | no connection |
| T-sec-4 | denied domains: call every row of the §4.3 deny list and `Storage.*`, `Network.getAllCookies`, `Browser.close`, `Extensions.loadUnpacked`, `SystemInfo.getInfo`, `Page.navigate file:///etc/hosts`, `chrome://settings`, `DOM.setFileInputFiles`, navigation to `chrome-extension://` | each returns the expected error; browser unaffected; audit entries present |
| T-sec-5 | incognito: open an incognito window with a tab | absent from `Koan.listTabs` and `Target.getTargets`; `attachToTarget` by guessed id refused; `Koan.takeOver` refused |
| T-sec-6 | pairing: spoofed `--name`; deny; timeout; rate limit; revoke while connected; revoke all; feature off while connected | dialog shows real path/ancestors; revoke closes sockets, releases leases |
| T-sec-7 | policy: `RemoteDebuggingAllowed=false`, `DeveloperToolsAvailability=2` | gateway stops / attach refused |
| T-sec-8 | `--remote-debugging-port=9222` on the default profile (Koan not running) | ignored with the stderr message, and `curl 127.0.0.1:9222` fails; with a non-default `--user-data-dir` still works |
| T-sec-9 | audit log: content of a typed password and of `Runtime.evaluate` source | neither appears |
| **Take-over** | | |
| T-take-1 | agent drives the user's active tab (approved); user types/clicks in the page | `Koan.paused`; mutating calls rejected; reads allowed; banner shows; no further injected input |
| T-take-2 | CDP-injected input does **not** pause (`kFromDebugger` filter); `Input.insertText` too | agent keeps running |
| T-take-3 | hand back via banner; agent resumes; `Koan.resume` alone does nothing | resumes only after the user's click |
| T-take-4 | move into agent Space and take back; pinned/Essentials refused; user closes the tab mid-run | origin Space/index restored; refusals; `detachedFromTarget` |
| T-take-5 | user switches Space / tab while the agent works | agent unaffected; the agent's tabs keep rendering |
| T-take-6 | two clients, two tasks | two Spaces, isolated registries; revoke one |
| **Compatibility** | | |
| T-ab-1 | `agent-browser` (pinned version) through the relay: `open`, `snapshot -i`, `click @e1`, `fill`, `eval`, `tab new`, `screenshot`, `pdf`, `record` (needs ffmpeg), `wait` on an animation | each works on a **background** tab; failures listed with the denied method |
| T-ab-2 | `cookies`, `state save`, `network route` | fail with `Koan.notPermitted`, no crash, no hang |
| T-ab-3 | agent-browser against a profile with 30 user tabs | it attaches to the agent Space tabs only |
| T-cdp-1 | Puppeteer `connect({browserWSEndpoint})` through the relay | attaches; limitations documented |
| T-cdp-3 | `Accessibility.getFullAXTree` from an untrusted client, incl. OOPIF and shadow DOM | works, or the facade proxies it |
| T-cdp-4 | `Page.captureScreenshot` with `clip`/`captureBeyondViewport` on the user's visible tab | refused or no relayout flicker |
| **UI** | | |
| T-ui-1 | indicator in each layout: normal, compact (sidebar hidden), fullscreen, multiple windows | at least one persistent mark is visible; menu-bar item correct |
| T-ui-2 | settings: toggle, client list, revoke, stop all, install CLI/skill | pref, socket, sessions follow the toggle immediately |
| T-ui-3 | unit tests (can run on Linux as pure logic): method policy table, URL scope, pairing HMAC, rate limiter, socket-path fallback, audit redaction | pass |

Every run also fails on a Koan log `:FATAL:`, a crash report, or a leaked lease (counted at exit).

## 16. Phasing

* **2a (security first, small):** the Chrome 136 parity fix (#3 above) and the `Koan` settings switch scaffold.
* **2b:** gateway, pairing, the untrusted client, registry, method policy, audit log, indicator (no UI-heavy parts).
* **2c:** agent Space, `Koan.openTab`, lease + freezer hook, `koan` CLI, skill file; Mac tests T-keep/T-focus/T-sec.
* **2d:** take-over/hand-back, banner, pause; `agent-browser` relay/plugin and T-ab.
* **Later:** `Koan.snapshot`/`act` (S2), `cookies`/`network` capabilities, O2 window mode, strict-mode Touch ID, signature-bound credentials.

Phase 2 starts after the 24 Nov parity merges unless reprioritized (issue #180). 2a does not wait for anything.

## 17. Open decisions for the maintainer

1. **Turn on Chrome's default-dir check in Koan (§3.2)** and ship it ahead of the feature, as its own PR? (Recommended: yes. Koan's default profile currently accepts `--remote-debugging-port`.)
2. **Agent workspace**: O1 a Space per task in the user's window (recommended), O2 a window, or both?
3. **Approval granularity**: per client until revoked (recommended; 30-day expiry), per browser launch, or every connection (offered as *strict mode*)?
4. **CLI scope**: lifecycle + relay only and let agent-browser own page actions (smaller, recommended), or also built-in `click`/`type`/`eval`?
5. **Incognito capability**: no incognito at all in v1 (recommended), or explicit per-tab hand-over only?
6. **Indicator when compact mode hides everything**: menu-bar item + Dock badge only (recommended), or hold a minimal always-visible strip while an agent is connected?
7. **Visible vs hidden-but-painting capture** for the lease: visible (recommended: unfreezes, compositor-animation path verified) vs `visibilityState` staying "hidden".
8. **Freezer fix**: the 3-line `IsBeingCaptured()` early return (recommended) or a `kAgentControlled` reason (cleaner, ≈5 files)?
9. **Glic dependency**: call `GlicPerfTraitsTracker` (public, upstream's recipe) or take the content patch for `OnBoostForBackgroundExecution`?
10. **Skill auto-install**: only on a settings click (recommended) or offered at onboarding like ego?
11. **`cookies` / `network` capabilities**: ship v1 without them (recommended; agent-browser's `state`/`cookies`/`network route` then fail by design), or ship site-scoped versions?
12. **Where the CLI is written** (Rust vs Go) and whether it lives in this repo.

## 18. Notes and discrepancies found while auditing

* `docs/PERFORMANCE.md` says energy saver "caps frame rates"; the audit found no frame-rate cap code in `cc/`,
  `viz/`, `ui/compositor` or `content/` (A§4). Either the doc is wrong or the cap is elsewhere: worth a check in the
  performance thread; it matters here because it decides whether agent tabs animate at full rate on battery.
* Chromium's own agent (`chrome/browser/actor/`, Glic) uses the same capture + priority recipe; its source is the
  best reference for phase 2 and is not otherwise linked into Koan.
* ego(lite) is closed source; only its public page and quick-start were read (2026-11-07). Its Space/hand-over and
  engine-snapshot ideas are adopted as options above; nothing else is assumed about its implementation.
