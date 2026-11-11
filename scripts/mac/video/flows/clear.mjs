// Video flow: the sidebar's Clear button, clicked with the real pointer, 10
// rounds, while record.sh records the window (see ../README.md). Per round:
// open 6 tabs in a Space (some still loading), click "Clear" where the view
// tree says it is on screen, then count the Space's tabs, as the sidebar
// lists them (KoanTabRow views) and as the model has them (chrome.tabs,
// through the e2e test_helper extension), right after the click and twice
// more. The rounds vary: tabs still loading, a tab pinned in the Space, an
// Essential, compact mode (the sidebar revealed at the window's edge first),
// two Spaces (Clear in the first and in the second).
//
// Every step is a mark, {t: Date.now(), round, label, action, ...}, in
// $MARKS (JSON, rewritten after each mark); frames.sh cuts the video around
// the action marks. The counts per round are in its "rounds".
//
// Needs the Koan that launch.sh started (its koan.env: KOAN_PID, UI_PORT,
// CDP_PORT, KOAN_MAC_TOOLS, KOAN_INPUT=real), the user away (usergate.sh
// >= 600 s) and the screen granted. Every pointer event checks that Koan's
// exact pid is frontmost (lib/mac.mjs, inputok.sh), and that nobody but us
// used the Mac since our last event (HIDIdleTime against ptr's
// own-input.ts); otherwise the flow stops, says why, and leaves the marks so
// far. It quits nothing: quit.sh does, by pid.
//
//   set -a; . <rec dir>/koan.env; set +a
//   MARKS=<rec dir>/clear.marks.json node scripts/mac/video/flows/clear.mjs [rounds, e.g. 1,6]
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import { ACCEL, ALT, CDP_PORT, SHIFT, UI_PORT, browserCall, connect, key, onPage, pages, sleep,
         viewTree, waitFor } from '../../../e2e/lib/koan.mjs';
import { KOAN_PID, askTestHelper, calibrate, createSpace, ptr, realClick, realInput, realInputWhyNot,
         spaceButtons, windowOf } from '../../../e2e/lib/mac.mjs';

const TOOLS = process.env.KOAN_MAC_TOOLS;
const MARKS = process.env.MARKS ?? `clear-${Date.now()}.marks.json`;
const TEST_HELPER_ID = 'efmifboglkpejppaiocdcajnkhbfoofo';
const VK_S = 83, VK_D = 68, VK_RIGHT = 39, VK_LEFT = 37;

// --- The log.
const out = { flow: 'clear', startedAt: Date.now(), koanPid: KOAN_PID, cdpPort: CDP_PORT,
              window: null, sidebar: null, marks: [], rounds: [], stopped: null };
const save = () => fs.writeFileSync(MARKS, JSON.stringify(out, null, 1));
let round = 0;
let server = null;
function mark(label, extra = {}, action = false) {
  const m = { t: Date.now(), round, label, action, ...extra };
  out.marks.push(m);
  save();
  console.log(`${new Date(m.t).toISOString().slice(11, 23)} r${round} ${action ? '*' : ' '} ${label}` +
              `${Object.keys(extra).length ? ' ' + JSON.stringify(extra) : ''}`);
  return m;
}
function stop(why, code = 3) {
  out.stopped = why;
  mark('stopped', { why });
  console.log(`STOPPED: ${why}`);
  server?.close();
  process.exit(code);
}

// --- Gates: our Koan, the user away, real input.
const sh = (cmd, args) => execFileSync(cmd, args, { encoding: 'utf8', timeout: 10000 }).trim();
if (!KOAN_PID || !UI_PORT || !CDP_PORT) stop('KOAN_PID, UI_PORT and CDP_PORT must come from launch.sh', 2);
{
  const listener = sh('lsof', ['-nP', `-iTCP:${CDP_PORT}`, '-sTCP:LISTEN', '-t']).split('\n')[0];
  const cmd = sh('ps', ['-o', 'command=', '-p', String(KOAN_PID)]);
  if (listener !== String(KOAN_PID) || !/--user-data-dir=\S*\/rec-/.test(cmd)) {
    stop(`port ${CDP_PORT} is not answered by a launch.sh Koan (listener ${listener}, pid ${KOAN_PID})`, 2);
  }
}
const idle = () => Number(sh(`${TOOLS}/usergate.sh`, []));
if (!(idle() >= 600)) stop(`the Mac was used ${idle()} s ago (need 600 s)`, 77);
if (!realInput()) stop(realInputWhyNot(), 77);
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
async function pointer(...args) {
  const back = userBack();
  if (back) stop(`the user is back (${back})`);
  if (!(await ptr(...args))) stop(`real input stopped: ${realInputWhyNot()}`);
}
async function clickAt(x, y) {
  const back = userBack();
  if (back) stop(`the user is back (${back})`);
  if (!(await realClick(x, y, 0))) stop(`real input stopped: ${realInputWhyNot()}`);
}

// --- Pages: /p (loaded), /slow (headers and a title at once, the rest after
// `ms`: the tab keeps loading), /hang (nothing for `ms`: no title, spinner).
const sockets = new Set();
const esc = s => String(s ?? '').replace(/[^\w .#-]/g, '');
server = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  const t = esc(u.searchParams.get('t') ?? 'Page'), b = esc(u.searchParams.get('b') ?? '#fff');
  const ms = Math.min(Number(u.searchParams.get('ms') ?? 20000), 60000);
  const head = `<!doctype html><title>${t}</title><body style="margin:0;background:${b};font:28px system-ui">` +
               `<h1 style="margin:40px">${t}</h1>`;
  res.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' });
  if (u.pathname === '/slow') {
    res.write(head);
    setTimeout(() => res.end('<p>loaded</p></body>'), ms);
  } else if (u.pathname === '/hang') {
    setTimeout(() => res.end(head), ms);
  } else {
    res.end(head);
  }
});
server.on('connection', s => {
  sockets.add(s);
  s.on('close', () => sockets.delete(s));
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const origin = `http://127.0.0.1:${server.address().port}`;
const COLORS = ['#fde2e2', '#e2f0fd', '#e6fde2', '#fdf6e2', '#efe2fd', '#e2fdf8', '#fde2f4', '#eeeeee'];
const pageUrl = (kind, title, i) =>
  `${origin}/${kind}?t=${encodeURIComponent(title)}&b=${encodeURIComponent(COLORS[i % COLORS.length])}`;

// --- Browser helpers.
const findPage = async test => (await pages()).find(test);
async function activate(page) {
  await browserCall('Target.activateTarget', { targetId: page.id });
  await sleep(400);
}
// The window's tabs from the model (test_helper's chrome.tabs.query), asked
// from `page` (a loaded 127.0.0.1 page).
async function modelTabs(page) {
  const r = await onPage(page, c => c.send('Runtime.evaluate', {
    expression: `chrome.runtime.sendMessage('${TEST_HELPER_ID}', 'list-tabs')`,
    awaitPromise: true, returnByValue: true }));
  return Array.isArray(r.result?.result?.value) ? r.result.result.value : null;
}
async function closeTabs(page, ids) {
  if (!ids.length) return;
  await onPage(page, c => c.send('Runtime.evaluate', {
    expression: `chrome.runtime.sendMessage('${TEST_HELPER_ID}', {close: ${JSON.stringify(ids)}})`,
    awaitPromise: true, returnByValue: true }));
  await sleep(800);
}
// The sidebar's rows (KoanTabRow), and the Clear button, from the view tree.
// A KoanTabRow is a hidden helper view inside its TabView (it carries the
// tab's properties): the row is on screen when that TabView is. viewTree() is
// in tree order, so the parent is the latest TabView before it.
function sidebarRows(vs) {
  const rows = [];
  let tabView = null;
  for (const v of vs) {
    if (v.cls === 'TabView') tabView = v;
    else if (v.cls === 'KoanTabRow') rows.push({ ...v, tabView: v.path.at(-1) === 'TabView' ? tabView : null });
  }
  const shown = rows.filter(v => v.tabView?.shown && v.tabView.w > 0 && v.tabView.h > 0);
  const kind = v => (v.props.Essential === 'true' ? 'essential' : v.props.PinnedInSpace === 'true' ? 'pinned' : 'normal');
  const count = list => list.reduce((n, v) => ({ ...n, [kind(v)]: (n[kind(v)] ?? 0) + 1 }), {});
  return { shown: count(shown), all: count(rows), urls: shown.map(v => [kind(v), v.props.Url ?? '']) };
}
const clearButtonOf = vs => vs.find(v => v.cls === 'KoanClearButton' && v.shown && v.w > 0);
const compactOn = vs => {
  const bv = vs.find(v => v.cls === 'BrowserView');
  return !!vs.find(v => v.cls === 'KoanHotZone' && v.shown && bv && v.h > bv.h / 2);
};
const regionOf = vs => vs.find(v => v.cls === 'VerticalTabStripRegionView' && v.shown && v.w > 0);

// Where a view is on screen (points): UI DevTools' box model when it has one
// (screen bounds, any widget), else its position in the browser window's
// root view plus the root's measured screen origin.
let rootOrigin = null;  // {dx, dy}: the root view's origin minus the window's.
async function screenRect(view) {
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
  if (!rootOrigin) return null;
  const w = (await windowOf((await anchorPage()).id))?.bounds;
  return { x: w.left + rootOrigin.dx + view.ax, y: w.top + rootOrigin.dy + view.ay, w: view.w, h: view.h,
           via: 'rootOrigin' };
}

// --- Spaces. Each Space has an anchor tab (a loaded 127.0.0.1 page, the
// selected tab when Clear is clicked, so it stays) to ask test_helper from.
const anchorUrl = s => `${origin}/p?t=${encodeURIComponent(`Anchor ${s}`)}&b=%23ffffff&anchor=${s}`;
let space = 'A';
let haveB = false;
const anchorPage = async (s = space) => findPage(p => p.url.endsWith(`anchor=${s}`));
async function switchSpace(to) {
  if (to === space) return;
  const page = await anchorPage();
  mark(`space-switch-${to}`, {}, true);
  await key(page, to === 'B' ? VK_RIGHT : VK_LEFT, ACCEL | ALT, to === 'B' ? 'ArrowRight' : 'ArrowLeft');
  await sleep(1500);
  space = to;
  const a = await anchorPage();
  if (a) await activate(a);
}
async function ensureSpaceB() {
  if (haveB) return true;
  const before = new Set((await pages()).map(p => p.id));
  mark('space-create-B', {}, true);
  if (!(await createSpace(await anchorPage('A'), 'Video B'))) {
    mark('space-create-failed');
    return false;
  }
  await sleep(1500);
  space = 'B';
  // The new Space's tab, or a new one, becomes B's anchor.
  const fresh = (await pages()).find(p => !before.has(p.id));
  if (fresh) {
    await onPage(fresh, c => c.send('Page.navigate', { url: anchorUrl('B') }));
  } else {
    await browserCall('Target.createTarget', { url: anchorUrl('B'), background: false });
  }
  await waitFor(() => anchorPage('B'), 8000);
  await sleep(1000);
  const buttons = spaceButtons(await viewTree());
  mark('space-B-ready', { spaces: buttons.map(b => `${b.name}${b.active ? '*' : ''}`) });
  haveB = buttons.length >= 2;
  await switchSpace('A');
  return haveB;
}

// Closes everything but the anchors (Essentials and pinned tabs too), in
// both Spaces, so each round starts from the same state.
async function reset() {
  const a = await anchorPage('A');
  const tabs = (await modelTabs(a)) ?? [];
  const keep = new Set(tabs.filter(t => /anchor=[AB]$/.test(t.url)).map(t => t.id));
  await closeTabs(a, tabs.filter(t => !keep.has(t.id)).map(t => t.id));
  const left = (await modelTabs(a)) ?? [];
  if (left.length > keep.size) mark('reset-left-tabs', { left: left.map(t => t.url) });
}

// Opens `n` tabs in the current Space, in the background (the anchor stays
// selected); `loading` of them never finish loading during the round.
async function openTabs(n, loading, tag) {
  const urls = [];
  for (let i = 0; i < n; i++) {
    const kind = i < loading ? (i % 2 ? 'hang' : 'slow') : 'p';
    urls.push(pageUrl(kind, `${tag} ${i + 1}${kind === 'p' ? '' : ' (loading)'}`, i));
  }
  for (const url of urls) {
    await browserCall('Target.createTarget', { url, background: true });
    await sleep(250);
  }
  return urls;
}

// The Space's state: model and sidebar. `anchor` asks.
async function snapshot(label) {
  const a = await anchorPage();
  const listed = a ? await modelTabs(a) : null;
  if (!listed) mark('model-unreadable', { anchor: !!a });
  const tabs = listed ?? [];
  const me = tabs.find(t => t.asker);
  const group = me?.groupId ?? -1;
  const vs = await viewTree();
  const rows = sidebarRows(vs);
  const inSpace = tabs.filter(t => !t.pinned && t.groupId === group);
  const pinnedUrls = new Set(rows.urls.filter(([k]) => k === 'pinned').map(([, u]) => u));
  const byGroup = {};
  for (const t of tabs) byGroup[t.pinned ? 'essential' : t.groupId] = (byGroup[t.pinned ? 'essential' : t.groupId] ?? 0) + 1;
  const s = {
    label,
    group,
    model: {
      window: tabs.length,
      space: inSpace.length,
      // What Clear should have closed: the Space's unpinned tabs, but the
      // selected one (Koan keeps it, and audible ones), and Koan's fresh tab.
      closable: inSpace.filter(t => !t.active && !t.audible && !pinnedUrls.has(t.url) &&
                                    !/^(chrome|about):/.test(t.url)).length,
      loading: inSpace.filter(t => t.status === 'loading').length,
      essentials: tabs.filter(t => t.pinned).length,
      byGroup,
    },
    rows: rows.shown,
    rowsAll: rows.all,
    clearShown: !!clearButtonOf(vs),
    compact: compactOn(vs),
  };
  mark(`count-${label}`, { model: s.model, rows: s.rows, clearShown: s.clearShown });
  return s;
}

// Hovers Clear (so it shows "Clear" rather than the line), then clicks it
// with the real pointer. In compact mode the sidebar is revealed first.
async function clickClear(compact) {
  const w = (await windowOf((await anchorPage()).id))?.bounds;
  if (compact) {
    mark('reveal-sidebar', {}, true);
    await pointer('move', w.left + 2, Math.round(w.top + w.height / 2), 250);
    await sleep(900);
  }
  let vs = await viewTree();
  let button = clearButtonOf(vs);
  if (!button) {
    const row = vs.find(v => v.cls === 'KoanClearTabsRow');
    mark('no-clear-button', { compact: compactOn(vs), region: regionOf(vs) ? 'shown' : 'hidden',
                              clearRowVisible: row?.props.Visible });
    // Still clear (Clear's own shortcut, Cmd+Shift+K: the same
    // CloseUnpinnedTabs()), so the round has its counts and its video.
    mark('clear-key', {}, true);
    await key(await anchorPage(), 75, ACCEL | SHIFT, 'k');
    return 'key';
  }
  let r = await screenRect(button);
  if (!r) {
    mark('no-screen-rect', { button: [button.ax, button.ay, button.w, button.h] });
    return false;
  }
  mark('hover-clear', { at: [Math.round(r.x + r.w / 2), Math.round(r.y + r.h / 2)], via: r.via }, true);
  await pointer('move', Math.round(r.x + r.w / 2), Math.round(r.y + r.h / 2), 250);
  await sleep(500);
  // Revealed, it may have grown (the arrow): where it is now.
  vs = await viewTree();
  button = clearButtonOf(vs) ?? button;
  r = (await screenRect(button)) ?? r;
  const at = [Math.round(r.x + r.w / 2), Math.round(r.y + r.h / 2)];
  mark('clear-click', { at, via: r.via, revealed: button.props.Revealed }, true);
  await clickAt(...at);
  return true;
}

// --- Setup.
mark('start', { origin });
await browserCall('Target.createTarget', { url: anchorUrl('A'), background: false });
await waitFor(() => anchorPage('A'), 8000);
// The launch's about:blank goes.
for (const p of await pages()) {
  if (p.url === 'about:blank') await browserCall('Target.closeTarget', { targetId: p.id });
}
await sleep(1500);
const a0 = await anchorPage('A');
if (!a0) stop('no anchor page', 2);
if (!(await modelTabs(a0))) stop('test_helper does not answer (launch.sh loads it)', 2);
{
  const vs = await viewTree();
  const w = (await windowOf(a0.id))?.bounds;
  out.window = { x: w.left, y: w.top, w: w.width, h: w.height };
  // The root view's origin on screen, measured with the real pointer.
  mark('calibrate', {}, true);
  const origin0 = await calibrate(a0, vs);
  if (origin0) rootOrigin = { dx: origin0.x - w.left, dy: origin0.y - w.top };
  const region = regionOf(vs);
  if (region) {
    const r = await screenRect(region);
    if (r) out.sidebar = { x: Math.round(r.x), y: Math.round(r.y), w: region.w, h: region.h };
  }
  mark('setup', { window: out.window, rootOrigin, sidebar: out.sidebar });
}

// --- The rounds.
const ROUNDS = [
  { name: 'plain' },
  { name: 'some-loading', loading: 3 },
  { name: 'all-loading', loading: 6 },
  { name: 'pinned', pinned: true },
  { name: 'essential', essential: true },
  { name: 'compact', compact: true },
  { name: 'compact-loading', compact: true, loading: 3 },
  { name: 'two-spaces', otherSpace: 3 },
  { name: 'two-spaces-clear-in-second', otherSpace: 3, inSecond: true },
  { name: 'everything', loading: 3, pinned: true, essential: true, compact: true, otherSpace: 3 },
];
const only = (process.argv[2] ?? '').split(',').filter(Boolean).map(Number);

for (const [i, v] of ROUNDS.entries()) {
  if (only.length && !only.includes(i + 1)) continue;
  round = i + 1;
  const rec = { round, variant: v.name, ...v };
  mark('round-start', { variant: v.name }, true);
  await switchSpace('A');
  await reset();
  // The other Space's tabs (Clear must leave them), and where Clear runs.
  if (v.otherSpace) {
    if (!(await ensureSpaceB())) {
      rec.skipped = 'could not make a second Space';
      out.rounds.push(rec);
      continue;
    }
    if (!v.inSecond) {
      await switchSpace('B');
      await openTabs(v.otherSpace, 0, `B${round}`);
      await switchSpace('A');
    } else {
      await openTabs(v.otherSpace, 0, `A${round}`);
      await switchSpace('B');
    }
  }
  const tag = `${space}${round}`;
  await openTabs(6, v.loading ?? 0, tag);
  // A tab pinned in the Space (Cmd+Shift+D on it), an Essential (test_helper
  // pins it the Chrome way, which Koan makes an Essential).
  if (v.pinned) {
    const url = pageUrl('p', `${tag} pinned`, 7);
    await browserCall('Target.createTarget', { url, background: false });
    const p = await waitFor(() => findPage(x => x.url === url), 5000);
    await sleep(800);
    mark('pin-in-space', {}, true);
    if (p) await key(p, VK_D, ACCEL | SHIFT, 'd');
    await sleep(800);
  }
  if (v.essential) {
    const url = pageUrl('p', `${tag} essential`, 6);
    await browserCall('Target.createTarget', { url, background: false });
    const p = await waitFor(() => findPage(x => x.url === url), 5000);
    await sleep(800);
    mark('make-essential', {}, true);
    if (p) await askTestHelper(p, 'pin-tab', 1000);
  }
  await activate(await anchorPage());
  await sleep(1500);
  if (v.compact) {
    mark('compact-on', {}, true);
    await key(await anchorPage(), VK_S, ACCEL, 's');
    await sleep(1200);
    // Pointer off the sidebar, so it really slides away.
    const w = (await windowOf((await anchorPage()).id))?.bounds;
    await pointer('move', Math.round(w.left + w.width * 0.7), Math.round(w.top + w.height / 2), 200);
    await sleep(800);
  }
  rec.before = await snapshot('before');
  if (v.compact && !rec.before.compact) mark('compact-did-not-turn-on');

  rec.clicked = await clickClear(!!v.compact);
  if (rec.clicked) {
    rec.after = [await snapshot('after-click')];
    await sleep(800);
    rec.after.push(await snapshot('after-1s'));
    await sleep(2000);
    rec.after.push(await snapshot('after-3s'));
    const last = rec.after.at(-1);
    rec.leftBehind = last.model.closable;
    rec.rowsVsModel = (last.rows.normal ?? 0) + (last.rows.pinned ?? 0) - last.model.space;
    // The other Space's tabs are all still there.
    if (v.otherSpace) {
      rec.otherSpaceTabs = Object.entries(last.model.byGroup)
          .filter(([g]) => g !== 'essential' && g !== String(last.group))
          .reduce((n, [, c]) => n + c, 0);
    }
    mark('round-result', { leftBehind: rec.leftBehind, rowsVsModel: rec.rowsVsModel,
                           otherSpaceTabs: rec.otherSpaceTabs });
  }
  if (v.compact) {
    const w = (await windowOf((await anchorPage()).id))?.bounds;
    await pointer('move', Math.round(w.left + w.width * 0.7), Math.round(w.top + w.height / 2), 200);
    mark('compact-off', {}, true);
    await key(await anchorPage(), VK_S, ACCEL, 's');
    await sleep(1200);
  }
  out.rounds.push(rec);
  save();
}

// --- Summary: one line per round.
await switchSpace('A');
mark('end');
console.log('\nround  variant                        clicked  left-behind  rows-model  other-space');
for (const r of out.rounds) {
  console.log(`${String(r.round).padStart(5)}  ${r.variant.padEnd(30)} ${String(r.clicked ?? r.skipped).padEnd(8)} ` +
              `${String(r.leftBehind ?? '-').padStart(11)}  ${String(r.rowsVsModel ?? '-').padStart(10)}  ` +
              `${r.otherSpaceTabs ?? '-'}`);
}
console.log(`marks: ${MARKS}`);
for (const s of sockets) s.destroy();
server.close();
process.exit(0);
