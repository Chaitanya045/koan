// End-to-end regression test for closing a window with compact mode on
// (#130: closing a window tears the browser down before its BrowserView, and
// anything that starts a sidebar animation then crashed). Compact mode is on
// from the seeded prefs (prefs/compact_close.json). A second window is opened
// and closed in each state the sidebar can be in, and after each the browser
// must still answer, with the first window as it was:
// - a plain close;
// - a close with the sidebar revealed by the edge hot zone;
// - a close after Ctrl+Alt+S (the sidebar pinned out);
// - a close right after the compact toggle, while it animates;
// - a close with the new-Space form open (its lock keeps the sidebar out);
// - a quit with compact mode on and a relaunch, which opens in compact mode.
//
//   scripts/e2e/run.sh compact_close
import { ACCEL } from './lib/koan.mjs';
import { spawn } from 'node:child_process';
import { ALT, CDP_PORT, CTRL, browserCall, check, connect, finish, front, key, mouseOn, pages,
         sleep, views, waitFor }
    from './lib/koan.mjs';

const VK = { S: 83, N: 78 };
const alive = async () => !!(await browserCall('Browser.getVersion')).result?.product;
const windows = vs => {
  const roots = vs.flatMap((v, i) => (v.cls === 'BrowserRootView' ? [i] : []));
  return roots.map((start, n) => vs.slice(start, roots[n + 1]));
};
const stripWidths = async () => (await views())
    .filter(v => v.cls === 'VerticalTabStripRegionView').map(v => v.w);

let serial = 0;
async function openWindow() {
  const r = await browserCall('Target.createTarget',
      { url: `https://example.com/?close${++serial}`, newWindow: true });
  const id = r.result.targetId;
  await sleep(2000);
  const page = (await pages()).find(p => p.id === id);
  // On macOS a window made through CDP isn't always the key window, and keys
  // sent to an unfocused window's page don't reach the browser's shortcuts.
  await front(page);
  await sleep(300);
  return { id, page };
}
async function closeWindow(w) {
  await browserCall('Target.closeTarget', { targetId: w.id });
  await sleep(1000);
}
const first = (await stripWidths())[0];
const [firstPage] = await pages();

// A plain close.
let w = await openWindow();
await closeWindow(w);
check('A plain close doesn\'t crash', await alive());

// The sidebar revealed by the edge hot zone.
w = await openWindow();
{
  const last = windows(await views()).at(-1);
  const bv = last.find(v => v.cls === 'BrowserView');
  const zone = last.find(v => v.cls === 'KoanHotZone' && v.shown && v.h > bv.h / 2);
  check('The new window has the sidebar\'s hot zone (compact mode on)', !!zone);
  if (zone) await mouseOn(zone, 'mouseEntered');
  await sleep(700);
}
await closeWindow(w);
check('A close with the sidebar revealed doesn\'t crash', await alive());

// Pinned out with Ctrl+Alt+S.
w = await openWindow();
await key(w.page, VK.S, ACCEL | ALT, 's');
await sleep(700);
await closeWindow(w);
check('A close with the sidebar pinned out doesn\'t crash', await alive());

// Right after the compact toggle, while it animates. Compact mode is the
// profile's (every window's), so the first window turns it back on.
w = await openWindow();
await key(w.page, VK.S, ACCEL, 's');
await sleep(50);
await closeWindow(w);
check('A close during the compact toggle doesn\'t crash', await alive());
await key(firstPage, VK.S, ACCEL, 's');
await sleep(1000);

// With the new-Space form open.
w = await openWindow();
await key(w.page, VK.N, CTRL | ALT, 'n');
// The windows' order in UI DevTools isn't the windows' order on every
// platform (on macOS the new one can come first): the form is the new
// window's if it goes with it.
const form = await waitFor(async () => (await views())
    .find(v => v.cls === 'KoanSpaceCreationView' && v.shown));
await closeWindow(w);
const formLeft = (await views()).some(v => v.cls === 'KoanSpaceCreationView' && v.shown);
check('The new-Space form opens in the new window', !!form && !formLeft,
      form ? (formLeft ? 'it stayed: it was in the first window' : '') : 'no form');
check('A close with the new-Space form open doesn\'t crash', await alive());

check('The first window\'s sidebar is as it was',
      (await stripWidths())[0] === first, `${(await stripWidths())[0]} vs ${first}`);

// Quit with compact mode on, relaunch on the same profile.
const version = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
const quit = await connect(version.webSocketDebuggerUrl);
await quit.send('Browser.close');
quit.close();
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
check('After quitting with compact mode on, Koan relaunches', await alive());
check('...in compact mode (the sidebar takes no space)',
      (await stripWidths())[0] === 0, `${(await stripWidths())[0]}`);

finish();
