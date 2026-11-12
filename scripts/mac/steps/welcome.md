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
| W-1a | Launch on a slow first run (or watch the first 200 ms) | Skip setup (top right) and Esc work before the card appears. |
| W-1r | **Real pointer and real keys only** (not CDP): click Get started, Back, Next; Tab, Shift+Tab, arrows, Space, Enter, Esc | Each works the first time (the c174e/f regression: none did). |
| W-1c | **Real pointer:** click Skip setup (top right), the steps bar, and the card's buttons where the card lies over the sidebar's top row and the window's top 40 px; double-click there too | Each click reaches the page (W30): nothing drags or zooms the window from under the setup. Only the empty space drags it (W-3). |
| W-1u | Launch (the startup tab is the empty tab) | The floating URL bar does **not** open over the setup, and the setup has the keyboard (Esc leaves). After Skip setup or Start browsing nothing floats; the tab's page has the keyboard. |
| W-1 | Launch | The window opens as usual (not resized), covered by the setup on a shadcn `background`: the traffic lights top left and usable, the steps bar at the top, **Skip setup** top right, the card "The only browser you need" with **Get started** and **Skip setup**. No Chromium intro window, no infobar, no white frame first (W10). Capture light and dark appearance. |
| W-2 | Get started, then Next through every step | Shortcuts, Import, Search engine, "You're all set" with **Start browsing**. Back on each returns; **Skip** on each moves on without applying it. Every icon is a Lucide icon (sparkles, keyboard, drive, search, check, arrows, x), but the search engines' logos (Simple Icons, in the text colour: Brave, Google, DuckDuckGo, Ecosia ... ; Bing and Yahoo keep their initial). The last step says "Enjoy browsing." (#349). |
| W-3 | With a real pointer: press on the empty space around the card (and in the top bar between the steps and Skip), move 200 px, release; then click Get started | The window follows and stays (W6). After the release, Get started takes one click (the page got its release). Repeat 10 times fast: no crash, no stuck window. |
| W-4 | Shortcuts: keep "Zen's shortcuts", Next … Start browsing | ⌘S toggles compact mode; ⌥⌘L inspects; Settings > Keyboard Shortcuts shows no custom keys. |
| W-5 | Shortcuts: "Chrome's shortcuts", Next … Start browsing | ⌘S opens Save Page (compact mode on ⌃⌘S), ⌥⌘C inspects, ⇧⌘J opens Downloads, ⇧⌘N a private window, ⌥⌘← → switch tabs, ⇧⌘D bookmarks all tabs. The menu bar shows those keys. Settings > Keyboard Shortcuts shows them as custom (W15). |
| W-6 | Shortcuts: Zen's, then switch only the ⌘S row to Chrome's; the preset shows neither choice | Only ⌘S is Save Page; the rest are Zen's. |
| W-7 | Search (outside the EEA, e.g. `--search-engine-choice-country=IN` or none): Brave is listed first and chosen; keep it, Next … | chrome://settings/search shows Brave as the default; a search from the URL bar goes to search.brave.com. Also with `--search-engine-choice-country=JP` (a region whose list may lack Brave): still Brave. |
| W-7b | Search: pick another engine, Back, Next | The engine shown as chosen after Back is the one picked; Next makes it the default. |
| W-8 | `--search-engine-choice-country=DE`, fresh profile: the search step | Chromium's choice list in its order, **nothing chosen**, the EEA wording. Pick an engine, Next … Start browsing; quit, relaunch twice: chrome://settings/search shows that engine; no Chromium choice dialog over the setup or after (W11). Next with nothing picked: the engine stays as it was, and Chromium's dialog asks at the next navigation, never over the setup. |
| W-8b | `--search-engine-choice-country=DE`: during the setup press ⌘N and wait on the new window's page for 5 s | No choice dialog in the new window while the setup shows. |
| W-8c | `--search-engine-choice-country=DE`: Skip setup on the first step | The setup closes. The choice dialog may show at the next navigation (the EEA requires a choice), never over the setup. |
| W-9 | Relaunch after finishing | No setup. |
| W-10 | `kill -9` on the import step; relaunch | No setup (it is seen once shown, W3); the session comes back (#169). |

## Edge cases

| # | Do | Expect |
|---|---|---|
| W-11 | Without clicking anything first, Esc; then on each step, Esc, and **Skip setup** on each | The setup closes at once, the keyboard is on the page, nothing of a later step applies (what earlier Nexts applied stays). |
| W-12 | Import: pick Safari (or any listed browser), Next, Back, pick "Start fresh", Next … Start browsing. Again with Back then the import step's **Skip** | **No** import dialog opens either time (W17). |
| W-12b | Import, on a Mac with no Safari or Firefox data (or a fresh macOS user) | Only "Start fresh" and "Import from another browser…"; no browser it can't import is listed (W16). |
| W-12c | Import: pick a browser, Next … on the last step press **Skip setup** or Esc | Same as Start browsing: the import dialog opens (then Cancel). |
| W-IMP | Import: pick Safari (if listed), Next … Start browsing | After the setup closes, Chromium's import dialog opens in front. **Press Cancel.** Nothing is imported. |
| W-CR | Crash the page's renderer: CDP `Page.crash` on the `chrome://koan-welcome/` target (or `kill -9` the "Koan Helper (Renderer)" that appears at launch). On the welcome step, on the shortcuts step, and within the first second (the data still loading) | The setup ends at once: no sad page stays over the window, no CHECK or crash in the log (W25). Relaunch: no setup. |
| W-13 | ⌘W during the setup; then (fresh profiles) ⌘T then ⌘W, ⇧⌘W, ⌘Q | ⌘W with one tab closes the window and the setup with it; Dock click opens a normal window without the setup. After ⌘T, ⌘W closes that tab and the setup stays over the window. No crash, no CHECK (W27). |
| W-13b | Launch, then switch to another app (⌘Tab) before the setup appears; in the setup's renderer `Page.crash` (W-CR) while the other app is in front | The setup ends; Koan does not come to the front (W29). |
| W-14b | During the setup: ⌘T, then ⌃Tab and ⌘1 to switch tabs | ⌘T's floating URL bar opens over the setup (shortcuts stay, W25); Esc closes it. The setup stays over every tab (never under a page) and keeps the keyboard after a click on it (W4). |
| W-14 | ⌘T, ⌘L, ⌘N, ⌘, and the menu bar's File and View items during the setup | No crash. They are not blocked (W20): record what each does and capture anything that shows over the setup. |
| W-15 | ⌃⌘F during the setup, then ⌃⌘F again; and resize the window from its corner to 800 x 600 and to the full screen | The setup covers the window throughout and its card stays whole and centred (scrolling inside if short); no crash. |
| W-16 | Right-click on the page and on the card | No context menu (W9). |
| W-17 | After the setup, type `chrome://koan-welcome` in a tab | A blank page; nothing changes (W7). |
| W-18 | `--incognito` on a fresh profile; a guest window | No setup (W21). |
| W-19 | Keyboard only: Tab, Shift+Tab, arrows in each choice list, Space, Enter on the buttons | Every control is reachable with a visible focus ring; arrows move within a list; the first control of each step has the keyboard. VoiceOver announces the step's title, each choice and each button. |
| W-20 | Settings > Look and Feel set to the Zen style, then a fresh first run | The setup is still in shadcn's look (W-1). |
