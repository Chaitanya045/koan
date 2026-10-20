// End-to-end test of Koan's power and memory defaults against a real build:
// Memory Saver and energy saver defaults, freezing of background tabs, and
// restored background tabs that load only when opened (like Zen).
//
//   scripts/e2e/run.sh power
import { browserSession, connect, evaluate, key, pageTargets, sleep, traceUi } from '../perf/cdp.mjs';
import { servePages } from '../perf/pages_server.mjs';

const CDP_PORT = process.env.CDP_PORT;
const pages = await servePages();
const browser = await browserSession(CDP_PORT);

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
const first = (await pageTargets(CDP_PORT))[0];

// Single toolbar (the default) slides the toolbar out of the window; it must
// not repaint there when the page changes (here: its title, 4 times a
// second). The tabs in the sidebar do repaint.
await navigate(first, `${pages.origin}/power.html?title`);
await sleep(2000);
const ui = await traceUi(browser, () => sleep(5000));
const paints = Object.fromEntries(ui.paints);
check('The hidden toolbar does not repaint', !paints.ToolbarView && !paints.LocationBarView,
      `toolbar ${paints.ToolbarView ?? 0}, location bar ${paints.LocationBarView ?? 0}, ` +
      `tabs ${paints.TabView ?? 0} paints in 5 s`);

// Defaults, as Settings > Performance reads them.
await navigate(first, 'chrome://settings/performance');
const pref = async name => {
  for (let i = 0; i < 20; i++) {
    const value = await evaluate(CDP_PORT, byId(first.id), `
        new Promise(r => chrome.settingsPrivate.getPref('${name}', p => r(p?.value)))`);
    if (value !== undefined) return value;
    await sleep(500);
  }
  console.log(`  could not read ${name} on ${await evaluate(
      CDP_PORT, byId(first.id), 'location.href')}`);
};
const memorySaver = await pref('performance_tuning.high_efficiency_mode.state');
check('Memory Saver is on by default', memorySaver === 2, `state ${memorySaver}`);
const energySaver = await pref('performance_tuning.battery_saver_mode.state');
check('Energy saver turns on whenever the system saves power', energySaver === 2,
      `state ${energySaver}`);

// Reopening a closed window loads its active tab only; the others load
// when first shown. Each page counts its loads in localStorage.
const count = id => `${pages.origin}/power.html?count=${id}`;
await navigate(first, count('a'));
for (const id of ['b', 'c', 'd']) {
  await browser.send('Target.createTarget', { url: count(id), background: true });
}
await sleep(3000);
const other = (await browser.send('Target.createTarget',
                                  { url: `${pages.origin}/power.html`, newWindow: true }))
    .result.targetId;
await sleep(2000);
await key(CDP_PORT, 87, 2 | 8, byId(first.id));  // Ctrl+Shift+W: close the window
await sleep(2000);
await key(CDP_PORT, 84, 2 | 8, byId(other));  // Ctrl+Shift+T: reopen it
await sleep(5000);
// Load counts by page id, e.g. {a: 2, b: 1}.
const loads = async () => Object.fromEntries(
    Object.entries(JSON.parse(await evaluate(
        CDP_PORT, byId(other), 'JSON.stringify({...localStorage})') ?? '{}'))
        .map(([k, v]) => [k, Number(v)]));
let n = await loads();
check('The reopened window loads its active tab', n['loads:a'] === 2,
      `a loaded ${n['loads:a']}x`);
check('...but not its background tabs', ['b', 'c', 'd'].every(id => n[`loads:${id}`] === 1),
      ['b', 'c', 'd'].map(id => `${id}: ${n[`loads:${id}`]}x`).join(', '));
const restoredB = (await pageTargets(CDP_PORT)).find(t => t.url.endsWith('count=b'));
if (restoredB) {
  await browser.send('Target.activateTarget', { targetId: restoredB.id });
  await sleep(3000);
  n = await loads();
}
check('A background tab loads when it is opened', n['loads:b'] === 2,
      restoredB ? `b loaded ${n['loads:b']}x` : 'restored tab b not found');

// Background tabs beyond the 5 most recently used freeze: their busy
// dedicated workers (timers that throttling doesn't slow) stop.
for (let i = 0; i < 8; i++) {
  await browser.send('Target.createTarget',
                     { url: `${pages.origin}/power.html?busy&tab=${i}`, background: true });
}
await sleep(15000);
let frozen = 0;
const gaps = [];
for (const t of (await pageTargets(CDP_PORT)).filter(t => t.url.includes('busy'))) {
  await browser.send('Target.activateTarget', { targetId: t.id });
  await sleep(500);
  const gap = await evaluate(CDP_PORT, byId(t.id), 'maxGap');
  gaps.push(Math.round(gap ?? -1));
  if (gap > 3000) frozen++;
  await browser.send('Target.closeTarget', { targetId: t.id });
}
check('Busy background tabs beyond the 5 most recent are frozen', frozen >= 3,
      `${frozen} of 8 paused > 3 s; longest pauses (ms): ${gaps.join(', ')}`);

browser.close();
pages.close();
console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
