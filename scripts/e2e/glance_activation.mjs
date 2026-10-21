// End-to-end test of the Glance trigger method (koan.glance.activation_method,
// seeded to Ctrl + Click by scripts/e2e/prefs/glance_activation.json):
// Ctrl+click opens Glance, Shift+click and Alt+click keep their normal
// meanings, the settings page shows and changes the method (for pages in
// new renderer processes), and a plain click navigates as usual.
//
//   scripts/e2e/run.sh glance_activation
//   (loads scripts/e2e/pages/glance.html)
import { readPref } from './lib/koan.mjs';

const UI_PORT = process.env.UI_PORT;
const CDP_PORT = process.env.CDP_PORT;
const sleep = ms => new Promise(r => setTimeout(r, ms));

async function connect(url) {
  const ws = new WebSocket(url);
  let id = 0;
  const pending = new Map();
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  };
  await new Promise((resolve, reject) => {
    ws.onopen = resolve;
    ws.onerror = reject;
    setTimeout(() => reject(new Error(`no connection to ${url}`)), 5000);
  });
  const send = (method, params = {}) => new Promise(resolve => {
    pending.set(++id, resolve);
    ws.send(JSON.stringify({ id, method, params }));
    setTimeout(() => resolve({ timedOut: true }), 5000);
  });
  return { send, close: () => ws.close() };
}

const targets = async () =>
  (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json());
const tabs = async () => (await targets()).filter(t => t.type === 'page');

async function glanceOpen() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  ui.close();
  let found = false;
  (function walk(n) {
    const a = n.attributes ?? [];
    if (a[a.indexOf('class') + 1] === 'KoanGlanceView') found = true;
    (n.children ?? []).forEach(walk);
  })(doc);
  return found;
}

async function windowCount() {
  const v = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
  const c = await connect(v.webSocketDebuggerUrl);
  const ids = new Set();
  for (const t of await tabs()) {
    const r = await c.send('Browser.getWindowForTarget', { targetId: t.id });
    if (r.result) ids.add(r.result.windowId);
  }
  c.close();
  return ids.size;
}

async function withPage(target, fn) {
  const c = await connect(target.webSocketDebuggerUrl);
  try { return await fn(c); } finally { c.close(); }
}

// Clicks the element with `id` in the (first) test page. Ctrl=2, Shift=8,
// Alt=1.
async function clickLink(id, modifiers,
                         page = undefined) {
  page ??= (await tabs()).find(t => t.url.endsWith('/glance.html'));
  await withPage(page, async c => {
    const r = await c.send('Runtime.evaluate', {
      expression: `JSON.stringify(document.getElementById('${id}').getBoundingClientRect())`,
      returnByValue: true,
    });
    const box = JSON.parse(r.result.result.value);
    const base = { x: box.x + 10, y: box.y + 10, modifiers, button: 'left', clickCount: 1 };
    await c.send('Input.dispatchMouseEvent', { ...base, type: 'mousePressed' });
    await c.send('Input.dispatchMouseEvent', { ...base, type: 'mouseReleased' });
  });
  await sleep(2000);
}

// Sends a plain key to the page target (used for Esc).
async function keyTo(target, code, key = '') {
  await withPage(target, async c => {
    const base = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, key };
    await c.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
    await c.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
  });
  await sleep(1500);
}

let failures = 0;
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
}

// The test page is glance.mjs's (run.sh opens example.com tabs for a test
// without a page of its own).
await withPage((await tabs())[0], c => c.send('Page.navigate', {
  url: new URL('pages/glance.html', import.meta.url).href,
}));
await sleep(2000);

// Ctrl+click opens Glance (the activation method), and Esc closes it.
await clickLink('glance', 2);
check('Ctrl+click opens Glance', await glanceOpen());
const glancePage = (await targets()).find(t => t.url.includes('glance=1'));
check('Glance hosts the linked page', !!glancePage, glancePage?.url ?? 'no target');
if (glancePage) {
  await keyTo(glancePage, 27, 'Escape');
  check('Esc closes Glance', !(await glanceOpen()));
}

// Shift+click opens a new window, as in Zen/Firefox, not Glance.
const windows = await windowCount();
await clickLink('glance', 8);
check('Shift+click opens a new window', !(await glanceOpen()) &&
      (await windowCount()) === windows + 1);

// Alt+click is no longer the trigger: no Glance, no popup. It goes back to
// being Chromium's alt+click (a download for same-site links).
await clickLink('glance', 1);
check('Alt+click does not open Glance',
      !(await glanceOpen()) && (await windowCount()) === windows + 1);

// Ctrl+click on a link Glance doesn't open (not a web link) does what
// Ctrl+click does without Glance: a background tab, not a popup window.
{
  const tabsBefore = (await tabs()).length;
  const windowsBefore = await windowCount();
  await clickLink('local', 2);
  check('Ctrl+click on a file link opens a background tab',
        !(await glanceOpen()) && (await tabs()).length === tabsBefore + 1 &&
            (await windowCount()) === windowsBefore,
        `${tabsBefore} -> ${(await tabs()).length} tab(s), ` +
            `${windowsBefore} -> ${await windowCount()} window(s)`);
  const opened = (await tabs()).find(t => t.url.endsWith('glance.html#local'));
  if (opened) {
    const b = await connect((await (await fetch(
        `http://127.0.0.1:${CDP_PORT}/json/version`)).json()).webSocketDebuggerUrl);
    await b.send('Target.closeTarget', { targetId: opened.id });
    b.close();
    await sleep(500);
  }
}

// Ctrl+click on a download link stays a download, like Alt+click does with
// the default method (no glance).
await clickLink('download', 2);
check('Ctrl+click on a download link does not open Glance', !(await glanceOpen()));

// The settings page shows the method and changes it. Renderers get it when
// they start, so a page opened afterwards in a new process uses it.
const browser = await connect(
    (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json())
        .webSocketDebuggerUrl);
const settingsId = (await browser.send('Target.createTarget',
    { url: 'chrome://koan-settings' })).result.targetId;
await sleep(2000);
const settings = (await targets()).find(t => t.id === settingsId);
const trigger = `document.querySelector(
    '[data-pref="koan.glance.activation_method"] select')`;
const shownMethod = await withPage(settings, async c => (await c.send('Runtime.evaluate', {
  expression: `${trigger}?.selectedOptions[0]?.textContent`, returnByValue: true,
})).result?.result?.value);
check('Settings show the trigger method', shownMethod === 'Ctrl + Click', shownMethod);
await withPage(settings, c => c.send('Runtime.evaluate', {
  expression: `(() => { const s = ${trigger};
      s.selectedIndex = [...s.options].findIndex(o => o.text === 'Alt + Click');
      s.dispatchEvent(new Event('change')); })()`,
}));
await sleep(1000);
const method = await readPref('koan.glance.activation_method');
check('...and changes it', method === 'alt', method);
await browser.send('Target.closeTarget', { targetId: settingsId });
// Chrome keeps a spare renderer process ready, started before the change;
// a throwaway page (another site) takes it, so the test page gets a process
// started after.
const spare = (await browser.send('Target.createTarget',
    { url: 'https://example.com/?spare' })).result.targetId;
await sleep(1500);
await browser.send('Target.closeTarget', { targetId: spare });
// Not another file:// page: Chrome reuses a same-site renderer for new
// pages (process-per-site up to a threshold), here the first page's.
const fresh = (await browser.send('Target.createTarget', {
  url: 'data:text/html,<a id="glance" href="https://example.com/?glance=2" ' +
      'style="display:block;width:300px;height:40px">glance</a>',
})).result.targetId;
await sleep(2000);
const seen = new Set((await targets()).map(t => t.id));
await clickLink('glance', 1, (await targets()).find(t => t.id === fresh));
check('...set to Alt + Click there, Alt+click opens Glance in a new page',
      await glanceOpen());
const altGlance = (await targets()).find(
    t => t.url.includes('glance=2') && !seen.has(t.id));
if (altGlance) {
  await keyTo(altGlance, 27, 'Escape');
}
await browser.send('Target.closeTarget', { targetId: fresh });
await sleep(1000);

// "Enable Glance" off: an Alt+click in a page opened afterwards is Chromium's
// again (no Glance, no popup window).
const settings2 = (await browser.send('Target.createTarget',
    { url: 'chrome://koan-settings' })).result.targetId;
await sleep(2000);
await withPage((await targets()).find(t => t.id === settings2), c => c.send(
    'Runtime.evaluate', { expression: `document.querySelector(
        '[data-pref="koan.glance.enabled"] input').click()` }));
await sleep(1000);
check('"Enable Glance" turns Glance off',
      (await readPref('koan.glance.enabled')) === false);
await browser.send('Target.closeTarget', { targetId: settings2 });
const spare2 = (await browser.send('Target.createTarget',
    { url: 'https://example.com/?spare2' })).result.targetId;
await sleep(1500);
await browser.send('Target.closeTarget', { targetId: spare2 });
const off = (await browser.send('Target.createTarget', {
  url: 'data:text/html,<a id="glance" href="https://example.com/?glance=3" ' +
      'style="display:block;width:300px;height:40px">glance</a>',
})).result.targetId;
await sleep(2000);
const windowsOff = await windowCount();
await clickLink('glance', 1, (await targets()).find(t => t.id === off));
check('...an Alt+click then opens neither Glance nor a popup',
      !(await glanceOpen()) && (await windowCount()) === windowsOff);
await browser.send('Target.closeTarget', { targetId: off });
await sleep(1000);
browser.close();

// A plain click navigates the tab as usual (and the test page is gone, so
// no further clicks happen after this).
const before = await tabs();
await clickLink('glance', 0);
const after = await tabs();
check('A plain click navigates the tab, no Glance',
      !(await glanceOpen()) && after.length === before.length &&
          before.some(t => t.url.startsWith('file://')) &&
          after.some(t => t.url.includes('glance=1')),
      `${before.length} -> ${after.length} tab(s)`);

console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
