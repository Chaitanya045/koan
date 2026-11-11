// What the video flows share (see ../README.md): the gates (our Koan, the
// user away, real input, checked before every pointer event), the marks log
// in $MARKS, the local pages, the model's tabs (test_helper) and the
// sidebar's rows (view tree), and where a view is on screen. A flow calls
// startFlow() first and setup() once Koan has its anchor tab.
// (clear.mjs predates this file and keeps its own copy of these.)
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import { CDP_PORT, UI_PORT, browserCall, connect, onPage, pages, sleep, viewTree,
         waitFor } from '../../../e2e/lib/koan.mjs';
import { KOAN_PID, calibrate, ptr, realClick, realInput, realInputWhyNot,
         windowOf } from '../../../e2e/lib/mac.mjs';

export const TOOLS = process.env.KOAN_MAC_TOOLS;
export const TEST_HELPER_ID = 'efmifboglkpejppaiocdcajnkhbfoofo';

// --- The log: {flow, window, sidebar, marks: [{t, round, label, action}],
// rounds, stopped}, rewritten after each mark.
let MARKS = null;
export const out = { flow: null, startedAt: Date.now(), koanPid: KOAN_PID, cdpPort: CDP_PORT,
                     window: null, sidebar: null, marks: [], rounds: [], stopped: null };
let round = 0;
let server = null;
const sockets = new Set();
export const setRound = n => { round = n; };
export const save = () => fs.writeFileSync(MARKS, JSON.stringify(out, null, 1));
export function mark(label, extra = {}, action = false) {
  const m = { t: Date.now(), round, label, action, ...extra };
  out.marks.push(m);
  save();
  console.log(`${new Date(m.t).toISOString().slice(11, 23)} r${round} ${action ? '*' : ' '} ${label}` +
              `${Object.keys(extra).length ? ' ' + JSON.stringify(extra) : ''}`);
  return m;
}
export function stop(why, code = 3) {
  out.stopped = why;
  mark('stopped', { why });
  console.log(`STOPPED: ${why}`);
  finish();
  process.exit(code);
}
export function finish() {
  for (const s of sockets) s.destroy();
  server?.close();
}

// --- Gates: our Koan, the user away, real input.
const sh = (cmd, args) => execFileSync(cmd, args, { encoding: 'utf8', timeout: 10000 }).trim();
const idle = () => Number(sh(`${TOOLS}/usergate.sh`, []));
export function startFlow(name) {
  out.flow = name;
  MARKS = process.env.MARKS ?? `${name}-${Date.now()}.marks.json`;
  if (!KOAN_PID || !UI_PORT || !CDP_PORT) stop('KOAN_PID, UI_PORT and CDP_PORT must come from launch.sh', 2);
  const listener = sh('lsof', ['-nP', `-iTCP:${CDP_PORT}`, '-sTCP:LISTEN', '-t']).split('\n')[0];
  const cmd = sh('ps', ['-o', 'command=', '-p', String(KOAN_PID)]);
  if (listener !== String(KOAN_PID) || !/--user-data-dir=\S*\/rec-/.test(cmd)) {
    stop(`port ${CDP_PORT} is not answered by a launch.sh Koan (listener ${listener}, pid ${KOAN_PID})`, 2);
  }
  if (!(idle() >= 600)) stop(`the Mac was used ${idle()} s ago (need 600 s)`, 77);
  if (!realInput()) stop(realInputWhyNot(), 77);
  return MARKS;
}
// Our last pointer event: ptr writes its time (s) to own-input.ts. Input
// newer than that, by more than a little, is someone else's.
function userBack() {
  let own = 0;
  try {
    own = Number(fs.readFileSync(`${TOOLS}/own-input.ts`, 'utf8'));
  } catch {}
  const sinceOwn = Date.now() / 1000 - own;
  const i = idle();
  return own && i + 3 < sinceOwn ? `input ${i} s ago, ours ${Math.round(sinceOwn)} s ago` : '';
}
export async function pointer(...args) {
  const back = userBack();
  if (back) stop(`the user is back (${back})`);
  if (!(await ptr(...args))) stop(`real input stopped: ${realInputWhyNot()}`);
}
export async function clickAt(x, y) {
  const back = userBack();
  if (back) stop(`the user is back (${back})`);
  if (!(await realClick(x, y, 0))) stop(`real input stopped: ${realInputWhyNot()}`);
}

// --- Pages: /p?t=<title>&b=<colour> (loaded at once).
const COLORS = ['#fde2e2', '#e2f0fd', '#e6fde2', '#fdf6e2', '#efe2fd', '#e2fdf8', '#fde2f4', '#eeeeee'];
const esc = s => String(s ?? '').replace(/[^\w .#-]/g, '');
export async function startPages() {
  server = http.createServer((req, res) => {
    const u = new URL(req.url, 'http://x');
    const t = esc(u.searchParams.get('t') ?? 'Page'), b = esc(u.searchParams.get('b') ?? '#fff');
    res.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' });
    res.end(`<!doctype html><title>${t}</title><body style="margin:0;background:${b};font:28px system-ui">` +
            `<h1 style="margin:40px">${t}</h1>`);
  });
  server.on('connection', s => {
    sockets.add(s);
    s.on('close', () => sockets.delete(s));
  });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const origin = `http://127.0.0.1:${server.address().port}`;
  return {
    origin,
    pageUrl: (title, i) => `${origin}/p?t=${encodeURIComponent(title)}&b=${encodeURIComponent(COLORS[i % COLORS.length])}`,
  };
}

// --- The model (test_helper's chrome.tabs, asked from `page`, a loaded
// 127.0.0.1 page).
export const findPage = async test => (await pages()).find(test);
export async function activate(page) {
  await browserCall('Target.activateTarget', { targetId: page.id });
  await sleep(400);
}
export async function modelTabs(page) {
  const r = await onPage(page, c => c.send('Runtime.evaluate', {
    expression: `chrome.runtime.sendMessage('${TEST_HELPER_ID}', 'list-tabs')`,
    awaitPromise: true, returnByValue: true }));
  return Array.isArray(r.result?.result?.value) ? r.result.result.value : null;
}
export async function closeTabs(page, ids) {
  if (!ids.length) return;
  await onPage(page, c => c.send('Runtime.evaluate', {
    expression: `chrome.runtime.sendMessage('${TEST_HELPER_ID}', {close: ${JSON.stringify(ids)}})`,
    awaitPromise: true, returnByValue: true }));
  await sleep(800);
}

// --- The sidebar. A KoanTabRow is a hidden helper view inside its TabView
// (it carries the tab's properties): the row is on screen when that TabView
// is. viewTree() is in tree order, so the parent is the latest TabView before
// it; `grid` says whether that TabView is an Essentials tile.
export function sidebarRows(vs) {
  const rows = [];
  let tabView = null;
  for (const v of vs) {
    if (v.cls === 'TabView') tabView = v;
    else if (v.cls === 'KoanTabRow' && v.path.at(-1) === 'TabView') {
      rows.push({ ...v, tabView, grid: tabView.path.includes('PinnedTabContainerView') });
    }
  }
  return rows.filter(v => v.tabView?.shown && v.tabView.w > 0 && v.tabView.h > 0);
}
export const clearButtonOf = vs => vs.find(v => v.cls === 'KoanClearButton' && v.shown && v.w > 0);
export const compactOn = vs => {
  const bv = vs.find(v => v.cls === 'BrowserView');
  return !!vs.find(v => v.cls === 'KoanHotZone' && v.shown && bv && v.h > bv.h / 2);
};
export const regionOf = vs => vs.find(v => v.cls === 'VerticalTabStripRegionView' && v.shown && v.w > 0);

// Where a view is on screen (points): UI DevTools' box model when it has one
// (screen bounds, any widget), else its position in the browser window's
// root view plus the root's origin as setup() measured it.
let rootOrigin = null;  // {dx, dy}: the root view's origin minus the window's.
export async function screenRect(view, page) {
  try {
    const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
    await ui.send('DOM.enable');
    const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
    // Node ids are handed out again per document: find it by class and size.
    const same = [];
    const walk = n => {
      const a = n.attributes ?? [];
      if (n.nodeName === 'View' && a[a.indexOf('class') + 1] === view.cls) same.push(n.nodeId);
      for (const c of n.children ?? []) walk(c);
    };
    walk(doc);
    for (const id of same) {
      const q = (await ui.send('DOM.getBoxModel', { nodeId: id })).result?.model?.border;
      if (q && Math.round(q[2] - q[0]) === view.w && Math.round(q[5] - q[1]) === view.h) {
        ui.close();
        return { x: q[0], y: q[1], w: view.w, h: view.h, via: 'boxModel' };
      }
    }
    ui.close();
  } catch {}
  if (!rootOrigin || !page) return null;
  const w = (await windowOf(page.id))?.bounds;
  return { x: w.left + rootOrigin.dx + view.ax, y: w.top + rootOrigin.dy + view.ay, w: view.w, h: view.h,
           via: 'rootOrigin' };
}

// Opens `url` as the anchor tab (closing the launch's about:blank), checks
// that test_helper answers, measures the root view's origin with the real
// pointer and records the window's and the sidebar's rects (frames.sh
// --crop sidebar). Returns the anchor page.
export async function setup(url) {
  await browserCall('Target.createTarget', { url, background: false });
  const anchor = await waitFor(() => findPage(p => p.url === url), 8000);
  for (const p of await pages()) {
    if (p.url === 'about:blank') await browserCall('Target.closeTarget', { targetId: p.id });
  }
  await sleep(1500);
  if (!anchor) stop('no anchor page', 2);
  if (!(await modelTabs(anchor))) stop('test_helper does not answer (launch.sh loads it)', 2);
  const vs = await viewTree();
  const w = (await windowOf(anchor.id))?.bounds;
  out.window = { x: w.left, y: w.top, w: w.width, h: w.height };
  mark('calibrate', {}, true);
  const origin0 = await calibrate(anchor, vs);
  if (origin0) rootOrigin = { dx: origin0.x - w.left, dy: origin0.y - w.top };
  const region = regionOf(vs);
  if (region) {
    const r = await screenRect(region, anchor);
    if (r) out.sidebar = { x: Math.round(r.x), y: Math.round(r.y), w: region.w, h: region.h };
  }
  mark('setup', { window: out.window, rootOrigin, sidebar: out.sidebar });
  return anchor;
}
