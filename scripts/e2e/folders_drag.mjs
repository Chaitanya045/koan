// End-to-end test of dragging tabs into and out of Zen-style folders, with
// real mouse input: a tab dropped right under a folder's row joins the
// folder; a folder's tab dropped among the loose tabs leaves it.
//
//   KOAN_XVFB=/path/to/Xvfb scripts/e2e/run.sh folders_drag
//
// Headless Ozone can't run a tab drag (it can't tell which window is under
// the pointer, so the drag is cancelled on release). So this test closes
// run.sh's Koan and starts it again on X11, in an Xvfb of its own (KOAN_XVFB,
// else Xvfb on PATH; without one it skips), and drags with XTest
// (lib/xdrag.py). Nothing appears on the desktop.
import { spawn, spawnSync } from 'node:child_process';
import path from 'node:path';
import { CDP_PORT, check, finish, pages, sleep } from './lib/koan.mjs';
import {
  browserCdp, moveToFolder, pressKey, rows, shownTabs, withView,
} from './lib/folders.mjs';

const xvfb = process.env.KOAN_XVFB ||
  spawnSync('sh', ['-c', 'command -v Xvfb'], { encoding: 'utf8' }).stdout.trim();
if (!xvfb) {
  console.log('SKIP  no Xvfb (set KOAN_XVFB): headless Ozone cannot drag tabs');
  process.exit(0);
}

const names = rs => rs.map(r => `${r.name}:${r.count}`).join(' ');
const up = async () => !!await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)
    .catch(() => null);

// An X server of our own: Xvfb picks a free display and writes it to fd 3.
const server = spawn(xvfb, ['-displayfd', '3', '-screen', '0', '1600x1000x24', '-nolisten',
                            'tcp'], { stdio: ['ignore', 'ignore', 'ignore', 'pipe'] });
let koan = null;
process.on('exit', () => { koan?.kill(); server.kill(); });
const display = await new Promise((resolve, reject) => {
  let out = '';
  server.stdio[3].on('data', d => {
    out += d;
    if (out.includes('\n')) resolve(`:${out.trim()}`);
  });
  server.once('exit', () => reject(new Error('Xvfb exited')));
});

// Koan again, on X11 with the same profile, with four tabs.
await browserCdp('Browser.close', {}).catch(() => {});
for (let i = 0; i < 40 && await up(); i++) await sleep(500);
await sleep(2000);
const args = process.env.KOAN_CHROME_ARGS.split('\n').filter(Boolean)
    .filter(a => !a.startsWith('--ozone-override-screen-size'))
    .map(a => (a === '--ozone-platform=headless' ? '--ozone-platform=x11' : a));
const queries = ['one', 'two', 'three', 'four'];
const env = { ...process.env, DISPLAY: display };
delete env.WAYLAND_DISPLAY;
koan = spawn(args[0], [...args.slice(1), '--window-position=0,0',
                       ...queries.map(q => `https://example.com/?${q}`)],
             { env, stdio: 'ignore' });
for (let i = 0; i < 60 && !await up(); i++) await sleep(500);
await sleep(4000);
const urls = (await pages()).map(p => p.url);
check('Koan runs on X11 in Xvfb, with the four tabs',
      queries.every(q => urls.includes(`https://example.com/?${q}`)),
      `${display}: ${urls.join(' ')}`);

// Where root view coordinates are on the X screen: the window's origin.
const target = (await pages())[0];
const { bounds } = (await browserCdp('Browser.getWindowForTarget',
                                     { targetId: target.id })).result;

// Drags the tab `from` picks (from shownTabs) to the middle of the tab `to`
// picks, with the mouse.
async function drag(from, to) {
  const points = await withView(/^TabView$/, 0, async (s) => {
    const ts = await shownTabs(s);
    const a = from(ts), b = to(ts);
    if (!a || !b) return null;
    const mid = t => [bounds.left + t.ax + t.w / 2, bounds.top + t.ay + t.h / 2];
    return [...mid(a), ...mid(b)];
  });
  if (!points) return false;
  const r = spawnSync('python3', [path.join(import.meta.dirname, 'lib/xdrag.py'),
                                  ...points.map(String)], { env, encoding: 'utf8' });
  if (r.status !== 0) console.log(`  (xdrag.py: ${r.stderr.trim()})`);
  await sleep(1500);
  return r.status === 0;
}

// A folder with the first tab: [Folder: one], two, three, four.
await moveToFolder(ts => ts[0], 'kKoanTabNewFolderItem');
await pressKey(/^KoanFolderNameField$/, 27);  // Escape: keeps "New Folder".
await sleep(800);
let r = await rows();
check('A folder with one tab to start with', r.length === 1 && r[0].count === 1, names(r));

// In: the last tab onto the folder's tab, right under the row.
check('Dragging the last tab up to the folder',
      await drag(ts => ts.at(-1), ts => ts[0]));
r = await rows();
check('...puts it in the folder', r[0]?.count === 2, names(r));
const indented = await withView(/^TabView$/, 0, async (s) => {
  const ts = await shownTabs(s);
  const minX = Math.min(...ts.map(t => t.x));
  return ts.slice(0, 2).every(t => t.x > minX) && ts.slice(2).every(t => t.x === minX);
});
check('...indented, under the row with the other', indented === true);

// Out: the folder's first tab to the end, among the loose tabs.
check('Dragging a folder tab down among the loose tabs',
      await drag(ts => ts[0], ts => ts.at(-1)));
r = await rows();
check('...takes it out of the folder', r[0]?.count === 1, names(r));

// Among the loose tabs, a loose tab stays loose.
check('Dragging a loose tab among the loose tabs',
      await drag(ts => ts.at(-1), ts => ts.at(-2)));
r = await rows();
check('...leaves the folder alone', r[0]?.count === 1, names(r));

finish();
