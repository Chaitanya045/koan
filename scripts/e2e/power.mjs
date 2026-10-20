// End-to-end test of Koan's power and memory behaviour against a real build:
// the hidden toolbar doesn't repaint, compact mode runs no polling timers,
// Memory Saver is on, restored background tabs load only when opened (after
// reopening a window and after a restart, Spaces included), and background
// tabs freeze, except pinned ones.
//
//   scripts/e2e/run.sh power
import { spawn } from 'node:child_process';
import { browserSession, connect, evaluate, key, pageTargets, sleep, traceUi, uiMouse,
         uiViews } from '../perf/cdp.mjs';
import { servePages } from '../perf/pages_server.mjs';

const CDP_PORT = process.env.CDP_PORT;
const UI_PORT = process.env.UI_PORT;
const pages = await servePages();
let browser = await browserSession(CDP_PORT);
let relaunched;

let failures = 0;
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
}

async function navigate(target, url) {
  const page = await connect(target.webSocketDebuggerUrl);
  await page.send('Page.navigate', { url });
  page.close();
}
const byId = id => t => t.id === id;
const byUrl = suffix => t => t.url.endsWith(suffix);
const start = (await pageTargets(CDP_PORT))[0];
const CTRL = 2, ALT = 1, SHIFT = 8;
const paintsOf = trace => Object.fromEntries(trace.paints);
const koanTasks = trace =>
  trace.tasks.filter(([from]) => from.startsWith('koan_')).reduce((n, [, c]) => n + c, 0);

// --- Defaults -----------------------------------------------------------

// As Settings > Performance reads it.
await navigate(start, 'chrome://settings/performance');
let memorySaver;
for (let i = 0; i < 20 && memorySaver === undefined; i++) {
  await sleep(500);
  memorySaver = await evaluate(CDP_PORT, byId(start.id), `new Promise(r =>
      chrome.settingsPrivate.getPref('performance_tuning.high_efficiency_mode.state',
                                     p => r(p?.value)))`);
}
check('Memory Saver is on by default', memorySaver === 2, `state ${memorySaver}`);

// --- Restoring tabs on demand ---------------------------------------------

// Each page counts its loads in localStorage["loads:<id>"].
const count = id => `${pages.origin}/power.html?count=${id}`;
async function loads(target) {
  const json = await evaluate(CDP_PORT, target, 'JSON.stringify({...localStorage})');
  return Object.fromEntries(Object.entries(JSON.parse(json ?? '{}'))
      .map(([k, v]) => [k.replace('loads:', ''), Number(v)]));
}
const show = n => Object.entries(n).map(([k, v]) => `${k}: ${v}x`).join(', ');

// Reopening a closed window loads its active tab only.
await navigate(start, count('a'));
for (const id of ['b', 'c', 'd']) {
  await browser.send('Target.createTarget', { url: count(id), background: true });
}
await sleep(3000);
const other = (await browser.send('Target.createTarget',
                                  { url: count('w'), newWindow: true })).result.targetId;
await sleep(2000);
await key(CDP_PORT, 87, CTRL | SHIFT, byId(start.id));  // Ctrl+Shift+W
await sleep(2000);
await key(CDP_PORT, 84, CTRL | SHIFT, byId(other));  // Ctrl+Shift+T
await sleep(5000);
let n = await loads(byId(other));
check('A reopened window loads its active tab', n.a === 2, show(n));
check('...but not its background tabs', n.b === 1 && n.c === 1 && n.d === 1, show(n));
const b = (await pageTargets(CDP_PORT)).find(byUrl('count=b'));
if (b) {
  await browser.send('Target.activateTarget', { targetId: b.id });
  await sleep(3000);
  n = await loads(byId(other));
}
check('A background tab loads when it is opened', n.b === 2, show(n));
if (!(n.a === 2 && n.b === 2)) {
  console.log(`  open pages: ${(await pageTargets(CDP_PORT)).map(t => t.url).join(' ')}`);
}

// After a restart only the active tab loads: not the window's other tabs,
// nor a tab in another Space. Window: r1 (active), r2, r3, and s in a
// Space of its own.
if ((await pageTargets(CDP_PORT)).find(byUrl('count=b'))) {
  await key(CDP_PORT, 87, CTRL | SHIFT, byUrl('count=b'));  // close the reopened window
  await sleep(2000);
}
await navigate((await pageTargets(CDP_PORT)).find(byId(other)), count('r1'));
await sleep(1000);
for (const id of ['r2', 'r3']) {
  await browser.send('Target.createTarget', { url: count(id), background: true });
}
await key(CDP_PORT, 78, CTRL | ALT, byUrl('count=r1'));  // Ctrl+Alt+N: new Space
await sleep(1500);
await browser.send('Target.createTarget', { url: count('s') });  // joins the Space
await sleep(2000);
await browser.send('Target.activateTarget', { targetId: other });  // back to r1
await sleep(2000);
const active = await evaluate(CDP_PORT, byId(other), 'document.visibilityState');
await browser.send('Browser.close');
browser.close();
for (let i = 0; i < 40; i++) {
  await sleep(500);
  if (!await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).catch(() => null)) break;
}
await sleep(2000);
const args = process.env.KOAN_CHROME_ARGS.split('\n').filter(Boolean);
relaunched = spawn(args[0], args.slice(1), { stdio: 'ignore' });
for (let i = 0; i < 60; i++) {
  await sleep(500);
  if (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).catch(() => null)) break;
}
await sleep(5000);
browser = await browserSession(CDP_PORT);
n = await loads(byUrl('count=r1'));
check('After a restart the active tab loads', n.r1 === 2, `r1 was ${active}; ${show(n)}`);
check('...but not the other tabs', n.r2 === 1 && n.r3 === 1, show(n));
check('...nor a tab in another Space', n.s === 1, show(n));

// --- Freezing ---------------------------------------------------------------

// Pin a busy tab first (Ctrl+Shift+D), then open 8 more busy tabs in the
// background. Up to 5 most recently used tabs stay unfrozen, so at least 3
// of the 8 must freeze; the pinned tab, the least recently used, must not.
const home = (await pageTargets(CDP_PORT)).find(byUrl('count=r1'));
const pinned = (await browser.send('Target.createTarget',
                                   { url: `${pages.origin}/power.html?busy&pinned` }))
    .result.targetId;
await sleep(2000);
await key(CDP_PORT, 68, CTRL | SHIFT, byId(pinned));
await sleep(1000);
await browser.send('Target.activateTarget', { targetId: home.id });
for (let i = 0; i < 8; i++) {
  await browser.send('Target.createTarget',
                     { url: `${pages.origin}/power.html?busy&tab=${i}`, background: true });
}
await sleep(15000);
let frozen = 0;
const gaps = [];
let pinnedGap;
for (const t of (await pageTargets(CDP_PORT)).filter(t => t.url.includes('busy'))) {
  await browser.send('Target.activateTarget', { targetId: t.id });
  await sleep(500);
  const gap = Math.round(await evaluate(CDP_PORT, byId(t.id), 'maxGap') ?? -1);
  if (t.id === pinned) {
    pinnedGap = gap;
  } else {
    gaps.push(gap);
    if (gap > 3000) frozen++;
  }
  await browser.send('Target.closeTarget', { targetId: t.id });
}
check('Busy background tabs beyond the 5 most recent are frozen', frozen >= 3,
      `${frozen} of 8 paused > 3 s; longest pauses (ms): ${gaps.join(', ')}`);
check('A pinned tab is never frozen', pinnedGap >= 0 && pinnedGap < 3000,
      `longest pause ${pinnedGap} ms`);

// --- Koan's views (after the restart) -------------------------------------

const first = (await pageTargets(CDP_PORT)).find(byUrl('count=r1'));

// Single toolbar (the default) slides the toolbar out of the window; it must
// not repaint there when the page changes (here: its title, 4 times a
// second). The tab in the sidebar does repaint.
await navigate(first, `${pages.origin}/power.html?title`);
await sleep(2000);
let paints = paintsOf(await traceUi(browser, () => sleep(5000)));
check('The hidden toolbar does not repaint', !paints.ToolbarView && !paints.LocationBarView,
      `toolbar ${paints.ToolbarView ?? 0}, location bar ${paints.LocationBarView ?? 0}, ` +
      `tab ${paints.TabView ?? 0} paints in 5 s`);

// Compact mode: a sidebar opened with Ctrl+Alt+S hides again without Koan
// polling (it used to check the pointer every 100 ms while open).
await key(CDP_PORT, 83, CTRL, byId(first.id));  // Ctrl+S
await sleep(1500);
const peek = await traceUi(browser, async () => {
  await key(CDP_PORT, 83, CTRL | ALT, byId(first.id));  // Ctrl+Alt+S
  await sleep(6000);
});
check('A peeked sidebar hides again without polling', koanTasks(peek) <= 3,
      `${koanTasks(peek)} Koan task(s) in 6 s; ${paintsOf(peek).TabView ?? 0} tab paints ` +
      'show the sidebar opened');

// Compact mode with multiple toolbars: the toolbar slides in from the top
// edge and away again; once away it must stop painting too (its layer is
// hidden when the slide-out animation ends).
await key(CDP_PORT, 66, CTRL | ALT, byId(first.id));  // Ctrl+Alt+B
await sleep(1500);
const zones = (await uiViews(UI_PORT))
    .filter(v => v.cls === 'KoanHotZone' && v.shown && v.w > v.h);
const reveal = await traceUi(browser, async () => {
  if (zones[0]) await uiMouse(UI_PORT, zones[0], 'mouseEntered');
  await sleep(2000);
});
paints = paintsOf(await traceUi(browser, () => sleep(4000)));
check('The toolbar slid back out does not repaint',
      paintsOf(reveal).ToolbarView > 0 && !paints.ToolbarView && !paints.LocationBarView,
      `${paintsOf(reveal).ToolbarView ?? 0} toolbar paints while revealed; then toolbar ` +
      `${paints.ToolbarView ?? 0}, location bar ${paints.LocationBarView ?? 0}`);
await key(CDP_PORT, 66, CTRL | ALT, byId(first.id));  // back to single toolbar
await key(CDP_PORT, 83, CTRL, byId(first.id));  // compact mode off
await sleep(1500);

browser.close();
pages.close();
if (relaunched) {
  const exited = new Promise(r => relaunched.once('exit', r));
  relaunched.kill();
  await Promise.race([exited, sleep(5000)]);
}
console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
