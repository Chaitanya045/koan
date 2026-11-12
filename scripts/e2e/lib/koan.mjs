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
export const IS_MAC = process.platform === 'darwin';
// The modifier of the platform's primary shortcuts (Koan's and Chromium's
// kAccel): Cmd (META) on macOS, Ctrl elsewhere. Keys bound to Ctrl on every
// platform (Koan's Ctrl+Alt+B / Ctrl+Alt+N, Ctrl+Tab, Ctrl+PageDown) keep CTRL.
export const ACCEL = IS_MAC ? META : CTRL;

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

// --- Keys.
//
// key() takes a Windows virtual key code (VK_*, ui::KeyboardCode), as every
// script passes. On Linux it sends that code as both the Windows and the
// native key code, as it always has.
//
// On macOS Chromium builds the NSEvent that the browser's shortcuts read
// from the native key code (content/browser/devtools/protocol/
// input_handler.cc: no native code, no os_event, so the key never leaves the
// page), and reads that code as a Mac virtual key code (kVK_*,
// ui/events/keycodes/keyboard_code_conversion_mac.mm), so a Windows code
// there is another key (VK_T, 84, is kVK_F5...). So on macOS key() sends the
// kVK_* code, the DOM `code`, the event's characters (`text`,
// `unmodifiedText`), and for Cmd+A/C/V/X/Z the editing command the Mac's
// key bindings would run (the page has no menu to do it).
//
// What CDP keys can't do on macOS: the main menu's key equivalents. A
// synthetic key goes to the page and then the browser window's accelerators
// (Koan's shortcut table, Chrome's global_keyboard_shortcuts_mac.mm), never
// through NSApp's main menu, so the keys only the menu bar has (Cmd+W Close
// Tab, Cmd+Shift+W Close Window, Cmd+Q Quit, Cmd+L Open Location, ...) do
// nothing. Steps that need them use the test_helper extension
// (lib/mac.mjs askTestHelper(), closeWindowOf()) or the real-input tools
// (lib/mac.mjs realKey(), #270), or skip on macOS (MAIN_MENU_ONLY_ON_MAC).
export const MAIN_MENU_ONLY_ON_MAC =
  'needs a main-menu key (Cmd+W/Cmd+Shift+W/Cmd+Q/Cmd+L), which CDP keys never reach on macOS; ' +
  'port it to the test_helper extension or real input (#270)';

// On macOS the menus Koan builds from a menu model (the sidebar's, the
// Space header's, a tab's, the Library's tab and row menus) are native
// NSMenus (MenuRunnerImplMac). UI DevTools can't see them, and while one is
// open its nested run loop keeps UI DevTools from answering, so a script
// that opens one hangs until the suite's timeout. Scripts skip those steps,
// reach the command another way, or pick the menu's items through
// Accessibility (lib/mac.mjs nativeMenu()) when NATIVE_MENUS is set.
export const NATIVE_MENUS = IS_MAC;
export const NATIVE_MENU_WHY =
  'needs a context menu, a native NSMenu on macOS that UI DevTools can\'t see or click';

// Ends a script that can't run on macOS with a SKIP (exit 77, which
// scripts/mac/suite.sh reports as SKIP, not as a pass); elsewhere nothing.
export function skipOnMac(why = MAIN_MENU_ONLY_ON_MAC) {
  if (!IS_MAC) return;
  console.log(`SKIP on macOS: ${why}`);
  process.exit(77);
}

// Windows VK -> [kVK_* (HIToolbox/Events.h), DOM code], for the keys the
// scripts use. Letters and digits are filled in below.
const MAC_KEYS = {
  8: [51, 'Backspace'], 9: [48, 'Tab'], 13: [36, 'Enter'], 27: [53, 'Escape'],
  32: [49, 'Space'], 33: [116, 'PageUp'], 34: [121, 'PageDown'], 35: [119, 'End'],
  36: [115, 'Home'], 37: [123, 'ArrowLeft'], 38: [126, 'ArrowUp'],
  39: [124, 'ArrowRight'], 40: [125, 'ArrowDown'], 46: [117, 'Delete'],
  106: [67, 'NumpadMultiply'], 186: [41, 'Semicolon'], 187: [24, 'Equal'],
  188: [43, 'Comma'], 189: [27, 'Minus'], 190: [47, 'Period'], 191: [44, 'Slash'],
  219: [33, 'BracketLeft'], 221: [30, 'BracketRight'],
};
'asdfhgzxcv_bqweryt'.split('').forEach((ch, i) => {
  if (ch !== '_') MAC_KEYS[ch.toUpperCase().charCodeAt(0)] = [i, `Key${ch.toUpperCase()}`];
});
Object.assign(MAC_KEYS, { 79: [31, 'KeyO'], 85: [32, 'KeyU'], 73: [34, 'KeyI'],
  80: [35, 'KeyP'], 76: [37, 'KeyL'], 74: [38, 'KeyJ'], 75: [40, 'KeyK'],
  78: [45, 'KeyN'], 77: [46, 'KeyM'] });
[29, 18, 19, 20, 21, 23, 22, 26, 28, 25].forEach((mac, n) =>
  (MAC_KEYS[48 + n] = [mac, `Digit${n}`]));
// The characters each non-letter key types (NSEvent characters).
const MAC_TEXT = { 8: '\x7f', 9: '\t', 13: '\r', 27: '\x1b', 32: ' ', 186: ';',
  187: '=', 188: ',', 189: '-', 190: '.', 191: '/', 219: '[', 221: ']' };
// Cmd+<key> -> the editing command macOS's key bindings send the page.
const MAC_COMMANDS = { 65: 'selectAll', 67: 'copy', 86: 'paste', 88: 'cut', 90: 'undo' };

// The Input.dispatchKeyEvent params (without `type`) for Windows key code
// `code` with `modifiers` and DOM key `keyName`.
export function keyEventParams(code, modifiers = 0, keyName) {
  if (!IS_MAC) {
    return { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers, key: keyName };
  }
  const mac = MAC_KEYS[code];
  if (!mac) throw new Error(`key(): no macOS key code for Windows key code ${code}`);
  const p = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: mac[0], code: mac[1],
              modifiers, key: keyName };
  let unmodified = MAC_TEXT[code];
  if (/^Key/.test(mac[1])) unmodified = mac[1].slice(3).toLowerCase();
  if (/^Digit/.test(mac[1])) unmodified = mac[1].slice(5);
  if (unmodified !== undefined) {
    // Shift gives the capital (what Chromium's Mac key tables match); other
    // modifiers keep the plain character, as AppKit's charactersIgnoringModifiers.
    const shifted = modifiers & SHIFT && /^Key/.test(mac[1]) ? unmodified.toUpperCase() : unmodified;
    Object.assign(p, { text: shifted, unmodifiedText: unmodified });
    if (!keyName && /^(Key|Digit)/.test(mac[1])) p.key = shifted;
  }
  if ((modifiers & (META | CTRL | ALT | SHIFT)) === META && MAC_COMMANDS[code]) {
    p.commands = [MAC_COMMANDS[code]];
  }
  return p;
}

// Sends keyEventParams() for one press over CDP session `c` (a connect()):
// rawKeyDown and keyUp, as the scripts always have, but on macOS a printable
// key without Cmd/Ctrl/Option goes as keyDown so it types its text. Not
// Escape: it types nothing, and the browser runs its accelerators (the
// Library's and the URL bar's Esc, ...) for a page's unhandled rawKeyDown
// only (views::UnhandledKeyboardEventHandler).
export async function sendKey(c, code, modifiers = 0, keyName) {
  const p = keyEventParams(code, modifiers, keyName);
  const types = IS_MAC && p.text && code !== 27 && !(modifiers & (CTRL | META | ALT))
      ? 'keyDown' : 'rawKeyDown';
  await c.send('Input.dispatchKeyEvent', { ...p, type: types });
  await c.send('Input.dispatchKeyEvent', { ...p, type: 'keyUp' });
}

// Sends a key press to `page`; keys the page doesn't handle reach the
// browser's accelerators like real input (on macOS not the main menu's: see
// above).
export async function key(page, code, modifiers, keyName) {
  await onPage(page, c => sendKey(c, code, modifiers, keyName));
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

// Koan's panes are a section of chrome://settings (koan_settings_ui.h). In a
// settings tab, `koanSettingsRoot` is the panes' shadow root (Chromium's
// own shadow roots hide it from document.querySelector()).
//
// An expression that shows Koan's pane `id` (looks, tabs, shortcuts, agents)
// through Chromium's router, as its menu does.
export const showPane = id => `import('chrome://settings/settings.js').then(
    ({Router}) => { const r = Router.getInstance();
                    r.navigateTo(r.getRouteForPath('/koan/${id}')); })`;
// An expression for the labels of the Koan items in Chromium's menu.
export const koanMenuItems = `[...document.querySelector('settings-ui').shadowRoot
    .querySelector('#leftMenu').shadowRoot.querySelectorAll('a[id^="koan"]')]
    .map(a => a.textContent.trim())`;
// An expression that searches with Chromium's toolbar search, as typing does.
export const searchSettings = text => `import('chrome://settings/settings.js').then(
    ({Router, routes}) => Router.getInstance().navigateTo(routes.BASIC,
        new URLSearchParams({search: ${JSON.stringify(text)}})))`;

// Calls Koan's settings handler from `page` (a settings tab), as the panes
// themselves do.
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

// Every native view as {cls, shown, w, h, x, y, nodeId, path, props};
// `shown` includes the ancestors', x/y are in the view's widget, `path` lists
// the ancestors' classes from the widget's root view, `props` has the view's
// properties (e.g. a label's Text, a button's TooltipText).
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
                  nodeId: n.nodeId, path, props: p };
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
// Sends a mouse event to `view` (from views()), at (x, y) in it.
//
// Node ids are handed out again in tree order on each DOM.getDocument, so
// once the tree changed (a view added or removed anywhere before it) the id
// views() saw is another view's, often one of the same class: on macOS the
// sidebar's hot zone's id named the top edge's. The view is found again in
// this session, by its class and its bounds.
export async function mouseOn(view, type, x = 10, y = 10, button = 'left') {
  if (!view) throw new Error(`mouseOn(): no view to send ${type} to (not found)`);
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const same = [];
  const walk = n => {
    const a = n.attributes ?? [];
    if (n.nodeName === 'View' && a[a.indexOf('class') + 1] === view.cls) same.push(n.nodeId);
    for (const c of n.children ?? []) walk(c);
  };
  walk(doc);
  let nodeId = same.length === 1 ? same[0] : null;
  for (const id of same.length > 1 ? same : []) {
    const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: id });
    const p = {};
    for (const rule of r.result?.matchedCSSRules ?? []) {
      for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
    }
    if (['X', 'Y', 'Width', 'Height'].every(k => p[k] === view.props?.[k])) {
      nodeId = id;
      break;
    }
  }
  await ui.send('DOM.dispatchMouseEvent', { nodeId: nodeId ?? view.nodeId,
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

// A view's properties (UI DevTools), within `ui`'s session.
async function viewProps(ui, nodeId) {
  const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId });
  const p = {};
  for (const rule of r.result?.matchedCSSRules ?? []) {
    for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
  }
  return p;
}

// Where the view with element id `id` is: its node and position in the root
// view of its widget, or null.
async function findInMenu(ui, id) {
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const search = (await ui.send('DOM.performSearch', { query: `id:${id}` })).result;
  const [itemId] = search?.resultCount ? (await ui.send('DOM.getSearchResults',
      { searchId: search.searchId, fromIndex: 0, toIndex: 1 })).result.nodeIds : [];
  // The views from the widget's root view down to the item.
  const chain = [];
  const findChain = n => {
    if (n.nodeId === itemId) {
      chain.push(n);
      return true;
    }
    for (const c of n.children ?? []) {
      if (findChain(c)) {
        chain.unshift(n);
        return true;
      }
    }
    return false;
  };
  if (!itemId || !findChain(doc)) return null;
  // Below the widget: the views of other widgets above don't count.
  const widgetAt = chain.findLastIndex(n => n.nodeName !== 'View');
  const views = chain.slice(widgetAt + 1);
  let x = 20, y = 0;
  for (const n of views.slice(1)) {
    const p = await viewProps(ui, n.nodeId);
    x += +p.X;
    y += +p.Y;
    if (n.nodeId === itemId) y += Math.floor(+p.Height / 2);
  }
  return { root: views[0], x, y };
}

// Runs an item of a tab's context menu: right-clicks the tab view (`tab`:
// {nodeId} of a TabView, or {title} of the tab) and clicks the menu item
// with element id `item`, e.g. 'kKoanAddToEssentialsMenuItem' (Koan's items
// in koan_pinned_tabs.cc), first opening the submenu item `via` if given.
// Returns false if the tab or an item isn't there.
export async function tabMenuCommand(tab, item, { via } = {}) {
  let ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  let doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  let tabId = tab.nodeId;
  const findTitled = async (n, parent) => {
    const a = n.attributes ?? [];
    if (a[a.indexOf('class') + 1] === 'TabTitle' &&
        (await viewProps(ui, n.nodeId)).Text === tab.title) {
      return parent.nodeId;
    }
    for (const c of n.children ?? []) {
      const found = await findTitled(c, n.nodeName === 'View' ? n : parent);
      if (found) return found;
    }
    return null;
  };
  tabId ??= await findTitled(doc, null);
  if (!tabId) {
    ui.close();
    return false;
  }
  for (const type of ['mousePressed', 'mouseReleased']) {
    await ui.send('DOM.dispatchMouseEvent', { nodeId: tabId,
        event: { type, x: 20, y: 10, button: 'right', wheelDirection: 'none' } });
  }
  ui.close();
  await sleep(1500);

  ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  if (via) {
    // Pointing at a submenu item opens it.
    const parent = await findInMenu(ui, via);
    if (!parent) {
      ui.close();
      return false;
    }
    await ui.send('DOM.dispatchMouseEvent', { nodeId: parent.root.nodeId,
        event: { type: 'mouseMoved', x: parent.x, y: parent.y, button: 'none',
                 wheelDirection: 'none' } });
    await sleep(1500);
  }
  const found = await findInMenu(ui, item);
  if (!found) {
    ui.close();
    return false;
  }
  for (const type of ['mousePressed', 'mouseReleased']) {
    await ui.send('DOM.dispatchMouseEvent', { nodeId: found.root.nodeId,
        event: { type, x: found.x, y: found.y, button: 'left', wheelDirection: 'none' } });
  }
  ui.close();
  await sleep(1500);
  return true;
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

// Each window's Space tint (its user colour override, an ARGB number; 0 for
// none: Zen's default theme or the tint off), in viewTree() order, read from
// KoanSpaceBarView's WindowTint property. A Space's colour never becomes the
// profile's (`browser.theme.user_color2` stays the user's).
export async function windowTints() {
  return (await viewTree()).filter(v => v.cls === 'KoanSpaceBarView')
      .map(v => Number(v.props.WindowTint ?? 0));
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
// a Koan pref with a settings row (Koan's settings handler), or one
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
export const setKoanPref = (pref, value) => setPrefThrough('chrome://settings/koan/looks',
    `import('chrome://resources/js/cr.js').then(m => m.sendWithPromise(
         'koanSetPref', ${JSON.stringify(pref)}, ${JSON.stringify(value)}))`);
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

// With SHOTS=<dir>, run.sh dumps each window's latest frame into
// <dir>/frames; this copies the browser window's (the biggest) to
// <dir>/<name>.png.
export function saveShot(name) {
  if (!process.env.SHOTS) {
    return;
  }
  const dir = path.join(process.env.SHOTS, 'frames');
  const file = fs.readdirSync(dir).filter(f => f.endsWith('.png'))
      .map(f => path.join(dir, f))
      .sort((a, b) => fs.statSync(b).size - fs.statSync(a).size)[0];
  if (file) {
    fs.copyFileSync(file, path.join(process.env.SHOTS, `${name}.png`));
  }
}

// The latest frame of the browser window (the biggest dump), decoded:
// {width, height, pixel(x, y) -> [r, g, b]}. 8-bit RGB(A) PNGs only.
export function windowFrame() {
  const dir = process.env.KOAN_DUMP_DIR;
  const file = fs.readdirSync(dir).filter(f => f.endsWith('.png'))
      .map(f => path.join(dir, f))
      .sort((a, b) => fs.statSync(b).size - fs.statSync(a).size)[0];
  return decodePng(file);
}

// A PNG file decoded: {width, height, pixel(x, y) -> [r, g, b]}. 8-bit,
// non-interlaced RGB(A) only (it throws on others).
export function decodePng(file) {
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
      if (chunk[8] !== 8 || ![2, 6].includes(chunk[9]) || chunk[12] !== 0) {
        throw new Error(`decodePng: ${file} is not an 8-bit non-interlaced RGB(A) PNG`);
      }
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
