// End-to-end test that a download Safe Browsing finds dangerous only once
// it is in never opens by itself when kept, whatever was clicked in the
// Library while it came in, and however long ago the Library closed (its
// sections are freed 30 s after it closes).
//
// Koan is restarted with a fake xdg-open (how Chromium opens files on
// Linux) that logs what it is asked to open. A local server sends a .exe
// slowly; library_downloads.flags makes its content DANGEROUS by hash, so
// the verdict comes when the file is complete. The test clicks the row
// while the file comes in (Zen's "open when done"), closes the Library,
// waits past its teardown, lets the file finish, keeps it on
// chrome://downloads, and checks nothing was opened. Then, as a control,
// it opens a safe download from the row menu, which must be logged.
//
//   scripts/e2e/run.sh library_downloads
import { spawn } from 'node:child_process';
import { chmodSync, existsSync, mkdirSync, openSync, readFileSync, writeFileSync } from 'node:fs';
import http from 'node:http';
import {
  CDP_PORT, CTRL, SHIFT, UI_PORT, browserCall, check, connect, evalIn, finish, key as keyTo,
  pages, sleep, visiblePage, waitFor, IS_MAC, META,
} from './lib/koan.mjs';
// Library Downloads: Ctrl+Shift+Y, on macOS Cmd+J (koan_shortcut_table.cc).
const DOWNLOADS_KEY = IS_MAC ? [74, META, 'j'] : [89, CTRL | SHIFT, 'Y'];

const KEY_Y = 89;
// The Library frees a closed panel after 30 s (koan_library_controller.cc).
const kCleanupDelay = 30e3;

let step = 'starting';
setTimeout(() => {
  check(`The test finishes (stuck at: ${step})`, false);
  finish();
}, 6 * 60e3);

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
async function click(view, button = 'left', x = 20, y = 20) {
  await mouse(view, 'mousePressed', x, y, button);
  await mouse(view, 'mouseReleased', x, y, button);
}

const shown = (vs, cls, pred = () => true) => vs.filter(v => v.cls === cls && v.shown && pred(v));
const row = async name => shown(await views(), 'KoanLibraryRow', v => v.p.Title === name)[0];

// Keys to the page showing, which passes the browser's shortcuts on.
async function key(code, modifiers, keyName = '') {
  await keyTo(await visiblePage(), code, modifiers, keyName);
  await sleep(900);
}

// Picks the item at `index` in the newest menu (by position: UI DevTools
// can't read menu labels).
async function pickMenu(index) {
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
  return null;
}

// --- Koan again on the same profile, with a fake xdg-open on the PATH. ---
const args = process.env.KOAN_CHROME_ARGS.split('\n').filter(Boolean);
const profile = args.find(a => a.startsWith('--user-data-dir=')).split('=')[1];
const bin = `${profile}/fake-bin`;
const openLog = `${profile}/opened.log`;
mkdirSync(bin, { recursive: true });
writeFileSync(`${bin}/xdg-open`, `#!/bin/sh\nprintf '%s\\n' "$*" >> '${openLog}'\n`);
chmodSync(`${bin}/xdg-open`, 0o755);
const opened = () => (existsSync(openLog) ? readFileSync(openLog, 'utf8') : '');

let relaunched = null;
process.on('exit', () => relaunched?.kill());
const up = () => fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).then(() => true, () => false);
{
  step = 'restarting Koan';
  const v = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
  const b = await connect(v.webSocketDebuggerUrl);
  await b.send('Browser.close');
  b.close();
  await waitFor(async () => !(await up()), 20000);
  await sleep(2000);
  const out = openSync(`${profile}/relaunch.log`, 'a');
  relaunched = spawn(args[0], [...args.slice(1), 'about:blank'], {
    stdio: ['ignore', out, out],
    env: { ...process.env, PATH: `${bin}:${process.env.PATH}` },
  });
  await waitFor(up, 30000);
  await waitFor(async () => (await pages()).length, 15000);
  await sleep(3000);
}

// --- A server that holds the end of a download until told to send it. ---
const LATER = 'MZ koan library later';  // Its hash is in library_downloads.flags.
let release;
const released = new Promise(r => { release = r; });
const server = http.createServer(async (req, res) => {
  if (req.url === '/later.exe') {
    res.writeHead(200, { 'Content-Type': 'application/octet-stream',
                         'Content-Disposition': 'attachment; filename="koan-later.exe"',
                         'Content-Length': String(LATER.length) });
    res.write(LATER.slice(0, 8));
    await released;
    res.end(LATER.slice(8));
  } else if (req.url === '/safe.txt') {
    res.writeHead(200, { 'Content-Type': 'application/octet-stream',
                         'Content-Disposition': 'attachment; filename="koan-safe.txt"' });
    res.end('Koan Library open test');
  } else {
    res.writeHead(404);
    res.end();
  }
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}`;
process.on('exit', () => server.close());

// --- The download comes in; a click on its row in the Library. ---
step = 'starting the slow download';
await browserCall('Target.createTarget', { url: `${base}/later.exe`, background: true });
await sleep(2000);
await key(...DOWNLOADS_KEY);
let later = await waitFor(() => row('koan-later.exe'), 8000);
check('The download coming in is listed in the Library', Boolean(later),
      later ? later.p.Subtitle : 'no row');
if (later) {
  await click(later);
  await sleep(800);
  later = await row('koan-later.exe');
  check('...and a click on it doesn\'t set it to open when done',
        !/Opens when/.test(later?.p.Subtitle ?? ''), later?.p.Subtitle);
}

// --- The Library closes, and is freed. ---
step = 'waiting for the closed Library to be freed';
await key(...DOWNLOADS_KEY);
await sleep(kCleanupDelay + 5000);
check('The closed Library is freed (its sections with it)',
      shown(await views(), 'KoanLibraryDownloadsSection').length === 0 &&
          !(await views()).some(v => v.cls === 'KoanLibraryView'));

// --- The file comes in, and Safe Browsing finds it dangerous. ---
step = 'finishing the download';
release();
const page = await (async () => {
  const targetId = (await browserCall('Target.createTarget',
      { url: 'chrome://downloads/' })).result.targetId;
  return waitFor(async () => (await pages()).find(p => p.id === targetId), 8000);
})();
const items = () => evalIn(page, `JSON.stringify(
    document.querySelector('downloads-manager').items_.map(
        i => ({ id: i.id, name: i.fileName, dangerous: i.isDangerous })))`)
    .then(json => JSON.parse(json ?? '[]'));
const laterItem = async () => (await items()).find(i => i.name === 'koan-later.exe');
let item = await waitFor(async () => {
  const i = await laterItem();
  return i?.dangerous ? i : null;
}, 20000);
check('Once it is in, Safe Browsing marks it dangerous', Boolean(item),
      JSON.stringify(await items()));

// --- Keep, on chrome://downloads (it needs a recent user interaction). ---
step = 'keeping the dangerous download';
if (item) {
  await waitFor(async () => {
    const c = await connect(page.webSocketDebuggerUrl);
    for (const type of ['mousePressed', 'mouseReleased']) {
      await c.send('Input.dispatchMouseEvent',
                   { type, x: 5, y: 300, button: 'left', clickCount: 1 });
    }
    c.close();
    await evalIn(page, `document.querySelector('downloads-manager').browserProxy_.handler
        .saveDangerousFromDialogRequiringGesture(${JSON.stringify(item.id)})`, true);
    await sleep(1000);
    return !(await laterItem())?.dangerous;
  }, 10000);
  item = await laterItem();
  check('Keep keeps it', item && !item.dangerous, JSON.stringify(item));
  await sleep(3000);
  check('...and it does not open by itself', !opened().includes('koan-later.exe'),
        opened().trim() || 'nothing opened');
}
await browserCall('Target.closeTarget', { targetId: page.id });
await sleep(1000);

// --- Control: opening a download from the row menu is seen. ---
step = 'opening a safe download from the row menu';
await browserCall('Target.createTarget', { url: `${base}/safe.txt`, background: true });
await sleep(2000);
await key(...DOWNLOADS_KEY);
const safe = await waitFor(async () => {
  const r = await row('koan-safe.txt');
  return r && !/\//.test(r.p.Subtitle ?? '/') ? r : null;
}, 8000) ?? await row('koan-safe.txt');
if (safe) {
  await click(safe, 'right');
  await sleep(800);
  // The menu of a finished download: "Open koan-safe.txt" first.
  const error = await pickMenu(0);
  check('A finished download\'s menu opens', !error, error ?? '');
}
const control = await waitFor(() => opened().includes('koan-safe.txt'), 8000);
check('...and Open there opens it (the check above would see an open)', Boolean(control),
      opened().trim() || 'nothing opened');

finish();
