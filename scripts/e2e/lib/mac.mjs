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
import { ACCEL, ALT, CTRL, IS_MAC, META, SHIFT, browserCall, connect, onPage, pages, sleep, typeInto, viewTree,
         waitFor } from './koan.mjs';

export { ACCEL, ALT, CTRL, IS_MAC, META, SHIFT };

// kVK_* (HIToolbox/Events.h) by key name.
const MAC_VK = {
  a: 0, s: 1, d: 2, f: 3, h: 4, g: 5, z: 6, x: 7, c: 8, v: 9, b: 11, q: 12,
  w: 13, e: 14, r: 15, y: 16, t: 17, 1: 18, 2: 19, 3: 20, 4: 21, 6: 22, 5: 23,
  9: 25, 7: 26, 8: 28, 0: 29, o: 31, u: 32, i: 34, p: 35, l: 37, j: 38,
  k: 40, n: 45, m: 46, Enter: 36, Tab: 48, ' ': 49, Backspace: 51,
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

export const KOAN_PID = Number(process.env.KOAN_PID || 0);
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
export async function rootMouse(root, types, x, y, button = 'left') {
  const ui = await connect(`ws://127.0.0.1:${process.env.UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('DOM.getDocument', { depth: -1 });
  for (const type of types) {
    await ui.send('DOM.dispatchMouseEvent', { nodeId: root.nodeId,
        event: { type, x: Math.round(x), y: Math.round(y), button, wheelDirection: 'none' } });
  }
  ui.close();
}

// --- Spaces.

// Creates a Space named `name` with Zen's Create-a-Space form (Ctrl+Alt+N,
// Ctrl on macOS too), from `page`. Whether the form came up.
export async function createSpace(page, name) {
  await key(page, 'n', CTRL | ALT, 1200);
  const field = await waitFor(async () => (await viewTree()).find(
      v => v.cls === 'Textfield' && v.shown && v.path.includes('KoanSpaceCreationView')), 5000);
  if (!field) return false;
  await typeInto(field, name, 13);
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
export const CLOSE_WINDOW_ITEM = 'menu item "Close Window" of menu 1 of menu bar item "File"';
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
// ptr <cmd> <args...> (see scripts/mac/tools/ptr.m), after the gate.
export async function ptr(...args) {
  if (!gate()) return false;
  execFileSync(`${TOOLS}/ptr`, args.map(String), { timeout: 30000 });
  return true;
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
  const args = [cmd, Math.round(x), Math.round(y), ...(ms === undefined ? [] : [Math.round(ms)])];
  execFileSync(`${TOOLS}/hold`, args.map(String), { timeout: 30000 });
  return true;
}
export const realClick = async (x, y, settle = 600) => {
  const ok = await ptr('click', Math.round(x), Math.round(y));
  await sleep(settle);
  return ok;
};

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
export async function calibrate(page, vs, webview) {
  vs ??= await viewTree();
  const w = await windowOf(page.id);
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
