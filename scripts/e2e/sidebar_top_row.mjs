// End-to-end test of the sidebar's top row and URL pill with real clicks
// (single toolbar, Zen's layout). The back / forward / reload buttons were
// window caption: on macOS a real click dragged the window and never reached
// them, while UI DevTools' clicks (which skip the window's hit test) passed.
// So the checks that matter use the real pointer (the bench's ptr, a CGEvent
// through AppKit like a person's click):
// - back, forward and reload each navigate, and the window doesn't move;
// - the pinned extension's button (scripts/e2e/extensions/action_popup,
//   pinned by prefs/sidebar_top_row.json) shows on the sidebar's hover and
//   opens its popup;
// - Copy URL copies the page's address and shows Zen's toast;
// - the Extensions button opens Chromium's extensions menu, and closes it.
// Without real input the same buttons are pressed through UI DevTools (the
// views' side only) and the AppKit checks are reported as skipped.
// Run by scripts/mac/suite.sh (sidebar_top_row.flags loads the extension).
import { execSync } from 'node:child_process';
import { centerOf, half, CDP_PORT, IS_MAC, check, evalIn, finish, onPage, pages, sleep, toastTexts,
         viewMouse, viewTree, waitFor } from './lib/koan.mjs';
import { alive, calibrate, fatalInLog, makeKey, ptr, realClick, realInput,
         realInputWhyNot, realKey, skipped, startServer, windowOf } from './lib/mac.mjs';

const EXTENSION_ID = 'fpfndddeinnmcgdmdlibaobomgbdjljd';

const server = await startServer();
const A = server.url('/p?t=Alpha'), B = server.url('/p?t=Beta');
const page = (await pages())[0];
check('A page to start from', !!page);
if (!page) finish();
const href = () => evalIn(page, 'location.href');
const go = async url => {
  await onPage(page, c => c.send('Page.navigate', { url }));
  return waitFor(async () => (await href()) === url, 8000);
};
check('Loads page A, then page B (so Back has somewhere to go)', !!(await go(A)) && !!(await go(B)));
await sleep(800);

// The row's own buttons; on Linux the row starts with Zen's app menu
// button (KoanAppMenuButton, #488), checked on its own below.
const inRow = vs => vs.filter(v => v.shown && v.path.at(-1) === 'KoanSidebarNavButtons')
    .sort((a, b) => a.ax - b.ax);
const navButtons = vs => inRow(vs).filter(v => v.cls !== 'KoanAppMenuButton');
const pillButton = (vs, tip) => vs.find(v => v.shown && v.path.includes('KoanUrlPill') &&
    v.props?.TooltipText === tip);
const extensionButton = vs => vs.find(v => v.shown && v.path.at(-1) === 'KoanSidebarExtensionsView');
const bounds = async () => (await windowOf(page.id).catch(() => null))?.bounds;
const sameBounds = (a, b) => a && b && a.left === b.left && a.top === b.top &&
    a.width === b.width && a.height === b.height;

let vs = await viewTree();
const row = navButtons(vs);
check('The top row has back, forward and reload', row.length === 3,
      row.map(v => v.props?.TooltipText).join(', '));
if (row.length !== 3) finish();
{
  const all = inRow(vs);
  const menu = all.filter(v => v.cls === 'KoanAppMenuButton');
  if (IS_MAC) {
    check('No app menu button in the row (the menu bar has its items)', menu.length === 0);
  } else {
    check('The row starts with the app menu button, before back',
          menu.length === 1 && all[0] === menu[0] &&
              menu[0].props?.TooltipText === 'Open application menu',
          all.map(v => `${v.cls}:${v.props?.TooltipText}`).join(', '));
  }
}

// The page's tab in front (the buttons act on the shown tab: the suite's
// profile starts on another, so UI DevTools' Back stayed on it and Copy URL
// copied its address), its window key, and with real input where its root
// view is on the screen (the real pointer over the page, see lib/mac.mjs
// calibrate()).
await makeKey(page);
let origin = null;
if (realInput()) {
  origin = await calibrate(page, vs);
  check('Real pointer: calibrated on the page', !!origin);
}
const real = () => !!origin && realInput();
const via = real() ? 'a real click' : 'UI DevTools';
const centre = v => ({ x: origin.x + centerOf(v).x, y: origin.y + centerOf(v).y });
// A click on `view` (from viewTree()): the real pointer at its centre, or
// UI DevTools' press and release on it.
async function press(view, settle = 900) {
  if (real()) {
    const c = centre(view);
    return realClick(c.x, c.y, settle);
  }
  for (const type of ['mousePressed', 'mouseReleased']) {
    await viewMouse(view, type, half(view.w), half(view.h));
  }
  await sleep(settle);
  return true;
}
// The real pointer over `view` (Zen's buttons show on the sidebar's hover).
async function hover(view) {
  if (!real()) return false;
  const c = centre(view);
  await ptr('move', Math.round(c.x), Math.round(c.y), 150);
  await sleep(500);
  return true;
}

// Back, forward, reload.
const [back] = row;
{
  const w0 = await bounds();
  await press(back);
  check(`Back (${via}) goes back to page A`, !!(await waitFor(async () => (await href()) === A, 5000)),
        await href());
  await press(navButtons(await viewTree())[1]);
  check(`Forward (${via}) goes forward to page B`,
        !!(await waitFor(async () => (await href()) === B, 5000)), await href());
  await evalIn(page, 'window.__koanMark = 1');
  await press(navButtons(await viewTree())[2]);
  check(`Reload (${via}) reloads page B`, !!(await waitFor(
      async () => (await evalIn(page, 'window.__koanMark').catch(() => 1)) === undefined, 5000)) &&
      (await href()) === B);
  const w1 = await bounds();
  if (real()) {
    check('...and the window did not move (the buttons are client area, not caption)',
          sameBounds(w0, w1), `${JSON.stringify(w0)} -> ${JSON.stringify(w1)}`);
  } else {
    skipped('the top row through the window\'s hit test', realInputWhyNot() ||
            'no real input: UI DevTools clicks skip the hit test');
  }
}

// The pinned extension: its button shows on the sidebar's hover and opens
// its popup.
vs = await viewTree();
const pill = vs.find(v => v.cls === 'KoanUrlPill' && v.shown);
check('The URL pill is there', !!pill);
if (real() && pill) {
  await hover(pill);
  const ext = await waitFor(async () => extensionButton(await viewTree()), 3000);
  check('The pinned extension\'s button shows while the sidebar is hovered', !!ext);
  if (ext) {
    await press(ext, 1200);
    const popup = await waitFor(async () => (await (await fetch(
        `http://127.0.0.1:${CDP_PORT}/json/list`)).json()).find(
        t => t.url === `chrome-extension://${EXTENSION_ID}/popup.html`), 5000);
    check(`A click (${via}) on it opens the extension's popup`, !!popup);
    await realKey('Escape', '', 600);
  }
} else {
  skipped('the pinned extension\'s button and popup',
          realInputWhyNot() || 'it shows on the sidebar\'s hover, which needs the real pointer');
}

// Copy URL: the page's address on the clipboard, and Zen's toast.
vs = await viewTree();
const copy = pillButton(vs, 'Copy URL');
check('The pill has Copy URL', !!copy);
if (copy) {
  if (IS_MAC) execSync('pbcopy', { input: '' });
  if (real()) await hover(copy);
  await press(copy, 300);
  // (toastTexts(): a whole viewTree() can take longer than the toast stays.)
  const toast = await waitFor(async () => (await toastTexts()).find(t => /^Copied /.test(t)),
                              2500) ?? '';
  check(`Copy URL (${via}) shows Zen's toast`, toast === 'Copied current URL!', JSON.stringify(toast));
  if (IS_MAC) {
    const text = execSync('pbpaste', { encoding: 'utf8' });
    check('...and copies the page\'s address', text === B, JSON.stringify(text));
  } else {
    skipped('the clipboard', 'read with pbpaste on macOS');
  }
}

// The Extensions button: Chromium's extensions menu, then closed again.
const menuShown = async () => (await viewTree()).some(v => v.shown &&
    /^ExtensionsMenu(MainPage)?View$/.test(v.cls));
vs = await viewTree();
const extensions = pillButton(vs, 'Extensions');
check('The pill has the Extensions button', !!extensions);
if (extensions) {
  const w0 = await bounds();
  if (real()) await hover(extensions);
  await press(extensions);
  check(`Extensions (${via}) opens the extensions menu`, !!(await waitFor(menuShown, 4000)));
  // Again, at the same place: the menu closes (it may have moved the pill's
  // buttons, so look again).
  const again = pillButton(await viewTree(), 'Extensions') ?? extensions;
  await press(again);
  check('...and a second click closes it', !!(await waitFor(async () => !(await menuShown()), 4000)));
  if (real()) {
    check('...and the window did not move', sameBounds(w0, await bounds()));
  }
}

check('Koan is alive', await alive());
check('Nothing fatal in the log', !fatalInLog(), fatalInLog());
server.close();
finish();
