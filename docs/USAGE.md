# Using Koan

Koan is Chromium with Zen's way of working: tabs in a sidebar, the page as a
floating card, workspaces, Glance and compact mode. Everything web-facing is
plain Chromium (rendering, DevTools, extensions, `chrome://` pages).

## Shortcuts

Koan uses Zen's default shortcuts (Zen shortcuts version 21). Where
Zen deliberately rebinds a key that Chrome uses, Koan follows Zen; the
"Chrome had" column says what moved. Everything not listed behaves as in
Chrome.

| Action | Keys | Chrome had |
|---|---|---|
| Toggle compact mode | Ctrl+S | Save page (now Ctrl+Alt+Shift+S) |
| Show the sidebar in compact mode | Ctrl+Alt+S (⌘⌥S) | Stays out until pressed again |
| Next / previous Space | Ctrl+Alt+Right / Ctrl+Alt+Left (⌘⌥→ / ⌘⌥←) | macOS: next / previous tab (still Ctrl+Tab) |
| Space 1 to 10 | macOS: Ctrl+1 … Ctrl+9, Ctrl+0 (no keys elsewhere, as in Zen; set them in Settings) | |
| Split view: side by side | Ctrl+Alt+V (⌘⌥V) | |
| Split view: stacked | Ctrl+Alt+H (⌘⌥H) | macOS: Hide Others |
| Split view: grid (two panes in Chromium: side by side) | Ctrl+Alt+G (⌘⌥G) | |
| Unsplit | Ctrl+Alt+U (⌘⌥U) | macOS: View Source (still in the View menu) |
| New split with an empty tab | Ctrl+Shift+* (⌘⇧*) | |
| New tab (opens the URL bar; the tab opens with what you pick) | Ctrl+T (⌘T) | New-tab page |
| Copy the page's URL | Ctrl+Shift+C (⌘⇧C) | Inspect element (now Ctrl+Shift+L) |
| Copy the page's URL as Markdown | Ctrl+Shift+Alt+C | |
| Pin / unpin tab (pinned to its Space) | Ctrl+Shift+D | Bookmark all tabs |
| Close tab; a pinned tab or Essential instead goes back to its pinned URL, unloads and switches away | Ctrl+W, Ctrl+F4 (⌘W) | |
| Close all unpinned tabs (in the Space; keeps the selected one; Ctrl+Shift+T reopens the last closed tab, one per press) | Ctrl+Shift+K | |
| Expand Glance to a tab | Ctrl+O (⌘O) while Glance is open | Open file (still on Linux; unbound on macOS, as in Zen) |
| Glance a link | Alt+click (⌥+click; trigger method in Koan's settings) | (Alt+click downloaded the link) |
| Close Glance | Esc (twice while typing in the page), or click the dimmed page | |
| Save page | Ctrl+Alt+Shift+S | |
| Open / close the Library | Alt+Shift+L (⌥⇧L) | |
| Library: History | Ctrl+H or Ctrl+Shift+H (⌘⇧H or ⌘Y) | History page (Ctrl+H; macOS ⌘Y) |
| Inspect element | Ctrl+Shift+L | |
| New window | Ctrl+Shift+N | New incognito window |
| New private window | Ctrl+Shift+P | Print using system dialog |
| Mute site | Ctrl+M | |
| Bookmarks | Ctrl+B | |
| Library: Downloads | Ctrl+Shift+Y (⌘J) | macOS: Downloads page (⌘⇧J, now ⌘J) |
| Reading mode | Ctrl+Alt+R | |
| Edit the address | Ctrl+L, or click the URL pill | |
| **Koan only:** switch single / multiple toolbars | Ctrl+Alt+B | |
| **Koan only:** create a Space | Ctrl+Alt+N | |
| Koan settings (Zen's "Open Preferences") | Ctrl+, | |

Change any of them in Settings > Keyboard Shortcuts (below).

### On macOS

Zen's "accel" is ⌘ on a Mac, so the keys above that say Ctrl are ⌘ there
(the Space and Mute shortcuts excepted), shown in Zen's notation: `⌘ ⇧ C`.
Zen's Firefox-based macOS differences are followed:

| Action | Keys | What it takes |
|---|---|---|
| Toggle compact mode | ⌘S | Save Page (now ⌘⌥⇧S, also in File) |
| Show the sidebar in compact mode | ⌘⌥S | |
| Next / previous Space | ⌘⌥→ / ⌘⌥← | Chrome's tab switching (still ⌃Tab, ⌃⇧Tab, ⇧⌘[ and ⇧⌘]) |
| Space 1 to 10 | ⌃1 … ⌃9, ⌃0 | macOS's Switch to Desktop 1-4, if you turned those on in System Settings |
| Split view | ⌘⌥V / ⌘⌥H / ⌘⌥G / ⌘⌥U, ⌘⇧* | ⌘⌥H is Hide Others, ⌘⌥U View Source |
| New tab (opens the URL bar) | ⌘T | |
| Copy the page's URL (as Markdown) | ⌘⇧C (⌘⇧⌥C) | Inspect element (now ⌘⌥L; ⌘⌥C still works) |
| Pin / unpin tab; close all unpinned tabs | ⌘⇧D; ⌘⇧K | Bookmark All Tabs |
| New window | ⌘N (Chrome's), ⌘⇧N | New Incognito Window (now ⌘⇧P) |
| New private window | ⌘⇧P | |
| Inspect element | ⌘⌥L, ⌘⇧L | Downloads (⌘⌥L), collapse of the vertical tab strip (⌘⇧L); ⌘⌥C still works |
| Downloads | ⌘J (Firefox's) | Chrome's ⌘⇧J |
| Mute site | ⌃M (Control, as in Firefox) | |
| View Source; Bookmark Manager | ⌘U; ⇧⌘O (Firefox's, which Zen keeps) | ⌘⌥U (Unsplit's now); ⌘⌥B |
| Stop loading | ⌘. and the stop button, not Esc (Zen leaves Esc unbound) | Esc |
| Bookmarks sidebar | ⌘B | |
| Reading mode | ⌘⌥R | |
| Library | ⌥⇧L | |
| Koan settings | ⌘, | Preferences… (still in the Koan menu, no key) |
| **Koan only:** single / multiple toolbars; create a Space | ⌃⌥B; ⌃⌥N | |

Koan's shortcut takes the key where it is the same as Chrome's menu key, and
the menu bar says so: the Chrome item that Koan took the key from shows no
key (Hide Others, Bookmark All Tabs ...), and Save Page, New Private
Window, Inspect Element, Mute and Downloads show Koan's key. A Space or tab
shortcut that Chrome treats as reserved (⌘⇧N, ⌘⌥←, ⌘⌥→) is tried before
Chrome's. Settings > Keyboard Shortcuts refuses keys macOS keeps for
itself (⌘Q, ⌘H, ⌘M, ⌘Space, ⌘Tab, ⌘`, ⌃ with the arrow keys, ⇧⌘3/4/5 ...),
and says "Replaces Chromium's shortcut" for the keys it takes from Chrome's
menus.

**Menu bar.** Zen 1.24b's: File, Edit, View, Spaces, History, Bookmarks,
Tools, Window, Help. Chrome's Tab menu is hidden and its Profiles menu removed (Zen sets `browser.profiles.enabled=false`), and the keys
follow the shortcuts in effect (also after you change one in Settings):

- **Zen** (app menu): Settings… (⌘,), which opens Koan's settings.
- **File:** New Blank Window (⌘⇧N, Zen's; a plain new window here, there is
  no window sync) after New Window, and New Private Window (⌘⇧P) for
  Chrome's Incognito.
- **View:** Appearance > Automatic / Light / Dark first, as in Zen; Chrome's
  bookmark bar, full-screen toolbar, full URLs, AI, Lens, vertical tabs, Cast
  and Developer items are hidden. (Zen's Toolbars, Sidebar and Collapse
  Pinned Tabs aren't there: Koan has no such menu items.)
- **Spaces:** Create Space, Edit Theme…, Change Name…, Change Icon…, Next
  Space (⌘⌥→), Previous Space (⌘⌥←). Edit Theme… and Change Icon… open on the
  Space's header. The Library, compact mode, split view, Glance and pinning
  are in the sidebar, the command palette and their keys, as in Zen.
- **Tools:** Downloads (⌘J), Extensions, and Browser Tools (Developer Tools,
  JavaScript Console, View Source ⌘U, Task Manager).

Items are greyed when they don't apply (no Spaces, ...).

**Dock menu:** New Window and New Private Window, as in Zen's Dock menu, and
your Spaces (✓ on the active one) to switch the window that was active last.

**Dock icon:** after you close the last window (⌘⇧W or the red button), a click
on Koan's Dock icon brings that window back, with its Spaces and tabs, as in
Zen. This follows Settings > On startup: with "Continue where you left off" (the
default) the window comes back; otherwise you get a new window.

When a page uses a key itself (for example Ctrl+S in an online editor), the
page gets it first, as in every browser.

## Layout

Koan starts in Zen's default layout, **single toolbar**: there is no toolbar
across the top, and the page reaches the top of the window. The top row of
the sidebar has the sidebar toggle, back / forward / reload and the tab
groups / tab search buttons; below it is a rounded grey pill with the
current site's icon and name (just the host, like Zen).

![Single toolbar](screenshots/desktop-single-toolbar.png)

Click the pill or press Ctrl+L to edit the address (see [URL bar](#url-bar)).

As in Zen, nothing sits above the page but a thin margin: move the pointer
to the top edge of the window to show the window buttons (close, ...); the
page moves down to make room and back up when you leave. When the buttons
are on the sidebar's side of the window (e.g. GNOME's buttons with tabs on
the right, or a "close,minimize,maximize:" button layout with tabs on the
left), they stay in the sidebar's top row instead, as in Zen.

Ctrl+Alt+B switches to the multiple-toolbars layout (a toolbar above the
page, as in Chrome) and back. The layout applies to all windows and
persists.

**Sidebar toggle**: the button at the left end of the sidebar's top row
(⌘S) toggles compact mode, as in Zen: the sidebar hides, the page fills the
card and there is no toolbar. Chromium's own collapse to an icon rail, with
its tab group and tab search buttons, is not in Koan: Zen has neither, and
the sidebar is always vertical and always full width. That also goes for the
menus: there is no "Show tabs horizontally" in the tab menu, the View menu or
the app menu, and no "Expand tabs on hover" (always on) in the sidebar
button's menu, which is the sidebar's menu. The View menu's "Toggle Sidebar's
Width" does the same as the button and has no key (⇧⌘L opens the inspector, as
in Zen). The button's tooltip is Zen's, "Toggle Compact Mode". ⇧⌘A searches the
open tabs, as in Zen: the URL bar opens in its tabs mode (a "Tabs" chip; Chromium's @tabs), you
type to filter the open tabs, and Enter on a match switches to that tab.

**Tabs on the right**: right-click an empty part of the sidebar and pick
**Tabs on the right** (or Settings > Look and Feel > Vertical Tabs).
Everything mirrors: the sidebar is on the right, its top row and resize
edge too, and compact mode reveals it at the right edge. (In a
right-to-left UI, where the sidebar is normally on the right, it goes to
the left.)

On macOS the window buttons (traffic lights) are AppKit's and stay at the
top left. With the sidebar there they are in its top row; with tabs on the
right they stay at the left, in a strip above the page; with the sidebar
hidden (compact mode) they hide too and come back with the sidebar. With
the toolbar on single toolbar (the default) the pointer at the top edge
shows nothing and moves nothing in the window, as in Zen; where compact mode
hides the toolbar without single toolbar, the top edge still slides the
toolbar in, and the lights with it.

**Fullscreen on macOS** keeps the same layout, as in Zen: with the pointer
at the top edge the menu bar comes down with AppKit's title bar strip (about
32 pt, with the traffic lights), compact mode on or off; no Koan toolbar and
no bookmarks bar. The sidebar's top row has the sidebar toggle and back / forward
/ reload; Ctrl+L or the pill edits the address over the page.

**Bookmarks bar**: off, as in Zen: none anywhere, also on the new tab page
and after importing bookmarks. Turn it on in Koan's settings (Look and Feel
> Appearance > Bookmarks bar); Chromium's own "Show bookmarks bar" setting
and the View menu's "Always Show Toolbar in Full Screen" (disabled while the
toolbar is hidden) don't change that.

## URL bar

Like Zen's, the URL bar opens where you are and floats once you type:
clicking the pill (or Ctrl+L) edits the address right in the pill; the
first keystroke lifts it into a card centred over the window (two thirds
of its width, at most 750 px, larger text, a soft shadow over the page)
with the suggestions under it. The pill is empty meanwhile. Esc, a click
outside the card or opening a result (or running an action) closes it;
switching to another window and back leaves it as it was. In compact mode,
with the sidebar hidden, it floats right away, also when you click the pill
of the peeked sidebar.

| Editing in the pill | Floating once you type | Ctrl+T |
|---|---|---|
| ![In the pill](screenshots/urlbar-in-pill.png) | ![Floating](screenshots/urlbar-floating.png) | ![Ctrl+T](screenshots/urlbar-new-tab.png) |

**Ctrl+T** (⌘T; also the sidebar's **+ New Tab** and the menus' New Tab)
opens the URL bar, empty, instead of a new-tab page: the tab opens only
when you open something from it, in a new tab next to the current one.
(With Chromium's horizontal tabs, the tab strip's **+** still opens a
new-tab page.) Esc, a click outside or Ctrl+T again closes it without
creating anything; what you had typed comes back if you press Ctrl+T again
within 45 seconds.

Hover the pill (or Tab to it) for the **copy** button: it copies the page's
URL and says so ("Copied current URL!"), as do Ctrl+Shift+C and
Ctrl+Shift+Alt+C (Markdown link; ⌘⇧C and ⌘⇧⌥C on macOS). The site's icon at the start of the
pill opens the site's panel (security, permissions, cookies and site settings);
on an empty tab it is a magnifier that edits the address. As in Zen, the pill
and the URL bar show none of Chromium's page actions (site settings, install,
bookmark star).

| "Normal": editing in place, widened | Copied |
|---|---|
| ![Normal](screenshots/urlbar-normal.png) | ![Copied current URL!](screenshots/urlbar-toast.png) |

Settings, in Koan's settings (Ctrl+,) under Look and Feel > URL Bar
(profile prefs, Zen's names in brackets):

| Setting | Pref | Default |
|---|---|---|
| Behavior: **Normal** (always edit in the pill; it widens over the page while you type), **Floating only when typing** (as above), **Always floating** | `koan.urlbar.behavior` (`zen.urlbar.behavior`) | Floating only when typing |
| Open the URL bar instead of a new tab page | `koan.urlbar.replace_newtab` (`zen.urlbar.replace-newtab`) | on |
| Show the copy URL button | `koan.urlbar.show_copy_url` (`zen.urlbar.single-toolbar-show-copy-url`) | on |

In the multiple-toolbars layout the address is edited in the toolbar, as
in Chrome; Ctrl+T still opens it instead of a new-tab page, and Esc closes
it in one press.

## Command palette

As in Zen, the URL bar runs actions too. Type three or more letters of an
action ("compact", "pin tab", "dark") and up to three matching actions
appear right below the first suggestion, with their shortcut; pick one with
the arrow keys and Enter, or click it. An action you have run comes first
the next time, and typing its whole name ("toggle compact mode") makes Enter
run it; actions you keep passing over drop below the search suggestions.
Typing the start of an address still opens the site.

To see every action, press Tab in an empty URL bar (Ctrl+L, Backspace,
Tab) or type `>`; Tab again leaves. Typing filters the list, and once you
have typed two letters Enter runs the first match.

The actions, with Zen's names: Toggle Compact Mode, New Split View, Expand
current view (unsplit), Copy Current URL, Settings, Open Private Window,
Open New Window, Pin / Unpin Tab, Add to / Remove from Essentials, Close
Tab, Reopen Closed Tab, Duplicate Current Tab, Reload Tab, Reload Tab
Without Cache, Next / Previous Tab, Capture Screenshot, Find in Page, Manage
Extensions, Switch to Automatic Appearance / Light Mode / Dark Mode, Print;
with workspaces also New Space, Next / Previous Space and "Focus on" each
other Space (type its name). Only the actions that apply are offered (Unpin
Tab on a pinned tab, Next Space with two Spaces, ...).

## Sidebar

Vertical tabs are on by default, laid out like Zen's. The
**Essentials** are square tiles at the top, up to four across, shared by
every Space. Below them is the Space header (the active Space's icon and
name), then the Space's pinned tabs, a thin line, **+ New Tab** (opens a
tab at the top of the Space's other tabs) and the other tabs. The pinned
tabs, the line, New Tab and the tabs scroll together; the Essentials and
the Space header stay put. A tab's close button shows while you point at
it.

A pinned tab or Essential keeps its site, as in Zen: a URL on another site
typed in its URL bar (or a search) opens in a new tab, and a link in it that
would open a new tab on another site opens in Glance instead. Both can be
turned off in Koan's settings (Pinned Tabs, Glance). "Search for ..." in the
context menu of selected text also opens in Glance.

While the pointer is over the sidebar, **↓ Clear** appears at the end of
the line: it closes the Space's unpinned tabs except the selected one and
any playing sound (if those are all there is, it closes them too), the
same as Ctrl+Shift+K. The Space stays (an emptied one gets a fresh tab),
and a toast reminds you that Ctrl+Shift+T reopens them, the last closed
tab first, one per press.

Drag the sidebar's edge to resize it (163 to 500 px, as in Zen). At the
bottom is Zen's bottom bar: the Library button on the left, **+** on the
right, which opens the Create menu (Create Space, New Split and New Tab),
and the Space switcher in the middle: one narrow button per Space, centred.
The current Space shows its emoji (or a dot if it has none) in colour, the
others a small grey dot that turns into their emoji while you hover it.
The Space buttons only show once there is more than one Space.

Right-clicking an empty part of the sidebar opens Zen's toolbar menu:
**Compact Mode** (below) and **Tabs on the right**. Dragging the sidebar's
edge resizes it; dragging it closed hides it in compact mode, as in Zen
("Sidebar hidden! Use Ctrl+S to bring the sidebar back", shown once). If
compact mode was set to hide just the toolbar, the drag makes it hide both.

## Pinned tabs and Essentials

As in Zen there are two kinds of pinned tab:

- **Pinned tabs** belong to their Space and sit in its pinned section,
  between the Space header and the line above **+ New Tab**; unpinned, a
  tab moves below New Tab. Pin a tab with Ctrl+Shift+D or "Pin" in its
  context menu. While a pinned tab is loaded, pointing at it shows a dash
  ("Unload and switch to tab") where other tabs have their close button.
- **Essentials** are shared by all Spaces, up to 12 per window: "Add to
  Essentials" in the tab's context menu shows how many are used ("3 / 12").
  Dragging a tab into the grid also makes it an Essential.

Both remember the URL they were pinned on, the **pinned URL**. When a pinned
tab goes elsewhere, a slash appears after its icon: hover it to see the
pinned page's icon and "Back to pinned url", click to go back (Ctrl+click
first moves the current page to a new tab). The context menu also has
"Reset Pinned Tab", "Replace with Current URL" and "Edit Pinned URL…"
("…Essential…" for Essentials).

![A pinned tab away from its pinned URL, and its reset button hovered](screenshots/pinned-tab-reset.png)

Closing a pinned tab or an Essential (Ctrl+W, its close button, a middle
click) doesn't close it: like Zen, it goes back to its pinned URL, is
unloaded to free memory and the next tab is selected. A pinned tab that is
already unloaded closes from its close button. Settings > Tab Management >
Pinned Tabs has Zen's choices for what closing does, and "Restore pinned
tabs to their originally pinned URL on startup" (off by default, as in
Zen). Pinned tabs, like Essentials, are never frozen in the background,
and Close Other Tabs, Close Tabs Below and Back in a tab with no history
leave them open. Drag a pinned tab below the other tabs to unpin it, or a
tab among the pinned ones to pin it. With several pinned tabs selected,
Ctrl+W does the same to each of them and closes none of the others.

Any tab's context menu has "Change Label…" (edit the name in place; Enter
keeps it, Esc cancels, an empty name goes back to the page title), "Change
Icon…" (an emoji instead of the favicon) and "Unload" (free the tab's
memory; it reloads when you select it). Pinned URLs, labels and icons are
kept with the session.

## Spaces

A Space is a set of tabs shown on its own, like Zen's Spaces. The window
always shows one: a new window starts in a Space named "Space", every tab
belongs to a Space, and new tabs open in the active one.

- **Create** a Space with **+ > Create Space** (or Ctrl+Alt+N): the sidebar
  shows Zen's Create-a-Space form (a collapsed sidebar expands over the
  page until you're done). Click the dashed box to pick an emoji
  (search by keyword), type a name, Theme… to pick its theme (see Space themes), then
  Create Space (or Enter); Escape or Cancel goes back.
- **Rename**: double-click the Space's name in the header (or right-click
  the header > Change Name…) and type; Enter keeps it, Escape cancels.
  Double-click the icon (or Change Icon…) to pick another emoji, or none.
- From the keyboard, the header takes focus (Tab through the sidebar):
  Enter or the menu key opens its menu, F2 renames the Space.
- **Theme…** (right-click the header) opens Zen's theme picker for
  the Space (see Space themes below).
- **Switch**: click a Space in the bottom bar, Ctrl+Alt+Left/Right (⌘⌥
  on macOS; they wrap around), on macOS Ctrl+1…0 for Space 1 to 10, swipe
  horizontally with two fingers over the sidebar (on a Mac, as in Zen, the Space moves against your
  fingers: moving them right goes to the next Space, and lifting them
  after enough travel, or a quick flick, switches Space), or turn the mouse wheel
  there (horizontally, or vertically while holding Ctrl); the right-click
  menu of the header also lists the Spaces. The old Space slides out and
  the new one in, as in Zen, and you land on the tab you left the Space on
  (also after a restart). Going straight to a tab of another Space (the
  address bar's "Switch to this tab", tab search) shows that tab.
- **Move To**: right-click a tab > Move To moves it (and the other
  selected tabs) to another Space and shows it.
- **Unload Space** (right-click the header or a Space button) frees the
  Space's tabs; they reload when you select them. Unload All Other Spaces
  frees all the others. Tabs on screen and tabs in use (playing sound, in
  a call or sharing the screen, open in DevTools, with a half-filled form,
  and others Memory Saver keeps) stay loaded; a toast says how many.
- **Delete Space** (same menu) closes the Space's tabs after asking. The
  last Space can't be deleted.
  It is gone for good: reopening one of its tabs (Ctrl+Shift+T, History)
  puts the tab in the Space you're in.
- Closing a Space's last tab leaves the Space with a new tab, where Zen
  shows an empty Space.
- Tabs in other Spaces keep running like any background tab (Koan's
  background-tab policy freezes all but the 5 most recently used ones);
  switching Space doesn't freeze them.

Spaces and the active one are restored on restart (Koan restores the
previous session by default). With Settings > On startup set to the New
Tab page or to pages, the window still opens with your Spaces (their
names, icons and colours; each with a new tab, the startup pages in the
Space that was active). So does a window opened on macOS after you closed
the last one.

Chrome's tab group commands do Zen's thing in a Space window: ⌘⌃P
(New tab group) opens the Create-a-Space form, ⌘⌃W (Close group) asks to
delete the Space, ⌘⌃X / ⌘⌃Z go to the next / previous Space, ⌘⌃C opens a
new tab as ⌘T does. Tab > Group Tab isn't there (use Move To). A Space is
never collapsed, and a tab group an extension makes becomes a Space with
the default theme (unless the extension sets a colour).

## Space themes

Every Space has its own gradient theme, as in Zen. Right-click the Space's
header (or a Space in the bottom bar) and choose **Theme…**, or run
**Open Theme Picker** from the command palette (for the active Space), to
open Zen's theme picker beside the sidebar. In the Create-a-Space form,
**Theme…** picks the new Space's theme; the window previews it until
you create the Space or cancel.

- **Colour field**: click to place a colour, drag the big dot to change it,
  right-click it to remove it. Up to three colours.
- **+ / − / harmony**: add or remove a colour; the third button cycles the
  colour harmony (Complementary, Split, Analogous, Triadic, Floating) that
  places the other dots around the big one.
- **Presets**: five pages of Zen's preset swatches (arrows to page).
- **Opacity**: the wave slider; a flatter wave is a lighter tint. On macOS,
  over the glass (the blurred desktop behind the window), it sets how much
  of the blur shows: to the left Zen's most transparent, to the right fully
  opaque (no blur). It works without colours there too, and each Space keeps
  its own value.
- **Grain**: drag the handle around the dial for Zen's grain texture.
- **Automatic / Light / Dark** (top of the field): the window scheme, which
  websites follow too.

From the keyboard: Tab to the field, Enter adds a colour, the arrow keys
move the big dot (Shift for bigger steps), Delete removes it; the swatches
are one Tab stop (arrows move between them, Space applies one); the opacity
slider and the grain dial take the arrow keys, Home and End.

Changes show on the window at once and are saved when the panel closes.
A Space's theme is kept with its tabs in the session, so it comes back
after a restart; a tab moved to another Space takes that Space's theme.
The window's sidebar, toolbar and the margin around the page show the
gradient; switching Spaces cross-fades to the next Space's gradient. A
theme also decides whether the window's own colours are light or dark and
sets their accent, like Zen; websites keep following the appearance setting.

What websites use (Automatic, Light or Dark) is the picker's scheme
buttons, and the palette's "Switch to ... Appearance / Mode".
**Settings > Look and Feel > Dark Theme Styles** picks Default, Night (a
black window) or Colorful (dark surfaces tinted with the accent); Night and
Colorful are Koan's own (Zen has only the setting's name so far).
**Settings > Tab Management > Workspaces** turns Space themes off. The
custom colour list of Zen's picker is off by default, as in Zen; turn it on
with the pref `koan.theme.gradient.show_custom_colors`.

With a Chrome Web Store theme installed, the theme keeps the window's look
and Space themes are off until you remove it (Settings > Appearance >
Reset to default).

## Compact mode

Ctrl+S hides the sidebar; the page fills the window with a thin margin.
Move the pointer to the left edge (or press Ctrl+Alt+S) to slide the
sidebar out, with its navigation buttons and URL pill. Ctrl+L and Ctrl+T
open the floating URL bar. The setting applies to all windows and persists.
The sidebar slides in over the page; the page doesn't move or reflow.

In the multiple-toolbars layout you choose what compact mode hides, in the
sidebar's menu under **Compact Mode**: **Hide sidebar** (the default, as in
Zen), **Hide toolbar** or **Hide both**. A hidden toolbar slides down when
the pointer reaches the top edge, with the window buttons.

The same choices are in Settings (Ctrl+,) > Look and Feel > Show in
compact view, with Zen's "Briefly make the toolbar popup when switching or
opening new tabs in compact mode" (off by default): the hidden sidebar then
pops up for a moment whenever you switch tabs.

## Glance

Alt+click a link (or Alt+Enter on a focused link) to preview it in a card
over the page, as in Zen. The card grows out of the click and shrinks back
into it when it closes; the page behind fades into the window background.
**Open Link in Glance** in the link context menu does the same. An Alt+click
on a link in the Glance page shows that link in the card. Shift+click opens
a new window, as in Zen and Firefox. Same-site links with a `download`
attribute still download.

Beside the card: close (Esc, or a click on the faded page), **Expand
Glance** to a tab (Ctrl+O; ⌘O on macOS) and **Split Glance** with the
current tab. As in Zen, while you type in the Glance page the first Esc
only turns the close button red ("Press Esc again to close"); a second Esc
within 3 seconds closes. A page that asks before you leave it ("Leave
site?") gets to ask.

On macOS, as in Zen (whose default trigger is `alt` on every platform):
Alt is the Option key, so ⌥+click opens Glance, Meta is ⌘, and the split
view shortcuts are ⌘⌥V / ⌘⌥H / ⌘⌥G / ⌘⌥U, which take over View Source
(⌘⌥U) and Hide Others (⌘⌥H) from the keyboard. Ctrl+click is a right-click
on macOS, so it isn't offered as a trigger there. On macOS the card
appears and goes without growing and shrinking (not verified on a Mac
yet).

**Settings** (Koan's settings, Look and Feel → Glance; Zen's
`zen.glance.enabled` and `zen.glance.activation-method`): **Enable Glance**,
and **Trigger method**: Ctrl + Click, Alt + Click (the default), Shift +
Click or Meta (Command) + Click. The trigger method reaches pages opened
after the change (pages already open, and new ones that share their
renderer, keep the old method until Koan restarts). With another trigger
method, a click Glance can't open (not a web link, an app window) does
what that modifier does without Glance (Ctrl: a background tab; Shift: a
window). The "Koan Glance" flag in `chrome://flags` removes Glance
altogether.

## Split view

Besides the shortcuts above:

- Tab context menu: **Add Split View...** splits the tab with the current
  one (or with a new tab, for the current tab), **Join 2 Tabs** splits a
  two-tab selection, **Split Out Tab** separates a split (Zen's labels).
  Upstream's **Arrange split view** and **Move tab into split view** stay.
- Link context menu: **Split Link to New Tab** opens the link beside the
  page; in a split, "Open link in left/right view" loads it in the other
  view.
- Drag a tab onto the left or right edge of the page to split with it
  (Chromium's split view drop targets).
- Chromium's split views have two panes (Zen's take four, in a grid): a
  third panel (Split Glance over a split, a split shortcut with more tabs
  selected) shows Zen's "Can't add more panels to the split view!" toast.

## Look

Koan's default look is **Zen's**: its colours (Zen 1.24b's zen-theme.css:
window frame `#181717` / `#e5e5e5`, content `#101010` / `#fafbff`, dialogs, panels, text, borders,
the system accent colour), per-Space gradient themes and, on macOS, glass.
Everything Koan or Chromium draws (the frame, sidebar, tabs, Essentials,
Spaces bar, URL bar, menus, bubbles, dialogs, find bar, settings, the new tab
page) takes its colours from that token set, so Chromium's own surfaces
match Zen too. The Zen look follows the system's light or dark mode.

**shadcn/ui's design system** is a second look, dark and near-black
(Chromium's greys never show): the same surfaces take their colours from
shadcn's tokens (`background`, `foreground`, `card`, `popover`, `primary`,
`secondary`, `muted`, `accent`, `destructive`, `border`, `input`, `ring`, the
`sidebar-*` set) and their corner radii from `--radius` (10px: sm 6, md 8,
lg 10, xl 14). The font is Geist (bundled, SIL OFL), from the next start.

**Settings > Look and Feel > Appearance** (browser-wide, applies at once to
every window):

- **Look**: Zen (default) or shadcn/ui. Zen has its gradient themes and the
  theme picker's per-Space colours; the choices below are shadcn's.
- **Mode** (shadcn only; Zen follows the system): Dark (default), Light, or Automatic (follow the system). The
  mode is the window's colour mode, so on macOS the title bar, scrollbars and
  native menus follow it. Websites keep their own colour scheme
  (Chromium's Appearance setting), as in Zen. Private windows are always
  dark.
- **Base colour**: Black (default), Neutral, Zinc, Slate, Stone or Gray.
  Black is Koan's near-black Zinc: background and frame `#09090b`, cards and
  popovers `#18181b`, borders and hover surfaces `#27272a`. The others are
  shadcn's own (Neutral's dark background is `#0a0a0a`, its cards `#171717`).
- **Accent colour**: none (the base's own primary colour), Blue, Green,
  Orange, Red, Rose, Violet or Yellow.
- **Paste a theme**: the CSS that ui.shadcn.com/themes or tweakcn.com makes
  (`:root { --background: oklch(...); ... } .dark { ... }`, in oklch, hsl,
  rgb or hex, and `--radius`). Unknown variables are ignored and missing
  ones keep the base colour's; text without any token is refused. Remove
  it to go back to the base colour.

The colours are shadcn's own OKLCH values, converted to sRGB exactly, and
generated from its registry (`scripts/gen_shadcn_palettes.py`).

In both looks the floating URL bar (typing in the URL bar, or ⌘T / ⌘L) is a
command-dialog card with a magnifier, a "Search..." placeholder (muted, also
while focused), a thin divider under the field and "title — url" suggestions.

- **Zen look**: the card is two thirds of the window wide (at most 750 px),
  centred as Zen centres it, on a flat `#181818` (white in light mode) fill
  with a 0.5 px outline, radius 15 and Zen's long soft shadow. The input row
  is 62 pt high with 17 pt medium text and no focus ring; the divider is
  inset from the card's edges. Suggestion rows are 49 pt high, radius 8, in
  14 pt medium text, the selected row in the accent colour. These row
  styles apply only to this card: any other omnibox popup keeps Chromium's.
- **shadcn look**: the card is 55% of the window wide, a little above the
  middle, on `popover` (#18181b) with a 1px `border` outline, shadcn's soft
  shadow and a `rounded-xl` corner, in Geist. Suggestions below it are
  `popover` too, with `accent` for the selected row, and the url in
  `muted-foreground`. Not yet: the background blur and the card's 0.9 alpha.

Limits: menus macOS draws itself (page context menus, the menu bar) are
dark or light with the mode but not coloured by the tokens; the font is
loaded at start. The system's high-contrast setting is not
followed by the colours the look sets itself (the system and core colours);
only Chromium's own high-contrast paths still apply.

## Settings

`chrome://koan-settings` (or Ctrl+,) has Koan's settings, laid out like
Zen's preferences; everything else is in Chromium's own settings, linked
from its sidebar.

- **Look and Feel**: the look (see "Look" above), the browser layout (Only
  Sidebar / Sidebar and Top Toolbar), tabs on the right, compact mode, what
  it hides and its flash popup.
- **Tab Management**: whether windows show the active Space's theme,
  whether background tabs freeze (see "Memory and power"), and Zen's tab
  behaviours (Zen's defaults):
  - closing a tab switches to the **most recently used** tab rather than a
    neighbour (on);
  - **Back** in a tab with no history (one opened from a link in another
    tab) closes it and returns to that tab (on; never for pinned tabs);
  - **Ctrl+Tab** (and Ctrl+Page Up/Down) cycles within the Essentials or
    within the other tabs, whichever you are on (off), and can skip tabs
    that haven't loaded yet (off). If "Cycle through the most recently used
    tabs with Ctrl+Tab" is on in Chromium's settings (Appearance), Ctrl+Tab
    follows that instead, as the page notes; Ctrl+Page Up/Down still follow
    Koan's settings.
- **Keyboard Shortcuts**: Zen's shortcut editor. Click a shortcut, type
  the new keys and press **Esc** (or **Enter**) to save them; **Backspace** clears the
  shortcut, **Tab** leaves it unchanged. A key another shortcut already
  uses is refused ("Conflicts with ..."), and so are Ctrl+Tab and
  Ctrl+Page Up/Down, which switch tabs. A key needs Ctrl, Alt or Win
  (⌃, ⌥ or ⌘ on macOS, where the keys macOS keeps for itself are refused too;
  or is a function key). **Reset to Default** restores Zen's keys.
  Shortcuts for features switched off in `chrome://flags` aren't listed.

Changes apply at once to every window, private windows included (they
share the regular profile's settings), and the page follows changes made
elsewhere (e.g. Ctrl+S or Ctrl+Alt+B). From a private window, Ctrl+, opens
the page in a regular one, like Chromium's settings.

![Settings](screenshots/settings-look-and-feel.png)

## Memory and power

Koan is set up to stay light (details in `PERFORMANCE.md`):

- **Tabs restore on demand**: after a restart, or when you reopen a closed
  window, only the tab you see loads; the others load when you open them,
  as in Zen.
- **Memory Saver is on**: a tab hidden for 4 hours (for example in a Space
  you haven't used) is unloaded and reloads when you open it. Pinned tabs,
  tabs playing audio and sites you exempt are kept. Change it, or add
  exceptions, in Settings > Performance.
- **Background tabs freeze**: apart from the 5 tabs you used last, tabs in
  the background stop running (they wake for a few seconds each minute).
  Pinned tabs never freeze, nor do tabs that played audio in the last hour
  (so media keys still work), are in a call, use a device or show
  notifications; that holds under energy saver too. Sites on Settings > Performance > "Always keep these
  sites active" never freeze either. Turn freezing off in Koan's settings
  (`chrome://koan-settings`, Tab Management > Performance).
- **Energy saver** works as in Chrome: on a MacBook it turns on below 20 %
  battery (Settings > Performance > Energy saver).

## Library

Zen's Library: press Alt+Shift+L or click the Library button (the box with
sparkles, at the start of the sidebar's bottom bar, before the Space
buttons). It slides in over the sidebar
with three sections down its side, and opens on the one you used last:

- **Downloads** (Ctrl+Shift+Y): every download, newest first, with its size,
  site and date (hover a row for where it came from). Search, or filter by
  file type (images, video, audio, documents, archives, apps) and date.
  Hover or focus a row for cancel / retry / show in folder and **⋯** (open,
  cancel, retry, copy the file or its link, remove from the list, delete
  the file after asking); right-click or the menu key opens the same menu.
  Click a finished download to show it in the file manager, or open it from
  **⋯** (a download in progress isn't set to open when done: a file only
  opens when you open it). A download Chrome warns about (dangerous or
  insecure) is never opened from here: clicking it shows Chrome's warning,
  where you keep or delete it. While anything downloads, the Downloads tab
  shows a progress ring. Right-click the Downloads tab to clear the list or open
  `chrome://downloads`.
- **Spaces**: a card per Space with its tabs, tinted with its theme's main
  colour. Click the name to rename it, the icon for the Spaces' emoji
  picker, the brush for the theme picker; drag the arrows at the bottom to reorder the Spaces (the
  bottom bar follows). **⋯** has Change Icon, Theme…, Unload Space,
  Unload All Other Spaces, Create Space and Delete Space (it asks, and not
  for the only Space). **+** after the last card, like Create Space, closes
  the Library and opens the Create-a-Space form. Click a tab to go there.
  The Library widens to fit the cards.
- **History** (Ctrl+H): the last 120 days by day. Search, or **Filter**:
  when (today since midnight, this week, this month), sort (by date, site, most visited,
  last visited) and what to look through (history or the tabs closed
  recently). Click a page to open it in a new tab; Ctrl+click or
  middle-click opens it in the background and keeps the Library open.
  Hover a row for its visit time and the forget / reopen buttons;
  right-click for "Forget About This Site" (its history, cookies, site data
  and cache). Right-click the History tab to clear history from the last
  hour, 12 hours, today, week, month or everything (it asks first, naming
  the range), or to open `chrome://history`. Where policy forbids deleting
  history (AllowDeletingBrowserHistory) nothing here offers to, as on
  Chrome's History page.

Esc, the back arrow at the bottom of the side column, the same shortcut
again or opening a tab closes the Library. Incognito windows have no
Library: there Ctrl+H shows Chrome's notice that history isn't saved, as
in Chrome, and the Downloads key opens `chrome://downloads`.
With "Reduce motion" set in the system, it opens and closes without
sliding.

## Media

When a tab in the background plays music or video, a card appears at the
bottom of the sidebar, as in Zen (macOS Now Playing and the media keys
control the same media): the site's icon, previous / play-pause / next and
mute. Hover it for the title and artist, the progress (click or drag to
seek), picture-in-picture and close. Click the card (or the icon) to go to
its tab, in its Space; the card hides while that tab is selected. Up to
three cards stack, the newest in front. Close pauses the media and
dismisses its card. From the keyboard, Tab into a card to expand it; on the
progress bar Left/Right seek by 5 seconds, Home/End jump to the ends.

![Media card, collapsed and on hover](screenshots/media-cards.png)

## Downloads

With the system's reduced motion setting on, Koan skips its animations
(the download animation, the welcome screen's, the media cards' fades).

Starting a download sends a small download icon flying from where you
clicked to the Library button at the bottom of the sidebar (or the
downloads button, with the toolbar showing and no Library button). While
compact mode hides the sidebar, it lands on a tile with the Library icon
that slides in at the bottom corner on the sidebar's side for a moment.

![Download animation](screenshots/download-animation.png)

## Moving the window

Drag empty space at the very top of a page (the top 5%) to move the window,
like a title bar, as in Zen. Text, links, buttons, videos and anything else
on the page that you can click or select keep working as usual; a click
there is still a click. Turn it off in Settings > Look and Feel > Window
dragging.

## Welcome

The first time Koan starts with a new profile it shows Zen's welcome
screen: "Welcome to a calmer internet", then a few pages to import from
another browser, pick a colour, choose the search engine and your
Essentials, and make Koan the default browser. Every page can be skipped
with Next. It shows once (start Koan with `--force-first-run` and a new user data
directory to see it again). Tab moves between its buttons, the arrow keys
between choices, Enter or Space picks, and Esc skips the rest. The window's
own buttons stay usable above it.

| | |
|---|---|
| ![Welcome](screenshots/welcome-start.png) | ![Import](screenshots/welcome-import.png) |

## Translucent window (macOS)

Like Zen's, the window is glass: the sidebar, the toolbar and the gutter
around the page show the desktop through, blurred, and the page is an opaque
card on top, with Zen's default theme's tint over the blur (dark: black at
40%, light: none). Turn it off with "Translucent window" in
`chrome://koan-settings` (Look and Feel, Theme Settings); it is off by itself
while macOS's Reduce Transparency (System Settings, Accessibility, Display)
is on, and while the window is being captured. An inactive window goes flat and opaque,
like Zen's. It needs macOS 26 or later, and on an Intel Mac it is off until you turn it on; not on other platforms.

Zen's `zen.theme.acrylic-urlbar` is "Glass URL bar" (`chrome://koan-settings`,
URL Bar): the floating URL bar (⌘T / ⌘L) turns translucent with the page
blurred behind it. Off by default, as in Zen; it also stays opaque while
macOS's Reduce Transparency is on and in a window that isn't active (Zen's
rules). The card is translucent over a blur of the page; only the card of
the window it opens in changes. The suggestion list under it stays opaque. Off
macOS only the page is blurred.

## Icons

Icons have Zen's look: the same vocabulary (back, reload, sidebar, +, close,
...), sizes (17 pt toolbar, 16 pt favicon and close, 18 pt menus, 20 pt
bottom-bar +), weight and rounded caps, in monochrome that takes the text
colour. Zen's own icons can't be redistributed (Nucleo), so Koan draws them
from [Lucide](https://lucide.dev) (ISC); see `.agent/notes/koan-icons.md`
for the table. Icons that Chromium's UI asks for in its Material style
(toolbar, tabs, location bar, menus, permission chips, download bubble) are
swapped for them. Web pages of Chromium (Settings, History, New Tab) keep their
own icons.

## Turning things off

Every Koan feature has a switch in `chrome://flags` (search "Koan"): Koan
sidebar, compact mode, workspaces, Glance, restore tabs on demand, freeze
background tabs, command palette, pinned tabs and Essentials, Library, media
controls, download animation, window dragging from pages, welcome screen.

The media controller and the download animation are on by default; like
Zen's (which keeps them in about:config), they have no setting yet, only
the prefs `koan.media_controls.enabled` and
`koan.downloads.download_animation`.

In compact mode with the sidebar hidden, a tab a page opens in the
background shows Zen's "New background tab opened!" toast; its button
switches to the tab. Renaming a Space in its header shows "Workspace has
been successfully renamed!".

Scrollbars are thin overlay bars that show while you scroll, like Zen's. For
classic, always-visible scrollbars start Koan with
`--disable-features=OverlayScrollbar`.

## Remote debugging

For scripts and tools that drive Koan over the DevTools protocol, start it
with its own profile (a throwaway directory is fine):

```sh
/Applications/Koan.app/Contents/MacOS/Koan --user-data-dir=/tmp/koan-test --remote-debugging-port=9222
```

On Koan's default profile (`~/Library/Application Support/Koan`) the
`--remote-debugging-port` and `--remote-debugging-pipe` switches are
refused, as in Chrome, so another program can't take over your real
browser by relaunching it. See [SECURITY.md](../SECURITY.md#remote-debugging).

## Installing the macOS build

A test build for Apple Silicon Macs (macOS 13 or later) is made every night
from `develop`:

- **Nightly pre-release:** the `nightly` release on the
  [Releases page](https://github.com/Chaitanya045/koan/releases/tag/nightly),
  `Koan-<version>-<commit>-arm64.dmg` and its `.sha256`. Each new nightly
  replaces it.
- **Any run** (also of a branch): open a run of the
  [macOS nightly workflow](https://github.com/Chaitanya045/koan/actions/workflows/macos.yml)
  and click the `Koan-….dmg` under "Artifacts" (you need to be signed in to
  GitHub). The file is the .dmg itself, not a zip. From a terminal
  (`gh run download` can't fetch it, because it expects a zip):

  ```sh
  run=<run id>
  id=$(gh api repos/Chaitanya045/koan/actions/runs/$run/artifacts \
    --jq '.artifacts[] | select(.name | endswith(".dmg")) | .id')
  gh api repos/Chaitanya045/koan/actions/artifacts/$id/zip > Koan.dmg
  ```

Open the .dmg and drag Koan to Applications.

The app isn't signed with an Apple Developer ID or notarised (it is only
signed ad hoc, which Apple Silicon needs to run it). So the first time,
macOS says "'Koan' Not Opened: Apple could not verify 'Koan' is free of
malware", with only **Done** and **Move to Bin**. On current macOS,
right-click → Open no longer gets past this. Allow it once:

1. **In Terminal**, remove the quarantine flag that the download added
   (simplest):

   ```sh
   xattr -dr com.apple.quarantine /Applications/Koan.app
   ```

2. Or click **Done**, then open **System Settings → Privacy & Security**,
   scroll down to the message about Koan and click **Open Anyway** (and
   confirm with your password).

If macOS instead says Koan **"is damaged and can't be opened"**, sign it
again on your Mac (ad hoc, with the `codesign` tool that comes with
macOS), then open it:

```sh
codesign --force --deep -s - /Applications/Koan.app
```

After that it opens like any other app. Every new build has a new ad-hoc
signature, so macOS asks again for access to the "Koan Safe Storage"
keychain item (where saved passwords and cookies are encrypted) after you
install a new nightly. Click **Always Allow**. The profile is in
`~/Library/Application Support/Koan`.

## Agent control

**Agent ready** (Settings > Agent Control, on by default) lets a local agent,
such as Claude Code or a script, open and work in background tabs. Koan listens
on a private unix socket in `~/Library/Application Support/Koan/AgentControl/`,
and **every new program has to be approved in a dialog that names it** before
it can do anything. While an agent is connected, a pill in the bottom corner of
every window says so; **Stop** closes every agent connection and forgets every
approval. Turning Agent ready off closes everything at once. It is off in
Incognito and Guest launches and with another `--user-data-dir` unless you turn
it on there. Your organisation's `RemoteDebuggingAllowed` policy turns it off.

The limits are settings: awake agent tabs (4, halved on battery in Low Power
Mode), approval requests per minute (3) and failed attempts before a 5-minute
block (5). Approvals last until Koan quits for now. The command line tool and
the tab features come next (see `.agent/notes/agent-control.md`).

## Install in the app menu

```sh
scripts/install_desktop.sh               # adds Koan + icon for this user
scripts/install_desktop.sh --uninstall
```

Koan keeps its profile in `~/.config/koan`, separate from Chromium's.
