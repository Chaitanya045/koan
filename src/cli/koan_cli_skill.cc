// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_skill.h"

namespace koan::cli {

std::string SkillGuide() {
  return R"(# Using Koan, the user's browser

You can open and work in tabs of your own in Koan, in a Space and a container
of your own: your tabs never see the user's logins, and the user's tabs are
not yours unless they give you one. Every command is `koan ...`.

1. `koan status`. If it says "paired: no", run `koan pair --label "<your
   name>"` and tell the user to approve it in Koan; wait for it. Never ask the
   user for keys and never read ~/.koan.
2. `koan open <url>` opens a background tab and prints its id (the first 4+
   characters of it are enough later). `--task <name>` groups tabs into a
   task; `--saved <name>` uses a saved container the user allowed you (it
   keeps logins). If that is refused (Koan.notGranted), tell the user: only
   they can allow it, in Settings > Agent Control.
3. Look: `koan snapshot <tab>` prints the page as an outline with @refs;
   `koan screenshot <tab> -o shot.png` saves what it shows.
4. Act: `koan act <tab> click @12`, `koan act <tab> fill @34 "text"`,
   `koan act <tab> press Enter`, `koan act <tab> scroll 600`,
   `koan act <tab> navigate <url>`. Take a new snapshot after each change:
   refs belong to the page as it was.
   With agent-browser installed, `koan agent-browser <its usual args>` runs
   it against Koan instead (the first time, the user approves koan-relay).
5. A tab of the user's: `koan take-over` asks them for the tab they are on
   (only its site); `koan release <tab>` gives it back when you are done.
6. When you are done: `koan end` closes your tabs and clears what they
   stored. `koan end --task <name>` ends one task.

If a command fails with Koan.paused, the user is using that tab: stop and
wait for them to hand it back. If `koan status` or `koan watch` shows
Koan.userNeeded (a dialog, a sign-in, a print, an app link, a security
warning), the page is waiting for the user: tell them what it asks, and
wait. Do not try to get around either.

Your tabs stay open between commands for as long as your task lasts (30
minutes without a command ends it). Some agents sandbox shell commands:
`koan` needs your home directory (~/.koan) and the socket in Koan's profile
directory (~/Library/Application Support/Koan/AgentControl).
)";
}

}  // namespace koan::cli
