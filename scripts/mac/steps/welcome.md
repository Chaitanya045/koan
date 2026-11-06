# Mac steps: the welcome, chrome://koan-welcome (onboarding v2, #181, #174)

For the Mac loop and the hands-on tester. A DCHECK build; a fresh `--user-data-dir` for every row unless it says otherwise;
**without** `--no-first-run` and `--no-default-browser-check`; always `--use-mock-keychain`, and
`--enable-logging=stderr --v=0`. After every row, check the log and crash reports (no CHECK, no DCHECK, no
`Unhandled chrome.send`). The page is a DevTools target (`/json/list`, URL `chrome://koan-welcome/`): read it with CDP
`Runtime.evaluate`.

**Import safety:** never import anything. On the import step keep **"Start fresh"** (the first choice), except in
W-IMP, which picks a browser only to see the dialog open and then presses **Cancel** in it.

What it should look like: shadcn's look (Koan's shadcn tokens, Geist, Lucide icons) whatever the browser's style, in
light and dark; not Zen's colours, no glass. Audit: `.agent/notes/upstream/welcome.md` (W1-W26).

## The flow

| # | Do | Expect |
|---|---|---|
| W-1 | Launch | The window opens as usual (not resized), covered by the setup on a shadcn `background`: the steps bar at the top, **Skip setup** top right, the traffic lights top left and usable, the card "Welcome to a calmer internet" with **Get started** and **Skip setup**. No Chromium intro window, no infobar, no white frame first (W10). Capture light and dark appearance. |
| W-2 | Get started, then Next through every step | Shortcuts, Import, Search engine, "You're all set" with **Start browsing**. Back on each returns; **Skip** on each moves on without applying it. Every icon is a Lucide icon (sparkles, keyboard, drive, search, check, arrows, x). |
| W-3 | Press on the empty space around the card (and in the top bar between the steps and Skip), move 200 px, release | The window follows and stays (W6). Repeat 10 times fast: no crash, no stuck window. Clicks in the card still work. |
| W-4 | Shortcuts: keep "Zen's shortcuts", Next … Start browsing | ⌘S toggles compact mode; ⌥⌘L inspects; Settings > Keyboard Shortcuts shows no custom keys. |
| W-5 | Shortcuts: "Chrome's shortcuts", Next … Start browsing | ⌘S opens Save Page (compact mode on ⌃⌘S), ⌥⌘C inspects, ⇧⌘J opens Downloads, ⇧⌘N a private window, ⌥⌘← → switch tabs, ⇧⌘D bookmarks all tabs. The menu bar shows those keys. Settings > Keyboard Shortcuts shows them as custom (W15). |
| W-6 | Shortcuts: Zen's, then switch only the ⌘S row to Chrome's; the preset shows neither choice | Only ⌘S is Save Page; the rest are Zen's. |
| W-7 | Search: keep Brave, Next … | Brave Search is the default (Settings > Search engine; a search from the URL bar goes to search.brave.com). |
| W-7b | Search: pick another engine, Back, Next | The engine shown as chosen after Back is the one picked; Next makes it the default. |
| W-8 | `--search-engine-choice-country=DE`; go through the setup with Next on the search step; quit, relaunch twice | No Chromium search engine choice dialog, over the setup or after (W11). |
| W-8b | `--search-engine-choice-country=DE`: during the setup press ⌘N and wait on the new window's page for 5 s | No choice dialog in the new window while the setup shows. |
| W-8c | `--search-engine-choice-country=DE`: Skip setup on the first step | The setup closes. The choice dialog may show at the next navigation (the EEA requires a choice), never over the setup. |
| W-9 | Relaunch after finishing | No setup. |
| W-10 | `kill -9` on the import step; relaunch | No setup (it is seen once shown, W3); the session comes back (#169). |

## Edge cases

| # | Do | Expect |
|---|---|---|
| W-11 | Esc on each step, and **Skip setup** on each | The setup closes at once, the keyboard is on the page, nothing of a later step applies (what earlier Nexts applied stays). |
| W-12 | Import: pick Safari (or any listed browser), Next, Back, pick "Start fresh", Next … Start browsing | **No** import dialog opens (W17). |
| W-IMP | Import: pick Safari (if listed), Next … Start browsing | After the setup closes, Chromium's import dialog opens in front. **Press Cancel.** Nothing is imported. |
| W-CR | Crash the page's renderer: CDP `Page.crash` on the `chrome://koan-welcome/` target (or `kill -9` the "Koan Helper (Renderer)" that appears at launch). On the welcome step, on the shortcuts step, and within the first second (the data still loading) | The setup ends at once: no sad page stays over the window, no CHECK or crash in the log (W25). Relaunch: no setup. |
| W-13 | ⌘W, then ⌘Q, during the setup (a fresh profile each) | No crash, no CHECK. After ⌘W: Dock click opens a normal window without the setup. |
| W-14 | ⌘T, ⌘L, ⌘N, ⌘, and the menu bar's File and View items during the setup | No crash. They are not blocked (W20): record what each does and capture anything that shows over the setup. |
| W-15 | ⌃⌘F during the setup, then ⌃⌘F again; and resize the window from its corner to 800 x 600 and to the full screen | The setup covers the window throughout and its card stays whole and centred (scrolling inside if short); no crash. |
| W-16 | Right-click on the page and on the card | No context menu (W9). |
| W-17 | After the setup, type `chrome://koan-welcome` in a tab | A blank page; nothing changes (W7). |
| W-18 | `--incognito` on a fresh profile; a guest window | No setup (W21). |
| W-19 | Keyboard only: Tab, Shift+Tab, arrows in each choice list, Space, Enter on the buttons | Every control is reachable with a visible focus ring; arrows move within a list; the first control of each step has the keyboard. VoiceOver announces the step's title, each choice and each button. |
| W-20 | Settings > Look and Feel set to the Zen style, then a fresh first run | The setup is still in shadcn's look (W-1). |
