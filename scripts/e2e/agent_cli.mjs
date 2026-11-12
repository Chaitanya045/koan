// End-to-end test of agent control (#180) driven by the `koan` CLI itself
// (Koan.app/Contents/Resources/bin/koan), the way an agent uses it, against
// the suite's test profile (KOAN_USER_DATA_DIR) and a throw-away HOME (the
// CLI keeps its keys in $HOME/.koan; the user's are never read):
// - pair: `koan pair` asks Koan; this test presses Allow in Koan's approval
//   dialog through UI DevTools, as the user would with the pointer (there is
//   no auto-approve switch, and none is added);
// - open, snapshot (an outline with @refs), act (click lands), screenshot
//   (a PNG), on a local page, in the agent's own background tab;
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
// - take-over of the user's tab (approved in its dialog the same way): a
//   same-site navigate works, and so does an off-site one (no limit); then
//   release;
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
import fs from 'node:fs';
import https from 'node:https';
import os from 'node:os';
import path from 'node:path';
import { browserCall, check, evalIn, finish, pages, setKoanPref, sleep, viewTree,
         waitFor } from './lib/koan.mjs';
import { IS_MAC, KOAN_LOG, alive, fatalInLog, rootMouse, rootOf, skip, skipped,
         startServer } from './lib/mac.mjs';

if (!IS_MAC) skip('agent control\'s CLI needs macOS (its holder resolves peers with LOCAL_PEERTOKEN)');
const CLI = process.env.KOAN_CLI;
const PROFILE = process.env.KOAN_PROFILE;
const AGENT_HOME = process.env.KOAN_AGENT_HOME;
if (!CLI || !fs.existsSync(CLI)) skip(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);
if (!PROFILE || !AGENT_HOME) skip('KOAN_PROFILE and KOAN_AGENT_HOME are set by scripts/mac/suite.sh');
const env = { ...process.env, HOME: AGENT_HOME, KOAN_USER_DATA_DIR: PROFILE };

// Runs `koan args...`; resolves {code, out, err}. `whileRunning` runs once
// the command started (e.g. to answer a dialog it waits for), with a
// function that returns what the command printed so far.
function koan(args, { timeout = 60000, whileRunning } = {}) {
  return new Promise(resolve => {
    const child = spawn(CLI, args, { env, stdio: ['ignore', 'pipe', 'pipe'] });
    let out = '', err = '';
    child.stdout.on('data', d => (out += d));
    child.stderr.on('data', d => (err += d));
    const timer = setTimeout(() => child.kill('SIGTERM'), timeout);
    child.on('close', code => {
      clearTimeout(timer);
      resolve({ code, out: out.trim(), err: err.trim() });
    });
    whileRunning?.(() => out + err).catch(e => console.log(`  (${e.message})`));
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
async function allow(title, printed) {
  if (!await waitFor(() => / in Koan\.\.\./.test(printed()), 30000)) return false;
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
  // Dialogs ignore a click right after they show (InputEventActivationProtector).
  await sleep(1500);
  const vs = await viewTree();
  const button = vs.find(v => v.nodeId === found.button.nodeId) ?? found.button;
  const root = rootOf({ views: vs.filter(v => v.widget === button.widget) });
  await rootMouse(root, ['mousePressed', 'mouseReleased'], button.ax + button.w / 2,
                  button.ay + button.h / 2);
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

// --- Agent ready, then pairing.
await waitFor(() => fs.existsSync(path.join(PROFILE, 'AgentControl', 'agent.json')), 15000);
let r = await koan(['status']);
check('koan status: Koan running, Agent ready', /running, Agent ready/.test(r.out), brief(r));
let allowed = false;
r = await koan(['pair', '--label', 'koan suite', '--capabilities', 'observe,read,drive'],
               { timeout: 90000, whileRunning: async printed => {
                 allowed = await allow(/^Allow a program to control Koan\?$/, printed);
               } });
check('koan pair: the approval dialog showed and was allowed', allowed);
check('koan pair: paired', r.code === 0 && /paired as/.test(r.out), brief(r));
if (r.code !== 0) {
  skipped('the rest of agent control', 'not paired');
  server.close();
  finish();
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
  const user = (await browserCall('Target.createTarget', { url: server.url('/p?t=User%20tab') })).result?.targetId;
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
