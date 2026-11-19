// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_skill.h"

namespace koan::cli {

std::string SkillGuide() {
  return R"(# Using Koan, the user's browser

Koan is the user's own web browser. With `koan`, you open and work in tabs
of your own in it, and, when the user agrees, in a tab of theirs. Your tabs
are in a Space of your own, and they use the browser as the user does: the
user's logins, extensions, saved passwords and site permissions (unless the
user turned some of that off in Koan: Settings > Agent Control > What agents
can use). Every command is `koan ...`.

Never use AppleScript ("Allow JavaScript from Apple Events"), a remote
debugging port, or another browser to get at the user's pages: `koan` is
the way in, and it is the one the user approved.

1. `koan status`. If it says "paired: no", run `koan pair --label "<your
   name>"` and tell the user to approve it in Koan (a dialog there); wait
   for it. A plain `koan pair` asks for everything an agent can do (its own
   tabs, listing the user's tabs, looking at and controlling the tab they
   give you, keeping the display awake); the user can untick any of it.
   Never ask the user for keys and never read or edit ~/.koan: `koan` looks
   after it (a pairing Koan has forgotten is removed, and the newest pairing
   is used, so you never need --client unless several agents share it).
2. `koan open <url>` opens a background tab and prints its id (the first 4+
   characters of it are enough later). `--task <name>` groups tabs into a
   task; `--saved <name>` uses a saved container the user allowed you (it
   keeps its own logins). If that is refused (Koan.notGranted), tell the
   user: only they can allow it, in Settings > Agent Control.
3. Look: `koan snapshot <tab>` prints the page as a compact outline:
   controls, links, landmarks and headings with @refs, each block's text on
   one line, states in brackets ([checked], [selected], [expanded],
   [collapsed], [disabled], [required], ...). `--full` prints every node of
   the tree (same refs) when you need more. `koan screenshot <tab> -o
   shot.png` saves what it shows.
4. Act: `koan act <tab> click @12`, `koan act <tab> fill @34 "text"`,
   `koan act <tab> select @56 "Option label or value"` (a dropdown:
   <select>; fill on one does the same), `koan act <tab> press Enter`,
   `koan act <tab> scroll 600`, `koan act <tab> navigate <url>`. An action
   that changed nothing fails and says why; it never prints "ok". Take a new
   snapshot after each change: refs belong to the page as it was, and a ref
   from before a navigation or reload fails with Koan.staleRef (take a new
   snapshot and use its refs). Other errors say what to do next
   (Koan.notVisible: open or scroll to what hides it; Koan.pageChanged:
   snapshot again).
   With agent-browser installed, `koan agent-browser <its usual args>` runs
   it against Koan instead (the first time, the user approves koan-relay).
5. A tab of the user's (to work on a page they have open, signed in): tell
   the user to bring that tab to the front in Koan, then run
   `koan take-over`. Koan asks them; once they allow it, you get the tab
   that is in front at that moment, and it prints its id. Each take-over asks
   again. If it fails with Koan.notPermitted (capability drive), the user
   unticked it when you paired: pair again with a plain `koan pair` and ask
   them to leave it on (Settings has no switch for it). `koan release <tab>`
   gives it back when you are done.
6. When you are done: `koan end` closes your tabs and clears what they
   stored. `koan end --task <name>` ends one task.

If koan says Koan asks the user to approve it again (it was started by
another program than the one that paired), tell the user to look in Koan,
and wait; or run koan from the agent that paired.

If a command fails with Koan.paused, the user is using that tab: stop and
wait for them to hand it back. If `koan status` or `koan watch` shows
Koan.userNeeded (a dialog, a sign-in, a print, an app link, a file or
device picker, screen sharing, a passkey, a security warning), the page is
waiting for the user: tell them what it asks, and wait. Do not try to get
around either.

Your tabs stay open between commands for as long as your task lasts (30
minutes without a command ends it). Some agents sandbox shell commands:
`koan` needs your home directory (~/.koan) and the socket in Koan's profile
directory (~/Library/Application Support/Koan/AgentControl).

## If `koan` is not found

Run it by its full path, inside the app:
`~/Applications/Koan.app/Contents/Resources/bin/koan` or
`/Applications/Koan.app/Contents/Resources/bin/koan`. Settings > Agent
Control > "Set up agents on this Mac" links it as ~/.local/bin/koan.

## From another machine (you run on a server, the user's Koan is on their Mac)

Run `koan` on the Mac over ssh, by its full path (a non-interactive ssh
shell may not have ~/.local/bin on its PATH), for example
`ssh <mac> ~/Applications/Koan.app/Contents/Resources/bin/koan status`.
Pair the same way, over ssh; the user approves in Koan on the Mac. Files
are on the Mac: `koan screenshot <tab> -o /tmp/shot.png` saves the PNG
there, so copy it back (`scp <mac>:/tmp/shot.png .`) to look at it. Every
command of a task must come from the same agent: keep using the same ssh
login rather than mixing it with other programs.
)";
}

}  // namespace koan::cli
