// End-to-end test of compact mode with single toolbar (Zen's default
// layout): it hides just the sidebar and keeps the sidebar's header, also
// across changes made while it is on: tabs on the right from the sidebar's
// menu (opened twice), and the layout switched away and back with
// Ctrl+Alt+B; and in a window opened meanwhile.
//
//   scripts/e2e/run.sh compact_single
import { ALT, CTRL, check, finish, key, sidebarMenu, sleep, views, visiblePage }
    from './lib/koan.mjs';

const find = (vs, cls) => vs.find(v => v.cls === cls);
const findShown = (vs, cls) => vs.find(v => v.cls === cls && v.shown);
// A key on the page showing; unhandled keys reach the browser's shortcuts.
const press = async (code, modifiers, name) => {
  await key(await visiblePage(), code, modifiers, name);
  await sleep(900);
};
const VK = { S: 83, B: 66, N: 78 };
// With single toolbar the menu has the compact toggle and Tabs on the right.
const TABS_ON_RIGHT = 1;

// Single-toolbar compact mode: the sidebar hidden, with its header; no
// toolbar above the page.
async function checkSingleToolbarCompact(when, onRight) {
  const vs = await views();
  const bv = findShown(vs, 'BrowserView');
  const strip = find(vs, 'VerticalTabStripRegionView');
  const zone = vs.find(v => v.cls === 'KoanHotZone' && v.shown && v.h > bv.h / 2);
  check(`${when}: compact mode keeps single toolbar`,
        strip?.w === 0 && find(vs, 'KoanSidebarNavButtons') &&
        find(vs, 'TopContainerView')?.path.includes('TopContainerOverlayView'),
        `sidebar ${strip?.w}, nav ${!!find(vs, 'KoanSidebarNavButtons')}, ` +
        `toolbar in ${find(vs, 'TopContainerView')?.path.at(-1)}`);
  check(`...with the sidebar's hot zone on the ${onRight ? 'right' : 'left'}`,
        zone && (onRight ? zone.x + zone.w === bv.x + bv.w : zone.x === bv.x),
        zone && `${zone.x}+${zone.w}`);
}

await press(VK.S, CTRL, 's');
await checkSingleToolbarCompact('Ctrl+S', false);

// Tabs on the right from the menu of the peeked sidebar, then back: the
// menu opens twice in the window.
await press(VK.S, CTRL | ALT, 's');
let missing = await sidebarMenu(TABS_ON_RIGHT);
check('The sidebar\'s menu has Tabs on the right', !missing, missing ?? '');
await sleep(3500);
await checkSingleToolbarCompact('Tabs on the right', true);
await press(VK.S, CTRL | ALT, 's');
missing = await sidebarMenu(TABS_ON_RIGHT);
check('...and opens again to put them back on the left', !missing, missing ?? '');
await sleep(3500);
await checkSingleToolbarCompact('Tabs on the left', false);

// Multiple toolbars and back while compact mode is on.
await press(VK.B, CTRL | ALT, 'b');
let vs = await views();
check('Ctrl+Alt+B in compact mode: the toolbar is above the page, sidebar hidden',
      find(vs, 'TopContainerView')?.path.at(-1) === 'BrowserView' &&
      find(vs, 'VerticalTabStripRegionView')?.w === 0 && !find(vs, 'KoanSidebarNavButtons'));
await press(VK.B, CTRL | ALT, 'b');
await checkSingleToolbarCompact('Ctrl+Alt+B again', false);

await press(VK.S, CTRL, 's');
vs = await views();
check('Ctrl+S leaves compact mode, still single toolbar',
      findShown(vs, 'VerticalTabStripRegionView')?.w >= 200 &&
      findShown(vs, 'KoanSidebarNavButtons'));

// A window opened while compact mode is on: Chromium starts it with the
// sidebar collapsed (compact mode's collapse is the profile's sidebar
// state), which mustn't count as the user's.
const windows = vs => {
  const roots = vs.flatMap((v, i) => (v.cls === 'BrowserRootView' ? [i] : []));
  return roots.map((start, n) => vs.slice(start, roots[n + 1]));
};
await press(VK.S, CTRL, 's');
await press(VK.N, CTRL, 'n');
await sleep(1500);
let all = windows(await views());
check('Ctrl+N in compact mode opens a second window', all.length === 2, `${all.length} windows`);
check('...which keeps single toolbar too',
      all.length === 2 && all.every(w => find(w, 'KoanSidebarNavButtons') &&
          find(w, 'TopContainerView')?.path.includes('TopContainerOverlayView') &&
          find(w, 'VerticalTabStripRegionView')?.w === 0));
await press(VK.S, CTRL, 's');
await sleep(1000);
all = windows(await views());
check('Leaving compact mode gives both windows their expanded sidebar back',
      all.length === 2 && all.every(w => findShown(w, 'VerticalTabStripRegionView')?.w >= 200 &&
                                         findShown(w, 'KoanSidebarNavButtons')),
      all.map(w => find(w, 'VerticalTabStripRegionView')?.w).join(', '));

finish();
