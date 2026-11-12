# Mac steps: containers per Space (#318)

These steps are for the Mac loop and the hands-on tester. Use a DCHECK build and a profile past the welcome. Zen's
reference: `ZenSpaceManager.mjs` (`getContextIdIfNeeded`, `updateWorkspaceActionsMenuContainer`),
`ZenSpaceCreation.mjs`, `zen-workspaces.ftl`, and `zen-tabs.css` for the container line. The design note is
`.agent/notes/upstream/space-containers.md`. `scripts/e2e/space_containers.mjs` covers the logins. These steps cover
the native menus, which UI DevTools can't click.

| # | Do | Expect |
|---|---|---|
| SC1 | Right-click the Space header. | After Theme… there is "Set Profile ▸" with Default (checked), a separator and New Profile…. |
| SC2 | Set Profile > New Profile…, type "Work", then Create. | The dialog is titled "New Profile" and explains what profiles do. Set Profile now lists Work with a blue dot, checked; Default is unchecked. |
| SC3 | In that Space, press ⌘T, type a site you are signed into elsewhere, then Enter. | The site is signed out: the tab is in Work's partition. |
| SC4 | Sign in there. Switch to a Space without a profile and open the same site. | That Space is still signed in as before, not as the Work login. |
| SC5 | ⌘-click a link in the Work tab, middle-click one, and use "Open Link in New Tab". | Each new tab is signed in as Work. |
| SC6 | In the Space without a profile, a tab that was there before SC2 opens links. | They stay in that tab's own (default) login. |
| SC7 | Drag the Work tab to the other Space (or use Move To). | The tab stays signed in as Work, and a 2 pt blue line shows at its leading edge. Back in Work, the line goes. |
| SC8 | Open the form with the bottom bar's + > Create Space (or Ctrl+⌥N) and look at its Profile row. | The row reads "Profile" with a tooltip, and the button says "Default". The button lists Default, Work and New Profile…. Pick Work, then Create. |
| SC9 | In the new Space, click a bookmark from its empty tab. | The site loads signed in as Work. The empty tab was in Work's container. |
| SC10 | Quit (⌘Q) and start again. | Both Spaces still have Work (Set Profile is checked) and the tabs are still signed in. Change a localStorage-backed theme on a site in one Space; the other keeps its own. |
| SC11 | Set Profile > Default on a Work Space. | Existing tabs stay signed in as Work (the line shows on them), and new tabs are in the default partition. |
| SC12 | Open a second window. | Its Spaces have the same profiles (window sync), and ⌘T there opens in the Space's profile. |
| SC13 | Downloads, History (⌘Y) and the URL bar's suggestions in a Work tab. | A download from a Work tab completes into the normal Downloads list. Pages visited in Work show in History and the suggestions, which are shared as in Firefox's containers. |
| SC14 | Incognito window. | No Spaces, no Profile row, no Set Profile. |
| SC15 | Delete the Work Space (another Space keeps Work). | The other Space is still signed in as Work: deleting a Space keeps its profile and its data. |
