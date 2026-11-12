// End-to-end test of Glance against a real build: Alt+click opens it,
// Ctrl+O (Cmd+O on macOS: Zen's zen-glance-expand is accel+O) expands it to
// a tab, Esc closes it, Shift+click still opens a window, and download links
// are left alone.
//
//   scripts/e2e/run.sh glance        (opens scripts/e2e/pages/glance.html)
//   scripts/mac/suite.sh --only glance <Koan.app>
import { ACCEL, IS_MAC, sendKey } from './lib/koan.mjs';
import { makeKey, realInput, realInputWhyNot, realKey, skipped } from './lib/mac.mjs';

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

// The size of the Glance card's WebView (UI DevTools), as "WxH".
async function glanceCardSize() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  let webView = null;
  (function walk(n, inGlance) {
    const a = n.attributes ?? [];
    const cls = a[a.indexOf('class') + 1];
    inGlance = inGlance || cls === 'KoanGlanceView';
    if (inGlance && cls === 'WebView') webView = n;
    (n.children ?? []).forEach(c => walk(c, inGlance));
  })(doc, false);
  let size = null;
  if (webView) {
    const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: webView.nodeId });
    const p = {};
    for (const rule of r.result?.matchedCSSRules ?? []) {
      for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
    }
    size = `${p.Width}x${p.Height}`;
  }
  ui.close();
  return size;
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

// Presses a key in `target`'s page (Windows key code `code`; lib/koan.mjs
// sends the Mac key code and characters on macOS).
async function keyTo(target, code, modifiers, key = '') {
  await withPage(target, c => sendKey(c, code, modifiers, key || undefined));
  await sleep(1500);
}

let failures = 0;
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
}

const ALT = 1, SHIFT = 8;
const EXPAND = IS_MAC ? 'Cmd+O' : 'Ctrl+O';

// Closes a Glance a failed check left open, so the next checks start clean.
// (Esc to every page of the link: the Glance page is one, an expanded tab
// may be another.)
async function closeLeftover() {
  if (!(await glanceOpen())) return;
  for (const page of (await targets()).filter(t => t.url.includes('glance=1'))) {
    await keyTo(page, 27, 0, 'Escape');
  }
}

// Alt+click opens Glance; Esc (sent to the Glance page) closes it.
await clickLink('glance', ALT);
check('Alt+click opens Glance', await glanceOpen());
const glancePage = (await targets()).find(t => t.url.includes('glance=1'));
check('Glance hosts the linked page', !!glancePage, glancePage?.url ?? 'no target');
if (glancePage) {
  // The page fills its card once the open animation is over (it used to
  // keep the animation's starting 94% size). Poll until the size settles.
  const pageSize = () => withPage(glancePage, async c => (await c.send('Runtime.evaluate', {
    expression: '`${innerWidth}x${innerHeight}`', returnByValue: true,
  })).result?.result?.value);
  let page = await pageSize();
  for (let i = 0; i < 20; i++) {
    await sleep(100);
    const next = await pageSize();
    if (next === page) break;
    page = next;
  }
  const card = await glanceCardSize();
  check('The Glance page fills its card', card && page === card, `page ${page}, card ${card}`);
  await keyTo(glancePage, 27, 0, 'Escape');
  check('Esc closes Glance', !(await glanceOpen()));
}

// Alt+click again, then Ctrl+O (Cmd+O) expands it into a tab.
const before = (await tabs()).length;
await clickLink('glance', ALT);
const glance2 = (await targets()).find(t => t.url.includes('glance=1'));
if (glance2) {
  await keyTo(glance2, 79, ACCEL, 'o');
}
const after = await tabs();
check(`${EXPAND} expands Glance into a tab`,
      !(await glanceOpen()) && after.length === before + 1 &&
          after.some(t => t.url.includes('glance=1')),
      `${before} -> ${after.length} tab(s)`);
await closeLeftover();

// On macOS a real Cmd+O goes through AppKit first (the key window, then the
// menu bar, whose File > Open File Koan leaves without the key): it expands
// the Glance as well, and opens no file dialog.
if (IS_MAC) {
  if (!realInput()) {
    skipped('A real Cmd+O expands Glance into a tab', realInputWhyNot());
  } else {
    // keypress2 posts to Koan's pid: the window has to be key.
    await makeKey((await tabs()).find(t => t.url.endsWith('/glance.html')));
    const realBefore = (await tabs()).length;
    await clickLink('glance', ALT);
    const opened = await glanceOpen();
    if (!(await realKey('o', 'c', 1500))) {
      skipped('A real Cmd+O expands Glance into a tab', realInputWhyNot());
    } else {
      const realAfter = (await tabs()).length;
      check('A real Cmd+O expands Glance into a tab',
            opened && !(await glanceOpen()) && realAfter === realBefore + 1,
            `glance opened ${opened}, ${realBefore} -> ${realAfter} tab(s)`);
    }
    await closeLeftover();
  }
}

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
