# Security policy

## Supported versions

Koan is in alpha (the first release, 0.0.1-alpha.1, came out on
2026-11-20). Only the latest release on the
[Releases page](https://github.com/Chaitanya045/koan/releases) is
supported: security fixes go to `develop` and ship in the next alpha or
nightly. There's no automatic updater yet, so install new versions by hand.
While Koan is in alpha, Chromium's security fixes are applied on a
best-effort basis, not on Chrome's schedule
([docs/RELEASING.md](docs/RELEASING.md)).

Koan follows Chromium's release branch for its milestone. A Koan build is
only as secure as the Chromium version it's built on, which is listed in
[`CHROMIUM_VERSION`](CHROMIUM_VERSION): always a release tag, never a main
commit. The aim is to take point releases (Chrome's weekly security fixes)
within days, and a new milestone within about two weeks of reaching Stable
([docs/REBASE.md](docs/REBASE.md#following-chromium-releases)). A scheduled
workflow watches for them and keeps one tracking issue up to date.

## Reporting a vulnerability

Please don't report security vulnerabilities in public issues, discussions
or pull requests.

Report them privately through GitHub instead:
[**Report a vulnerability**](https://github.com/Chaitanya045/koan/security/advisories/new)
(Security tab → Advisories → Report a vulnerability). Include:

- what the vulnerability is and what an attacker could do with it;
- the steps to reproduce it, with a proof of concept if you have one;
- the Koan commit or version, and the Chromium version
  (`chrome://version`);
- whether it still happens with the related Koan feature switched off in
  `chrome://flags`.

You can expect an acknowledgement within a few days. We'll keep you
informed while we investigate and fix it, and we'll credit you in the
advisory unless you'd rather stay anonymous.

## Remote debugging

Koan, like Chrome, refuses `--remote-debugging-port` and
`--remote-debugging-pipe` when it runs on its default user data directory
(`~/Library/Application Support/Koan`); it prints "DevTools remote debugging
requires a non-default data directory" and starts no server. Without that,
any local process could launch Koan with a debugging port against your real
profile and control the browser, including your signed-in sessions. (This
applies only when Koan isn't already running: a second launch hands its
command line to the running instance, which ignores the switch.) Pass a
different `--user-data-dir` for debugging and automation (a separate, empty
profile).

This is a speed bump, not a boundary:

- a process running as you can still copy the profile, or start Koan with
  another `--user-data-dir`;
- the check compares the directory as a string. On macOS's case-insensitive
  APFS, `--user-data-dir` spelled in another case
  (`.../library/application support/koan`) names the same directory but isn't
  recognised as the default one, so it can bypass the check. Chrome has the
  same gap. (`scripts/mac/remote_debugging.sh` leg C reports whether it does
  on the test Mac.)

It stops the silent, one-flag attack, not malware that already runs as you.
Chrome's `chrome://inspect#remote-debugging` switch (a prompt for each
connection) works as in Chromium.

## What agents can use

Agents you approve (Settings > Agents) use the browser as you do, unless you
turn a part off in **What agents can use**: your logins, saved passwords,
addresses and cards, your extensions and history are all **on** by default.
Each switch is read by every block it controls; with it off the block applies
in agent tabs (`.agent/notes/agent-control.md` sections 7.0 and 7.2).

What that means, plainly:

- **Your logins** on: an agent's tabs share your cookies and sessions, so the
  agent acts on sites as you. It can read what those pages show you. Your
  cookies and sign-in tokens are still never sent to the agent program (Koan's
  scrubber drops them from what it reads), but a page signed in as you is
  yours to the agent.
- **Saved passwords** and **Addresses and cards** on: an agent uses them the
  way you do, through Chrome's own autofill (`koan autofill`): it sees what the
  dropdown shows you ("Visa ••••4242", "you@example.com (saved password)"),
  picks one, and Chrome fills the form. It never reads the secrets it fills:
  passwords, card numbers, CVCs and IBANs. A filled one reads "•••• (filled)"
  to the agent, script in its page is refused until the page moves on or the
  field is cleared, screenshots paint over filled card fields, and Koan
  replaces any copy of a value it filled (in a page's text, a console message,
  a request) with "[secret]" in everything sent to the agent. Nothing is
  filled when an agent's page loads, and silent sign-in
  (`navigator.credentials.get`) is off in agent tabs, so Koan knows every
  password it filled. A password or card number Koan did not fill (one the
  password manager filled in your own tab, or one you typed before you
  handed the tab back) is treated the same way: Koan reads those fields
  itself, in a world neither the page nor the agent sees, locks script and
  scrubs the value. Known limit there: a value a page puts into a field by
  script, after the last input Koan saw, is found only at the agent's next
  script read. A login is filled only on its own site, into a form that
  sends it there. When Chrome would ask you first (Touch ID, your device
  password, a card's CVC), it still asks you: the agent waits until you verify
  in Koan. Known limits: the page itself can read what is filled (it has to,
  to send it), so a malicious page could show it in a way Koan doesn't
  recognise (an image, an encoding Koan doesn't look for); values shorter than
  6 characters (a CVC) are kept out by the field lock and the snapshot
  redaction, not by the scrubber; and a password an agent types itself is the
  agent's.
- **Your extensions** off while **Your logins** is on: agent tabs share your
  tabs' renderer processes, so Koan can't stop your extensions' content scripts
  there. It still keeps extensions from finding agent tabs by id, attaching
  the debugger to them, watching their requests and getting their navigation
  events.
- Your own extensions with the "tabs" permission can always see agent tabs'
  URLs and titles (Chromium has no way to hide a tab from them).

Whatever the switches say: every agent program needs your approval, the pill
shows it and Stop ends it, the agent can't act on a security warning page,
dialogs and prompts a page raises wait for you instead of popping up over your
work (so do file pickers, device choosers, screen-share pickers and passkey or
security key requests, which agent containers never get at all; passkey
autofill and the passkey requests a page makes without asking, such as
saving one after a password sign-in, stay refused), and agent tabs never get
File System Access pickers, or add search engines, protocol handlers or app
badges. A site
permission an agent tab asks for is prompted only when you open that tab.

With **Your logins** off, agents work in **containers**: storage partitions of
their own inside your profile, so their cookies, storage and cache are never
yours. They are throwaway (cleared when the task ends) unless you saved one for
an agent, and there Koan also refuses every site permission. **Strict** mode
gives each task a hidden profile of its own, sharing none of your settings,
extensions, history or permissions.

The "Your tab" mode is different: there the agent drives one of your own tabs,
after you approve that tab. By default it keeps control wherever the tab goes
(other sites, redirects, frames of other sites in it), until you use the tab,
take it back or press Stop; privileged pages and security warnings stay out of
its reach. With **Limit a taken-over tab to its site** on, it may act only on
the site you approved (frames from other sites are cut off, and leaving the
site leaves it able only to read the tab, with password fields hidden). While
it holds the tab, your saved passwords and addresses are filled there only if
those switches are on.

`koan agent-browser` connects agent-browser through `koan-relay`, a separate
program you approve like any agent. The relay listens on 127.0.0.1 only, on a
random port, behind a random secret kept in a file only you can read
(`~/.koan/relay/`). Known limit: TCP can't tell which program connects, so any
program running as you that reads that file can use the relay's session,
within what you approved for the relay. One connection at a time, and the
relay ends after 15 minutes without use or when you stop agents in Koan.

## Koan or Chromium?

Koan adds its own UI and features on top of Chromium. If a vulnerability is
in Chromium itself, which is the case when it reproduces in Chrome or
Chromium too, report it to the
[Chromium security team](https://www.chromium.org/Home/chromium-security/reporting-security-bugs/).
That way every Chromium-based browser gets the fix. You're welcome to let
us know as well, so Koan can pick up the fix quickly.
