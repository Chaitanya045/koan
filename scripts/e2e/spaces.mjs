// End-to-end test of Zen's Spaces on a real build: the window always shows
// one Space, the Space header (icon and name, its menu: Change Name...,
// Change Icon..., Unload, Delete with confirmation), the bottom bar (one
// button per Space, "+" with the Create menu), the Create-a-Space form with
// the emoji picker, switching (bottom bar, keys with wrap-around, the
// mouse wheel), each Space's last active tab, "Move To" in the tab context
// menu and a Space whose last tab closes coming back with a new tab.
//
//   scripts/e2e/run.sh spaces
import {
  ALT, CTRL, check, evalIn, finish, key as pageKey, menuMouse, openMenus,
  pages as pageTargets, sleep, typeInto, viewEvent, viewMouse, viewTree,
  visiblePage,
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

async function wheel(view, direction) {
  await mouse(view, 'mouseWheel', 10, 10, 'none', direction);
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
  const buttons = shown(vs, 'KoanSpaceButton').map(b => ({
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

const VK = { N: 78, T: 84, W: 87, LEFT: 37, RIGHT: 39, RETURN: 13, ESCAPE: 27 };
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
await wheel(s.header.view, 'right');
s = await sidebar();
check('A horizontal mouse wheel over the sidebar switches Spaces',
      s.header?.name === 'Research', s.header?.name);
await wheel(s.header.view, 'left');
s = await sidebar();
check('...both ways', s.header?.name === 'Space', s.header?.name);

// ---- The Space menu (right-click the header).
async function headerMenu() {
  const header = (await sidebar()).header.view;
  await mouse(header, 'mousePressed', 20, 20, 'right');
  await mouse(header, 'mouseReleased', 20, 20, 'right');
  await sleep(1500);
  return menus(await views())[0];
}
let menu = await headerMenu();
// Change Name..., Change Icon..., Edit Theme..., the Spaces, Unload Space,
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

// ---- Move To (tab context menu) moves the tab and shows its Space.
await click(s.buttons[0].view);
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

finish();
