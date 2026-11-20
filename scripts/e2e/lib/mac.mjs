// Helpers for the e2e scripts that scripts/mac/suite.sh runs on the Mac
// (window_teardown, window_sync, space_drag, welcome_input, agent_cli):
// keys that reach the browser's shortcuts on macOS, real input through the
// Mac bench tools when the suite allows it, windows, a local web server,
// the browser's log and skips.
//
// Keys. CDP's Input.dispatchKeyEvent hands an unhandled key to the browser
// only with a native key code; on macOS that code is built into an NSEvent,
// so it has to be the Mac's virtual key code (kVK_*). key() here takes a key
// name; lib/koan.mjs key() takes a Windows key code and converts it.
//
// Real input (KOAN_INPUT=real, set by suite.sh only after its gates passed:
// the screen is granted and the user has been away for 10 minutes).
// keypress2 posts to Koan's pid; ptr moves the real pointer. Every event is
// sent only while Koan's exact pid (KOAN_PID) is the frontmost app
// (inputok.sh), otherwise realInput() turns off for the rest of the run and
// the test reports its real-input checks as skipped, never as passed.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { ACCEL, ALT, CTRL, IS_MAC, META, SHIFT, browserCall, connect, createSpace as koanCreateSpace,
         decodePng, onPage, pages, sleep, viewMouse, viewTree, waitFor } from './koan.mjs';

export { ACCEL, ALT, CTRL, IS_MAC, META, SHIFT };

// kVK_* (HIToolbox/Events.h) by key name.
const MAC_VK = {
  a: 0, s: 1, d: 2, f: 3, h: 4, g: 5, z: 6, x: 7, c: 8, v: 9, b: 11, q: 12,
  w: 13, e: 14, r: 15, y: 16, t: 17, 1: 18, 2: 19, 3: 20, 4: 21, 6: 22, 5: 23,
  9: 25, 7: 26, 8: 28, 0: 29, o: 31, u: 32, i: 34, p: 35, l: 37, j: 38,
  k: 40, n: 45, m: 46, '[': 33, ']': 30, Enter: 36, Tab: 48, ' ': 49, Backspace: 51,
  Escape: 53, ArrowLeft: 123, ArrowRight: 124, ArrowDown: 125, ArrowUp: 126,
  PageUp: 116, PageDown: 121,
};
// Windows virtual key codes (what windowsVirtualKeyCode wants).
const WIN_VK = {
  Enter: 13, Tab: 9, ' ': 32, Backspace: 8, Escape: 27, ArrowLeft: 37,
  ArrowUp: 38, ArrowRight: 39, ArrowDown: 40, PageUp: 33, PageDown: 34,
};
const CODE = {
  Enter: 'Enter', Tab: 'Tab', ' ': 'Space', Backspace: 'Backspace',
  Escape: 'Escape', ArrowLeft: 'ArrowLeft', ArrowRight: 'ArrowRight',
  ArrowDown: 'ArrowDown', ArrowUp: 'ArrowUp', PageUp: 'PageUp', PageDown: 'PageDown',
};
export const macKeyCode = name => MAC_VK[name.length === 1 ? name.toLowerCase() : name];

// The keyDown/keyUp params for `name` ('w', 'Tab', 'Escape', ...).
function keyParams(name, modifiers) {
  const printable = name.length === 1;
  const lower = printable ? name.toLowerCase() : name;
  const win = printable ? lower.toUpperCase().charCodeAt(0) : WIN_VK[name];
  const code = printable ? (/[0-9]/.test(lower) ? `Digit${lower}` :
                                (lower === ' ' ? 'Space' : `Key${lower.toUpperCase()}`))
                         : CODE[name];
  const native = IS_MAC ? MAC_VK[lower] : win;
  if (native === undefined || win === undefined) throw new Error(`no key code for ${name}`);
  const p = { windowsVirtualKeyCode: win, nativeVirtualKeyCode: native, code,
              key: printable && (modifiers & SHIFT) ? name.toUpperCase() : name, modifiers };
  if (printable) {
    // NSEvent's characters: what the menu and Chromium's key tables match.
    const ch = modifiers & SHIFT ? lower.toUpperCase() : lower;
    Object.assign(p, { text: ch, unmodifiedText: lower });
  } else if (name === 'Enter') {
    Object.assign(p, { text: '\r', unmodifiedText: '\r' });
  } else if (name === 'Tab') {
    Object.assign(p, { text: '\t', unmodifiedText: '\t' });
  } else if (name === 'Escape') {
    Object.assign(p, { text: '\u001b', unmodifiedText: '\u001b' });
  }
  return p;
}

// Presses `name` with `modifiers` in `page` (CDP); keys the page doesn't
// handle reach the browser's shortcuts, on macOS too.
export async function key(page, name, modifiers = 0, settle = 600) {
  const p = keyParams(name, modifiers);
  const shortcut = modifiers & (CTRL | META | ALT);
  await onPage(page, async c => {
    await c.send('Input.dispatchKeyEvent',
                 { ...p, type: p.text && !shortcut ? 'keyDown' : 'rawKeyDown' });
    await c.send('Input.dispatchKeyEvent', { ...p, type: 'keyUp' });
  });
  await sleep(settle);
}

// --- The browser.

// (A let: a script that starts Koan again hands the new pid to setKoanPid(),
// so real input goes to it.)
export let KOAN_PID = Number(process.env.KOAN_PID || 0);
export function setKoanPid(pid) {
  KOAN_PID = Number(pid || 0);
}
export const KOAN_LOG = process.env.KOAN_LOG;

// Whether the test's Koan still runs (its exact pid, from suite.sh), or, with
// no pid, whether it answers.
export async function alive() {
  if (KOAN_PID) {
    try {
      process.kill(KOAN_PID, 0);
    } catch {
      return false;
    }
  }
  try {
    return !!(await browserCall('Browser.getVersion')).result?.product;
  } catch {
    return false;
  }
}

// The browser's log so far (stderr, --enable-logging=stderr).
export const log = () => (KOAN_LOG && fs.existsSync(KOAN_LOG) ? fs.readFileSync(KOAN_LOG, 'utf8') : '');
// How often `re` matches the log (its own flags kept, e.g. m for a $ at each
// line's end).
export const countInLog = re =>
  (log().match(new RegExp(re.source, re.flags.replace('g', '') + 'g')) ?? []).length;
// The first fatal line in the log (FATAL, a failed CHECK or DCHECK), or ''.
export const fatalInLog = () =>
  log().split('\n').find(l => /FATAL|Check failed|CHECK failed|DCHECK/.test(l)) ?? '';

// --- Windows (CDP).

export async function windowOf(targetId) {
  return (await browserCall('Browser.getWindowForTarget', { targetId })).result;
}
// The windows that have pages: [{windowId, bounds, pages}], in window id order.
export async function windows() {
  const byId = new Map();
  for (const page of await pages()) {
    const w = await windowOf(page.id);
    if (!w?.windowId) continue;
    if (!byId.has(w.windowId)) byId.set(w.windowId, { windowId: w.windowId, bounds: w.bounds, pages: [] });
    byId.get(w.windowId).pages.push(page);
  }
  return [...byId.values()].sort((a, b) => a.windowId - b.windowId);
}
// Opens `url` in a new window; returns {targetId, windowId}.
export async function openWindow(url) {
  const targetId = (await browserCall('Target.createTarget', { url, newWindow: true })).result?.targetId;
  await waitFor(async () => (await pages()).some(p => p.id === targetId), 8000);
  await sleep(2500);
  return { targetId, windowId: (await windowOf(targetId))?.windowId };
}
// Has the test_helper extension (loaded by the test's .flags) do `action`
// ('close-window', 'pin-tab', or {discard: [urls]}) for `page`, a 127.0.0.1
// page: for what synthetic input can't reach on macOS. Returns whether the
// page could ask.
const TEST_HELPER_ID = 'efmifboglkpejppaiocdcajnkhbfoofo';
export async function askTestHelper(page, action, settle = 800) {
  const asked = await onPage(page, c => c.send('Runtime.evaluate', {
    expression: `!!globalThis.chrome?.runtime?.sendMessage &&
        (chrome.runtime.sendMessage('${TEST_HELPER_ID}', ${JSON.stringify(action)}).catch(() => {}), true)`,
    returnByValue: true }));
  await sleep(settle);
  return asked.result?.result?.value === true;
}

// Close Window for `page` without real input. On macOS the Close Window and
// Close Tab keys belong to the main menu, which synthetic (CDP) keys never
// reach, so test_helper closes the window: chrome.windows.remove() runs
// BrowserView::Close(), as the menu item and the red button do. Elsewhere
// it sends the key.
export async function closeWindowOf(page, settle = 800) {
  if (!IS_MAC) {
    await key(page, 'w', ACCEL | SHIFT, settle);
    return true;
  }
  return askTestHelper(page, 'close-window', settle);
}

export async function setWindowState(windowId, windowState) {
  await browserCall('Browser.setWindowBounds', { windowId, bounds: { windowState } });
}

// --- Widgets (UI DevTools). viewTree() numbers each widget; a browser
// window's widget is the one with a BrowserView.

// The views of each browser window: [{widget, views}], in tree order.
export async function browserWidgets(vs) {
  vs ??= await viewTree();
  const ids = [...new Set(vs.filter(v => v.cls === 'BrowserView').map(v => v.widget))];
  return ids.map(widget => ({ widget, views: vs.filter(v => v.widget === widget) }));
}
// The browser widget showing a tab titled `title` (shown in its sidebar).
export async function widgetWithTab(title, vs) {
  vs ??= await viewTree();
  for (const w of await browserWidgets(vs)) {
    if (w.views.some(v => v.cls === 'TabTitle' && v.shown && v.props.Text === title)) return w;
  }
  return null;
}
// A widget's root view (where input is routed like a real event's).
export const rootOf = w => w.views.find(v => /RootView$/.test(v.cls));

// A real-looking click on `view` (from viewTree()): the events go to its
// widget's root view, which routes them like real input (pre-target
// handlers, context menus, drag controllers). `at` is in the view.
export async function clickThroughRoot(vs, view, { button = 'left', at } = {}) {
  const root = rootOf({ views: vs.filter(v => v.widget === view.widget) });
  const x = Math.round(view.ax + (at?.x ?? Math.min(view.w / 2, 24)));
  const y = Math.round(view.ay + (at?.y ?? view.h / 2));
  await rootMouse(root, ['mousePressed', 'mouseReleased'], x, y, button);
  await sleep(800);
}
// UI DevTools numbers the nodes again on each DOM.getDocument, in tree
// order, and on macOS the widgets come in NSApp.windows' order: a widget
// made since `root` was read (a drag's image, a header, a toast) can come
// before it and move its number. So the events go to the root view of the
// widget `root` was in (its name and place among the widgets of that name,
// from viewTree()), as this document numbers it.
export function freshRootId(doc, root) {
  if (root.wname === undefined) return root.nodeId;
  const roots = [];
  const walk = n => {
    const a = n.attributes ?? [];
    if (n.nodeName === 'Widget' && (a[a.indexOf('name') + 1] ?? '') === root.wname) {
      const view = (n.children ?? []).find(c => {
        const ca = c.attributes ?? [];
        return c.nodeName === 'View' && /RootView$/.test(ca[ca.indexOf('class') + 1] ?? '');
      });
      if (view) roots.push(view.nodeId);
    }
    for (const c of n.children ?? []) walk(c);
  };
  walk(doc);
  // (Its old number, if that is still a root view of a widget of the name.)
  if (roots.includes(root.nodeId)) return root.nodeId;
  return roots[root.wn ?? 0] ?? root.nodeId;
}
export async function rootMouse(root, types, x, y, button = 'left') {
  const ui = await connect(`ws://127.0.0.1:${process.env.UI_PORT}/0`);
  await ui.send('DOM.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result?.root;
  const nodeId = doc ? freshRootId(doc, root) : root.nodeId;
  for (const type of types) {
    await ui.send('DOM.dispatchMouseEvent', { nodeId,
        event: { type, x: Math.round(x), y: Math.round(y), button, wheelDirection: 'none' } });
  }
  ui.close();
}

// --- The URL bar.

// Puts the URL bar into editing with the whole address selected, as a
// person does, so typed text replaces the address. Off macOS: Ctrl+L, which
// a CDP key reaches. On macOS Cmd+L is a main-menu key that CDP keys never
// reach, so: a click on the URL pill's middle, which selects the whole
// address (Zen's clickSelectsAll, #328; the pill's first ~30 px are the
// site-icon button, which opens the site panel instead); with the sidebar
// hidden (compact mode: no pill to click), a real Cmd+L in `page`'s window
// when real input is on. Returns {field, how, selectedAll}: the shown
// OmniboxViewViews (viewTree()) or null, and whether all its text was
// selected.
export async function focusUrlBar(page) {
  let how = 'Ctrl+L';
  if (!IS_MAC) {
    await key(page, 'l', CTRL, 300);
  } else {
    const vs = await viewTree();
    const sidebarOut = vs.some(v => v.cls === 'VerticalTabStripRegionView' && v.shown && v.w > 0);
    const pill = vs.find(v => v.cls === 'KoanUrlPill' && v.shown && v.w > 0 && v.h > 0);
    if (sidebarOut && pill) {
      how = 'a click on the pill';
      await clickThroughRoot(vs, pill, { at: { x: Math.round(pill.w / 2), y: Math.round(pill.h / 2) } });
    } else if (realInput() && await makeKey(page) && await realKey('l', 'c', 300)) {
      how = 'a real Cmd+L';
    } else {
      return { field: null, how: `no pill to click and ${realInputWhyNot() || 'no key window'}`,
               selectedAll: false };
    }
  }
  await sleep(400);  // the click's select-all is posted
  const field = await waitFor(async () => (await viewTree()).find(
      v => v.cls === 'OmniboxViewViews' && v.shown), 4000) || null;
  const text = field?.props?.Text ?? '';
  const sel = (field?.props?.SelectedRange ?? '').match(/\d+/g)?.map(Number) ?? [];
  return { field, how, selectedAll: !text || (Math.min(...sel) === 0 && Math.max(...sel) === text.length) };
}

// --- Spaces.

// Creates a Space named `name` with Zen's Create-a-Space form (Ctrl+Alt+N,
// Ctrl on macOS too), from `page` (lib/koan.mjs createSpace(): the window
// is made key first on macOS). Whether the form came up.
export async function createSpace(page, name) {
  if (!await koanCreateSpace(page, name)) return false;
  await sleep(1200);
  return true;
}
// The bottom bar's shown Space buttons in `w` (a browserWidgets() entry, or
// every widget): [{name, active, view}], left to right / top to bottom.
export function spaceButtons(vs, widget) {
  return vs.filter(v => v.cls === 'KoanSpaceButton' && v.shown &&
                        (widget === undefined || v.widget === widget))
      .map(v => ({ name: v.props.TooltipText ?? '', active: v.props.Active === 'true', view: v }))
      .sort((a, b) => a.view.ax - b.view.ax || a.view.ay - b.view.ay);
}
// The shown tab view whose title is `title`, or undefined.
export function tabViewTitled(vs, title) {
  const label = vs.find(v => v.cls === 'TabTitle' && v.shown && v.props.Text === title);
  if (!label) return undefined;
  return vs.filter(v => v.cls === 'TabView' && v.shown && v.widget === label.widget &&
                        v.ax <= label.ax && v.ay <= label.ay && v.ax + v.w >= label.ax + 1 &&
                        v.ay + v.h >= label.ay + 1)
      .sort((a, b) => a.w * a.h - b.w * b.h)[0];
}
// Selects the tab titled `title` as the user does, a click on its row in the
// sidebar (UI DevTools); whether there was one. A tab an agent holds doesn't
// come forward through CDP (Target.activateTarget, Page.bringToFront):
// Koan holds that as the page's own focus request (HoldActivationForAgent()).
export async function selectTabRow(title) {
  const row = tabViewTitled(await viewTree(), title);
  if (!row) return false;
  for (const type of ['mousePressed', 'mouseReleased']) {
    await viewMouse(row, type, Math.round(row.w / 2), Math.round(row.h / 2));
  }
  return true;
}

// --- Real input (Mac bench tools; see the header).

const TOOLS = process.env.KOAN_MAC_TOOLS;
let realOk = IS_MAC && process.env.KOAN_INPUT === 'real' && !!TOOLS &&
    fs.existsSync(`${TOOLS}/keypress2`) && fs.existsSync(`${TOOLS}/ptr`);
let realWhyNot = realOk ? '' : 'real input is off (suite.sh sets KOAN_INPUT=real after its gates)';
export const realInput = () => realOk;
export const realInputWhyNot = () => realWhyNot;

// Brings Koan's pid to the front (the bench's activate tool, else System
// Events), then checks it is the frontmost app. Off for good on a refusal.
function gate() {
  if (!realOk) return false;
  try {
    if (fs.existsSync(`${TOOLS}/activate`)) {
      execFileSync(`${TOOLS}/activate`, [String(KOAN_PID)], { stdio: 'ignore', timeout: 5000 });
    } else {
      execFileSync('osascript', ['-e', `tell application "System Events" to set frontmost of ` +
          `(first process whose unix id is ${KOAN_PID}) to true`], { stdio: 'ignore', timeout: 5000 });
    }
  } catch {}
  try {
    execFileSync(`${TOOLS}/inputok.sh`, [String(KOAN_PID)], { stdio: 'ignore', timeout: 5000 });
    return true;
  } catch (e) {
    realOk = false;
    realWhyNot = `inputok.sh refused (exit ${e.status}: ${e.status === 7 ? 'the screen is not granted' :
                                                     'Koan is not the frontmost app'})`;
    console.log(`  (real input stopped: ${realWhyNot})`);
    return false;
  }
}
// Makes `page`'s window the key window (CDP's Page.bringToFront: the browser
// window's Activate(), which orders it front and makes it key), and waits
// until the page has the focus. Real input needs that: keypress2 posts to
// Koan's pid, so a main-menu key (Cmd+Shift+W, Cmd+Q) goes to whatever window
// is key, if any, and a page in a window that isn't key sees no mouse moves.
// A window opened through CDP (Target.createTarget) isn't made key, and
// bringing the app to the front (gate()) doesn't make one key either.
// Returns whether the page got the focus.
export async function makeKey(page, timeout = 3000) {
  if (!page) return false;
  await onPage(page, c => c.send('Page.bringToFront')).catch(() => {});
  return !!(await waitFor(async () => (await onPage(page, c => c.send('Runtime.evaluate',
      { expression: 'document.hasFocus()', returnByValue: true }))).result?.result?.value,
      timeout));
}

// Which window has the keyboard, for failure details: on macOS a CDP key
// reaches the browser's shortcuts, and a UI DevTools key a widget's focused
// view, only in the key window. Whether Koan is the frontmost app and its
// focused window's title (Accessibility, read only), and the pages whose
// document has the focus (none while a view, e.g. the URL bar, has it).
export async function keyWindowReport(pid = KOAN_PID) {
  const focusedPages = [];
  for (const p of await pages().catch(() => [])) {
    const r = await onPage(p, c => c.send('Runtime.evaluate',
        { expression: 'document.hasFocus()', returnByValue: true })).catch(() => null);
    if (r?.result?.result?.value) focusedPages.push(p.url);
  }
  let ax = '';
  if (IS_MAC && pid) {
    try {
      ax = execFileSync('osascript', ['-l', 'JavaScript', '-e',
          `const p = Application('System Events').applicationProcesses.whose({ unixId: ${pid} })[0];
           let w = '(none)';
           try { w = p.attributes.byName('AXFocusedWindow').value().name(); } catch (e) {}
           JSON.stringify({ frontmost: p.frontmost(), windows: p.windows().length, focusedWindow: w })`],
          { encoding: 'utf8', timeout: 10000 }).trim();
    } catch (e) {
      ax = `(Accessibility: ${String(e.stderr || e.message).trim().split('\n')[0]})`;
    }
  }
  return `key window: ${ax || 'n/a'}; pages with the focus: ${focusedPages.join(' ') || 'none'}`;
}

// Whether the menu bar item `item` (an AppleScript specifier within the menu
// bar of Koan's process, e.g. 'menu item "Close Window" of menu 1 of menu
// bar item "File"') is enabled, as Accessibility (System Events) reads it:
// what a person sees in the menu, and whether its key works. true / false,
// or null off macOS or where it can't be read (no Accessibility grant for
// osascript). Reading it is not input.
export function menuItemEnabled(item) {
  if (!IS_MAC || !KOAN_PID) return null;
  try {
    const out = execFileSync('osascript', ['-e', 'tell application "System Events" to tell ' +
        `(first process whose unix id is ${KOAN_PID}) to get enabled of ${item} of menu bar 1`],
        { encoding: 'utf8', timeout: 10000 }).trim();
    return out === 'true' ? true : out === 'false' ? false : null;
  } catch {
    return null;
  }
}
// --- Native context menus. On macOS the menus Koan builds from a menu model
// (a tab's, a folder's, the Library rows') are NSMenus (lib/koan.mjs
// NATIVE_MENUS); while one is open, its tracking loop runs no Chromium
// tasks, so neither CDP nor UI DevTools answers until it closes. Scripts open
// one (a right-click, which UI DevTools then answers only once the menu has
// closed) and pick its items through Accessibility (lib/native_menu_ax.js),
// which AppKit answers inside the menu, like a person reading and clicking
// it. Reading and pressing a menu item there is not real input.
const NATIVE_MENU_AX = new URL('./native_menu_ax.js', import.meta.url).pathname;
// lib/native_menu_ax.js `op` for Koan's process `pid` (it changes when a
// script restarts Koan): its JSON answer, or {error}.
// The script also hit-tests the process's menu-level windows (a menu no walk
// of the tree reaches: see there); if that part fails the script (the raw
// Accessibility bridge), the op runs again without it, as before.
let menuHitTest = true;
export function nativeMenu(op, path = [], pid = KOAN_PID) {
  if (!IS_MAC || !pid) return { error: 'not macOS, or no Koan pid' };
  const ask = hit => {
    try {
      const out = execFileSync('osascript', ['-l', 'JavaScript', NATIVE_MENU_AX,
                                             JSON.stringify({ op, path, pid, hit })],
                               { encoding: 'utf8', timeout: 15000 });
      return JSON.parse(out.trim() || '{}');
    } catch (e) {
      return { error: String(e.stderr || e.message).trim().split('\n')[0] };
    }
  };
  let r = ask(menuHitTest);
  if (r.error && menuHitTest && op !== 'probe') {
    const without = ask(false);
    if (!without.error) {
      menuHitTest = false;
      console.log(`  (native menus: the hit test fails (${r.error}); without it from now on)`);
    }
    r = without;
  }
  return r;
}
// Why scripts can't drive Koan's native menus here ('' when they can):
// System Events must be allowed to read Koan (Accessibility).
export function nativeMenusWhyNot(pid = KOAN_PID) {
  if (!IS_MAC || !pid) return 'not macOS, or no Koan pid';
  const r = nativeMenu('probe', [], pid);
  return r.ok ? '' : `System Events can't read Koan's menus (${r.error ?? 'no menu bar'})`;
}
// Waits until a native context menu is open (or, `open` false, none is):
// the last read, or null on a timeout.
export async function waitNativeMenu(open = true, pid = KOAN_PID, timeout = 4000) {
  const end = Date.now() + timeout;
  for (;;) {
    const r = nativeMenu('read', [], pid);
    if (!r.error && !!r.open === open) return r;
    if (Date.now() > end) return null;
    await sleep(250);
  }
}
// Whether the browser answers CDP (its /json/version) within `timeout` ms.
// While an NSMenu is open its tracking loop runs no Chromium tasks, so it
// doesn't.
export const browserAnswers = (timeout = 2000) =>
  fetch(`http://127.0.0.1:${process.env.CDP_PORT}/json/version`,
        { signal: AbortSignal.timeout(timeout) }).then(r => r.ok, () => false);
// Why no native menu was seen, for failure details: what Accessibility read
// (System Events' answer, or its error) and whether the browser answers
// CDP. A browser that doesn't answer has a menu open that Accessibility
// didn't find (then what Accessibility sees, lib/native_menu_ax.js dump);
// one that answers has none.
export async function nativeMenuReport(pid = KOAN_PID) {
  const ax = nativeMenu('read', [], pid);
  const answers = await browserAnswers();
  return `Accessibility read ${JSON.stringify(ax)}; the browser ${answers ? 'answers' :
      `does not answer (a menu is open, or it hangs; Accessibility sees ${
          JSON.stringify(nativeMenu('dump', [], pid))})`}`;
}

// Why a native menu that opened can't be driven from the bench ('' when it
// can, or when none opened): the browser doesn't answer (an NSMenu's
// tracking loop holds it) and Koan has a menu-level window on screen, yet
// neither the Accessibility tree, nor a walk up from a hit test, nor a hit
// scan of that window reads an item (lib/native_menu_ax.js). Call it before
// closing the menu. The Mac cycles 16 to 18: the hit test reads the item
// under a point, but no element read from an attribute can be used again
// (the JavaScript for Automation bridge throws on it).
export async function nativeMenuStuckWhy(pid = KOAN_PID) {
  if (!IS_MAC || !pid || (pid === KOAN_PID && await browserAnswers())) return '';
  const r = nativeMenu('read', [], pid);
  if (r.open) return '';
  const hit = nativeMenu('dump', [], pid).hit;
  const wins = Array.isArray(hit?.windows) ? hit.windows : [];
  if (!wins.length) return '';
  return `a native menu opened (Koan's ${wins[0]} window), but Accessibility from the ` +
      `bench can't read its items (the walk up from the hit test stops at ` +
      `${JSON.stringify(hit.at)}, the hit scan read ${JSON.stringify(hit.scanned ?? null)}; ` +
      `trusted ${hit.trusted})`;
}

// Closes the open native menu, if any (AXCancel, then a real Escape where
// real input is on). Whether none is open now: Accessibility finds none and
// the browser answers (a menu Accessibility doesn't find still keeps the
// browser from answering; only a real Escape closes that one).
// A real Escape goes to the tracking menu (keypress2 posts to Koan's pid);
// one closes the innermost (sub)menu, so up to three are pressed.
export async function closeNativeMenu(pid = KOAN_PID) {
  const closed = async (timeout, answer = 2000) => !!(await waitNativeMenu(false, pid, timeout)) &&
      (pid !== KOAN_PID || await browserAnswers(answer));
  if (await closed(0)) return true;
  nativeMenu('cancel', [], pid);
  if (await closed(2000)) return true;
  if (pid !== KOAN_PID || !realInput()) return closed(0, 8000);
  for (let i = 0; i < 3; i++) {
    if (!(await realKey('Escape'))) break;
    if (await closed(1000, i === 2 ? 8000 : 3000)) return true;
  }
  return false;
}
// Presses the item at `path` (titles, or positions with separators counted,
// through submenus) of the open native menu, and waits for the menu to
// close. A miss closes the menu. {ok, detail}.
export async function pickNativeMenu(path, pid = KOAN_PID) {
  const r = nativeMenu('click', path, pid);
  if (!r.clicked) {
    await closeNativeMenu(pid);
    return { ok: false, detail: JSON.stringify({ path, ...r }) };
  }
  const closed = await waitNativeMenu(false, pid);
  if (!closed) await closeNativeMenu(pid);
  return { ok: !!closed, detail: closed ? r.clicked : `${r.clicked}: the menu stayed open` };
}

// A real click on the open native menu's item `title` (or position) at the
// top level, where a person clicks it (native_menu_ax.js point), and a wait
// for the menu to close. `click` makes the click (x, y in screen points;
// realClick by default). {ok, detail, point}.
export async function clickNativeMenuItem(title, { pid = KOAN_PID, click = realClick } = {}) {
  const r = nativeMenu('point', [title], pid);
  if (!r.point) {
    await closeNativeMenu(pid);
    return { ok: false, detail: JSON.stringify({ title, ...r }) };
  }
  if (!(await click(r.point.x, r.point.y))) {
    await closeNativeMenu(pid);
    return { ok: false, detail: `no real click (${realWhyNot || 'the gate refused it'})`, point: r.point };
  }
  const closed = await waitNativeMenu(false, pid);
  if (!closed) await closeNativeMenu(pid);
  return { ok: !!closed, detail: closed ? `${r.title} (${r.via})` : `${r.title}: the menu stayed open`,
           point: r.point };
}

export const CLOSE_WINDOW_ITEM ='menu item "Close Window" of menu 1 of menu bar item "File"';
// The app menu's Quit item (its title has the app's name).
export const QUIT_ITEM = '(first menu item of menu 1 of menu bar item 2 whose name starts with "Quit")';

// keypress2 flags: c cmd, s shift, k ctrl, a alt.
export async function realKey(name, flags = '', settle = 500) {
  if (!gate()) return false;
  execFileSync(`${TOOLS}/keypress2`, [String(KOAN_PID), String(macKeyCode(name)), flags],
               { timeout: 5000 });
  await sleep(settle);
  return true;
}
// The shown tab (its page) of `page`'s window, or null.
export async function shownTabOf(page) {
  const windowId = (await windowOf(page.id).catch(() => null))?.windowId;
  if (!windowId) return null;
  for (const p of await pages()) {
    if ((await windowOf(p.id).catch(() => null))?.windowId !== windowId) continue;
    const r = await onPage(p, c => c.send('Runtime.evaluate',
        { expression: 'document.visibilityState', returnByValue: true })).catch(() => null);
    if (r?.result?.result?.value === 'visible') return p;
  }
  return null;
}

// A main-menu key for `page` (Cmd+W, Cmd+Shift+W, Cmd+L, Cmd+T,
// Cmd+Shift+T, Ctrl+Tab, ...: CDP keys never reach the menu bar on macOS),
// or one of Chrome's own key equivalents outside Koan's shortcuts (Cmd+1,
// Ctrl+Shift+PageUp: a CDP Ctrl+PageDown was seen not to reach Chrome's on
// the Mac). On macOS a real key (realKey(), `flags` as keypress2's) once
// the page's window is key (makeKey() on the window's shown tab, so making
// the window key selects no other tab: the key acts on the window, as a
// CDP key to a background tab's page does elsewhere); elsewhere `cdpKey()`,
// the CDP key the script sends there. Returns whether the key went: false
// on macOS with real input off, where the caller skips the checks that
// need it (menuKeyWhyNot() says why).
export async function menuKey(page, name, flags, cdpKey, settle = 800) {
  if (!IS_MAC) {
    await cdpKey();
    return true;
  }
  if (!realOk) return false;
  if (!(await makeKey(await shownTabOf(page) ?? page))) {
    console.log(`  (${page?.url}: the page's window never got the focus)`);
  }
  return realKey(name, flags, settle);
}
export const menuKeyWhyNot = () =>
  `needs a real key on macOS (the main menu's, or Chrome's own key equivalent: CDP keys ` +
  `don't reach them); ${realWhyNot}`;

// Whether no modifier key is held on the Mac before a real press. Pointer
// events made from a NULL source take the modifiers the HID system holds, and
// on 2026-11-19 a held ⌘ turned every real press into a ⌘-click (tabs
// multi-selected, a press that deselected a tab and dragged nothing:
// pin_drag, space_drag). ptr.m and hold.m now give their events no
// modifiers; a held one is still released here (ptr release-mods) and said,
// and real input stops if it stays held. true where this ptr can't tell
// (built before `mods`).
let modsUnknownSaid = false;
function noHeldModifiers() {
  if (!ptrHas('release-mods')) {
    if (!modsUnknownSaid) {
      modsUnknownSaid = true;
      console.log(`  (real input: ${TOOLS}/ptr can't tell held modifier keys; rebuild it from ` +
                  'scripts/mac/tools/ptr.m)');
    }
    return true;
  }
  const run = op => {
    try {
      return execFileSync(`${TOOLS}/ptr`, [op], { encoding: 'utf8', timeout: 5000 }).trim();
    } catch (e) {
      return String(e.stdout ?? '').trim() || `ptr ${op}: exit ${e.status}`;
    }
  };
  const held = out => out.match(/held=(\S+)/)?.[1] ?? 'none';
  const before = run('mods');
  if (held(before) === 'none') return true;
  const after = run('release-mods');
  console.log(`  (real input: the Mac held ${held(before)} before a press (${before}); ` +
              `released: ${after})`);
  if (held(after) === 'none') return true;
  realOk = false;
  realWhyNot = `a modifier key stays held on the Mac (${after}): every real press would carry it`;
  return false;
}
const PRESSES = new Set(['click', 'rclick', 'altclick', 'cmdclick', 'down']);

// ptr <cmd> <args...> (see scripts/mac/tools/ptr.m), after the gate. A
// failure throws with why: ptr exits 2 for a command it doesn't know, which a
// bench binary built before that command was added does (down, dragto, up
// and cmdclick came in #265: the bench copies and builds the tools by hand,
// scripts/mac/README.md "Tools").
export async function ptr(...args) {
  if (!gate()) return false;
  if (PRESSES.has(args[0]) && !noHeldModifiers()) return false;
  try {
    execFileSync(`${TOOLS}/ptr`, args.map(String), { timeout: 30000 });
  } catch (e) {
    const how = e.status === 2 ? `exit 2: this ptr has no "${args[0]}"; rebuild ${TOOLS}/ptr ` +
                                 'from scripts/mac/tools/ptr.m'
        : e.signal ? `killed by ${e.signal}` : `exit ${e.status}`;
    const err = String(e.stderr ?? '').trim();
    throw new Error(`ptr ${args.join(' ')} failed (${how})${err ? `: ${err}` : ''}`);
  }
  return true;
}
// Whether the bench's ptr binary knows `cmd` (its name is a string in the
// binary; read, not run, so no input). false without the tool.
export function ptrHas(cmd) {
  try {
    return fs.readFileSync(`${TOOLS}/ptr`).includes(Buffer.from(`${cmd}\0`));
  } catch {
    return false;
  }
}
// hold down|to|up x y [ms] (see scripts/mac/tools/hold.m: a held-button
// drag, one step per call), after the gate.
export async function hold(cmd, x, y, ms) {
  if (!gate()) return false;
  if (!fs.existsSync(`${TOOLS}/hold`)) {
    realOk = false;
    realWhyNot = `no ${TOOLS}/hold (build scripts/mac/tools/hold.m)`;
    return false;
  }
  if (cmd === 'down' && !noHeldModifiers()) return false;
  const args = [cmd, Math.round(x), Math.round(y), ...(ms === undefined ? [] : [Math.round(ms)])];
  execFileSync(`${TOOLS}/hold`, args.map(String), { timeout: 30000 });
  return true;
}
export const realClick = async (x, y, settle = 600) => {
  const ok = await ptr('click', Math.round(x), Math.round(y));
  await sleep(settle);
  return ok;
};

// A window button of Koan's front browser window (the biggest standard
// window: the pill and border are child windows), read through
// Accessibility: {x, y} its centre in screen points, or {error}.
// `subrole` AXMinimizeButton, AXCloseButton or AXZoomButton.
export function windowButtonPoint(subrole = 'AXMinimizeButton', pid = KOAN_PID) {
  if (!IS_MAC || !pid) return { error: 'not macOS, or no Koan pid' };
  try {
    const out = execFileSync('osascript', ['-l', 'JavaScript', '-e',
        `const p = Application('System Events').applicationProcesses.whose({ unixId: ${pid} })[0];
         const area = w => { try { const [a, b] = w.size(); return a * b; } catch (e) { return 0; } };
         const ws = p.windows().filter(w => { try { return w.subrole() === 'AXStandardWindow'; } catch (e) { return false; } })
             .sort((a, b) => area(b) - area(a));
         let r = { error: 'no standard window' };
         for (const w of ws) {
           const b = w.buttons().find(x => { try { return x.subrole() === '${subrole}'; } catch (e) { return false; } });
           if (!b) { r = { error: 'no ${subrole}' }; continue; }
           const [x, y] = b.position(); const [bw, bh] = b.size();
           r = { x: Math.round(x + bw / 2), y: Math.round(y + bh / 2) };
           break;
         }
         JSON.stringify(r)`], { encoding: 'utf8', timeout: 10000 }).trim();
    return JSON.parse(out);
  } catch (e) {
    return { error: String(e.stderr || e.message).trim().split('\n')[0] };
  }
}
// Minimises Koan's window as a person does: a real click on its minimise
// button (CDP's Browser.setWindowBounds minimized did not minimise it on the
// Mac bench, cycle 19). `click` makes the click (realClick by default).
// {ok, why}.
export async function minimiseWithButton({ pid = KOAN_PID, click = realClick } = {}) {
  const at = windowButtonPoint('AXMinimizeButton', pid);
  if (at.error) return { ok: false, why: `the minimise button: ${at.error}` };
  // The traffic lights show on hover: the pointer there first.
  await ptr('move', at.x, at.y).catch(() => false);
  await sleep(300);
  if (!(await click(at.x, at.y))) return { ok: false, why: `no real click (${realWhyNot || 'the gate refused it'})` };
  return { ok: true, why: `clicked ${at.x},${at.y}` };
}
// Whether this Mac session can minimise any window now
// (scripts/mac/tools/can_minimise.m, built into a temporary directory if the
// bench has none): '' if it can, else why not. Asked once.
let canMinimiseWhy = null;
export function benchCannotMinimise() {
  if (canMinimiseWhy !== null) return canMinimiseWhy;
  if (!IS_MAC) return (canMinimiseWhy = 'not macOS');
  try {
    let tool = TOOLS && `${TOOLS}/can_minimise`;
    if (!tool || !fs.existsSync(tool)) {
      tool = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'koan-canmin-')), 'can_minimise');
      execFileSync('clang', ['-fobjc-arc', '-framework', 'Cocoa', '-o', tool,
                             new URL('../../mac/tools/can_minimise.m', import.meta.url).pathname],
                   { stdio: 'ignore', timeout: 120000 });
    }
    execFileSync(tool, [], { encoding: 'utf8', timeout: 15000 });
    canMinimiseWhy = '';
  } catch (e) {
    canMinimiseWhy = /minimised=0/.test(String(e.stdout ?? ''))
        ? 'this Mac session minimises no window now (a plain AppKit window of its own did not either: ' +
          'the session is locked, as it is while the person is away); run it unlocked and idle'
        : `can_minimise failed: ${String(e.message).split('\n')[0]}`;
  }
  return canMinimiseWhy;
}
// Brings minimised Koan windows back (Accessibility's AXMinimized; not
// input). Whether it could.
export function unminimise(pid = KOAN_PID) {
  if (!IS_MAC || !pid) return false;
  try {
    execFileSync('osascript', ['-l', 'JavaScript', '-e',
        `const p = Application('System Events').applicationProcesses.whose({ unixId: ${pid} })[0];
         for (const w of p.windows()) { try { if (w.attributes.byName('AXMinimized').value())
             w.attributes.byName('AXMinimized').value = false; } catch (e) {} }`],
        { timeout: 10000 });
    return true;
  } catch {
    return false;
  }
}

// Where the real pointer is, in screen points from the main screen's top-left
// (the coordinates of Browser.getWindowForTarget's bounds): {x, y}, read
// without moving it or counting as input (AppKit's NSEvent.mouseLocation).
// null off macOS, or if it can't be read.
export function realPointer() {
  if (!IS_MAC) return null;
  try {
    const out = execFileSync('osascript', ['-l', 'JavaScript', '-e',
        'ObjC.import("AppKit"); const p = $.NSEvent.mouseLocation; ' +
        'const h = $.NSScreen.screens.objectAtIndex(0).frame.size.height; `${p.x} ${h - p.y}`'],
        { encoding: 'utf8', timeout: 5000 });
    const [x, y] = out.trim().split(' ').map(Number);
    return Number.isFinite(x) && Number.isFinite(y) ? { x, y } : null;
  } catch {
    return null;
  }
}

// Why a check that the sidebar slides away can't run: the real pointer rests
// over the band of `width` the sidebar is out in, at the window's left (or
// right, `onRight`) edge, in the window that shows the first page. Upstream's
// expand-on-hover keeps the sidebar out while View::IsMouseHovered(), which
// reads the real pointer, whatever synthetic events say: someone at the Mac
// with the pointer left there holds it out. '' when the check can run
// (always off macOS).
export async function realPointerHoldsSidebar(width, onRight = false) {
  const p = realPointer();
  const page = p && (await pages())[0];
  const b = page && (await windowOf(page.id))?.bounds;
  if (!b) return '';
  const x = p.x - b.left, y = p.y - b.top;
  // (A few points more: the hot zone and the sidebar's resize area.)
  const band = width + 8;
  const over = y >= 0 && y < b.height && (onRight ? x >= b.width - band && x < b.width
                                                   : x >= 0 && x < band);
  return over ? `the real pointer rests over the sidebar (${Math.round(x)},${Math.round(y)} ` +
                'in the window): upstream keeps it out under the pointer' : '';
}
// Where the real pointer is in `page`'s window, in points from the window's
// top-left ({x, y}, with the window's bounds `b`), or null (off macOS, or
// unknown).
export async function realPointerInWindow(page) {
  const p = realPointer();
  const b = p && page && (await windowOf(page.id).catch(() => null))?.bounds;
  return b ? { x: p.x - b.left, y: p.y - b.top, b } : null;
}
// With real input, moves the real pointer to the middle of `page`'s window,
// 2/3 down: over the page, away from the sidebar (on either side) and the
// top bar, whose reveals upstream keeps while the real pointer is over them
// (View::IsMouseHovered(), whatever synthetic events say). The Mac cycle 18:
// a pointer left at 93,66 in the window held the sidebar out in
// settings_effects_visual, revealed Clear in sidebar_tabs and kept compact
// mode's toolbar in power. Whether the pointer rests there now: off macOS
// always; without real input only if it happens to.
export async function parkPointer(page) {
  if (!IS_MAC) return true;
  let at = await realPointerInWindow(page);
  if (!at) return false;
  const x = Math.round(at.b.width / 2), y = Math.round(at.b.height * 2 / 3);
  const near = a => Math.abs(a.x - x) < 120 && Math.abs(a.y - y) < 120;
  if (!near(at) && realInput()) {
    await ptr('move', at.b.left + x, at.b.top + y, 100);
    await sleep(300);
    at = await realPointerInWindow(page);
  }
  return !!at && near(at);
}
// Why a check after parkPointer() returned false may fail for the pointer
// alone: where it rests in the window.
export async function pointerNotParkedWhy(page, what) {
  const at = await realPointerInWindow(page);
  return `the real pointer rests ${at ? `at ${Math.round(at.x)},${Math.round(at.y)} in the window` :
                                        'where it can\'t be read'} and real input is ` +
         `${realInput() ? 'on but could not move it' : `off (${realInputWhyNot()})`}: ${what}`;
}

// check(name, ok, detail), or a skip when the real pointer holds the
// sidebar out (realPointerHoldsSidebar()).
export async function checkSlidesAway(check, name, ok, detail, width = 230) {
  const why = !ok && await realPointerHoldsSidebar(width);
  if (why) skipped(name, why);
  else check(name, ok, detail);
}

// Where a window's root view is on the screen, measured: the real pointer
// moves over `page` (the window's active tab) and the page's own mousemove
// gives screenX - clientX, the page's origin on screen; minus the page view's
// position in the root view (`webview`, from viewTree(): by default the
// biggest shown ContentsWebView). {x, y} or null.
export async function calibrate(page, vs, webview, windowTarget) {
  vs ??= await viewTree();
  // (`windowTarget`: a tab of the window, for a page that isn't a tab, e.g.
  // the welcome, which Browser.getWindowForTarget doesn't know.)
  const w = await windowOf((windowTarget ?? page).id).catch(() => null);
  if (!w?.bounds) return null;
  webview ??= vs.filter(v => v.cls === 'ContentsWebView' && v.shown && v.w > 100)
      .sort((a, b) => b.w * b.h - a.w * a.h)[0];
  if (!w || !webview) return null;
  await onPage(page, c => c.send('Runtime.evaluate', { expression: `
      window.__koanMove = null;
      addEventListener('mousemove', e => { window.__koanMove =
          { sx: e.screenX, sy: e.screenY, cx: e.clientX, cy: e.clientY }; }, true);` }));
  // Guess: the root view at the window's top-left; aim at the page's middle.
  const gx = w.bounds.left + webview.ax + webview.w / 2;
  const gy = w.bounds.top + webview.ay + webview.h / 2;
  if (!(await ptr('move', Math.round(gx), Math.round(gy), 200))) return null;
  await ptr('move', Math.round(gx) + 3, Math.round(gy) + 2, 100);
  await sleep(300);
  const m = (await onPage(page, c => c.send('Runtime.evaluate',
      { expression: 'window.__koanMove', returnByValue: true }))).result?.result?.value;
  if (!m) return null;
  return { x: m.sx - m.cx - webview.ax, y: m.sy - m.cy - webview.ay };
}

// How a person reaches an agent's tab (an agent's own tabs live in its own
// Space, design 2026-11-10): a real click on the sidebar's agent chip (or,
// with the sidebar hidden, the corner dot), then a real click on its menu's
// "Go to <title> (<agent>...)", which switches to the tab's Space and selects
// it. CDP can't do it: Koan holds a CDP activation of a tab an agent holds
// (#232). `page` is a tab of the person's in that window, never an agent's:
// it is brought to the front (makeKey(), CDP: a user's tab isn't held) to
// calibrate the real pointer. Needs real input. {ok, why, items}.
export async function goToAgentTab(title, page, settle = 900) {
  if (!realInput()) return { ok: false, why: realInputWhyNot(), items: [] };
  await makeKey(page);
  const vs = await viewTree();
  const origin = await calibrate(page, vs);
  if (!origin) return { ok: false, why: 'could not calibrate the real pointer', items: [] };
  const chip = vs.find(v => v.cls === 'KoanAgentChipButton' && v.shown && v.w > 0) ??
               vs.find(v => v.cls === 'KoanAgentCornerDot' && v.shown && v.w > 0);
  if (!chip) return { ok: false, why: 'no agent chip or corner dot on screen', items: [] };
  // The corner dot is a widget of its own: its origin from the window's.
  let at = { x: origin.x + chip.ax + chip.w / 2, y: origin.y + chip.ay + chip.h / 2 };
  if (chip.cls === 'KoanAgentCornerDot') {
    const r = await rootOfWidgetOnScreen(chip.wname);
    if (r) at = { x: r.x + chip.ax + chip.w / 2, y: r.y + chip.ay + chip.h / 2 };
  }
  if (!(await realClick(at.x, at.y, 300))) return { ok: false, why: `chip: ${realWhyNot}`, items: [] };
  const menu = await waitNativeMenu(true, KOAN_PID, 4000);
  const items = menu?.items ?? [];
  if (!menu) return { ok: false, why: 'the chip\'s menu did not open', items };
  const item = items.find(i => (i ?? '').startsWith(`Go to ${title} (`));
  if (!item) {
    await closeNativeMenu();
    return { ok: false, why: `no "Go to ${title} (...)" item`, items };
  }
  const picked = await clickNativeMenuItem(item);
  await sleep(settle);
  return { ok: picked.ok, why: picked.detail, items, item };
}
// A widget's origin on the screen in points, by its name (UI DevTools gives
// a widget's bounds in screen points), or null.
async function rootOfWidgetOnScreen(name) {
  const ui = await connect(`ws://127.0.0.1:${process.env.UI_PORT}/0`);
  try {
    await ui.send('DOM.enable');
    await ui.send('CSS.enable');
    const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
    let found = null;
    const walk = n => {
      const a = n.attributes ?? [];
      if (!found && n.nodeName === 'Widget' && a[a.indexOf('name') + 1] === name) found = n;
      for (const c of n.children ?? []) walk(c);
    };
    walk(doc);
    if (!found) return null;
    const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: found.nodeId });
    const p = {};
    for (const rule of r.result?.matchedCSSRules ?? []) {
      for (const prop of rule.rule.style.cssProperties) p[prop.name.toLowerCase()] = prop.value;
    }
    return { x: +p.x, y: +p.y };
  } finally {
    ui.close();
  }
}

// --- The window's pixels (for the checks that read headless Ozone's frame
// dumps off macOS).

// The bench's winid tool (scripts/mac/tools/winid.m: a pid's on-screen
// windows), built into a temporary directory if the bench has none.
let winidPath = null;
function winidTool() {
  if (winidPath) return winidPath;
  const bench = TOOLS && `${TOOLS}/winid`;
  if (bench && fs.existsSync(bench)) return (winidPath = bench);
  const built = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'koan-winid-')), 'winid');
  execFileSync('clang', ['-fobjc-arc', '-framework', 'CoreGraphics', '-framework', 'Foundation',
                         '-o', built, new URL('../../mac/tools/winid.m', import.meta.url).pathname],
               { stdio: 'ignore', timeout: 60000 });
  return (winidPath = built);
}

// The window showing `page` as the screen has it: `screencapture -x -o
// -l<window>` (that window alone, no shadow, no sound), converted from the
// display's colour space to sRGB (sips), decoded (decodePng()) with `scale`,
// the pixels per point. The window is found by its bounds among Koan's pid's
// windows. {frame} or {error}. Needs the screen-recording grant the bench's
// visual checks have; a translucent (glass) window shows the desktop through.
export async function captureWindow(page) {
  if (!IS_MAC || !KOAN_PID) return { error: 'macOS only, with KOAN_PID' };
  const b = (await windowOf(page.id))?.bounds;
  if (!b) return { error: 'no window for the page' };
  try {
    const near = (x, y) => Math.abs(x - y) <= 2;
    const lines = execFileSync(winidTool(), [String(KOAN_PID)], { encoding: 'utf8', timeout: 10000 })
        .trim().split('\n').map(l => l.split(' ').map(Number));
    const w = lines.find(f => f[5] === 0 && near(f[1], b.left) && near(f[2], b.top) &&
                              near(f[3], b.width) && near(f[4], b.height));
    if (!w) return { error: `no on-screen window at ${JSON.stringify(b)}: ${lines.join('; ')}` };
    const file = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'koan-shot-')), 'window.png');
    execFileSync('screencapture', ['-x', '-o', `-l${w[0]}`, file], { stdio: 'ignore', timeout: 15000 });
    execFileSync('sips', ['--matchTo', '/System/Library/ColorSync/Profiles/sRGB Profile.icc', file],
                 { stdio: 'ignore', timeout: 15000 });
    const frame = decodePng(file);
    fs.rmSync(path.dirname(file), { recursive: true, force: true });
    return { frame: { ...frame, scale: frame.width / b.width } };
  } catch (e) {
    return { error: String(e.message ?? e).split('\n')[0] };
  }
}

// --- A local web server (no network needed). /p?t=<title>&b=<colour>
// serves a page with that title and background; other paths come from
// `routes` ({path: html, or a function of the request that returns it}).
export async function startServer(routes = {}) {
  const server = http.createServer((req, res) => {
    const u = new URL(req.url, 'http://x');
    const route = routes[u.pathname];
    const html = (typeof route === 'function' ? route(req) : route) ?? `<!doctype html><title>${
        (u.searchParams.get('t') ?? 'Page').replace(/[<&]/g, '')}</title>
        <body style="margin:0;background:${(u.searchParams.get('b') ?? '#fff').replace(/[^#\w]/g, '')}">
        <h1>${(u.searchParams.get('t') ?? 'Page').replace(/[<&]/g, '')}</h1></body>`;
    res.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' });
    res.end(html);
  });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const { port } = server.address();
  return { port, url: (p = '/') => `http://127.0.0.1:${port}${p}`, close: () => server.close() };
}

// --- Skips. A test prints "SKIP <what>: <why>" for checks it could not run;
// suite.sh counts them. Exit 77 (skip()) when nothing could run at all.
export function skipped(what, why) {
  console.log(`SKIP  ${what}: ${why}`);
}
export function skip(why) {
  console.log(`SKIP  ${why}`);
  process.exit(77);
}
