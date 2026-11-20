// End-to-end test of `koan autofill` (.agent/notes/agent-control.md section
// 7.0, "Autofill for agents") and of the agent never reading what it fills
// (section 4.5), driven by the `koan` CLI the way an agent uses it, against
// the suite's test profile and a throw-away HOME. Test-only data, made in the
// test profile through chrome://settings' own private APIs (an address, a
// local card, a saved login for the fixture server); never the user's data or
// profile. Local fixture pages only.
// - an address: --list shows it as a label, --pick fills the form, and the
//   snapshot shows the address (addresses are no secret);
// - a login: --list says "(saved password)" and never the password; the
//   pick fills it, the snapshot shows "•••• (filled)", act's click submits,
//   and the fixture server receives the password;
// - script is refused in a frame with a filled password (Koan.secretField),
//   over koan-relay's raw CDP (Runtime.evaluate, Runtime.callFunctionOn);
// - a card: with mandatory reauth off it fills, reads "•••• (filled)", and
//   the screenshot says its fields are hidden; with it on (as a user who
//   asked for Touch ID before filling) the pick waits for the user:
//   Koan.userNeeded autofill-verify, the pill "koan suite needs you to
//   verify a card", nothing filled;
// - a form that posts to another site: Koan.offSite;
// - every reply the agent got (koan's output, every relay message) is
//   grepped for the password and the card number, raw, URL-encoded and in
//   base64: none has them.
// local_state/agent_autofill.json turns Agent ready on. macOS only (the CLI's
// holder needs the Mac's peer resolver). Run by scripts/mac/suite.sh;
// scripts/mac/steps/agent_autofill.md has the steps a person checks.
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { browserCall, check, evalIn, finish, pages, sleep, viewTree,
         waitFor } from './lib/koan.mjs';
import { AGENT_HOME, CLI, PROFILE, allow, allowWhy, brief,
         koan as koanRaw } from './lib/agent.mjs';
import { IS_MAC, alive, fatalInLog, skip, skipped } from './lib/mac.mjs';

if (!IS_MAC) skip('agent control\'s CLI needs macOS (its holder resolves peers with LOCAL_PEERTOKEN)');
if (!CLI || !fs.existsSync(CLI)) skip(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);
if (!PROFILE || !AGENT_HOME) skip('KOAN_PROFILE and KOAN_AGENT_HOME are set by scripts/mac/suite.sh');

// Test-only secrets: a made-up password, Visa's public test number.
const PASSWORD = 'k0an-Test-Pw!42';
const CARD = '4111111111111111';

// Everything the agent was told, for the grep at the end.
const replies = [];
const koan = async (args, options) => {
  const r = await koanRaw(args, options);
  replies.push(r.out, r.err);
  return r;
};

// The fixture: an address form, a card form, a login form that posts to
// /signed-in (the body is kept), and one that posts to another site
// (localhost is another site than 127.0.0.1).
const posted = [];
const routes = {
  '/address': `<!doctype html><title>Address</title><form>
    <label>Name <input id=name autocomplete=name></label>
    <label>Street <input id=street autocomplete=address-line1></label>
    <label>City <input id=city autocomplete=address-level2></label>
    <label>ZIP <input id=zip autocomplete=postal-code></label></form>`,
  '/card': `<!doctype html><title>Card</title><form>
    <label>Name on card <input id=ccname autocomplete=cc-name></label>
    <label>Card number <input id=number autocomplete=cc-number></label>
    <label>Expiry <input id=exp autocomplete=cc-exp></label></form>`,
  '/login': `<!doctype html><title>Sign in</title>
    <form method=post action=/signed-in>
    <label>Email <input id=user name=user autocomplete=username></label>
    <label>Password <input id=pass name=pass type=password
           autocomplete=current-password></label>
    <button>Sign in</button></form>`,
  '/elsewhere': port => `<!doctype html><title>Elsewhere</title>
    <form method=post action="http://localhost:${port}/collect">
    <label>Email <input id=user name=user autocomplete=username></label>
    <label>Password <input id=pass name=pass type=password></label>
    <button>Sign in</button></form>`,
};
const server = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  let body = '';
  req.on('data', d => (body += d));
  req.on('end', () => {
    if (req.method === 'POST') posted.push({ path: u.pathname, body });
    const route = routes[u.pathname];
    const html = typeof route === 'function' ? route(server.address().port)
        : route ?? `<!doctype html><title>Done</title><h1>Signed in</h1>`;
    res.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' });
    res.end(html);
  });
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const url = p => `http://127.0.0.1:${server.address().port}${p}`;

// --- Test data, through chrome://settings' own APIs (as its Add buttons).
async function settingsTab() {
  const targetId = (await browserCall('Target.createTarget',
      { url: 'chrome://settings/addresses', background: true })).result?.targetId;
  return waitFor(async () => (await pages()).find(p => p.id === targetId), 10000);
}
const settings = await settingsTab();
const call = expr => evalIn(settings, `new Promise(resolve => { try { ${expr} } catch (e) {
    resolve('error: ' + e.message); } })`);
const made = {
  address: await call(`chrome.autofillPrivate.saveAddress({fields: [
      {type: 'NAME_FULL', value: 'Ada Lovelace'},
      {type: 'ADDRESS_HOME_LINE1', value: '12 Main St'},
      {type: 'ADDRESS_HOME_CITY', value: 'Springfield'},
      {type: 'ADDRESS_HOME_ZIP', value: '62704'},
      {type: 'ADDRESS_HOME_COUNTRY', value: 'US'}]}); resolve('ok');`),
  card: await call(`chrome.autofillPrivate.saveCreditCard({name: 'Ada Lovelace',
      cardNumber: '${CARD}', expirationMonth: '08', expirationYear: '2029'});
      resolve('ok');`),
  login: await call(`chrome.passwordsPrivate.addPassword({url: '${url('/login')}',
      username: 'ada@example.com', password: '${PASSWORD}', note: '',
      useAccountStore: false}, () => resolve(chrome.runtime.lastError ?
      'error: ' + chrome.runtime.lastError.message : 'ok'));`),
};
const setReauth = on => call(`chrome.settingsPrivate.setPref(
    'autofill.payment_methods_mandatory_reauth', ${on}, '', ok => resolve(ok));`);
check('test data: an address, a card and a login made in the test profile',
      made.address === 'ok' && made.card === 'ok' && made.login === 'ok', JSON.stringify(made));
await sleep(1500);

// --- Pair.
await waitFor(() => fs.existsSync(path.join(PROFILE, 'AgentControl', 'agent.json')), 15000);
let r = await koan(['pair', '--label', 'koan suite', '--capabilities', 'observe,read,drive'],
                   { timeout: 90000, whileRunning: async printed => {
                     if (!await allow(/^Allow a program to control Koan\?$/, printed)) {
                       console.log(`  (${allowWhy()})`);
                     }
                   } });
check('koan pair: paired', r.code === 0 && /paired as/.test(r.out), brief(r));
if (r.code !== 0) {
  skipped('the rest of agent_autofill', 'not paired');
  server.close();
  finish();
}

const refOf = (text, re) => text.split('\n').find(l => re.test(l))?.match(/@(\d+)/)?.[0];
async function openTab(p) {
  const opened = await koan(['open', url(p), '--task', 'autofill']);
  const tab = opened.out.split('\n').pop();
  await sleep(1200);
  return tab;
}
const snapshot = tab => koan(['snapshot', tab, '--interactive']).then(s => s.out);

// --- An address.
{
  const tab = await openTab('/address');
  const name = refOf(await snapshot(tab), /textbox "Name"/);
  const listed = await koan(['autofill', tab, name, '--list']);
  check('address: --list shows the saved address as a numbered label',
        listed.code === 0 && /^1\. .*Ada Lovelace.*\[address\]/m.test(listed.out), brief(listed));
  const picked = await koan(['autofill', tab, name, '--pick', '1']);
  check('address: --pick 1 fills through Chrome\'s autofill', picked.code === 0 &&
        /ok: filled address/.test(picked.out), brief(picked));
  const after = await snapshot(tab);
  check('...the snapshot shows the address (no secret)', /12 Main St/.test(after) && /Springfield/.test(after),
        after.split('\n').filter(l => /textbox/.test(l)).join(' | '));
}

// --- A login: fills, reads "•••• (filled)", submits, the server gets it.
{
  const tab = await openTab('/login');
  let snap = await snapshot(tab);
  const user = refOf(snap, /textbox "Email"/);
  const listed = await waitFor(async () => {
    const l = await koan(['autofill', tab, user, '--list']);
    return l.code === 0 && l;
  }, 10000);
  check('login: --list shows "(saved password)" and the username',
        listed && /ada@example\.com \(saved password\)/.test(listed.out), listed ? brief(listed) : 'none');
  const picked = await koan(['act', tab, 'autofill', user, 'ada@example.com']);
  check('login: act\'s autofill step fills it', picked.code === 0 && /filled password/.test(picked.out),
        brief(picked));
  snap = await snapshot(tab);
  const pass = snap.split('\n').find(l => /textbox "Password"/.test(l)) ?? '';
  check('...the agent sees the password field as "•••• (filled)"', /•••• \(filled\)/.test(pass), pass);
  const button = refOf(snap, /button "Sign in"/);
  const sent = await koan(['act', tab, 'click', button, '--until', 'text~Signed in']);
  check('...act\'s click submits it', sent.code === 0, brief(sent));
  const body = posted.find(p => p.path === '/signed-in')?.body ?? '';
  check('...the fixture server received the password (the page sent it)',
        new URLSearchParams(body).get('pass') === PASSWORD, body.replace(/pass=[^&]*/, 'pass=…'));
}

// --- Script is refused while the password is filled (koan-relay's raw CDP).
const relay = await (async () => {
  const bin = fs.mkdtempSync(path.join(AGENT_HOME, 'ab-'));
  const urlFile = path.join(bin, 'cdp');
  fs.writeFileSync(path.join(bin, 'agent-browser'),
                   `#!/bin/sh\numask 077\nprintf %s "$AGENT_BROWSER_CDP" > '${urlFile}'\n`, { mode: 0o700 });
  let relayAllowed = false;
  const a = await koan(['agent-browser', '--session', 'autofill', 'get', 'url'], {
    timeout: 90000, extraEnv: { PATH: `${bin}:${process.env.PATH}` },
    whileRunning: async printed => { relayAllowed = await allow(/^Allow .*control Koan\?$/, printed); } });
  const wsUrl = fs.existsSync(urlFile) ? fs.readFileSync(urlFile, 'utf8').trim() : '';
  fs.rmSync(urlFile, { force: true });
  check('koan-relay: started (stand-in agent-browser)', a.code === 0 && /^ws:\/\//.test(wsUrl),
        `${relayAllowed ? '' : allowWhy()}; ${brief(a)}`);
  if (!wsUrl) return null;
  const ws = new WebSocket(wsUrl);
  if (!await new Promise(res => { ws.onopen = () => res(true); ws.onerror = () => res(false); })) return null;
  let nextId = 1;
  const waiting = new Map();
  ws.onmessage = e => {
    replies.push(String(e.data));
    const m = JSON.parse(e.data);
    if (m.id && waiting.has(m.id)) { waiting.get(m.id)(m); waiting.delete(m.id); }
  };
  const send = (method, params = {}, sessionId) => new Promise(resolve => {
    const id = nextId++;
    waiting.set(id, resolve);
    ws.send(JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) }));
    setTimeout(() => waiting.has(id) && (waiting.delete(id), resolve({ error: { message: 'timeout' } })), 30000);
  });
  return { ws, send };
})();
if (relay) {
  const created = await relay.send('Target.createTarget', { url: url('/login'), background: true });
  const attached = await relay.send('Target.attachToTarget',
                                    { targetId: created.result?.targetId, flatten: true });
  const session = attached.result?.sessionId;
  await sleep(1500);
  const doc = await relay.send('DOM.getDocument', {}, session);
  const userNode = await relay.send('DOM.querySelector',
                                    { nodeId: doc.result?.root?.nodeId, selector: '#user' }, session);
  const described = await relay.send('DOM.describeNode', { nodeId: userNode.result?.nodeId }, session);
  const node = described.result?.node?.backendNodeId;
  const filled = await waitFor(async () => {
    const f = await relay.send('Koan.autofill', { backendNodeId: node, pick: 1 }, session);
    return f.result?.filled && f;
  }, 10000);
  check('relay: Koan.autofill fills the login', !!filled, JSON.stringify(filled ?? {}).slice(0, 200));
  const evaluated = await relay.send('Runtime.evaluate',
      { expression: 'document.getElementById("pass").value', returnByValue: true }, session);
  check('relay: Runtime.evaluate in the frame is refused (Koan.secretField)',
        evaluated.error?.data === 'Koan.secretField', JSON.stringify(evaluated).slice(0, 200));
  const resolved = await relay.send('DOM.resolveNode', { backendNodeId: node }, session);
  const called = await relay.send('Runtime.callFunctionOn', {
      objectId: resolved.result?.object?.objectId,
      functionDeclaration: 'function() { return this.form.pass.value; }', returnByValue: true }, session);
  check('relay: Runtime.callFunctionOn is refused too', called.error?.data === 'Koan.secretField',
        JSON.stringify(called).slice(0, 200));
  const tree = await relay.send('Accessibility.getFullAXTree', {}, session);
  check('relay: the accessibility tree shows the password field as filled, not its value',
        JSON.stringify(tree).includes('•••• (filled)') && !JSON.stringify(tree).includes(PASSWORD));
  // The page moves on: script runs again.
  await relay.send('Page.navigate', { url: url('/done') }, session);
  await sleep(1500);
  const again = await relay.send('Runtime.evaluate', { expression: '1 + 1', returnByValue: true }, session);
  check('relay: after the page moves on, script runs again', again.result?.result?.value === 2,
        JSON.stringify(again).slice(0, 160));
  relay.ws.close();
} else {
  skipped('Runtime.evaluate refusal over koan-relay', 'no relay');
}

// --- A card: fills and is hidden, then (mandatory reauth on) asks the user.
{
  await setReauth(false);
  const tab = await openTab('/card');
  const number = refOf(await snapshot(tab), /textbox "Card number"/);
  const listed = await koan(['autofill', tab, number, '--list']);
  check('card: --list shows the card by its last four digits only',
        listed.code === 0 && /1111/.test(listed.out) && !listed.out.includes(CARD), brief(listed));
  const picked = await koan(['autofill', tab, number, '--pick', '1']);
  check('card: --pick fills it (mandatory reauth off)', picked.code === 0 && /filled card/.test(picked.out),
        brief(picked));
  const snap = await snapshot(tab);
  const line = snap.split('\n').find(l => /textbox "Card number"/.test(l)) ?? '';
  check('...its number reads "•••• (filled)"', /•••• \(filled\)/.test(line) && !snap.includes(CARD), line);
  const shot = await koan(['screenshot', tab, '--size', 'mini', '-o', path.join(AGENT_HOME, 'card.png')]);
  check('...the screenshot says the card fields are hidden in it',
        shot.code === 0 && /card fields Koan filled are hidden/.test(shot.err), brief(shot));

  const reauth = await setReauth(true);
  const tab2 = await openTab('/card');
  const number2 = refOf(await snapshot(tab2), /textbox "Card number"/);
  const verify = await koan(['autofill', tab2, number2, '--pick', '1']);
  check('card with mandatory reauth: the pick waits for the user (Koan.userNeeded autofill-verify)',
        verify.code !== 0 && /Koan\.userNeeded: .*verify this card/.test(verify.err + verify.out),
        `${reauth}; ${brief(verify)}`);
  const status = await koan(['status']);
  check('...koan status shows what waits for the user', /autofill-verify/.test(status.out), brief(status));
  const vs = await viewTree();
  check('...the pill says "koan suite needs you to verify a card"',
        vs.some(v => v.shown && /koan suite needs you to verify a card/.test(v.props.Text ?? '')));
  const after = await snapshot(tab2);
  check('...nothing was filled', /textbox "Card number"/.test(after) && /•••• \(empty\)/.test(after),
        after.split('\n').find(l => /Card number/.test(l)) ?? '');
  await setReauth(false);
}

// --- A form that sends the login to another site.
{
  const tab = await openTab('/elsewhere');
  const user = refOf(await snapshot(tab), /textbox "Email"/);
  const picked = await koan(['autofill', tab, user, '--pick', '1']);
  check('a form posting to another site: Koan.offSite', /Koan\.offSite/.test(picked.err + picked.out),
        brief(picked));
}

// --- Nothing the agent got carries a secret.
{
  const forms = s => [s, encodeURIComponent(s), Buffer.from(s).toString('base64').slice(0, 12)];
  const all = replies.join('\n');
  const found = [...forms(PASSWORD), ...forms(CARD)].filter(f => all.includes(f));
  check('no reply the agent got has the password or the card number (raw, URL-encoded, base64)',
        found.length === 0, found.length ? `found ${found.length} form(s)` : `${replies.length} replies`);
}

await koan(['end']);
await browserCall('Target.closeTarget', { targetId: settings.id });
server.close();
check('Koan is alive, nothing fatal in its log', await alive() && !fatalInLog());
finish();
