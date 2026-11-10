// End-to-end test that dragging (or pressing keys at) the sidebar's edge never
// leaves Chromium's icon rail (audit tests 17, 18, 19; #154):
// - a drag from expanded below the snap width turns compact mode on, once,
//   and never shows a 56 wide strip, also with the tabs on the right;
// - with compact mode on and the sidebar revealed (Ctrl+Alt+S), the same drag
//   leaves compact mode on and the strip as it was;
// - the keyboard: Left on the focused resize area many times, the same as a
//   drag; Right many times widens to the 500 maximum.
//
//   scripts/e2e/run.sh vertical_tabs_drag
import { ALT, CTRL, check, finish, key, mouseOn, readPref, setKoanPref, sidebarMenu, sleep, views,
         visiblePage } from './lib/koan.mjs';
import { VK, alive, checkKoanSidebar, leakLogged, press, railShown, region }
    from './lib/vertical_tabs.mjs';

const resizeArea = vs => vs.find(v => v.cls === 'ResizeArea' && v.shown &&
                                      v.path.includes('VerticalTabStripRegionView'));

// Drags the edge `dx` (negative: toward the window's outer edge on the left)
// with `widths` sampled on the way; releases at the end.
async function drag(dx, { release = true } = {}) {
  const resize = resizeArea(await views());
  await mouseOn(resize, 'mousePressed', 2, 300);
  const seen = [];
  for (const step of [0.25, 0.5, 0.75, 1]) {
    await mouseOn(resize, 'mouseDragged', 2 + dx * step, 300);
    await sleep(150);
    seen.push(region(await views())?.w);
  }
  if (release) {
    await mouseOn(resize, 'mouseReleased', 2 + dx, 300);
    await sleep(1500);
  }
  return seen;
}

// 17: expanded -> dragged below the snap width -> compact mode, no rail.
let vs = await views();
checkKoanSidebar('Start', {}, vs);
let seen = await drag(-260);
check('Mid-drag the strip never becomes the 56 wide rail',
      seen.every(w => w >= 100), seen.join(', '));
vs = await views();
checkKoanSidebar('After the drag', { hidden: true }, vs);
check('...compact mode is on', await readPref('koan.compact_mode.enabled') === true);

// 18: compact on, sidebar revealed (peeked): the same drag snaps back.
await press(VK.S, CTRL | ALT, 's', 900);
vs = await views();
check('The peeked sidebar is out', region(vs)?.w >= 200, `${region(vs)?.w}`);
seen = await drag(-260);
vs = await views();
check('Dragging the revealed sidebar leaves no rail', !railShown(vs) &&
      seen.every(w => w === 0 || w >= 100), seen.join(', '));
check('...and compact mode stays on', await readPref('koan.compact_mode.enabled') === true);
await press(VK.S, CTRL, 's', 1500);
checkKoanSidebar('Compact off', {}, await views());

// 17 with the tabs on the right: the edge is on the other side.
await setKoanPref('koan.tabs.on_right', true);
await sleep(1500);
seen = await drag(260);
check('Tabs on the right: the drag never shows the rail', seen.every(w => w >= 100), seen.join(', '));
checkKoanSidebar('Tabs on the right, after the drag', { hidden: true }, await views());
await press(VK.S, CTRL, 's', 1500);
await setKoanPref('koan.tabs.on_right', false);
await sleep(1500);

// 19: the keyboard. Focus the resize area (it is a focusable view) and press
// Left until the width would be below the snap width.
const focus = async () => {
  const resize = resizeArea(await views());
  await mouseOn(resize, 'mousePressed', 2, 300);
  await mouseOn(resize, 'mouseReleased', 2, 300);
};
await focus();
const page = await visiblePage();
for (let i = 0; i < 8; i++) {
  await key(page, 37, 0, 'ArrowLeft');
}
await sleep(1500);
vs = await views();
check('Left on the resize area many times hides the sidebar with compact mode, no rail',
      !railShown(vs) && region(vs)?.w === 0, `${region(vs)?.w}`);
await press(VK.S, CTRL, 's', 1500);
await focus();
for (let i = 0; i < 8; i++) {
  await key(page, 39, 0, 'ArrowRight');
}
await sleep(1000);
vs = await views();
check('Right many times widens the sidebar to at most 500',
      region(vs)?.w > 230 && region(vs)?.w <= 500, `${region(vs)?.w}`);

check('Koan still answers', await alive());
check('The leak detector is silent', !leakLogged());
finish();
