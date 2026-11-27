# Mac steps: quick window for links from other apps (#472)

For the Mac loop and the hands-on tester. A DCHECK build, a profile past the welcome, Koan running with one window.
Design: `.agent/notes/upstream/quick-window.md`. Reference: Arc's Little Arc. Record QW2, QW5 and QW7 (the "Done
when" run: a link from another app read and closed, a link moved to a Space, a routed link still going to its Space).

Prepare: Spaces "Home" (active, a few tabs, one of them github.com) and "Media"; Settings > Tab Management >
Workspaces > "Links from other apps open in": **A quick window**.

| # | Do | Expect |
|---|---|---|
| QW1 | Settings: the row's default on a fresh profile | "A Space (Space routing)"; with it, QW2's link opens as a tab in Home, as before (SR8). |
| QW2 | From Terminal: `open https://example.com`; then click a link in Mail or Notes | Each opens its own small window, centred, over the other app, with **one top row and no title bar**: the traffic lights at its start, centred in the row, then the lock and example.com (read-only), then one round button at its end (Move to Space; no Close button, the red light closes). The row is the window's colour (as the main window's sidebar) in dark and light; the page below it is a card inset by 8 px with rounded corners, like the main window's. No title text anywhere, no strip above the row. The main window still shows the tab it showed; its sidebar has no new tab. Screenshot it (`screencapture -l`) in dark and light, with `open https://example.com` and with a very long URL (`open "https://example.com/?q=$(printf 'a%.0s' {1..300})"`): the address ends before the button. |
| QW2b | Drag the row by the address text, and by the empty space between the address and the button; double-click the row; drag the window's edges and corners | The window moves (the address text doesn't get selected); the double click zooms it as a title bar's would; the edges resize it. Click the lock: the page info bubble opens (the lock is no drag area). Hover the lights: AppKit's coloured buttons; the yellow one minimizes it, the green one makes it fullscreen and back. |
| QW3 | In the quick window: Esc; again: ⌘W; again: the red traffic light | Each closes it. Nothing new in the main window, nothing in History > Recently Closed windows. |
| QW4 | Esc in a page that uses it (a site's search overlay or modal that calls preventDefault) | The page's overlay closes, the window stays; a second Esc closes the window. |
| QW5 | Open a link (QW2), scroll down, type in a field; press Move to Space | A menu: "Move to Space", then Home (first: the most recent), Media. Pick Media: the main window comes to front on Media, the page is its last tab, at the same scroll position with the typed text (no reload); the quick window is gone. |
| QW6 | Open a link; ⌘O | The page goes to Home (the most recent Space) at once, as in QW5. |
| QW7 | Space Routing Settings: Contains `github.com`, Open it in Media. `open https://github.com/koan-browser/koan` | No quick window: the tab opens in Media in the background with the toast (SR8). A route to "Most recent space" also wins (the tab opens in Home). |
| QW8 | Give Media a profile ("Work", Set Profile) and sign in to a site there; set "Default route for external links" to Media; `open` that site | The quick window shows you signed in (Media's profile). Move it to Home (Default profile): it stays signed in and shows the Work colour line in the sidebar. |
| QW9 | In a quick window, ⌘-click a link, and click a target=_blank link | Each opens as a normal tab in the main window's active Space (not in the quick window). |
| QW10 | ⌘⌥N in the main window; File > New Quick Window; ⌘K "New Quick Window" | The floating URL bar opens empty over the main window. Type a URL, Enter: it opens in a quick window, the main window unchanged. Esc instead: nothing opens. Pick an open tab's row: it switches to that tab. ⌘⌥N (or the menu item) with a quick window in front: the main window's bar opens. No Chrome menu item shows ⌥⌘N any more. |
| QW11 | Quit Koan with a quick window open, start it again | The main window comes back; the quick window doesn't. While it was open: it had no sidebar or Spaces, the Library's Spaces didn't list it, and a second window didn't get its page through window sync. |
| QW12 | A private window in front, `open https://example.com`; an agent task running (agent control) during QW2 | The link opens as before (a regular window's tab or a quick window from the regular profile, never in the private window); the agent's tabs are untouched. Close every window (the app keeps running), `open https://example.com`: a window opens with the link, no quick window. One window showing only a new tab page: the link opens in it. |

Logs: no `DCHECK`/`CHECK` and no "TabStripModel" re-entrancy warning while moving (QW5, QW6).
