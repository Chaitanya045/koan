// End-to-end test of compact mode's variants (Zen's "Compact Mode" submenu)
// in the multiple-toolbars layout. prefs/compact_variants.json starts in
// compact mode hiding just the toolbar; the sidebar's context menu switches
// to hiding both and to hiding just the sidebar.
//
//   scripts/e2e/run.sh compact_variants
import { ALT, CTRL, check, finish, key, mouseOn, sleep, views, visiblePage }
    from './lib/koan.mjs';

const find = (vs, cls) => vs.find(v => v.cls === cls);
const findShown = (vs, cls) => vs.find(v => v.cls === cls && v.shown);
// A key on the page showing; unhandled keys reach the browser's shortcuts.
const press = async (code, modifiers, name) => {
  await key(await visiblePage(), code, modifiers, name);
  await sleep(900);
};

const VK = { S: 83 };

// Right-clicks an empty part of the sidebar (through the window's root view,
// so the click is routed like a real one), then picks the menu items at
// `indices` in turn, each in the newest (sub)menu. Returns what went wrong.
async function sidebarMenu(...indices) {
  let vs = await views();
  const strip = findShown(vs, 'VerticalTabStripRegionView');
  const root = findShown(vs, 'BrowserRootView');
  const x = strip.x + strip.w / 2, y = strip.y + strip.h - 120;
  await mouseOn(root, 'mousePressed', x, y, 'right');
  await mouseOn(root, 'mouseReleased', x, y, 'right');
  await sleep(800);
  for (const index of indices) {
    vs = await views();
    // The newest menu widget's root view, and its items.
    const menuRoot = vs.findLast(v => v.cls.endsWith('RootView') && v.cls !== 'BrowserRootView');
    const items = vs.slice(vs.indexOf(menuRoot))
        .filter(v => v.cls === 'MenuItemView' && v.path[0] === menuRoot?.cls);
    const item = items[index];
    if (!item) {
      return `no item ${index}: ${vs.filter(v => /Menu|RootView/.test(v.cls)).map(v => v.cls)}`;
    }
    const at = [item.x - menuRoot.x + 10, item.y - menuRoot.y + item.h / 2];
    await mouseOn(menuRoot, 'mouseMoved', ...at);
    await sleep(600);
    await mouseOn(menuRoot, 'mousePressed', ...at);
    await mouseOn(menuRoot, 'mouseReleased', ...at);
    await sleep(800);
  }
  return null;
}

// The menu: Compact Mode > (Enable compact mode, Hide sidebar, Hide toolbar,
// Hide both), Tabs on the right.
const COMPACT_MODE = 0, HIDE_SIDEBAR = 1, HIDE_BOTH = 3;

let vs = await views();
const bv = findShown(vs, 'BrowserView');
let strip = findShown(vs, 'VerticalTabStripRegionView');
let top = find(vs, 'TopContainerView');
let card = findShown(vs, 'ContentsContainerView');
let zones = vs.filter(v => v.cls === 'KoanHotZone' && v.shown);
check('Hide toolbar: the sidebar stays', strip?.w >= 200, `width ${strip?.w}`);
check('...the toolbar is hidden above the page',
      top?.path.includes('TopContainerOverlayView'), top?.path.at(-1));
check('...the page reaches up to the gutter', card?.y === bv.y + 8, `y ${card?.y}`);
check('...one hot zone, along the top edge above the page',
      zones.length === 1 && zones[0].y === bv.y && zones[0].x >= strip.x + strip.w &&
      zones[0].h === 8, JSON.stringify(zones.map(z => [z.x, z.y, z.w, z.h])));

let missing = await sidebarMenu(COMPACT_MODE, HIDE_BOTH);
check('The sidebar\'s menu has Compact Mode > Hide both', !missing, missing ?? '');
vs = await views();
strip = find(vs, 'VerticalTabStripRegionView');
top = find(vs, 'TopContainerView');
zones = vs.filter(v => v.cls === 'KoanHotZone' && v.shown);
check('Hide both: the sidebar takes no space', strip?.w === 0, `width ${strip?.w}`);
check('...the toolbar is still hidden', top?.path.includes('TopContainerOverlayView'));
check('...hot zones along the left and top edges', zones.length === 2 &&
      zones.some(z => z.x === bv.x && z.w === 8 && z.h > bv.h / 2) &&
      zones.some(z => z.y === bv.y && z.h === 8 && z.w > bv.w / 2),
      JSON.stringify(zones.map(z => [z.x, z.y, z.w, z.h])));

// The hidden sidebar slides in for the menu: pick the variant from it.
await press(VK.S, CTRL | ALT, 's');
missing = await sidebarMenu(COMPACT_MODE, HIDE_SIDEBAR);
check('...and the menu switches to Hide sidebar', !missing, missing ?? '');
await sleep(4000);
vs = await views();
strip = find(vs, 'VerticalTabStripRegionView');
top = find(vs, 'TopContainerView');
check('Hide sidebar: the sidebar takes no space', strip?.w === 0, `width ${strip?.w}`);
check('...and the toolbar is back above the page', top?.path.at(-1) === 'BrowserView');

await press(VK.S, CTRL, 's');
vs = await views();
check('Ctrl+S leaves compact mode', findShown(vs, 'VerticalTabStripRegionView')?.w >= 200 &&
      find(vs, 'TopContainerView')?.path.at(-1) === 'BrowserView' &&
      !vs.some(v => v.cls === 'KoanHotZone'));

finish();
