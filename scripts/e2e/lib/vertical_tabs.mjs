// Shared checks for the "Koan owns the sidebar" tests (vertical_tabs_*.mjs,
// .agent/notes/upstream/vertical-tabs.md section 4): whatever an old profile, a
// shortcut, a drag or a setting does, the window keeps Koan's vertical
// sidebar, and Chromium's icon rail, horizontal tab strip and toolbar don't
// show up.
import fs from 'node:fs';
import { browserCall, check, key, sleep, views, visiblePage } from './koan.mjs';

export const VK = { S: 83, L: 76, A: 65, B: 66, F: 70 };

export const region = vs => vs.find(v => v.cls === 'VerticalTabStripRegionView');
// Chromium's collapsed rail: the strip is shown, but only about 56 wide.
export const railShown = vs => {
  const r = region(vs);
  return !!r && r.shown && r.w > 0 && r.w < 120;
};
// Chromium's own toolbar drawn in the window (single toolbar parks it in the
// overlay, where it isn't).
export const chromeToolbarShown = vs => vs.some(
    v => v.cls === 'ToolbarView' && v.shown && v.h > 0 &&
         !v.path.includes('TopContainerOverlayView'));
export const horizontalStripShown = vs => vs.some(
    v => v.cls === 'TabStripRegionView' && v.shown && v.h > 0);

// The window's sidebar as Koan has it: vertical, no rail, no horizontal
// strip, Koan's Space header inside it; expanded (`hidden` false, 163 to 500
// wide: Zen's limits) or hidden by compact mode (`hidden` true, no width).
export function checkKoanSidebar(when, { hidden = false, toolbar = false } = {}, vs) {
  const r = region(vs);
  check(`${when}: the sidebar is Koan's vertical strip`, !!r && !horizontalStripShown(vs));
  check(`${when}: ${hidden ? 'hidden by compact mode (no width)' : 'expanded at Zen\'s width'}`,
        hidden ? r?.w === 0 : r?.w >= 163 && r?.w <= 500, `${r?.w}`);
  check(`${when}: no icon rail`, !railShown(vs));
  check(`${when}: Chromium's toolbar is ${toolbar ? 'shown' : 'not drawn'}`,
        chromeToolbarShown(vs) === toolbar);
  if (!hidden) {
    check(`${when}: Koan's Space header is in the sidebar`,
          vs.some(v => v.cls === 'KoanSpaceHeaderView' && v.path.includes('VerticalTabStripRegionView')));
  }
}

// A key on the page showing; unhandled keys reach the browser's shortcuts.
export async function press(code, modifiers, name, wait = 900) {
  await key(await visiblePage(), code, modifiers, name);
  await sleep(wait);
}

export const alive = async () => !!(await browserCall('Browser.getVersion')).result?.product;

// Chromium's tab search bubble: a WebUIBubbleDialogView / TabSearch... in any
// widget. Koan's answer to Shift+Cmd+A is the URL bar, not that bubble.
export const tabSearchBubbleShown = vs => vs.some(
    v => v.shown && /TabSearch/.test(v.cls) && v.cls !== 'TabSearchButton');

// Upstream's expand-on-hover or vertical tabs hint (a feature promo bubble).
export const promoBubbleShown = vs => vs.some(v => v.shown && /FeaturePromoBubble|HelpBubble/.test(v.cls));

// The strip's width, sampled for `ms`: every distinct value.
export async function sampleWidths(ms) {
  const seen = new Set();
  const end = Date.now() + ms;
  while (Date.now() < end) {
    seen.add(region(await views())?.w ?? -1);
    await sleep(100);
  }
  return [...seen];
}

// The leak detector (KoanVerticalTabsPolicy, debug builds) logs a fatal when
// something other than Koan changes the strip's mode or collapse state; a
// build without DCHECKs has nothing to find. Needs KEEP_LOG=<file> in run.sh.
export function leakLogged() {
  const log = process.env.KEEP_LOG;
  if (!log || !fs.existsSync(log)) {
    console.log('note: KEEP_LOG isn\'t set, so the leak detector\'s log is not checked');
    return false;
  }
  return /changed the vertical tab strip's mode|collapsed or expanded by something other/
      .test(fs.readFileSync(log, 'utf8'));
}
