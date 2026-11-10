# Building Koan on your Mac

Koan has no download yet, so you build it yourself, and one command does all
of it. It takes a long time the first time (mostly waiting), and after that
an update is the same command again.

```sh
git clone https://github.com/Chaitanya045/koan.git ~/src/koan
cd ~/src/koan
scripts/build.sh
```

When it finishes, Koan is in `~/Applications/Koan.app`. Open it from Finder,
Spotlight or Launchpad.

## What you need

| | |
|---|---|
| **Mac** | Apple Silicon (M1 or later). Intel Macs are not supported. |
| **macOS** | 15.6 or later; tested on macOS 27. |
| **Xcode** | The full Xcode from the App Store, 26 or later (tested with 27.1). The Command Line Tools alone are not enough. Open Xcode once after installing it and accept the licence. |
| **Metal toolchain** | Since Xcode 26 it is a separate download (about 700 MB). Chromium's graphics layer (ANGLE) needs it: `xcodebuild -downloadComponent MetalToolchain` |
| **Disk** | About **100 GB free**: ~31 GB for Chromium's source, ~15 GB for the build, plus room for updates. |
| **RAM** | 16 GB recommended. 8 GB works, slowly. |
| **Network** | The first run downloads about 30 GB. |

You don't need Homebrew, Python packages or anything else: Chromium's own
tools (depot_tools) are fetched into the clone.

If Xcode is not at `/Applications/Xcode.app` (for example
`Xcode-beta.app`), tell the scripts where it is:

```sh
export DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer
```

Check everything before you start (it changes nothing):

```sh
scripts/build.sh --check
```

Each line says `ok`, `warn` (it will work, but read why) or `FAIL` (with what
to do about it). `scripts/build.sh` runs the same checks first and stops on a
`FAIL`.

## What the command does

`scripts/build.sh` goes through these steps and skips each one that is
already done:

1. **Checks** the prerequisites above.
2. **Fetches** depot_tools and Chromium at the version in
   [`CHROMIUM_VERSION`](../CHROMIUM_VERSION), with only what a Mac build
   needs (`scripts/bootstrap.sh`).
3. **Applies** Koan's patches and branding to that Chromium.
4. **Configures** a release build (`chromium/src/out/Koan/args.gn`, from
   [`build/koan-mac.gn`](../build/koan-mac.gn): optimised, no debug
   symbols).
5. **Builds** Koan.app, at a pace that keeps your Mac usable, and keeps it
   from sleeping while it builds (the display can still turn off).
6. **Installs** it: copies `Koan.app` to `~/Applications`.

Everything it writes stays inside the clone: Chromium in `chromium/`,
depot_tools in `depot_tools/`, the tool caches in `.cache/mac/`, and a log of
every run in `logs/`. Nothing goes into your home folder except the installed
app (and, once you use Koan, its profile, like any browser).

### Interrupted? Run it again

Closing the lid, Ctrl-C, a dropped connection or a restart doesn't lose
anything. Run `scripts/build.sh` again and it carries on where it stopped:
the download resumes and the build continues from the last finished file.

### Faster, or quieter

By default it uses 2–6 parallel jobs depending on how much memory is free,
and runs them at low priority, so you can keep working (it will still be
warm and loud).

```sh
scripts/build.sh --fast        # every core, normal priority: the Mac is busy
JOBS=3 scripts/build.sh        # exactly 3 jobs
scripts/build.sh --no-install  # build, but don't copy to ~/Applications
scripts/build.sh --dry-run     # show what it would do, do nothing
scripts/build.sh --help        # all the options
```

Don't use `--fast` on a 16 GB Mac while you work: a Chromium build at full
speed can make it unresponsive.

## How long it takes

| | MacBook Air | MacBook Pro (Pro/Max chip) |
|---|---|---|
| First download (~30 GB) | 1–3 hours, mostly your connection | same |
| First build | a working day or a night, roughly 8–12 hours | roughly 3–5 hours with `--fast` |
| Update with a small Koan change | minutes to an hour | less |
| Update to a new Chromium version | most of a first build | most of a first build |

These are estimates; tell us your times. Every build links the whole browser
again, so even a one-line change takes a few minutes.

## Updating

```sh
cd ~/src/koan
git pull && scripts/build.sh
```

That is all, including when the pull moves Koan to a new Chromium version:
the script notices that `CHROMIUM_VERSION` changed, moves the checkout to the
new version (`scripts/update_chromium.sh`), re-applies Koan and rebuilds. A
new Chromium version rebuilds most of the tree, so it takes hours, like the
first build.

Quit Koan before the install step; if it is running, the script stops before
copying and asks you to quit it and run the command again (nothing is
rebuilt).

## Troubleshooting

Every run writes `logs/build-<date>-<time>.log`. When asking for help, send
the end of the newest one.

**`FAIL  Xcode: no full Xcode`.** Install Xcode from the App Store and open it
once. If it lives somewhere other than `/Applications/Xcode.app`, set
`DEVELOPER_DIR` (see above). `xcode-select` doesn't matter: the scripts use
`DEVELOPER_DIR`.

**`FAIL  Xcode licence`.** `sudo xcodebuild -license accept`, or open Xcode
and agree.

**`missing Metal Toolchain` or `'metallib' not found`, or the build stops at
`angle_metal_internal_shaders_to_air`.** Install the toolchain:
`xcodebuild -downloadComponent MetalToolchain`. Check it with
`xcodebuild -showComponent MetalToolchain`. If it is installed and the error
stays: the scripts point Apple's tools at a home folder inside the clone
(`CFFIXED_USER_HOME`, so they leave your home folder alone), and under it
`xcrun` cannot find the Metal toolchain. The build works around that by
linking the toolchain into `out/Koan/metal-toolchain` each run; if you ran
`gn gen` or `autoninja` by hand, run `scripts/build.sh` instead so the link is
refreshed (its target moves on every restart).

**The download fails (`HTTP 500`, `RPC failed`, timeouts).** Google's source
servers sometimes refuse a request. The script tries three times; if it
still fails, run `scripts/build.sh` again later. It resumes.

**`chromium/src has changes that are not Koan's patches`.** You (or a tool)
edited a Chromium file. Save your change elsewhere if you want it, then
`git -C chromium/src checkout -- <file>` and run the build again.

**`Some patches don't apply`.** The Koan you pulled doesn't match its own
Chromium version. If you didn't change anything, it is our bug: open an
issue with the log, and meanwhile go back to the last version that worked
(`git checkout <previous commit> && scripts/build.sh`).

**Keychain prompt: "Koan wants to use your confidential information stored in
'Koan Safe Storage'".** Koan keeps the key that encrypts your saved
passwords and cookies in your login keychain, like Chrome does. Your build
is signed only locally (ad hoc), and each build has a new signature, so macOS
asks again after every update. Enter your Mac password and click **Always
Allow** (it holds until the next update). If you click Deny, Koan still
works, but warns that it can't encrypt your data.

**"Koan can't be opened" / Gatekeeper.** Shouldn't happen: an app you built
yourself isn't quarantined, so macOS opens it without a warning. If you
copied `Koan.app` from another Mac, that copy is quarantined; build it on
this Mac instead.

**The Mac is slow or hot while building.** That's expected. Use a lower
`JOBS` (`JOBS=2 scripts/build.sh`), or let it build overnight. Stopping it is
safe (Ctrl-C), and running it again resumes.

**Out of disk space.** `out/Koan` can be deleted (`rm -rf
chromium/src/out/Koan`) and rebuilt; `.cache/mac` (tool caches) and `logs/`
can be deleted too. Chromium's source (`chromium/`) is what takes the most
room, and is needed.

## Removing Koan

Delete `~/Applications/Koan.app` and the clone. Koan's profile (history,
bookmarks, logins) is in `~/Library/Application Support/Koan`; delete that
too if you don't want to keep it, and the "Koan Safe Storage" item in
Keychain Access.
