// End-to-end test of Spaces across restarts, on one profile: Zen brings back
// the Spaces of the last session with the last active one shown, and makes a
// Space only for a profile that has none. Koan made one more Space at every
// start once restored windows joined window sync (#270).
// - two Spaces ("Space", "Work", Work shown), then a quit and a start with
//   "continue where you left off" (Koan's default): the same two, Work shown;
// - a second quit and start: still two (a Space per start shows here);
// - on macOS, closing the last window (the app stays), then a quit and a
//   start: still two;
// - a start with the New Tab page instead (session.restore_on_startup = 5):
//   the two come back from koan.workspaces.list, Work shown;
// - the saved window sync state (koan.window_sync.state) has the same two;
// - nothing fatal in the log, Koan alive throughout.
// Run by scripts/mac/suite.sh (or scripts/e2e/run.sh spaces_relaunch). The
// starts reuse KOAN_CHROME_ARGS (the profile, without the URLs).
import { spawn } from 'node:child_process';
import { openSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { CDP_PORT, browserCall, check, connect, finish, pages, readPref, sleep, viewTree,
         waitFor } from './lib/koan.mjs';
import { IS_MAC, closeWindowOf, createSpace, fatalInLog, spaceButtons } from './lib/mac.mjs';

const args = process.env.KOAN_CHROME_ARGS.split('\n').filter(Boolean);
const profile = args.find(a => a.startsWith('--user-data-dir='))?.split('=').slice(1).join('=');
let relaunched = null;
// Never leave a started Koan running (it would hold the test ports).
process.on('exit', () => relaunched?.kill());

const up = async () => !!await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).catch(() => null);
// lib/mac.mjs alive() checks the first Koan's pid, which the first quit ends.
const alive = async () => !!(await browserCall('Browser.getVersion').catch(() => null))?.result?.product;

async function quit() {
  const v = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
  const browser = await connect(v.webSocketDebuggerUrl);
  await browser.send('Browser.close');
  browser.close();
  for (let i = 0; i < 60 && await up(); i++) await sleep(500);
  await sleep(2000);
}

async function start() {
  relaunched?.kill();
  // Its log goes after the first Koan's, for fatalInLog().
  const log = process.env.KOAN_LOG ? openSync(process.env.KOAN_LOG, 'a') : 'ignore';
  relaunched = spawn(args[0], args.slice(1), { stdio: ['ignore', log, log] });
  for (let i = 0; i < 60 && !await up(); i++) await sleep(500);
  // The restore, the window sync join and their updates.
  await sleep(6000);
}

// The bottom bar's Spaces, once the window shows some.
async function spaces() {
  const buttons = await waitFor(async () => {
    const found = spaceButtons(await viewTree());
    return found.length ? found : null;
  }, 10000) ?? [];
  return { names: buttons.map(b => b.name).join(), active: buttons.find(b => b.active)?.name };
}

async function expectTwo(when) {
  const s = await spaces();
  check(`${when}: the two Spaces, and no other`, s.names === 'Space,Work', s.names);
  check(`${when}: the Space shown last is shown`, s.active === 'Work', s.active);
}

const firstPage = async () => (await pages()).find(p => p.type === 'page');

// ---- Two Spaces, Work shown.
check('A new profile has one Space', (await spaces()).names === 'Space', (await spaces()).names);
check('Create a Space', await createSpace(await firstPage(), 'Work'));
await expectTwo('Before the first quit');

// ---- Quit and start, twice ("continue where you left off").
await quit();
await start();
await expectTwo('After a quit and a start');
await quit();
await start();
await expectTwo('After a second quit and start');

// ---- macOS: the last window closes, the app stays; then a quit and a start.
if (IS_MAC) {
  check('Close the last window', await closeWindowOf(await firstPage(), 1500));
  check('...and Koan stays running', await alive());
  await quit();
  await start();
  await expectTwo('After closing the last window, a quit and a start');
}

// ---- A start on the New Tab page: the Spaces come from koan.workspaces.list.
await quit();
const prefsFile = join(profile, 'Default', 'Preferences');
const prefs = JSON.parse(readFileSync(prefsFile, 'utf8'));
prefs.session = { ...prefs.session, restore_on_startup: 5 };
writeFileSync(prefsFile, JSON.stringify(prefs));
await start();
await expectTwo('After a start on the New Tab page');

const saved = (await readPref('koan.window_sync.state'))?.spaces ?? [];
check('The saved window sync state has the same two Spaces',
      saved.map(s => s.name).join() === 'Space,Work', JSON.stringify(saved.map(s => s.name)));
check('Koan is alive', await alive());
check('Nothing fatal in the log', !fatalInLog(), fatalInLog());

await quit();
finish();
