# Security policy

## Supported versions

Koan hasn't had its first release yet. Until it does, security fixes go to
the `develop` branch only. After v0.1.0, fixes will go to the latest release
and to `develop`.

Koan follows Chromium's stable releases. A Koan build is only as secure as
the Chromium version it's built on, which is listed in
[`CHROMIUM_VERSION`](CHROMIUM_VERSION). Keeping that version current is part
of Koan's release process.

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

## Koan or Chromium?

Koan adds its own UI and features on top of Chromium. If a vulnerability is
in Chromium itself, which is the case when it reproduces in Chrome or
Chromium too, report it to the
[Chromium security team](https://www.chromium.org/Home/chromium-security/reporting-security-bugs/).
That way every Chromium-based browser gets the fix. You're welcome to let
us know as well, so Koan can pick up the fix quickly.
