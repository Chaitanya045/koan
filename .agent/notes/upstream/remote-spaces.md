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
  `bind [127.0.0.1]:N: Address already in use` and
  `channel_setup_fwd_listener_tcpip: cannot listen to port: N` (`channels.c`), plus
  `Could not request local forwarding.` when no local forward at all came up
  (`ssh.c ssh_init_forwarding`). With `ExitOnForwardFailure=no` ssh carries on without it, so the
  tunnel ends the attempt itself and retries at once on a new port (below).
- **Forwards come up only after authentication** (`ssh_init_forwarding` runs in `ssh_session2`,
  after `ssh_login`). Verified with a passphrase askpass that took 2 s: `read_passphrase:
  requested to askpass` at 0.115 s, then at 2.289 s `Authenticated to …`, the listening line and
  `Entering interactive session.` in the same millisecond. So the port is free for anyone to take
  for as long as the user takes to answer a prompt. See "Is the listener ssh's?".
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
  - A failed askpass (non-zero exit, or killed) makes `read_passphrase()` return `""` for a
    password, passphrase or keyboard-interactive prompt, and ssh **sends that** to the server,
    which logs and counts a failed login (faillock, 2FA lockouts). So a secret prompt is never
    refused through the helper: on a cancel, Stop() or teardown Koan writes nothing and kills
    the process group first (SIGTERM, which ends ssh at once: before `client_loop()` it has no
    TERM handler), and the helper dies with ssh. Verified against a private sshd with
    password-only auth: a helper that exits 1 produced two `Failed password` lines; killing the
    group while the helper waited produced none, only `Connection closed by authenticating
    user … [preauth]`.
  - A rejected host key is still answered with an explicit `no`: `confirm()` returns 0 and
    `check_host_key()` fails during the key exchange, before any authentication, with
    `Host key verification failed.`
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
  | `Received disconnect from …: Too many authentication failures`, then `Disconnected from …` | `packet.c` | `kAuthFailed` |
  | `Timeout, server … not responding.` | `clientloop.c` | `kConnectionLost` |
  | `Connection to … closed by remote host.` | `clientloop.c` | `kConnectionLost` |
  | `Connection closed by …`, `Connection reset by …` | `packet.c` | `kConnectionLost` (`kHostUnreachable` if never connected) |
  | `bind [127.0.0.1]:N: …`, `cannot listen to port: N` (Koan's N only, while ssh runs) | `channels.c` | `kPortInUse` (`ReadSshForwardLine()`) |
  | `channel_setup_fwd_listener_tcpip: getaddrinfo(127.0.0.1): …` (while ssh runs) | `channels.c` | `kIpv6Only` (`ReadSshForwardLine()`) |

- **Only ssh's last line decides** (`ClassifySshExit()`). The server's text shares ssh's stderr:
  the banner is printed as sent, line breaks and all (`sshconnect2.c input_userauth_banner`,
  `fmprintf`), keyboard-interactive names and instructions and disconnect reasons go through
  `logit()` (`VIS_SAFE` keeps `\r` and `\n`), and a ProxyJump's ssh prints its own host's banner
  there too. Verified: a banner reading `Permission denied (publickey).` and `WARNING: REMOTE
  HOST IDENTIFICATION HAS CHANGED!` arrives as those exact lines, also through a jump. A
  matching line anywhere therefore proves nothing. What the server can't forge is the position
  of ssh's own reason: ssh prints its `fatal()` last and exits (verified for a changed key, a
  failed login, a refused connection and the same three through a ProxyJump). So:
  - a run that reached `kConnected` is always `kConnectionLost` (its host key and login were
    accepted);
  - a run that ssh didn't end itself (the supervisor reports 128 + N, or -1) printed no reason,
    and is `kUnknown`;
  - otherwise the last kept line decides. Two reasons read the line before it, which ssh prints
    just before: `Host key for … has changed and you have requested strict checking.` makes
    `Host key verification failed.` `kHostKeyChanged`, and a disconnect's reason precedes
    `Disconnected from …`. `Killed by signal N.` (a ProxyJump's ssh at `LogLevel VERBOSE`, hung
    up after ours exits) is dropped like the other verbose notes.
  - The detail is that same line, never an earlier one the server may have written.
  - Accepted: a server's disconnect reason (or a fake one sent before the key exchange by
    someone on the path) can say `Too many authentication failures` and stop the retries. Such a
    party can block the connection anyway.

## Is the listener ssh's?

Koan picks the port, releases it, and ssh binds it only after authenticating, which can take as
long as a password, a 2FA code or a host key question. Any local process (another user's, or a
sandboxed app allowed to listen) can bind `127.0.0.1:N` in between. A SOCKS5 answer on the port
therefore proves nothing on its own: before this fix, Koan would have sent the Space's traffic
(cookies, dev-site sessions) to that process.

**The proof Koan uses: ssh's own debug1 lines, in order** (`SshForwardLine`,
`ReadSshForwardLine()`, `SshTunnel::OnForwardLine()`). ssh sets up its local forwards one after
another, synchronously, right after authenticating, and only then enters `client_loop()`. Koan's
`-D` is always the first local forward: command-line forwards are added while the options are
parsed, before any config is read (verified: it is `channel 0`, ahead of a config's
`LocalForward`). For each forward, `channel_setup_fwd_listener_tcpip()` (`channels.c` ~3888)
prints the listening line *before* `bind()`, then either an error (`bind …`, `listen …`) or
nothing:

```
debug1: Local forwarding listening on 127.0.0.1 port <N>.     kBinding (before bind)
bind [127.0.0.1]:<N>: Address already in use                  kFailed
channel_setup_fwd_listener_tcpip: cannot listen to port: <N>  kFailed
... the user's own forwards ...
debug1: Entering interactive session.                         kSessionStarted (clientloop.c)
```

- `kSessionStarted` after `kBinding` for exactly `127.0.0.1 port <N>`, with no `kFailed` for `<N>`
  between, means ssh holds a listening socket on `127.0.0.1:N`. Only then is the port probed,
  and a SOCKS5 answer means "connected".
- Nobody else can hold the same address and port at that point. ssh sets `SO_REUSEADDR` but not
  `SO_REUSEPORT` (`set_reuseaddr()`), so an exact duplicate bind fails both on Linux and on
  macOS/BSD, where sharing needs `SO_REUSEPORT` on every socket. A wildcard (`0.0.0.0:N`) listener
  can coexist on macOS, but a connection to `127.0.0.1` goes to the more specific socket, ssh's.
- The listening line alone is no proof: it comes before `bind()`, and Koan could probe between
  it and the bind error. Neither is `Entering interactive session.` alone: a ProxyJump's ssh
  prints it too if the user's config gives that hop `LogLevel DEBUG`, and it comes before ours.
  So a session line without the listening line first is ignored. A ProxyJump's ssh has no
  forwards at all (`-W` implies `ClearAllForwardings`, `readconf.c fill_default_options()`), so
  every forward line is the main ssh's.
- `kFailed` (a bind, listen or socket error for `127.0.0.1:N`, `cannot listen to port: N`, or
  `Could not request local forwarding.`) before that ends the attempt as `kPortInUse`: ssh is
  killed and Koan retries at once on a new port.
- `kUnsupported`: `channel_setup_fwd_listener_tcpip: getaddrinfo(127.0.0.1): …` (no port in
  it). The user's config has `AddressFamily inet6` for the host. It sets the hints for every
  forward's lookup too (`hints.ai_family = ssh->chanctxt->IPv4or6`, `channels.c`), so ssh can't
  listen on 127.0.0.1 with any port. Verified: ssh authenticates, prints that line and `Could
  not request local forwarding.`, and runs on without `-D`. There is no clean way to pin the
  forward alone: `-4` / `-o AddressFamily=inet` would also change how the host is reached (an
  IPv6-only host would break), `-D [127.0.0.1]:N` takes the same lookup, and `-D localhost:N`
  listens on `::1` (verified), which isn't the address Koan proxies to. With any other family the
  lookup of a numeric 127.0.0.1 can't fail, and Koan's `-D` is the first forward, so the line is
  Koan's. The attempt ends as `kIpv6Only`, which is never retried: retrying would log in again
  each time, and only the user's config can fix it.
- The lines need `-o LogLevel=DEBUG1`. It is set with `-o`, not `-v`: ssh passes only `-v` on to a
  ProxyJump's ssh (`ssh.c`, "Optional -v arguments if -v set"; verified, the jump's command has
  no `-v`).
- No timeout: if the lines never come, the tunnel stays `kConnecting` and fails closed. A
  timeout would need a start point, and ssh prompts for as long as the user takes.
- Unit tests: an answer without the listening line never connects; the line for another port,
  `::1`, `0.0.0.0`, `127.0.0.2` or without `debug1: ` doesn't count; our bind error retries on a
  new port; the user's failing forward doesn't stop us.

Verified with OpenSSH 10.4p1 against a private `sshd` on 127.0.0.1 (throwaway keys, `-F` config).
Each line is ssh's, ending `\r\n` (`log.c do_log()`):

- **Plain key login:** `Authenticated to 127.0.0.1 ([127.0.0.1]:2299) using "publickey".`,
  `debug1: Local connections to 127.0.0.1:41234 forwarded to remote address socks:0`,
  `debug1: Local forwarding listening on 127.0.0.1 port 41234.`,
  `debug1: channel 0: new port-listener [port listener] (inactive timeout: 0)`,
  `debug1: Requesting no-more-sessions@openssh.com`, `debug1: Entering interactive session.`, all
  at 0.130 s. The SOCKS5 probe then answered `05 00`.
- **Another process already listening on our port:** the listening line, then
  `bind [127.0.0.1]:41235: Address already in use`,
  `channel_setup_fwd_listener_tcpip: cannot listen to port: 41235`,
  `Could not request local forwarding.`, and then ssh went on to `Entering interactive
  session.` and kept running. That is the hijack: with only a SOCKS probe, that listener would
  have been "connected".
- **ProxyJump:** `Setting implicit ProxyCommand from ProxyJump: /usr/bin/ssh -F … -W
  '[%h]:%p' lab` (no `-v`), then the same sequence for our port.

A Python mirror of `ReadSshForwardLine()` and the tunnel's state, run over the raw stderr of four
runs (another listener on our port, a busy user `LocalForward`, ProxyJump, plain), gave
`kPortInUse`, then "listening" for the other three. After the chatter filter, the lines kept were
only the bind errors (and `Permission denied (publickey).` or `connect to host … Connection
refused` in two failing runs).

**Residual risk.** The lines share stderr with text that the remote side controls: the
pre-authentication banner (`sshconnect2.c input_userauth_banner`, `fmprintf`) and a
keyboard-interactive name or instruction (`logit()`, `VIS_SAFE` keeps newlines). A host could
print a fake listening line and session line for `<N>`. Only a host whose key the user has
already accepted can do this, since the banner comes after the key exchange. The destination
host gains nothing: it is the SOCKS exit and sees all of the Space's traffic anyway. A malicious
*jump* host could, if a local process also wins the bind race on an unpredictable port. Both
are accepted. The same text could fake Koan's bind error (a retry on a new port) or the
`getaddrinfo(127.0.0.1)` line (the attempt ends as `kIpv6Only`), which such a host could cause
anyway by refusing the login.

**Alternatives considered.**
- On macOS, `libproc` (`proc_listpgrppids()` over the tunnel's process group, then
  `PROC_PIDLISTFDS` and `PROC_PIDFDSOCKETINFO`, looking for a TCP listener on `127.0.0.1:N`)
  would prove ownership in the kernel. It is Mac-only code that can't be compiled or tested on
  Linux, it needs a different implementation per OS (`/proc/net/tcp` plus fd inodes on Linux),
  and it polls. It could be added on top later.
- `-E <fifo>` would send ssh's log away from stderr and the banner, but keyboard-interactive
  text would still reach it, and Koan would read two streams.
- A Unix-socket `-D` doesn't exist (`DynamicForward` takes only `[address:]port`).

## The command

```
/bin/sh -c <supervisor> koan-ssh-tunnel \
  /usr/bin/ssh -N -n -T -D 127.0.0.1:<port> \
    -o ExitOnForwardFailure=no -o ClearAllForwardings=no \
    -o ServerAliveInterval=10 -o ServerAliveCountMax=3 \
    -o ConnectTimeout=15 -o ControlMaster=no -o ControlPath=none \
    -o ForkAfterAuthentication=no -o RemoteCommand=none -o RequestTTY=no \
    -o PermitLocalCommand=no -o LogLevel=DEBUG1 [-p port] [-l user] -- <host>
env: inherited (SSH_AUTH_SOCK, HOME) + SSH_ASKPASS=<dir>/askpass
     SSH_ASKPASS_REQUIRE=force, SSH_ASKPASS_PROMPT unset
     PATH=<inherited, or /usr/bin:/bin:/usr/sbin:/sbin>:/opt/homebrew/bin:/usr/local/bin
     LC_CTYPE=UTF-8 if none of LC_ALL, LC_CTYPE, LANG is set
```

- **Binaries.** Only `/usr/bin/ssh` and `/bin/sh` are used. Each must be a regular executable
  owned by root and writable by nobody else (on macOS both sit on the SIP-protected system
  volume). PATH is never searched for them.
- **PATH.** A GUI app on macOS gets launchd's `/usr/bin:/bin:/usr/sbin:/sbin`, so a
  `ProxyCommand` such as `cloudflared access ssh …` or `tailscale nc %h %p` (installed by Homebrew
  in `/opt/homebrew/bin` on Apple Silicon, `/usr/local/bin` on Intel) would fail with "command not
  found". ssh runs it through the user's shell with this PATH. Koan appends both directories
  after whatever PATH it has (the system's, if none), unless they are already there, so system
  tools keep precedence. Both directories belong to the user, and so does every process that can
  write to them, which is out of scope as for `~/.ssh`. The askpass helper sets its own
  `PATH=/usr/bin:/bin`.
- **Locale.** A GUI app usually has no `LANG`, so ssh's `msetlocale()` (`utf8.c`) leaves the C
  locale, and non-ASCII text (a server's prompt, a name) is shown as `\ooo` escapes. When none of
  `LC_ALL`, `LC_CTYPE` and `LANG` is set (or all are empty), Koan sets `LC_CTYPE=UTF-8`.
  `UTF-8` is a valid macOS `LC_CTYPE` name: `/usr/share/locale/UTF-8/LC_CTYPE` exists, and
  Terminal sets `LC_CTYPE=UTF-8` itself (from macOS's locale layout, not checked on the bench;
  Linux has no such locale, so there the variable does nothing). With `-N` ssh opens no session
  channel, so the `SendEnv LANG LC_*` in macOS's `/etc/ssh/ssh_config` sends nothing to the
  server.
- **Debug output.** At `LogLevel=DEBUG1` ssh prints about 60 lines for a plain login. The tunnel
  reads its two lines (above). `AddSshStderrLine()` drops every `debug1:`/`debug2:`/`debug3:`
  line, plus the verbose notes that `INFO` hides (`Authenticated to …`, `Transferred: …`,
  `Bytes per second: …`), so they can't push real errors out of the 40-line cap or become a
  failure's detail. That way the failure is classified from the same lines as at `INFO`. A
  `debug1:` line can name a `ProxyCommand` (`Executing proxy command: exec …`), paths and the
  server's `Remote:` notes, and it is never kept. No log line has a secret in it: passwords,
  passphrases and keyboard-interactive answers are never logged (`readpass.c` logs only
  `read_passphrase: requested to askpass`, and `sshconnect2.c` logs the number of prompts at
  debug2, never a response).
- **No connection sharing.** A ControlMaster would make this ssh a mux client of another process,
  or a master that outlives it. ForkAfterAuthentication (`-f`) would daemonize out of the process
  group.
- **The user's config** still applies: keys, `IdentityAgent`, `ProxyJump`/`ProxyCommand`
  (their children stay in the group), `User`, `Port`, `HostName`, known_hosts.
  - Accepting a new host key adds it to `~/.ssh/known_hosts`, as in a terminal.
  - A `LocalForward`/`RemoteForward`/`DynamicForward` in the config also applies to Koan's ssh.
    A config's `ClearAllForwardings yes` would clear Koan's `-D` too (`readconf.c
    fill_default_options()` clears the command line's forwards as well), and the tunnel would
    wait forever for a listener; Koan passes `-o ClearAllForwardings=no`, which the command line
    wins. Verified: with `ClearAllForwardings yes` in the config ssh entered its session with no
    listening line; with the option, the listening line came. So the user's forwards stay. With
    `ExitOnForwardFailure=yes`, one that failed (very often the case: a terminal's ssh already
    holds `LocalForward 3000 localhost:3000`) would end the tunnel and make it retry forever.
    So Koan passes `ExitOnForwardFailure=no` and watches only its own `-D` (above). Verified: with
    port 3999 taken and `LocalForward 3999 localhost:22` in the config, ssh printed
    `bind [127.0.0.1]:3999: Address already in use`, went on to
    `Entering interactive session.`, and our port answered `05 00`. The failed user forward's
    line stays in the stderr lines, but it is never read as a port clash
    (`ClassifySshExit()` no longer returns `kPortInUse`; only the tunnel does, by port).
  - A user forward that works is opened by Koan's ssh too, as by any ssh to that host.

## Process lifetime

The supervisor runs `ssh` in the foreground and a watcher in the background:

```sh
exec 3<&0 </dev/null
(IFS= read -r _ <&3; trap '' TERM; kill -TERM 0; sleep 3; kill -KILL 0) 2>/dev/null 4>&- &
exec 3<&-
"$@" 4>&-
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
  group was empty 0.05 s after the pipe closed. A member that ignores SIGTERM (a `ProxyCommand`
  that traps it) gets SIGKILL from the watcher 3 s later (`kSshKillGraceSeconds`): after a
  crash nobody else would send it. The watcher ignores SIGTERM only from then on, so the
  script's own `kill -TERM 0` at ssh's end still ends it. Verified: with such a member, the
  group was empty 3.3 s after the pipe closed.
- **When ssh exits by itself,** the script kills what is left of the group (a stray helper) and
  exits with ssh's status (verified: 255 is passed through).
- **The exit pipe.** fd 4 is the write end of a second pipe, which only the script itself keeps
  (`4>&-` for ssh and for the watcher; ssh's children never see it). It closes exactly when the
  supervisor exits, whatever the rest of the group does. Stderr can't tell: a member that
  ignores SIGTERM and keeps fd 2 holds it open after ssh and the supervisor are gone. Verified:
  with `(trap '' TERM; exec sleep 60) &` as ssh's child, the exit pipe closed 0.01 s after ssh
  exited while stderr stayed open (and without `4>&-`, it stayed open too). Alternatives:
  Chromium has no asynchronous process-exit watcher on POSIX (the kqueue `EVFILT_PROC` uses in
  `base/process/` are blocking waits), polling `WaitForExitWithTimeout(0)` reaps the
  supervisor before its group could be killed, and `waitid(WNOWAIT)` would be untested Mac-only
  behaviour.
- **Koan's own checks.** The group always gets SIGKILL before the supervisor is reaped: the
  group id can't be reused while the supervisor is unreaped.
  - On destruction the io side sends SIGTERM to the group first of all, before it drops the
    askpass server (which answers nothing), then SIGKILL as soon as the exit pipe closes (or
    after 3 s), and only then reaps the supervisor.
  - In normal operation the end of ssh is the exit pipe closing, or EOF on stderr (every member
    holds stderr, the supervisor until it exits; the watcher's goes to /dev/null). The group then
    gets SIGKILL, what ssh printed is read from the stderr pipe to the end, and the supervisor
    is reaped by polling `WaitForExitWithTimeout(0)`, so no thread blocks on `waitpid`. Only then
    is the exit reported.
  - A helper of the group whose request can't be read gets no answer (it might be a password
    prompt), and the group gets SIGTERM.
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
   anything else it exits 1, which Koan never sends: a cancelled secret, an open request at
   teardown and an unreadable request get no answer at all, and ssh is killed instead (see
   "Askpass" above).
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
- **Port clashes** (ssh's bind error for Koan's own port, read while it runs) are retried at
  once, up to 3 times, on a new port.
- **The port can change** between connections. Every attempt asks for the last connected port
  again, which works after a failed attempt, but a restart of a connected tunnel usually gets a
  new one: the old ssh is killed asynchronously and still holds the port when the new attempt
  picks, and even once it's gone, the SOCKS connections it accepted leave the port in
  `TIME_WAIT`, which fails a plain `bind()` of it (verified on Linux; ssh's own bind sets
  `SO_REUSEADDR`, Koan's pick doesn't). Waiting for the old group wouldn't make the port
  stable, so the tunnel doesn't: PR 1 and PR 3 take `status.port` from every `kConnected`.
- **Never retried:** `kHostKeyChanged`, `kHostKeyNotVerified` (rejected, or strict checking),
  `kAuthFailed`, `kCancelled`, `kBadConfig`, `kIpv6Only`, `kSshTooOld` and `kSshNotFound`. Only
  `Reconnect()` (the user) or `Start()` tries again.
- **Sleep, wake and network changes** come through `SshTunnelSystemEvents`, from `PowerMonitor`
  and `NetworkChangeNotifier`.
  - While the Mac sleeps, no attempt starts.
  - On resume, or on a network change (once the new network is up, not on `CONNECTION_NONE`),
    a waiting retry starts at once with the backoff reset.
  - A connected tunnel restarts at once (usually on a new port, above), because its TCP connection is
    probably dead and ServerAlive would need up to 30 s to notice. The exception is a tunnel
    whose connection needed the user's input (password, code, host key, security-key touch):
    restarting it would ask again, so it waits for ServerAlive instead.

## For PR 1 and PR 3

- **Wiring.** One `SshTunnel` per remote Space (or per container), plus one
  `SshTunnelSystemEvents`, all on the UI thread, with one `SshTunnelPlatformPosix` per profile or
  for the whole browser.
  - `OnSshTunnelStatusChanged`: on every `kConnected`, call
    `SetRuntimeProxy(container, status.port)` (the port can differ from the last connection's);
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
- **Apple's ssh messages.** The listening and session lines are checked against OpenSSH 9.9
  sources and 10.4 runs. Apple's `/usr/bin/ssh` is OpenSSH 9.x with Apple's patches. The Mac test
  plan has to confirm a real connection; if Apple's text differed, the tunnel would stay
  `kConnecting` (fail closed) and the unit tests that pin the strings would need updating.
- **A Homebrew ssh** (`/opt/homebrew/bin/ssh`) is ignored on purpose. A pref could allow it later.
- **ServerAlive** is 10 s × 3. It could be shorter for faster detection, at the cost of more
  keepalive traffic.
