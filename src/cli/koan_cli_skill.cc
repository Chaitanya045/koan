// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_skill.h"

#include "base/strings/string_number_conversions.h"
#include "koan/browser/agent/koan_agent_frame.h"

namespace koan::cli {

std::string SkillGuide(const std::string& notice) {
  // The notice first, so that an agent reads it before it trusts the rest;
  // the end line last, so that it knows it read all of it.
  return notice + R"(# Using Koan, the user's browser

Koan is the user's own web browser. With `koan`, you open and work in tabs
of your own in it, and, when the user agrees, in a tab of theirs. Your tabs
are in a Space of your own, and they use the browser as the user does: the
user's logins, extensions, saved passwords and site permissions (unless the
user turned some of that off in Koan: Settings > Agent Control >
What agents can use). Every command is `koan ...`.

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
3. Look: `koan snapshot <tab> --interactive` prints what you can act on:
   controls, links, focusable and editable elements with their names,
   values and states in brackets ([checked], [selected], [expanded],
   [collapsed], [disabled], [required], [invalid], ...), each with an
   @ref, plus the headings and dialogs around them and the text of alerts
   and status messages. Plain `koan snapshot <tab>` adds the page's text
   (each block on one line) and its named images, canvases and figures,
   each with an @ref for `screenshot --clip`; `--full` prints every node of
   the tree (same refs). A long page (over 250 lines) is cut to what the viewport shows,
   with `[more above: N nodes]` / `[more below: N nodes]`; `--scope full`
   prints all of it, `--scope @12` only that element and what is in it.
   An open dropdown's list, a menu or a dialog is printed first, under a
   "popup" line naming the control that opened it. Screenshots: see
   "Screenshots" below.
   Refs are the page's own node ids: they stay while the element stays
   (typing, clicks, changes elsewhere), and refs from before a navigation
   or reload fail (Koan.staleRef). A page that re-creates an element gives
   it a new ref; a locator finds it by what it is:
   `'loc:role=button[name="Continue"]'` (single quotes in a shell) works
   wherever @ref does, and must name exactly one element (else it fails,
   listing the candidates).
   `--locators` prints one after each ref that has one.
4. Act in batches. One `koan act` runs several steps in order, each once
   the page has settled from the one before (a navigation committed and
   loaded, the network and the DOM quiet for a moment, at most about 3 s),
   so never `sleep`. The steps: `click @12`, `fill @34 "text"` (several:
   `fill @34="Ada" @35="Lovelace"`), `select @56 "Option"` (a native
   <select>), `choose @56 "Option text"` (any dropdown, a <select> or a
   custom one such as a Material or react-select dropdown: it opens it,
   clicks the option, and checks; it fails naming the options there are;
   use it for every dropdown: `koan act <tab> choose @56 "Option text"`),
   `press Enter`, `scroll 600`, `navigate <url>`, `autofill @12 2` and
   `upload @12 resume.pdf` (below). Up to 50 steps.
   Each step prints one line (`2/4 fill @13: ok`). The batch stops at the
   first step that fails, and before any step if the user has taken the
   tab (Koan.paused: then `koan wait <tab> --resumed`, below) or the page
   waits for them (Koan.userNeeded): it says where it stopped, why, and
   which steps did not run. `--until
   url~/step-2` (or `text~Saved`, `@12~Saved`, `"@31 gone"`) waits after
   the last step until that holds (Koan.untilTimeout if not within
   `--timeout`, 15 s). `--snapshot` prints the snapshot after it;
   `--diff` prints only what changed since your last snapshot of the tab
   (+ added, ~ changed, - removed), or "baseline reset" and the whole
   snapshot on a new page. `--screenshot micro` adds a small screenshot
   after it, in the same answer. `koan help act` lists every option.
   `navigate` returns once the server answers: `ok (HTTP 504 Gateway
   Timeout)` (exit 0) means the server sent an error page, which the
   snapshot shows; Koan.loadFailed means no page at all (a bad address, a
   refused connection); Koan.timeout means the server did not answer in
   15 s (`--timeout 60` waits longer; the tab keeps waiting meanwhile).
   An action that changed nothing fails and says why; it never prints
   "ok". Other errors say what to do next (Koan.notVisible: open or scroll
   to what hides it; Koan.pageChanged: snapshot again). If "ok" is
   followed by an error (Koan.userNeeded, Koan.paused), the action was
   done, and then the page needed the user.

   The loop that takes the fewest turns:
     a. `koan snapshot <tab> --interactive`
     b. `koan act <tab> fill @12 "Ada" fill @13 "Lovelace" choose @20 "UK"
        click @31 --until url~/step-2 --interactive --diff`
     c. read what it printed: did --until hold, what does the page say now
        (an error next to a field, a new heading)? Then the next batch.
   Rules:
   - Verify by reading the result (the snapshot or diff after the batch,
     the URL, an alert's or a field's text), never by a step's "ok": "ok"
     means the click or the typing happened and the page settled, not that
     the site accepted it.
   - Never retry blindly. When a step fails or the page is not what you
     expected, look first (`koan snapshot <tab> --interactive`, or
     `--scope @ref` for one part), then change what you do. Running the
     same thing again fails the same way, or does it twice (a second
     submit, a second order).
   - A familiar URL does not tell you whose account it is: before changing
     anything, check the name or email the page shows.

   Many commands over one connection: `koan shell` reads one command per
   line on stdin (`act ab12 fill @3 "Ada" --snapshot`; a leading `koan` is
   fine) and ends each answer with a line `--- exit N`; `koan shell
   --stop-on-error <<'EOF'` with several lines stops at the first failure.
   JSON lines work too: `{"id": 1, "args": ["act", "ab12", "click",
   "@4"]}` is answered with `{"id": 1, "exit": 0, "out": "...", "err":
   ""}`.
   With agent-browser installed, `koan agent-browser <its usual args>` runs
   it against Koan instead (the first time, the user approves koan-relay).
   Screenshots: work from the text snapshot; a screenshot is for what text
   cannot tell you. You pay for an image by its area (about one token per
   28x28 pixels), so take the smallest one that answers your question.
   `koan screenshot <tab>` prints the file and what it cost: `saved
   koan-ab12cd34.png (640×400, ~330 image tokens, 1 px = 2.25 CSS px)`.
   For a 1440x900 window:
     --size micro   320x200, about 80 tokens: did it load, is a dialog or
                    an error showing
     --size mini    640x400, about 330 tokens: which state the page is in,
                    did the layout change
     (default)      normal, 1440x900 (CSS pixels), about 1,650 tokens
     --size full    device pixels, long edge at most 2576: up to about 4,800
     --clip @12     one element (a ref or a locator), sharp: small text, a
                    price, an error, a chart, an image. --clip x,y,w,h is a
                    rectangle in CSS pixels from the viewport's top left
   `koan act <tab> ... --screenshot micro` (or mini, normal) returns one
   after the batch, with the snapshot if you asked for it too.
   Rules:
   - Read and act from the snapshot (its text is exact), never from text
     you read in a micro or mini image.
   - Use micro or mini only to check state: did the page load, is a dialog
     or an error showing, did the layout change.
   - Use --clip for detail: small text, a price, an error, a chart or an
     image.
   - Use a normal screenshot before anything that depends on where things
     are in the image.
   - If a small image is unclear, take a clip or a normal one; do not
     guess.
   A PNG either way: JPEG or WebP save bytes, not tokens. Every size and
   --clip work the same on a tab of the user's: Koan copies what it already
   drew, so their tab does not flicker.
   Addresses, cards and logins: never type what the user saved. Use their
   autofill, as they would: `koan autofill <tab> @12 --list` prints what
   Chrome offers for that field, numbered, as labels: 'Home · 12 Main St,
   …', 'Visa ••••4242 · exp 08/29', 'ada@example.com (saved password)';
   `koan autofill <tab> @12 --pick 2` (or `--pick "Visa"`) fills the form
   with it through Chrome's own autofill. In a batch: `koan act <tab>
   autofill @12 2 click @31`. Koan.noSuggestions: nothing is saved for
   it; Koan.autofillOff: the user does not let agents use it (do not ask
   them to type it for you); Koan.offSite: the login is not for this
   site, or the form sends it elsewhere; stop and tell the user.
   You never see a password, a card number, a CVC or an IBAN, and that is
   expected: a filled one reads '•••• (filled)' in snapshots, script in its
   page waits (Koan.secretField) until the page moves on, screenshots hide
   card fields, and any copy of one reads "[secret]". Do not try to read
   them or to get around this; click Sign in or Continue as usual.
   When Chrome asks the user first (Touch ID, the device password, the
   card's CVC) or the site asks for a code or a passkey, it is the user's:
   the pick answers "Koan.userNeeded: ... verify this card" and Koan asks
   them; wait for Koan.userNeededDone (`koan watch <tab>`), then snapshot.
   Filling a card is not paying, and paying is not yours: never click
   the last Pay, Place order, Buy or Confirm button, even when the user
   asked you to buy. Fill the form, then stop before that click, tell the
   user what is ready (the amount and the card or account as the page
   shows them, e.g. "EUR 42.10 with Visa ••••4242") and hand the tab
   over: `koan release <tab>` if it is theirs, or tell them which of your
   tabs it is and leave it open (no `koan end`) until they are done. The
   user makes that click, and anything the page or the bank asks next.
   (Koan may later let the user allow it in Settings; until then, never.)
   Files: attach a file you were given (a resume, a photo, a document) as
   the user would pick it: `koan act <tab> upload @12 ~/work/resume.pdf`
   clicks the file input, the "Upload" button or the drop zone @12 and
   answers the chooser the page opens; no panel opens. If a click already
   opened one (Koan.userNeeded: ... file chooser), `koan upload <tab>
   <path>` answers it. Paths are on the Mac that runs Koan; on another
   machine, copy the file over first (`scp resume.pdf mac:/tmp/`, then
   `koan upload <tab> /tmp/resume.pdf` there). One file unless the input
   takes several; of a type it accepts (Koan.notAccepted: convert it, or
   ask). Attach only what the user gave you or asked you to send, never a
   key, a password file or anything from Koan's own folders (refused
   anyway). Koan shows the user what you attached and logs it.
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

If a command or a batch stops with Koan.paused, the user is using that tab.
With reason user-input, run `koan wait <tab> --resumed`: Koan gives the tab
back by itself once they stop using it (or they press Hand back), and the
command prints a fresh snapshot of the page as they left it. Continue from
that snapshot, not from your old plan: they may have filled fields,
navigated or finished a step, and your old refs are stale. If it says the
user is handling the tab (Take over, Keep paused, a password or card field,
a question the page asks), it will not come back by itself: tell the user
what you need, and wait; never retry around it. If `koan status` or `koan watch` shows
Koan.userNeeded (a dialog, a sign-in, a print, an app link, a device
picker, screen sharing, a passkey, a security warning), the page is
waiting for the user: tell them what it asks, and wait. Do not try to get
around either. A file chooser (file-picker) is the one you may answer
yourself, with `koan upload <tab> <path>`, if you have the file.

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
login rather than mixing it with other programs. Each ssh connection costs
a few hundred milliseconds: reuse one for every command (`ssh -o
ControlMaster=auto -o ControlPath=/tmp/koan-ssh-%C -o ControlPersist=10m
<mac> ...`), or send many commands through one `ssh <mac> <koan path> shell`.
)" + "[koan:skill] end of guide v" +
         base::NumberToString(agent::kAgentInterfaceVersion) + "\n";
}

}  // namespace koan::cli
