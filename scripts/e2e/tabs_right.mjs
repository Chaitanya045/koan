// End-to-end test of Zen's "Tabs on the right" (koan.tabs.on_right, seeded
// by prefs/tabs_right.json): the sidebar on the right with its row
// mirrored and its resize area on its left edge; compact mode hides it at
// the right edge; the multiple-toolbars layout puts the toolbar on the left.
//
//   scripts/e2e/run.sh tabs_right
import { ALT, CTRL, boundsOf, check, finish, key, setChromePref, setKoanPref, sleep, views,
         visiblePage }
    from './lib/koan.mjs';

const find = (vs, cls) => vs.find(v => v.cls === cls);
const findShown = (vs, cls) => vs.find(v => v.cls === cls && v.shown);
// A key on the page showing; unhandled keys reach the browser's shortcuts.
const press = async (code, modifiers, name) => {
  await key(await visiblePage(), code, modifiers, name);
  await sleep(900);
};

const VK = { S: 83, B: 66 };

let vs = await views();
const bv = findShown(vs, 'BrowserView');
const right = bv.x + bv.w;
let strip = findShown(vs, 'VerticalTabStripRegionView');
check('The sidebar is on the right edge of the window',
      strip && strip.x > bv.x + bv.w / 2 && strip.x + strip.w === right,
      `strip ${strip?.x}+${strip?.w}, window right ${right}`);
let card = findShown(vs, 'ContentsContainerView');
check('...the page card is on its left, with the gutter on the window side',
      card && card.x === bv.x + 8 && card.x + card.w <= strip.x && card.y === bv.y + 8,
      `card ${card?.x},${card?.y} ${card?.w}x${card?.h}`);
const resize = vs.find(v => v.cls === 'ResizeArea' &&
                            v.path.includes('VerticalTabStripRegionView'));
check('...the resize area is on the sidebar\'s left edge', resize?.x === strip.x,
      `resize ${resize?.x}, strip ${strip?.x}`);
const row = findShown(vs, 'VerticalTabStripTopContainer');
const nav = findShown(vs, 'KoanSidebarNavButtons');
const toggle = vs.find(v => v.shown && v.path.at(-1) === 'VerticalTabStripTopContainer' &&
                            v.cls !== 'KoanSidebarNavButtons' && v.w > 0);
check('...its top row is mirrored: navigation buttons left, sidebar toggle right',
      nav && toggle && nav.x < toggle.x && toggle.x + toggle.w > row.x + row.w / 2,
      `nav ${nav?.x}, toggle ${toggle?.cls} ${toggle?.x}, row ${row?.x}+${row?.w}`);

// The window buttons' hot zone runs along the top edge above the page.
const topZone = vs.find(v => v.cls === 'KoanHotZone' && v.shown && v.h === 8);
check('...the window buttons\' hot zone is along the top above the page',
      topZone && topZone.y === bv.y && topZone.x === bv.x && topZone.x + topZone.w === strip.x,
      topZone && `${topZone.x}+${topZone.w}`);

// Compact mode hides the sidebar at the right edge.
await press(VK.S, CTRL, 's');
vs = await views();
strip = find(vs, 'VerticalTabStripRegionView');
check('Compact mode hides the sidebar', strip?.w === 0, `width ${strip?.w}`);
card = findShown(vs, 'ContentsContainerView');
check('...the card has a gutter on the right too', card && card.x + card.w === right - 8,
      `card right ${card && card.x + card.w}`);
const zone = vs.find(v => v.cls === 'KoanHotZone' && v.shown && v.h > bv.h / 2);
check('...and the sidebar hot zone is on the right edge',
      zone && zone.x + zone.w === right && zone.w === 8, `zone ${zone?.x}+${zone?.w}`);
await press(VK.S, CTRL | ALT, 's');
await sleep(500);
const [peek] = await boundsOf('VerticalTabStripRegionView');
check('Ctrl+Alt+S slides it in from the right',
      peek && peek.w > 100 && peek.x + peek.w === bv.w, `${peek?.x}+${peek?.w}`);
await press(VK.S, CTRL, 's');

// Multiple toolbars: the toolbar is above the page, left of the sidebar.
await press(VK.B, CTRL | ALT, 'b');
vs = await views();
strip = findShown(vs, 'VerticalTabStripRegionView');
const top = findShown(vs, 'TopContainerView');
check('Multiple toolbars: the toolbar is on the left, beside the sidebar',
      top?.path.at(-1) === 'BrowserView' && top.x === bv.x && top.x + top.w <= strip.x,
      `toolbar ${top?.x}+${top?.w}, strip ${strip?.x}`);
await press(VK.B, CTRL | ALT, 'b');

// A window without Koan's sidebar (horizontal tabs) isn't mirrored: the
// setting changes nothing in it.
const topLevel = async () => (await views())
    .filter(v => v.shown && v.path.at(-1) === 'BrowserView')
    .map(v => `${v.cls} ${v.x},${v.y} ${v.w}x${v.h}`).join('; ');
check('Horizontal tabs', (await setChromePref('vertical_tabs.enabled', false)) === true);
await sleep(1500);
const mirrored = await topLevel();
await setKoanPref('koan.tabs.on_right', false);
await sleep(1500);
const plain = await topLevel();
check('...aren\'t mirrored by tabs on the right', mirrored === plain,
      mirrored === plain ? '' : `${mirrored} | ${plain}`);
await setChromePref('vertical_tabs.enabled', true);

finish();
