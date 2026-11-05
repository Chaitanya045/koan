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

## Session 18–20 (16:50–18:47 IST)
| Build | Result |
|---|---|
| KoanCheck-d77 (plain develop 77a44141) | red close with compact OFF → SEGV in Widget::IsFullscreen ← UpdateCaptionButtons ← OnBrowserViewDestroying (#153 filed, blocker) |
| KoanCheck-c129t (#129 baafd745) | no crash: red close plain/compact, ⇧⌘W in fullscreen, ⌘W last tab in compact+fullscreen (keeps a new tab), ⇧⌘W with the Create Space form open in fullscreen, hold-⌘Q with a fullscreen window; after Koan > Quit Koan, relaunch reopens both windows in compact PASS (s19-a, s19-004). Minor: a held ⌘Q once did not quit (Warn Before Quitting on) |
| KoanCheck-c157 (#152 babe488c + #157 d94fb1fb) | video page-fullscreen, single toolbar and compact: pointer 2 s at top and left edge, nothing slides in, video fills below the menu bar, no gap/radius; Esc restores (s20-a, s20-b) PASS. View menu has no "Always Show Toolbar in Full Screen" PASS (s20-c9). Close window during video fullscreen and during browser fullscreen: no crash PASS. **B45 (major, #157):** on a page whose keydown listener calls preventDefault (data: test page), ⌘L does nothing and ⌃⌘F does not enter fullscreen, windowed or in fullscreen; ⌘T/⌘W/⌘N still act (s20-c, s20-d, s20-e). ⌘L works on example.com (s20-f) |

## Session 21 (19:16 IST): KoanCheck-m158 (develop 17d35100 + #158 65aa2d4e)
PASS: popup under the card, rounded, no AI Mode chip, no "+", no entity thumbnails, no trending (s21-a, s21-c78b); ⌘L floats at once with the URL selected (s21-a); a click on the pill edits in the pill (s21-b); a fresh NTP doesn't open the bar by itself (s21-a); "@" shows only @history/@bookmarks/@tabs, no @gemini/@aimode (s21-c78b); first run with --search-engine-choice-country=DE: the welcome's own engine page lists the DE engines, DuckDuckGo chosen, no Chromium choice dialog, searches go to DuckDuckGo (s21-014-w4, 019).
**FAIL B46 (#158):** ⌘T with empty text shows no rows at all: no open tabs with "Switch to tab", no top sites (s21-c910). Rows appear only after typing ("wiki" → open tab with Switch to this tab).
Minor: suggestion rows are ~40 pt apart (plan: 49 pt); first run shows Chromium's "Continue where you left off… Settings" infobar plus the "Relaunch… encrypted" infobar (#141).

## Session 22 (19:21–19:28 IST): KoanCheck-m150 (develop 17d35100 + #150 3823d8f3) — partial, paused for #145
PASS: pin via menu → above the divider; ⌘W ×4 with pinned tabs (⌘W on a selected pinned tab switches away, keeps the pinned row unloaded, Zen default); closing the last unpinned tab selects a pinned tab, window stays; ⇧⌘W and Koan > Quit Koan with pinned tabs: no crash/dump; relaunch restores the pinned tabs; (6) with 20 tabs the pinned run, divider and New Tab scroll with the list, Space header and pill stay put.
NOT TESTABLE here: (5) collapsed sidebar — this base has no #143, the button gives Chromium's rail (no New Tab icon; on hover it expands with Chromium's "Tab groups / Tab search" header): re-check on a #143 base. Tab-key focus order — macOS keyboard navigation (AppleKeyboardUIMode) is off and is a system setting.
TODO when resumed: (8) drag a tab into/out of the pinned section; Space switch (Ctrl+1/2, dots) with pinned tabs: rows must not jump.
Tester slip: a click meant for the rail toggle hit my own window's red close button (no crash).

## Session 23–24 (20:08–20:12 IST)
**m150 (#150) finished:** (8) drag GitHub from the list into the pinned section → it becomes pinned (menu "Unpin", dash on hover); it landed above Wikipedia (drop at the edge) (s23-drag-in); drag it back below New Tab → unpinned (s23-drag-out). Space switch Space↔Two ×3 with a pinned run in each: frames at +120 ms already settled, identical row positions, each Space shows only its own pinned run (s23-switch). No crash. **#150 hands-on: PASS** except (5) and Tab order not testable on this base / Mac.
**m158c (#158 31cb19af, SHA 56f6180b…): ⌘T-empty still FAILS:** on a page, an empty ⌘T (fresh, or after typing and deleting, waited 2.5 s) shows only the "Search..." field, no open-tab rows, no top sites (s24-empty-cmdT, s24-empty-cmdT-wait). On the startup NTP the first ⌘T showed no bar at all (s24-001). Typed rows work: "wiki" → open tab row with "Switch to this tab", Enter switches to the existing tab (s24-switch-to-tab).

## Session 25–26 (20:13–20:21 IST): KoanCheck-c159 (develop 17d35100 + #159 32d629f2), Spaces lockdown — partial
No "Spaces:" DCHECK, no FATAL/CHECK, no dump in any log (4 launches).
| # | Result |
|---|---|
| 1 G1 | **FAIL**: Edit Theme > Blue is selected (s25-blue-selected) but the window doesn't change: sidebar rgb(24,23,23) and card identical before/after (s25-blue); Spaces tint off/on changes nothing (s25-tint-toggle). (Zen's dark default look; may be the look's fixed tokens overriding the tint) |
| 5 G2 | PASS: ticker page in Space A, 15 s in Space B: "ticks 23 / seconds 23" (s26-ticker). chrome://discards is disabled in this build ("Internal debugging pages are currently disabled") |
| 7 G4 | PASS: ⌘⌃P opens Create form; ⌘⌃W "Delete Space?" (Cancel keeps; disabled with one Space); ⌘⌃X/Z next/prev with wrap (s25-xz-wrap); ⌘⌃C floating bar, Esc leaves no tab |
| 10 G7 | **FAIL**: Tab menu has no "Group Tab" (s25-tabmenu), but with ⌥ held the alternate "Group Selected Tab" (disabled) shows (s25-tabmenu-opt). Popup window part not done |
| 12 G9 | PASS: startup "New tab page", 3 Spaces (C with 🚀, B blue), Quit + relaunch: Space/B/🚀C in order, each a new tab, on C (s26-g9-relaunch); last window closed + ⌘N: the Spaces come back on B. kill -9 + relaunch: one Space; **Restore gives 4 Spaces incl. a second "Space" with the startup NTP** (s26-restore-4spaces) — the #140 duplicate, fixed by #145 which isn't in this base |
| 13 G10 | PASS: close a tab of B2, Delete B2 (Yes), ⌘⇧T → tab in the shown Space, B2 gone (s26-g10); close B's last page, ⌘⇧T → back in B (s26-g10b) |
| 14 G11 | PASS: 20 switches + 10 s, no Focus/Unfocus promo |
| 17 G14 (part) | PASS: 20 rapid ⌘⌃X switches then ⇧⌘W mid-animation: no Spaces: DCHECK, no crash |
| 2, 3, 4, 6, 8, 9, 11, 15, 16, 17 rest | not done yet |

## Session 27 (2026-11-05 07:22–07:29 IST): KoanCheck-c159d (develop dcf9bdaa + #150 b78d5b75 + #159 e08ddea5)
No "Spaces:" DCHECK, FATAL or dump in any of 5 launches.
| # | Result |
|---|---|
| 1 | **FAIL** (again): Blue selected, sidebar 24,23,23 unchanged, pill 48→49/50/53 (invisible) (s27-a) |
| 2 | not judgeable while 1 fails (both windows look identical) |
| 8 | PASS with --disable-features=UseActionsForBrowserCommands: ⌘⌃P adds one tab, no group/bubble (s27-b); ⌘⌃X/C fine; **⌘⌃W closes every tab of the Space except one, with no "Delete Space?" prompt** (s27-z: 3 tabs → 1); ⇧⌘T brings them back (s27-z17) |
| 9 | PASS: no group editor bubble under the flag |
| 10 | **FAIL** (again): ⌥ shows disabled "Group Selected Tab" (s27-c4). Popup Tab menu unchanged (Group Tab, disabled) PASS; Incognito: Group Tab back PASS (s27-f) |
| 16 | PASS: #tab-groups-focusing Disabled → Spaces shown as Chrome tab groups (collapsible headers), no crash (s27-focus-off) |
| 17 | PASS: Delete Space on a "Leave site?" page → prompt; Cancel keeps the Space and the page, no DCHECK (s27-leave-site, s27-c45). The Space's other tabs are already closed by then (partial delete) — minor. Popup and Incognito: no DCHECK. A tab dropped on another Space's dot does nothing (#76). Inactive main window drew an empty sidebar while the popup was front (gone after closing it) — minor |
| 3, 11, 15 | not run: they need a Chrome Web Store theme, a copy of the user's own Chrome profile, or installing an extension, which the test rules forbid |
| 4, 6 | not run (need an old-build profile / Chrome profile seed) |

## Session 28 (08:01–08:07 IST): KoanCheck-c150d (#150 b78d5b75) — PASS
Space switch Space↔Two with a pinned run in each, region stills every ~0.1 s: the switch slides, the incoming Space has its pinned run, divider and rows from the first frame, nothing grows in or jumps (s28-switch-frames). ⌘W ×6 in a Space with one pinned tab: alternates between the pinned tab (divider hidden when no normal tabs) and a fresh new tab, window stays, no crash (s28-cmdw). ⇧⌘W on a second window, Koan > Quit: no crash; relaunch restores both Spaces with their pinned runs (s28-relaunch). No Spaces: DCHECK, FATAL, dump or .ips.
Not testable: trackpad swipe (synthetic scroll events carry no gesture phase); Tab-key order (macOS keyboard navigation is off, a system setting); collapsed sidebar (no #155 in this base).
Tester slip: a misplaced right-click opened Change Icon… (emoji picker), closed with Esc.

## Session 29 (09:23–09:26 IST): KoanCheck-c155h (develop b7a833c6 + #155 51de0d10)
PASS: ⇧⌘L and the sidebar button toggle compact both ways, never a rail, no clipping (s29-a); ⇧⌘L with the URL bar focused and in fullscreen (s29-b); ⇧⌘A opens the floating URL bar (s29-b); dragging the sidebar edge to below the minimum: it narrows smoothly, never a 56 px rail, on release compact on + toast "Sidebar hidden! Use ^S…" (s29-c, s29-drag); menus: tab menu no "Show tabs horizontally", View "Collapse Vertical Tabs ⇧⌘L" and no "Show Tabs Vertically", sidebar button / empty sidebar / Space header menus only Koan's (s29-menus); settings/appearance no tab-strip row (s29-d); kill -9 in compact + Restore: hidden from the first frame, no slide (s29-kr). No DCHECK/FATAL/dump.
FAIL/unclear: keyboard resize — click on the edge then ←×12: width unchanged (s29-d left).
Minor: while dragging narrower the top row splits (traffic lights on their own row) before compact kicks in (s29-drag).
Not done: drag in compact's revealed sidebar, 10-minute hint (would hold the loop 11 min), toast-once (second drag).

## Session 30 (09:52–09:57 IST): KoanCheck-c90g (develop b7a833c6 + #90 c49738b1), --use-mock-keychain
PASS: no keychain infobar (s30-a); every menu opens, no crash (s30-m1, m2); ⌘S, ⌘⌥→, ⌘⌥⇧S, ⇧⌘N (one window), ⌘O (nothing) from a page and the URL bar (s30-keys); in a Glance ⌘S, ⌘⌥⇧S, ⇧⌘N, ⌘⇧D (pins the Glance's parent), ⌘O expands (s30-glance); Esc doesn't stop a load (s30-e); tab menu has no "Show tabs horizontally" (s30-tmc); rebind Toggle Compact Mode to ⌃⇧K: saved, ⌘S dead, ⌃⇧K toggles (s30-r2c, r34); "Toggle Sidebar's Width" in View. No DCHECK/dump.
FAIL: View still shows "✓ Show tabs vertically" (s30-m1); the Profiles menu is in the menu bar (the loop's note says hidden) (s30-m2).
CONFLICT: ⇧⌘L opens DevTools here (#90) vs compact in #155 (s30-e, 3rd frame).

## Session 31 (10:18–10:25 IST): KoanCheck-c162c (develop b7a833c6 + #162 a368dedb)
### B47. DCHECK crash while typing a data: URL with a space in the URL bar (autocomplete_result.cc:335) — major (DCHECK builds; release: wrong match contents)
Steps: fresh profile, ⌘T, type `data:text/html,<title>drag</title><body style=margin:0;background:%23345;color:white;font:20px monospace;...` key by key.
Actual: at the space after "20px", `FATAL autocomplete_result.cc:335 DCHECK failed: AutocompleteMatch::SanitizeString(match.contents) == match.contents`
(Type url-what-you-typed, Provider HistoryURL; contents end in a trailing space) ← AutocompleteResult::AppendMatches ← AutocompleteController::UpdateResult ←
OmniboxViewViews::DoInsertChar. Koan quit; "Koan quit unexpectedly" (mine, dismissed). Log: s31-dcheck-omnibox.txt.
#162 checks: drag on the top band moves the window and the page sees `move,down,up,CLICK` (s31-drag) — how far the window follows can't be judged
with synthetic events (control: a native drag in Zen also lagged: +7,+93 for +100,+100); 20 fast drags: no crash; after a drag, hovering the sidebar/pill
sends no `move` to the page (s31-hover) PASS; right-click during a drag: page menu, no crash (s31-mid). Not done: ⌘L/⌘T mid-drag (my guard refused them),
tiling, close/⌘Q during a drag, fullscreen.

## Session 32 (11:17–11:20 IST): KoanCheck-c163c (develop b7a833c6 + #163 8de6e92e)
PASS: ⌘D "Bookmark added" bubble right under the pill, fully on screen (s32-cmdD); notification prompt (S) at the pill (s32-notif-S); in compact with the sidebar hidden the prompt sits inside the window at the top left, never above it (s32-notif-CS); ⇧⌘W with the bookmark bubble open and hold-⌘Q with a prompt open: no crash/DCHECK/dump.
INCONCLUSIVE: keyboard answer — Tab never focuses the prompt's buttons (macOS Full Keyboard Access off, a system setting). Peek + page info: after clicking the pill's site icon in the peeked sidebar the sidebar showed as a blank panel (no rows, no page info) and stayed after Esc (s32-peek-close-quit, frames 1-2) — needs a re-check.
Not done: camera (would turn the Mac's camera on), CT/CB layouts, download panel, fullscreen.

## Session 33 (12:55–13:00 IST): KoanCheck-c162d (#162 451282b4)
PASS: top-band drag moves the window (221,55 → 270,77), page log `move,down,move,up`; 20 fast drags no crash; step 3: after a drag, moving inside the sidebar adds no event to the page (s33-hover, rows 2-3 identical); 8b: drag released over the sidebar, then a text drag-select out of the page over the sidebar: the page sees `down,move,up` once, never up before down (s33-select-out); 8: ⇧⌘W and held ⌘Q during a held drag: ignored while the move loop runs, no crash/DCHECK/dump. 
Minor: the drag-select out of the page left only one character selected (should extend to the line start).
Not judgeable: tiling — dragging to the left screen edge pushed the window off-screen (x −960) with no tiling preview (synthetic events; macOS tiling may need a real pause at the edge), so step 6 wasn't done.

## Session 34 (14:36–14:39 IST): KoanCheck-c155i (#155 1020a184 on develop ef76c876)
PASS: drag the edge of compact's revealed (peeked) sidebar: it doesn't resize at all, never a rail, compact stays (s34-peekdrag); toast once per session: 1st drag below min → "Sidebar hidden!", 2nd → compact with no toast (s34-t); tab / sidebar button / Space header / empty sidebar menus only Koan's (s34-menus). No DCHECK/dump.
Report (as the test list asks): Ctrl+Alt+B layout, app menu > More tools still lists "Show tabs horizontally" greyed out (s34-mt); the app menu also has Chromium's "Tab groups" and "Customise Chrome".
Not done: the 10-minute hint (would hold the loop 11 min).

## Session 35 (15:14–15:17 IST): KoanCheck-c163d (#163 ea32f963 on develop ef76c876)
My earlier "blank sidebar panel" (s32) was a mis-click: in the peeked layout the pill sits at y≈224, my click at y=119 hit empty space. Not reproduced.
### B48. Compact peek + page info: the sidebar slides away and page info hangs outside the window — major (#163 step 8)
Steps: compact on, peek the sidebar, click the pill's site-info button. Expected (#163 step 8): page info opens at the pill and the sidebar stays out while it's open; Esc: sidebar slides away.
Actual: page info opens, but the sidebar hides at once; the bubble stays where the pill was, mostly outside the window over the desktop (x 0–320, window starts at 221) (s35-pageinfo-sidebar-gone). Esc closes it, no blank panel (s35-pageinfo-esc). Same with the pointer moved onto the page.
PASS: https download finishes silently, no panel opens by itself (s35-dl-fs left); notification prompt in native fullscreen at the pill (s35-dl-fs right). No DCHECK/dump.
Minor: a plain-http download is blocked ("This file can't be downloaded securely") with no visible notice anywhere except the Library (s35-lib-blocked) — no panel opens, so the user doesn't learn it failed.
Not done: camera/microphone (I won't turn on the Mac's camera or mic), CT/CB layouts.

## Session 36 (15:47–15:51 IST): KoanCheck-c90i (#90 fbeed2fd on develop ef76c876)
PASS: menu bar is exactly Koan File Edit View Spaces History Bookmarks Tools Window Help, no Profiles, no Tab (s36-bar); every menu opens, no crash (s36-m1, m2); 6× ⌘` between two windows: no crash; ⋮ > More tools (Ctrl+Alt+B layout) has no "Show tabs horizontally/vertically" (s36-mt; the single-toolbar layout has no ⋮ button); ⌘J opens the Library's Downloads; ⇧⌘L opens the inspector; ⌘S toggles compact (s36-k). View has "Toggle Sidebar's Width" and still "✓ Show tabs vertically" (expected on #90 alone). No DCHECK/dump.
Not done: the Dock menu (my rules: clicks only in my own windows), the rebind (passed on c90g).

## Session 37 (16:12–16:16 IST): KoanCheck-c163e (#163 4813ee5e)
PASS: B48 fixed — compact + peek + the pill's site-info button: page info under the pill, the sidebar stays out (also with the pointer on the page), Esc closes both (s37-peek-pageinfo); Ctrl+Alt+B layout: page info at the omnibox's site icon (s37-cab-pageinfo). No DCHECK/dump.
### B49. After page info in the Ctrl+Alt+B layout, fullscreen + compact never reveals the sidebar at the left edge — major (develop ef76c876, not #163)
Steps: ⌃⌘F, Ctrl+S (compact on), move the pointer to x=0..3 and rest 1.5–2 s. Expected (Zen, and c128b): the sidebar slides in. Actual: nothing (s37-fs-no-peek: left = compact off shows the sidebar, right = compact on, pointer at the left edge, no sidebar). Unknown whether #163 or develop ef76c876.
B49 narrowed (session 38, 16:30–16:36): both orders (fullscreen→compact, compact→fullscreen) reveal fine on a fresh profile (s38-orders-ok); Ctrl+Alt+B on/off alone: fine (s38-D); peek + pill page info alone: fine. **Trigger:** Ctrl+Alt+B layout → click the omnibox's site icon (page info) → Esc → Ctrl+Alt+B back → ⌃⌘F → Ctrl+S → pointer to the left edge: nothing (s38-F). Same on develop ef76c876 alone (KoanCheck-def7) (s38-G). Likely a stale hover/reveal lock left by the page-info bubble anchored in the multi-toolbar layout.

## Session 39 (21:00–21:03 IST): KoanCheck-c159k (#159 16dfc726 on develop 47932183)
All three earlier failures now PASS:
- 1 (new criterion: accents only): default Space WindowTint=0; Red → WindowTint=-2543579 (0xFFD92025); selected row and pill warm-shifted (pill 48,48,49 → 53,49,48; header text 78,79,82 → 121,109,108) (s39-default-vs-red); window B on the default Space: WindowTint=0, neutral accents, window A stays red (s39-two-windows).
- 8 (--disable-features=UseActionsForBrowserCommands): ⌘⌃W asks "Delete Space?" (s39-delete-space); Cancel keeps all 4 tabs (s39-cancel-kept).
- 10: no Tab menu in the bar (#90); tab context menu with ⌥ held has no group items (s39-ctx-opt).
No Spaces: DCHECK, FATAL or dump.

## Session 40 (21:37–21:40 IST): KoanCheck-c165i (#165 7410d102 on develop 47932183 + #159), Zen side by side
FAIL (confirms the loop's glance.mjs): **double ⌘W (60 ms apart) closes the Glance AND the parent tab** (s40-a, frame 3: only a new tab left); Zen closes only the Glance, the parent stays (s40-z).
FAIL: ⌘L in a Glance, Esc, then Esc again: the card stays — and stays stuck after more Esc and a click (s40-kr); Zen: 1st Esc closes the bar, 2nd closes the card (s40-zr). ⌘L edits in the pill (Zen floats; #165 says by user decision).
FAIL: Glance page context menu > "Open Link in New Window": no window opens (window list unchanged).
PASS: ⌘R reloads only the Glance (child loads 2, as Zen); Open Link in New Tab opens it next to the parent; Copy Link Address copies; Inspect opens DevTools for the Glance page; ⌘Q with a Glance open: no crash, nothing from the Glance restored (step 13). No DCHECK/dump.
Still: Glance side buttons are blank white discs (#138), Zen has ×/expand/split icons (s40-z).
Not done: steps 11/12 (chrome://discards), 5 (⌘[ history), 9 (⌘⇧C), Translate/Lens items.
