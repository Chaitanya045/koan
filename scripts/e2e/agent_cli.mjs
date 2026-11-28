// End-to-end test of agent control (#180) driven by the `koan` CLI itself
// (Koan.app/Contents/Resources/bin/koan), the way an agent uses it, against
// the suite's test profile (KOAN_USER_DATA_DIR) and a throw-away HOME (the
// CLI keeps its keys in $HOME/.koan; the user's are never read):
// - pair: a plain `koan pair` (as the guide says) asks Koan for every
//   capability, drive among them; this test presses Allow in Koan's approval
//   dialog through UI DevTools, as the user would with the pointer (there is
//   no auto-approve switch, and none is added);
// - stale pairings (2026-11-19): a pairing Koan does not know is removed by
//   `koan status` (which says "paired: no (Koan forgot ...)" when it was the
//   only one) and by a command, which then uses the good one, never "several
//   paired clients ... --client";
// - Settings > Agent Control > Set Up Agents writes the koan-browser skill
//   for the installed agents and links ~/.local/bin/koan, in the test's HOME
//   (KOAN_AGENT_SETUP_HOME), and Remove takes them out again;
// - open, snapshot (an outline with @refs), act (click lands), screenshot
//   (a PNG), on a local page, in the agent's own background tab; each
//   screenshot size's pixels on the Mac's display (normal is CSS pixels),
//   --clip @ref / a locator as the element's box, scaled, and act
//   --screenshot micro (also in koan shell) in the same answer; with Koan's
//   window covered by another app's (real input only), the agent tab's
//   capture still has the page in it (Koan.captureScaled under the lease);
// - settle and return: act waits for the page (a slow fetch after a click
//   shows in act --snapshot with no sleep); navigate returns with the
//   server's status (504) at once, and a server that never answers is a
//   Koan.timeout after --timeout; a Material-style dropdown of divs whose
//   listbox is a portal: its open list comes first in the snapshot, its
//   option takes on the first click, and act choose works; koan shell runs
//   several commands on one connection; per-command times, old against new
//   (INFO lines);
// - batches (koan act with several steps): a 3-page form done in at most 3
//   koan calls (locators across the pages, --until on the last URL), with
//   the old one-verb-per-call loop's calls and time against it (INFO); a
//   batch stopped by the user taking the tab (the pill's Take over, as the
//   user would press it) says where it stopped and types nothing after;
//   --diff after a change prints only the change, and "baseline reset" on a
//   new page; a ref of a re-rendered element is refused, its locator works,
//   and a locator that names two elements lists them;
// - a form (#340): act select sets a dropdown (and fill on it does too), the
//   form submits the value; fill on a checkbox fails instead of "ok";
//   checkbox and radio states show in the snapshot; a ref from before a
//   navigate (same site, and cross-site where the new renderer numbers its
//   nodes from 1 again) is refused (Koan.staleRef); the compact snapshot of
//   a long page is much shorter than --full, with the same refs, which work;
// - "What agents can use" at its defaults (#272: every switch on, the
//   take-over site limit off): the content-marker extension
//   (agent_cli.flags) runs in the agent's tab as in a user tab, and its
//   site permissions are the user's (prompt, not denied);
// - an agent tab showing a certificate warning: acting on it is refused
//   (Koan.interstitial);
// - take-over of the user's tab (approved in its dialog the same way): each
//   screenshot size gives the agent tab's pixels there too (one capture path,
//   Koan.captureScaled), --clip with a locator is the button's box, and the
//   page (pages/agent_capture_watch.html) saw no resize, no devicePixelRatio
//   change and no media query change; a same-site navigate works, and so
//   does an off-site one (no limit); then release;
// - end: the tasks close;
// - every switch off and the site limit on (Settings > Agent Control), then
//   a new task: its tab is in a container, where the extension doesn't run
//   and geolocation and notifications are denied, unprompted; a take-over's
//   off-site navigate is refused (Koan.offSite);
// - a second pairing whose launcher exits while the dialog is open (the
//   2026-11-11 end-user pass): its agent's commands work, and a koan no agent
//   started is refused with a reason;
// - a koan started by another program than the paired agent, with no
//   connection open: Koan asks the user again, and koan says why while it
//   waits (#340);
// - Koan alive, nothing fatal in its log.
// local_state/agent_cli.json turns Agent ready on for the test profile (it
// is off in a non-default --user-data-dir). macOS only: the CLI's holder
// needs the Mac's peer resolver. Run by scripts/mac/suite.sh.
import { execFileSync, spawn } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import https from 'node:https';
import os from 'node:os';
import path from 'node:path';
import { centerOf, browserCall, check, decodePng, evalIn, finish, pages, setKoanPref, showPane, sleep,
         viewTree, waitFor } from './lib/koan.mjs';
import { IS_MAC, KOAN_LOG, alive, clickThroughRoot, fatalInLog, goToAgentTab, realInput, realInputWhyNot,
         rootMouse, rootOf, skip, skipped, startServer, windowOf } from './lib/mac.mjs';

if (!IS_MAC) skip('agent control\'s CLI needs macOS (its holder resolves peers with LOCAL_PEERTOKEN)');
const CLI = process.env.KOAN_CLI;
const PROFILE = process.env.KOAN_PROFILE;
const AGENT_HOME = process.env.KOAN_AGENT_HOME;
if (!CLI || !fs.existsSync(CLI)) skip(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);
if (!PROFILE || !AGENT_HOME) skip('KOAN_PROFILE and KOAN_AGENT_HOME are set by scripts/mac/suite.sh');
const env = { ...process.env, HOME: AGENT_HOME, KOAN_USER_DATA_DIR: PROFILE };

// Runs `koan args...`; resolves {code, out, err}. `whileRunning` runs once
// the command started (e.g. to answer a dialog it waits for), with a
// function that returns what the command printed so far. koan() resolves
// once both are done: Allow lets the command finish, and its exit could
// come in before allow() returns (the 2026-11-19 run: paired, yet the check
// on allow()'s answer failed).
function koan(args, { timeout = 60000, whileRunning, home } = {}) {
  return new Promise(resolve => {
    const child = spawn(CLI, args, { env: home ? { ...env, HOME: home } : env,
                                     stdio: ['ignore', 'pipe', 'pipe'] });
    let out = '', err = '';
    child.stdout.on('data', d => (out += d));
    child.stderr.on('data', d => (err += d));
    const timer = setTimeout(() => child.kill('SIGTERM'), timeout);
    const running = whileRunning?.(() => out + err)
        .catch(e => console.log(`  (${e.message})`));
    child.on('close', async code => {
      clearTimeout(timer);
      await running;
      resolve({ code, out: out.trim(), err: err.trim() });
    });
  });
}
const brief = r => `exit ${r.code}; ${(r.err || r.out).split('\n')[0].slice(0, 160)}`;

// Presses the Allow button of the dialog whose title matches `title` (a
// RegExp; UI DevTools, through the dialog widget's root view, like a click),
// once the command (`printed`, from koan()'s whileRunning) says it waits
// for the user in Koan. Not before: on macOS a widget made while a UI
// DevTools session is between its DOM.enable and DOM.getDocument crashes
// the browser in UI DevTools (DOMAgentMac::OnNativeWidgetAdded(), whose
// element_root() is still null), and each poll opens a session.
// `allowWhy` says why the last call returned false (a failure's detail).
let allowWhy = '';
async function allow(title, printed) {
  allowWhy = 'koan never said it waits in Koan';
  if (!await waitFor(() => / in Koan\.\.\./.test(printed()), 30000)) return false;
  allowWhy = 'no dialog with that title and an Allow button within 20s';
  // The dialog is shown in a task after the request arrives.
  await sleep(1000);
  const found = await waitFor(async () => {
    const vs = await viewTree();
    const label = vs.find(v => v.shown && title.test(v.props.Text ?? ''));
    if (!label) return null;
    const button = vs.find(v => v.widget === label.widget && v.shown && v.cls === 'MdTextButton' &&
                                v.props.Text === 'Allow');
    return button && { vs, button };
  }, 20000);
  if (!found) return false;
  allowWhy = '';
  // Dialogs ignore a click right after they show (InputEventActivationProtector).
  await sleep(1500);
  const vs = await viewTree();
  const button = vs.find(v => v.nodeId === found.button.nodeId) ?? found.button;
  const root = rootOf({ views: vs.filter(v => v.widget === button.widget) });
  const c = centerOf(button);
  await rootMouse(root, ['mousePressed', 'mouseReleased'], c.x, c.y);
  return true;
}

// Pages: /agent says what it may use and has a button to click; /form is a
// sign-up form whose submissions /done records; /long is a long text page.
const submitted = [];
const server = await startServer({
  '/form': `<!doctype html><title>Sign up</title><h1>Sign up</h1>
    <form action=/done method=get>
      <label>Plan <select name=plan><option value=free>Free</option>
        <option value=pro>Pro</option><option value=team>Team</option></select></label>
      <label><input type=radio name=contact value=email checked> Email</label>
      <label><input type=radio name=contact value=sms> SMS</label>
      <label><input type=checkbox name=news> News</label>
      <label>Name <input name=who></label>
      <button>Send</button>
    </form>`,
  '/done': req => {
    submitted.push(new URL(req.url, 'http://x').search);
    return '<!doctype html><title>Done</title><h1>Thanks</h1><button>Again</button>';
  },
  '/long': `<!doctype html><title>Long</title><h1 id=h>Long page</h1>${
    Array.from({ length: 40 }, (_, i) =>
      `<p>Paragraph ${i} has <b>bold</b>, <i>italic</i> and <span>split</span> text<br>over two lines.</p>`).join('')}
    <button onclick="document.getElementById('h').textContent='Pressed'">Press here</button>`,
  '/watch': fs.readFileSync(new URL('./pages/agent_capture_watch.html', import.meta.url), 'utf8'),
  '/agent': `<!doctype html><title>Agent page</title><h1 id=h>Ready</h1>
    <button onclick="document.getElementById('h').textContent='Clicked'">Press me</button>
    <p id=perm>checking</p>
    <script>
      Promise.all(['geolocation', 'notifications'].map(name =>
          navigator.permissions.query({ name }).then(s => name + '=' + s.state, e => name + '=error')))
        .then(r => { document.getElementById('perm').textContent = r.join(' '); });
    </script>`,
});
const AGENT_URL = server.url('/agent');
// What the agent tab's screenshots measured: the take-over's compare to it.
let agentShots = null;

// --- Agent ready, then pairing.
await waitFor(() => fs.existsSync(path.join(PROFILE, 'AgentControl', 'agent.json')), 15000);
let r = await koan(['status']);
check('koan status: Koan running, Agent ready', /running, Agent ready/.test(r.out), brief(r));
let allowed = false;
r = await koan(['pair', '--label', 'koan suite'],
               { timeout: 90000, whileRunning: async printed => {
                 allowed = await allow(/^Allow a program to control Koan\?$/, printed);
               } });
check('koan pair: the approval dialog showed and was allowed', allowed, allowWhy);
check('koan pair: paired', r.code === 0 && /paired as/.test(r.out), brief(r));
// The dialog adds a switch only for what was asked, on: all of it came back.
check('koan pair (no --capabilities): asked for, and got, everything, drive among it',
      /allowed: space, observe, read, drive, keep-awake$/.test(r.out), brief(r));
if (r.code !== 0) {
  skipped('the rest of agent control', 'not paired');
  server.close();
  finish();
}
r = await koan(['status']);
check('koan status: paired: yes (Koan knows the pairing)', /^paired: yes$/m.test(r.out), brief(r));

// A tab of the user's (not one an agent opened, `koan tabs --json`'s
// agentOwned), with a web page or an empty tab: a page object, or null.
async function userPage() {
  let agents = new Set();
  try {
    agents = new Set(JSON.parse((await koan(['tabs', '--json'])).out).tabs
        .filter(e => e.agentOwned).map(e => e.targetId));
  } catch {}
  return (await pages()).find(p => p.type === 'page' && !agents.has(p.id) && !/^(chrome|devtools)/.test(p.url)) ??
         null;
}

// --- Stale pairings: a record Koan has never heard of (as after its profile
// was replaced), newer than the good one so that it would be the default.
// Pinned to this agent as the good one is (koan pair pins it): koan prefers
// this agent's own pairings (ChooseClient(), #397), so an unpinned record
// would never be tried, nor removed.
const PROFILE_REAL = fs.realpathSync(PROFILE);
function pinOfGoodPairing() {
  const clients = path.join(AGENT_HOME, '.koan', 'clients');
  for (const id of fs.existsSync(clients) ? fs.readdirSync(clients) : []) {
    try {
      const c = JSON.parse(fs.readFileSync(path.join(clients, id, 'client.json'), 'utf8'));
      if (c.pinned && c.label !== 'stale') return c.pinned;
    } catch {}
  }
  return undefined;
}
function stalePairing(home) {
  const pinned = pinOfGoodPairing();
  const id = crypto.randomBytes(16).toString('hex');
  const dir = path.join(home, '.koan', 'clients', id);
  fs.mkdirSync(dir, { recursive: true, mode: 0o700 });
  fs.writeFileSync(path.join(dir, 'key'), crypto.randomBytes(32).toString('base64'), { mode: 0o600 });
  fs.writeFileSync(path.join(dir, 'client.json'), JSON.stringify({
    clientId: id, label: 'stale', userDataDir: PROFILE_REAL, pairedAt: Date.now() + 3600e3,
    ...(pinned ? { pinned } : {}) }),
    { mode: 0o600 });
  return dir;
}
{
  let stale = stalePairing(AGENT_HOME);
  r = await koan(['status']);
  check('Stale pairing: koan status removes it and still says paired: yes (the good one)',
        /^paired: yes$/m.test(r.out) && /forgot pairing/.test(r.err) && !fs.existsSync(stale), brief(r));
  stale = stalePairing(AGENT_HOME);
  r = await koan(['tabs']);
  check('Stale pairing: a command removes it and runs as the good one (no --client)',
        r.code === 0 && !fs.existsSync(stale) && !/several paired clients/.test(r.err) &&
        (r.err.match(/Koan forgot this pairing/g) ?? []).length === 1, brief(r));
  // Only a forgotten pairing, in a HOME of its own.
  const lone = fs.mkdtempSync(path.join(AGENT_HOME, 'lone-'));
  stale = stalePairing(lone);
  r = await koan(['status'], { home: lone });
  check('Stale pairing: koan status says "paired: no (Koan forgot this pairing; run koan pair)"',
        /^paired: no \(Koan forgot this pairing; run koan pair\)$/m.test(r.out) && !fs.existsSync(stale),
        brief(r));
  stale = stalePairing(lone);
  r = await koan(['open', AGENT_URL], { home: lone });
  check('Stale pairing: koan open with only a forgotten pairing reports the failure once',
        r.code === 3 && r.out === '' && !fs.existsSync(stale) &&
        (r.err.match(/Koan forgot this pairing/g) ?? []).length === 1 &&
        /run koan pair/.test(r.err), brief(r));
}

// --- Open, snapshot, act, screenshot.
r = await koan(['open', AGENT_URL]);
const tab = r.out.split('\n').at(-1);
check('koan open: prints a tab id', r.code === 0 && /^[0-9A-Fa-f]{8,}/.test(tab), brief(r));
await sleep(2500);
r = await koan(['snapshot', tab]);
const snap = r.out;
const ref = snap.split('\n').find(l => /Press me/.test(l))?.match(/@(\d+)/)?.[0];
check('koan snapshot: an outline with the page\'s button and a @ref', r.code === 0 && !!ref,
      brief(r));
r = await koan(['act', tab, 'click', ref ?? '@0']);
check('koan act click: ok', r.code === 0, brief(r));
await sleep(800);
r = await koan(['snapshot', tab]);
check('...the click landed (the heading says Clicked)', /Clicked/.test(r.out), brief(r));
const shot = path.join(AGENT_HOME, 'shot.png');
r = await koan(['screenshot', tab, '-o', shot]);
const png = fs.existsSync(shot) ? fs.readFileSync(shot) : Buffer.alloc(0);
check('koan screenshot: a PNG of the page', r.code === 0 && png.subarray(1, 4).toString() === 'PNG' &&
      png.readUInt32BE(16) > 100, brief(r));

// --- Screenshot sizes (2026-11-20): what each size gives on this display
// (a Retina Mac: 2x), in the agent's own background tab (the lease draws
// it); normal is CSS pixels; --clip @ref is the button's box with its 8 px
// margin, scaled; act --screenshot micro returns a file in the same answer,
// and so does the same line in koan shell.
{
  const pngSize = f => {
    const b = fs.existsSync(f) ? fs.readFileSync(f) : Buffer.alloc(0);
    return b.subarray(1, 4).toString() === 'PNG' ? [b.readUInt32BE(16), b.readUInt32BE(20)] : null;
  };
  const near = (got, want) => !!got && Math.abs(got[0] - want[0]) <= 2 && Math.abs(got[1] - want[1]) <= 2;
  const LINE = /^saved (.+) \((\d+)×(\d+), ~(\d+) image tokens, 1 px = ([\d.]+) CSS px\)$/m;
  // koan_cli_screenshot.cc's PlanShot(), for the check.
  const fitted = (w, h, perCss, edge) => {
    let f = Math.min(1, edge / (Math.max(w, h) * perCss));
    const pixels = w * h * (perCss * f) ** 2, max = 4784 * 784;
    if (pixels > max) f *= Math.sqrt(max / pixels);
    return [Math.max(1, Math.round(w * perCss * f)), Math.max(1, Math.round(h * perCss * f))];
  };
  const agentPage = (await pages()).find(p => p.id === tab);
  const view = agentPage && JSON.parse(await evalIn(agentPage,
      'JSON.stringify({ w: visualViewport.width, h: visualViewport.height, dpr: devicePixelRatio, ' +
      'b: document.querySelector("button").getBoundingClientRect() })') ?? 'null');
  check('Screenshot sizes: the agent tab says its viewport and display scale', !!view?.w, JSON.stringify(view));
  if (view?.w) {
    const { w, h, dpr } = view;
    const want = { micro: fitted(w, h, Math.min(1, dpr), 320), mini: fitted(w, h, Math.min(1, dpr), 640),
                   normal: fitted(w, h, Math.min(1, dpr), 1568), full: fitted(w, h, dpr, 2576) };
    const tokens = {};
    for (const size of ['micro', 'mini', 'normal', 'full']) {
      const f = path.join(AGENT_HOME, `shot-${size}.png`);
      r = await koan(['screenshot', tab, '--size', size, '-o', f]);
      const got = pngSize(f);
      const line = r.out.match(LINE);
      tokens[size] = line ? +line[4] : NaN;
      check(`koan screenshot --size ${size}: ${want[size].join('×')} on this ${dpr}× display, as its line says`,
            r.code === 0 && near(got, want[size]) && !!line && +line[2] === got[0] && +line[3] === got[1],
            `${brief(r)}; the PNG is ${got?.join('×')}`);
    }
    console.log(`INFO  screenshot tokens for a ${w}×${h} viewport at ${dpr}×: ${JSON.stringify(tokens)}`);
    agentShots = { w, h, dpr, want, fitted, pngSize, near, LINE };
    check('...normal is CSS pixels (1×), the viewport\'s own size when it fits 1568',
          Math.max(w, h) > 1568 || near(pngSize(path.join(AGENT_HOME, 'shot-normal.png')), [Math.round(w), Math.round(h)]));
    check('...micro costs at most a tenth of full', tokens.micro * 10 <= tokens.full, JSON.stringify(tokens));
    const plain = path.join(AGENT_HOME, 'shot-default.png');
    r = await koan(['screenshot', tab, '-o', plain, '--json']);
    let json = null;
    try { json = JSON.parse(r.out); } catch {}
    check('koan screenshot (no --size) is normal; --json gives path, width, height, imageTokens, cssPxPerPx',
          r.code === 0 && near(pngSize(plain), want.normal) && json?.path === plain &&
          json?.width === pngSize(plain)?.[0] && json?.imageTokens > 0 && json?.cssPxPerPx > 0, brief(r));

    // The button's box, 8 CSS px around it, cut to the viewport and rounded
    // outwards to whole DIPs, as Koan copies (koan_agent_capture.cc).
    const { b } = view;
    const left = Math.max(0, Math.floor(b.x - 8)), top = Math.max(0, Math.floor(b.y - 8));
    const cw = Math.min(w, Math.ceil(b.x + b.width + 8)) - left;
    const ch = Math.min(h, Math.ceil(b.y + b.height + 8)) - top;
    const clipped = path.join(AGENT_HOME, 'shot-clip.png');
    r = await koan(['screenshot', tab, '--clip', ref ?? '@0', '-o', clipped]);
    check(`koan screenshot --clip ${ref}: the button's box at device pixels (${Math.round(cw * dpr)}×${Math.round(ch * dpr)})`,
          r.code === 0 && near(pngSize(clipped), [cw * dpr, ch * dpr]), `${brief(r)}; ${pngSize(clipped)?.join('×')}`);
    r = await koan(['screenshot', tab, '--clip', ref ?? '@0', '--size', 'normal', '-o', clipped]);
    check('...with --size normal, at CSS pixels', r.code === 0 && near(pngSize(clipped), [cw, ch]),
          `${brief(r)}; ${pngSize(clipped)?.join('×')}`);
    r = await koan(['screenshot', tab, '--clip', 'loc:role=button[name="Press me"]', '-o', clipped]);
    check('...a locator works as the ref does', r.code === 0 && near(pngSize(clipped), [cw * dpr, ch * dpr]), brief(r));
    r = await koan(['screenshot', tab, '--clip', `${Math.round(w) + 50},0,20,20`, '-o', clipped]);
    check('...a rectangle off the viewport is Koan.offScreen', r.code !== 0 && /Koan\.offScreen/.test(r.err), brief(r));
    r = await koan(['screenshot', tab, '--clip', 'loc:role=button[name="Press me"]', '--size', 'micro', '-o', clipped]);
    check('...--clip with --size micro is at most 320 on its long side',
          r.code === 0 && Math.max(...(pngSize(clipped) ?? [999])) <= 320, brief(r));

    const after = path.join(AGENT_HOME, 'shot-act.png');
    fs.rmSync(after, { force: true });
    r = await koan(['act', tab, 'click', ref ?? '@0', '--screenshot', 'micro', '-o', after]);
    const acted = r.out.match(LINE);
    check('koan act --screenshot micro: "ok", then the screenshot\'s line, in the same answer',
          r.code === 0 && /^ok/.test(r.out) && !!acted && acted[1] === after && near(pngSize(after), want.micro),
          `${brief(r)}; ${r.out.split('\n').slice(-1)[0]}`);
    r = await koan(['act', tab, 'click', ref ?? '@0', '--snapshot', '--diff', '--screenshot', 'mini', '-o', after,
                    '--json']);
    try { json = JSON.parse(r.out); } catch { json = null; }
    check('koan act --snapshot --diff --screenshot mini --json: the snapshot and the screenshot',
          r.code === 0 && typeof json?.snapshot === 'string' && json?.screenshot?.path === after &&
          near([json.screenshot.width, json.screenshot.height], want.mini), brief(r));
    const sh = await new Promise(resolve => {
      const child = spawn(CLI, ['shell'], { env, stdio: ['pipe', 'pipe', 'pipe'] });
      let out = '';
      child.stdout.on('data', d => (out += d));
      child.on('close', code => resolve({ code, out }));
      child.stdin.end(`act ${tab} press Tab --screenshot micro -o ${after}\n`);
    });
    check('koan shell: act --screenshot micro answers with the screenshot\'s line',
          LINE.test(sh.out) && /^--- exit 0$/m.test(sh.out), sh.out.slice(0, 200));
  }
}

// --- A covered window (real input only: it changes what is on the screen):
// Koan's window under another app's window (scripts/mac/tools/cover.m), the
// agent's background tab idle past the lease's 3 s hold-off, so it is not
// drawing. Koan.captureScaled takes the lease's capturer, waits for a fresh
// frame and copies it: the image is the size asked for and has the page in
// it (not blank, not the frame from before it was hidden).
if (!realInput() || !agentShots) {
  skipped('Covered window: the agent tab\'s capture', agentShots ? realInputWhyNot() : 'the sizes failed');
} else {
  let cover = null;
  try {
    const bin = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'koan-cover-')), 'cover');
    execFileSync('clang', ['-fobjc-arc', '-framework', 'AppKit', '-o', bin,
                           new URL('../mac/tools/cover.m', import.meta.url).pathname],
                 { stdio: 'ignore', timeout: 120000 });
    const b = (await windowOf(tab))?.bounds;
    cover = b && spawn(bin, [b.left, b.top, b.width, b.height, 60].map(String),
                       { stdio: ['pipe', 'pipe', 'ignore'] });
    let said = '';
    cover?.stdout.on('data', d => (said += d));
    if (cover) await waitFor(() => /ready/.test(said), 10000);
  } catch (e) {
    console.log(`  (no cover window: ${e.message})`);
  }
  const front = (await pages()).find(p => p.id !== tab && !/^(chrome|devtools)/.test(p.url));
  const covered = cover && front &&
      await waitFor(async () => await evalIn(front, 'document.visibilityState') === 'hidden', 8000);
  check('Covered window: another app\'s window covers Koan\'s (its shown tab went hidden)', !!covered,
        cover ? 'the front tab stayed visible' : 'the cover tool did not start');
  if (covered) {
    await sleep(4000);  // past the lease's hold-off: the agent tab stopped drawing
    const f = path.join(AGENT_HOME, 'shot-covered.png');
    const t0 = Date.now();
    r = await koan(['screenshot', tab, '--size', 'mini', '-o', f]);
    const ms = Date.now() - t0;
    const got = agentShots.pngSize(f);
    let colours = 0;
    try {
      const img = decodePng(f);
      const set = new Set();
      for (let y = 0; y < img.height; y += 4) {
        for (let x = 0; x < img.width; x += 4) set.add(img.pixel(x, y).join(','));
      }
      colours = set.size;
    } catch {}
    check(`Covered window: the agent tab's mini capture is ${agentShots.want.mini.join('×')} and has the page in ` +
          'it (not blank)', r.code === 0 && agentShots.near(got, agentShots.want.mini) && colours > 2,
          `${brief(r)}; ${got?.join('×')}, ${colours} colours, ${ms} ms`);
    console.log(`INFO  covered-window capture of an idle agent tab: ${ms} ms`);
  }
  if (cover) {
    cover.stdin.end();
    cover.kill('SIGTERM');
  }
  if (front) await waitFor(async () => await evalIn(front, 'document.visibilityState') === 'visible', 8000);
}

// --- A form: dropdowns, states, stale refs (#340).
{
  r = await koan(['open', server.url('/form')]);
  const form = r.out.split('\n').at(-1);
  await sleep(2000);
  r = await koan(['snapshot', form]);
  const refOf = (text, re) => text.split('\n').find(l => re.test(l))?.match(/@(\d+)\s*$/)?.[0];
  const plan = refOf(r.out, /^\s*combobox "Plan"/);
  const news = refOf(r.out, /^\s*checkbox "News"/);
  const who = refOf(r.out, /^\s*textbox "Name"/);
  const send = refOf(r.out, /^\s*button "Send"/);
  check('Form: the snapshot has the dropdown, checkbox, field and button with refs',
        r.code === 0 && !!(plan && news && who && send), brief(r));
  check('Form: ...the default radio shows [checked], the others not',
        /radio "Email" \[checked[,\]]/.test(r.out) && /radio "SMS" (\[(?!checked)[^\]]*\] )?@/.test(r.out) &&
        /checkbox "News" (\[(?!checked)[^\]]*\] )?@/.test(r.out), r.out.split('\n').filter(l => /radio|checkbox/.test(l)).join(' | '));
  r = await koan(['act', form, 'select', plan ?? '@0', 'Pro']);
  check('Form: act select sets the dropdown by its label', r.code === 0 && /selected "Pro"/.test(r.out), brief(r));
  r = await koan(['act', form, 'fill', plan ?? '@0', 'Team']);
  check('Form: ...fill on a dropdown sets it too', r.code === 0 && /selected "Team"/.test(r.out), brief(r));
  r = await koan(['act', form, 'select', plan ?? '@0', 'pro']);
  check('Form: ...select by value', r.code === 0 && /selected "Pro"/.test(r.out), brief(r));
  r = await koan(['act', form, 'select', plan ?? '@0', 'Gold']);
  check('Form: ...an option it lacks fails, naming its options (Koan.noOption)',
        r.code !== 0 && /Koan\.noOption/.test(r.err) && /"Free", "Pro", "Team"/.test(r.err), brief(r));
  r = await koan(['act', form, 'fill', news ?? '@0', 'yes']);
  check('Form: fill on a checkbox fails (Koan.notEditable), never "ok"',
        r.code !== 0 && /Koan\.notEditable/.test(r.err) && !/^ok/.test(r.out), brief(r));
  r = await koan(['act', form, 'click', news ?? '@0']);
  check('Form: act click on the checkbox', r.code === 0, brief(r));
  r = await koan(['act', form, 'fill', who ?? '@0', 'Ada']);
  check('Form: act fill on the text field', r.code === 0 && /^ok/.test(r.out), brief(r));
  await sleep(300);
  r = await koan(['snapshot', form]);
  check('Form: the snapshot shows the checkbox [checked] and the dropdown\'s value',
        /checkbox "News" \[checked[,\]]/.test(r.out) && /combobox "Plan" = "Pro"/.test(r.out),
        r.out.split('\n').filter(l => /combobox|checkbox/.test(l)).join(' | '));
  r = await koan(['act', form, 'click', send ?? '@0']);
  const sent = await waitFor(() => submitted.length > 0, 8000);
  check('Form: Send submits what was set (plan=pro, news, Ada, email)',
        r.code === 0 && sent && /plan=pro/.test(submitted.at(-1)) && /news=on/.test(submitted.at(-1)) &&
        /who=Ada/.test(submitted.at(-1)) && /contact=email/.test(submitted.at(-1)),
        `${brief(r)}; ${submitted.at(-1)}`);
  await sleep(1000);
  // The tab shows /done now: the form's refs belong to the page before.
  r = await koan(['act', form, 'click', send ?? '@0']);
  check('Stale ref: a ref from before a same-site navigate is refused (Koan.staleRef)',
        r.code !== 0 && /Koan\.staleRef/.test(r.err) && /koan snapshot/.test(r.err), brief(r));
  check('...and nothing was submitted again', submitted.length === 1, `${submitted.length} submissions`);
  // Cross-site (127.0.0.1 to localhost): a new renderer, whose node ids
  // start again, so the same form's ref would name the new page's dropdown
  // unless it is refused.
  r = await koan(['act', form, 'navigate', server.url('/form')]);
  await sleep(2000);
  r = await koan(['snapshot', form]);
  const plan2 = refOf(r.out, /^\s*combobox "Plan"/);
  r = await koan(['act', form, 'navigate', `http://localhost:${server.port}/form`]);
  await sleep(2000);
  r = await koan(['act', form, 'select', plan2 ?? '@0', 'Team']);
  check('Stale ref: a ref from before a cross-site navigate is refused (Koan.staleRef)',
        r.code !== 0 && /Koan\.staleRef/.test(r.err), brief(r));
  r = await koan(['act', form, 'click', '@999999']);
  check('Stale ref: a ref no snapshot showed is refused too', r.code !== 0 && /Koan\.staleRef/.test(r.err),
        brief(r));
  // An option of a closed dropdown has no box: a click on it chooses it
  // instead of failing with a raw CDP error. Its ref from the raw tree (the
  // outline may leave a closed list's options out).
  r = await koan(['snapshot', form, '--json']);
  let option;
  try {
    option = JSON.parse(r.out).nodes.find(n => n.role?.value === 'option' && n.name?.value === 'Team')
      ?.backendDOMNodeId;
  } catch {}
  option = option && `@${option}`;
  r = await koan(['act', form, 'click', option ?? '@0']);
  check('Form: a click on an option of a closed dropdown chooses it (no "Node does not have a layout object")',
        r.code === 0 && /selected "Team"/.test(r.out) && !/-32000/.test(r.err), brief(r));

  // Compact against --full on a long page; refs are the same in both.
  r = await koan(['act', form, 'navigate', server.url('/long')]);
  await sleep(2000);
  const compact = await koan(['snapshot', form]);
  const full = await koan(['snapshot', form, '--full']);
  const lines = t => t.split('\n').length;
  check('Compact snapshot: much shorter than --full (under half)',
        compact.code === 0 && full.code === 0 && lines(compact.out) * 2 < lines(full.out),
        `${lines(compact.out)} vs ${lines(full.out)} lines`);
  check('Compact snapshot: no InlineTextBox lines, text runs joined',
        !/InlineTextBox/.test(compact.out) && /"Paragraph 0 has bold, italic and split text over two lines\."/.test(compact.out) &&
        /InlineTextBox/.test(full.out), compact.out.split('\n').slice(0, 4).join(' | '));
  const press = refOf(compact.out, /^\s*button "Press here"/);
  check('Compact snapshot: the button\'s ref is the same in --full',
        !!press && refOf(full.out, /^\s*button "Press here"/) === press, `${press}`);
  r = await koan(['act', form, 'click', press ?? '@0']);
  await sleep(500);
  const after = await koan(['snapshot', form]);
  check('Compact snapshot: its ref works (the heading says Pressed)',
        r.code === 0 && /heading "Pressed"/.test(after.out), brief(r));
}

// --- Settle and return (#cli-speed): act waits for the page, so --snapshot
// shows what the action did with no sleep; navigate returns with the
// server's status, fast, and a server that never answers is a Koan.timeout
// after --timeout, not "no answer"; a Material-style dropdown of plain divs
// whose listbox is a portal at the end of a long page: its open list is the
// snapshot's first lines, its option takes on the first click, and choose
// works; koan shell runs several commands over one connection. Each step
// prints how long it took (INFO lines), old way against new, for the PR.
{
  const COUNTRIES = ['Argentina', 'Brazil', 'Chile', 'India', 'Uganda', 'United Kingdom',
                     'United States', 'Zambia'];
  const PAGES = {
    '/fetch': `<!doctype html><title>Fetch</title><h1>Fetch</h1>
      <button onclick="fetch('/slow-data').then(r => r.text()).then(t => {
        document.getElementById('out').textContent = t; })">Load</button>
      <p id=out>Nothing yet</p>`,
    // Like Material UI's Select: a div with role=combobox that opens on
    // mousedown, a listbox appended to the body (after a page longer than
    // the compact snapshot's 1500 lines), which grows in (a transform
    // transition) and scrolls; an option takes a click only after a
    // pointerdown with the left button down (buttons 1), as libraries that
    // check it do.
    '/mui': `<!doctype html><title>Profile</title><h1>Profile</h1>
      <label id=cl>Country</label>
      <div id=cb role=combobox tabindex=0 aria-labelledby=cl aria-haspopup=listbox
           aria-expanded=false aria-controls=menu style="border:1px solid;width:200px">Choose</div>
      <h2 id=picked>Picked: none</h2>
      ${Array.from({ length: 1600 }, (_, i) => `<p>Filler ${i}</p>`).join('')}
      <script>
        const cb = document.getElementById('cb');
        let pop = null;
        function close() { pop?.remove(); pop = null; cb.setAttribute('aria-expanded', 'false'); }
        cb.addEventListener('mousedown', e => {
          if (e.button !== 0 || pop) return;
          e.preventDefault();
          pop = document.createElement('div');
          pop.style = 'position:fixed;inset:0';
          pop.addEventListener('click', e => { if (e.target === pop) close(); });
          const list = document.createElement('ul');
          list.id = 'menu';
          list.setAttribute('role', 'listbox');
          list.setAttribute('aria-labelledby', 'cl');
          list.style = 'position:absolute;left:20px;top:90px;margin:0;padding:4px;list-style:none;' +
                       'background:#fff;border:1px solid #888;max-height:120px;overflow:auto;' +
                       'transform:scale(0.6);transform-origin:top left;transition:transform 300ms';
          for (const name of ${JSON.stringify(COUNTRIES)}) {
            const li = document.createElement('li');
            li.setAttribute('role', 'option');
            li.textContent = name;
            li.style = 'padding:6px 12px';
            li.addEventListener('pointerdown', e => { li.dataset.down = String(e.buttons); });
            li.addEventListener('click', () => {
              if (li.dataset.down !== '1') return;
              cb.textContent = name;
              document.getElementById('picked').textContent = 'Picked: ' + name;
              close();
            });
            list.append(li);
          }
          pop.append(list);
          document.body.append(pop);
          cb.setAttribute('aria-expanded', 'true');
          requestAnimationFrame(() => requestAnimationFrame(() => { list.style.transform = 'scale(1)'; }));
        });
      </script>`,
  };
  const pagesServer = http.createServer((q, s) => {
    const u = new URL(q.url, 'http://x');
    if (u.pathname === '/slow-data') {
      setTimeout(() => s.end('Loaded late'), 1500);
    } else if (u.pathname === '/504') {
      s.writeHead(504, { 'content-type': 'text/html' });
      s.end('<!doctype html><title>Gateway</title><h1>Upstream timed out</h1>');
    } else if (u.pathname === '/504-empty') {
      s.writeHead(504);
      s.end();
    } else if (u.pathname === '/hang') {
      // Never answers (until the server closes).
    } else {
      s.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' });
      s.end(PAGES[u.pathname] ?? '<!doctype html><title>Page</title><h1>Page</h1>');
    }
  });
  await new Promise(res => pagesServer.listen(0, '127.0.0.1', res));
  const at = p => `http://127.0.0.1:${pagesServer.address().port}${p}`;
  const timed = async (args, opts) => {
    const t0 = Date.now();
    const result = await koan(args, opts);
    return { ...result, ms: Date.now() - t0 };
  };
  const refOf = (text, re) => text.split('\n').find(l => re.test(l))?.match(/@(\d+)\s*$/)?.[0];

  // A click whose result comes from a slow fetch.
  r = await koan(['open', at('/fetch')]);
  const ft = r.out.split('\n').at(-1);
  r = await koan(['act', ft, 'navigate', at('/fetch'), '--snapshot']);
  const load = refOf(r.out, /^\s*button "Load"/);
  check('Settle: act navigate --snapshot prints ok and the page\'s snapshot',
        r.code === 0 && /^ok$/m.test(r.out) && !!load, brief(r));
  // The old way: act, sleep 1.5 s, snapshot (three steps, the sleep a guess).
  let t0 = Date.now();
  r = await koan(['act', ft, 'click', load ?? '@0']);
  await sleep(1500);
  let snap = await koan(['snapshot', ft]);
  const oldMs = Date.now() - t0;
  r = await koan(['act', ft, 'navigate', at('/fetch'), '--snapshot']);
  const load2 = refOf(r.out, /^\s*button "Load"/);
  const clicked = await timed(['act', ft, 'click', load2 ?? '@0', '--snapshot']);
  check('Settle: act click --snapshot shows the slow fetch\'s result, no sleep',
        clicked.code === 0 && /^ok\n/.test(clicked.out) && /"Loaded late"/.test(clicked.out),
        brief(clicked));
  console.log(`INFO  click + slow fetch: old (act, sleep 1.5, snapshot) ${oldMs} ms` +
              ` (${/Loaded late/.test(snap.out) ? 'saw it' : 'missed it'}); new (act --snapshot) ${clicked.ms} ms`);

  // Navigate: a server's error status, at once.
  let nav = await timed(['act', ft, 'navigate', at('/504'), '--snapshot']);
  check('Navigate: a 504 page returns fast with its status (ok (HTTP 504 ...), exit 0)',
        nav.code === 0 && /^ok \(HTTP 504/.test(nav.out) && /Upstream timed out/.test(nav.out) &&
        nav.ms < 5000, `${nav.ms} ms; ${brief(nav)}`);
  nav = await timed(['act', ft, 'navigate', at('/504-empty')]);
  check('Navigate: a 504 with no body (Chromium\'s error page) says the status too',
        nav.code === 0 && /^ok \(HTTP 504/.test(nav.out) && nav.ms < 5000, `${nav.ms} ms; ${brief(nav)}`);
  nav = await timed(['act', ft, 'navigate', at('/hang'), '--timeout', '3']);
  check('Navigate: a server that never answers is Koan.timeout after --timeout, not "no answer"',
        nav.code !== 0 && /Koan\.timeout/.test(nav.err) && !/no answer/.test(nav.err) &&
        nav.ms >= 3000 && nav.ms < 8000, `${nav.ms} ms; ${brief(nav)}`);
  // Koan stopped that navigation: the tab answers again at once (Chromium
  // holds a navigating tab's renderer commands until it commits or fails).
  const after = await timed(['snapshot', ft]);
  check('...and the tab answers right after it (the navigation was stopped)',
        after.code === 0 && after.ms < 5000, `${after.ms} ms; ${brief(after)}`);

  // A Material-style dropdown in a portal.
  r = await koan(['act', ft, 'navigate', at('/mui'), '--snapshot']);
  let combo = refOf(r.out, /^\s*combobox "Country"/);
  check('Dropdown: the snapshot has the custom combobox', r.code === 0 && !!combo, brief(r));
  let opened = await timed(['act', ft, 'click', combo ?? '@0', '--snapshot']);
  const lines = opened.out.split('\n');
  check('Dropdown: after opening it, the snapshot starts with its listbox (a portal past the 1500th line)',
        opened.code === 0 && /^popup of combobox "Country" @\d+/.test(lines[1] ?? '') &&
        lines.slice(1, 14).some(l => /option "Uganda" @\d+/.test(l)),
        lines.slice(0, 4).join(' | '));
  const uganda = refOf(opened.out, /^\s*option "Uganda"/);
  r = await koan(['act', ft, 'click', uganda ?? '@0', '--snapshot']);
  check('Dropdown: the option takes on the first click (buttons 1, after its list stopped growing)',
        r.code === 0 && /heading "Picked: Uganda"/.test(r.out), brief(r));
  const chose = await timed(['act', ft, 'choose', combo ?? '@0', 'Zambia', '--snapshot']);
  check('Dropdown: act choose opens it, scrolls to the option, clicks it and checks it',
        chose.code === 0 && /^ok: chose "Zambia"/.test(chose.out) && /heading "Picked: Zambia"/.test(chose.out),
        brief(chose));
  console.log(`INFO  custom dropdown: choose --snapshot ${chose.ms} ms (one command; before: click, sleep, snapshot, click, sleep, snapshot)`);
  r = await koan(['act', ft, 'choose', combo ?? '@0', 'Atlantis']);
  check('Dropdown: choose of an option it lacks fails naming the options (Koan.noOption)',
        r.code !== 0 && /Koan\.noOption/.test(r.err) && /"Argentina", "Brazil"/.test(r.err), brief(r));
  await koan(['act', ft, 'press', 'Escape']);

  // koan shell: several commands, one connection; and what a command costs.
  function shell(lines) {
    return new Promise(resolve => {
      const t0 = Date.now();
      const child = spawn(CLI, ['shell'], { env, stdio: ['pipe', 'pipe', 'pipe'] });
      let out = '';
      child.stdout.on('data', d => (out += d));
      child.on('close', code => resolve({ code, out, ms: Date.now() - t0 }));
      child.stdin.end(lines.join('\n') + '\n');
    });
  }
  r = await koan(['act', ft, 'navigate', at('/fetch')]);
  const sh = await shell([`koan tabs`, `snapshot ${ft}`, `# a note`,
                          JSON.stringify({ id: 'j1', args: ['act', ft, 'press', 'Tab'] }),
                          `act ${ft} fill @999999 "x"`]);
  const ends = sh.out.match(/^--- exit \d+$/gm) ?? [];
  const json = sh.out.split('\n').find(l => l.startsWith('{"id":"j1"'));
  check('koan shell: each plain command answered, ending "--- exit N" (a failure is one too)',
        ends.length === 3 && ends[0] === '--- exit 0' && ends[1] === '--- exit 0' && ends[2] !== '--- exit 0' &&
        /button "Load" @\d+/.test(sh.out) && /Koan\.staleRef/.test(sh.out), sh.out.slice(0, 200));
  let parsed;
  try { parsed = JSON.parse(json ?? ''); } catch {}
  check('koan shell: a JSON line gets one JSON answer with its id', parsed?.exit === 0 && parsed?.out === 'ok',
        json ?? 'none');
  const N = 10;
  t0 = Date.now();
  for (let i = 0; i < N; ++i) await koan(['snapshot', ft]);
  const separate = (Date.now() - t0) / N;
  const many = await shell(Array.from({ length: N }, () => `snapshot ${ft}`));
  const together = many.ms / N;
  check('koan shell: N snapshots in one shell all succeed',
        (many.out.match(/^--- exit 0$/gm) ?? []).length === N, `${many.out.slice(0, 120)}`);
  console.log(`INFO  per command (koan snapshot, this Mac, no ssh): separate processes ${separate.toFixed(1)} ms,` +
              ` koan shell ${together.toFixed(1)} ms`);
  pagesServer.closeAllConnections?.();
  pagesServer.close();
}

// --- Batches: several steps in one koan act (agent-control.md section 12,
// "Batches"). The pages: a 3-page form (/wiz/1 -> /wiz/2 -> /wiz/3 ->
// /wiz/done, which records what was sent); /typing, whose button waits 2.5 s
// for the server; /list, whose button adds an item; /rerender, whose
// Shuffle re-creates its Continue button.
{
  const wizard = [];
  const WIZ = {
    '/wiz/1': `<!doctype html><title>Step 1</title><h1>Your name</h1>
      <form action=/wiz/2><label>First name <input name=first></label>
      <label>Last name <input name=last></label>
      <label>Country <select name=country><option>India</option><option>United Kingdom</option>
        <option>Uganda</option></select></label><button>Next</button></form>`,
    '/wiz/2': q => `<!doctype html><title>Step 2</title><h1>Contact</h1>
      <form action=/wiz/3>${[...q.searchParams].map(([k, v]) =>
        `<input type=hidden name=${k} value="${v.replace(/"/g, '&quot;')}">`).join('')}
      <label>Email <input name=email type=email></label>
      <label>Plan <select name=plan><option>Free</option><option>Pro</option></select></label>
      <button>Next</button></form>`,
    '/wiz/3': q => `<!doctype html><title>Step 3</title><h1>Confirm</h1>
      <form action=/wiz/done>${[...q.searchParams].map(([k, v]) =>
        `<input type=hidden name=${k} value="${v.replace(/"/g, '&quot;')}">`).join('')}
      <label><input type=checkbox name=agree> I agree</label><button>Submit</button></form>`,
    '/wiz/done': q => {
      wizard.push(Object.fromEntries(q.searchParams));
      return `<!doctype html><title>Done</title><h1>Thanks, ${q.searchParams.get('first')}</h1>`;
    },
    '/typing': `<!doctype html><title>Typing</title><h1>Typing</h1>
      <label>One <input id=f1></label>
      <button onclick="fetch('/slow').then(r => r.text()).then(t => { document.getElementById('st').textContent = t; })">Check</button>
      <p id=st>Not checked</p>
      <label>Two <input id=f2></label><label>Three <input id=f3></label>`,
    '/list': `<!doctype html><title>List</title><h1>List</h1><ul id=l><li>First item</li></ul>
      <label>Note <input id=note></label>
      <button onclick="const li = document.createElement('li'); li.textContent = 'Item ' + (l.children.length + 1);
        l.append(li); document.getElementById('st').textContent = 'Added';">Add</button>
      <p id=st role=status></p>`,
    '/rerender': `<!doctype html><title>Rerender</title><h1 id=h>Waiting</h1><div id=box></div>
      <button onclick="render()">Shuffle</button><button>Save</button><button>Save</button>
      <script>
        function render() {
          document.getElementById('box').innerHTML = '<button id=go>Continue</button>';
          document.getElementById('go').onclick = () => { document.getElementById('h').textContent = 'Continued'; };
        }
        render();
      </script>`,
  };
  const batchServer = http.createServer((q, s) => {
    const u = new URL(q.url, 'http://x');
    if (u.pathname === '/slow') {
      setTimeout(() => s.end('Checked'), 2500);
      return;
    }
    const page = WIZ[u.pathname];
    s.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' });
    s.end(typeof page === 'function' ? page(u) : page ?? '<!doctype html><title>Page</title>');
  });
  await new Promise(res => batchServer.listen(0, '127.0.0.1', res));
  const at = p => `http://127.0.0.1:${batchServer.address().port}${p}`;
  const loc = (role, name) => `loc:role=${role}[name="${name}"]`;
  const refOf = (text, re) => text.split('\n').find(l => re.test(l))?.match(/@(\d+)/)?.[0];
  const counted = () => {
    let calls = 0;
    const t0 = Date.now();
    return { run: async (args, opts) => { ++calls; return koan(args, opts); },
             get calls() { return calls; }, get ms() { return Date.now() - t0; } };
  };

  // The 3-page form in at most 3 calls: open, one batch, (the batch's own
  // snapshot is the check).
  const quick = counted();
  r = await quick.run(['open', at('/wiz/1'), '--task', 'batch']);
  const wiz = r.out.split('\n').at(-1);
  r = await quick.run(['act', wiz,
    'fill', loc('textbox', 'First name'), 'Ada', 'fill', loc('textbox', 'Last name'), 'Lovelace',
    'choose', loc('combobox', 'Country'), 'United Kingdom', 'click', loc('button', 'Next'),
    'fill', loc('textbox', 'Email'), 'ada@example.com', 'choose', loc('combobox', 'Plan'), 'Pro',
    'click', loc('button', 'Next'),
    'click', loc('checkbox', 'I agree'), 'click', loc('button', 'Submit'),
    '--until', 'url~/wiz/done', '--interactive']);
  const sent = wizard.at(-1) ?? {};
  check('Batch: a 3-page form in at most 3 koan calls, every step one receipt',
        quick.calls <= 3 && r.code === 0 && (r.out.match(/^\d+\/9 /gm) ?? []).length === 9 &&
        /^until url~\/wiz\/done: met$/m.test(r.out), `${quick.calls} calls; ${brief(r)}`);
  check('...the server got every field, and the batch\'s snapshot shows the last page',
        sent.first === 'Ada' && sent.last === 'Lovelace' && sent.country === 'United Kingdom' &&
        sent.email === 'ada@example.com' && sent.plan === 'Pro' && sent.agree === 'on' &&
        /heading "Thanks, Ada"/.test(r.out), JSON.stringify(sent));
  const newMs = quick.ms;

  // The same form, the old way: one verb per call, a snapshot to read each
  // page's refs.
  const slow = counted();
  r = await slow.run(['act', wiz, 'navigate', at('/wiz/1')]);
  let snap = await slow.run(['snapshot', wiz]);
  for (const [re, verb, text] of [[/textbox "First name"/, 'fill', 'Ada'], [/textbox "Last name"/, 'fill', 'Lovelace'],
                                  [/combobox "Country"/, 'select', 'United Kingdom'], [/button "Next"/, 'click']]) {
    await slow.run(['act', wiz, verb, refOf(snap.out, re) ?? '@0', ...(text ? [text] : [])]);
  }
  snap = await slow.run(['snapshot', wiz]);
  for (const [re, verb, text] of [[/textbox "Email"/, 'fill', 'ada@example.com'], [/combobox "Plan"/, 'select', 'Pro'],
                                  [/button "Next"/, 'click']]) {
    await slow.run(['act', wiz, verb, refOf(snap.out, re) ?? '@0', ...(text ? [text] : [])]);
  }
  snap = await slow.run(['snapshot', wiz]);
  for (const re of [/checkbox "I agree"/, /button "Submit"/]) {
    await slow.run(['act', wiz, 'click', refOf(snap.out, re) ?? '@0']);
  }
  snap = await slow.run(['snapshot', wiz]);
  check('...the old way gets there too (the comparison is fair)', /heading "Thanks, Ada"/.test(snap.out),
        brief(snap));
  console.log(`INFO  3-page form: old loop (one verb a call) ${slow.calls} calls, ${slow.ms} ms;` +
              ` batch ${quick.calls} calls, ${newMs} ms`);

  // The loop the guide recommends, with refs: snapshot --interactive, then a
  // batch per page with --until and --diff.
  const guided = counted();
  r = await guided.run(['act', wiz, 'navigate', at('/wiz/1')]);
  snap = await guided.run(['snapshot', wiz, '--interactive']);
  const ref = re => refOf(snap.out, re) ?? '@0';
  snap = await guided.run(['act', wiz, 'fill', `${ref(/textbox "First name"/)}=Ada`, `${ref(/textbox "Last name"/)}=Lovelace`,
                           'choose', ref(/combobox "Country"/), 'Uganda', 'click', ref(/button "Next"/),
                           '--until', 'url~/wiz/2', '--interactive', '--diff']);
  check('Batch: --diff after a navigation says "baseline reset" and prints the new page',
        snap.code === 0 && /^baseline reset \(the tab shows a new page\)/m.test(snap.out) &&
        /textbox "Email"/.test(snap.out), brief(snap));
  snap = await guided.run(['act', wiz, 'fill', ref(/textbox "Email"/), 'ada@example.com', 'choose',
                           ref(/combobox "Plan"/), 'Free', 'click', ref(/button "Next"/),
                           '--until', 'url~/wiz/3', '--interactive', '--diff']);
  snap = await guided.run(['act', wiz, 'click', ref(/checkbox "I agree"/), 'click', ref(/button "Submit"/),
                           '--until', 'url~/wiz/done', '--interactive', '--diff']);
  check('...the guide\'s loop (snapshot --interactive, a batch a page) finishes the form',
        snap.code === 0 && /heading "Thanks, Ada"/.test(snap.out) && wizard.at(-1)?.country === 'Uganda',
        brief(snap));
  console.log(`INFO  3-page form, the guide's loop with refs: ${guided.calls} calls, ${guided.ms} ms`);

  // A stale ref mid-batch fails, and nothing after it runs.
  r = await koan(['act', wiz, 'navigate', at('/wiz/1')]);
  snap = await koan(['snapshot', wiz, '--interactive']);
  r = await koan(['act', wiz, 'click', ref(/button "Next"/), 'fill', ref(/textbox "First name"/), 'Late']);
  check('Batch: a ref from before a navigation, mid-batch, stops it (Koan.staleRef, nothing guessed)',
        r.code !== 0 && /^1\/2 click @\d+: ok/m.test(r.out) &&
        /^stopped at 2\/2 fill @\d+: Koan\.staleRef/m.test(r.out), brief(r));

  // The user takes the tab while a batch runs: it stops before the next
  // step, and says where. The tab is shown so that its pill (Take over) is:
  // it is in the agent's Space, so through the chip's menu ("Go to", real
  // clicks), as the user reaches it (CDP's activation of it is held).
  r = await koan(['open', at('/typing'), '--task', 'batch']);
  const typing = r.out.split('\n').at(-1);
  snap = await koan(['snapshot', typing, '--interactive']);
  const [one, check1, two, three] = [/textbox "One"/, /button "Check"/, /textbox "Two"/, /textbox "Three"/]
      .map(re => refOf(snap.out, re) ?? '@0');
  await sleep(1000);
  const mine = await userPage();
  const went = !realInput() ? { ok: false, why: realInputWhyNot() }
             : mine ? await goToAgentTab('Typing', mine) : { ok: false, why: 'no tab of the user\'s' };
  const shown = went.ok ? {} : { error: { message: `the chip's "Go to Typing": ${went.why}; ` +
                                                    `${JSON.stringify(went.items ?? [])}` } };
  await sleep(500);
  let tookOver = false;
  const batch = koan(['act', typing, 'fill', one, 'first', 'click', check1, 'fill', two, 'second',
                      'fill', three, 'third'], {
    whileRunning: async () => {
      // During the click's wait for the slow server.
      await sleep(900);
      const vs = await viewTree();
      const button = vs.find(v => v.shown && v.cls === 'LabelButton' && v.props.Text === 'Take over');
      if (button) {
        await clickThroughRoot(vs, button);
        tookOver = true;
      }
    } });
  r = await batch;
  const typed = await waitFor(async () => {
    const page = (await pages()).find(p => p.id === typing);
    return page && evalIn(page, `[f1.value, f2.value, f3.value].join('|')`);
  });
  if (shown.error || !tookOver) {
    skipped('a batch stopped by the user', shown.error ? shown.error.message : 'no Take over button in the pill');
  } else {
    check('Batch: the user taking the tab stops it, saying where and why (Koan.paused)',
          r.code !== 0 && /^stopped at [23]\/4 .*Koan\.paused/m.test(r.out) && /^not run: .*4\/4 fill/m.test(r.out),
          brief(r));
    check('...nothing is typed after the user took the tab', typed === 'first||', `${typed}`);
    // Hand it back (the pill), as the user would.
    const vs = await viewTree();
    const back = vs.find(v => v.shown && v.cls === 'LabelButton' && v.props.Text === 'Hand back');
    if (back) await clickThroughRoot(vs, back);
    await sleep(800);
  }
  // Back to a tab of the user's (CDP's activation of an agent's tab would
  // be held as a focus request).
  const user = await userPage();
  if (user) await browserCall('Target.activateTarget', { targetId: user.id });

  // --diff after a change: only the change. A ref stays the same while its
  // element does (typing in it, a change elsewhere).
  r = await koan(['act', wiz, 'navigate', at('/list')]);
  snap = await koan(['snapshot', wiz]);
  const add = refOf(snap.out, /button "Add"/);
  const note = refOf(snap.out, /textbox "Note"/);
  r = await koan(['act', wiz, 'fill', note ?? '@0', 'kept', 'click', add ?? '@0', '--diff']);
  const diffBody = r.out.split('\n').filter(l => /^[+~-] /.test(l));
  check('Batch: --diff after a change prints only what changed (the field, the new item)',
        r.code === 0 && /^diff since the last snapshot of this page: /m.test(r.out) &&
        diffBody.some(l => /^\+ .*Item 2/.test(l)) && diffBody.some(l => /^~ textbox "Note" = "kept"/.test(l)) &&
        !/heading "List"/.test(diffBody.join('\n')), r.out.slice(0, 300));
  r = await koan(['act', wiz, 'fill', note ?? '@0', 'again']);
  check('...the field\'s ref still works after the page changed around it', r.code === 0, brief(r));

  // A locator survives a re-render; a ref does not, and is not guessed.
  r = await koan(['act', wiz, 'navigate', at('/rerender')]);
  snap = await koan(['snapshot', wiz, '--locators']);
  const go = refOf(snap.out, /button "Continue"/);
  check('Locators: the snapshot prints a locator after a ref that has one, not after the two Saves',
        /button "Continue" @\d+ loc:role=button\[name="Continue"\]/.test(snap.out) &&
        /button "Save" @\d+$/m.test(snap.out), snap.out.slice(0, 300));
  r = await koan(['act', wiz, 'click', refOf(snap.out, /button "Shuffle"/) ?? '@0']);
  r = await koan(['act', wiz, 'click', go ?? '@0']);
  check('Locators: a re-rendered element\'s old ref is refused (Koan.staleRef)',
        r.code !== 0 && /Koan\.staleRef/.test(r.err), brief(r));
  r = await koan(['act', wiz, 'click', loc('button', 'Continue'), '--snapshot']);
  check('...its locator finds the new element and the click lands', r.code === 0 && /heading "Continued"/.test(r.out),
        brief(r));
  r = await koan(['act', wiz, 'click', loc('button', 'Save')]);
  check('...a locator that names two elements fails, listing both (Koan.ambiguous)',
        r.code !== 0 && /Koan\.ambiguous/.test(r.err) && (r.err.match(/button "Save" @\d+/g) ?? []).length === 2,
        brief(r));

  r = await koan(['end', '--task', 'batch']);
  check('Batch: koan end --task batch', r.code === 0, brief(r));
  batchServer.closeAllConnections?.();
  batchServer.close();
}

// --- Extensions and permissions in an agent tab (`agentTab`), against a
// user tab. `shared`: the switches are on, so the agent tab runs the user's
// extensions and has the user's site permissions; else it is in a container,
// where neither reaches it.
async function extensionsAndPermissions(when, agentTab, shared) {
  const user = (await browserCall('Target.createTarget', { url: AGENT_URL, background: true })).result?.targetId;
  await sleep(2500);
  const page = (await pages()).find(p => p.id === user);
  const ext = page && await evalIn(page, 'document.documentElement.dataset.koanExtension ?? ""');
  check(`${when}: control: the test extension runs in a user tab`, ext === 'ran', `"${ext}"`);
  const userPerm = page && await evalIn(page, 'document.getElementById("perm").textContent');
  check(`${when}: control: a user tab may ask for permissions`, /geolocation=prompt/.test(userPerm ?? ''),
        userPerm);
  if (user) await browserCall('Target.closeTarget', { targetId: user });
  const snap = await koan(['snapshot', agentTab]);
  const perm = snap.out.split('\n').find(l => /geolocation=/.test(l)) ?? brief(snap);
  if (shared) {
    check(`${when}: the extension runs in the agent tab too`,
          snap.code === 0 && /EXTENSION RAN/.test(snap.out), brief(snap));
    check(`${when}: ...which has the user's site permissions (prompt, not denied)`,
          /geolocation=prompt/.test(snap.out) && /notifications=prompt/.test(snap.out), perm);
  } else {
    check(`${when}: no extension ran in the agent tab`,
          snap.code === 0 && !/EXTENSION RAN/.test(snap.out), brief(snap));
    check(`${when}: ...where geolocation and notifications are denied, unprompted`,
          /geolocation=denied/.test(snap.out) && /notifications=denied/.test(snap.out), perm);
  }
}
await extensionsAndPermissions('Defaults', tab, true);

// --- A certificate warning in an agent tab: no acting on it.
{
  let cert = null;
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'koan-cert-'));
  try {
    execFileSync('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                             '-subj', '/CN=127.0.0.1', '-keyout', `${dir}/k.pem`, '-out', `${dir}/c.pem`],
                 { stdio: 'ignore', timeout: 30000 });
    cert = { key: fs.readFileSync(`${dir}/k.pem`), cert: fs.readFileSync(`${dir}/c.pem`) };
  } catch {}
  if (!cert) {
    skipped('interstitial', 'openssl could not make a self-signed certificate');
  } else {
    const bad = https.createServer(cert, (q, s) => s.end('<title>Bad cert</title><h1>Behind the warning</h1>'));
    await new Promise(res => bad.listen(0, '127.0.0.1', res));
    r = await koan(['open', `https://127.0.0.1:${bad.address().port}/`]);
    const badTab = r.out.split('\n').at(-1);
    await sleep(3000);
    r = await koan(['act', badTab, 'press', 'Enter']);
    check('An agent tab behind a certificate warning: act is refused (Koan.interstitial)',
          r.code !== 0 && /Koan\.interstitial/.test(r.err + r.out), brief(r));
    bad.close();
  }
  fs.rmSync(dir, { recursive: true, force: true });
}

// --- Take-over of the user's tab. `limited`: "Limit a taken-over tab to
// its site" is on, so an off-site navigate is refused.
async function takeOver(when, limited) {
  const user = (await browserCall('Target.createTarget', { url: server.url('/watch') })).result?.targetId;
  await sleep(2000);
  await browserCall('Target.activateTarget', { targetId: user });
  await sleep(800);
  let ok = false;
  let r = await koan(['take-over'], { timeout: 90000,
                                      whileRunning: async printed => {
                                        ok = await allow(/ wants to control a tab$/, printed);
                                      } });
  const taken = r.out.split('\n').at(-1);
  check(`${when}: koan take-over: the user's tab, after its prompt was allowed`,
        ok && r.code === 0 && !!taken, brief(r));
  if (!limited) await userTabShots(when, taken);
  r = await koan(['act', taken, 'navigate', server.url('/p?t=Same%20site')]);
  check(`${when}: ...a same-site navigate works`, r.code === 0, brief(r));
  r = await koan(['act', taken, 'navigate', `http://localhost:${server.port}/p?t=Elsewhere`]);
  if (limited) {
    check(`${when}: ...an off-site navigate is refused (Koan.offSite)`,
          r.code !== 0 && /Koan\.offSite/.test(r.err + r.out), brief(r));
  } else {
    check(`${when}: ...and so does an off-site one (no site limit)`, r.code === 0, brief(r));
  }
  r = await koan(['release', taken]);
  check(`${when}: koan release: gives it back`, r.code === 0, brief(r));
  if (user) await browserCall('Target.closeTarget', { targetId: user }).catch(() => {});
}

// Screenshots of the taken-over tab (2026-11-20, one capture path): every
// size as on the agent's tab, --clip there, and the page saw nothing.
async function userTabShots(when, taken) {
  const page = (await pages()).find(p => p.id === taken);
  const seen = async () => page && JSON.parse(await evalIn(page, 'JSON.stringify(window.koanSeen ?? null)') ??
                                              'null');
  const before = await seen();
  check(`${when}: the user's tab counts what it sees (pages/agent_capture_watch.html)`, !!before,
        JSON.stringify(before));
  if (!agentShots || !page) {
    skipped(`${when}: screenshots of the user's tab`, agentShots ? 'no page' : 'the agent tab\'s sizes failed');
    return;
  }
  const { want, pngSize, near, LINE, fitted } = agentShots;
  const view = JSON.parse(await evalIn(page, 'JSON.stringify({ w: visualViewport.width, h: visualViewport.height, ' +
      'dpr: devicePixelRatio, b: document.getElementById("press").getBoundingClientRect() })') ?? 'null');
  // The same window: the same viewport as the agent's tab, so the same sizes.
  const same = view && Math.abs(view.w - agentShots.w) <= 1 && Math.abs(view.h - agentShots.h) <= 1;
  const mine = view && {
    micro: fitted(view.w, view.h, Math.min(1, view.dpr), 320),
    mini: fitted(view.w, view.h, Math.min(1, view.dpr), 640),
    normal: fitted(view.w, view.h, Math.min(1, view.dpr), 1568),
    full: fitted(view.w, view.h, view.dpr, 2576) };
  for (const size of ['micro', 'mini', 'normal', 'full']) {
    const f = path.join(AGENT_HOME, `user-shot-${size}.png`);
    const r = await koan(['screenshot', taken, '--size', size, '-o', f]);
    const got = pngSize(f);
    const expected = same ? want[size] : mine[size];
    check(`${when}: koan screenshot --size ${size} of the user's tab: ${expected.join('×')}, as on the agent's tab`,
          r.code === 0 && near(got, expected) && LINE.test(r.out) && !/whole|device pixels/.test(r.err),
          `${brief(r)}; the PNG is ${got?.join('×')}`);
  }
  const { b } = view;
  // Rounded outwards to whole DIPs, as Koan copies (koan_agent_capture.cc).
  const left = Math.max(0, Math.floor(b.x - 8)), top = Math.max(0, Math.floor(b.y - 8));
  const cw = Math.min(view.w, Math.ceil(b.x + b.width + 8)) - left;
  const ch = Math.min(view.h, Math.ceil(b.y + b.height + 8)) - top;
  const clipped = path.join(AGENT_HOME, 'user-shot-clip.png');
  let r = await koan(['screenshot', taken, '--clip', 'loc:role=button[name="Press me"]', '-o', clipped]);
  check(`${when}: ...--clip with a locator on the user's tab: the button's box at device pixels ` +
        `(${Math.round(cw * view.dpr)}×${Math.round(ch * view.dpr)})`,
        r.code === 0 && near(pngSize(clipped), [cw * view.dpr, ch * view.dpr]), `${brief(r)}; ` +
        `${pngSize(clipped)?.join('×')}`);
  r = await koan(['snapshot', taken, '--interactive']);
  const ref = r.out.split('\n').find(l => /button "Press me"/.test(l))?.match(/@\d+/)?.[0];
  r = await koan(['screenshot', taken, '--clip', ref ?? '@0', '--size', 'normal', '-o', clipped]);
  check(`${when}: ...--clip ${ref} --size normal on the user's tab: CSS pixels`,
        r.code === 0 && near(pngSize(clipped), [cw, ch]), `${brief(r)}; ${pngSize(clipped)?.join('×')}`);
  const after = await seen();
  check(`${when}: ...and the user's page saw no resize, no devicePixelRatio change and no media query change`,
        !!after && after.resize === 0 && after.viewport === 0 && after.dpr === 0 && after.media === 0 &&
        after.dprAtLoad === view.dpr, JSON.stringify(after));
}

await takeOver('Defaults', false);

// --- End.
r = await koan(['end']);
check('koan end', r.code === 0, brief(r));
r = await koan(['status']);
check('koan status after end: no connection', /connection: none/.test(r.out), brief(r));
// --- Every switch off, the site limit on: a new task's tab is in a
// container (Your logins is read when a task starts).
for (const pref of ['logins', 'passwords', 'autofill', 'extensions', 'history']) {
  await setKoanPref(`koan.agent_control.use.${pref}`, false);
}
await setKoanPref('koan.agent_control.limit_take_over_to_site', true);
r = await koan(['open', AGENT_URL]);
const boxed = r.out.split('\n').at(-1);
check('Switches off: koan open: a new task\'s tab', r.code === 0 && /^[0-9A-Fa-f]{8,}/.test(boxed), brief(r));
await sleep(2500);
await extensionsAndPermissions('Switches off', boxed, false);
await takeOver('Switches off', true);
r = await koan(['end']);
check('Switches off: koan end', r.code === 0, brief(r));

// --- A koan that another program started (#340): no connection is open,
// so it starts one, whose chain lacks the agent that paired. Koan asks the
// user again (the pairing pins the agent), and koan says why while it waits
// instead of only "the user did not answer in time" after a minute.
{
  const out = path.join(AGENT_HOME, 'rechain.out');
  const printed = () => { try { return fs.readFileSync(out, 'utf8'); } catch { return ''; } };
  // The subshell outlives the shell that started it, so this test is not
  // among koan's ancestors.
  const orphan = spawn('/bin/sh', ['-c', '(sleep 1; "$0" tabs >"$1" 2>&1; echo "exit=$?" >>"$1") &', CLI, out],
                       { env, stdio: 'ignore' });
  await new Promise(res => orphan.on('close', res));
  const ok = await allow(/^A paired agent was started by a different program$/, printed);
  check('Another program\'s koan: koan says why Koan asks again, while it waits',
        /not by the agent that paired it/.test(printed()) && /approve it in Koan\.\.\./.test(printed()),
        printed().trim().split('\n')[0]);
  check('...and, allowed, its command runs', ok && !!await waitFor(() => /exit=0/.test(printed()), 15000),
        printed().trim().split('\n').at(-1));
  r = await koan(['end']);
  check('...koan end', r.code === 0, brief(r));
}

// --- A pairing whose launcher exits while its dialog is open (the end-user
// pass of 2026-11-11): `koan pair` started in the background by a shell that
// has gone by the time Allow is pressed. The browser pins the agent when the
// connection comes in; koan must record the same one, or its holder turns
// every later command of that agent away (agent-missing, which koan printed
// as "no answer").
{
  const out = path.join(AGENT_HOME, 'pair-bg.out');
  const printed = () => { try { return fs.readFileSync(out, 'utf8'); } catch { return ''; } };
  const launcher = spawn('/bin/sh', ['-c', '"$0" pair --label "koan suite bg" --capabilities observe,read >"$1" 2>&1 & sleep 2',
                                     CLI, out], { env, stdio: 'ignore' });
  await new Promise(res => launcher.on('close', res));
  const ok = await allow(/^Allow a program to control Koan\?$/, printed);
  const id = (await waitFor(() => /paired as ([0-9a-f]{32})/.exec(printed()), 15000))?.[1];
  check('A pairing whose launcher exited during the dialog: paired', ok && !!id, printed().trim().split('\n').at(-1));
  if (id) {
    r = await koan(['open', '--client', id, AGENT_URL]);
    check('...its agent\'s koan open works (the holder serves it)',
          r.code === 0 && /^[0-9A-Fa-f]{8,}/.test(r.out.split('\n').at(-1)), brief(r));
    // A koan with no agent above it (its own launcher gone) is turned away by
    // the holder, and says so instead of "no answer".
    const refusedOut = path.join(AGENT_HOME, 'tabs-orphan.out');
    // The subshell outlives the shell that started it, then becomes koan.
    const orphan = spawn('/bin/sh', ['-c', '(sleep 1; exec "$0" tabs --client "$1" >"$2" 2>&1) &',
                                     CLI, id, refusedOut], { env, stdio: 'ignore' });
    await new Promise(res => orphan.on('close', res));
    const said = await waitFor(() => {
      const text = fs.existsSync(refusedOut) ? fs.readFileSync(refusedOut, 'utf8') : '';
      return /koan: /.test(text) && text;
    }, 10000);
    check('...a koan its agent did not start is refused, and says why (Hold.refused)',
          /Hold\.refused/.test(said || '') && !/no answer/.test(said || ''), (said || '').trim());
    r = await koan(['end', '--client', id]);
    check('...koan end', r.code === 0, brief(r));
  }
}

// --- Settings > Agent Control > Set Up Agents, in the test's HOME (the
// browser runs with KOAN_AGENT_SETUP_HOME=AGENT_HOME; the user's ~/.claude
// is never touched): Claude Code and Codex are "installed", Cursor not.
{
  for (const dir of ['.claude', '.codex']) fs.mkdirSync(path.join(AGENT_HOME, dir), { recursive: true });
  const skillOf = dir => path.join(AGENT_HOME, dir, 'koan-browser', 'SKILL.md');
  const repoSkill = fs.readFileSync(path.join(process.env.KOAN_ROOT, 'skills', 'koan-browser', 'SKILL.md'), 'utf8');
  const targetId = (await browserCall('Target.createTarget',
      { url: 'chrome://settings/koan/agents', background: true })).result?.targetId;
  const settings = await waitFor(async () => (await pages()).find(p => p.id === targetId));
  await evalIn(settings, showPane('agents'));
  const button = sel => `koanSettingsRoot?.querySelector(${JSON.stringify(sel)})`;
  const shown = await waitFor(() => evalIn(settings, `${button('#agent-setup-button')}?.textContent ?? ''`), 15000);
  check('Set up agents: the button is in Settings > Agent Control', /^Set Up Agents$/.test(shown || ''), `${shown}`);
  check('...nothing is written before the click', !fs.existsSync(skillOf('.claude/skills')) &&
        !fs.existsSync(path.join(AGENT_HOME, '.local/bin/koan')));
  await evalIn(settings, `${button('#agent-setup-button')}.click()`);
  const said = await waitFor(() => evalIn(settings, `${button('#agent-setup-status')}?.textContent ?? ''`)
      .then(t => /Set up\.|Could|left/.test(t) && t), 15000);
  check('Set up agents: the click says it set up', said === 'Set up.', `${said}`);
  check('...Claude Code\'s skill is the repo\'s koan-browser skill',
        fs.existsSync(skillOf('.claude/skills')) && fs.readFileSync(skillOf('.claude/skills'), 'utf8') === repoSkill);
  check('...Codex\'s, in ~/.agents/skills', fs.existsSync(skillOf('.agents/skills')));
  check('...nothing for Cursor, which is not installed', !fs.existsSync(path.join(AGENT_HOME, '.cursor')));
  let linked = '';
  try { linked = fs.readlinkSync(path.join(AGENT_HOME, '.local/bin/koan')); } catch {}
  check('...~/.local/bin/koan links to the koan in the app', linked === CLI || (linked && fs.realpathSync(linked) === fs.realpathSync(CLI)),
        linked);
  const viaLink = linked && await new Promise(res => {
    const c = spawn(path.join(AGENT_HOME, '.local/bin/koan'), ['version'], { env });
    c.on('close', code => res(code === 0));
    c.on('error', () => res(false));
  });
  check('...and runs', !!viaLink);
  // Remove shows once the page has re-read what is set up (after "Set up.",
  // not with it): wait for it, then for all three to go.
  const remove = await waitFor(() => evalIn(settings, `!!${button('#agent-setup-remove')}`), 10000);
  check('Set up agents: a Remove button once set up', !!remove);
  await evalIn(settings, `${button('#agent-setup-remove')}?.click()`);
  await waitFor(() => !fs.existsSync(skillOf('.claude/skills')) && !fs.existsSync(skillOf('.agents/skills')) &&
                      !fs.existsSync(path.join(AGENT_HOME, '.local/bin/koan')), 10000);
  check('Set up agents: Remove takes the skills and the link out',
        !fs.existsSync(skillOf('.claude/skills')) && !fs.existsSync(skillOf('.agents/skills')) &&
        !fs.existsSync(path.join(AGENT_HOME, '.local/bin/koan')));
  if (targetId) await browserCall('Target.closeTarget', { targetId }).catch(() => {});
}

// `koan status` only checks that Koan answers (it connects and hangs up):
// the gateway must not take that for a program it could not find.
if (KOAN_LOG && fs.existsSync(KOAN_LOG)) {
  const warned = fs.readFileSync(KOAN_LOG, 'utf8').split('\n').filter(l => /no audit token for a connection/.test(l));
  check('koan status leaves no "no audit token" warning in Koan\'s log', warned.length === 0, warned[0]);
}
check('Koan is alive', await alive());
check('Nothing fatal in the log', !fatalInLog(), fatalInLog());
server.close();
finish();
