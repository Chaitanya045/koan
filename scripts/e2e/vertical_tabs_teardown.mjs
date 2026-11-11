// End-to-end stress test of the sidebar's collapse state around compact mode
// and window teardown (audit tests 21, 22 and 24; #154). Everything goes
// through KoanVerticalTabsPolicy::SetSidebarHidden(); whatever the timing, the
// browser must keep answering and each window must end in one of the two
// states Koan has (expanded, or hidden by compact mode), never the rail.
//
// - compact mode on and off 20 times in about 10 seconds, then a second window
//   closed in the middle of it;
// - closing a window with the sidebar revealed (hot zone), pinned out
//   (Ctrl+Alt+S), and with the new-Space form open (see compact_close.mjs for
//   the same with compact mode on from the start); the first window unchanged;
// - fullscreen in and out with compact mode off and on (macOS native
//   fullscreen; elsewhere the checks around it still run): no rail, no
//   Chromium toolbar at the top edge, no mode-switch toast;
// - at the end the leak detector has said nothing.
//
//   scripts/e2e/run.sh vertical_tabs_teardown
import { ACCEL } from './lib/koan.mjs';
import { ALT, CTRL, browserCall, check, finish, key, mouseOn, pages, sleep, views, waitFor }
    from './lib/koan.mjs';
import { VK, alive, checkKoanSidebar, leakLogged, press, railShown, region } from './lib/vertical_tabs.mjs';
import { goFullscreen, isMac } from './lib/fullscreen.mjs';

const windows = vs => {
  const roots = vs.flatMap((v, i) => (v.cls === 'BrowserRootView' ? [i] : []));
  return roots.map((start, n) => vs.slice(start, roots[n + 1]));
};
let serial = 0;
async function openWindow() {
  const r = await browserCall('Target.createTarget',
      { url: `https://example.com/?vt${++serial}`, newWindow: true });
  await sleep(2000);
  return { id: r.result.targetId, page: (await pages()).find(p => p.id === r.result.targetId) };
}
const closeWindow = async w => {
  await browserCall('Target.closeTarget', { targetId: w.id });
  await sleep(1000);
};
const settle = async (ms = 1500) => sleep(ms);

checkKoanSidebar('Start', {}, await views());

// Test 21: 20 toggles in ~10 s, a window closed in the middle (the rest go
// to the first window).
const [firstPage] = await pages();
let w = await openWindow();
for (let i = 0; i < 20; i++) {
  await key(w?.page ?? firstPage, VK.S, ACCEL, 's');
  await sleep(150);
  if (i === 10) {
    await closeWindow(w);
    w = null;
  }
}
check('20 compact toggles with a window closed in the middle: still alive', await alive());
await settle(2500);
const hiddenNow = region(await views())?.w === 0;
check('The first window ended in one of Koan\'s two states',
      hiddenNow || region(await views())?.w >= 163, `${region(await views())?.w}`);
check('...and no rail', !railShown(await views()));
if (hiddenNow) {
  await press(VK.S, ACCEL, 's', 1500);
}
checkKoanSidebar('After the toggles', {}, await views());

// Closing with the sidebar revealed, pinned out and with the form open.
await press(VK.S, ACCEL, 's', 1500);
w = await openWindow();
{
  const last = windows(await views()).at(-1);
  const bv = last.find(v => v.cls === 'BrowserView');
  const zone = last.find(v => v.cls === 'KoanHotZone' && v.shown && v.h > bv.h / 2);
  if (zone) await mouseOn(zone, 'mouseEntered');
  await sleep(700);
}
await closeWindow(w);
check('A close with the sidebar revealed: alive', await alive());
w = await openWindow();
await key(w.page, VK.S, ACCEL | ALT, 's');
await sleep(700);
await closeWindow(w);
check('A close with the sidebar pinned out: alive', await alive());
w = await openWindow();
await key(w.page, 78, CTRL | ALT, 'n');
await waitFor(async () => windows(await views()).at(-1)
    .find(v => v.cls === 'KoanSpaceCreationView' && v.shown));
await closeWindow(w);
check('A close with the new-Space form open: alive', await alive());
await press(VK.S, ACCEL, 's', 1500);
checkKoanSidebar('The first window after the closes', {}, await views());

// Test 22: fullscreen with compact mode off and on.
if (await isMac()) {
  for (const compact of [false, true]) {
    if (compact) await press(VK.S, ACCEL, 's', 1500);
    await goFullscreen(true);
    let vs = await views();
    check(`Fullscreen (compact ${compact}): no rail`, !railShown(vs));
    check(`Fullscreen (compact ${compact}): no mode-switch toast`,
          !vs.some(v => v.shown && /Toast/.test(v.cls) && v.cls !== 'KoanToastView'));
    await goFullscreen(false);
    vs = await views();
    checkKoanSidebar(`After fullscreen (compact ${compact})`, { hidden: compact }, vs);
  }
  await press(VK.S, ACCEL, 's', 1500);
} else {
  console.log('note: native fullscreen is macOS only; test 22 skipped here');
}

check('Koan still answers', await alive());
check('The leak detector is silent', !leakLogged());
finish();
