# Koan vs Zen

How Koan compares with Zen, feature by feature. "Same" means it behaves the
way Zen does; "Different" says how and why; "Missing" means Koan doesn't
have it yet.

Koan's engine is Chromium, so everything about rendering, web platform
features, DevTools and extensions is Chrome's, not Firefox's. That is the
point of Koan.

## Layout and look

| Zen | Koan | Notes |
|---|---|---|
| Vertical tabs in a sidebar | Same | Upstream Chromium vertical tabs, on by default. |
| Page as a floating rounded card | Same | |
| Sidebar and toolbar on one surface | Same | |
| Single-toolbar layout (address in the sidebar), the default | Same | Sidebar top row: toggle, back / forward / reload (plus Chromium's tab groups / tab search); URL pill below: the site's name, the copy-URL button on hover, the site panel button. |
| Window buttons hidden until the pointer reaches the top edge (single toolbar), Linux and Windows | Same | (Not on macOS, see below.) The page starts 8px below the window top; the strip with the frame's buttons opens on hover and pushes the page down, like Zen's. Buttons on the sidebar's side stay in its top row, as Zen places them (`ZenVerticalTabsManager._updateEvent`). Where compact mode hides the toolbar, the buttons come with the revealed toolbar. |
| Window buttons (traffic lights) on macOS | Partial | AppKit's buttons, **always visible** in the sidebar's top row (Zen on macOS doesn't hide them: `zen.view.hide-window-controls` is for other platforms), centred 22pt from the top in a 44pt row (Zen's 38pt row with 3pt margins), followed by the sidebar toggle and the navigation buttons. They go with the sidebar only where it is hidden (compact mode): Zen's compact mode shows nothing in the top row, Koan keeps its header there (single toolbar) and shows the lights with it. With tabs on the right they stay at the window's left, over the strip above the page that opens at the top edge (Koan's, as on other platforms). The pointer on them keeps the revealed toolbar open (Zen's `onLeave`); for the peeked sidebar that isn't there yet: #98's keep-hover has no hook for it, so moving the pointer onto the lights lets the sidebar slide away after 150ms. Measured on a Zen 1.24b Mac: 14pt circles, centres x=19/42/65. Not tested on a Mac yet. The lights' own positions are AppKit's: on the Mac (macOS 27) they measured x=19.8/42.8, y=18.8 before the 44pt row (Zen: 19/42, y 22); Koan doesn't move them (Chromium's figures, 12,10 62x18, are for macOS 26: pitch 20 there). The sidebar toggle's x (Chromium's caption width, about 76) and the navigation buttons (Zen: 31x29, right-aligned at sidebar width - 99/-68/-37; Koan: 28x28) are not matched or checked on screen yet. |
| Native fullscreen on macOS (top-edge reveal, no traffic light gap) | Partial | The content card stays in fullscreen (the usual gutter and radius, the sidebar beside it; with compact mode on and no split view, Zen's `zen.view.borderless-fullscreen`: no gap and no radius, the page fills the screen) and the toolbar is hidden until the top-edge reveal ("Always show toolbar in full screen" is off by default, Chromium: on; on a screen with a camera notch the reveal sits below the notch, where AppKit puts it). The traffic lights slide in with the menu bar and the sidebar's top row keeps no room for them. Missing: Zen's row of back / forward / reload in the sidebar (Koan's single toolbar is off in fullscreen, where Chromium hosts the top container); the row has Chromium's toggle and buttons. Not tested on a Mac since. |
| Multiple-toolbars layout | Same | Ctrl+Alt+B (Zen sets this in Settings). |
| Collapsed-sidebar layout (icons only, expands on hover) | Same | The sidebar toggle collapses it (Chromium's collapsed vertical tabs); the toolbar is then above the page. Like Zen, single toolbar needs the full sidebar: collapsing leaves it, expanding comes back to it. |
| Tabs on the right (`zen.tabs.vertical.right-side`) | Same | Sidebar context menu, and Settings > Look and Feel > Vertical Tabs. The whole window is mirrored, including the sidebar's top row, its resize edge and the compact-mode reveal edge. Tabs themselves keep their left-to-right layout, as in Zen. Windows without Koan's sidebar (horizontal tabs, popups, apps) aren't mirrored. The setting is logical, like the rest of the UI: in a right-to-left UI it puts the sidebar on the left. |
| Dragging the sidebar's edge closed hides it | Same | Turns compact mode on with the sidebar hidden, and shows Zen's "Sidebar hidden! Use Ctrl+S to bring the sidebar back" toast once (with the compact mode shortcut as set; ⌘S on macOS). Leaving compact mode brings the sidebar back at its width. With "Hide toolbar" chosen, the drag switches compact mode to hiding both, so that the sidebar does hide (Zen just turns compact mode on, which there hides only the toolbar). |
| Toolbar context menu on the sidebar | Same | Compact Mode submenu (just the toggle with single toolbar, as in Zen) and Tabs on the right. Zen's other entries (customize toolbar, ...) don't apply. |
| Sidebar tab list: "+ New Tab" row above the tabs, tab rows, close button on hover | Same | The row opens the tab at the top of the Space, as Zen does with its New Tab button on top; Chromium's "+" below the tabs is hidden. Rows are 36px with a 14px radius, Zen's selected (white at 80%, 18% in dark mode) and hover colours, a 16px favicon 10px before the title. Zen's faint shadow under the selected tab isn't drawn. Ctrl+T opens the URL bar instead (see URL bar); with replace-newtab off, it opens the tab at the end. |
| Separator above the tabs with **Clear** on sidebar hover | Same | Shown while the Space has unpinned tabs. Clear keeps the selected and audible tabs like Zen's, keeps the Space (an emptied one gets a fresh tab) and Ctrl+Shift+T reopens what it closed, the last closed tab per press. Zen also keeps tabs in Picture-in-Picture or sharing camera/mic; Koan doesn't check those yet. |
| "Tabs Closed!" toast | Same | Top right (top left with tabs on the right, as in Zen), Zen's look; spring animation approximated with an ease-out scale. |
| Sidebar width 163–500 px, drag to resize | Same | Upstream resize handle with Zen's limits (Chromium's 126–400 px with the Koan sidebar off; a saved width is clamped to whichever applies). |
| Default look | Same | Zen's colours (Zen 1.24b) as Koan's token set, so Chromium's own surfaces match too. shadcn/ui's design system (dark, near-black, Geist) is a second look: Settings > Look and Feel > Look (#48). |
| Theme follows the system light/dark mode | Same | In the Zen look. The shadcn look is dark by default; its Mode can be Light or Automatic. |
| Thin overlay scrollbars, shown while scrolling | Same | Chromium's overlay scrollbars, on by default on Linux in Koan. Zen follows GTK's overlay-scrolling setting ("Always Show Scrollbars" in GNOME's accessibility settings); Koan always overlays for now (#30). For classic scrollbars, start Koan with `--disable-features=OverlayScrollbar`. Comparison: `screenshots/scrollbars-zen-vs-koan.png` (Zen left, Koan right, while scrolling). |
| Gradient theme picker | Different | Workspaces tint the browser with one colour (Chromium's Material colour system), no gradient editor (#41, PR #62). The shadcn style is flat: gradients belong to the Zen style (the seam is `koan::look::CurrentLook().style`). |
| Translucent window on macOS (the theme's opacity shows the desktop through the sidebar; `zen.theme.acrylic-sidebar` / `acrylic-urlbar`) | Missing | Koan's frame is opaque (#79). |
| Haptic feedback on macOS, Zen's Mac metrics (42px toolbar, 32px Space buttons, window-radius corners), greyed-out inactive windows | Missing | #80. |
| Zen Mods (community CSS themes) | Missing | Impossible: Koan's UI is native Views, not CSS, so mods can't apply. Chrome themes from the Chrome Web Store work. |
| Boosts (per-site colour, text, zap, custom CSS) | Missing | Possible on Chromium, since they restyle web pages (#83). |

## URL bar

| Zen | Koan | Notes |
|---|---|---|
| URL bar behaviour: normal / floating only when typing (default) / always floating (`zen.urlbar.behavior`) | Same | `koan.urlbar.behavior`, same values and labels in Koan's settings (Look and Feel > URL Bar). "Floating on type": Ctrl+L and a click on the pill edit in the pill, the first keystroke floats the bar. Zen floats a *mouse*-opened bar never (it stays in place, widened); Koan floats it on typing too, as #36 asks. |
| Floating URL bar look | Same (Zen style); shadcn style differs | In the shadcn style it is a command-dialog card: 55% of the window, a little above the middle, `popover` with a 1px `border`, shadcn's shadow, no click-outside shadow of Zen's size. Zen style:  Centred card, `min(2/3 window width, 750px)`, centred vertically as if 333px tall, radius 15, a 62px input row with 17px medium text, 49px suggestion rows (radius 8) only in this card, Zen's long shadow (`0 30px 140px -15px`, 80% / 60% black), fades in. Zen doesn't dim the page beyond that shadow. What the suggestions say and their order are the omnibox providers'. |
| Click outside closes the floating bar | Different | The click only closes the bar; in Zen it also reaches what you clicked. |
| Switching to another window keeps the bar | Same | What was typed, and a new-tab session, survive Alt-Tab / ⌘-Tab. |
| Compact mode: the sidebar stays out while its URL bar is edited | Different | Koan floats the bar right away when it's opened from the peeked sidebar's pill: Chromium collapses an expanded-on-hover sidebar while the omnibox's suggestions are open. |
| Pill and card details | Different | The site panel button is always shown (Zen shows it on hover) and the pill has no hover background. |
| Ctrl+T / New Tab opens the URL bar instead of a tab (`zen.urlbar.replace-newtab`) | Same | `koan.urlbar.replace_newtab`. Esc, a click outside or Ctrl+T again create nothing; an unfinished search comes back within 45 s (`zen.urlbar.wait-to-clear`; Zen also drops it on a tab switch, Koan doesn't). The sidebar's "+ New Tab" row, the tab strip's "+", the menus' New Tab and ⌘T on macOS (Chromium's New Tab command) do the same. With horizontal tabs (Chromium's own setting; Zen has none), the tab strip's "+" still opens a new-tab page. What you open goes next to the current tab (Chrome's placement for a new foreground tab); with replace-newtab off, the New Tab row opens its tab at the top of the space, like Zen. Zen also unhighlights the current tab meanwhile; Koan doesn't. |
| Copy-URL button in the pill on hover (`zen.urlbar.single-toolbar-show-copy-url`) | Same | `koan.urlbar.show_copy_url`. |
| "Copied current URL!" toast | Partial | The same toast as "Tabs Closed!" (one `KoanToast` per window); the copy button, Ctrl+Shift+C and Ctrl+Shift+Alt+C (⌘⇧C, ⌘⇧⌥C on macOS; "Copied current URL as Markdown!") show it. Zen's toast on macOS also has a Share button (the system share sheet); Koan's doesn't. Zen copies the URL without tracking parameters (its stripped URI); Koan copies it as is (#78). |
| Site panel (security, permissions, extensions; share, reader mode, screenshot, bookmark) | Different | The pill's site button opens Chromium's page info bubble (security, permissions, cookies, site settings). The action row (#78) and the Extensions section (#73) aren't in it yet. |
| Extension buttons in the sidebar's top row (`zen.view.overflow-webext-toolbar`) | Missing | The single-toolbar layout hides the toolbar that holds them, so extension popups can't be reached with the mouse (#73). |
| URL bar commands (command palette) | Partial | See Command palette below (#40). |

## Compact mode

| Zen | Koan | Notes |
|---|---|---|
| Toggle with Ctrl+S | Same | |
| Hide the sidebar, reveal at the left edge | Same | With single toolbar (the default) that's all, like Zen. |
| Hide the toolbar too (multiple toolbars), reveal at the top edge | Same | |
| Show the sidebar (Cmd+Opt+S, `zen-user-show`) | Same | A toggle: the sidebar stays out, wherever the pointer is, until the shortcut is used again (it used to hide itself after a few seconds). |
| A popup opened from the sidebar keeps it open (`has-popup-menu`) | Same | Upstream's menus on the sidebar hold it; Koan's Space and + menus now do too. Zen also keeps it until the next mouse move after the popup closes; Koan lets go when it closes. |
| Toggling compact mode animates the sidebar's margin for 0.1 s (`animate-sidebar`) | Missing | The sidebar goes at once and the page changes size once. |
| Leaving the window through the edge keeps the sidebar out within 200 pt / 100 pt (`ZenMouseTrackerCocoa.mm`) | Missing | See the row above on pointer events outside the window; needs a native tracker. |
| The empty tab keeps the sidebar out (`zen-has-empty-tab`); a drag of a tab keeps it out (`movingtab`) | Missing | |
| Native fullscreen with compact mode: the page runs edge to edge, no margins or radius (`zen.view.borderless-fullscreen`); flash after leaving DOM fullscreen | Missing | Compact mode doesn't apply in fullscreen yet. |
| The hidden sidebar slides in over the page, without a relayout per frame | Same | The strip is laid out once at its width and its layer slides (a transform, set on each tick of a main-thread animation, not run on the compositor); the strip's children aren't laid out again. Upstream resizes it on every frame. Moving the slide to the compositor (`LayerAnimator`) is a follow-up. |
| Hiding the sidebar doesn't jump the page | Same | Compact mode gives the sidebar no width from the moment it starts collapsing, so the page changes size once, not at the animation's end. |
| The sidebar stays out 150 ms after the pointer leaves its edge (`sidebar-keep-hover.duration`) | Same | From the edge hot zone; once the pointer is on the sidebar, Chromium's own hover tracking closes it as soon as the pointer leaves (no 150 ms). |
| A revealed toolbar hides 1000 ms after the pointer leaves through the window's edge (`toolbar-hide-after-hover.duration`, the fallback without the native tracker); inside the window the toolbar element collapses on the next frame | Different | Koan uses 1000 ms for every leave of a revealed toolbar and of the window buttons' strip (it was 400 ms), a deliberate approximation until the two cases are split. A peeked sidebar no longer hides by itself. |
| Reveal: `translate 0.25s` with `--zen-compact-mode-func` (a `linear()` spring table), hide `translate 0.15s ease` (src/zen/compact-mode/sidebar.inc.css) | Same | The strip is laid out once and its layer slides with Zen's 101-point table (including the 1.1% overshoot), 250 ms in and CSS `ease` 150 ms out, driven by a Koan animation on the main thread from upstream's hover state. With reduced motion it jumps. |
| The revealed panel floats: `--zen-compact-float` 10 px inset on top/bottom/left, rounded, shadowed, darker | Different | Chromium's fly-out draws edge to edge in the docked fill. Not changed yet. |
| After the pointer leaves the window, a revealed sidebar/toolbar stays until it is 200 pt (sides) / 100 pt (top, bottom) beyond the window edge (`outside-window-edge-offset.*`, prefs/zen/compact-mode.yaml) | Missing | Views gets no pointer events outside the window, so Koan hides a revealed sidebar or toolbar once the pointer leaves the window (after the keep-hover time). This is a tolerance on hiding, not a bigger hot zone: the edge zone itself is 8 px inside the window. |
| Compact mode and single toolbar in native fullscreen (toolbars hidden, shown at the top edge) | Missing | Koan's layouts need a non-fullscreen window; in macOS fullscreen Chromium's toolbar shows above the page. Staged work (see #94). |
| Choose to hide only the sidebar / only the toolbar / both | Same | Zen's "Compact Mode" submenu in the sidebar's context menu; and in Settings > Look and Feel > Show in compact view (pref `koan.compact_mode.hide`). With single toolbar compact mode hides the sidebar only (Zen offers just the toggle there). A revealed toolbar is above the page only, beside a visible sidebar (Zen's navbar). Hiding just the toolbar is disabled with a collapsed sidebar and the window buttons on its side, as in Zen; a window in that state hides the sidebar instead. Zen also rewrites the setting to "Hide sidebar" then; Koan keeps it (one setting serves every window). |
| Briefly pop the sidebar up when switching tabs (`toolbar-flash-popup`) | Same | Off by default, 800 ms, not in split view, as in Zen. In Settings > Look and Feel, with Zen's label. |
| Themed background for the compact sidebar/toolbar | Same | Koan's compact sidebar and toolbar always use the window's (Space-tinted) colours. Zen 1.23 no longer has the `zen.view.compact.color-toolbar` / `color-sidebar` prefs (the setting's string is left over, unused), so there is nothing to switch off. |

## Spaces (workspaces) and Essentials

| Zen | Koan | Notes |
|---|---|---|
| Named Spaces, one shown at a time | Same | Built on Chromium tab groups shown one at a time (upstream group focus mode). Any tab group in a normal window is a Space, also one an extension makes (`chrome.tabs.group`). |
| The window always shows one Space | Same | Every unpinned tab is in a Space; a new window starts in "Space". |
| An empty Space (its last tab closed) | Different | A Space is a tab group, which can't be empty, so it gets a new tab instead. |
| Space header (icon and name) above the Space's tabs | Same | Below the Essentials, 44px, hover background and a "..." menu button like Zen's. Hidden while the sidebar is collapsed to icons (Zen shows just the icon). Koan's header also takes keyboard focus: Enter (or the menu key) opens the Space menu, F2 renames; screen readers hear the Space's name when switching. |
| Rename in place (double-click the name, or Change Name...) | Same | Enter keeps, Escape cancels, leaving the field (or switching Space) keeps, with Zen's "Workspace has been successfully renamed!" toast (not when the name didn't change). |
| Change Icon... / double-click the icon | Same | |
| Space menu: Change Name..., Change Icon..., Edit Theme..., the Spaces, Unload Space, Unload All Other Spaces, Create Space, Delete Space | Same | Delete asks first and is off for the last Space, like Zen. Zen's Set Profile (containers), Manage Spaces (Library), Share Space and Space routing aren't there. |
| Bottom bar: one button per Space, "+" with the Create menu | Same | 30px buttons, inactive ones grey and faded, shrinking to dots when they don't fit; hidden while there is one Space; stacked above "+" in the icons-only sidebar. Screen readers get them as a group of choices with the active one checked. No drag to reorder yet (#76). |
| Dragging a tab over a Space button, or holding it at the sidebar's left / right edge, switches Space so the tab can be dropped there | Missing | Zen's `handle_spaceIconDragOver()` (at once) and `zen.tabs.dnd-switch-space-delay` / `zen.workspaces.dnd-switch-padding` (edge, 500 ms); use Move To (#76). |
| Reorder Spaces by dragging their bottom-bar buttons | Missing | #76; the Library's Spaces list (#43) also reorders. |
| Windows share Essentials, pinned tabs and Spaces (`zen.window-sync.*`), New Blank Window | Missing | Each Koan window has its own Essentials and Spaces (#75). |
| Space routing: sites and external links open in a chosen Space | Missing | Possible; Zen's container option isn't (#81). |
| Create menu: Create Space, Create Folder, New Split, New Tab | Same | Features add their entries (`KoanWorkspaceController::AddCreateMenuEntry()`); Create Folder comes with folders (#60). |
| Create-a-Space form in the sidebar | Same | Title, icon and name, Edit Theme..., Create Space (enabled once named) and Cancel. Zen's Profile row (containers) isn't there. Zen creates the Space before the form; Koan on Create. From a collapsed sidebar, the sidebar expands over the page while the form is open. |
| Emoji picker with search | Same | Koan's own set of emoji searched by keyword (Zen's list is larger); no page of SVG icons. |
| Edit Theme... (gradient theme picker) | Different | Koan picks one colour per Space (or Default); the browser takes it while the Space is active. |
| Switch with Ctrl+Alt+Left/Right (Cmd+Option on macOS), wrapping around | Same | On macOS Koan's keys come before Chrome's Cmd+Option+arrow tab switching. |
| Ctrl+1...0 switch to Space 1 to 10 (macOS) | Same | Zen 1.24b binds Alt+1…Alt+0 on the other platforms (`zen-keysets.inc.xhtml`, `ZEN_WORKSPACE_SWITCH_MODIFIER`); Koan, a macOS browser, leaves them unbound there (they are Chromium's select-tab keys); any key can be set in Settings > Keyboard Shortcuts. Zen 1.24b also leaves Firefox's Open File (Cmd+O) and Bookmark All Tabs (Cmd+Shift+D) unbound (`browser-sets-inc-xhtml.patch`); so does Koan's menu bar on macOS, where Cmd+O is Expand Glance's and does nothing without a Glance. Not done: Zen's Cmd+Shift+O (bookmarks library), Cmd+K / Cmd+Opt+F (search), Cmd+I (page info), which Chrome has under other keys or not at all. |
| Switch with a horizontal two-finger swipe over the sidebar | Same | On macOS (`KoanSpaceSwipe`), with Zen 1.24b's numbers (`ZenSpacesSwipe.mjs`, Gecko's `SwipeTracker.cpp` as Zen patches it): the gesture amount is the fingers' travel over 550pt (`widget.swipe.pixel-size`); the Space follows by Zen's `translateX` recurrence (amount x 100, force multiplier 1 - x / (4.5 x width) held below 0.5) as `translateX / 2` percent of the sidebar's width, moving **against** the fingers; lifting them switches Space if the amount plus the last movement's velocity (amounts per second) times 0.5 reaches 0.25 (137.5pt of travel at rest), else it springs back (250ms, `switch-animation-duration`). **Fingers moving right go to the next Space** (`zen.workspaces.natural-scroll` off, not exposed). Wraps around, lands on the Space's last tab. Over the page, Chromium's swipe back and forward is untouched. Elsewhere upstream's swipe controller: one switch per swipe, the strip doesn't follow. Not tested on a Mac yet. |
| Switching with the mouse wheel over the sidebar: horizontal, or vertical with Ctrl | Same | One Space per notch, at most every 200 ms, anywhere over the sidebar. On Linux and Windows a horizontal wheel goes through upstream's swipe controller (one switch per burst). Zen's prefs for the modifier key and natural scrolling aren't exposed (Ctrl, not natural). |
| Swiping past the last Space | Different | Koan wraps around (Zen's `zen.workspaces.wrap-around-navigation`, on by default); Zen's swipe can also end in its "create a Space" or Library edge actions, which Koan doesn't have. |
| Slide animation between Spaces (250 ms) | Same | The old Space slides out and the new one in, the shorter way round. Zen also cross-fades the themes. |
| Each Space remembers its last active tab | Same | Also after a restart (the Space's most recently shown tab). Showing another Space's tab directly (the address bar's "Switch to this tab", tab search, an extension) shows that tab. |
| "Move To" a Space from the tab context menu | Same | Lists the other Spaces; moves the selected tabs and shows the Space. Replaces Chromium's tab group items. |
| Unload Space / Unload All Other Spaces | Different | Chromium's tab discarding. Tabs that aren't loaded are skipped, like Zen's. Tabs on screen (also a split view's other pane) and tabs in use as Memory Saver sees it (playing sound, picture-in-picture, capturing, connected to a device, open in DevTools, with form input, kept by an extension or the "always keep these sites active" list) stay loaded, and a toast says how many. |
| New tabs open in the active Space | Same | |
| Spaces are not Chromium's saved tab groups | Same | Zen has no list of saved groups; Koan keeps Spaces out of Chromium's (so a reopened window's Spaces also load on demand). |
| Restored after restart | Same | Koan restores the previous session by default, like Zen. |
| Containers per Space | Missing | Chromium has profiles, not containers. |
| Essentials shared by all Spaces | Same | Pinned tabs, shown as Zen's tiles (46px tall, up to four across) at the top of the sidebar. |
| Essentials tile glows with the site's icon colour when selected | Different | The selected tile is plain white at 85% (20% in dark mode), Zen's look with `zen.theme.essentials-favicon-bg` off. |
| Pinned URL, "Back to pinned url" reset button | Same | Hover the slash after the icon; Ctrl+click separates the page into a new tab. Essentials show no label, as in Zen. |
| Reset Pinned Tab, Replace with Current URL, Edit Pinned URL… | Same | Always offered for a pinned tab, as in Zen; a reset ignores no part of the URL (a hash-routed app's `#inbox` is reset too), while the reset button only shows once the page itself changed. Replace and Edit… are in an "Edit Pinned URL" ("Edit Essential URL") submenu, as in Zen. Zen's "Reset Pinned Tab to Pinned URL" shortcut is in the shortcut editor, without a key, as in Zen. |
| Closing a pinned tab: reset, unload and switch | Same | Ctrl+W, Ctrl+F4 (Cmd+W and the menu bar on macOS), the close button and middle click; Zen's six behaviours in Settings > Tab Management > Pinned Tabs. |
| Restore pinned tabs to their pinned URL on startup | Same | Settings > Tab Management > Pinned Tabs, off by default like Zen.  Pinned URLs, labels and icons come back with the session (Koan continues where you left off by default); with Settings > On startup set to open a new tab page, Chromium reopens Essentials from its own list and their current URL becomes the pinned URL. |
| Change Label… / Change Icon… | Same | Inline rename; the icon picker is a grid of emoji plus a field for any other, smaller than Zen's searchable picker. |
| Unload a tab | Same | Chromium's tab discarding. Resetting a tab that isn't loaded keeps it unloaded until it is shown, like Zen. Reset, unload and switch on a loaded tab away from its pinned URL loads the pinned page once before unloading it (Zen rewrites the unloaded tab's state instead; Chromium can't change an unloaded tab's page without loading it). |
| Toasts: pinned URL replaced / updated / invalid, tab renamed | Same | Zen's texts, in Koan's toast. |
| Unload all tabs of a folder | Missing | Koan has no folders yet (#42, PR #60); folders can use the same unload. |
| Live Folders (RSS feeds, GitHub pull requests as a self-refreshing folder) | Missing | Needs folders first (#42); noted there as a follow-up. |
| A pinned tab stays on its site: a URL from the URL bar on another domain opens a new tab (`zen.tabs.open-pinned-in-new-tab`); a new tab it opens on another domain opens in Glance (`zen.glance.open-essential-external-links`) | Missing | Koan navigates the pinned tab away (#74). |
| At most 12 Essentials | Different | "Add to Essentials" shows how many are used ("3 / 12") and is off when the tab (a split view counts two) doesn't fit. Zen refuses a 13th dropped on the tiles; Koan accepts the drop, then moves the tab back to the Space as a normal tab once the drag is over. The same for one duplicated from an Essential. |
| Dragging pinned tabs | Same | Dragged out of the Space's pinned tabs, a pinned tab is unpinned; a tab dropped among them is pinned; an Essential dropped among them keeps its pinned URL as a pinned tab. Applied once the tabs stop moving, since Chromium moves them along the drag. Moved to another Space, a pinned tab stays pinned, at the top. |
| Pinned tabs in a split view | Same | A split view is one unit, as in Zen: pinning, unpinning, Add to / Remove from Essentials and dragging take both tabs along and keep them split. Splitting a pinned tab with a normal one pins both. |
| Close Other Tabs, Close Tabs Below, Back with no history | Same | Leave pinned tabs and Essentials alone. |
| Pinned tabs that belong to their Space | Different | Ctrl+Shift+D or "Pin": the tab stays at the top of its Space, survives Clear and has a pinned URL. Zen lists them above the Space's separator; in Koan they are the first tabs below it, because Chromium's tab strip puts its separator between its pinned tiles (the Essentials) and the Space's tab group, which holds both kinds. |
| Close all unpinned tabs (Ctrl+Shift+K, or Clear in the sidebar) | Same | Keeps the selected tab and the Space, like Zen; Ctrl+Shift+T reopens the last closed tab, one per press. Pinned tabs and Essentials stay. |

## Glance and split view

| Zen | Koan | Notes |
|---|---|---|
| Alt+click a link opens Glance | Same | |
| Glance on/off | Same | "Enable Glance" in Koan's settings (Glance group); the "Koan Glance" flag removes it altogether. |
| Trigger method Alt / Ctrl / Shift / Meta + click | Different | Same choices (no Ctrl on macOS, where Ctrl+click is a right-click; Alt = Option, Zen's default everywhere), but a change only reaches pages in renderer processes started afterwards; Zen applies it at once. |
| "Open Link in Glance" in the link context menu | Same | |
| Card grows from the clicked link, shrinks back on close | Similar | Zen's 350 ms, similar easing (no overshoot), card size (80% × full height) and fade timings. Zen grows a snapshot of the link's own rect along a small arc; Koan grows a small card straight out of the click (the browser doesn't know the link's rect). |
| Page behind fades to 30% and shrinks to 97% | Similar | Same fade (the window background laid over the page at 70%); no shrink, which would show the page's square container behind its rounded corners. |
| "Search for ..." on selected text opens in Glance (`zen.glance.enable-contextmenu-search`) | Missing | #74. |
| Glance on a link in the Glance page | Different | Zen 1.23 ignores it; Koan shows the link in the card (as #37 asks). |
| Close with Esc or a click outside | Same | beforeunload can keep the page; while typing, the first Esc turns the close button red and a second closes (Zen asks whenever anything in the page has focus, and labels the button "Confirm"). macOS: no grow/shrink (the page is an NSView); not verified on a Mac. |
| Expand to a tab (Ctrl+O), split Glance | Same | Zen's button column: 34 px round buttons, 12 px apart, beside the card. No expand animation. |
| Split view side by side / stacked | Same | Ctrl+Alt+V / Ctrl+Alt+H (⌘⌥ on macOS, Zen's accel). |
| Split grid of 3–4 tabs (Ctrl+Alt+G) | Missing | Chromium's split view has exactly two panes: `TabStripModel::AddToNewSplit` CHECKs one or two tabs, `SplitTabLayout` is side by side or stacked, `MultiContentsView` holds two contents. Ctrl+Alt+G splits side by side. |
| Tab context menu "Add Split View… / Join N Tabs / Split Out Tab" | Same | Zen's labels on upstream's split item; "Join N Tabs" is disabled past two tabs. Upstream's "Arrange split view" and "Move tab into split view" submenus stay. |
| "Split Link to New Tab" in the link context menu | Same | Upstream's item, so it sits before "Open link in incognito window" (Zen: after the private window item, next to "Open Link in Glance"). In a split, upstream's "Open link in left/right view" instead (Zen would add a third panel). |
| Drag a tab onto the page edge to split | Same | Upstream's split view drop targets (on by default). |
| Split resize handles | Similar | Both: a gap between the panes with a resize cursor; upstream shows a small handle on hover. No Zen hover header (rearrange / unsplit buttons). |
| Toast when the split limit is reached | Same | Zen's "Can't add more panels to the split view!" when a third panel would join (Koan's limit is two). |
| Unsplit (Ctrl+Alt+U) | Same | |

## Command palette

| Zen | Koan | Notes |
|---|---|---|
| Actions suggested while typing in the URL bar | Different | Zen's labels and fuzzy matching from three characters, right below the first suggestion, but at most three (Zen: five), so they don't crowd history and search suggestions out of Chromium's eight rows. A Chromium omnibox provider, so it works with any URL bar look. |
| Shortcut shown with each action | Same | In Zen's notation, as in the settings page ("Ctrl+Alt+⇧+C", "Ctrl+,"; "⌘ ⇧ C" on macOS), as the row's dimmed second part rather than a tag. It is the key in effect, so a shortcut changed in chrome://koan-settings shows. |
| Learns the actions you use | Different | Used actions come first, and passed-over ones drop below the search suggestions (Zen's results learner). Zen also runs a used action on Enter for any text matching it; Koan only when you type its whole label, so typing the start of a site's address (`pri` for a site, after using Print) still opens the site. |
| Actions mode: Tab in an empty URL bar lists every action | Different | Koan types `>` (you can type it too; a second Tab leaves it) instead of showing an "Actions" chip, and lists as many actions as Chromium's popup has rows (about eight) above a "> – search" row; type to filter. As in Zen, Enter runs the first action once two characters are typed. |
| "Focus on" a Space | Same | Matched by the Space's name, which is shown beside "Focus on". |
| Add to / Remove from Essentials | Same | As in the tab's context menu; Pin / Unpin Tab pin the tab to its Space. |
| Open Theme Picker, New Folder, Collapse / Expand All Folders | Missing | Not in Koan yet (#41, #42); each feature adds its actions to the palette's registry. |
| Open Library | Same | Zen's second action; toggles the Library. |
| Reset Pinned Tab | Same | Offered while the tab is away from its pinned URL, as in Zen. |
| Toggle Tabs on right | Missing | The setting exists (Look and Feel, sidebar context menu); the palette action isn't there yet (#77). |
| New Blank Window | Missing | Koan doesn't sync windows, so it would be the same as Open New Window; it comes with window sync (#75). |
| Open Space Routing, New Boost | Missing | Koan has no Space routing (#81) or Boosts (#83). |
| Extension actions ("Extension" + name) | Missing | An extension's popup opens from its toolbar button, which the single-toolbar layout hides (#73). |

Comparison (Zen left, Koan right): `screenshots/palette-zen-vs-koan-typed.png` (typing) and `screenshots/palette-zen-vs-koan-actions.png` (Tab in an empty URL bar).

## Library

| Zen | Koan | Notes |
|---|---|---|
| Library panel sliding in over the sidebar, sections down its side | Same | Downloads, Spaces, History, in Zen's order, with Zen's sizes (418px, 110px side column) and opening spring. With tabs on the right it slides in from the right. Zen also slides the page aside; Koan's panel slides over it (Chromium draws the page in its own native window, which view transforms don't move). |
| Alt+Shift+L, and History / Downloads keys open their section | Same | Zen's Firefox keys: Ctrl+H, Ctrl+Shift+H (⌘⇧H, ⌘Y on macOS) open History, Ctrl+Shift+Y (⌘J) Downloads; pressed again they close it. Chrome's app menu still opens `chrome://history` and `chrome://downloads`. |
| Library button in the sidebar's footer | Same | At the start of the Spaces' bottom bar (shown with Spaces on), in the room Zen keeps there for its sidebar button. Zen's hover stack of recent downloads on that button is missing. |
| Opens on the last section used | Same | |
| "Open Library" in the command palette | Same | |
| History: search, when / sort / source filters, grouped by day or site | Same | Default range 120 days, 100 rows at a time, like Zen. |
| History: closed tabs as a source | Same | From Chromium's recently closed tabs (all windows, tabs only). Zen renames the tab "Archive" then; Koan keeps "History". |
| History: open, open in background (Ctrl/middle click), forget, reopen | Same | Forgetting is off where policy forbids deleting history (AllowDeletingBrowserHistory), as on Chrome's History page. |
| History: Forget About This Site | Koan addition | Firefox's sidebar has it; Zen's Library menu has only Reopen Page and Forget This Page. Removes the site's history, cookies, site data and cache after asking. |
| History: clear by range from the tab's menu | Same | Last hour / 12 hours / today (since midnight, as the Today filter) / week / month / everything. The confirmation names the range ("Clear history from the last hour?"). |
| History / Downloads: drag a row out (as a link, as a file) | Missing | |
| Downloads: search, type and date filters, newest first | Same | |
| Downloads: progress ring, show in folder, retry, cancel | Same | A download held back by a warning (dangerous or insecure): clicking it opens Chrome's warning (the download bubble, or `chrome://downloads` without a toolbar), where it is kept or deleted. |
| Downloads: click a download in progress to open it when done | Different | Koan never sets a download to open when done: Safe Browsing's verdict can come after the Library has closed, and a file kept from a warning would then open by itself. A file opens only when the user opens it (⋯ > Open), as in Chrome's download bubble. |
| Downloads: menu (open, copy file, copy link, retry, remove, delete) | Same | Also Cancel, and "Review warning" on a held-back download. "Hide from Zen" is "Remove from history" (not offered where policy forbids deleting history). Delete File asks first. Zen's "Clear Download Preview" is missing. |
| Downloads: picture previews instead of file icons | Same | Decoded out of process. |
| Spaces: cards with the space's tabs, rename, icon, theme, drag to reorder, add | Same | On the Spaces' API (#57): their emoji picker, Edit Theme, Unload, Create form and Delete (which asks and keeps one Space). The theme is the Space's colour (Koan has no gradient editor yet, #41). |
| Spaces: live copy of each space's sidebar (drag tabs between cards, folders) | Different | Cards list the space's tabs; click one to go there. Moving tabs between spaces is done in the sidebar. |
| Media and Boosts sections | Missing | Zen-specific features Koan doesn't have. |
| Opening the Library with a swipe | Missing | |

## Keyboard and settings

| Zen | Koan | Notes |
|---|---|---|
| Zen's default shortcuts | Same | All of Zen's defaults (shortcuts version 21); see `USAGE.md`. |
| Zen's macOS shortcuts | Same | Zen's "accel" is ⌘ on a Mac, so Koan's keys are ⌘-based there, with Zen's Mac-only ⌃1…⌃0 (Space N), Firefox's ⌃M (mute), ⌘⌥L and ⌘⇧L (inspector) and ⌘J (downloads). Where a key is also Chrome's (⌘S, ⌘⇧D, ⌘⇧N, ⌘⌥H, ⌘⌥U, ⌘⇧C, ⌘⌥L, ⌘⌥→/←) Koan's wins, as on Linux, and Chrome's menu item gives up the key or moves to the one Koan uses (Save Page ⌘⌥⇧S, New Private Window ⌘⇧P); see `USAGE.md`. Shown in Zen's notation (`⌘ ⇧ C`). Written from Zen's source and Chromium's Mac key tables; not yet tried on a Mac. |
| Esc stops loading | Same | Zen leaves Esc unbound as Stop (`key_stop`, `browser-sets-inc-xhtml.patch`); Cmd+. and the stop button still stop. Koan's hook in `BrowserCommandController` makes Esc only close the find bar, and the page gets it otherwise. |
| View Source Cmd+U, Show All Bookmarks Cmd+Shift+O | Same | Firefox's keys, which Zen keeps: Chrome's View Source (Cmd+Option+U, Koan's Unsplit) and Bookmark Manager (Cmd+Option+B) items show and use them on macOS. |
| Menu bar | Partial | Zen 1.24b's order: File Edit View Spaces History Bookmarks Tools Window Help. Chrome's Profiles and Tab menus are hidden (not removed: the app controller and the menu bridges look their items up by tag), and so are Chrome's View items that Zen hasn't (bookmark bar, full-screen toolbar, full URLs, AI Mode, Lens, vertical tabs, Cast, Developer). View starts with Appearance; Spaces has Zen's seven items; Tools has Downloads, Extensions and Browser Tools. Not done: Zen's View > Toolbars / Sidebar submenus and Collapse Pinned Tabs, Tools > Sign In and Page Info, and File items for Zen's Smart / Classic windows. "New Blank Window" is a plain new window here: Koan has no window sync. Hidden Chrome items leave their separators, so a menu can show a doubled line. |
| Dock menu | Different | Zen's is Firefox's: New Window, New Private Window. Koan's has those and lists the Spaces of the window that was active last to switch to (Zen has none). |
| Firefox's shortcuts Zen inherits | Mostly | Where Chrome already uses the key for something else (e.g. Ctrl+J downloads), Koan keeps Chrome's. |
| Shortcut editor in Settings | Partial | `chrome://koan-settings` > Keyboard Shortcuts: Zen's groups and labels, Esc saves, Backspace clears, conflicts refused, Reset to Default (confirmed; Zen also restarts). It lists Koan's 26 shortcuts (Zen's own and the Firefox ones Koan implements), not Zen's full list: Chromium's other shortcuts aren't editable, since Koan only adds keys on top of Chromium's. A key needs Ctrl, Alt or Win (⌘ on macOS), or is a function key, and on macOS isn't one macOS keeps for itself (⌘Q, ⌘Space, ⌘Tab ...); Zen accepts any key, but Koan's keys also work in the address bar, where a bare letter would break typing. |
| Zen settings pages | Partial | `chrome://koan-settings` (Ctrl+,): Look and Feel, Tab Management, Keyboard Shortcuts, with Zen's labels; Chromium's own settings are linked from its sidebar. Only the settings Koan has are shown (see below): 13 today, against Zen's few dozen. Comparison: `screenshots/settings-zen-vs-koan.png` (Zen left, Koan right). |
| Settings: Only Sidebar / Sidebar and Top Toolbar | Same | Look and Feel > Browser Layout. |
| Settings: URL bar behaviour, replace new tab, copy URL button | Same | Look and Feel > URL Bar. |
| Settings: compact mode variants and flash popup, tabs on the right | Same | Look and Feel. |
| Settings: Collapsed Sidebar layout card, dark theme styles | Missing | Added to the settings page as each feature lands (#41). Window dragging's row is there (Look and Feel > Window dragging, #44). The collapsed layout is the sidebar's collapsed state (Chromium's, not a pref), so the layout picker doesn't offer it yet; the sidebar toggle does. Glance's are there (#37). |
| Settings: Vertical Tabs group (new tab button on the tab list / at the top, how to expand tabs), Spaces group (scroll modifier, natural scroll, new tab when the last unpinned tab closes, animation duration) | Missing | The Vertical Tabs group is declared; no Koan setting yet (#77). |
| Rename any tab (double-click its label, "Change Label…"; `zen.tabs.rename-tabs`) | Partial | Koan has Change Label… for pinned tabs only (#82). |
| Page-load progress bar (`zen.view.enable-loading-indicator`) | Missing | #82. |
| Settings: pinned tab close behaviour, restore pinned tabs to their URL | Same | Tab Management > Pinned Tabs. |
| Settings: container-specific Essentials | Missing | Chromium has no containers. |
| Switch to the most recently used tab on close | Same | Tab Management, on by default like Zen. A tab's opener still comes first, as in Zen (Firefox's owner rule). Only tabs you have looked at count: one opened in the background and never shown doesn't. In a Space it picks among the Space's tabs and the Essentials; collapsed groups are skipped. |
| Close tab and switch to its owner on Back with no history | Same | Tab Management, on by default. Upstream Chromium's back-to-opener: only for tabs opened from a link in another tab (Zen: tabs with an owner), never pinned ones. |
| Ctrl+Tab within Essentials or Space tabs; ignore pending tabs | Same | Tab Management, off by default. In tab order (Chromium's default), Ctrl+Tab stays in the active Space plus the Essentials, as upstream's group focus does, and these settings narrow it further. |
| Ctrl+Tab in recently used order (Firefox's setting, which Zen keeps) | Same | Chromium's own setting: chrome://settings > Appearance > "Cycle through the most recently used tabs with Ctrl+Tab" (off by default). It goes through the profile's tabs in every window and Space. When it is on it wins for Ctrl+Tab over Koan's two Ctrl+Tab settings, which then apply only to Ctrl+Page Up/Down; the Koan rows say so, like Zen's warning for the same combination. |

## Performance and power

Details, measurements and the reasoning for each default: `PERFORMANCE.md`.

| Zen | Koan | Notes |
|---|---|---|
| Restored tabs load when first opened | Same | Chromium loads 4 to 20 background tabs at startup; Koan loads only the visible one (`chrome://flags/#koan-restore-tabs-on-demand`). |
| Tab unloading under memory pressure only (Firefox's low-memory unloader) | Different | Chromium's Memory Saver, on by default: tabs hidden for 4 h are discarded until opened (Settings > Performance). |
| Unload Space / Unload all other Spaces (Space menu) | Different | In the Space menu; tabs in use stay loaded (see Spaces above). Memory Saver also discards a Space's tabs once they've been hidden for 4 h. |
| Background tabs throttled (Firefox timer budgets) | Different | Chromium's timer throttling, plus freezing of all but the 5 most recently used background tabs; pinned tabs (Essentials) and tabs that played audio are never frozen. Switch in Koan's settings (Tab Management > Performance). |
| No energy saver | Different | Chromium's energy saver, as in Chrome: on a MacBook it turns on below 20 % battery (Settings > Performance). |

## Smaller pieces

| Zen | Koan | Notes |
|---|---|---|
| Media controller at the bottom of the sidebar | Same | A card per background tab playing media (through a media session): site icon, previous / play-pause / next, mute; on hover title, artist, seekable progress, picture-in-picture and close. Shows after a second of sound, hides while its tab is selected or in picture-in-picture, back half a second after switching away; up to three cards stack. The same media session drives macOS Now Playing and the media keys (Chromium's, on by default on macOS). Zen's microphone / camera sharing card and its scrolling of long titles are missing. |
| Download animation | Same | The icon flies along Zen's arc (same timing, scale and fade) from the last click to the Library button, else the downloads button, else (compact mode hiding the sidebar) a tile with the Library icon at the bottom of the sidebar's side (right with tabs on the right, as in Zen). Chrome's own "download started" arrow is skipped while Zen's plays. |
| Drag the window from the top of the page (`zen.view.drag-window-from-content`) | Same | Top 5% of the page, empty space only (Zen's checks: no text, link, form control, media, frame, focusable or ARIA widget, interactive cursor); 4px threshold, 50px when maximized. On macOS the window follows the pointer until the button is released (Chromium's window move loop, as for a tab dragged out); on Linux the window manager moves it. Settings > Look and Feel > Window dragging. The page gets the button released when the drag starts, so a click on the empty space there may still fire. Only the active tab's page drags (in split view, the active panel). |
| Welcome screen on first run | Different | Same flow and words (title, import, colours, search engine, Essentials, default browser, done) in native views. It works from the keyboard and with screen readers (Tab, arrows, Esc to skip), and leaves the window's buttons (the traffic lights on macOS) usable. Koan paints a gradient instead of Zen's video, shows the Essentials as initials instead of the sites' logos, offers nine colours instead of Zen's gradient picker, and doesn't resize the window. Zen's ad-blocker page is left out: Chrome extensions install from the Chrome Web Store with the user's confirmation, so Koan can't install one for you. |
| Toasts | Same | One toast per window (`KoanToast`) with Zen's look: "Copied current URL!", "Tabs Closed! Use [Ctrl+Shift+T] to undo.", the split view limit, "Sidebar hidden!", and "New background tab opened!" (compact mode with the sidebar hidden, a page opening a tab in the background; its tab button switches to the tab) and "Workspace has been successfully renamed!" (renaming a Space in its header). Showing a toast replaces the one before (Zen stacks them in place). The pinned-tab toasts belong with #38's pinned tab URL editing. |

## Browser basics

| Zen | Koan | Notes |
|---|---|---|
| Engine | Different | Chromium (Blink, V8) instead of Firefox (Gecko). |
| Extensions | Different | Chrome extensions (Chrome Web Store), not Firefox add-ons. |
| Sync | Missing | Koan ships without Google API keys, so Google sync isn't available. |
| Automatic updates, "Update Complete!" sidebar notice | Missing | Install new releases from GitHub (#84). |
| Zen Share (share a Space or folder as a link) | Missing | Needs Zen's own server (`share.zen-browser.app`); there is no Koan service, so none is planned. |
| DRM video (Widevine) | Missing | Not bundled. |
