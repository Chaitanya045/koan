# Koan control inventory (click everything)

Builds: P = preview 09d39ad9, K90 = #90 (746ea88f / 7f9e2d6a), K129 = #129 4d20b7c5. Zen = Zen 1.24b, own fresh profile.
Status: OK, BUG (issue #), TODO (not yet clicked), N/A. Screenshots: `~/work/koan/.cache/usertest/<session>/`.
Rows come from screenshots plus the UI DevTools view-tree dump of each screen state (`inv-<state>.tsv` next to this file).

## A. Normal window: sidebar top row
| control | where | expected (Zen) | actual | status | issue |
|---|---|---|---|---|---|
| traffic lights (close) | top row, left | closes the window | closes; in compact → crash | BUG | #130 |
| traffic lights (minimise, zoom/green hover tile menu) | top row | macOS standard | green hover shows macOS tile menu (s4/040) | OK | |
| sidebar toggle button | top row | toggles compact mode | Chromium icon rail + full Chromium toolbar (s1/017) | BUG | #132 |
| back / forward / reload | top row | navigate | TODO | TODO | |
| no other buttons (Zen has none) | top row | – | TODO via dump | TODO | |

## B. URL pill
| control | where | expected (Zen) | actual | status | issue |
|---|---|---|---|---|---|
| pill click / ⌘L | under top row | floating centred bar | on NTP: dropdown in sidebar (s1/003); on page: in-pill dropdown "Ask Google about…" + "+" (s4/016) | BUG | #135 |
| pill look | | grey rounded pill, identity icon | plain text, no pill (s1/001-sidebar-crop) | BUG (minor) | |
| site-settings "tune" icon (right) | pill | Zen: identity icon on the left opens site panel | Chromium tune icon on the right | TODO click | |
| page-info / install / star icons on focus | pill | none | Chromium icons appear (s2/017) | BUG (minor) | |
| ⌘T floating bar | centre | Zen panel | AI Mode chip, Google rows, "+" (s1/010) | BUG | #101 |

## C. Tabs list
| control | where | expected (Zen) | actual | status | issue |
|---|---|---|---|---|---|
| tab row click | list | selects | selects | OK | |
| tab close × (hover) | row right | closes tab | TODO | TODO | |
| favicon | row | site favicon | Glance-expanded tab shows a globe (s2/022) | BUG (minor) | |
| audio/mute indicator | row | click mutes | TODO (YouTube) | TODO | |
| middle-click tab | row | closes | TODO | TODO | |
| drag reorder | list | reorders | TODO | TODO | |
| "+ New Tab" | above tabs | opens floating bar | TODO | TODO | |
| "Clear" | divider | closes unpinned tabs | TODO | TODO | |
| pinned section / divider | | pinned above divider | pinned under "+ New Tab" (s2/005) | BUG | #137 |

## D. Essentials
| control | where | expected (Zen) | actual | status | issue |
|---|---|---|---|---|---|
| tile click | top grid | selects | selects (s2/018) | OK | |
| tile right-click | | Essential menu (context-menus.md §8) | TODO | TODO | |

## E. Space header and bottom bar
| control | where | expected (Zen) | actual | status | issue |
|---|---|---|---|---|---|
| Space name double-click | header | inline rename | inline rename (s2/016-017); toast says "Workspace" | OK/BUG(minor) | |
| Space header "…" / right-click | header | more-actions menu | TODO | TODO | |
| bottom-left menu icon (two-dot glyph) | bottom bar | Zen: menu/app button | TODO | TODO | |
| Space dots | bottom bar | click switches; current highlighted | no current highlight (s2/013-014); click TODO | BUG (minor) | |
| "+" | bottom bar | Create New popup: Create Space / Create Folder / New Split / New Tab | menu (s2/006) TODO items | TODO | |
| Create Space panel button | panel | creates | does nothing (s2/011) | BUG | #136 |

## F. Context menus (every item)
| menu | expected (Zen) | actual | status | issue |
|---|---|---|---|---|
| normal tab | context-menus.md §8 | Chromium-ish list (s2/002): no Move to Folder, no "1/12", Chromium items | BUG (minor) — click each TODO | |
| pinned tab | §8 | TODO | TODO | |
| Essential | §8 | TODO | TODO | |
| folder | §8 | TODO | TODO | |
| Space header | §8 | TODO | TODO | |
| empty sidebar | §8 (Enable compact mode, Hide…, New Folder, Live Folder, Theme…, Tabs on the right) | had "Enable compact mode" (s1/028); rest TODO | TODO | |
| page / link / image | Open Link in Glance, Split Link to New Tab | TODO | TODO | |

## G. Menu bar (every item) — K90
| menu | items clicked | status | issue |
|---|---|---|---|
| Koan | TODO each | | |
| File | New tab/window/blank/private, Re-open closed, Open File…, Open Location, Close Window/Tab, Save Page As, Share, Print — TODO each | | |
| Edit / View / Spaces / History / Bookmarks / Profiles / Tab / Window / Help | Spaces items OK (s4); Appearance OK; others TODO | | |

## H. Other states
| state | controls | status |
|---|---|---|
| compact | edge reveal OK; lights over page #133; top row #133 | |
| fullscreen | top peek — TODO (user's report) | |
| small window | TODO | |
| Incognito | TODO | |
| second window | TODO | |
| Glance | 3 buttons blank (#138); Esc OK; ⌘O OK; click each button TODO | |
| split view | ⌘⌥V/U OK; H/G TODO; pane footers × / favicon TODO | |
| Library | rail items TODO | |
| settings | every row TODO; rebind #139 | |
