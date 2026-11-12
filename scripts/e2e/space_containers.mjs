// End-to-end test of Zen's containers per Space (#318): the Arc-alternatives
// review's check that a Space keeps its own logins. The profile starts with
// two profiles (Alpha, Beta) and the Spaces Start, A (Alpha), B (Beta),
// A two (Alpha) and None (no profile), from prefs/space_containers.json
// (koan.containers.user, koan.workspaces.list: a start without a session
// restore recreates them). A local page logs in with a cookie and a
// localStorage theme; /whoami says which cookie the server got.
// - signed in and themed in A, the same URL in B is signed out with no theme,
//   and the server sees no cookie there; A two (A's profile) is signed in;
//   None and Start are signed out;
// - B signs in as someone else, A logs out: B stays signed in, A two is out;
// - the Create-a-Space form has Zen's Profile row, on Default;
// - after a quit and a start, the Spaces keep their profiles: B is still
//   signed in, None still isn't;
// - nothing fatal in the log, Koan alive throughout.
// Run by scripts/mac/suite.sh (or scripts/e2e/run.sh space_containers). The
// start reuses KOAN_CHROME_ARGS (the profile, without the URLs). New tabs open
// through CDP (Target.createTarget), which opens them like Cmd+T: in the
// active Space, with no page behind them.
import { spawn } from 'node:child_process';
import { openSync } from 'node:fs';
import { CDP_PORT, CTRL, ALT, browserCall, check, connect, evalIn, finish, pages, sleep,
         typeInto, viewMouse, viewTree, waitFor } from './lib/koan.mjs';
import { fatalInLog, key, spaceButtons, startServer } from './lib/mac.mjs';

const server = await startServer({
  '/login': `<!doctype html><title>Login</title><script>
    window.login = name => { document.cookie = 'login=' + name + '; max-age=86400; path=/'; };
    window.logout = () => { document.cookie = 'login=; max-age=0; path=/'; };
    window.state = async () => JSON.stringify({
      cookie: document.cookie, theme: localStorage.getItem('theme'),
      server: await (await fetch('/whoami', { cache: 'no-store' })).text() });
  </script>`,
  '/whoami': req => req.headers.cookie ?? '',
});
const PAGE = space => server.url(`/login?space=${encodeURIComponent(space)}`);

const args = process.env.KOAN_CHROME_ARGS.split('\n').filter(Boolean);
let relaunched = null;
process.on('exit', () => relaunched?.kill());
const up = async () => !!await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).catch(() => null);
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
  const log = process.env.KOAN_LOG ? openSync(process.env.KOAN_LOG, 'a') : 'ignore';
  relaunched = spawn(args[0], args.slice(1), { stdio: ['ignore', log, log] });
  for (let i = 0; i < 60 && !await up(); i++) await sleep(500);
  await sleep(6000);
}

// The bottom bar's Space buttons, once the window shows them.
const buttons = async () => await waitFor(async () => {
  const found = spaceButtons(await viewTree());
  return found.length > 1 ? found : null;
}, 10000) ?? [];

// Shows the Space called `name` (its bottom bar button).
async function show(name) {
  const button = (await buttons()).find(b => b.name === name);
  if (!button) return false;
  if (!button.active) {
    await viewMouse(button.view, 'mousePressed', 6, 6);
    await viewMouse(button.view, 'mouseReleased', 6, 6);
  }
  return !!await waitFor(async () => (await buttons()).find(b => b.name === name)?.active, 5000);
}

// Opens the login page in a new tab of the Space called `name`; its page.
async function openIn(name) {
  if (!await show(name)) return null;
  const url = PAGE(name);
  const targetId = (await browserCall('Target.createTarget', { url })).result?.targetId;
  const page = await waitFor(async () => (await pages()).find(p => p.id === targetId), 8000);
  if (page) await waitFor(() => evalIn(page, 'typeof window.state === "function"'), 8000);
  return page;
}

const state = async page => JSON.parse(await evalIn(page, 'state()') ?? '{}');
const signedOut = s => !s.cookie && !s.theme && !s.server;

// ---- The Spaces and their profiles came from the stored list.
const names = (await buttons()).map(b => b.name).join();
check('The stored Spaces are recreated', names === 'Start,A,B,A two,None', names);

// ---- A logs in; B, None and Start don't see it, A two does.
const a = await openIn('A');
check('A page opens in Space A', !!a);
await evalIn(a, "login('alice'); localStorage.setItem('theme', 'dark'); true");
let s = await state(a);
check('A is signed in, with its theme',
      s.cookie === 'login=alice' && s.theme === 'dark' && s.server === 'login=alice', JSON.stringify(s));

const b = await openIn('B');
s = await state(b);
check('The same URL in B (its own profile) is signed out, with no theme', signedOut(s), JSON.stringify(s));

const a2 = await openIn('A two');
s = await state(a2);
check("A two (A's profile) is signed in, with A's theme",
      s.cookie === 'login=alice' && s.theme === 'dark' && s.server === 'login=alice', JSON.stringify(s));

const none = await openIn('None');
s = await state(none);
check('None (no profile) is signed out', signedOut(s), JSON.stringify(s));
const startPage = await openIn('Start');
s = await state(startPage);
check('Start (no profile) is signed out', signedOut(s), JSON.stringify(s));

// ---- B signs in as bob, A logs out: B stays in, A two is out.
await evalIn(b, "login('bob'); localStorage.setItem('theme', 'light'); true");
await evalIn(a, 'logout(); true');
s = await state(b);
check('Logging out in A leaves B signed in',
      s.cookie === 'login=bob' && s.theme === 'light' && s.server === 'login=bob', JSON.stringify(s));
s = await state(a2);
check("...and A two, A's profile, is signed out too", !s.cookie && !s.server, JSON.stringify(s));
await evalIn(a, "login('alice'); true");

// ---- Zen's Profile row in the Create-a-Space form (Ctrl+Alt+N), on Default.
await key(startPage, 'n', CTRL | ALT, 1200);
const profileButton = await waitFor(async () => (await viewTree()).find(
    v => v.cls === 'KoanSpaceProfileButton' && v.shown), 5000);
check("The Create-a-Space form has Zen's Profile row, on Default",
      profileButton?.props.Text === 'Default', profileButton?.props.Text ?? 'no row');
// Escape in the name field cancels.
const nameField = (await viewTree()).find(
    v => v.cls === 'Textfield' && v.shown && v.path.includes('KoanSpaceCreationView'));
if (nameField) await typeInto(nameField, '', 27);

// ---- A quit and a start ("continue where you left off"): the Spaces keep
// their profiles, and the logins are on disk.
await quit();
await start();
check('After a start, the same Spaces', (await buttons()).map(x => x.name).join() === names,
      (await buttons()).map(x => x.name).join());
const b2 = await openIn('B');
s = b2 ? await state(b2) : {};
check('After a start, B is still signed in', s.cookie === 'login=bob' && s.server === 'login=bob',
      JSON.stringify(s));
const a3 = await openIn('A');
s = a3 ? await state(a3) : {};
check('After a start, A is signed in as before', s.cookie === 'login=alice', JSON.stringify(s));
const none2 = await openIn('None');
s = none2 ? await state(none2) : {};
check('After a start, None is still signed out', signedOut(s), JSON.stringify(s));

check('Koan is alive', await alive());
check('Nothing fatal in the log', !fatalInLog(), fatalInLog());
await quit();
server.close();
finish();
