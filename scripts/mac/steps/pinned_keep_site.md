# Mac steps: a pinned tab keeps its site (#74, Z8 of #161)

For the Mac loop and the hands-on tester. A DCHECK build, a profile past the welcome, Glance on (the default). Real
clicks and keys. Zen's reference: `zen.tabs.open-pinned-in-new-tab`, `zen.glance.open-essential-external-links` and
`zen.glance.enable-contextmenu-search`, all on.

Prepare: an Essential on https://github.com, a tab pinned to the Space on https://en.wikipedia.org, a normal tab on
https://example.com. For the link steps pin a page served by `scripts/mac/glance/serve.sh` with
`<a href="https://apple.com" target="_blank">` and a same-site `target=_blank` link (an http(s) page: Glance takes
http(s) only).

| # | Do | Expect |
|---|---|---|
| PK1 | Select the Essential (github.com), ⌘L, type `apple.com`, Enter | A new tab on apple.com in the active Space; the Essential still shows github.com. |
| PK2 | Same Essential, ⌘L, `github.com/notifications`, Enter; then type `koan browser` (a search), Enter | The first loads in the Essential. The search opens a new tab. `gist.github.com` also opens a new tab (another host). |
| PK3 | In a pinned page, click a `target=_blank` link to another site | It opens in Glance over the pinned tab, growing out of the click. Expand (the button) makes it a normal tab. No empty tab is left behind. |
| PK4 | The same link with ⌘-click, and with a middle click | A background tab, as before (no Glance). |
| PK5 | A `target=_blank` link to the **same** site in the pinned page | A normal new tab. |
| PK6 | A normal (unpinned) tab, the same `target=_blank` link | A normal new tab. |
| PK7 | Settings > Pinned Tabs: turn off "Open other sites from a pinned tab's URL bar in a new tab"; Glance: turn off "Open links from Essentials and pinned tabs to other sites in Glance"; repeat PK1 and PK3 | PK1 navigates the Essential; PK3 opens a new tab. Turn both back on. |
| PK8 | In any normal tab select a word, right-click > "Search Brave for ..." (your engine) | The results open in Glance. With ⌘ held when you click the item: a background tab. In a Glance page: a normal tab. Glance setting "Open \"Search for\"..." off: a new tab. |
| PK9 | PK3 with Glance off altogether | A normal new tab. |
| PK10 | PK3 then quickly close the pinned tab (⌘W) before Glance appears | No crash (the posted open finds no tab). |
