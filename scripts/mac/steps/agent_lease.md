# Mac steps: the agent tab's keep-alive lease (#180, design section 8)

For the hands-on tester. `scripts/mac/suite.sh --only agent_lease` covers a background tab, a covered window, a
minimised window, release after 3 s, the cap and the user's own background tab. These steps cover what a script can't:
another Desktop, a locked screen, display sleep and Low Power Mode. Use a fresh test profile with Agent ready on, and
`koan` from `Koan.app/Contents/Resources/bin/` paired with that profile (`KOAN_USER_DATA_DIR=<profile> koan pair`).

**The page.** Save the script below as `lease.mjs` and run `node lease.mjs`. It serves the agent_lease test page on
`http://127.0.0.1:8765/lease` (a canvas that changes every frame, a sliding red box, and a heading `raf=<n>
timers=<n>`).

```js
import http from 'node:http';
const page = `<!doctype html><title>Lease</title><canvas id=c width=240 height=120></canvas>
<div id=b style="width:40px;height:40px;background:#e33;animation:s 1s linear infinite alternate"></div>
<h1 id=h></h1><style>@keyframes s{to{transform:translateX(360px)}}</style><script>
let raf=0,timers=0;const x=c.getContext('2d');
(function f(){raf++;x.fillStyle='hsl('+raf*7%360+' 80% 50%)';x.fillRect(0,0,240,120);
h.textContent='raf='+raf+' timers='+timers;requestAnimationFrame(f)})();
setInterval(()=>{timers++;h.textContent='raf='+raf+' timers='+timers},16);</script>`;
http.createServer((q, s) => s.end(page)).listen(8765, '127.0.0.1');
```

**The reading.** `koan open http://127.0.0.1:8765/lease` prints the tab id. Then, from a terminal on the other
Desktop or over ssh (not in Koan's window):

```sh
for i in 1 2 3 4 5; do koan snapshot <tab> | grep -o 'raf=[0-9]* timers=[0-9]*'; sleep 0.6; done
```

Each line is about 0.8 s after the one before. **Live** means both numbers grow by at least 20 a line (rAF is about 48
a line at 60 Hz, 24 at 30 Hz under Energy Saver). **Throttled** means rAF doesn't grow and timers grow by about 1 a
line. To check release, wait 5 s with no command, then run the loop again. Its first line must show rAF where the
last line of the previous loop left it, plus one or two frames.

| # | Do | Expect |
|---|---|---|
| 1 | Koan on Desktop 1 with the user's tab in front. Switch to Desktop 2 (⌃→) and run the reading there | Live. The screenshots from `koan screenshot <tab> -o /tmp/l<i>.png` taken 0.5 s apart differ (the canvas colour and the box position). |
| 2 | Koan's window in full screen (its own Space), switch to another Space, then run the reading | Live. |
| 3 | Lock the screen (⌃⌘Q), run the reading over ssh, then unlock | Live while locked (the display stays on for a few seconds). Afterwards Koan's own tab repaints at once, and Console shows no GPU errors. |
| 4 | `pmset displaysleepnow` over ssh, run the reading, then wake the display | **Expected gap:** rAF stops while the display sleeps (the display link stops; the lease holds no display-sleep assertion, `stay_awake` is off by design). Timers should still run unthrottled while a command is in flight (the page scheduler sees a visible page). Write down what you saw: rAF growing means the timer fallback kicked in. |
| 5 | On battery (unplug), turn on Low Power Mode (System Settings > Battery), run `scripts/mac/suite.sh --only agent_lease` | The log says `power: battery, Low Power Mode on: expected cap 2`. The cap case passes with 2 of 6 tabs live. The other cases pass (rAF may be about 30/s if Energy Saver is on too, still above the 25/s threshold). |
| 6 | Same as step 5, then turn Low Power Mode off while a command waits on a queued tab | The queued command runs within about 1 s (the cap goes back to 4 on the next tick). |
| 7 | Same as step 5: `koan open` a tab, then `koan snapshot` it while Koan is hidden (⌘H) | Live. |
| 8 | Activity Monitor > CPU, during step 1, then 10 s after the last command | The agent tab's renderer (the "Koan Helper (Renderer)" whose CPU rises while the loop runs) uses CPU during the loop, and close to 0 % ten seconds after. A user's background tab with the same page uses about 0 %. |

Record the numbers from each step in the PR or issue comment. Steps 3, 4 and 7 were not checked by any test before.
