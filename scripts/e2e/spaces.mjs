// End-to-end test of Zen's Spaces on a real build: the window always shows
// one Space, the Space header (icon and name, its menu: Change Name...,
// Change Icon..., Unload, Delete with confirmation) and its keyboard path,
// the bottom bar (one button per Space, "+" with the Create menu), the
// Create-a-Space form with the emoji picker (also from a collapsed
// sidebar), switching (bottom bar, keys with wrap-around), each Space's
// last active tab, "Move To" in the tab context menu and a Space whose last
// tab closes coming back with a new tab. (The wheel, swipes and restarts:
// spaces_switching.mjs.)
//
//   scripts/e2e/run.sh spaces
import {
  ALT, CTRL, SHIFT, browserCall, check, evalIn, finish, key as pageKey,
  menuMouse, openMenus, pages as pageTargets, readPref, sleep, typeInto,
  viewEvent, viewMouse, viewTree, visiblePage, waitFor, windowTints,
} from './lib/koan.mjs';

const views = viewTree;
const shown = (vs, cls) => vs.filter(v => v.cls === cls && v.shown);
const one = (vs, cls) => shown(vs, cls)[0];
const mouse = viewMouse;
const type = typeInto;
const menus = openMenus;
const chooseMenuItem = (menu, item) => menuMouse(menu, item);

async function click(view, button = 'left') {
  await mouse(view, 'mousePressed', 10, 10, button);
  await mouse(view, 'mouseReleased', 10, 10, button);
  await sleep(1200);
}

// A key press on a view (UI DevTools).
async function viewKey(view, keyCode) {
  await viewEvent(view, 'DOM.dispatchKeyEvent',
                  { type: 'keyPressed', keyCode, code: 0, flags: 0, key: 0, isChar: false });
  await sleep(1200);
}

// Sends a key to the active tab's page; keys it doesn't handle reach the
// browser's shortcuts, like real input.
async function key(code, modifiers = 0, keyName = '') {
  await pageKey(await visiblePage(), code, modifiers, keyName);
  await sleep(1200);
}

// The pages that still have a renderer (not discarded), and the visible
// one's URL.
async function pages() {
  const live = [];
  let visible = null;
  for (const page of await pageTargets()) {
    const state = await evalIn(page, 'document.visibilityState');
    if (state) live.push(page.url);
    if (state === 'visible') visible = page.url;
  }
  return { live, visible };
}

// The sidebar's state: Space buttons, header, visible tabs.
async function sidebar() {
  const vs = await views();
  // In their order on screen (UI DevTools doesn't follow reordered views).
  const buttons = shown(vs, 'KoanSpaceButton').sort((a, b) => a.ax - b.ax).map(b => ({
    label: b.props.Label ?? '', name: b.props.TooltipText ?? '',
    active: b.props.Active === 'true', view: b }));
  const header = one(vs, 'KoanSpaceHeaderView');
  return {
    vs,
    buttons,
    header: header && header.h > 0 ? { name: header.props.Name ?? '',
                                       icon: header.props.Icon ?? '',
                                       view: header } : null,
    tabs: shown(vs, 'TabView').filter(t => t.h > 1).length,
    create: shown(vs, 'ImageButton').find(b => b.props.TooltipText === 'Create New'),
  };
}

const VK = { N: 78, T: 84, W: 87, LEFT: 37, RIGHT: 39, RETURN: 13, ESCAPE: 27, F2: 113 };
const ROCKET = '\u{1F680}';

// ---- One Space from the start, like Zen.
let s = await sidebar();
check('The window starts in one Space named "Space"',
      s.header?.name === 'Space' && s.header.icon === '' && s.tabs === 2,
      JSON.stringify({ header: s.header?.name, tabs: s.tabs }));
check('The bottom bar hides the Space buttons while there is one Space',
      s.buttons.length === 0 && !!s.create, `${s.buttons.length} button(s)`);
check('No tab group header or Unfocus pill in the Space',
      shown(s.vs, 'TabGroupHeaderView').every(v => v.h === 0 || v.w === 0) &&
      shown(s.vs, 'TabGroupUnfocusButton').length === 0);

// ---- "+": Zen's Create menu (Create Space | New Split, New Tab; features
// add theirs).
await click(s.create);
let plus = menus(await views())[0];
check('"+" opens the Create menu', plus?.items.length === 3,
      `${plus?.items.length} item(s)`);
if (plus) {
  await chooseMenuItem(plus, plus.items[0]);
}
s = await sidebar();
const plusForm = one(s.vs, 'KoanSpaceCreationView');
check('...whose Create Space opens the Create-a-Space form', !!plusForm);
if (plusForm) {
  await click(shown(s.vs, 'MdTextButton')
      .find(b => b.path.includes('KoanSpaceCreationView') && b.props.Text === 'Cancel'));
}

// ---- The Create-a-Space form (Ctrl+Alt+N), with the emoji picker.
await key(VK.N, CTRL | ALT, 'n');
s = await sidebar();
let form = one(s.vs, 'KoanSpaceCreationView');
let createButton = shown(s.vs, 'MdTextButton')
    .find(b => b.path.includes('KoanSpaceCreationView') && b.props.Text === 'Create Space');
check('Ctrl+Alt+N opens the Create-a-Space form in place of the Space',
      !!form && form.h > 100 && !s.header && s.tabs === 0,
      JSON.stringify({ form: form && form.h, header: !!s.header, tabs: s.tabs }));
check('...with Create Space disabled until the Space has a name',
      createButton?.props.Enabled === 'false', createButton?.props.Enabled);

await click(one(s.vs, 'KoanSpaceIconField'));
let vs = await views();
const search = shown(vs, 'Textfield').find(t => t.path.includes('KoanEmojiPicker'));
check('The icon opens the emoji picker', !!search);
await type(search, 'rocket');
vs = await views();
const cells = shown(vs, 'KoanEmojiCell');
check('Searching the picker filters the emoji',
      cells.length === 1 && cells[0].props.Label === ROCKET,
      cells.map(c => c.props.Label).join(' '));
await click(cells[0]);
vs = await views();
check('Picking an emoji sets the icon',
      one(vs, 'KoanSpaceIconField')?.props.Icon === ROCKET);

const nameField = shown(vs, 'Textfield').find(t => t.path.includes('KoanSpaceCreationView'));
await type(nameField, 'Research', VK.RETURN);
s = await sidebar();
check('Enter creates the Space and shows it',
      s.header?.name === 'Research' && s.header.icon === ROCKET && s.tabs === 1 &&
      !one(s.vs, 'KoanSpaceCreationView'),
      JSON.stringify({ header: s.header && [s.header.icon, s.header.name], tabs: s.tabs }));
check('The bottom bar shows both Spaces, the new one active',
      s.buttons.length === 2 && s.buttons[1].label === ROCKET &&
      s.buttons[1].active && s.buttons[0].label === '' && !s.buttons[0].active,
      JSON.stringify(s.buttons.map(b => [b.label, b.name, b.active])));

// ---- Zen's bottom bar (zen-mac-spec.md 2A): "+" (32 on macOS, 30 elsewhere)
// at the end, 6 in; the Space switcher in the middle: Zen's shrunk buttons
// (half of 34, 32 elsewhere), side by side and centred between the leading
// button and "+"; the current Space shows its icon, the others a dot until
// hovered (B18).
{
  const bar = one(s.vs, 'KoanSpaceBarView');
  const size = s.create?.h;
  const [first, second] = s.buttons.map(b => b.view);
  const half = size === 32 ? 17 : 16;
  check('"+" is at the end of the bottom bar, 6 in',
        [30, 32].includes(size) && s.create.w === size && s.create.x === bar.w - 6 - size,
        JSON.stringify({ bar: bar?.w, create: s.create && [s.create.x, s.create.w, s.create.h] }));
  check('...the Space buttons are Zen\'s shrunk ones, side by side',
        first?.w === half && second?.w === half && first.h === size &&
            second.x === first.x + first.w,
        JSON.stringify(s.buttons.map(b => [b.view.x, b.view.w, b.view.h])));
  check('...centred in the bar',
        Math.abs((first.x + second.x + second.w) - bar.w) <= 1,
        `${first.x}..${second.x + second.w} in ${bar.w}`);
  check('...the current Space shows its icon, the other a dot',
        s.buttons[1].view.props.Collapsed === 'false' &&
            s.buttons[0].view.props.Collapsed === 'true',
        JSON.stringify(s.buttons.map(b => [b.name, b.active, b.view.props.Collapsed])));
  await mouse(first, 'mouseEntered', 5, 10);
  await sleep(500);
  s = await sidebar();
  check('Hovering the other Space shows its icon, and the current one a dot',
        s.buttons[0].view.props.Collapsed === 'false' &&
            s.buttons[1].view.props.Collapsed === 'true',
        JSON.stringify(s.buttons.map(b => [b.name, b.view.props.Collapsed])));
  await mouse(s.buttons[0].view, 'mouseExited', 5, 10);
  await sleep(500);
  s = await sidebar();
  check('...and back when the pointer leaves',
        s.buttons[1].view.props.Collapsed === 'false' &&
            s.buttons[0].view.props.Collapsed === 'true',
        JSON.stringify(s.buttons.map(b => [b.name, b.view.props.Collapsed])));
}

await key(VK.T, CTRL, 't');
s = await sidebar();
check('A new tab opens in the active Space', s.tabs === 2, `${s.tabs} tab(s)`);

// ---- Switching: the bottom bar, keys (wrapping around), the wheel.
await click(s.buttons[0].view);
s = await sidebar();
check('Clicking a Space in the bottom bar shows it',
      s.header?.name === 'Space' && s.tabs === 2 && s.buttons[0].active,
      JSON.stringify({ header: s.header?.name, tabs: s.tabs }));

// Each Space remembers its last active tab: make the first tab active,
// leave and come back.
await click(shown(s.vs, 'TabView')[0]);
const remembered = (await pages()).visible;
await key(VK.RIGHT, CTRL | ALT);
s = await sidebar();
check('Ctrl+Alt+Right goes to the next Space', s.header?.name === 'Research');
await key(VK.RIGHT, CTRL | ALT);
s = await sidebar();
check('...and wraps around to the first', s.header?.name === 'Space');
check('Coming back lands on the Space\'s last active tab',
      (await pages()).visible === remembered, remembered);

// ---- The Space menu (right-click the header).
async function headerMenu() {
  const header = (await sidebar()).header.view;
  await mouse(header, 'mousePressed', 20, 20, 'right');
  await mouse(header, 'mouseReleased', 20, 20, 'right');
  await sleep(1500);
  return menus(await views())[0];
}
let menu = await headerMenu();
// Change Name..., Change Icon..., Theme..., the Spaces, Unload Space,
// Unload All Other Spaces, Create Space, Delete Space.
check('The header menu has Zen\'s entries and the Spaces',
      menu?.items.length === 9, `${menu?.items.length} item(s)`);

// Change Name...: rename in place.
await chooseMenuItem(menu, menu.items[0]);
vs = await views();
const rename = shown(vs, 'Textfield').find(t => t.path.includes('KoanSpaceHeaderView'));
check('Change Name... edits the name in the header', !!rename);
if (rename) {
  await type(rename, 'Home', VK.RETURN);
}
s = await sidebar();
check('...and Enter renames the Space', s.header?.name === 'Home', s.header?.name);
const renamedToast = s.vs.filter(v => v.path.includes('KoanToastView') && v.cls === 'Label')
    .map(v => v.props.Text).join(' ');
check('...with Zen\'s toast', renamedToast === 'Workspace has been successfully renamed!',
      renamedToast);

// Change Icon...: Zen's picker, applied right away.
menu = await headerMenu();
await chooseMenuItem(menu, menu.items[1]);
vs = await views();
const picked = shown(vs, 'KoanEmojiCell')[0];
check('Change Icon... opens the emoji picker', !!picked);
if (picked) {
  await click(picked);
}
s = await sidebar();
check('...and picking sets the Space\'s icon',
      s.header?.icon === picked?.props.Label && s.buttons[0].label === s.header?.icon,
      JSON.stringify([s.header?.icon, picked?.props.Label]));
await key(VK.ESCAPE);

// The keyboard path: the header takes focus; F2 renames, Enter opens the
// menu.
s = await sidebar();
check('The header is a focusable button with a menu',
      s.header?.view.props.FocusBehavior === 'ALWAYS',
      s.header?.view.props.FocusBehavior);
await viewKey(s.header.view, VK.F2);
let field = shown(await views(), 'Textfield').find(t => t.path.includes('KoanSpaceHeaderView'));
check('F2 on the header renames the Space', !!field);
if (field) {
  await type(field, '', VK.ESCAPE);
}
s = await sidebar();
check('...and Escape keeps the name', s.header?.name === 'Home' &&
      !shown(s.vs, 'Textfield').some(t => t.path.includes('KoanSpaceHeaderView')),
      s.header?.name);
await viewKey(s.header.view, VK.RETURN);
menu = menus(await views())[0];
check('Enter on the header opens the Space menu', menu?.items.length === 9,
      `${menu?.items.length} item(s)`);
if (menu) {
  await chooseMenuItem(menu, menu.items[0]);  // Change Name..., to close it
  field = shown(await views(), 'Textfield').find(t => t.path.includes('KoanSpaceHeaderView'));
  if (field) {
    await type(field, '', VK.ESCAPE);
  }
}

// Unload Space frees the Space's other tabs; the active one stays.
const before = await pages();
menu = await headerMenu();
await chooseMenuItem(menu, menu.items[5]);
await sleep(1500);
const after = await pages();
check('Unload Space discards the Space\'s background tabs',
      after.live.length === before.live.length - 1 && after.visible === before.visible,
      `${before.live.length} -> ${after.live.length} live page(s)`);

// ---- A Space whose last tab closes comes back with a new tab. (Zen puts a
// new Space after the active one: Home, Scratch, Research.)
await key(VK.N, CTRL | ALT, 'n');
vs = await views();
await type(shown(vs, 'Textfield').find(t => t.path.includes('KoanSpaceCreationView')),
           'Scratch', VK.RETURN);
s = await sidebar();
check('A third Space, after the active one',
      s.buttons.map(b => b.name).join() === 'Home,Scratch,Research' &&
      s.header?.name === 'Scratch', s.buttons.map(b => b.name).join());
await key(VK.W, CTRL, 'w');
await sleep(1000);
s = await sidebar();
check('Closing its last tab keeps the Space, with a new tab',
      s.buttons.length === 3 && s.header?.name === 'Scratch' && s.tabs === 1,
      JSON.stringify({ spaces: s.buttons.length, header: s.header?.name, tabs: s.tabs }));

// ---- The Create form from a collapsed sidebar: the sidebar expands over
// the page while the form is open.
// The sidebar toggle, found again each time: collapsing moves the toolbar.
const toggle = async () => (await views()).find(v => v.shown && v.w > 0 &&
    v.path.at(-1) === 'VerticalTabStripTopContainer' && v.cls !== 'KoanSidebarNavButtons');
await click(await toggle());
await sleep(1000);
await key(VK.N, CTRL | ALT, 'n');
s = await sidebar();
form = one(s.vs, 'KoanSpaceCreationView');
check('Collapsed, Ctrl+Alt+N shows the Create form (the sidebar expands)',
      !!form && form.h > 100 && form.w >= 200, JSON.stringify(form && [form.w, form.h]));
const cancel = shown(s.vs, 'MdTextButton')
    .find(b => b.path.includes('KoanSpaceCreationView') && b.props.Text === 'Cancel');
await click(cancel);
await sleep(1000);
s = await sidebar();
check('...and Cancel goes back to the collapsed sidebar with the tabs',
      !one(s.vs, 'KoanSpaceCreationView') && s.tabs === 1 &&
      (one(s.vs, 'KoanSpaceBarView')?.w ?? 999) < 150,
      JSON.stringify({ tabs: s.tabs, bar: one(s.vs, 'KoanSpaceBarView')?.w }));
await click(await toggle());
await sleep(1000);
s = await sidebar();
check('...and the sidebar expands again', !!s.header, JSON.stringify(s.header?.name));

// ---- Move To (tab context menu) moves the tab and shows its Space.
await click(s.buttons[0].view);
s = await sidebar();
// The Space's first tab, made active (Ctrl+1) so that it is the one the
// Space remembers. (A synthetic click on a tab would leave its drag
// handling pressed, and the context menu wouldn't open.)
await key(49, CTRL, '1');
s = await sidebar();
const tab = shown(s.vs, 'TabView')[0];
viewEvent(tab, 'DOM.dispatchMouseEvent',
          { type: 'mousePressed', x: 10, y: 10, button: 'right', wheelDirection: 'none' }, false);
viewEvent(tab, 'DOM.dispatchMouseEvent',
          { type: 'mouseReleased', x: 10, y: 10, button: 'right', wheelDirection: 'none' }, false);
await sleep(1500);
const tabMenu = menus(await views())[0];
let moveTo = null;
// Hovering an item opens its submenu: Move To is the one listing the two
// other Spaces.
for (const item of tabMenu?.items ?? []) {
  await menuMouse(tabMenu, item, ['mouseMoved']);
  await sleep(700);
  moveTo = menus(await views()).find(m => m.root !== tabMenu.root && m.items.length === 2);
  if (moveTo) break;
}
check('The tab context menu has Move To with the other Spaces', !!moveTo);
if (moveTo) {
  await chooseMenuItem(moveTo, moveTo.items[1]);  // Research
}
s = await sidebar();
check('Move To moves the tab and shows its new Space',
      s.header?.name === 'Research' && s.tabs === 3,
      JSON.stringify({ header: s.header?.name, tabs: s.tabs }));
// The Space it left no longer has the tab it remembered.
await click(s.buttons[0].view);
s = await sidebar();
check('...and the Space it left still shows, on one of its own tabs',
      s.header?.name === 'Home' && s.tabs >= 1,
      JSON.stringify({ header: s.header?.name, tabs: s.tabs }));
await click(s.buttons[2].view);

// ---- Delete Space asks first, then shows the Space before it.
menu = await headerMenu();
await chooseMenuItem(menu, menu.items.at(-1));
vs = await views();
const yes = shown(vs, 'MdTextButton').find(b => b.props.Text === 'Yes') ??
            shown(vs, 'LabelButton').find(b => b.props.Text === 'Yes');
check('Delete Space asks for confirmation', !!yes);
if (yes) {
  await click(yes);
}
s = await sidebar();
check('...and deletes the Space, showing another',
      s.buttons.map(b => b.name).join() === 'Home,Scratch' && s.header?.name === 'Scratch',
      JSON.stringify({ spaces: s.buttons.map(b => b.name), header: s.header?.name }));

// ---- The form's buttons take real clicks. The events above go straight to
// a view; these are sent to the window's root view, which routes them by
// position like the pointer does, so a view over the buttons (or a caption
// region) would swallow them.
async function rootClick(view) {
  const vs = await views();
  const root = vs.find(v => v.cls === 'BrowserRootView' && v.widget === view.widget);
  const x = Math.round(view.ax + view.w / 2);
  const y = Math.round(view.ay + view.h / 2);
  await mouse(root, 'mouseMoved', x, y);
  await mouse(root, 'mousePressed', x, y);
  await mouse(root, 'mouseReleased', x, y);
  await sleep(1200);
}
await key(VK.N, CTRL | ALT, 'n');
s = await sidebar();
await type(shown(s.vs, 'Textfield').find(t => t.path.includes('KoanSpaceCreationView')), 'Clicked');
s = await sidebar();
const formButton = text => shown(s.vs, 'MdTextButton')
    .find(b => b.path.includes('KoanSpaceCreationView') && b.props.Text === text);
check('The form is open with Create Space enabled once it has a name',
      formButton('Create Space')?.props.Enabled === 'true');
await rootClick(formButton('Create Space'));
s = await sidebar();
check('Clicking Create Space creates the Space and shows it',
      s.header?.name === 'Clicked' && !one(s.vs, 'KoanSpaceCreationView'),
      JSON.stringify({ header: s.header?.name, form: !!one(s.vs, 'KoanSpaceCreationView') }));
await key(VK.N, CTRL | ALT, 'n');
s = await sidebar();
await rootClick(formButton('Cancel'));
s = await sidebar();
check('Clicking Cancel closes the form', !one(s.vs, 'KoanSpaceCreationView'));
// ---- Spaces lockdown (.agent/notes/upstream/tab-groups-omnibox-fullscreen.md
// section 1.3): upstream's tab group side effects and entry points stay out
// of a Spaces window. The Spaces here: Home, Scratch, Clicked (shown). Chrome's group keys are Shift+Alt+P/W/X/Z/C here
// (Cmd+Ctrl on a Mac). A DCHECK build also runs Koan's leak detector (G14)
// after every update of the steps below.
const KEY = { C: 67, P: 80, W: 87, X: 88, Z: 90 };
const noEditor = vs => shown(vs, 'TabGroupEditorBubbleView').length === 0;
const userColor = await readPref('browser.theme.user_color2');

// G1: a Space with the default theme leaves the window its user colour; the
// profile's colour is never the Space's.
check('G1: a default-theme Space gives the window no colour of its own',
      (await windowTints()).every(t => !t), JSON.stringify(await windowTints()));

// G4: "New tab group" opens Zen's Create-a-Space form, with no group or
// editor.
await key(KEY.P, SHIFT | ALT, 'P');
s = await sidebar();
check('G4: New tab group opens the Create-a-Space form, not a group',
      !!one(s.vs, 'KoanSpaceCreationView') && noEditor(s.vs) &&
      shown(s.vs, 'KoanSpaceButton').length === 3,
      JSON.stringify({ form: !!one(s.vs, 'KoanSpaceCreationView'),
                       spaces: s.buttons.length }));
const formCancel = shown(s.vs, 'MdTextButton')
    .find(b => b.path.includes('KoanSpaceCreationView') && b.props.Text === 'Cancel');
if (formCancel) {
  await click(formCancel);
}
// G4: next / previous group are the next / previous Space, wrapping.
await key(KEY.X, SHIFT | ALT, 'X');
s = await sidebar();
check('G4: Focus next group goes to the next Space (wrapping around)',
      s.header?.name === 'Home', s.header?.name);
await key(KEY.Z, SHIFT | ALT, 'Z');
s = await sidebar();
check('G4: Focus previous group goes back', s.header?.name === 'Clicked',
      s.header?.name);
// G4: "Close group" asks to delete the Space (Zen's question); No keeps it.
await key(KEY.W, SHIFT | ALT, 'W');
vs = await views();
const no = shown(vs, 'MdTextButton').find(b => b.props.Text === 'No') ??
           shown(vs, 'LabelButton').find(b => b.props.Text === 'No');
check('G4: Close group asks before deleting the Space', !!no);
if (no) {
  await click(no);
}
s = await sidebar();
check('...and No keeps it, with its tabs', s.buttons.length === 3 &&
      s.header?.name === 'Clicked' && s.tabs >= 1,
      JSON.stringify({ spaces: s.buttons.map(b => b.name), tabs: s.tabs }));
// G4: "New tab in group" is Koan's new tab (the URL bar), which opens no tab
// by itself.
const tabsBefore = s.tabs;
await key(KEY.C, SHIFT | ALT, 'C');
await key(VK.ESCAPE);
s = await sidebar();
check('G4: New tab in group opens Koan\'s new tab, not an NTP tab',
      s.tabs === tabsBefore && noEditor(s.vs), `${tabsBefore} -> ${s.tabs}`);

// G2: a tab in another Space keeps running (no focus-mode freezing vote;
// Koan's background-tab policy keeps the 5 most recently used running).
await click(s.buttons[0].view);  // Home
const ticker = (await browserCall('Target.createTarget', {
  url: 'data:text/html,<title>ticker</title><script>n=0;setInterval(()=>n++,100)</script>',
})).result.targetId;
const tickerPage = await waitFor(async () => (await pageTargets()).find(p => p.id === ticker));
await sleep(1500);
s = await sidebar();
await click(s.buttons[1].view);  // Scratch, away from Home
await sleep(3000);
const n1 = await evalIn(tickerPage, 'n');
await sleep(1500);
const n2 = await evalIn(tickerPage, 'n');
check('G2: a tab in another Space isn\'t frozen by switching Space',
      typeof n1 === 'number' && n2 > n1, `${n1} -> ${n2}`);

// G1: the profile's colour stays the user's (two windows on differently
// coloured Spaces: spaces_switching.mjs).
check('G1: no Space colour reached the profile',
      await readPref('browser.theme.user_color2') === userColor);

finish();
