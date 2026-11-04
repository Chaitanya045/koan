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
