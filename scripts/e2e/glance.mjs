// End-to-end test of Glance against a real build: Alt+click opens it,
// Ctrl+O expands it to a tab, Esc closes it, Shift+click still opens a
// window, and download links are left alone.
//
//   scripts/e2e/run.sh glance        (opens scripts/e2e/pages/glance.html)
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
  await new Promise(r => (ws.onopen = r));
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

// Clicks the element with `id` in the test page. Modifiers: Alt=1 Shift=8.
async function clickLink(id, modifiers) {
  const page = (await tabs()).find(t => t.url.endsWith('/glance.html'));
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

async function keyTo(target, code, modifiers, key = '') {
  await withPage(target, async c => {
    const base = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers, key };
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

const ALT = 1, CTRL = 2, SHIFT = 8;

// Alt+click opens Glance; Esc (sent to the Glance page) closes it.
await clickLink('glance', ALT);
check('Alt+click opens Glance', await glanceOpen());
const glancePage = (await targets()).find(t => t.url.includes('glance=1'));
check('Glance hosts the linked page', !!glancePage, glancePage?.url ?? 'no target');
if (glancePage) {
  await keyTo(glancePage, 27, 0, 'Escape');
  check('Esc closes Glance', !(await glanceOpen()));
}

// Alt+click again, then Ctrl+O expands it into a tab.
const before = (await tabs()).length;
await clickLink('glance', ALT);
const glance2 = (await targets()).find(t => t.url.includes('glance=1'));
if (glance2) {
  await keyTo(glance2, 79, CTRL, 'o');
}
const after = await tabs();
check('Ctrl+O expands Glance into a tab',
      !(await glanceOpen()) && after.length === before + 1 &&
          after.some(t => t.url.includes('glance=1')),
      `${before} -> ${after.length} tab(s)`);

// Shift+click opens a new window, as in Zen/Firefox, not Glance.
const windows = await windowCount();
await clickLink('glance', SHIFT);
check('Shift+click opens a new window', !(await glanceOpen()) &&
      (await windowCount()) === windows + 1);

// Alt+click on a download link is not a Glance.
await clickLink('download', ALT);
check('Alt+click on a download link does not open Glance', !(await glanceOpen()));

// A synthesized Alt+click (script, no user gesture) opens neither Glance
// nor a popup window. Last: it may navigate the test page away.
const windowsBefore = await windowCount();
const testPage = (await tabs()).find(t => t.url.endsWith('/glance.html'));
await withPage(testPage, c => c.send('Runtime.evaluate', { expression: 'fakeAltClick()' }));
await sleep(2000);
check('A scripted Alt+click opens neither Glance nor a popup',
      !(await glanceOpen()) && (await windowCount()) === windowsBefore);

console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
