// End-to-end test of switching Spaces against a real build, with pages that
// count their loads: a real mouse wheel and a swipe over the sidebar (events
// through the window, so upstream's swipe controller and Koan's wheel
// handler see them), wrapping around past the last Space, swiping past
// unloaded Spaces without loading their other tabs, switching to a tab of
// another Space (it stays the tab shown), a restart (the Spaces, their
// names, icons, theme and order come back, no extra Space appears, a
// selected Essential stays selected, and swiping through them loads only the
// tab each one lands on), and Move To into an unloaded Space loading only
// the moved tab.
//
//   scripts/e2e/run.sh spaces_switching
import { spawn } from 'node:child_process';
import { servePages } from '../perf/pages_server.mjs';
import {
  CDP_PORT, CTRL, ALT, UI_PORT, browserCall, check, connect, createSpace, evalIn, finish,
  key, menuMouse, onPage, openMenus, pages, readPref, SHIFT, sleep, viewEvent,
  viewMouse, viewTree, visiblePage, waitFor,
} from './lib/koan.mjs';

const site = await servePages();
const url = id => `${site.origin}/power.html?count=${id}`;
const byId = id => (p => p.url.endsWith(`count=${id}`));
const find = async id => (await pages()).find(byId(id));

// Each page counts its loads in localStorage["loads:<id>"]; one origin, so
// any loaded page reads them all. Scripts only run in pages known to be
// loaded, so that no check depends on what DevTools does with an unloaded
// tab.
async function loads(reader) {
  const json = await evalIn(await find(reader), 'JSON.stringify({...localStorage})');
  return Object.fromEntries(Object.entries(JSON.parse(json ?? '{}'))
      .map(([k, v]) => [k.replace('loads:', ''), Number(v)]));
}
// Whether page `id` (expected to be loaded) is the one on screen.
const isShown = async id =>
  await evalIn(await find(id), 'document.visibilityState') === 'visible';
const show = n => Object.entries(n).sort().map(([k, v]) => `${k}: ${v}x`).join(', ');
const delta = (after, before) => Object.fromEntries(
    Object.entries(after).map(([k, v]) => [k, v - (before[k] ?? 0)]));

async function navigate(page, to) {
  await onPage(page, c => c.send('Page.navigate', { url: to }));
  await waitFor(async () => (await pages()).find(p => p.id === page.id)?.url === to);
  await sleep(500);
}

// The sidebar: Space buttons (name, icon, active), the header.
async function sidebar() {
  const vs = await viewTree();
  // In their order on screen (UI DevTools doesn't follow reordered views).
  const buttons = vs.filter(v => v.cls === 'KoanSpaceButton' && v.shown)
      .sort((a, b) => a.ax - b.ax).map(b => ({
    name: b.props.TooltipText ?? '', icon: b.props.Label ?? '',
    active: b.props.Active === 'true', view: b }));
  const header = vs.find(v => v.cls === 'KoanSpaceHeaderView' && v.shown && v.h > 0);
  const root = header && vs.find(v => v.widget === header.widget && v.path.length === 0);
  return { vs, buttons, header, root, name: header?.props.Name ?? '',
           icon: header?.props.Icon ?? '' };
}

async function click(view, button = 'left') {
  await viewMouse(view, 'mousePressed', 10, 10, button);
  await viewMouse(view, 'mouseReleased', 10, 10, button);
  await sleep(1200);
}

// Mouse wheel notches over the sidebar, sent to the window's root view so
// they go through the event pipeline (pre-target handlers included), in one
// burst: one gesture for upstream's swipe controller.
async function wheel(direction, notches = 1) {
  const { root, header } = await sidebar();
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('DOM.getDocument', { depth: -1 });
  for (let i = 0; i < notches; i++) {
    await ui.send('DOM.dispatchMouseEvent', { nodeId: root.nodeId, event: {
      type: 'mouseWheel', x: header.ax + 30, y: header.ay + 10, button: 'none',
      wheelDirection: direction } });
  }
  ui.close();
  // Past upstream's gesture timeout (300 ms) and Zen's slide.
  await sleep(1500);
}

async function headerMenu() {
  const { header } = await sidebar();
  await viewMouse(header, 'mousePressed', 20, 20, 'right');
  await viewMouse(header, 'mouseReleased', 20, 20, 'right');
  await sleep(1500);
  return openMenus(await viewTree())[0];
}

// ---- Three Spaces: "Space" (a1, a2), B (b1, b2) and C (c1, c2), each on
// its second tab.
await navigate(await visiblePage(), url('a1'));
await browserCall('Target.createTarget', { url: url('a2'), background: true });
await sleep(1500);
for (const name of ['B', 'C']) {
  await createSpace(await visiblePage(), name);
  const id = name.toLowerCase();
  await navigate(await visiblePage(), url(`${id}1`));
  await browserCall('Target.createTarget', { url: url(`${id}2`) });
  await sleep(1500);
}
let s = await sidebar();
check('Three Spaces, each on its second tab',
      s.buttons.map(b => b.name).join() === 'Space,B,C' && s.name === 'C' &&
      await isShown('c2'),
      `${s.buttons.map(b => b.name).join()} / ${s.name}`);

// ---- Back to the first Space; unload the others.
await click(s.buttons[0].view);
s = await sidebar();
check('The first Space shows its last active tab', s.name === 'Space' &&
      await isShown('a1'));
let menu = await headerMenu();
// ... Unload Space, Unload All Other Spaces, Create Space, Delete Space.
await menuMouse(menu, menu.items.at(-3));
await sleep(2000);
let before = await loads('a1');

// ---- A real wheel over the sidebar switches Space (upstream's swipe on
// Linux), landing on each Space's last active tab without loading its
// first one.
await wheel('right');
s = await sidebar();
check('A wheel notch over the sidebar goes to the next Space', s.name === 'B', s.name);
check('...on its last active tab', await isShown('b2'));
await wheel('right', 3);
s = await sidebar();
check('A swipe (a burst of wheel events) moves one Space', s.name === 'C', s.name);
let n = delta(await loads('a1'), before);
check('Swiping past an unloaded Space loads only the tab it lands on',
      n.b2 === 1 && n.c2 === 1 && !n.b1 && !n.c1 && !n.a1 && !n.a2, show(n));

await wheel('right', 3);
s = await sidebar();
check('Swiping past the last Space wraps around to the first', s.name === 'Space', s.name);
await wheel('left');
s = await sidebar();
check('...and back past the first to the last', s.name === 'C', s.name);

// ---- Switching to a tab of another Space (as the omnibox's "Switch to this
// tab", tab search or an extension do) shows that tab, not the Space's
// last active one.
await browserCall('Target.activateTarget', { targetId: (await find('a2')).id });
await sleep(1500);
s = await sidebar();
check('Activating a tab of another Space shows that Space',
      s.name === 'Space' && s.buttons[0].active, s.name);
check('...on that tab, not the Space\'s last active one', await isShown('a2'));

// ---- B gets an icon and a colour, then the browser restarts in "Space".
await click(s.buttons[1].view);
menu = await headerMenu();
await menuMouse(menu, menu.items[1]);  // Change Icon...
const emoji = (await viewTree()).find(v => v.cls === 'KoanEmojiCell' && v.shown);
await click(emoji);
await key(await find('b2'), 27, 0, 'Escape');  // close the picker
await sleep(1000);
menu = await headerMenu();
await menuMouse(menu, menu.items[2]);  // Edit Theme...
[menu] = openMenus(await viewTree());
await menuMouse(menu, menu.items[1]);  // the first colour
const seed = await waitFor(() => readPref('koan.workspaces.tint_applied'));
s = await sidebar();
const icon = s.icon;
check('B has an icon and a colour', icon === emoji?.props.Label && !!seed,
      `${icon} / ${seed}`);
// Back with Ctrl+Alt+Left. (Right after the theme menu, with the emoji
// picker still open, a synthetic click on the bar can be dropped.)
await key(await find('b2'), 37, CTRL | ALT, 'ArrowLeft');
await sleep(1500);
s = await sidebar();
check('Back in the first Space, on its last active tab',
      s.name === 'Space' && await isShown('a2'), s.name);

// An Essential, selected when the browser closes: e1, pinned (Ctrl+Shift+D).
await browserCall('Target.createTarget', { url: url('e1') });
await sleep(1500);
await key(await find('e1'), 68, CTRL | SHIFT, 'D');
await sleep(1000);
check('An Essential is selected', await isShown('e1') &&
      (await viewTree()).some(v => v.cls === 'TabView' && v.shown &&
                                   v.path.some(p => /Pinned/.test(p))));

const v = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
const browser = await connect(v.webSocketDebuggerUrl);
await browser.send('Browser.close');
browser.close();
for (let i = 0; i < 40; i++) {
  await sleep(500);
  if (!await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).catch(() => null)) break;
}
await sleep(2000);
const args = process.env.KOAN_CHROME_ARGS.split('\n').filter(Boolean);
const relaunched = spawn(args[0], args.slice(1), { stdio: 'ignore' });
// Never leave it running (it would hold the test ports), even on a crash.
process.on('exit', () => relaunched.kill());
for (let i = 0; i < 60; i++) {
  await sleep(500);
  if (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).catch(() => null)) break;
}
await sleep(5000);

// ---- After the restart.
before = await loads('e1');
s = await sidebar();
check('After a restart the Spaces are back, in order, and no other',
      s.buttons.map(b => b.name).join() === 'Space,B,C',
      s.buttons.map(b => b.name).join());
check('...with their icons', s.buttons[1]?.icon === icon,
      s.buttons.map(b => b.icon).join(' '));
check('...in the Space that was shown, with the Essential still selected',
      s.name === 'Space' && await isShown('e1'), s.name);
await wheel('right');
s = await sidebar();
check('Swiping after a restart lands on the Space\'s last active tab',
      s.name === 'B' && await isShown('b2'), s.name);
check('...and B keeps its colour',
      await waitFor(async () => await readPref('koan.workspaces.tint_applied') === seed),
      `${await readPref('koan.workspaces.tint_applied')} vs ${seed}`);
await wheel('right');
s = await sidebar();
n = delta(await loads('e1'), before);
check('Swiping through restored Spaces loads only the tabs they land on',
      s.name === 'C' && n.b2 === 1 && n.c2 === 1 && !n.a1 && !n.a2 && !n.b1 && !n.c1,
      `${s.name}; ${show(n)}`);

// ---- Move To into an unloaded Space loads only the moved tab: in C, unload
// the others, then move c2 to B.
menu = await headerMenu();
await menuMouse(menu, menu.items.at(-3));  // Unload All Other Spaces
await sleep(2000);
before = await loads('c2');
const spaceTabs = (await viewTree())
    .filter(v => v.cls === 'TabView' && v.shown && v.h > 1 &&
                 !v.path.some(p => /Pinned/.test(p)))
    .sort((a, b) => a.ay - b.ay);
const moved = spaceTabs.at(-1);  // c2, the newest
for (const type of ['mousePressed', 'mouseReleased']) {
  viewEvent(moved, 'DOM.dispatchMouseEvent',
            { type, x: 10, y: 10, button: 'right', wheelDirection: 'none' }, false);
}
await sleep(1500);
const tabMenu = openMenus(await viewTree())[0];
let moveTo = null;
// Move To's submenu lists the two other Spaces.
for (const item of tabMenu?.items ?? []) {
  await menuMouse(tabMenu, item, ['mouseMoved']);
  await sleep(700);
  moveTo = openMenus(await viewTree())
      .find(m => m.root !== tabMenu.root && m.items.length === 2);
  if (moveTo) break;
}
if (moveTo) {
  await menuMouse(moveTo, moveTo.items[1]);  // B
}
await sleep(1500);
s = await sidebar();
n = delta(await loads('c2'), before);
check('Move To into an unloaded Space shows it on the moved tab',
      !!moveTo && s.name === 'B' && await isShown('c2'), s.name);
check('...and loads none of its other tabs', !n.b1 && !n.b2 && !n.c2, show(n));

relaunched.kill();
site.close();
finish();
