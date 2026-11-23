// User flow: an agent attaches a file the person gave it (#420, `koan upload`
// and act's upload step). Test cases UF-UP-* in .agent/notes/user-flows.md.
// The person watches an agent fill a job application in its own tab: the
// agent attaches the resume through the page's styled "Upload resume"
// button (a hidden <input type=file>), the page shows what it received (its
// own read of the name, size and SHA-256), Koan says what the agent attached,
// and no open panel shows. Then the person takes the tab over: the agent's
// next upload is refused (paused) until the person hands it back.
// scripts/e2e/agent_upload.mjs checks every chooser kind and refusal; this
// is the person's view of it, recorded.
import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { AGENT_HOME, CLI, begin, brief, clickElement, clickPill, end, evalIn, goToAgentTab, indicator, koan,
         lastLine, openAnchor, pageById, pages, pair, parkPointer, recalibrate, refOf, sayIndicator, skipFlow, sleep,
         step, tabIdOk, viewTree, waitFor } from './uf_lib.mjs';

const fixtures = await begin('uf_upload', 'An agent attaches the file it was given', '#420');
if (!CLI || !fs.existsSync(CLI)) skipFlow(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);

// The file the person gave the agent, in the agent's HOME.
const dir = fs.mkdtempSync(path.join(AGENT_HOME, 'files-'));
const content = '%PDF-1.4\n% Ada Lovelace, analyst. Resume for the user flow.\n%%EOF\n';
const resume = path.join(dir, 'resume.pdf');
fs.writeFileSync(resume, content);
const sha = crypto.createHash('sha256').update(content).digest('hex');

// A careers page: a styled button over a hidden input, and what the page
// received, as it reads it.
const html = `<!doctype html><title>Apply: Analyst</title>
<body style="font:18px -apple-system,sans-serif;margin:48px;max-width:640px">
<h1>Analyst, Engines team</h1>
<label>Full name <input id=name style="font:inherit;padding:8px" aria-label="Full name"></label>
<p><input id=f type=file hidden accept=".pdf,.doc,.docx">
<button id=b style="font:inherit;padding:12px 20px;border-radius:10px;border:0;background:#111;color:#fff"
        onclick="document.getElementById('f').click()">Upload resume</button>
<span id=out role=status style="margin-left:12px;color:#0a7">No file yet</span></p>
<script>
  window.got = null;
  const hex = b => [...new Uint8Array(b)].map(x => x.toString(16).padStart(2, '0')).join('');
  document.getElementById('f').addEventListener('change', async e => {
    const f = e.target.files[0];
    window.got = { name: f.name, size: f.size,
                   sha: hex(await crypto.subtle.digest('SHA-256', await f.arrayBuffer())) };
    document.getElementById('out').textContent = 'Attached: ' + f.name + ' (' + f.size + ' bytes)';
  });
</script>`;
const server = http.createServer((req, res) => {
  res.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' });
  res.end(html);
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const formUrl = `http://127.0.0.1:${server.address().port}/apply`;

const anchor = await openAnchor(fixtures.url('/p?t=Your%20tab'));
await parkPointer();
let tab = null;
const received = async () => {
  const p = await waitFor(async () => (await pages()).find(x => x.id === tab), 4000);
  return p ? JSON.parse(await evalIn(p, 'JSON.stringify(window.got)') ?? 'null') : null;
};

await step('UF-UP-1', 'The agent pairs and opens the application in a tab of its own', async t => {
  const { r, approval } = await pair(t, 'UF agent');
  t.check('paired with a real click on Allow', r.code === 0 && approval?.real, approval?.why ?? brief(r));
  const o = await koan(['open', formUrl, '--task', 'apply']);
  tab = lastLine(o);
  t.check('koan open: its tab', o.code === 0 && tabIdOk(tab), brief(o));
  await sleep(1500);
  const g = await goToAgentTab('Apply: Analyst');
  t.check('the person goes to it from the sidebar chip (real clicks)', g.ok, `${g.why}; ${JSON.stringify(g.items)}`);
  await parkPointer();
  await t.shot('the-form');
}, { needs: ['real input'] });

await step('UF-UP-2', 'The agent fills the name and attaches the resume through the styled button', async t => {
  const snap = await koan(['snapshot', tab, '--interactive']);
  const name = refOf(snap.out, /"Full name"/);
  const button = refOf(snap.out, /button "Upload resume"/);
  t.check('the outline has the name field and the button', !!name && !!button, snap.out.slice(0, 300));
  const r = await koan(['act', tab, 'fill', name, 'Ada Lovelace', 'upload', button, resume, '--snapshot']);
  t.check('act: fill, then upload @button resume.pdf: "attached resume.pdf"',
          r.code === 0 && /attached resume\.pdf/.test(r.out), brief(r));
  t.check('...its --snapshot shows the page saying what it got', /Attached: resume\.pdf/.test(r.out),
          r.out.split('\n').slice(-4).join(' | '));
  const s = await indicator(400);
  const said = s.vs.filter(v => v.shown && /attached/.test(v.props.Text ?? '')).map(v => v.props.Text);
  t.check('Koan tells the person: "UF agent attached resume.pdf" (pill or toast)',
          said.some(x => /UF agent attached resume\.pdf/.test(x)), `${JSON.stringify(said)}; ${sayIndicator(s)}`);
  await t.shot('attached');
  const got = await received();
  t.check('the page read the very file: name, size and SHA-256',
          got?.name === 'resume.pdf' && got?.size === Buffer.byteLength(content) && got?.sha === sha,
          JSON.stringify(got));
  const vs = await viewTree();
  t.check('no open panel or sheet in Koan\'s window', !vs.some(v => v.shown && /NSOpenPanel|SelectFileDialog/.test(v.cls)),
          'a file dialog view is shown');
}, { needs: ['real input', 'screen recording'] });

await step('UF-UP-3', 'The person takes the tab over: the agent can\'t attach until it\'s handed back', async t => {
  const p = await pageById(tab);
  await recalibrate(p);
  const c = await clickElement(p, 'document.querySelector("h1")', 400);
  t.check('a real click in the agent\'s page pauses it', c.ok, c.why);
  const snap = await koan(['snapshot', tab, '--interactive']);
  const button = refOf(snap.out, /button "Upload resume"/);
  let r = await koan(['act', tab, 'upload', button ?? '@1', resume]);
  t.check('the agent\'s upload is refused while the person has the tab (Koan.paused)',
          r.code !== 0 && /Koan\.paused/.test(r.err + r.out), brief(r));
  await t.shot('paused');
  const h = await clickPill('Hand back');
  t.check('a real click on Hand back', h.ok, h.why);
  r = await koan(['act', tab, 'upload', button ?? '@1', resume]);
  t.check('handed back: the agent attaches again', r.code === 0 && /attached resume\.pdf/.test(r.out), brief(r));
  r = await koan(['end']);
  t.check('koan end', r.code === 0, brief(r));
}, { needs: ['real input'] });

server.close();
end();
