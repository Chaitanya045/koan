# Onboarding (the welcome flow)

Zen 1.24b. Source: `src/zen/welcome/ZenWelcome.mjs` (1041 lines), `zen-welcome.css`, `welcome-background.mp4`,
`locales/en-US/browser/browser/zen-welcome.ftl`, `src/zen/tests/welcome/browser_welcome.js`, `ZenStartup.mjs` (when it starts).
**Visuals, colours, timings and the WebUI port plan are already captured live** in `.agent/notes/zen-mac-spec.md` section 1 and 13
(frame-by-frame, screenshots `onboarding-0*.jpg`) and `.agent/notes/zen-onboarding-port.md`; this file is the **step-by-step behaviour**
(input -> result -> what is stored). No import was ever run while capturing; keep it that way when testing.

## 1. When it starts

- On the first window of a profile where `zen.welcome-screen.seen` is false (`ZenStartup.mjs` ~173; a window with Spaces/welcome disabled does not start it). Setting that pref true
  skips it (used for all live tests).
- While it runs the root has `zen-welcome-stage`; all other browser elements are hidden (`clearBrowserElements`); no floating URL bar or session
  opens (`cmd-t-cmd-l-session.md`).
- The window is **resized to min(1200, avail width) x min(720, avail height), centred on the screen, and the size is locked**
  (min/max width and height set on the root with `!important`) until the flow ends.

## 2. The start screen

A full-window looping green **gradient video** (`welcome-background.mp4`, autoplay, muted) and, centred, the two-line title
**"Welcome to" / "a calmer internet"** built from one element per character, then a round arrow button.
- Title: each character springs in (`bounce .3, visualDuration .45 s`, stagger **.035 s**, start delay **.8 s**).
- The button fades/slides in (`y 20 -> 0`, blur 2 -> 0 px, spring stiffness 300 / damping 20 / mass 1.8, delay .1 s).
- **Click the arrow**: the title characters and the button fade up (`opacity 1 -> 0`, `y 0 -> -14`, 0.3 s ease-in, stagger .012 s); then the pages UI replaces the start
  screen (`pages` container fades in over **0.45 s**).

## 3. Pages (left: text and buttons, right: content)

Layout: a left column with the **Back** button on top (disabled on the first page), the title and description(s), and the buttons at the bottom; the right pane
shows the page content. Transitions: the old text slides out left (`x 0 -> -80 x direction`, fades, 0.3 s), the new text slides in from `120 x direction`
(spring `bounce .4, visualDuration .3 s`, each child staggered by **.03 s**); the content cross-fades (`kFade`, ~.25 s). **Back** goes to the previous page; a page's `skip()`
is evaluated in the direction of travel. **Next** (`zen-generic-next`, primary) commits the page then moves on.

| # | Page id | Title / description | Content and effect |
|---|---|---|---|
| 1 | `import` | "Bring your stuff along." / pick a browser to import bookmarks, history, passwords | A list of **importable browsers** (each with its profiles, brand icon) and a first option **"NO, start fresh."** (default). **Next** with a browser selected opens the native migration wizard (`MigrationUtils.showMigrationWizard`, first-run entrypoint, startup migration for that profile); with "NO" nothing is imported. **Koan: do not implement import in the test path** (port plan section 9) |
| 2 | `colors` | "Your space, your colors." / give each space its own colour identity | Removes the video background; the **theme picker panel** (`theme-picker.md`) opens docked over the right pane (`nonnative`, `noautohide`, anchor sized to the panel minus a 20 px shadow, +90 px on macOS). Edits change the first Space's theme live. On leaving, the panel fades (0.3 s) and hides |
| 3 | `search` | "Pick your search engine." / "...You can always change it later!" | Radio list of the visible engines (Wikipedia and eBay filtered out); the current default is preselected; picking sets it as the default **immediately** (`SearchService.setDefault`, reason USER) |
| 4 | `block-ads` | "Block ads and trackers?" / Zen can install uBlock Origin | **Skipped** when no ad-blocker could be fetched (`_adBlocker === null`). Radios **"YES, block ads for me."** / **"NO, show me everything."**; on Next with YES, uBlock Origin (`uBlock0@raymondhill.net`) is installed in the background |
| 5 | `essentials` | "Choose the apps you use most." / keep them handy in your sidebar | A **mock browser sidebar** (three dots, an Essentials grid, two mock tabs) and 9 toggle tiles for apps: Obsidian, Discord, Trello, Slack, GitHub, Tuta, Notion, Google Calendar, Figma; selected ones appear in the mock Essentials grid. Buttons: **Next** and **Skip for now** (clears the selection and moves on) |
| 6 | `default-browser` | "Make Zen your default?" / links from other apps open in Zen | Radios **"YES, make Zen default."** / **"NO, still exploring my options."**; YES calls the shell service `setDefaultBrowser(false)` on Next |
| 7 | `finish` | "All set? Let's get rolling!" / ready to go | Button **"Dive in!"**; right pane empty |

## 4. Finishing (`finish()`)

1. Buttons removed, Back disabled; `gZenWorkspaces.reorganizeTabsAfterWelcome()`.
2. Choices applied (`#applyChoices`): each chosen essential app is **added to history (typed visit)**, opened as a lazy background tab with its bundled icon, made to look loaded
   and **added to Essentials** (`addToEssentials`); then a **folder named "zen basics"** is created holding the currently selected tab (not the empty tab).
3. `#zen-welcome` fades out (`opacity 1 -> 0`), the content container is removed, the size lock is released (`unlockWindowSize`), the window is not moved again.
4. The browser elements (sidebar, page card, ...) fade in from opacity 0 -> 1; a resize event recalculates layouts; collapsed folders re-layout.
5. The "seen" state (`zen.welcome-screen.seen`) is what stops it from showing again; where exactly it is written was not traced (LIVE CHECK 4).

## 5. Edge cases

- Cmd+Q and window close during onboarding: nothing is applied except what was committed already (search engine, and an ad-blocker install already started).
- Back from "colors" keeps the Space's theme edits.
- If the translation of a page's text/buttons is slow, stale renders are dropped (render tokens), so rapid Next/Back never shows mixed pages.
- Import page with no migrators available: only "NO, start fresh." is listed.

## 6. LIVE CHECK

1. Skipped in live sessions by seeding `zen.welcome-screen.seen`. To verify the flow without risk, launch a **fresh profile without the seed** and drive only via DOM (never click an import option): record the exact window size (1200 x 720), centre position, and page timings. This was done on 2026-11-03 for the spec; re-check only if a divergence shows up.
2. What "zen basics" folder contains when the selected tab is the empty tab (nothing: an empty folder).
3. Whether Skip-for-now also skips `commit` of earlier selections.
4. When `zen.welcome-screen.seen` becomes true (end of flow vs start).

## 7. Test script (Koan, using the port plan)

1. Fresh profile: window opens 1200 x 720, centred, size locked, video playing, title animates in over ~1.5 s, arrow appears.
2. Click the arrow: title fades up, pages fade in; Back is disabled. Next through every page; check each page's title/buttons from the table; Back returns with the reverse slide.
3. Pick a non-default search engine: it is the default immediately.
4. Choose three apps on the Essentials page: they appear in the mock grid; Next; after finish they are Essentials (no folder content issues), and a "zen basics" folder exists.
5. Finish: welcome fades, browser fades in, window is resizable again.
6. Relaunch: no welcome.
