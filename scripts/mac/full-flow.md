# The one computer-use pass per Mac cycle

**When:** once per Mac cycle, only after `scripts/mac/suite.sh` has passed on the same `Koan.app` (or its failures
are understood). It is the only step with computer use (screenshots and clicks with a model watching), so keep it to
this list and don't repeat steps the suite already checks. If something here fails, write it down, file it, and turn
it into a scripted check for the next cycle. Don't debug it with more screenshots.

**Setup:** a DCHECK build launched from a terminal with its log captured (`--enable-logging=stderr --v=0 >log 2>&1`),
**a fresh `--user-data-dir`** (never the user's profile), `--force-first-run`. At the end, grep the log for
`FATAL|Check failed|DCHECK` and look for new `Koan*.ips` files in `~/Library/Logs/DiagnosticReports`.

Spend about one screenshot per row. Each row is: what you do, then what must be true.

| # | Do | Must be true |
|---|---|---|
| **First launch** | | |
| 1 | Launch. | The welcome covers the window and the traffic lights still work above it. No URL bar opens over it, and no system dialog (keychain, default browser) shows. |
| **Onboarding** (#181) | | |
| 2 | Real pointer: Get started, then Next on each step. On Shortcuts, flip one row. On Import, keep Start fresh. On Search, keep Brave. Then Start browsing. | Every click and key works (W-1r). The card's shadcn look is right in light and dark. The browser opens on Zen's empty tab. |
| 3 | Look at Settings › Keyboard Shortcuts and the search engine. | They match what you chose. |
| **Daily Zen use** | | |
| 4 | Open 4 sites. Pin one with ⇧⌘D, add one to the Essentials, rename one with a double-click. | Rows, icons and the loading bar look like Zen 1.24b: spacing, hover colour, no spinner. |
| 5 | ⌘T and type. ⇧⌘A switches tabs. ⌃⌥N creates a Space with an emoji and a theme. ⌥⌘→ switches Space. | The floating URL bar, the Tabs chip, the Space theme and the switch animation look right. |
| 6 | Drag a tab to the other Space's button, then hold one at the sidebar's edge. | The Space switches under the pointer and the trackpad ticks (haptics are the one thing scripts can't feel). |
| 7 | Swipe between Spaces with two fingers, and between pages with "Swipe between pages" off (Z29). | Like Zen. Real trackpad only. |
| **Compact, Glance, split** | | |
| 8 | ⌘S for compact mode. Hover the left edge, then the top edge. Go native fullscreen (⌃⌘F) and hover again. | The sidebar and toolbar slide in with Zen's timing. Fullscreen has no Chromium toolbar strip and the traffic lights behave. |
| 9 | Alt+click a link (Glance). Expand it, then close it. Open a split view (two tabs side by side) and resize it. | Glance's buttons, backdrop and animation look like Zen. The split divider tracks the pointer. |
| **Window sync** (#75) | | |
| 10 | ⌘N for a second window. Select the pinned tab in it, then click back into the first window. | Both windows show the same Spaces, Essentials and pinned tabs. The page moves between windows without a reload and without flicker. |
| 11 | ⇧⌘N for a New Blank Window, then quit with ⌘Q and relaunch. | The blank window has no Spaces. After the relaunch both windows come back as they were. |
| **Agent control** (#180) | | |
| 12 | In Terminal: `koan pair` (approve it), `koan open https://example.com`, `koan screenshot <id> -o /tmp/a.png`. | The approval dialog names the program. The agent's tab never comes to the front, and the indicator shows. The PNG shows the page. |
| 13 | `koan take-over` on your current tab, approve it, then type in that tab yourself. | The take-over prompt shows. Your typing pauses the agent and the banner says so. Hand back works. |
| **End** | | |
| 14 | Quit. Grep the log and check the crash reports. | Nothing fatal and no new `.ips`. |

Write the result as one line per row (`ok`, or what was wrong), plus the build's label. Nothing else.
