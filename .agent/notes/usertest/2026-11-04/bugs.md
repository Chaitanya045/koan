# Koan user-test bugs

Build: Koan-preview.app (release, develop 09d39ad9), macOS 27.0.1, MacBook Air 1470x956 pt. Fresh profile each session
(`--user-data-dir=~/work/koan-mac/tmp/usertest/profile-<n> --no-first-run`). Zen 1.24b reference with its own fresh profile.
Screenshots: `~/work/koan/.cache/usertest/<session>/` (committed copies under `.agent/notes/usertest/` on branch test/user-sessions).
Real HID pointer and key events, one action at a time, looked at between steps.

## Session 1 (2026-11-04 13:16 IST)

### B1. Sidebar toggle button turns Koan into Chromium's collapsed vertical tabs with Chromium's full toolbar — major
Steps: fresh profile, open a few sites, click the sidebar button in the top row (right of the traffic lights).
Expected (Zen, s1/021-zen-page-compact.png): the button toggles compact mode; the sidebar hides, the page fills the card, no toolbar.
Actual (s1/017-toggle-click.png): Chromium's collapsed vertical tab strip (icon rail with Chromium's tab-group and tab-search icons) and
Chromium's horizontal toolbar appears on top: omnibox with the full URL, "Install", bookmark star, profile avatar, three-dot menu.
This is the "looks like Chromium vertical tabs" report.

### B2. After collapsing and expanding the sidebar, every page is drawn shifted left and clipped — blocker
Steps: B1, then click the rail's toggle button to expand again.
Expected: page laid out inside the card.
Actual (s1/024-clipped-persist-2x.png, s1/025-wiki-after.png, s1/026-mdn-back.png): web contents are still laid out from x≈78 (the
collapsed layout), the card starts at x≈262, so ~184 pt of every page's left side is cut off (MDN "ML", "DB."; Wikipedia logo and
article column cut). Persists across tab switches, on every tab, and after resizing the window (s1/027-resized.png).

### B3. Cmd+S opens Save Page ("Save As…, Web Page, Complete") instead of toggling compact mode — major
Steps: any page, press Cmd+S. Expected (Zen keyboard-shortcuts.md §1): compact mode toggles. Actual (s1/015-cmdS.png): macOS Save sheet.
(Known: the shortcut work is #90, not merged in this build.)

### B4. Cmd+L on the startup page opens Chromium's omnibox dropdown squeezed into the sidebar — major
Steps: fresh launch, Cmd+L. Expected (Zen, main-22-cmd-l-panel.png): the floating centred URL bar.
Actual (s1/003-cmdL.png): a 230 pt dropdown inside the sidebar: "→ Pres…" (truncated hint), an "AI Mode" chip, Google "Trending searches"
with thumbnails, a "+" button. Only after typing does it turn into the floating panel (s1/007-keys.png).

### B5. Floating URL bar is Chromium's omnibox: "AI Mode" chip, Google entity rows, "+" button — major
Steps: Cmd+T (s1/010-cmdT.png) or type in Cmd+L (s1/007-keys.png, s1/011-typed-gh.png).
Expected (Zen main-21/main-23): Zen's panel, no AI Mode chip, no "+" button, Zen rows. Actual: Chromium's AI Mode chip on the right,
"wik - Google Search" rows with entity thumbnails, a round "+" (add context) button at the bottom.

### B6. Startup shows Chrome's Google New Tab Page and Chrome branding — major
Steps: fresh launch. Expected (Zen, s1/002-zen-launch.png): no page, the empty state. Actual (s1/001-launch.png): Google NTP with
"Gmail", "Images", apps grid, "Web Store" shortcut, "Customize Koan" button; the tab's favicon is the Chrome logo.
(Known: the empty tab/new tab page is #96, not merged.)

### B7. Chromium infobar "Relaunch the browser to load your profile data and keep it encrypted" on first launch — major
Steps: fresh profile launch. Actual (s1/001-launch.png): Chromium infobar with a "Relaunch" button above the page (shown again on the
Wikipedia tab, s1/025-wiki-after.png). Zen shows nothing like it.

### B8. Menu bar is Chromium's (Profiles, Tab; no Spaces, Tools); View menu has bookmarks bar, "Show tabs vertically", AI Mode — major
Expected (Zen main-18/19/20, s1/002-zen-launch.png): Zen File Edit View Spaces History Bookmarks Tools Window Help; View = Appearance,
Toolbars, Sidebar, Collapse Pinned Tabs, Zoom, Page Style, Enter Full Screen. Actual (s1/016-view-menu.png): Profiles and Tab menus;
View = Always Show Bookmarks Bar, Always Show Toolbar in Full Screen, Always show full URLs, Always show AI Mode, Show tabs vertically ✓,
Collapse Vertical Tabs ⇧⌘L, Cast…, Developer. (Known: #90.)

### B9. Sidebar top: URL pill has no pill background; Chromium tune icon; collapse icon is "≡<"; bottom-left icon looks broken — minor
Expected (Zen ref-01): a rounded grey pill with the site identity icon on the left; Zen's sidebar icon; the bottom row has the Space dots.
Actual (s1/001-sidebar-crop.png, s1/009-wiki.png): plain text "Search..."/host with no box; Chromium's "tune" site-settings icon on the
right; "≡<" icon; bottom-left is a tiny two-dot glyph; no Space indicator dots.

### B10. Compact mode: traffic lights stay drawn on top of the web page — major
Steps: right-click empty sidebar > "Enable compact mode", pointer on the page. Expected (Zen s1/021-zen-page-compact.png): with the sidebar
hidden no traffic lights are drawn over the page. Actual (s1/029-compact-on.png, s1/031-edge-leave.png): red/yellow/green buttons over the
page's top-left corner (over MDN's banner logo).

### B11. Compact mode hover reveal: the sidebar's top row breaks into two rows with an empty gap — major
Steps: compact mode on, move the pointer to the window's left edge. Expected (Zen main-15-compact-hover-reveal.png): one top row (traffic
lights, sidebar button, back/forward/reload), the URL pill right under it, floating sidebar with rounded corners over the page.
Actual (s1/030-edge-hover.png): traffic lights alone on the first row, sidebar button + back/forward/reload on a second row (y≈115), ~100 pt
of empty space, then the URL pill at y≈224; the sidebar is flush with the window edge, no float/rounding.

### B12. CRASH: closing a window while compact mode is on (SEGV in BrowserAnimationController::GetMotionInfo) — blocker
Reproduced twice (pids 14585 and 18578), deterministic.
Steps: right-click empty sidebar > "Enable compact mode"; then close the window: (a) Cmd+W on the last tab (crash 1, 13:23:28), or
(b) click the red close button (crash 2, 13:26:5x). Expected (Zen): the window closes (or stays with the empty tab), no crash.
Actual: "Koan quit unexpectedly" (s1/036-relaunch-after-crash.png, s1/044-crash2-dialog.png). Log: `Received signal 11 SEGV_ACCERR 000000000008`.
Symbolized with atos (base from the minidump):
```
BrowserAnimationController::GetMotionInfo(BrowserAnimationGroup, BrowserAnimationMotion) const + 92
BrowserAnimationController::Start(...)
VerticalTabStripRegionView::RequestCollapse(bool) + 240
tabs::VerticalTabStripStateControllerImpl::RequestCollapse(bool) + 40
koan::KoanCompactModeController::Deactivate(bool) + 1308
koan::KoanCompactModeController::OnBrowserViewDestroying() + 172
koan::KoanBrowserViewController::DestroyFor(BrowserView*) + 72
BrowserView::~BrowserView() + 240
... views::Widget::~Widget, BrowserWidget::~BrowserWidget, Browser::~Browser, BrowserManagerService::DeleteBrowser,
UnloadController::OnWindowCloseComplete
```
(Compact deactivation during BrowserView teardown starts a collapse animation on an already-destroyed animation controller.)
Logs/dumps: s1/logs/koan-1-crash1.log, koan-1-crash2.log, crash1-pid14585.dmp, crash2-pid18578.dmp.

### B13. Closing the last tab closes the window — major
Steps: compact off or on, Cmd+W until the last regular tab closes (s1/034-one-left.png -> s1/035-last-closed.png).
Expected (Zen live-results "Empty tab": closing the only tab keeps the window, selects the empty tab, sidebar shown, URL bar not opened).
Actual: the window closes (and with compact on, crashes, B12).

### B14. After closing a tab onto the NTP the floating URL bar opens by itself; Esc and Cmd+W are then ignored; Cmd+W on the last NTP recreates it — major
Steps (restored session, compact on): Cmd+W on the GitHub tab -> the window shows an NTP with the floating "Search…" bar already open
(s1/040-repro-close.png). Esc: nothing. Cmd+W: nothing (s1/041-after-cmdw.png). A click outside closes the bar (s1/042). Cmd+W on that NTP:
the tab is replaced by another NTP with the bar open again (s1/043-closed-again.png): the window can't be closed with Cmd+W.
Expected (Zen): the URL bar never opens by itself after a close; Esc closes it in one press.

### B15. After "Restore pages" a second Space appears (two dots in the sidebar's bottom row) — major
Steps: crash (B12), relaunch the same profile, click "Restore" in the Chromium "Restore pages?" bubble, hover to reveal the sidebar.
Expected: one Space ("Space"), the restored tabs in it. Actual (s1/039-restored-hover.png): two Space dots at the bottom; the NTP that was
open at startup is no longer in the current Space (it seems to sit in a phantom second Space). Also: only the last tab (GitHub) was
restored, not the 6 tabs that were open before (they were closed one by one first, so this part may be correct).

### B16. Crash recovery is Chromium's "Restore pages? Koan didn't shut down correctly" bubble over Chrome's NTP (AI Mode, Create images) — minor
s1/037-after-ignore.png. Zen restores the previous session itself; no Chrome bubble, no Google NTP.

## Session 2 (preview 09d39ad9, profile-2, 13:27 IST)

### B17. "Create Space" button does nothing; only Enter in the name field creates the Space — major
Steps: sidebar bottom "+" > Create Space, type "Work", click "Create Space" (twice, 2 s apart). Expected (Zen spaces.md): the Space is created and
selected. Actual (s2/009-space-created.png, s2/011-second-click.png): the form stays. Enter in the name field works (s2/012-enter.png).

### B18. Space indicator dots at the bottom never show which Space is active — minor
s2/013-ctrl1.png vs s2/014-cmdopt-right.png: two identical grey dots in both Spaces. Zen highlights the current Space (icon / filled dot).

### B19. Pinned tabs are listed below "+ New Tab" with no divider, mixed with normal tabs — major
Steps: right-click a tab > Pin (s2/004-menu2.png -> s2/005-pinned.png). Expected (essentials-and-pinned.md: the pinned section sits between the Space
header and the divider, normal tabs under "+ New Tab"). Actual: the pinned GitHub tab is just moved to the top of the normal list, under "+ New Tab".

### B20. Tab context menu is not Zen's — minor
s2/002-tab-menu.png vs main-17: no "Move to Folder ▸", no "7 / 12" count on Add to Essentials, Chromium items "Move Tab to New Window",
"Add Tab to Reading List", "Show tabs horizontally", labels "Reload"/"Pin"/"Duplicate" instead of "Reload Tab"/"Pin Tab"/"Duplicate Tab".

### B21. Glance side buttons are blank white discs (no ×, expand, split icons) and overlap each other — major
Steps: Opt-click a link (s2/020-glance.png, crop s2/020-glance-buttons-crop.png). Expected (main-16-glance.png): three round grey buttons with
the close, expand and split icons, spaced apart. Actual: three plain white circles nearly touching. (Esc closes, Cmd+O expands: OK.)

### B22. Cmd+Opt+V on the last tab splits it with a brand-new Google NTP; split panes carry Chromium footers — minor
s2/023-split-v.png. Zen splits with the next tab; split panes have no favicon/× footer strip (main-08-split-view.png).

### B23. Space rename toast says "Workspace has been successfully renamed!" — minor
s2/017-renamed.png. Zen's wording uses "Space"; also after Enter the URL pill takes focus (shows the Chromium page-info/install/star icons).

### B24. Library has no Routing/Media/Boosts sections and no bottom tools; it pushes the page instead of covering it — minor
s2/025-library.png vs main-09-library.png.

## Session 3 (#90 build e64e88e4, profile-3, 13:33 IST)

Pass: Cmd+S toggles compact from a page and from the URL bar, no Save dialog (s3/004, s3/006, s3/008); Cmd+Opt+Shift+S opens Save Page
(s3/009); Spaces menu Create Space works; Cmd+Opt+Left/Right switch Spaces from the URL bar and a page and wrap (s3/012-014).

### B2 (update). The clipped-page bug also happens on every Cmd+S compact-off — blocker
Cmd+S on, Cmd+S off: the page stays laid out for the hidden sidebar and its left ~230 pt are cut off (s3/005-cmdS-off.png, s3/008-cmdS-from-urlbar.png).
With #90 making Cmd+S the compact key this will hit every user who tries compact mode.

### B25. #90 menu bar still has Chromium's Profiles and Tab menus, no Tools menu; "Create Space" shows ⌃⌥N — minor
s3/010-spaces-menu.png vs main-20-menubar-spaces.png (Zen: File Edit View Spaces History Bookmarks Tools Window Help; Create Space has no key).

### B26. On launch the floating URL bar is already open over the NTP, overlapping its search box — major
s3/001-launch.png (fresh profile, #90 e64e88e4). Zen opens with nothing focused (live-results: "no URL bar open").

## Session 4 (#90 build 746ea88f, profile-4, 13:36 IST): the PR #90 checklist

| Check | From a page | From the URL bar | From a Glance |
|---|---|---|---|
| Cmd+S toggles compact, no Save dialog | PASS (s4/002) | PASS (s3/006, e64e88e4) | PASS (s4/009) |
| Cmd+Opt+Shift+S Save Page | PASS (s3/009) | – | PASS (s4/011) |
| Cmd+Opt+Left/Right Spaces | PASS, wraps (s3/013-014) | PASS (s3/012) | PASS (s4/023); the Glance is closed on return (s4/024) |
| Cmd+Shift+N one new window, not Incognito | PASS | PASS (s4/015) | PASS (s4/012); held 1.5 s: one window (s4/014) |
| Cmd+O | nothing without a Glance: PASS (s4/007) | – | expands: PASS (s4/025) |
| Cmd+Shift+D pin/unpin | PASS (s4/004-006) | PASS (s4/019 "Unpin") | see B30 |

Also PASS: Cmd+U opens view-source in a new tab (s4/026); Esc does not stop a loading page (s4/027-028); every menu-bar menu opened with no crash
(s4-menus-1.jpg, s4-menus-2.jpg); View > Appearance "Websites will use:" (disabled) / Automatic / Light / Dark, the tick follows (s4/029-030 crops);
Spaces menu: Create Space, Edit Theme…, Change Name… (inline rename), Change Icon… (emoji picker), Next/Previous Space, the Space list with
⌃1/⌃2 and the icon, Move To ▸ Work (moved the tab), File > New Blank Window ⇧⌘N / New Private Window ⇧⌘P.

### B27. Rebinding "Toggle Compact Mode" in chrome://koan-settings is not saved; menu label and keys unchanged — major (#90)
Steps: chrome://koan-settings, find "compact", click the Toggle Compact Mode field (⌘ S), press ⌃⇧K: the field shows "^ ⇧ K" (s4/047-rebound.png).
Click elsewhere. Expected (#90): the View menu shows ⌃⇧K for Toggle Compact Mode, ⌘S stops working, ⌃⇧K toggles.
Actual: View menu still "Toggle Compact Mode ⌘S" (s4/048-crop.png); ⌘S still toggles (s4/049-old-cmdS.png); ⌃⇧K does nothing (s4/050);
after reloading the settings page the field is "⌘ S" again (s4/052). Pressing Enter after the new key records "↩" as the shortcut (s4/053) and
is not saved either (s4/054).

### B28. Spaces > Edit Theme… shows a bare radio list of 9 colour names; choosing one changes nothing visible — major
s4/042-after-esc.png, s4/043-green.png. Expected (Zen, picker-01-open.jpg): the gradient theme picker (colour field, dots, presets, light/dark,
texture). Actual: "Default / Blue / Cyan / Green / …" radio list over the sidebar; after "Green" the sidebar and window look the same.

### B29. Change Icon… emoji picker stays open after picking an icon and follows to the other Space — minor
s4/033-icon-set.png, s4/037-moved.png. Zen closes the picker on selection. Esc closes it (s4/038).

### B30. Pin state is invisible: pinned and unpinned tabs look identical (see B19), so Cmd+Shift+D from a Glance can't be judged — minor
Cmd+Shift+D in a Glance then once more in the URL bar left the tab "Pin" (s4/018-pinstate-crop.png); one press from the URL bar alone gives
"Unpin" (s4/019-crop.png). So the Glance press probably pinned the parent (Zen's behaviour) but nothing on screen shows it.

### B31. Spaces menu "Create Space" shows ⌃⌥N; File/Window/Help are Chromium's (Profiles and Tab menus, "Show tabs vertically", AI Mode) — minor
s4-menus-1.jpg, s4-menus-2.jpg vs main-18/19/20.

## Issues filed (2026-11-04 13:55 IST)
B12 #130 · B2 #131 · B1 #132 · B10+B11 #133 · B13+B14+B26 #134 · B4 #135 · B17 #136 · B19(+B30) #137 · B21 #138 · B27 #139 · B15 #140 · B7 #141.
Commented on existing: B5 -> #101, B6/B22 -> #95, B28 -> #41, B3/B8/B25/B31 -> #71, PR #90 checklist -> #90.
Not yet tested: fullscreen + top peek, folders, split view ⌘⌥H/G, Incognito window, small window, ⌘N second window use, quit/relaunch session restore, onboarding (first run), trackpad swipe, #129 re-test.

## Tester incident (2026-11-04 14:17)
A mistyped coordinate (x=50, outside my Koan window) clicked the user's desktop Calendar widget and launched Calendar.app (pid 54265). I quit that pid at once (it wasn't running before). The input tool now refuses any click whose topmost window isn't one of my own pids.

## Session 5 (14:13 IST): fix builds and fullscreen

| Build | Check | Result |
|---|---|---|
| Koan-release-129-4d20b7c5 | compact on, red close button | **no crash**; window closes, app stays (s5) — #130 fixed |
| same | compact on (Ctrl+S), ⌘W to the last tab | **no crash** — #130 fixed; the window still closes (#134) |
| same | ⌘N after closing | new window **not** in compact mode: Zen's compact is global — minor |
| KoanCheck-c90g (#90 e75acc5b) | rebind Toggle Compact Mode to ⌃⇧K, click away | field green, ⌃⇧K; **saved after reload**; old ⌘S **dead**; ⌃⇧K toggles — #139 fixed (s5-011/016-crop, s5-013/014). Menu bar now Koan File Edit View Spaces History Bookmarks Tools Profiles Window Help (Tab gone, Tools added; Profiles still extra). View has no "Toggle Compact Mode" item to show the new key (s5-012-crop): **check** |
| KoanCheck-c142 | Ctrl+S on/off ×3 | **no clipping** — #131 fixed (s5-020) |
| c142 | top-row sidebar button | still Chromium rail + toolbar (expected: c143 fixes) (s5-021) |
| KoanCheck-c143 | top-row sidebar button on/off | toggles compact, no Chromium rail or toolbar — #132 fixed (s5-030, 033); revealed top row still split (#133); clipping on off (#131, not in this build) |
| KoanCheck-c144 | compact: lights over the page | **gone** (s5-040); hover reveal: one top row (lights, toggle, back/fwd/reload), pill under it — #133 fixed (s5-041) |
| c144 | ⌃⌘F fullscreen with compact on | **B32** below |

### B32. Fullscreen: compact mode is dropped (sidebar shown, page clipped) and the top peek shows Chromium's full toolbar — major
Build: KoanCheck-c144 (develop + #144); same top peek as the user's report on the preview.
Steps: compact on, ⌃⌘F, then move the pointer to the top edge.
Expected (Zen fullscreen-and-notch.md, live-results: compact + fullscreen → card edge to edge (0,0,1470,923), sidebar only on left-edge hover;
the top shows only the macOS menu bar; no toolbar, no bookmarks bar).
Actual: (1) on entering fullscreen the sidebar is shown and the page is drawn shifted/clipped under it (s5-050-fs-compact.jpg);
(2) the top peek shows Chromium's toolbar under the menu bar: traffic lights, back/forward/reload, site-settings icon + "github.com" omnibox,
Install, bookmark star, profile avatar, ⋮ (s5-051-fs-top-peek.jpg). Exit restores compact correctly (no crash).

## Session 6 (14:21 IST, KoanCheck-c144, profile-10): click everything, normal window
View-tree inventory: `inv-normal.tsv` (UI DevTools; every shown view). Checklist: `inventory.md`.

### B33. Tab menu "Show tabs horizontally" turns Koan into Chrome; going back loses the Space header — major
Steps: right-click a tab or an Essential > "Show tabs horizontally". Then right-click a tab > "Show tabs vertically".
Expected (Zen): no such item (Zen has no horizontal tab strip).
Actual: Chromium's horizontal tab strip with tab-group chips "Unfocus" and "Space" (the Space is a Chromium tab group) and the full
Chromium toolbar (omnibox, "Ask" AI button, Install, star, avatar, ⋮) (s6-026-horizontal.jpg, s6-027-htab-menu.jpg). After
"Show tabs vertically" the sidebar is back but the **Space header row ("Space" + options) is gone** (s6-028-back-vertical.jpg, s6-033).

### B34. "Tabs on the right" removes the traffic lights — major
Steps: right-click empty sidebar > Tabs on the right. Expected (Zen): sidebar on the right; traffic lights stay at the top left.
Actual: no close/minimise/zoom buttons anywhere (s6-031-tabs-right.jpg, s6-031-corners.jpg). Turning it off brings them back (s6-033).

### B35. Empty-sidebar menu has 2 items; Zen has 10 — minor
s6-030. Koan: Enable compact mode, Tabs on the right. Zen (context-menus.md §8): Enable compact mode, Hide sidebar / Hide toolbar / Hide both,
New Folder, Live Folder ▸, Theme…, Tabs on the right, Customize Toolbar….

### B36. Pill "Site information" opens Chromium's page-info bubble — minor
s6-004-siteinfo.jpg (Connection is secure / Cookies and site data / Site settings). Zen: its site-data panel, with the identity icon on the
left of the pill, Clear site data, Add Extensions, All Site Settings.

### B37. Space options menu: no Manage Spaces…, Share Space…, Space Routing Settings; "Edit Theme…" vs Zen "Theme…" — minor
s6-006-space-options.jpg.

### B38. Create New (+) popup has no "Create Folder" — minor
s6-012-create-new.jpg: Create Space / New Split / New Tab. Zen: Create Space, Create Folder, ---, New Split, New Tab; the "+" rotates 45°.

### B39. After Clear, Cmd+Shift+T restores one tab, not the cleared batch — minor (check in Zen)
s6-022 -> s6-023/024: two tabs cleared, one ⇧⌘T brought back only GitHub.

### B40. Tab/Essential context menus still carry Chromium items — minor (extends B20)
s6-001-menu-check.jpg, s6-025-essential-menu.jpg: "Move Tab to New Window", "Add Tab to Reading List", "Show tabs horizontally", "Mute Site";
missing Zen's Move to Folder, New Folder, Bookmark Tab…, Move to Start/End, Select All Tabs, Close Duplicate/Multiple, Reopen Closed Tabs,
Add Route for Domain, "1 / 12" on Add to Essentials.

PASS this session: Copy URL (clipboard + "Copied current URL!" toast, s6-003); Unload Space (toast, s6-007); Clear (toast "Tabs Closed! Use ⇧⌘T to undo.",
keeps the selected tab, s6-008); "+ New Tab" row opens the floating bar (s6-010); Library button, rail Downloads / Spaces / History, Filter
(When / Sort / Closed tabs), ← close (s6-016..021); Essential menu has Remove from Essentials, Change Icon…, Edit Essential URL ▸, Reset Essential Tab
and no Close/Pin (s6-025). Note (dump): the whole Chromium ToolbarView stays alive in the tree, hidden only by its layer (TopContainerView
layer visible=false); it is what the fullscreen peek shows (#146).

### B41. Split Link from an Essential: the split lands inside the Essentials tile and the sidebar's bottom bar is drawn twice — major
Steps: open the Wikipedia Essential, right-click a link ("China") > Split Link to New Tab.
Expected (Zen split-view.md): the two tabs split side by side; the new tab is a normal tab in the Space list; one bottom bar.
Actual (s6-043-split-link.jpg): the split works, but the Essentials tile now shows both favicons (W + globe), so the new tab became part of the
Essential; the bottom row (Library button + "+") appears **twice** (y≈855 and y≈899). Panes carry Chromium footers ("en.wikipedia.org ×", "×").
(The Space header came back here.)

### B42. Page context menu is Chromium's (Cast…, Google Lens "Search this tab with image search", Create QR code, Open in reading mode) — minor
s6-041-page-menu.jpg. Link menu has Zen's Split Link to New Tab and Open Link in Glance (OK) plus Chromium's "Copy link to highlight", "Search Google for…" (s6-042).

## Session 7–11 (14:29–14:45 IST): fix builds

| Build | Result |
|---|---|
| KoanCheck-c136 probe | Create Space / Cancel / Edit Theme… real clicks: nothing; buttons only get move/enter/exit events (lines sent to the loop) |
| Koan-release-129-5560b0c0 | PASS: no crash on red close in compact, close with the Space form open, close after ⌘⌥S, toggle+close within 50 ms (both ways), quit in compact; relaunch reopens both windows in compact |
| KoanCheck-c128b | fullscreen peek: no Chromium toolbar now, but an empty ~32 pt grey strip under the menu bar covers the page (#146 comment); compact+fullscreen edge to edge, left edge reveals sidebar. Also: ⌃⌘F hid my crash dialog behind fullscreen |
| KoanCheck-c147 | **crash on launch** (DCHECK label.cc:1005) |
| KoanCheck-c147b | PASS: launches; rounded grey pill, no tune/page actions; Space dots show the current Space and switch on click (s10-003-crops) |
| KoanCheck-c145b | #136 PASS (Create, Cancel, Edit Theme… on real clicks; no window drag); **#140 still FAILS**: after SIGSEGV + Restore a second Space also named "Space" appears with the startup NTP (s9-012/013) |
| KoanCheck-c150 | (1) pin → above the divider PASS (s11-004); (2) unpin → back below New Tab live PASS (s11-008); (3) dash on loaded pinned hover, × after unload PASS (s11-005-crop, s11-006); (4) each Space shows its own pinned run, 18 switches no crash PASS (s11-c-spaces); horizontal↔vertical round trip no crash, but the Space header row is gone afterwards (#148 still) (s11-017); ⌘⇧D is Chromium's Bookmark All Tabs on this base (no #90) — checks done via the menu; (5)(6)(8) not done yet |

Minor: B43 a single ⌘Q only shows Chromium's "Hold ⌘Q to Quit" bubble (s7-017) — Zen quits (with its own confirm) on one press; to verify in Zen.

## Session 12–15 (15:10–15:24 IST): c145d, c151, c128c, c90h
Incident 15:10–15:13: the Mac loop's scripted run kept driving while I was on screen (its gate counted my keys as its own); my guard stopped my input at once. The loop paused at my request and now needs an explicit "screen free" grant.

| Build | Result |
|---|---|
| KoanCheck-c145d (#145 52cf4214) | (a) 3 tabs, SIGSEGV, Restore: ONE Space, no dots PASS (s12-005). (b-i) quit/relaunch with "Space"=only NTP + "Work": both Spaces, empty Space kept PASS. (b-ii) SIGSEGV + Restore: exactly two Spaces, empty "Space" survives, Ctrl+3 nothing PASS (s12-c-ii, c-ii2); minor: Restore selects "Work" though "Space" was active. (c) close last tab of Work: keeps a new tab, window stays PASS (s12-c-019). (d) #136: Edit Theme list, Create, Cancel PASS (s12-c-d). No crash. |
| KoanCheck-c151 (#151 258305fe) | (1) wikipedia.org pill shows W favicon PASS; (2) NTP pill "Search..." + magnifier PASS (s13-c-12); (3) no "Press Tab" hint PASS, AI Mode chip still shows (expected w/o #96) (s13-004/006); (4) ⌘⇧W with floating bar and with pill editing, hold-⌘Q with bar open: no crash PASS; (5) 🚀 Space emoji unclipped current and hovered PASS (s13-c-emoji); (6) resting pill rgb(54,53,53) on 24 ≈ 13.5% white PASS. |
| KoanCheck-c128c (#128 06db10fd) | fullscreen top peek: menu bar only, no grey strip PASS (s14-001; a transparent Koan window 0,33 1470×115 exists during the peek); Tabs on the right: lights stay top-left above the page PASS (s14-003); fullscreen + right: no strip, no lights, 8 pt gutter PASS (s14-004); off again normal (s14-005). No crash. |
| KoanCheck-c90h (#90 2717a7b4 + #148) | tab menu: no "Show tabs horizontally" PASS (s15-001); settings/appearance: no "Tab strip position" PASS (still Chromium rows: AI Mode side panel, tab search, bookmarks bar — minor, #101) (s15-002); ⌘⇧D pins, ⌘S compact, ⌘⇧P Incognito PASS (Incognito page says "Chromium won't save" — minor) (s15-005); profile copy with vertical_tabs.enabled=false starts vertical PASS (s15-006). |

### Tester incident 15:43: a click at (1397,16) hit the macOS clock in the menu bar (Notification Center); closed with Esc at once. The guard now refuses menu-bar clicks right of the app menus (x≥660).

## Session 16 (15:43 IST): KoanCheck-m90 (develop 39294e04 + #90 2717a7b4), merge-line pass
PASS: ⌘S toggles compact and back with no clipping (s16/001-002); ⌘⇧D pins (s16/003); ⇧⌘L opens the inspector (s16/004); ⌘⇧P opens an Incognito window (s16/005);
tab menu has no "Show tabs horizontally" (s16/006); settings/appearance has no "Tab strip position" (s16/007); a profile copy with vertical_tabs.enabled=false starts vertical with its session (s16/009). No crash, no FATAL/CHECK.
Minor: a second ⇧⌘L does not close DevTools; compact draws the traffic lights over the page (expected until #144); Incognito page says "Chromium won't save"; Chrome's appearance page keeps AI Mode / tab search / bookmarks-bar rows (#101).

## Session 17 (16:31 IST): plain develop 77a44141 (KoanCheck-d77)
### B44. develop 77a44141: closing any window crashes (SEGV in Widget::IsFullscreen ← KoanCompactModeController::UpdateCaptionButtons ← Deactivate ← OnBrowserViewDestroying) — blocker
Hands-on repro: fresh profile, open Wikipedia, click the red close button (compact OFF) → "Koan quit unexpectedly", `Received signal 11 SEGV_ACCERR 0x160`. Regression from the #128 merge; fix in #129 (5ece4c30/baafd745).
