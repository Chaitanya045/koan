// End-to-end test of `koan upload` and act's `upload` step
// (.agent/notes/agent-control.md section 9.4, "Uploads"; #420), driven by the
// `koan` CLI the way an agent uses it, against the suite's test profile and a
// throw-away HOME. Local fixture pages and files made in the run's HOME only.
// - a plain <input type=file>: `act upload @ref` clicks it, Koan answers the
//   chooser, the page gets the file (its name, size and SHA-256, read back by
//   the page itself), and no open panel (sheet) shows;
// - the same in two commands: a click, then `koan upload`;
// - a `multiple` + `accept` input: two files; a file of a type it doesn't
//   accept (Koan.notAccepted); two files for a single input
//   (Koan.tooManyFiles);
// - a hidden input behind a styled button, and a drop zone that opens a
//   chooser on click;
// - a folder input (webkitdirectory): Koan.notSupported;
// - a missing, a folder and an unreadable path: clear errors, and nothing is
//   clicked;
// - a tab the agent doesn't drive (the user's own): refused;
// - (with real input) a tab the user took over: Koan.paused;
// - the activity log has each file's name and size, never its contents.
// local_state/agent_upload.json turns Agent ready on. macOS only (the CLI's
// holder needs the Mac's peer resolver). Run by scripts/mac/suite.sh
// (suite/e2e.list); scripts/mac/steps/agent_upload.md has the steps a person
// checks.
import { execFileSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { centerOf, browserCall, check, connect, evalIn, finish, pages, sleep, viewTree,
         waitFor } from './lib/koan.mjs';
import { AGENT_HOME, CLI, PROFILE, allow, allowWhy, brief, koan } from './lib/agent.mjs';
import { IS_MAC, KOAN_PID, alive, fatalInLog, goToAgentTab, realClick, realInput, realInputWhyNot,
         skip, skipped, startServer } from './lib/mac.mjs';

if (!IS_MAC) skip('agent control\'s CLI needs macOS (its holder resolves peers with LOCAL_PEERTOKEN)');
if (!CLI || !fs.existsSync(CLI)) skip(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);
if (!PROFILE || !AGENT_HOME) skip('KOAN_PROFILE and KOAN_AGENT_HOME are set by scripts/mac/suite.sh');

// --- The files the agent was given (in the run's HOME, not a protected
// folder: no macOS folder prompt).
const DIR = fs.mkdtempSync(path.join(AGENT_HOME, 'upload-'));
const make = (name, content) => {
  const p = path.join(DIR, name);
  fs.writeFileSync(p, content);
  return { path: p, name, size: Buffer.byteLength(content),
           sha: crypto.createHash('sha256').update(content).digest('hex') };
};
const RESUME = make('resume.pdf', '%PDF-1.4\n% koan upload test: Ada Lovelace, analyst\n%%EOF\n');
const NOTES = make('notes.txt', 'Notes for the application.\n'.repeat(40));
const PIC = make('pic.png', Buffer.from('89504e470d0a1a0a0000000d49484452', 'hex'));
const DOCX = make('cv.docx', 'not really a docx');
const LOCKED = make('locked.pdf', 'secret');
fs.chmodSync(LOCKED.path, 0);
const FOLDER = path.join(DIR, 'folder');
fs.mkdirSync(FOLDER);
// A line of this file's contents must never reach the log.
const CONTENT_MARK = 'koan upload test: Ada Lovelace';

// --- The fixture pages. Each records what its input received, read by the
// page itself: each file's name, size and SHA-256, and how many change
// events fired; and how often it was clicked.
const report = `<output id=out></output><script>
  window.got = null; window.changes = 0; window.clicks = 0;
  const hex = b => [...new Uint8Array(b)].map(x => x.toString(16).padStart(2, '0')).join('');
  const input = document.getElementById('f');
  input.addEventListener('click', () => window.clicks++);
  input.addEventListener('change', async () => {
    window.changes++;
    const out = [];
    for (const f of input.files) {
      out.push({ name: f.name, size: f.size, sha: hex(await crypto.subtle.digest('SHA-256', await f.arrayBuffer())) });
    }
    window.got = out;
    document.getElementById('out').textContent = 'received ' + out.map(f => f.name).join(', ');
  });
</script>`;
const server = await startServer({
  '/plain': `<!doctype html><title>Plain</title><h1>Apply</h1>
    <label>Resume <input id=f type=file aria-label="Resume"></label>${report}`,
  '/multi': `<!doctype html><title>Multi</title><h1>Attachments</h1>
    <input id=f type=file multiple accept=".txt,image/png" aria-label="Attachments">${report}`,
  '/hidden': `<!doctype html><title>Hidden</title><h1>Apply</h1>
    <input id=f type=file hidden accept=".pdf,.doc,.docx">
    <button id=b style="padding:12px 20px;border-radius:8px;background:#222;color:#fff"
            onclick="document.getElementById('f').click()">Upload resume</button>${report}`,
  '/drop': `<!doctype html><title>Drop</title><h1>Documents</h1>
    <input id=f type=file hidden>
    <div id=zone role=button tabindex=0 aria-label="Drop files here or browse"
         style="width:320px;height:120px;border:2px dashed #888;display:grid;place-items:center"
         onclick="document.getElementById('f').click()"
         ondragover="event.preventDefault()">Drop files here or browse</div>${report}`,
  '/folder': `<!doctype html><title>Folder</title><h1>Folder</h1>
    <input id=f type=file webkitdirectory aria-label="Project folder">${report}`,
});

// No open panel: no sheet on any of Koan's windows, and no window named Open
// (System Events; null where Accessibility can't read Koan's windows).
function panels() {
  if (!KOAN_PID) return null;
  try {
    const out = execFileSync('osascript', ['-l', 'JavaScript', '-e',
        `const p = Application('System Events').applicationProcesses.whose({ unixId: ${KOAN_PID} })[0];
         JSON.stringify(p.windows().map(w => ({ name: w.name(), sheets: w.sheets().length })))`],
        { encoding: 'utf8', timeout: 10000 });
    const windows = JSON.parse(out);
    return windows.filter(w => w.sheets > 0 || /^Open$/.test(w.name ?? '')).length;
  } catch {
    return null;
  }
}
const checkNoPanel = what => {
  const shown = panels();
  if (shown === null) skipped(`${what}: no open panel`, 'Accessibility cannot read Koan\'s windows');
  else check(`${what}: no open panel (sheet) shows`, shown === 0, `${shown} panel(s)`);
};

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
  skipped('the rest of agent_upload', 'not paired');
  server.close();
  finish();
}

// The control's line, never a heading's: the pages' headings can share a
// control's name ("Attachments").
const refOf = (text, re) => text.split('\n').find(l => re.test(l) && !/^\s*heading /.test(l))
    ?.match(/@(\d+)/)?.[0];
// The whole of koan's answer, for a failing check.
const full = r => `exit ${r.code}; ${(r.out + ' | ' + r.err).replace(/\s+/g, ' ').slice(0, 600)}`;
async function openTab(p) {
  const opened = await koan(['open', server.url(p), '--task', 'upload']);
  const tab = opened.out.split('\n').pop();
  await sleep(1200);
  return tab;
}
const snapshot = tab => koan(['snapshot', tab, '--interactive']).then(s => s.out);
const pageOf = async id => waitFor(async () => (await pages()).find(p => p.id === id), 5000);
// What the page received, as it read it.
const received = async tab => evalIn(await pageOf(tab), 'JSON.stringify({ got: window.got, ' +
    'changes: window.changes, clicks: window.clicks })').then(s => JSON.parse(s ?? '{}'));
const same = (got, files) => Array.isArray(got) && got.length === files.length &&
    files.every((f, i) => got[i].name === f.name && got[i].size === f.size && got[i].sha === f.sha);
const say = page => JSON.stringify(page).slice(0, 240);

// --- A plain input: act's upload step clicks it and answers the chooser.
const plain = await openTab('/plain');
{
  const ref = refOf(await snapshot(plain), /"Resume"/);
  r = await koan(['act', plain, 'upload', ref, RESUME.path]);
  check('plain input: act upload @ref <path> attaches it',
        r.code === 0 && /ok: attached resume\.pdf \(\d+ B\)/.test(r.out), brief(r));
  const page = await waitFor(async () => { const p = await received(plain); return p.got && p; }, 5000);
  check('...the page got the file: its name, size and SHA-256 as the page reads them',
        page && same(page.got, [RESUME]) && page.changes === 1, say(page));
  checkNoPanel('plain input');
  const vs = await viewTree();
  check('...the window says "koan suite attached resume.pdf" (the toast, or the pill)',
        vs.some(v => v.shown && /koan suite attached resume\.pdf/.test(v.props.Text ?? '')));
}

// --- The same in two commands: a click, then koan upload.
{
  await evalIn(await pageOf(plain), 'window.got = null; window.changes = 0');
  const ref = refOf(await snapshot(plain), /"Resume"/);
  const clicked = await koan(['act', plain, 'click', ref]);
  check('a click on the input: act says the page waits with a file chooser, and how to answer it',
        /file chooser/.test(clicked.out + clicked.err) && /koan upload/.test(clicked.out + clicked.err),
        brief(clicked));
  const status = await koan(['status']);
  check('...koan status shows the file-picker waiting', /file-picker/.test(status.out), brief(status));
  r = await koan(['upload', plain, path.relative(process.cwd(), RESUME.path)]);
  check('...koan upload <tab> <path> (relative) answers it', r.code === 0 && /attached resume\.pdf/.test(r.out),
        brief(r));
  const page = await waitFor(async () => { const p = await received(plain); return p.got && p; }, 5000);
  check('...the page got it', page && same(page.got, [RESUME]), say(page));
  r = await koan(['upload', plain, RESUME.path]);
  check('...koan upload with no chooser open: Koan.noFileChooser after its wait',
        r.code !== 0 && /Koan\.noFileChooser/.test(r.err + r.out), brief(r));
  const ref2 = refOf(await snapshot(plain), /"Resume"/);
  r = await koan(['act', plain, 'upload', ref2, RESUME.path, NOTES.path]);
  check('two files for a single input: Koan.tooManyFiles', r.code !== 0 && /Koan\.tooManyFiles/.test(r.err + r.out),
        brief(r));
  // That chooser is still the page's: answered now.
  await koan(['upload', plain, RESUME.path]);
}

// --- multiple + accept.
{
  const tab = await openTab('/multi');
  const ref = refOf(await snapshot(tab), /"Attachments"/);
  r = await koan(['act', tab, 'upload', ref, NOTES.path, PIC.path]);
  check('multiple + accept: two files of accepted types', r.code === 0 &&
        /attached notes\.txt .*pic\.png/.test(r.out), full(r));
  let page = await waitFor(async () => { const p = await received(tab); return p.got && p; }, 5000);
  check('...the page got both, in order', page && same(page.got, [NOTES, PIC]), say(page));
  await evalIn(await pageOf(tab), 'window.got = null');
  r = await koan(['act', tab, 'upload', refOf(await snapshot(tab), /"Attachments"/), DOCX.path]);
  check('a .docx for accept=".txt,image/png": Koan.notAccepted, naming the file and the list',
        r.code !== 0 && /Koan\.notAccepted/.test(r.err + r.out) && /cv\.docx/.test(r.err + r.out), brief(r));
  page = await received(tab);
  check('...the page got nothing', !page.got, say(page));
  await koan(['upload', tab, NOTES.path]);
}

// --- A hidden input behind a styled button.
{
  const tab = await openTab('/hidden');
  const ref = refOf(await snapshot(tab), /button "Upload resume"/);
  r = await koan(['act', tab, 'upload', ref, RESUME.path, '--snapshot']);
  check('hidden input behind a button: act upload on the button attaches it',
        r.code === 0 && /attached resume\.pdf/.test(r.out), brief(r));
  check('...--snapshot shows the page after (it says what it received)',
        /received resume\.pdf/.test(r.out), r.out.split('\n').slice(-3).join(' | '));
  const page = await waitFor(async () => { const p = await received(tab); return p.got && p; }, 5000);
  check('...the page got it', page && same(page.got, [RESUME]), say(page));
  checkNoPanel('hidden input');
}

// --- A drop zone that opens a chooser when clicked, in a longer batch.
{
  const tab = await openTab('/drop');
  const ref = refOf(await snapshot(tab), /"Drop files here or browse"/);
  r = await koan(['act', tab, 'scroll', '50', 'upload', ref, NOTES.path, 'scroll', '-50']);
  check('drop zone: act upload on it as the 2nd of 3 steps', r.code === 0 &&
        /2\/3 upload .*: ok: attached notes\.txt/.test(r.out) && /3\/3/.test(r.out), full(r));
  const page = await waitFor(async () => { const p = await received(tab); return p.got && p; }, 5000);
  check('...the page got it', page && same(page.got, [NOTES]), say(page));
}

// --- A folder input.
{
  const tab = await openTab('/folder');
  r = await koan(['act', tab, 'upload', refOf(await snapshot(tab), /"Project folder"/), NOTES.path]);
  check('webkitdirectory: Koan.notSupported (folders not yet)', r.code !== 0 &&
        /Koan\.notSupported/.test(r.err + r.out), brief(r));
}

// --- Paths that can't be uploaded: nothing is clicked.
{
  await evalIn(await pageOf(plain), 'window.got = null; window.clicks = 0');
  const ref = refOf(await snapshot(plain), /"Resume"/);
  for (const [what, file, code] of [['missing', path.join(DIR, 'nope.pdf'), 'fileNotFound'],
                                    ['a folder', FOLDER, 'notAFile'],
                                    ['unreadable', LOCKED.path, 'fileUnreadable']]) {
    r = await koan(['act', plain, 'upload', ref, file]);
    check(`${what} path: Koan.${code}, naming it`, r.code !== 0 &&
          new RegExp(`Koan\\.${code}`).test(r.err + r.out) && (r.err + r.out).includes(path.basename(file)),
          brief(r));
    r = await koan(['upload', plain, file]);
    check(`...and from koan upload too`, r.code !== 0 && new RegExp(`Koan\\.${code}`).test(r.err + r.out),
          brief(r));
  }
  const page = await received(plain);
  check('...the input was never clicked, and got nothing', page.clicks === 0 && !page.got, say(page));
}

// --- A tab the agent doesn't drive: the user's own.
{
  const user = (await browserCall('Target.createTarget', { url: server.url('/plain'), background: true }))
      .result?.targetId;
  await sleep(1500);
  r = await koan(['upload', user, RESUME.path]);
  check('the user\'s own tab: refused (not the agent\'s)', r.code !== 0 && !/attached/.test(r.out), brief(r));
  const page = await received(user);
  check('...its page got nothing', !page.got, say(page));
  await browserCall('Target.closeTarget', { targetId: user });
}

// --- A tab the user took over: Koan.paused (real clicks: Go to, Take over).
if (!realInput()) {
  skipped('upload on a taken-over tab', `needs real input (Go to, Take over): ${realInputWhyNot()}`);
} else {
  const from = (await browserCall('Target.createTarget', { url: server.url('/p?t=Start') })).result?.targetId;
  await sleep(1500);
  // The plain tab's title is "Plain".
  const g = await goToAgentTab('Plain', await pageOf(from));
  const taken = g.ok && await clickPillButton('Take over');
  if (!taken) {
    skipped('upload on a taken-over tab', `could not take it over (${g.why ?? 'no Take over button'})`);
  } else {
    await sleep(800);
    r = await koan(['upload', plain, RESUME.path]);
    check('a tab the user took over: Koan.paused, nothing attached', r.code !== 0 &&
          /Koan\.paused/.test(r.err + r.out), brief(r));
    await clickPillButton('Hand back');
  }
  await browserCall('Target.closeTarget', { targetId: from });
}

// --- The activity log: names and sizes, never contents.
{
  const logDir = path.join(PROFILE, 'AgentControl', 'Log');
  const lines = fs.existsSync(logDir) ? fs.readdirSync(logDir).filter(f => f.endsWith('.jsonl'))
      .flatMap(f => fs.readFileSync(path.join(logDir, f), 'utf8').split('\n')) : [];
  const uploads = lines.filter(l => l.includes('"Koan.upload"'));
  check('the activity log has each upload with the files\' names and sizes',
        uploads.some(l => /resume\.pdf \(\d+ B\)/.test(l) && /"outcome":"ok"/.test(l)) &&
        uploads.some(l => /notes\.txt \(.*\), pic\.png/.test(l)), `${uploads.length} upload line(s)`);
  check('...and refusals as denied', uploads.some(l => /"outcome":"denied"/.test(l)));
  check('...never a file\'s contents', !lines.some(l => l.includes(CONTENT_MARK)));
}

// A real click on the pill's button labelled `text` (as agent_indicator does).
async function clickPillButton(text) {
  const vs = await viewTree();
  const button = vs.find(v => v.shown && v.cls === 'LabelButton' && v.props.Text === text &&
                              v.wname === 'KoanAgentIndicator');
  if (!button) return false;
  const ui = await connect(`ws://127.0.0.1:${process.env.UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  let widget = null;
  const walk = n => {
    const a = n.attributes ?? [];
    if (!widget && n.nodeName === 'Widget' && a[a.indexOf('name') + 1] === 'KoanAgentIndicator') widget = n;
    for (const c of n.children ?? []) walk(c);
  };
  walk(doc);
  let at = null;
  if (widget) {
    const m = await ui.send('CSS.getMatchedStylesForNode', { nodeId: widget.nodeId });
    const p = {};
    for (const rule of m.result?.matchedCSSRules ?? []) {
      for (const prop of rule.rule.style.cssProperties) p[prop.name.toLowerCase()] = prop.value;
    }
    at = { x: +p.x, y: +p.y };
  }
  ui.close();
  if (!at) return false;
  const c = centerOf(button);
  return realClick(at.x + c.x, at.y + c.y, 900);
}

await koan(['end']);
fs.chmodSync(LOCKED.path, 0o600);
server.close();
check('Koan is alive, nothing fatal in its log', await alive() && !fatalInLog());
finish();
