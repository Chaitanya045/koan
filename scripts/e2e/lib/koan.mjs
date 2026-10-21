// Helpers for Koan's end-to-end tests: the DevTools protocol (pages, keys,
// clicks, prefs) and UI DevTools (native views). Koan runs as started by
// scripts/e2e/run.sh, with UI DevTools on UI_PORT and the DevTools protocol
// on CDP_PORT.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';

export const UI_PORT = process.env.UI_PORT;
export const CDP_PORT = process.env.CDP_PORT;
export const sleep = ms => new Promise(r => setTimeout(r, ms));

// Key modifiers for Input.dispatchKeyEvent.
export const ALT = 1, CTRL = 2, META = 4, SHIFT = 8;

export async function connect(url) {
  const ws = new WebSocket(url);
  let id = 0;
  const pending = new Map();
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  };
  await new Promise(r => (ws.onopen = r));
  // Times out instead of hanging when a command closes this target.
  const send = (method, params = {}) => new Promise(resolve => {
    pending.set(++id, resolve);
    ws.send(JSON.stringify({ id, method, params }));
    setTimeout(() => resolve({ timedOut: true }), 5000);
  });
  return { send, close: () => ws.close() };
}

export async function pages() {
  return (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .filter(t => t.type === 'page');
}

export async function browserCall(method, params = {}) {
  const v = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
  const c = await connect(v.webSocketDebuggerUrl);
  const r = await c.send(method, params);
  c.close();
  return r;
}

export async function onPage(page, fn) {
  const c = await connect(page.webSocketDebuggerUrl);
  try {
    return await fn(c);
  } finally {
    c.close();
  }
}

export async function evalIn(page, expression, userGesture = false) {
  const r = await onPage(page, c => c.send('Runtime.evaluate',
      { expression, returnByValue: true, awaitPromise: true, userGesture }));
  return r.result?.result?.value;
}

// Polls `fn` until it returns something truthy; returns that, or the last
// value after `timeout` ms.
export async function waitFor(fn, timeout = 5000) {
  const end = Date.now() + timeout;
  let value;
  while (!(value = await fn()) && Date.now() < end) {
    await sleep(200);
  }
  return value;
}

export async function front(page) {
  await onPage(page, c => c.send('Page.bringToFront'));
  await sleep(500);
}

// Sends a key press to `page`; keys the page doesn't handle reach the
// browser's accelerators like real input.
export async function key(page, code, modifiers, keyName) {
  await onPage(page, async c => {
    const base = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers,
                   key: keyName };
    await c.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
    await c.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
  });
  await sleep(300);
}

// Clicks the element `selector` in `page` with real (trusted) mouse events.
export async function click(page, selector) {
  const box = await evalIn(page, `(() => {
    const r = document.querySelector(${JSON.stringify(selector)}).getBoundingClientRect();
    return { x: r.x + r.width / 2, y: r.y + r.height / 2 };
  })()`);
  await onPage(page, async c => {
    for (const type of ['mousePressed', 'mouseReleased']) {
      await c.send('Input.dispatchMouseEvent',
                   { type, x: box.x, y: box.y, button: 'left', clickCount: 1 });
    }
  });
  await sleep(300);
}

// The page that is showing (the active tab of its window).
export async function visiblePage() {
  for (const page of await pages()) {
    if (await evalIn(page, 'document.visibilityState') === 'visible') {
      return page;
    }
  }
  return null;
}

// Calls chrome://koan-settings' handler from `page` (a settings tab), as the
// page itself does.
export function settingsCall(page, method, ...args) {
  return evalIn(page, `import('chrome://resources/js/cr.js').then(
      m => m.sendWithPromise(${JSON.stringify(method)},
                             ...${JSON.stringify(args)}))`);
}

// A profile pref's value (its default if unset), from
// chrome://prefs-internals/<pref>.
export async function readPref(pref) {
  const target = (await browserCall('Target.createTarget',
      { url: `chrome://prefs-internals/${pref}`, background: true })).result.targetId;
  await sleep(500);
  const page = (await pages()).find(p => p.id === target);
  const text = await waitFor(() => evalIn(page, 'document.body?.innerText'));
  await browserCall('Target.closeTarget', { targetId: target });
  return JSON.parse(text);
}

// Every native view as {cls, shown, w, h, x, y, nodeId, path}; `shown`
// includes the ancestors', x/y are in the view's widget, `path` lists the
// ancestors' classes from the widget's root view.
export async function views() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const out = [];
  const walk = async (n, depth, shown, path, ox, oy) => {
    const a = n.attributes ?? [];
    const cls = a[a.indexOf('class') + 1];
    if (depth > 0 && n.nodeName === 'View' && cls) {
      const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
      const p = {};
      for (const rule of r.result?.matchedCSSRules ?? []) {
        for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
      }
      shown = shown && p.Visible !== 'false';
      const v = { cls, shown, w: +p.Width, h: +p.Height, x: ox + +p.X, y: oy + +p.Y,
                  nodeId: n.nodeId, path };
      out.push(v);
      path = [...path, cls];
      ox = v.x;
      oy = v.y;
    } else if (n.nodeName === 'Widget') {
      [path, ox, oy] = [[], 0, 0];
    }
    for (const c of n.children ?? []) await walk(c, depth + 1, shown, path, ox, oy);
  };
  await walk(doc, 0, true, [], 0, 0);
  ui.close();
  return out;
}

// Just the bounds (in the parent view's coordinates) of the views of class
// `cls`: quicker than views() for timing-sensitive checks.
export async function boundsOf(cls) {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const nodes = [];
  const walk = n => {
    const a = n.attributes ?? [];
    if (n.nodeName === 'View' && a[a.indexOf('class') + 1] === cls) nodes.push(n);
    for (const c of n.children ?? []) walk(c);
  };
  walk(doc);
  const out = [];
  for (const n of nodes) {
    const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
    const p = {};
    for (const rule of r.result?.matchedCSSRules ?? []) {
      for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
    }
    out.push({ x: +p.X, y: +p.Y, w: +p.Width, h: +p.Height });
  }
  ui.close();
  return out;
}

// Sends a native mouse event to `view` (from views()) at `x`, `y` in its own
// coordinates. The view gets it directly; sent to a root view, it is routed
// like real input.
export async function mouseOn(view, type, x = 10, y = 10, button = 'left') {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('DOM.getDocument', { depth: -1 });
  await ui.send('DOM.dispatchMouseEvent', { nodeId: view.nodeId,
      event: { type, x, y, button, wheelDirection: 'none' } });
  ui.close();
}

export const shownViews = (vs, cls) => vs.filter(v => v.cls === cls && v.shown);

// Clicks the first shown native view of class `cls` (UI DevTools; needs the
// ui-debug-tools-enable-synthetic-events feature, which run.sh enables).
export async function clickView(cls) {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  let target = null;
  const walk = async (n, shown) => {
    if (target) return;
    const a = n.attributes ?? [];
    if (n.nodeName === 'View' && a[a.indexOf('class') + 1]) {
      const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
      for (const rule of r.result?.matchedCSSRules ?? []) {
        for (const prop of rule.rule.style.cssProperties) {
          if (prop.name === 'Visible' && prop.value === 'false') shown = false;
        }
      }
      if (shown && a[a.indexOf('class') + 1] === cls) {
        target = n;
        return;
      }
    }
    for (const c of n.children ?? []) await walk(c, shown);
  };
  await walk(doc, true);
  for (const type of ['mousePressed', 'mouseReleased']) {
    await ui.send('DOM.dispatchMouseEvent', { nodeId: target.nodeId,
        event: { type, x: 10, y: 10, button: 'left', wheelDirection: 'none' } });
  }
  ui.close();
  await sleep(500);
}

// Number of shown views of class `cls`.
export async function countViews(cls) {
  return shownViews(await views(), cls).length;
}

// Every native view as {cls, nodeId, shown, x, y, w, h, ax, ay, widget,
// path, props}: (ax, ay) is its position in its widget's root view, `path`
// the class names of its ancestors in that widget, `props` its UI DevTools
// properties (Visible, Text, TooltipText, ...).
export async function viewTree() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const out = [];
  let widget = 0;
  const walk = async (n, shown, path, ox, oy) => {
    const a = n.attributes ?? [];
    const cls = a[a.indexOf('class') + 1];
    if (n.nodeName === 'Widget') {
      widget++;
      path = [];
      ox = 0;
      oy = 0;
    } else if (n.nodeName === 'View' && cls) {
      const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
      const p = {};
      for (const rule of r.result?.matchedCSSRules ?? []) {
        for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
      }
      shown = shown && p.Visible !== 'false';
      // A widget's root view is at its origin.
      if (path.length) {
        ox += +p.X;
        oy += +p.Y;
      }
      out.push({ cls, nodeId: n.nodeId, shown, x: +p.X, y: +p.Y, w: +p.Width,
                 h: +p.Height, ax: ox, ay: oy, widget, path, props: p });
      path = [...path, cls];
    }
    for (const c of n.children ?? []) await walk(c, shown, path, ox, oy);
  };
  await walk(doc, true, [], 0, 0);
  ui.close();
  return out;
}

// Sends a native event to a view from viewTree() (UI DevTools); `method` is
// DOM.dispatchMouseEvent or DOM.dispatchKeyEvent. Fire-and-forget when
// `wait` is false.
export async function viewEvent(view, method, event, wait = true) {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('DOM.getDocument', { depth: -1 });
  const p = ui.send(method, { nodeId: view.nodeId, event });
  if (wait) await p;
  ui.close();
}

export const viewMouse = (view, type, x, y, button = 'left', wheelDirection = 'none') =>
  viewEvent(view, 'DOM.dispatchMouseEvent', { type, x, y, button, wheelDirection });

// Types `text` into a Textfield from viewTree() (char events), then presses
// `keyCode` (if any) on it.
export async function typeInto(view, text, keyCode = 0) {
  for (const ch of text) {
    await viewEvent(view, 'DOM.dispatchKeyEvent', { type: 'keyPressed',
      keyCode: ch.codePointAt(0), code: 0, flags: 0, key: ch.codePointAt(0),
      isChar: true });
  }
  if (keyCode) {
    await viewEvent(view, 'DOM.dispatchKeyEvent', { type: 'keyPressed', keyCode,
      code: 0, flags: 0, key: 0, isChar: false });
  }
  await sleep(1200);
}

// The open menus, one {root, items} per menu widget, items in order
// (separators left out). Menus take input through their root view.
export function openMenus(vs) {
  const byWidget = new Map();
  for (const v of vs.filter(v => v.cls === 'MenuItemView' && v.path.includes('SubmenuView'))) {
    if (!byWidget.has(v.widget)) byWidget.set(v.widget, []);
    byWidget.get(v.widget).push(v);
  }
  return [...byWidget.entries()].map(([widget, items]) => ({
    root: vs.find(v => v.widget === widget && v.cls === 'MenuHostRootView'),
    items,
  }));
}

// Hovers (`types` = ['mouseMoved']) or picks a menu item, at its position
// in the menu's root view. (Needs a sized headless screen, see
// koan_test_env.sh: menus are clamped to the screen.)
export async function menuMouse(menu, item,
                                types = ['mouseMoved', 'mousePressed', 'mouseReleased']) {
  for (const type of types) {
    await viewMouse(menu.root, type, item.ax + 8, item.ay + item.h / 2);
  }
  await sleep(1200);
}

// Creates a Space named `name` with Zen's Create-a-Space form (Ctrl+Alt+N
// from `page`, the name, Enter).
export async function createSpace(page, name) {
  await key(page, 78, CTRL | ALT, 'n');
  const field = await waitFor(async () => (await viewTree()).find(
      v => v.cls === 'Textfield' && v.shown && v.path.includes('KoanSpaceCreationView')));
  await typeInto(field, name, 13);
}

let failures = 0;
export function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
}

export function finish() {
  console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
  process.exit(failures ? 1 : 0);
}

// Sets a pref through a page opened in the background, then closes it:
// a Koan pref with a settings row (chrome://koan-settings' handler), or one
// of Chromium's (chrome://settings).
async function setPrefThrough(url, expression) {
  const targetId = (await browserCall('Target.createTarget',
      { url, background: true })).result.targetId;
  const page = await waitFor(async () => (await pages()).find(p => p.id === targetId));
  const value = await waitFor(() => evalIn(page, expression));
  await browserCall('Target.closeTarget', { targetId });
  await sleep(500);
  return value;
}
export const setKoanPref = (pref, value) => setPrefThrough('chrome://koan-settings',
    `import('chrome://resources/js/cr.js').then(m => m.sendWithPromise(
         'setPref', ${JSON.stringify(pref)}, ${JSON.stringify(value)}))`);
export const setChromePref = (pref, value) => setPrefThrough('chrome://settings',
    `new Promise(r => chrome.settingsPrivate.setPref(${JSON.stringify(pref)},
         ${JSON.stringify(value)}, '', r))`);

// Right-clicks an empty part of the sidebar (through the window's root view,
// so the click is routed like a real one), then picks the menu items at
// `indices` in turn, each in the newest (sub)menu. Returns what went wrong,
// or null.
export async function sidebarMenu(...indices) {
  let vs = await views();
  const strip = vs.find(v => v.cls === 'VerticalTabStripRegionView' && v.shown);
  const root = vs.find(v => v.cls === 'BrowserRootView' && v.shown);
  if (!strip || !root) {
    return 'no sidebar';
  }
  const x = strip.x + strip.w / 2, y = strip.y + strip.h - 120;
  await mouseOn(root, 'mousePressed', x, y, 'right');
  await mouseOn(root, 'mouseReleased', x, y, 'right');
  await sleep(800);
  for (const index of indices) {
    vs = await views();
    // The newest menu widget's root view, and its items.
    const menuRoot = vs.findLast(v => v.cls.endsWith('RootView') && v.cls !== 'BrowserRootView');
    const items = vs.slice(vs.indexOf(menuRoot))
        .filter(v => v.cls === 'MenuItemView' && v.path[0] === menuRoot?.cls);
    const item = items[index];
    if (!item) {
      return `no item ${index} of ${items.length}`;
    }
    const at = [item.x - menuRoot.x + 10, item.y - menuRoot.y + item.h / 2];
    await mouseOn(menuRoot, 'mouseMoved', ...at);
    await sleep(600);
    await mouseOn(menuRoot, 'mousePressed', ...at);
    await mouseOn(menuRoot, 'mouseReleased', ...at);
    await sleep(800);
  }
  return null;
}

// The latest frame of the browser window (the biggest dump), decoded:
// {width, height, pixel(x, y) -> [r, g, b]}. 8-bit RGB(A) PNGs only.
export function windowFrame() {
  const dir = process.env.KOAN_DUMP_DIR;
  const file = fs.readdirSync(dir).filter(f => f.endsWith('.png'))
      .map(f => path.join(dir, f))
      .sort((a, b) => fs.statSync(b).size - fs.statSync(a).size)[0];
  const png = fs.readFileSync(file);
  let pos = 8, width = 0, height = 0, channels = 0;
  const data = [];
  while (pos < png.length) {
    const length = png.readUInt32BE(pos);
    const type = png.toString('ascii', pos + 4, pos + 8);
    const chunk = png.subarray(pos + 8, pos + 8 + length);
    if (type === 'IHDR') {
      width = chunk.readUInt32BE(0);
      height = chunk.readUInt32BE(4);
      channels = chunk[9] === 6 ? 4 : 3;
    } else if (type === 'IDAT') {
      data.push(chunk);
    }
    pos += 12 + length;
  }
  const raw = zlib.inflateSync(Buffer.concat(data));
  const stride = width * channels;
  const out = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)];
    for (let x = 0; x < stride; x++) {
      const v = raw[y * (stride + 1) + 1 + x];
      const a = x >= channels ? out[y * stride + x - channels] : 0;
      const b = y > 0 ? out[(y - 1) * stride + x] : 0;
      const c = x >= channels && y > 0 ? out[(y - 1) * stride + x - channels] : 0;
      const p = a + b - c;
      const pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
      const paeth = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      out[y * stride + x] = (v + [0, a, b, (a + b) >> 1, paeth][filter]) & 0xff;
    }
  }
  return {
    width, height,
    pixel: (x, y) => [...out.subarray(y * stride + x * channels,
                                      y * stride + x * channels + 3)],
  };
}
