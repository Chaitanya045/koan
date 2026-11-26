# Remote Spaces (#445): the managed SSH tunnel

Date: 2026-11-27. OpenSSH sources read at tag `V_9_9_P1` (openssh/openssh-portable). Behaviour
checked locally against OpenSSH 10.4p1 (a private `sshd` on 127.0.0.1 with throwaway keys and
its own known_hosts; no remote host). Chromium 158.

The issue: a Space is connected to the user's Linux box over SSH, and in that Space
`localhost:3000` is the box's port 3000. The user has SSH only, and never opens a terminal.

The work is split into three PRs:
1. A per-container proxy setting, and a hook that gives a Space's storage partition a runtime
   SOCKS proxy.
2. This one: `src/browser/remote/`, the tunnel. It is self-contained and depends on //base
   (plus //net for the system-events adapter).
3. The UI: the host picker, the state chip and the prompt dialog.

## The pieces

| File | What |
|---|---|
| `koan_ssh_config.*` | `~/.ssh/config` and its `Include`s, tokenized like `readconf.c`: the concrete `Host` aliases for the picker |
| `koan_ssh_command.*` | `SshDestination` (alias or `user@host[:port]`, validated), ssh's argv, the supervisor script, the environment |
| `koan_ssh_failure.*` | Why ssh ended: stderr lines plus the exit code give an `SshFailure`, and which failures are retried |
| `koan_ssh_text.*` | Sanitizing untrusted text (control and bidi characters, invalid UTF-8, length) |
| `koan_ssh_askpass.*` | The askpass protocol, `SshPrompt`/`SshPromptReply`/`SshSecret`, and the `SshPromptDelegate` for PR 3 |
| `koan_ssh_askpass_server.*` | The browser's end of the helper: FIFOs in a 0700 directory (POSIX) |
| `koan_ssh_tunnel.*` | `SshTunnel`, the state machine (states, backoff, prompts, sleep and network), over `SshTunnelPlatform` |
| `koan_ssh_tunnel_platform_posix.*` | The real platform: launch, stderr, reaping, port pick, SOCKS probe |
| `koan_ssh_tunnel_system_events.*` | `base::PowerMonitor` plus `net::NetworkChangeNotifier`, forwarded to a tunnel |
| `koan_ssh_test_fakes.*` | `FakeSshTunnelPlatform`, `FakeSshPromptDelegate` (for this PR's tests and PR 3's) |

No upstream patch.

## OpenSSH facts, with sources

- **A `-D` port can't be dynamic.** `readconf.c parse_forward()` rejects `listen_port == 0` for
  everything but remote forwards (`!remotefwd && fwd->listen_port == 0` → fail). "Allocated port
  %u" exists only for `-R` (`ssh.c ssh_confirm_remote_forward`). So Koan binds `127.0.0.1:0`
  itself, reads the port, closes the socket and passes the port on. A clash shows as
  `bind [127.0.0.1]:N: Address already in use` plus `Could not request local forwarding.`
  (`channels.c channel_setup_fwd_listener_tcpip`, `ssh.c ssh_init_forwarding`), which is fatal with
  `ExitOnForwardFailure=yes`. Koan then retries at once on a new port (verified).
- **Forwards come up only after authentication** (`ssh_init_forwarding` runs in `ssh_session2`,
  after `ssh_login`; verified: the bind error came after both prompts). So the `-D` listener
  answering the SOCKS5 greeting (`05 01 00` → `05 00`) means "connected". Koan polls it every
  250 ms and never sleeps blindly. The debug line "Local forwarding listening on" is printed
  before `bind()`, so it is not a ready signal.
- **ssh can't watch Koan.** With `-N` there is no session channel, so stdin is never read, and EOF
  on stdin does nothing. `ssh.c main` also `closefrom(STDERR_FILENO + 1)`s, so an inherited fd
  can't reach the askpass helper. Hence the supervisor (below).
- **Options come before the config.** The first value obtained wins and the command line is read
  first, so `-o` overrides the user's config. `ControlPath=none` and `RemoteCommand=none` are
  cleared by `CLEAR_ON_NONE` (`readconf.c fill_default_options`). `ForkAfterAuthentication`,
  `SessionType` and `StdinNull` are OpenSSH 8.7+. An unknown option is fatal, with
  `command-line: line 0: Bad configuration option: …`, which Koan reports as `kSshTooOld`.
- **The destination can't become an option after `--`.** `ssh.c` sets `opt_terminated` when
  `av[optind-1]` is `--` and then won't `goto again` to re-parse options after the host (without
  `--`, `ssh host -oProxyCommand=…` *is* parsed). ssh's own `valid_hostname()` refuses a leading
  `-`, whitespace, control characters, quotes, backticks and `$ \ ; & < > | ( ) { }`, and `valid_ruser()` the same for the
  user. Koan is stricter: a host is `[A-Za-z0-9._-]` (not starting with `-` or `.`) or an IPv6
  literal; a user is `[A-Za-z0-9._+-]`. Koan also passes `-l user` and `-p port` instead of
  `user@host:port`. ssh doesn't parse `:port` in a destination (only `ssh://` URIs, which Koan
  refuses).
- **Askpass** (`readpass.c`):
  - `ssh_askpass()` runs `execlp(askpass, askpass, prompt)`. The prompt is argv[1], and the
    answer is the helper's stdout, at most 1023 bytes, cut at the first `\r` or `\n`. A non-zero
    exit means no answer.
  - `SSH_ASKPASS_REQUIRE=force` uses askpass even without `DISPLAY` or a tty (OpenSSH 8.4+;
    macOS ships 9.x or later).
  - `SSH_ASKPASS_PROMPT` is the only hint. It is `confirm` for `ask_permission()`
    (`RP_ASK_PERMISSION`), and `none` for `notify_start()` notices. The notice helper's output is
    ignored, and ssh SIGTERMs it when done (for example "Confirm user presence for key …" while a
    FIDO key waits for a touch). Every other prompt comes with no hint.
  - ssh doesn't tell the helper whether an answer may echo (`RP_ECHO` isn't passed on), so Koan
    masks everything.
  - `ask_permission()` treats an empty answer or "yes" as **allowed**, which is why Koan always
    answers questions with an explicit `yes` or `no`.
  - A failed (cancelled) password askpass makes `read_passphrase()` return `""`, and ssh sends
    that, so a cancel also stops the tunnel.
- **The unknown host key prompt** (`sshconnect.c check_host_key`, `confirm()`):
  - It goes through `read_passphrase(msg, RP_ECHO)` with **no hint**. The text starts with
    `The authenticity of host '…' can't be established` and ends
    `Are you sure you want to continue connecting (yes/no/[fingerprint])? `.
  - The fingerprint line is `… key fingerprint is SHA256:….` in 9.x and
    `… key fingerprint is: SHA256:…` in 10.x (verified on 10.4).
  - "no" (or nothing) makes ssh fail with `Host key verification failed.`, exit 255 (verified).
  - A server's keyboard-interactive prompts reach askpass as `(user@host) <prompt>`
    (`sshconnect2.c input_userauth_info_req`, `asmprintf`), so a server can't make one start like
    ssh's own question.
  - Other yes/no questions from ssh: the IP-mismatch warning `… (yes/no)? `, and
    `Accept updated hostkeys? (yes/no): ` (`clientloop.c`, `UpdateHostKeys=ask`).
- **A changed host key never gets a prompt.** Under `ask` or `yes` ssh prints
  `WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!` and
  `Host key for … has changed and you have requested strict checking.`, then fails
  (`sshconnect.c` ~1330). Koan reports `kHostKeyChanged` and never retries.
- **Other messages Koan reads** (`koan_ssh_failure.cc`):

  | Messages | Source | Reason |
  |---|---|---|
  | `Could not resolve hostname` | `ssh.c` | `kHostNotFound` |
  | `ssh: connect to host … port …: <errno>` | `sshconnect.c ssh_connect_direct` | `kHostUnreachable` |
  | `Connection timed out during banner exchange` | `kex.c` | `kHostUnreachable` |
  | `…: Permission denied (…)` | `sshconnect2.c` | `kAuthFailed` |
  | `Timeout, server … not responding.` | `clientloop.c` | `kConnectionLost` |
  | `Connection to … closed by remote host.` | `clientloop.c` | `kConnectionLost` |
  | `Connection closed by …`, `Connection reset by …` | `packet.c` | `kConnectionLost` (`kHostUnreachable` if never connected) |

## The command

```
/bin/sh -c <supervisor> koan-ssh-tunnel \
  /usr/bin/ssh -N -n -T -D 127.0.0.1:<port> \
    -o ExitOnForwardFailure=yes -o ServerAliveInterval=10 -o ServerAliveCountMax=3 \
    -o ConnectTimeout=15 -o ControlMaster=no -o ControlPath=none \
    -o ForkAfterAuthentication=no -o RemoteCommand=none -o RequestTTY=no \
    -o PermitLocalCommand=no -o LogLevel=INFO [-p port] [-l user] -- <host>
env: inherited (SSH_AUTH_SOCK, HOME, PATH) + SSH_ASKPASS=<dir>/askpass
     SSH_ASKPASS_REQUIRE=force, SSH_ASKPASS_PROMPT unset
```

- **Binaries.** Only `/usr/bin/ssh` and `/bin/sh` are used. Each must be a regular executable
  owned by root and writable by nobody else (on macOS both sit on the SIP-protected system
  volume). PATH is never searched.
- **No connection sharing.** A ControlMaster would make this ssh a mux client of another process,
  or a master that outlives it. ForkAfterAuthentication (`-f`) would daemonize out of the process
  group.
- **The user's config** still applies: keys, `IdentityAgent`, `ProxyJump`/`ProxyCommand`
  (their children stay in the group), `User`, `Port`, `HostName`, known_hosts.
  - Accepting a new host key adds it to `~/.ssh/known_hosts`, as in a terminal.
  - A `LocalForward`/`RemoteForward`/`DynamicForward` in the config also applies, and a failing
    one ends the tunnel (`ExitOnForwardFailure`). This is an open question.

## Process lifetime

The supervisor runs `ssh` in the foreground and a watcher in the background:

```sh
exec 3<&0 </dev/null
(IFS= read -r _ <&3; kill -TERM 0) 2>/dev/null &
exec 3<&-
"$@"
r=$?
trap '' TERM
kill -TERM 0 2>/dev/null
exit "$r"
```

- **Its own group.** Koan launches it with `base::LaunchOptions::new_process_group`
  (`POSIX_SPAWN_SETPGROUP` on macOS), so ssh, the watcher, every askpass helper and every
  ProxyCommand share one process group whose id is the supervisor's pid.
- **Koan holds the only write end of the stdin pipe.** It is created close-on-exec, and macOS
  spawns with `POSIX_SPAWN_CLOEXEC_DEFAULT`. When that end closes, the watcher reads EOF and kills
  the group. The end closes when the tunnel stops (the `Process` is destroyed on the tunnel's
  sequence), and also when Koan quits or **crashes**, because the kernel closes it. Verified: the
  group was empty 0.05 s after the pipe closed.
- **When ssh exits by itself,** the script kills what is left of the group (a stray helper) and
  exits with ssh's status (verified: 255 is passed through).
- **Koan's own checks.**
  - On destruction the io side also sends SIGTERM to the group, then SIGKILL after 3 s, and only
    then reaps the supervisor. The group id can't be reused while the supervisor is unreaped.
  - In normal operation the end of ssh is EOF on its stderr pipe. Every member holds stderr; the
    watcher's goes to /dev/null. The group then gets SIGKILL (anything that closed stderr but
    kept running), and the supervisor is reaped by polling `WaitForExitWithTimeout(0)`, so no
    thread blocks on `waitpid`.
- **Threads.** All of this runs on a `MayBlock` ThreadPool sequence per process, with
  `FileDescriptorWatcher`, so the UI thread does no blocking work.

## The askpass helper and its threat model

Per ssh run, Koan makes a fresh `mkdtemp` directory (mode 0700) under the user's `$TMPDIR`.
`/var/folders/…/T` is per-user on macOS. It holds:

- `askpass`, mode 0700, the POSIX sh script `kHelperScript`;
- `ctl`, a FIFO (0600) that Koan opens O_RDWR (never EOF, and helpers' opens never block);
- per request, `q.<pid>` (0600: the hint and the prompt, which are not secret) and `a.<pid>`
  (a 0600 FIFO for the answer).

The exchange:

1. The helper `mkfifo`s `a.$$` and opens it read-write on fd 3, so the open never blocks.
2. It writes `q.$$`, then writes `$$\n` to `ctl`. That write is under `PIPE_BUF`, so it's atomic.
3. It reads one status line from fd 3. On `ok` it runs `head -n 1 <&3`, so the answer goes from
   the FIFO straight to ssh's pipe and **never enters a shell variable, argv or a file**. On
   anything else it exits 1.
4. On the browser side, the request's pid must be in the tunnel's process group
   (`getpgid(pid) == supervisor pid`). `q.<pid>` must be a regular file of the user's (opened
   O_NOFOLLOW, one link, at most 8 KiB). It is unlinked once read.
5. The answer is written non-blocking to `a.<pid>` after an `fstat` (it must be a FIFO of the
   user's). If the helper is gone, the open fails instead of hanging.
6. The helper's EXIT trap removes its files. The whole directory is deleted when the ssh run
   ends.

The design meets the brief's security requirements as follows:

- **Secrets are never logged, written to disk or persisted.** The answer crosses only the
  kernel's pipe buffers and is not in any argv or environment. `SshSecret` wipes its bytes
  (`OPENSSL_cleanse`) when it is destroyed, moved from or cleared, and Koan never logs ssh's
  stderr or prompts.
  - Residual risk: a `std::string` that reallocated earlier may leave a copy. The encoder
    `reserve()`s first, and the UI's text field is PR 3's job.
- **Who can reach the channel.**
  - Other users can't, because of the 0700 directory and 0600 FIFOs.
  - Other processes of the same user could open `ctl`, but they get no prompt and no answer
    unless their pid is in the tunnel's process group.
  - A same-user attacker could still race for `a.<pid>`. That is out of scope: such a process can
    read `~/.ssh` and the keys anyway.
  - macOS offers no peer credential on FIFOs. A Unix socket would have one (`LOCAL_PEERPID`), but
    POSIX sh can't speak to a socket without `nc`.
- **The remote's text is untrusted.**
  - The prompt is sanitized (`SanitizeSshText`: control and bidi characters removed, invalid
    UTF-8 replaced, 8 KiB cap), and the UI must show it as quoted plain text.
  - The kind of a prompt is decided by ssh's own framing: server prompts always start with
    `(user@host) `. A server therefore can't pass for a host-key question or a confirmation, and
    it can only ever receive "yes" or "no".
  - The dialog's title uses the destination the user typed, not the prompt.
- **Cleanup.** If Koan crashes, the group dies with the pipe, and the stale directory under
  `$TMPDIR` holds no secrets (macOS cleans `$TMPDIR` periodically).

### Alternatives considered

- **A Unix socket.** It needs `nc -U` or a compiled helper. A compiled helper means bundling a new
  executable in the app, which is an upstream patch to the Mac bundle.
- **Koan's own binary as `SSH_ASKPASS`.** This needs an upstream hook in Chrome's main to divert
  early, and starting the whole browser binary for each prompt is slow.
- **Passing an inherited fd.** ssh's `closefrom(3)` rules it out.

## States and reconnect

`SshTunnel` has these states (with `SshTunnelStatus`):

- **kStopped**
- **kConnecting**
- **kNeedsInput** (with `prompt`)
- **kConnected** (with `port`)
- **kRetrying** (with `failure`, `detail` and `next_attempt`)
- **kFailed** (with `failure` and `detail`)

Retry and recovery:

- **Backoff.** Retries back off at 1, 2, 4, … up to 60 s. A connection that lasts 30 s resets the
  backoff; one that flaps keeps growing it.
- **Port clashes** are retried at once, up to 3 times, on a new port. Otherwise the last port is
  reused, so PR 1's proxy setting usually stays the same.
- **Never retried:** `kHostKeyChanged`, `kHostKeyNotVerified` (rejected, or strict checking),
  `kAuthFailed`, `kCancelled`, `kBadConfig`, `kSshTooOld` and `kSshNotFound`. Only
  `Reconnect()` (the user) or `Start()` tries again.
- **Sleep, wake and network changes** come through `SshTunnelSystemEvents`, from `PowerMonitor`
  and `NetworkChangeNotifier`.
  - While the Mac sleeps, no attempt starts.
  - On resume, or on a network change (once the new network is up, not on `CONNECTION_NONE`),
    a waiting retry starts at once with the backoff reset.
  - A connected tunnel restarts at once, keeping its port, because its TCP connection is
    probably dead and ServerAlive would need up to 30 s to notice. The exception is a tunnel
    whose connection needed the user's input (password, code, host key, security-key touch):
    restarting it would ask again, so it waits for ServerAlive instead.

## For PR 1 and PR 3

- **Wiring.** One `SshTunnel` per remote Space (or per container), plus one
  `SshTunnelSystemEvents`, all on the UI thread, with one `SshTunnelPlatformPosix` per profile or
  for the whole browser.
  - `OnSshTunnelStatusChanged`: on `kConnected`, call `SetRuntimeProxy(container, status.port)`;
    in every other state, set "disconnected". The Space must **fail closed** and never fall back
    to a direct connection.
  - Destroy the tunnel on Space disconnect and on profile shutdown. Quit and crash are covered by
    the pipe.
- **Chromium bypasses the proxy for localhost** by default: the implicit `<local>`/loopback rules
  in `net::ProxyBypassRules`. For `localhost:3000` to reach the box, PR 1's config must remove
  them with `<-loopback>`, and use `socks5://127.0.0.1:<port>`, which resolves names on the
  remote side.
- **The UI** implements `SshPromptDelegate`:
  - kSecret is a masked field;
  - kConfirm is yes/no;
  - kHostKey shows the fingerprint, with "Connect" and "Cancel";
  - kNotice is a message with no buttons;
  - all with the destination as the title and the prompt as quoted text.

  Reasons get localized strings in the UI. Use `SshFailure`, never ssh's text, except as
  "details".
- **The host picker:** `ReadSshConfigHosts(base::GetHomeDir())` on a MayBlock task, plus a free
  text field through `ParseSshDestination()`. Save `SshDestinationToString()`.

## Open questions

- **The macOS 15+ Local Network privacy prompt.** ssh connecting to a LAN address is attributed
  to Koan (the responsible process), so the first connection shows "Koan would like to find
  devices on your local network". That is expected. `disclaim_responsibility` is left off.
- **Forwards in the user's config** (`LocalForward` and the like) apply to Koan's ssh too, and a
  failing one ends the tunnel. `ClearAllForwardings` would also clear Koan's `-D`, so there is no
  clean fix. The UI could show the stderr detail.
- **The locale.** A GUI app's environment usually has no `LANG`, so ssh may print non-ASCII prompt
  text as `\ooo` escapes (`utf8.c`). Setting `LC_CTYPE=UTF-8` for ssh would fix that. It isn't
  set yet.
- **A Homebrew ssh** (`/opt/homebrew/bin/ssh`) is ignored on purpose. A pref could allow it later.
- **ServerAlive** is 10 s × 3. It could be shorter for faster detection, at the cost of more
  keepalive traffic.
