// Helpers for Koan's end-to-end tests: the DevTools protocol (pages, keys,
// clicks, prefs) and UI DevTools (native views). Koan runs as started by
// scripts/e2e/run.sh, with UI DevTools on UI_PORT and the DevTools protocol
// on CDP_PORT.
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

// Every native view as {cls, shown, w, h}; `shown` includes the ancestors'.
export async function views() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const out = [];
  const walk = async (n, depth, shown) => {
    const a = n.attributes ?? [];
    const cls = a[a.indexOf('class') + 1];
    if (depth > 0 && n.nodeName === 'View' && cls) {
      const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
      const p = {};
      for (const rule of r.result?.matchedCSSRules ?? []) {
        for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
      }
      shown = shown && p.Visible !== 'false';
      out.push({ cls, shown, w: +p.Width, h: +p.Height });
    }
    for (const c of n.children ?? []) await walk(c, depth + 1, shown);
  };
  await walk(doc, 0, true);
  ui.close();
  return out;
}

export const shownViews = (vs, cls) => vs.filter(v => v.cls === cls && v.shown);

// Number of shown views of class `cls`.
export async function countViews(cls) {
  return shownViews(await views(), cls).length;
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
