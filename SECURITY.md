# Security policy

## Supported versions

Koan hasn't had its first release yet. Until it does, security fixes go to
the `develop` branch only. After v0.1.0, fixes will go to the latest release
and to `develop`.

Koan follows Chromium's release branch for its milestone. A Koan build is
only as secure as the Chromium version it's built on, which is listed in
[`CHROMIUM_VERSION`](CHROMIUM_VERSION): always a release tag, never a main
commit. Point releases (Chrome's weekly security fixes) are taken within
days, and a new milestone within about two weeks of reaching Stable
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

## Koan or Chromium?

Koan adds its own UI and features on top of Chromium. If a vulnerability is
in Chromium itself, which is the case when it reproduces in Chrome or
Chromium too, report it to the
[Chromium security team](https://www.chromium.org/Home/chromium-security/reporting-security-bugs/).
That way every Chromium-based browser gets the fix. You're welcome to let
us know as well, so Koan can pick up the fix quickly.
