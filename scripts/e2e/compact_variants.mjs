// End-to-end test of compact mode's variants (Zen's "Compact Mode" submenu)
// in the multiple-toolbars layout. prefs/compact_variants.json starts in
// compact mode hiding just the toolbar; the sidebar's context menu switches
// to hiding both and to hiding just the sidebar. On macOS that menu is a
// native NSMenu that UI DevTools can't reach (NATIVE_MENUS), so the pref is
// set as its items set it (koan.compact_mode.hide), and the screenshot of
// the revealed toolbar, from Linux's frame dumps (compact_variants.flags),
// isn't taken (the .flags' "# macOS: run" leaves the dump flag out).
//
//   scripts/e2e/run.sh compact_variants
import { ACCEL } from './lib/koan.mjs';
import fs from 'node:fs';

import { ALT, NATIVE_MENUS, check, finish, key, mouseOn, setKoanPref, sidebarMenu, sleep, views,
         visiblePage } from './lib/koan.mjs';
import { checkSlidesAway } from './lib/mac.mjs';

const find = (vs, cls) => vs.find(v => v.cls === cls);
const findShown = (vs, cls) => vs.find(v => v.cls === cls && v.shown);
// A key on the page showing; unhandled keys reach the browser's shortcuts.
const press = async (code, modifiers, name) => {
  await key(await visiblePage(), code, modifiers, name);
  await sleep(900);
};

const VK = { S: 83 };

// The menu: Compact Mode > (Enable compact mode, Hide sidebar, Hide toolbar,
// Hide both), Tabs on the right.
const COMPACT_MODE = 0, HIDE_SIDEBAR = 1, HIDE_BOTH = 3;
// What each item sets koan.compact_mode.hide to
// (KoanCompactModeController::SetCompactModeHides()).
const HIDES = { [HIDE_SIDEBAR]: 'sidebar', [HIDE_BOTH]: 'both' };
// Picks Compact Mode > `item` in the sidebar's menu; on macOS sets the pref
// it sets. What went wrong, or null.
async function compactMenu(item) {
  if (!NATIVE_MENUS) return sidebarMenu(COMPACT_MODE, item);
  return await setKoanPref('koan.compact_mode.hide', HIDES[item]) ? null
      : `koanSetPref refused koan.compact_mode.hide=${HIDES[item]}`;
}
const MENU = NATIVE_MENUS ? ' (on macOS its pref: the menu is native)' : '';

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

// The toolbar slides in beside the sidebar, above the page only (Zen's
// navbar), not over the sidebar's top row.
const toolbarZone = zones[0];
await mouseOn(toolbarZone, 'mouseEntered');
top = find(await views(), 'TopContainerView');
check('...the toolbar is revealed beside the sidebar, not over it',
      top && top.x === strip.x + strip.w && top.x + top.w === bv.x + bv.w,
      `toolbar ${top?.x}+${top?.w}, sidebar ${strip.x}+${strip.w}`);
// SHOTS=<dir> keeps the window's latest frame (compact_variants.flags).
if (process.env.SHOTS && process.env.KOAN_DUMP_DIR) {
  const dir = process.env.KOAN_DUMP_DIR;
  const frame = fs.readdirSync(dir).filter(f => f.endsWith('.png'))
      .map(f => `${dir}/${f}`).sort((a, b) => fs.statSync(b).size - fs.statSync(a).size)[0];
  fs.copyFileSync(frame, `${process.env.SHOTS}/compact-revealed-toolbar.png`);
}
await sleep(1000);

// (The pointer leaves the toolbar for the menu; on macOS, with no menu, it
// just leaves.)
if (NATIVE_MENUS) await mouseOn(toolbarZone, 'mouseExited');
let missing = await compactMenu(HIDE_BOTH);
check(`The sidebar's menu has Compact Mode > Hide both${MENU}`, !missing, missing ?? '');
if (NATIVE_MENUS) await sleep(3000);
vs = await views();
strip = find(vs, 'VerticalTabStripRegionView');
top = find(vs, 'TopContainerView');
zones = vs.filter(v => v.cls === 'KoanHotZone' && v.shown);
// (Unless the real pointer holds it out, lib/mac.mjs checkSlidesAway().)
await checkSlidesAway(check, 'Hide both: the sidebar takes no space', strip?.w === 0,
                      `width ${strip?.w}`);
check('...the toolbar is still hidden', top?.path.includes('TopContainerOverlayView'));
check('...hot zones along the left and top edges', zones.length === 2 &&
      zones.some(z => z.x === bv.x && z.w === 8 && z.h > bv.h / 2) &&
      zones.some(z => z.y === bv.y && z.h === 8 && z.w > bv.w / 2),
      JSON.stringify(zones.map(z => [z.x, z.y, z.w, z.h])));

// The hidden sidebar slides in for the menu: pick the variant from it.
if (!NATIVE_MENUS) await press(VK.S, ACCEL | ALT, 's');
missing = await compactMenu(HIDE_SIDEBAR);
check(`...and the menu switches to Hide sidebar${MENU}`, !missing, missing ?? '');
await sleep(4000);
vs = await views();
strip = find(vs, 'VerticalTabStripRegionView');
top = find(vs, 'TopContainerView');
// (Unless the real pointer holds it out, lib/mac.mjs checkSlidesAway().)
await checkSlidesAway(check, 'Hide sidebar: the sidebar takes no space', strip?.w === 0,
                      `width ${strip?.w}`);
check('...and the toolbar is back above the page', top?.path.at(-1) === 'BrowserView');

await press(VK.S, ACCEL, 's');
vs = await views();
check('Ctrl+S leaves compact mode', findShown(vs, 'VerticalTabStripRegionView')?.w >= 200 &&
      find(vs, 'TopContainerView')?.path.at(-1) === 'BrowserView' &&
      !vs.some(v => v.cls === 'KoanHotZone'));

finish();
