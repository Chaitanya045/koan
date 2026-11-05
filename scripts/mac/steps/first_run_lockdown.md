# Mac steps: first run and crash restore (#169)

For the Mac loop and the hands-on tester. A DCHECK build, a fresh `--user-data-dir` under `$KOAN_MAC` unless a step
says otherwise, **without** `--no-first-run` and `--no-default-browser-check`; `--use-mock-keychain` stays on
(nothing here touches the real keychain). Zen's reference: the welcome only; no Mozilla prompts; after a crash the
session comes back silently (once, like Firefox's `max_resumed_crashes=1`).

## First run (NT1, NT3, NT4, NT6)

| # | Do | Expect |
|---|---|---|
| NT1 | First launch | One browser window with Koan's welcome. No Chromium sign-in intro or profile-picker window, no infobar, no system prompt. |
| NT3 | Finish the welcome, quit (⌘Q), relaunch twice | One window each time, no welcome, no infobar of any kind (session-restore, default-browser, pin, PDF), no bubble. |
| NT4 | `--search-engine-choice-country=DE`, fresh profile, then relaunch | No Chromium search-engine choice page or modal, first run or later. |
| NT6 | On the welcome's default-browser page pick "NO, still exploring", relaunch three times | No default-browser, pin or PDF infobar each time. |

## Crash restore (NT5), profile with 3 Spaces and 6 tabs

Prepare: 3 Spaces, 8 pages in all (count them with CDP `/json/list` or the sidebar). `kill -9` is the crash.

| # | Do | Expect |
|---|---|---|
| NT5 | `kill -9`, relaunch | No "Restore pages?" window. **One** browser window. The 3 Spaces (not 4: the startup Space is dropped) and all 8 pages back. No welcome. |
| NT5a | After NT5, quit with ⌘Q, relaunch | The same session again (a normal restore), count unchanged, no bubble. |
| NT5b | `kill -9`, relaunch with `--incognito` | No crash and no bubble (an Incognito window has no session to restore). Then quit, relaunch normally: the session is restored (the crash was not acknowledged). |
| NT5c | `kill -9`; relaunch; wait until the restored tabs are up (>= ~3 s, but < 60 s); `kill -9` again; relaunch | The **"Restore pages?" bubble** is shown this time (second crash within a minute of a silent restore: the count is written to disk right at launch, so even a very early kill finds it), not another silent restore. Clicking Restore restores. Then quit cleanly and `kill -9` once more: silent again (the clean exit reset the count). Also: relaunch after a silent restore, wait >= 65 s, `kill -9`, relaunch: silent again (the count was reset after 60 s). |
| NT5d | `kill -9` with two Koan profiles open (two `--user-data-dir`s or the profile switcher, if easy) | Each profile restores its own windows once; no duplicate windows or Spaces. |
| NT5e | After NT5 (restored, still running), close every window with ⌘⇧W, click the Dock icon | A window opens. The crashed session is **not** restored a second time (no duplicate windows or Spaces); the Dock reopen restores the last window as it does without a crash, or opens a new window. |
| NT5f | Launch with `--hide-crash-restore-bubble` after a `kill -9`, **with startup = New Tab page** (with "continue" the normal startup restores anyway and the flag is invisible) | No Koan crash restore and no bubble (the flag makes upstream treat the launch as clean): a New Tab page only. |
| NT5g | `kill -9`, relaunch with a URL: `open -a Koan https://example.com` | The session is restored and the URL's tab is not lost or closed (the restore is posted until the launch has added its tabs). |

Watch in every run: no `CHECK`, `DCHECK` or `NOTREACHED` in the log, no new `.ips`, and the exit type of the profile
(`Preferences` > `profile.exit_type`) is `Normal` after a clean quit.
