# Mac steps: bubbles about the page with the toolbar hidden (#163; Z5, Z11, Z12, Z14)

For the Mac loop and the hands-on tester. Placement and "follows the toolbar" can't be read from UI DevTools (its
views carry widget-local bounds only), so take a window capture of each step (`screencapture -l <window id>`, see
`koan-mac-window-capture`) and compare the bubble's arrow with the pill's site icon. DCHECK build, a fresh profile,
real pointer and keys (`scripts/mac/tools`). webcamtests.com needs a camera; any page with `getUserMedia` does.

Layouts: S = single toolbar (default); CS = compact + hide sidebar (single toolbar keeps the header); CT = compact +
hide toolbar (Ctrl+Alt+B first: multiple toolbars); CB = compact + hide both.

| # | Do | Expect |
|---|---|---|
| 1 | S: Cmd+D | The bookmark bubble's arrow is at the pill's site icon, the bubble fully on screen. Esc closes it. Also File/Bookmarks menu > Bookmark This Tab (same), and `chrome.bookmarks.create` + the page action (no crash). |
| 2 | S: `navigator.mediaDevices.getUserMedia({video:true})` | The permission bubble opens at the pill (not above the window). Allow: a red camera icon next to the site icon (tooltip says in use); the camera light is on; click the site icon: page info lists the camera. Stop the tracks: the icon goes. |
| 3 | S: `Notification.requestPermission()` | The bubble opens at the pill. Block via keyboard (Tab to Block, Enter): no mouse movement needed to proceed. |
| 4 | S: a site whose notification request is quiet (a page that requests without a gesture, `chrome://flags` quiet UI) | The pill shows the permission icon (tooltip); pressing the site icon opens page info. |
| 5 | CT: `Notification.requestPermission()` with the pointer left alone | The toolbar comes in **at once** (no slide), the bubble is under the omnibox/at the revealed toolbar (not at the top screen edge). Answer it with the keyboard: the toolbar hides about 1 s later without touching the mouse. Let the page cancel the request (navigate away): same. |
| 6 | CT: Cmd+= on a page (the zoom bubble anchors to the toolbar, so it **does** open one); then click into an empty text field (an anchor lookup, no suggestions, nothing opens) | Cmd+=: the toolbar comes in at once and goes about 1 s after the zoom bubble closes (also if you close it with Esc and don't touch the mouse). The empty field: the toolbar does **not** come in. |
| 7 | CS and CB: `Notification.requestPermission()` | The bubble points at the header's buttons (top row) or the fixed point at the top, never above the window. |
| 8 | CS: peek the sidebar (left edge), click the pill's site icon | Page info opens at the pill and the sidebar stays out while it is open; Esc: the sidebar slides away. A permission prompt arriving while the sidebar is peeked: same. |
| 9 | S: download a file | No panel opens by itself; the download flies to the Library. Settings > Downloads > "Show downloads when they're done" on: the panel opens at the pill; off: not. |
| 10 | Ctrl+Alt+B layout (not compact), fullscreen (S and CT) | Anchors as before in the toolbar layout (upstream's); in fullscreen the same bubbles at the pill / the revealed toolbar. |
| 11 | Close the window with a bubble open (each of the above); Cmd+Q | No crash, no CHECK/DCHECK in the log. |
