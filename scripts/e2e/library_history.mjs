// End-to-end test of what the Library's History deletes, read back from the
// history itself: visits of different ages are written into the profile's
// History database, then Forget This Page (checked with a fresh query),
// Clear history → Last hour (the older visit stays), the
// AllowDeletingBrowserHistory policy (nothing offers to delete), and
// private windows (no Library; Ctrl+H does what Chrome does there).
//
//   scripts/e2e/run.sh library_history
import { spawn } from 'node:child_process';
import { openSync, readFileSync, writeFileSync } from 'node:fs';
import { DatabaseSync } from 'node:sqlite';
import {
  ALT, CDP_PORT, CTRL, SHIFT, UI_PORT, browserCall, check, connect, finish, key as keyTo,
  pages, sleep, visiblePage, waitFor, ACCEL, IS_MAC, META,
} from './lib/koan.mjs';
// Library History: Ctrl+H, on macOS Cmd+Y (koan_shortcut_table.cc).
const HISTORY_KEY = IS_MAC ? [89, META, 'y'] : [72, CTRL, 'h'];

const KEY_L = 76, KEY_H = 72, KEY_P = 80, ESC = 27;

// What the test is doing, for the watchdog: a step that never returns (a
// DevTools call left unanswered) fails the test instead of holding the
// shared build lock.
let step = 'starting';
setTimeout(() => {
  check(`The test finishes (stuck at: ${step})`, false);
  finish();
}, 6 * 60e3);
// `promise`, or null after `ms`.
const within = (promise, ms) =>
  Promise.race([promise, new Promise(r => setTimeout(() => r(null), ms))]);

// Every native view as {cls, p (its properties), shown, x, y, w, h, nodeId,
// path}; x/y are in the view's widget.
async function views() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const out = [];
  const walk = async (n, shown, path, ox, oy) => {
    const a = n.attributes ?? [];
    const cls = a[a.indexOf('class') + 1];
    if (n.nodeName === 'View' && cls) {
      const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
      const p = {};
      for (const rule of r.result?.matchedCSSRules ?? []) {
        for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
      }
      shown = shown && p.Visible !== 'false';
      const v = { cls, p, shown, x: ox + +p.X, y: oy + +p.Y, w: +p.Width, h: +p.Height,
                  nodeId: n.nodeId, path };
      out.push(v);
      [path, ox, oy] = [[...path, cls], v.x, v.y];
    } else if (n.nodeName === 'Widget') {
      [path, ox, oy] = [[], 0, 0];
    }
    for (const c of n.children ?? []) await walk(c, shown, path, ox, oy);
  };
  await walk(doc, true, [], 0, 0);
  ui.close();
  return out;
}

async function mouse(view, type, x, y, button = 'left') {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('DOM.getDocument', { depth: -1 });
  await ui.send('DOM.dispatchMouseEvent', { nodeId: view.nodeId,
      event: { type, x, y, button, wheelDirection: 'none' } });
  ui.close();
}
async function click(view, button = 'left') {
  const x = Math.floor(view.w / 2), y = Math.floor(view.h / 2);
  await mouse(view, 'mousePressed', x, y, button);
  await mouse(view, 'mouseReleased', x, y, button);
}

// Keys straight to a native view (the search field).
async function typeInto(view, text) {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('DOM.getDocument', { depth: -1 });
  for (const ch of text) {
    await ui.send('DOM.dispatchKeyEvent', { nodeId: view.nodeId,
        event: { type: 'keyPressed', keyCode: 0, code: 0, flags: 0,
                 key: ch.charCodeAt(0), isChar: true } });
  }
  ui.close();
}

// Keys to the page showing, which passes the browser's shortcuts on.
async function key(code, modifiers, keyName = '', page = null) {
  await keyTo(page ?? await visiblePage(), code, modifiers, keyName);
  await sleep(900);
}

const shown = (vs, cls, pred = () => true) => vs.filter(v => v.cls === cls && v.shown && pred(v));

// The Library's state, and the titles of the History rows it lists.
async function library() {
  const vs = await views();
  const view = vs.find(v => v.cls === 'KoanLibraryView' && v.p.Open === 'true');
  return {
    open: Boolean(view),
    titles: view ? shown(vs, 'KoanLibraryRow', v => v.path.includes('KoanLibraryHistorySection'))
        .map(v => v.p.Title).filter(Boolean) : [],
  };
}
const seeded = s => s.titles.filter(t => t?.startsWith('Seeded')).sort();

// Picks the items at `indices` in turn, each in the newest (sub)menu. UI
// DevTools can't read their labels (MenuItemView paints them), so by
// position. Returns what went wrong, or null.
async function pickMenu(...indices) {
  for (const index of indices) {
    const vs = await views();
    const root = vs.findLast(v => v.cls.endsWith('RootView') && v.cls !== 'BrowserRootView' &&
                                  vs.some(i => i.cls === 'MenuItemView' && i.path[0] === v.cls));
    const items = vs.slice(vs.indexOf(root))
        .filter(v => v.cls === 'MenuItemView' && v.path[0] === root?.cls);
    const item = items[index];
    if (!item) return `no item ${index} of ${items.length}`;
    const at = [item.x - root.x + 10, item.y - root.y + item.h / 2];
    await mouse(root, 'mouseMoved', ...at);
    await sleep(600);
    await mouse(root, 'mousePressed', ...at);
    await mouse(root, 'mouseReleased', ...at);
    await sleep(800);
  }
  return null;
}

// --- Restarting Koan on the same profile, with the database or prefs
// edited while it's down. ---
const args = process.env.KOAN_CHROME_ARGS.split('\n').filter(Boolean);
const profile = args.find(a => a.startsWith('--user-data-dir=')).split('=')[1];
let relaunched = null;
// The restarted browser's output, shown if the test fails.
const relaunchLog = `${profile}/relaunch.log`;
process.on('exit', code => {
  relaunched?.kill();
  if (code) {
    try {
      console.log(`--- last lines of the restarted Koan's log\n${
        readFileSync(relaunchLog, 'utf8').split('\n').slice(-40).join('\n')}`);
    } catch {}
  }
});
const up = () => fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).then(() => true, () => false);
async function restart(edit) {
  const v = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
  const b = await connect(v.webSocketDebuggerUrl);
  await b.send('Browser.close');
  b.close();
  await waitFor(async () => !(await up()), 20000);
  await sleep(2000);
  edit();
  relaunched?.kill();
  const out = openSync(relaunchLog, 'a');
  relaunched = spawn(args[0], [...args.slice(1), 'about:blank'],
                     { stdio: ['ignore', out, out] });
  await waitFor(up, 30000);
  await waitFor(async () => (await pages()).length, 15000);
  await sleep(3000);
}

// Chromium's times: microseconds since 1601.
const chromeTime = ms => (BigInt(ms) + 11644473600000n) * 1000n;
const now = Date.now();
const SEEDS = [
  ['https://recent.koan.test/', 'Seeded recent', now - 10 * 60e3],
  ['https://hours.koan.test/', 'Seeded hours', now - 3 * 3600e3],
  ['https://days.koan.test/', 'Seeded days', now - 3 * 86400e3],
];
await restart(() => {
  const db = new DatabaseSync(`${profile}/Default/History`);
  for (const [url, title, ms] of SEEDS) {
    const id = db.prepare(`INSERT INTO urls (url, title, visit_count, typed_count,
        last_visit_time, hidden) VALUES (?, ?, 1, 1, ?, 0)`)
        .run(url, title, chromeTime(ms)).lastInsertRowid;
    // A typed visit, the start and end of its chain.
    db.prepare(`INSERT INTO visits (url, visit_time, from_visit, transition, segment_id)
        VALUES (?, ?, 0, ?, 0)`).run(id, chromeTime(ms), 0x30000001);
  }
  db.close();
});

// --- The seeded visits are listed. ---
await key(KEY_L, ALT | SHIFT, 'L');
let state = await waitFor(async () => {
  const s = await library();
  return seeded(s).length === 3 ? s : null;
}) ?? await library();
check('History lists the visits written into the database',
      seeded(state).length === 3, state.titles.join(', '));

// --- Forget This Page (the row's trash), checked by a fresh query. ---
{
  const row = shown(await views(), 'KoanLibraryRow', v => v.p.Title === 'Seeded days')[0];
  await mouse(row, 'mouseEntered', 20, 20);
  await sleep(300);
  const trash = shown(await views(), 'ImageButton',
                      v => v.path.includes('KoanLibraryRow') &&
                           (v.p.TooltipText ?? v.p.Tooltip) === 'Remove from history')[0];
  await click(trash);
  await sleep(1000);
  // A search asks the history service again.
  const field = shown(await views(), 'Textfield',
                      v => v.path.includes('KoanLibraryHistorySection'))[0];
  await typeInto(field, 'Seeded');
  state = await waitFor(async () => {
    const s = await library();
    return seeded(s).length === 2 ? s : null;
  }) ?? await library();
  check('Forget This Page deletes it from history (a new search doesn\'t find it)',
        seeded(state).join() === 'Seeded hours,Seeded recent', state.titles.join(', '));
}
await key(KEY_L, ALT | SHIFT, 'L');  // Closes it.

// --- Clear history → Last hour, after a confirmation naming the range. ---
await key(KEY_L, ALT | SHIFT, 'L');
await waitFor(async () => (await library()).open);
{
  const tab = shown(await views(), 'KoanLibraryTab', v => v.p.Label === 'History')[0];
  await click(tab, 'right');
  await sleep(800);
  // Clear history ▸ Last hour.
  const error = await pickMenu(0, 0);
  check('The History tab\'s menu offers Clear history ▸ Last hour', !error, error ?? '');
  const dialog = await waitFor(async () => {
    const vs = await views();
    return shown(vs, 'Label', v => v.p.Text === 'Clear history from the last hour?').length
        ? vs : null;
  });
  check('...and asks first, naming the range', Boolean(dialog));
  // Views ignores a dialog's buttons for a moment after it shows (input
  // protection against clicks meant for what was there before), so wait,
  // and click again while the dialog is still up.
  step = 'accepting the Clear history dialog';
  await sleep(1000);
  for (let i = 0; i < 3; i++) {
    const vs = await views();
    const accept = shown(vs, 'MdTextButton', v => v.p.Text === 'Clear history')[0];
    if (!accept) break;
    await click(accept);
    await sleep(1500);
  }
}
// Closed and opened again, it asks the history service again.
await key(KEY_L, ALT | SHIFT, 'L');
await key(KEY_L, ALT | SHIFT, 'L');
state = await waitFor(async () => {
  const s = await library();
  return s.open && !s.titles.includes('Seeded recent') ? s : null;
}, 8000) ?? await library();
check('Clearing the last hour deletes the visit from 10 minutes ago',
      state.open && !state.titles.includes('Seeded recent'), state.titles.join(', '));
check('...and keeps the one from 3 hours ago', state.titles.includes('Seeded hours'),
      state.titles.join(', '));
await key(KEY_L, ALT | SHIFT, 'L');

// --- A private window (Ctrl+Shift+P) has no Library; Ctrl+H does what
// Chrome does there: its notice that history isn't saved (chrome_pages.cc
// ShowHistory()), not the History page. ---
{
  const contextOf = async () => (await browserCall('Target.getTargets')).result.targetInfos
      .filter(t => t.type === 'page');
  const before = await contextOf();
  const normal = before[0].browserContextId;
  await key(KEY_P, ACCEL | SHIFT, 'P');
  const privatePage = await waitFor(async () =>
    (await contextOf()).find(t => t.browserContextId !== normal), 8000);
  const page = privatePage &&
      (await pages()).find(p => p.id === privatePage.targetId);
  check('Ctrl+Shift+P opens a private window', Boolean(page));
  if (page) {
    await key(KEY_L, ALT | SHIFT, 'L', page);
    check('A private window has no Library (Alt+Shift+L)', !(await library()).open);
    step = 'Ctrl+H in the private window';
    await within(key(...HISTORY_KEY, page), 10000);
    step = 'looking for the incognito history notice';
    const notice = await within(waitFor(async () =>
      shown(await views(), 'IncognitoClearBrowsingDataDialog').length > 0), 20000);
    check('...and Ctrl+H there shows Chrome\'s incognito history notice',
          Boolean(notice) && !(await within(library(), 10000))?.open);
    step = 'closing the private window';
    await within(key(ESC, 0, 'Escape', page), 10000);
    await within(browserCall('Target.closeTarget', { targetId: page.id }), 10000);
    await sleep(1000);
  }
}

step = 'restarting with deleting history not allowed';
// --- With deleting history not allowed (AllowDeletingBrowserHistory),
// nothing in History offers to. ---
await restart(() => {
  const file = `${profile}/Default/Preferences`;
  const prefs = JSON.parse(readFileSync(file, 'utf8'));
  prefs.history = { ...prefs.history, deleting_enabled: false };
  writeFileSync(file, JSON.stringify(prefs));
});
await key(KEY_L, ALT | SHIFT, 'L');
await waitFor(async () => (await library()).titles.includes('Seeded hours'));
{
  const row = shown(await views(), 'KoanLibraryRow', v => v.p.Title === 'Seeded hours')[0];
  await mouse(row, 'mouseEntered', 20, 20);
  await sleep(300);
  const actions = shown(await views(), 'ImageButton', v => v.path.includes('KoanLibraryRow'))
      .map(v => v.p.TooltipText ?? v.p.Tooltip);
  check('Without the right to delete history, a row has no Remove button',
        actions.includes('Reopen page') && !actions.includes('Remove from history'),
        actions.join(', '));
  await click(row, 'right');
  await sleep(800);
  const items = shown(await views(), 'MenuItemView').length;
  check('...and its menu only Reopen Page', items === 1, `${items} item(s)`);
  await key(ESC, 0, 'Escape');
}

finish();
