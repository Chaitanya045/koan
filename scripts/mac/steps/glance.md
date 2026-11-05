# Glance: commands and teardown on the Mac (PR #165)

These are the Mac steps for audit 3 GL2/GL4 (`.agent/notes/upstream/split-glance-library-media.md` §2.5: GT4, GT8, GT13, GT14) and for PR #165's review findings.

Run the test Koan with `--vmodule=koan_glance_controller=1`: it logs `Koan Glance: open`, `closing`, `torn down`, `the page becomes a tab` and `tab strip change …`, which say why a card went.

The bench rules apply to every run: the screen is granted, the frontmost pid is the test Koan's, and the run uses a fresh `pass-` profile, a DCHECK build and logs captured from the terminal.

**Scripted:** `scripts/mac/glance/glance.mjs` runs steps 1–10 and 13 (the first part). It uses the DevTools protocol and UI DevTools for the checks. Real keys and menu clicks go through the bench tools in `scripts/mac/tools/` (`keypress2`, `inputok.sh`, `frontpid.sh` and `grantok.sh`; the same files as in #155). Compile the `.m` files on the Mac (`clang -framework Cocoa -o keypress2 keypress2.m`) and copy them to `TOOLS`. Those tools write and read the bench's fixed paths under `~/work/koan-mac/tmp`. Steps 11, 12, 14 and 15 stay manual.

```
scripts/mac/glance/serve.sh &        # on the Mac: the parent on 127.0.0.1:8761, the Glance pages on localhost:8762
CDP_PORT=9341 UI_PORT=9340 PID=<test Koan pid> \
  MAC='ssh -S /tmp/koan-cm.sock chaitanya@<mac>' TOOLS='~/work/koan-mac/tmp' \
  node scripts/mac/glance/glance.mjs            # or: ... glance.mjs 3 5 8
```

`MAC` is empty when the script runs on the Mac itself. Without `PID`, keys go through CDP. That is not conclusive on macOS, because CDP keys don't reach the menu bar's key equivalents, and the menu steps are skipped. Each run prints `PASS` / `FAIL` lines and ends `ALL PASSED` or `N FAILED`.

Open `http://127.0.0.1:8761/parent.html` in the test Koan. Every Glance below is opened by ⌥-clicking a link of that page. The script uses CDP mouse events, which count as trusted input; by hand, use `ptr altclick`. The Glance is closed again afterwards (Esc) unless the step closes it.

The checks the harness reads:

| What | How |
|---|---|
| The card is open | UI DevTools: a shown `KoanGlanceView` (as `scripts/e2e/glance.mjs` `glanceOpen()`). |
| The Glance page | The CDP page target whose URL is on `localhost:8762`. It is not a tab. |
| Tabs | CDP page targets on `127.0.0.1:8761`, or the sidebar's tab rows in UI DevTools. |
| A page reloaded | `performance.timeOrigin` changed (CDP `Runtime.evaluate` in that target). The fixtures also count loads: `#loads`. |
| Zoom | `window.devicePixelRatio` in the target (it scales with page zoom). |
| Find bar | UI DevTools: no shown `FindBarView`. |
| Menu bar items | `osascript -e 'tell application "System Events" to tell (first process whose unix id is <pid>) to click menu item "Close Tab" of menu "File" of menu bar 1'` (likewise View > Reload This Page, Edit > Find > Find…). Read an item's state with `get enabled of menu item ...`. |
| Logs | No `:FATAL:`, no `Check failed`, no crash report for the test pid. |

## Steps

Keys are posted with `scripts/mac/tools/keypress2 <pid> <keycode> c` (⌘), the pointer with `ptr` (both compiled on the Mac). The keycodes are: W 13, R 15, F 3, L 37, [ 33, ] 30, = 24, - 27, 0 29, C 8 (with `cs` for ⌘⇧C), Esc 53.

1. **GT4 ⌘W by key.** Glance `#child`, then ⌘W. The card is gone, and the tab count and the parent's URL are unchanged. Open the Glance again, then press ⌘W twice within 100 ms (during the close animation): only the Glance closes. ⌘W once more, after the card is gone, closes the parent tab. *macOS:* the card doesn't animate (it closes at once), so there is no "during the close"; a second ⌘W closes the tab, as it should (the script skips this part there).
2. **GT4 File > Close Tab.** Glance `#child`, then File > Close Tab from the menu bar: only the Glance closes.
3. **⌘W with a page that eats keys.** Glance `#keeper`, then ⌘W: the card closes (reserved key, `PreHandleKeyboardEvent`). The page may show "saw w" first; that's fine. **This step decides review 2's finding 6.** On macOS the reservation works only while the browser window is the key NSWindow; the Glance's child window has no command dispatcher. If the card stays open, report it together with which window was key: the fix is a dispatcher for the Glance's NSWindow that asks `IsReservedCommandOrKey`.
4. **Reload.** Glance `#child`, then ⌘R: the Glance target's `timeOrigin` changes and `#loads` shows 2. The parent's `#loads` is unchanged. Repeat with View > Reload This Page.
5. **Back/Forward follow the Glance (review finding 1).** Use a fresh parent tab with no history: ⌘T, open `parent.html` from the URL bar, then reload it once so it has no back entry. Glance `#child`, then click `#next` inside the card, which needs a real click (`ptr click`).
   - Before ⌘[: the toolbar/sidebar Back is enabled.
   - ⌘[: the Glance shows `child.html` and the parent's URL is unchanged. ⌘] goes forward again.
   - **Background noise:** a second tab, opened in the background *before* the Glance (CDP `Target.createTarget` with `background: true`; a click on the parent page while the card is open would close the card). With the card up, reload that tab over CDP (`Runtime.evaluate('location.reload()')` in its target). Then ⌘[ in the Glance must still go back. This is the case review 1 found broken.
   - Esc: Back is disabled again (the fresh tab can't go back).
6. **Zoom.** Glance `#child`, then ⌘= twice. The Glance target's `devicePixelRatio` grows, and the parent's is unchanged. ⌘0 resets it (this is the bug's other case: the parent at 100% used to disable Actual Size). ⌘- shrinks it. Reset with ⌘0 before going on.
7. **Find is off in a Glance (review finding 5).** Glance `#child`. Edit > Find > Find… is disabled (`get enabled`), and ⌘F opens no find bar. After Esc, Find is enabled again and ⌘F opens the parent's find bar (then Esc).
8. **⌘L edits the Glance URL in place (user decision).**
   - First, typing: Glance `#child`, click its field, type `2`. The field gets it (not the parent page). The script also logs `document.hasFocus()` of both pages: on macOS the card's window never becomes key (a `TYPE_CONTROL` widget), so this decides how keys reach a Glance page there.
   - Glance `#child`, then ⌘L. The URL bar opens **in the sidebar pill**, not floating, with `http://localhost:8762/child.html` selected (UI DevTools: the omnibox text).
   - Type `localhost:8762/child2.html`, then Enter. The Glance now shows child2. The card is still a Glance (no new tab), and the parent's URL is unchanged.
   - ⌘L again, then Esc: the bar closes, the card stays, and the Glance page has focus again: a second Esc closes the card.
   - ⌘L, type `chrome://version`, then Enter: the card closes and the tab shows chrome://version. (Go back afterwards.)
   - *Live check:* compact mode with the sidebar hidden (Koan's compact-mode key: Ctrl+S on this base, ⌘S once #90's shortcuts land; ⌘S is Save Page here): ⌘L in a Glance floats the bar. Report whether it is drawn above or under the card.
9. **⌘⇧C copies the Glance URL.** Glance `#child`, then ⌘⇧C. The clipboard (`pbpaste`) has `http://localhost:8762/child.html` and the toast shows. After Esc, ⌘⇧C copies the parent's URL.
10. **POST reload (no resubmit).** Glance `#form`, click "POST it" (real click), then ⌘R. Nothing reloads, no dialog appears, and the server log (`serve.log`) shows no second POST.
11. **GT8 discard, the default.** Glance `#child` over the active tab, then in another window open `chrome://discards` and urgently discard the parent tab. No crash. The Glance stays open over the discarded tab: in M158 a discard keeps the WebContents (`kWebContentsDiscard`).
12. **GT8 replace (the `kReplaced` path).** Relaunch with `--disable-features=WebContentsDiscard` (pass the launcher's full `--disable-features` list plus this one, see `scripts/mac/README.md` on #155). Repeat step 11: no crash, and the card closes, because the legacy discard replaces the WebContents.
13. **GT13 window close / quit.** With a Glance open, ⌘⇧W closes the window: no crash. Open a window again, open a Glance, then ⌘Q. No crash, and on the next launch no Glance page comes back as a tab.
14. **GT14 context menu (hands-on tester).** Right-click inside the Glance page and run every item: Back, Reload, Open Link in New Tab / Window, Copy Link, Inspect, Translate, Lens. No crash, and new tabs open next to the parent.
15. **Spaces rows still work (moved into the same table).** In a Spaces window: ⌘⌃P opens the Create Space form, ⌘⌃X / ⌘⌃Z cycle Spaces, and Tab > Group Tab is hidden. In View > Always Show Toolbar in Full Screen (compact mode, toolbar hidden), the item is hidden and ⇧⌘F does nothing. ⌘T opens the URL bar (replace-newtab).

The Zen side-by-side, which goes to the tester: steps 1, 4, 5, 8 and 9 in Zen 1.24b's Glance (see `.agent/notes/zen-behaviour/glance.md`).
