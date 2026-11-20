// Measures what screenshot sizes cost an agent and whether small ones still
// get the work done (2026-11-20; .agent/notes/agent-control.md section 12,
// "Screenshot sizes"). The same three flows run three ways, through the
// `koan` CLI as an agent runs it, on local pages:
//   full    a screenshot at device pixels after each check (what
//           `koan screenshot` gave before sizes);
//   normal  a normal (CSS pixel) screenshot after each check, the new
//           default;
//   micro   the guide's way: act --screenshot micro in the same call, the
//           text snapshot for every word and number, --clip for the chart.
// Flows: a sign-up form (success: the server got the values), a report
// page with small print and a price (read from the snapshot) and a canvas
// chart (which only an image shows: success needs at least one image pixel
// per CSS pixel over it), and a state check (an error banner, then a
// dialog, which the snapshot after the click must show).
// For each run it records success, the number of koan calls, the image
// tokens (estimated from each PNG's size: w×h/784, at most 4,784 each) and
// the time. No model is called: this measures what the CLI returns. Prints
// a table, a RESULT line (JSON), and writes the JSON to $KOAN_BENCH_OUT if
// set. Fails if a flow fails in any mode, or if micro does not cost less
// than normal and normal less than full.
// Run on the Mac (not in the default list):
//   scripts/mac/suite.sh --skip-cxx --skip-visual --all-e2e --only agent_shot_bench <Koan.app>
// local_state/agent_shot_bench.json turns Agent ready on for the profile.
import fs from 'node:fs';
import path from 'node:path';
import { check, finish, waitFor } from './lib/koan.mjs';
import { AGENT_HOME, CLI, PROFILE, allow, allowWhy, brief, koan } from './lib/agent.mjs';
import { IS_MAC, alive, fatalInLog, skip, startServer } from './lib/mac.mjs';

if (!IS_MAC) skip('agent control\'s CLI needs macOS (its holder resolves peers with LOCAL_PEERTOKEN)');
if (!CLI || !fs.existsSync(CLI)) skip(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);
if (!PROFILE || !AGENT_HOME) skip('KOAN_PROFILE and KOAN_AGENT_HOME are set by scripts/mac/suite.sh');

const submitted = [];
const server = await startServer({
  '/form': `<!doctype html><title>Sign up</title><h1>Sign up</h1>
    <form action=/done method=get>
      <label>Plan <select name=plan><option value=free>Free</option>
        <option value=pro>Pro</option><option value=team>Team</option></select></label>
      <label>Name <input name=who></label>
      <button>Send</button>
    </form>`,
  '/done': req => {
    submitted.push(new URL(req.url, 'http://x').search);
    return '<!doctype html><title>Done</title><h1>Thanks</h1>';
  },
  '/report': fs.readFileSync(new URL('./pages/agent_shots.html', import.meta.url), 'utf8'),
});

await waitFor(() => fs.existsSync(path.join(PROFILE, 'AgentControl', 'agent.json')), 15000);
let r = await koan(['pair', '--label', 'koan bench'], { timeout: 90000, whileRunning: async printed => {
  if (!await allow(/^Allow a program to control Koan\?$/, printed)) console.log(`  (${allowWhy()})`);
} });
check('koan pair: paired', r.code === 0 && /paired as/.test(r.out), brief(r));
if (r.code !== 0) {
  server.close();
  finish();
}

const tokensOf = (w, h) => Math.min(4784, Math.max(1, Math.ceil(w * h / 784)));
const pngSize = f => {
  const b = fs.existsSync(f) ? fs.readFileSync(f) : Buffer.alloc(0);
  return b.subarray(1, 4).toString() === 'PNG' ? [b.readUInt32BE(16), b.readUInt32BE(20)] : null;
};
const LINE = /^saved (.+) \((\d+)×(\d+), ~\d+ image tokens, 1 px = ([\d.]+) CSS px\)$/m;

// One run of the flows in one mode: koan() counted, images summed.
async function run(mode) {
  const stats = { mode, calls: 0, images: 0, tokens: 0, ms: 0, flows: {} };
  const t0 = Date.now();
  let shots = 0;
  const call = async args => {
    stats.calls++;
    const res = await koan(args);
    const m = res.out.match(LINE);
    if (m) {
      const size = pngSize(m[1]);
      if (size) {
        stats.images++;
        stats.tokens += tokensOf(...size);
      }
      res.shot = { path: m[1], size, cssPerPx: +m[4] };
    }
    return res;
  };
  const file = () => path.join(AGENT_HOME, `bench-${mode}-${++shots}.png`);
  // A check's look at the page: in the act call (micro), or one more call.
  const looked = async (tab, actArgs) => {
    if (mode === 'micro') return call(['act', tab, ...actArgs, '--screenshot', 'micro', '-o', file()]);
    const res = await call(['act', tab, ...actArgs]);
    await call(['screenshot', tab, '--size', mode, '-o', file()]);
    return res;
  };
  const open = async url => (await call(['open', url])).out.split('\n').at(-1);

  // 1. The form.
  {
    const before = submitted.length;
    const tab = await open(server.url('/form'));
    await call(['snapshot', tab, '--interactive']);
    const res = await looked(tab, ['choose', 'loc:role=combobox[name="Plan"]', 'Pro',
                                   'fill', 'loc:role=textbox[name="Name"]', 'Ada',
                                   'click', 'loc:role=button[name="Send"]',
                                   '--until', 'url~/done', '--interactive', '--diff']);
    const sent = submitted.slice(before).at(-1) ?? '';
    stats.flows.form = res.code === 0 && /plan=pro/.test(sent) && /who=Ada/.test(sent) && /Thanks/.test(res.out);
  }
  // 2. The report: words and numbers from the snapshot, the chart from an
  // image with at least one pixel per CSS pixel over it.
  const report = await open(server.url('/report'));
  {
    const snap = await call(['snapshot', report]);
    const text = ['$1,234.56', '$3.20', 'KX-4471-B'].every(t => snap.out.includes(t));
    let chart;
    if (mode === 'micro') {
      await call(['screenshot', report, '--size', 'micro', '-o', file()]);
      // By its ref: the snapshot gives a named image one
      // (`image "Sales by quarter" @N`), as an agent would use it.
      const line = snap.out.split('\n').find(l => /"Sales by quarter"/.test(l));
      const ref = line?.match(/@\d+/)?.[0];
      chart = ref ? await call(['screenshot', report, '--clip', ref, '-o', file()])
                  : { code: 1, out: `no ref on the chart's line: ${line ?? 'none'}` };
    } else {
      chart = await call(['screenshot', report, '--size', mode, '-o', file()]);
    }
    stats.flows.report = text && !!chart?.shot && chart.shot.cssPerPx <= 1;
  }
  // 3. State: an error after a click, then a dialog.
  {
    const paid = await looked(report, ['click', 'loc:role=button[name="Pay now"]', '--snapshot', '--interactive',
                                       '--diff']);
    const deleting = await looked(report, ['click', 'loc:role=button[name="Delete report"]', '--snapshot',
                                           '--interactive']);
    stats.flows.state = paid.code === 0 && /Payment failed: card declined/.test(paid.out) &&
                        deleting.code === 0 && /dialog "Delete this report\?"/.test(deleting.out);
  }
  await call(['end']);
  stats.ms = Date.now() - t0;
  return stats;
}

const results = [];
for (const mode of ['full', 'normal', 'micro']) results.push(await run(mode));
console.log('INFO  mode     calls  images  image tokens  time    form  report  state');
for (const s of results) {
  console.log(`INFO  ${s.mode.padEnd(8)} ${String(s.calls).padStart(5)}  ${String(s.images).padStart(6)}  ` +
              `${String(s.tokens).padStart(12)}  ${(s.ms / 1000).toFixed(1).padStart(5)}s  ` +
              `${s.flows.form ? 'ok' : 'FAIL'}${' '.repeat(s.flows.form ? 4 : 2)}${s.flows.report ? 'ok' : 'FAIL'}` +
              `${' '.repeat(s.flows.report ? 6 : 4)}${s.flows.state ? 'ok' : 'FAIL'}`);
}
console.log(`RESULT ${JSON.stringify(results)}`);
if (process.env.KOAN_BENCH_OUT) fs.writeFileSync(process.env.KOAN_BENCH_OUT, JSON.stringify(results, null, 2));

for (const s of results) {
  for (const [flow, ok] of Object.entries(s.flows)) check(`${s.mode}: the ${flow} flow succeeds`, ok);
}
const by = Object.fromEntries(results.map(s => [s.mode, s]));
check('micro costs fewer image tokens than normal, and normal fewer than full',
      by.micro.tokens < by.normal.tokens && by.normal.tokens < by.full.tokens,
      `micro ${by.micro.tokens}, normal ${by.normal.tokens}, full ${by.full.tokens}`);
check('micro takes no more koan calls than normal', by.micro.calls <= by.normal.calls,
      `micro ${by.micro.calls}, normal ${by.normal.calls}`);
check('Koan is alive', await alive());
check('Nothing fatal in the log', !fatalInLog(), fatalInLog());
server.close();
finish();
