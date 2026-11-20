// User flow: an agent does a real task through `koan` (#401, #406). Test
// cases UF-AGT-* in .agent/notes/user-flows.md. The person pairs it (a real
// click); the agent signs up on a 3-page form with a custom dropdown
// (scripts/e2e/pages/uf_signup.html) in at most 3 koan calls (open, one
// batch with locators, --until, --interactive and a micro screenshot), and
// the flow counts the calls and the wall time against the old
// one-verb-a-call loop. Then the CLI's own behaviour an agent relies on:
// choose and the popup first in the snapshot, a validation error seen in
// --interactive, --diff and --scope, navigate's HTTP status (504, exit 0)
// and the --timeout, koan shell, and the person looking at the agent's tab.
import fs from 'node:fs';
import path from 'node:path';
import { AGENT_HOME, CLI, begin, brief, browserCall, end, evalIn, info, koan, lastLine, loc, openAnchor,
         pageById, pair, parkPointer, refOf, signups, skipFlow, sleep, step, tabIdOk } from './uf_lib.mjs';

const fixtures = await begin('uf_agent_task', 'An agent signs up on a 3-page form, and the CLI it relies on',
                             '#401 #406');
if (!CLI || !fs.existsSync(CLI)) skipFlow(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);
const anchor = await openAnchor(fixtures.url('/p?t=Your%20work'));
await parkPointer();
let tab = null;
const pngSize = f => {
  const b = fs.existsSync(f) ? fs.readFileSync(f) : Buffer.alloc(0);
  return b.subarray(1, 4).toString() === 'PNG' ? [b.readUInt32BE(16), b.readUInt32BE(20)] : null;
};
const SHOT_LINE = /^saved (.+) \((\d+)×(\d+), ~(\d+) image tokens, 1 px = ([\d.]+) CSS px\)$/m;

await step('UF-AGT-1', 'The person pairs the agent (a real click on Allow)', async t => {
  const { r, approval } = await pair(t, 'UF agent');
  t.check('paired with a real click on Allow', r.code === 0 && approval?.real, approval?.why ?? brief(r));
}, { needs: ['real input'] });

await step('UF-AGT-2', 'The agent signs up on the 3-page form in at most 3 koan calls', async t => {
  const calls0 = Date.now();
  let calls = 0;
  const run = async (args, opts) => { calls++; return koan(args, opts); };
  let r = await run(['open', fixtures.url('/signup/1'), '--task', 'signup']);
  tab = lastLine(r);
  t.check('koan open --task signup: a background tab', r.code === 0 && tabIdOk(tab), brief(r));
  const micro = path.join(AGENT_HOME, 'signup-micro.png');
  r = await run(['act', tab,
    'fill', loc('textbox', 'First name'), 'Ada', 'fill', loc('textbox', 'Last name'), 'Lovelace',
    'fill', loc('textbox', 'Email'), 'ada@example.com', 'choose', loc('combobox', 'Country'), 'United Kingdom',
    'click', loc('button', 'Next'),
    'choose', loc('combobox', 'Experience'), '3-5 years', 'fill', loc('textbox', 'City'), 'London',
    'click', loc('button', 'Next'),
    'click', loc('checkbox', 'I agree to the terms'), 'click', loc('button', 'Create account'),
    '--until', 'url~/signup/done', '--interactive', '--screenshot', 'micro', '-o', micro]);
  const ms = Date.now() - calls0;
  const sent = signups.at(-1) ?? {};
  t.check('one batch: every step a receipt, --until met', r.code === 0 && (r.out.match(/^\d+\/10 /gm) ?? []).length === 10 &&
          /^until url~\/signup\/done: met$/m.test(r.out), brief(r) + ' | ' + r.out.split('\n').slice(0, 12).join(' | '));
  t.check('the server got every field, the custom dropdown\'s choice too',
          sent.first === 'Ada' && sent.last === 'Lovelace' && sent.email === 'ada@example.com' &&
          sent.country === 'United Kingdom' && sent.experience === '3-5 years' && sent.city === 'London' &&
          sent.agree === 'on', JSON.stringify(sent));
  t.check('the batch\'s snapshot shows the last page', /heading "Welcome, Ada"/.test(r.out), r.out.slice(-300));
  const line = r.out.match(SHOT_LINE);
  const size = pngSize(micro);
  t.check('...and its micro screenshot, in the same answer (long edge 320, ~80-100 tokens)',
          !!line && !!size && Math.max(...size) <= 320 && +line[4] <= 140, `${line?.[0]}; ${size}`);
  t.check(`in at most 3 koan calls (${calls})`, calls <= 3, `${calls} calls`);
  info(`3-page signup, batch: ${calls} koan calls, ${ms} ms`);
});

await step('UF-AGT-3', 'The same form the old way (one verb a call), for the numbers', async t => {
  const t0 = Date.now();
  let calls = 0;
  const run = async args => { calls++; return koan(args); };
  await run(['act', tab, 'navigate', fixtures.url('/signup/1')]);
  let s = await run(['snapshot', tab]);
  for (const [re, verb, text] of [[/textbox "First name"/, 'fill', 'Grace'], [/textbox "Last name"/, 'fill', 'Hopper'],
                                  [/textbox "Email"/, 'fill', 'grace@example.com'], [/combobox "Country"/, 'select', 'India'],
                                  [/button "Next"/, 'click']]) {
    await run(['act', tab, verb, refOf(s.out, re) ?? '@0', ...(text ? [text] : [])]);
  }
  s = await run(['snapshot', tab]);
  await run(['act', tab, 'choose', refOf(s.out, /combobox "Experience"/) ?? '@0', '1-3 years']);
  await run(['act', tab, 'click', refOf(s.out, /button "Next"/) ?? '@0']);
  s = await run(['snapshot', tab]);
  await run(['act', tab, 'click', refOf(s.out, /checkbox "I agree/) ?? '@0']);
  await run(['act', tab, 'click', refOf(s.out, /button "Create account"/) ?? '@0']);
  s = await run(['snapshot', tab]);
  t.check('the old loop gets there too (a fair comparison)', /heading "Welcome, Grace"/.test(s.out), brief(s));
  info(`3-page signup, old loop: ${calls} koan calls, ${Date.now() - t0} ms`);
});

await step('UF-AGT-4', 'A custom dropdown: its list comes first in the snapshot; choose; a missing option', async t => {
  let r = await koan(['act', tab, 'navigate', fixtures.url('/signup/2?first=Ada'), '--snapshot', '--interactive']);
  const combo = refOf(r.out, /^\s*combobox "Experience"/);
  t.check('the snapshot has the custom combobox', r.code === 0 && !!combo, brief(r));
  r = await koan(['act', tab, 'click', combo ?? '@0', '--snapshot']);
  const lines = r.out.split('\n');
  t.check('opened, its listbox (a portal) is the first thing in the snapshot',
          /^popup of combobox "Experience" @\d+/.test(lines[1] ?? '') && lines.slice(1, 10).some(l => /option "5-10 years"/.test(l)),
          lines.slice(0, 4).join(' | '));
  const opt = refOf(r.out, /^\s*option "5-10 years"/);
  r = await koan(['act', tab, 'click', opt ?? '@0', '--snapshot', '--interactive']);
  const agentPage = await pageById(tab);
  const picked = agentPage && await evalIn(agentPage, 'document.getElementById("exp-value").value');
  t.check('the option takes on the first click (the page has it)', r.code === 0 && picked === '5-10 years',
          `${JSON.stringify(picked)}; ${brief(r)}`);
  r = await koan(['act', tab, 'choose', combo ?? '@0', 'Atlantis']);
  t.check('choose of an option it lacks: Koan.noOption, naming the options',
          r.code !== 0 && /Koan\.noOption/.test(r.err) && /"Less than 1 year"/.test(r.err), brief(r));
  await koan(['act', tab, 'press', 'Escape']);
});

await step('UF-AGT-5', 'A validation error shows in --interactive; --diff prints only the change; --scope', async t => {
  let r = await koan(['act', tab, 'navigate', fixtures.url('/signup/2'), '--snapshot', '--interactive']);
  const next = refOf(r.out, /button "Next"/), city = refOf(r.out, /textbox "City"/), combo = refOf(r.out, /combobox "Experience"/);
  r = await koan(['act', tab, 'click', next ?? '@0', '--interactive']);
  t.check('Next without a choice: the alert "Choose your experience" is in the interactive snapshot',
          /Choose your experience/.test(r.out), r.out.slice(0, 300));
  // The same mode as the last snapshot (--interactive): a diff against a
  // snapshot of another mode starts a new baseline, by design.
  r = await koan(['act', tab, 'fill', city ?? '@0', 'Paris', '--interactive', '--diff']);
  const body = r.out.split('\n').filter(l => /^[+~-] /.test(l));
  t.check('--diff after a fill prints only what changed', /^diff since the last snapshot/m.test(r.out) &&
          body.some(l => /^~ textbox "City" = "Paris"/.test(l)) && body.length <= 4, r.out.slice(0, 300));
  r = await koan(['snapshot', tab, '--scope', combo ?? '@0']);
  const scoped = r.out.split('\n').filter(l => l.trim());
  t.check('--scope @ref prints only that element', r.code === 0 && scoped.length <= 4 &&
          scoped.some(l => /combobox "Experience"/.test(l)) && !scoped.some(l => /textbox "City"/.test(l)), r.out);
  const full = await koan(['snapshot', tab]);
  const inter = await koan(['snapshot', tab, '--interactive']);
  info(`snapshot of signup/2: ${full.out.length} chars, --interactive ${inter.out.length}, --scope ${r.out.length}`);
  t.check('--interactive is no longer than the plain snapshot', inter.out.length <= full.out.length,
          `${inter.out.length} vs ${full.out.length}`);
});

await step('UF-AGT-6', 'navigate returns the server\'s status at once (504: exit 0), and --timeout', async t => {
  let t0 = Date.now();
  let r = await koan(['act', tab, 'navigate', fixtures.url('/504'), '--snapshot']);
  let ms = Date.now() - t0;
  t.check('a 504: "ok (HTTP 504 ...)", exit 0, the page in the snapshot, under 5 s',
          r.code === 0 && /^ok \(HTTP 504/.test(r.out) && /Upstream timed out/.test(r.out) && ms < 5000, `${ms} ms; ${brief(r)}`);
  t0 = Date.now();
  r = await koan(['act', tab, 'navigate', fixtures.url('/hang'), '--timeout', '3']);
  ms = Date.now() - t0;
  t.check('a server that never answers: Koan.timeout after --timeout 3 (3-8 s), not "no answer"',
          r.code !== 0 && /Koan\.timeout/.test(r.err) && !/no answer/.test(r.err) && ms >= 3000 && ms < 8000,
          `${ms} ms; ${brief(r)}`);
  t0 = Date.now();
  r = await koan(['act', tab, 'navigate', fixtures.url('/p?t=Fetch')]);
  t.check('a normal page after it works', r.code === 0, brief(r));
});

await step('UF-AGT-7', 'koan shell: many commands over one connection, faster per command', async t => {
  const N = 8;
  let t0 = Date.now();
  for (let i = 0; i < N; i++) await koan(['snapshot', tab, '--interactive']);
  const separate = (Date.now() - t0) / N;
  const lines = [`koan tabs`, `# a note`, JSON.stringify({ id: 'j1', args: ['act', tab, 'press', 'Tab'] }),
                 ...Array.from({ length: N }, () => `snapshot ${tab} --interactive`)];
  t0 = Date.now();
  const r = await koan(['shell'], { input: lines.join('\n') + '\n' });
  const together = (Date.now() - t0) / (N + 2);
  const exits = r.out.match(/^--- exit \d+$/gm) ?? [];
  t.check(`every plain line answered "--- exit 0" (${N + 1})`, exits.length === N + 1 && exits.every(e => e === '--- exit 0'),
          `${exits.length}: ${r.out.slice(0, 200)}`);
  const json = r.out.split('\n').find(l => l.startsWith('{"id":"j1"'));
  let parsed = null;
  try { parsed = JSON.parse(json ?? ''); } catch {}
  t.check('the JSON line got one JSON answer', parsed?.exit === 0, json ?? 'none');
  info(`per command: separate ${separate.toFixed(0)} ms, koan shell ${together.toFixed(0)} ms`);
  t.check('a command in the shell costs no more than a separate one', together <= separate * 1.1,
          `${together.toFixed(0)} vs ${separate.toFixed(0)} ms`);
});

await step('UF-AGT-8', 'The person looks at the agent\'s tab; the agent ends the task', async t => {
  await koan(['act', tab, 'navigate', fixtures.url('/signup/3?first=Ada&last=Lovelace&email=ada%40example.com')]);
  const shown = await browserCall('Target.activateTarget', { targetId: tab });
  t.check('the agent\'s tab can be shown', !shown.error, shown.error?.message);
  await sleep(1200);
  await t.shot('agent-tab');
  await browserCall('Target.activateTarget', { targetId: anchor.id });
  const r = await koan(['end', '--task', 'signup']);
  t.check('koan end --task signup', r.code === 0, brief(r));
  await sleep(800);
  const left = (await koan(['tabs'])).out;
  t.check('its tab is gone', !left.includes(tab.slice(0, 8)), left.slice(0, 200));
  await koan(['end']);
});

end();
