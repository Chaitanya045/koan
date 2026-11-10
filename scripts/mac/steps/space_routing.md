# Mac steps: Space routing (#81)

For the Mac loop and the hands-on tester. A DCHECK build, a profile past the welcome. Zen's reference:
`ZenSpaceRoutingManager.sys.mjs`, `zen-space-routing.ftl`, the Library's Routing section (`zen-library.ftl`).

Prepare: Spaces "Home" (active) and "Media"; a page with `<a href="https://www.youtube.com" target="_blank">` and a
`window.open("about:blank")` that then sets `location` to youtube.com.

| # | Do | Expect |
|---|---|---|
| SR1 | Right-click the Space header > Space Routing Settings | The dialog: the empty text, New route, Default route for external links (Most recent space). Also from ⌘K "Open Space Routing". |
| SR2 | New route: Contains `youtube.com`, Open it in Media, Save; reopen | The route is kept (also after a restart). |
| SR3 | In Home, click the target=_blank link | Home stays in front on its tab; the tab is in Media; toast "New tab opened in Media". Switch to Media: it lands on that tab. |
| SR4 | ⌘T, type youtube.com, Enter (and the URL bar's new-tab path) | Same as SR3. |
| SR5 | The window.open() page | The tab goes to Media on its first navigation. |
| SR6 | An Essential or pinned tab on youtube.com; ⌘⇧T a closed youtube tab; Duplicate a youtube tab; restart with youtube tabs in Home | None of them move. |
| SR7 | Route `a(?=b)` as RegEx | The field turns red and Save is off; Contains makes it valid again. |
| SR8 | Default route for external links: Media. From Terminal: `open https://example.com`; from Mail/Notes click a link; Handoff a page from an iPhone | Each opens in Media in the background with the toast (window in front: the most recent one). Quit Koan and `open https://example.com`: the same once the window is up. |
| SR9 | Right-click a github.com tab > Add Route for Domain; select two tabs on two sites > Add Route for Domains | A Contains `github.com` route, then a RegEx `(a\.com|b\.org)`, both to Most recent space; the settings open. |
| SR10 | Rename Media to Videos (double-click the header) | The routes and the external default now say Videos and still work. |
| SR11 | Remove a route, Save; Cancel after edits | Removed; Cancel keeps the old routes. |
| SR12 | A route to a Space this window doesn't have (from another window) | Nothing moves; the dialog lists that name as a choice. |
