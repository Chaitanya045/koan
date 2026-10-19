// End-to-end test of Koan's keyboard shortcuts against a real build.
//
//   scripts/e2e/run.sh shortcuts
//
// Expects Koan running off-screen with UI DevTools on UI_PORT and the
// DevTools protocol on CDP_PORT (run.sh starts it). Keys are sent to the
// page, so unhandled ones reach the browser's accelerators exactly like real
// input. Prints one PASS/FAIL line per check; exits non-zero on any failure.
const UI_PORT = process.env.UI_PORT;
const CDP_PORT = process.env.CDP_PORT;
const sleep = ms => new Promise(r => setTimeout(r, ms));

// --- protocol helpers --------------------------------------------------------
async function connect(url) {
  const ws = new WebSocket(url);
  let id = 0;
  const pending = new Map();
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  };
  await new Promise(r => (ws.onopen = r));
  // Times out instead of hanging when a shortcut closes this target.
  const send = (method, params = {}) => new Promise(resolve => {
    pending.set(++id, resolve);
    ws.send(JSON.stringify({ id, method, params }));
    setTimeout(() => resolve({ timedOut: true }), 5000);
  });
  return { send, close: () => ws.close() };
}

async function pages() {
  return (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .filter(t => t.type === 'page');
}

async function activePage() {
  // The first listed page is the most recently active one.
  return (await pages())[0];
}

// Collects every native view as {cls, visible, w, h} (UI DevTools).
async function views() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const out = [];
  const walk = async (n, depth) => {
    const a = n.attributes ?? [];
    const cls = a[a.indexOf('class') + 1];
    if (depth > 0 && n.nodeName === 'View' && cls) {
      const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
      const p = {};
      for (const rule of r.result?.matchedCSSRules ?? []) {
        for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
      }
      out.push({ cls, visible: p.Visible !== 'false', w: +p.Width, h: +p.Height });
    }
    for (const c of n.children ?? []) await walk(c, depth + 1);
  };
  await walk(doc, 0);
  ui.close();
  return out;
}

const count = (vs, cls) => vs.filter(v => v.cls === cls && v.visible).length;

// Sends a key press to the active page. Modifiers: Alt=1 Ctrl=2 Shift=8.
async function key(code, modifiers, keyName = '') {
  const page = await activePage();
  const c = await connect(page.webSocketDebuggerUrl);
  const base = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers, key: keyName };
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
  c.close();
  await sleep(1200);
}

async function evalIn(page, expression) {
  const c = await connect(page.webSocketDebuggerUrl);
  const r = await c.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  c.close();
  return r.result?.result?.value;
}

async function browserCall(method, params = {}) {
  const v = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
  const c = await connect(v.webSocketDebuggerUrl);
  const r = await c.send(method, params);
  c.close();
  return r;
}

// Reads the clipboard by pasting (Ctrl+V) into a fresh textarea on the page.
async function clipboard() {
  const page = await activePage();
  await evalIn(page, `(() => {
    document.getElementById('koan-paste')?.remove();
    const t = document.createElement('textarea');
    t.id = 'koan-paste';
    document.body.append(t);
    t.focus();
  })()`);
  await key(86, 2, 'v');
  const value = await evalIn(page,
      `(() => { const t = document.getElementById('koan-paste');
                const v = t.value; t.remove(); return v; })()`);
  return value;
}

// --- checks ------------------------------------------------------------------
let failures = 0;
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
}

const CTRL = 2, ALT = 1, SHIFT = 8;
const VK = { S: 83, C: 67, D: 68, K: 75, V: 86, H: 72, U: 85, N: 78, B: 66, RIGHT: 39 };

// Compact mode: Ctrl+S on, Ctrl+Alt+S peeks the sidebar, Ctrl+S off.
await key(VK.S, CTRL, 's');
let vs = await views();
// With single toolbar (the default) compact mode hides only the sidebar,
// like Zen: hot zones for the sidebar and the window buttons' strip, none
// for a toolbar.
check('Ctrl+S turns compact mode on', count(vs, 'KoanHotZone') === 2 &&
      vs.find(v => v.cls === 'VerticalTabStripRegionView')?.w === 0);
await key(VK.S, CTRL | ALT, 's');
vs = await views();
const strip = vs.find(v => v.cls === 'VerticalTabStripRegionView');
check('Ctrl+Alt+S shows the sidebar in compact mode', strip && strip.w >= 200, `width ${strip?.w}`);
await sleep(4500);
vs = await views();
const after = vs.find(v => v.cls === 'VerticalTabStripRegionView');
check('...and hides it again when the pointer stays away', after && after.w === 0, `width ${after?.w}`);
await key(VK.S, CTRL, 's');
vs = await views();
check('Ctrl+S turns compact mode off', count(vs, 'KoanHotZone') === 1);

// Copy URL / as Markdown.
await key(VK.C, CTRL | SHIFT, 'c');
let clip = await clipboard();
check('Ctrl+Shift+C copies the URL', clip === 'https://example.com/', JSON.stringify(clip));
await key(VK.C, CTRL | SHIFT | ALT, 'c');
clip = await clipboard();
check('Ctrl+Shift+Alt+C copies a Markdown link',
      clip === '[Example Domain](https://example.com/)', JSON.stringify(clip));

// Pin / unpin.
await key(VK.D, CTRL | SHIFT, 'd');
vs = await views();
const pinnedAfter = vs.some(v => v.cls === 'PinnedTabContainerView' && v.h > 0 && v.visible);
check('Ctrl+Shift+D pins the tab', pinnedAfter);
await key(VK.D, CTRL | SHIFT, 'd');

// Split view: vertical, horizontal (layout switch), unsplit.
await key(VK.V, CTRL | ALT, 'v');
vs = await views();
check('Ctrl+Alt+V splits side by side', count(vs, 'SplitTabView') === 1 &&
      count(vs, 'ContentsContainerView') === 2);
await key(VK.H, CTRL | ALT, 'h');
vs = await views();
const panes = vs.filter(v => v.cls === 'ContentsContainerView' && v.visible);
check('Ctrl+Alt+H stacks the split', panes.length === 2 && panes[0].w === panes[1].w &&
      panes[0].h < 500, panes.map(p => `${p.w}x${p.h}`).join(' '));
await key(VK.U, CTRL | ALT, 'u');
vs = await views();
check('Ctrl+Alt+U unsplits', count(vs, 'SplitTabView') === 0);

// Close all unpinned tabs: leaves one fresh tab.
await key(VK.K, CTRL | SHIFT, 'k');
let ps = await pages();
check('Ctrl+Shift+K closes unpinned tabs', ps.length === 1, `${ps.length} page(s)`);

// New window.
const windowsBefore = (await browserCall('Browser.getWindowForTarget', {
  targetId: (await activePage()).id })).result?.windowId;
await key(VK.N, CTRL | SHIFT, 'n');
ps = await pages();
const ids = new Set();
for (const p of ps) {
  const w = (await browserCall('Browser.getWindowForTarget', { targetId: p.id })).result?.windowId;
  ids.add(w);
}
check('Ctrl+Shift+N opens a new window', ids.size === 2 && ids.has(windowsBefore), `${ids.size} window(s)`);

console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
