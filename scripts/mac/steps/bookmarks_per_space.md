# Mac steps: bookmarks per Space (#480)

These steps are for the Mac loop and the hands-on tester. Use a DCHECK build and a profile past the welcome, with two
Spaces, "Work" and "Home", and the bookmarks bar on (⇧⌘B). Zen's reference is `ZenSpaceBookmarksStorage.js`,
`ZenSpaceManager.mjs` (`isBookmarkInAnotherWorkspace`, `_invalidateBookmarkContainers`) and
`browserPlacesViews-js.patch`. The design note is `.agent/notes/upstream/bookmarks-per-space.md`. The browser tests
`KoanBookmarkSpacesBrowserTest.*` cover the rule, the bar and the suggestions. These steps cover the native menus,
which UI DevTools can't click, and the macOS menu bar, which only the Mac has.

| # | Do | Expect |
|---|---|---|
| BPS-1 | In Work, open example.com and press ⌘D. | The bubble has a "Spaces" row under Folder, set to "All Spaces"; its list has All Spaces, Work and Home. Pick Work, then Done. |
| BPS-2 | Bookmark example.org (⌘D, Spaces: All Spaces, Done). Open the menu bar's Bookmarks menu in Work, then switch to Home and open it again. | In Work it lists both bookmarks. In Home it lists example.org only. The bookmarks bar follows the same way, with no gap where example.com was. |
| BPS-3 | In Home, right-click example.org on the bar > Spaces…, tick Home and Work, Save. Open ⌘D on example.org. | The row names both Spaces, in sidebar order ("Work, Home"). Both Spaces show the bookmark. |
| BPS-4 | Put both bookmarks in a new folder on the bar. Right-click the folder > Spaces…, tick Work, Save. Switch to Home. | The folder is gone from the bar and from the menu bar's Bookmarks menu in Home, and back in Work. |
| BPS-5 | In Home, type "example" in the URL bar. | The suggestions have no example.com bookmark row (a history row for it may still show). In Work they do. |
| BPS-6 | Open a second window (⌘N), in Home. Use the menu bar's Bookmarks menu while each window is in front. | The menu follows the front window's Space. |
| BPS-7 | Rename Work to "Office" (double-click its header), then delete it (Space menu > Delete Space, and confirm). | After the rename nothing changes. After the delete, the folder and example.com show in every Space. |
| BPS-8 | Settings > Tab Management > Workspaces: turn "Bookmarks per Space…" off. | Every bookmark shows on the bar, in the menus and in the suggestions. ⌘D has no Spaces row, and the context menu has no Spaces…. Turn it back on: the assignments are back. |
| BPS-9 | Quit (⌘Q) and start again. | The bookmarks still show only in their Spaces. |
| BPS-10 | Open a private window (File > New Incognito Window) and a New Blank Window (File > New Blank Window), with the bookmarks bar on. | Both show every bookmark. In the private window ⌘D has no Spaces row. |
