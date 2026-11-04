// End-to-end test that the sidebar animates without laying anything out
// again on every frame (a relayout storm flickers, most on macOS):
// - expanding, collapsing and compact mode's hiding of the sidebar resize the
//   page once, not on every frame (counted as the page's resize events);
// - the hidden sidebar's reveal lays the strip out once, at the width it
//   shows at, and slides it in with a layer transform (sampled as the strip's
//   width while it slides in and out), and doesn't resize the page at all.
//
//   scripts/e2e/run.sh sidebar_animation
import { CTRL, ALT, boundsOf, check, evalIn, finish, key, mouseOn, sleep, views, visiblePage }
    from './lib/koan.mjs';

const findShown = (vs, cls) => vs.find(v => v.cls === cls && v.shown);
const VK = { S: 83, B: 66 };
// Upstream's expand-on-hover width, which compact mode's reveal slides in.
const REVEAL_WIDTH = 240;
// A frame at a time would be ~15 resizes for a 250 ms animation; a single
// reflow gives one, plus what the layout switch around it adds.
const MAX_RESIZES = 4;

const page = await visiblePage();
const resizes = async () => {
  const count = await evalIn(page, 'window.__resizes');
  await evalIn(page, 'window.__resizes = 0');
  return count;
};
await evalIn(page, `window.__resizes = 0;
                    addEventListener('resize', () => window.__resizes++); 0`);

const press = async (code, modifiers, name) => {
  await key(page, code, modifiers, name);
  await sleep(900);
};
const toggleOf = vs => vs.find(v => v.shown && v.path.at(-1) === 'VerticalTabStripTopContainer' &&
                                    v.cls !== 'KoanSidebarNavButtons' && v.w > 0);
const clickOn = async view => {
  await mouseOn(view, 'mousePressed');
  await mouseOn(view, 'mouseReleased');
  await sleep(1200);
};

// The widths of the sidebar's strip, sampled as fast as UI DevTools answers,
// for `ms`.
async function sampleStripWidths(ms) {
  const widths = [];
  const end = Date.now() + ms;
  while (Date.now() < end) {
    const [strip] = await boundsOf('VerticalTabStripRegionView');
    widths.push(strip?.w ?? 0);
  }
  return widths;
}
const distinct = widths => [...new Set(widths)].sort((a, b) => a - b);

// The sidebar's toggle collapses and expands it.
await resizes();
let vs = await views();
await clickOn(toggleOf(vs));
check('Collapsing the sidebar resizes the page once, not on every frame',
      (await resizes()) <= MAX_RESIZES);
await press(VK.B, CTRL | ALT, 'b');
check('...and so does expanding it', (await resizes()) <= MAX_RESIZES);

// Compact mode hides it without a jump.
await press(VK.S, CTRL, 's');
vs = await views();
check('Compact mode hides the sidebar completely',
      !findShown(vs, 'VerticalTabStripRegionView') ||
      findShown(vs, 'VerticalTabStripRegionView').w === 0);
check('...resizing the page once', (await resizes()) <= MAX_RESIZES);

// The reveal: hovering the hot zone at the edge.
const zone = vs.find(v => v.cls === 'KoanHotZone' && v.shown &&
                          v.h > findShown(vs, 'BrowserView').h / 2);
check('The sidebar has a hot zone along its edge', !!zone);

await mouseOn(zone, 'mouseEntered');
let widths = await sampleStripWidths(900);
let shown = distinct(widths.filter(w => w > 0));
check('The hot zone reveals the sidebar', widths.at(-1) === REVEAL_WIDTH,
      `final width ${widths.at(-1)}`);
check('...laid out once at its width, not at a new one on every frame',
      shown.length === 1, `widths ${shown.join(', ')} of ${widths.length} samples`);
check('...without resizing the page', (await resizes()) === 0);

await mouseOn(zone, 'mouseExited');
widths = await sampleStripWidths(1500);
shown = distinct(widths.filter(w => w > 0));
check('The sidebar slides away again', widths.at(-1) === 0, `final width ${widths.at(-1)}`);
check('...without a new width on every frame', shown.length <= 1,
      `widths ${shown.join(', ')} of ${widths.length} samples`);
check('...or resizing the page', (await resizes()) === 0);

// Leaving compact mode expands the sidebar again.
await press(VK.S, CTRL, 's');
await sleep(500);
vs = await views();
check('Ctrl+S gives the sidebar back', findShown(vs, 'VerticalTabStripRegionView')?.w >= 200);

finish();
