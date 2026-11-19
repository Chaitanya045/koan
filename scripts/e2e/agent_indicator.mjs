// End-to-end test of agent control's layered indicator (#180,
// .agent/notes/agent-control.md section 6.2), driven by the `koan` CLI as an
// agent would (see agent_cli.mjs for the pairing):
// - connected, on a tab no agent holds: the sidebar's chip ("koan suite"),
//   no pill, no border, no corner dot;
// - a take-over of the user's tab: on that tab the pill "… is controlling
//   this tab" with Take back and Stop, and the accent border round the page;
//   the sidebar chip stays;
// - another tab shown: no pill and no border there, the chip; back on the
//   taken-over tab, the pill again;
// - compact mode hiding the sidebar, on a tab no agent holds: the corner dot
//   stands in for the chip; off again, the chip is back and the dot gone;
// - release: the pill goes, the chip stays;
// - the agent's own tab shown: the pill with Take over (not Take back);
// - koan end: chip, pill and dot all gone, and the "disconnected" toast.
// local_state/agent_indicator.json turns Agent ready on. macOS only. Run by
// scripts/mac/suite.sh.
import fs from 'node:fs';
import path from 'node:path';
import { browserCall, check, finish, setKoanPref, sleep, viewTree, waitFor } from './lib/koan.mjs';
import { AGENT_HOME, CLI, PROFILE, allow, allowWhy, brief, koan } from './lib/agent.mjs';
import { IS_MAC, alive, fatalInLog, skip, skipped, startServer } from './lib/mac.mjs';

if (!IS_MAC) skip('agent control\'s CLI needs macOS (its holder resolves peers with LOCAL_PEERTOKEN)');
if (!CLI || !fs.existsSync(CLI)) skip(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);
if (!PROFILE || !AGENT_HOME) skip('KOAN_PROFILE and KOAN_AGENT_HOME are set by scripts/mac/suite.sh');

const server = await startServer({});

// What the indicator shows now. Read once a change has settled: the
// indicator makes widgets as tabs change, and a widget made while a UI
// DevTools session starts crashes the browser on macOS (see lib/agent.mjs).
async function indicator(settle = 1500) {
  await sleep(settle);
  const vs = await viewTree();
  const shown = cls => vs.filter(v => v.cls === cls && v.shown);
  const pill = shown('KoanAgentIndicatorView')[0];
  const pillText = pill ? vs.filter(v => v.widget === pill.widget && v.shown && v.cls === 'Label')
                            .map(v => v.props.Text).join(' ') : '';
  const pillButtons = pill ? vs.filter(v => v.widget === pill.widget && v.shown && v.cls === 'LabelButton')
                               .map(v => v.props.Text) : [];
  const chip = shown('KoanAgentChipButton')[0];
  const chipText = chip ? vs.filter(v => v.widget === chip.widget && v.shown && v.cls === 'Label' &&
                                         v.path.includes('KoanAgentChipButton'))
                            .map(v => v.props.Text).join(' ') : '';
  return {
    pill: !!pill, pillText, pillButtons,
    border: shown('KoanAgentBorderView').length > 0,
    chip: !!chip && chip.w > 0, chipText,
    corner: shown('KoanAgentCornerDot').length > 0,
    toasts: vs.filter(v => v.shown && v.cls === 'Label' && /connected|disconnected/.test(v.props.Text ?? ''))
              .map(v => v.props.Text),
  };
}
const say = s => JSON.stringify({ ...s, toasts: undefined });

async function newUserTab(title) {
  const id = (await browserCall('Target.createTarget', { url: server.url(`/p?t=${encodeURIComponent(title)}`) }))
      .result?.targetId;
  await sleep(1500);
  return id;
}
async function show(targetId) {
  await browserCall('Target.activateTarget', { targetId });
}

// --- Pair, then connect (koan's holder keeps the connection between
// commands).
await waitFor(() => fs.existsSync(path.join(PROFILE, 'AgentControl', 'agent.json')), 15000);
let r = await koan(['pair', '--label', 'koan suite', '--capabilities', 'observe,read,drive'],
                   { timeout: 90000, whileRunning: async printed => {
                     if (!await allow(/^Allow a program to control Koan\?$/, printed)) {
                       console.log(`  (${allowWhy()})`);
                     }
                   } });
check('koan pair: paired', r.code === 0 && /paired as/.test(r.out), brief(r));
if (r.code !== 0) {
  skipped('the rest of the indicator', 'not paired');
  server.close();
  finish();
}

const plain = await newUserTab('Plain tab');
await show(plain);
await koan(['status']);
let s = await indicator(2500);
check('Connected, on a tab no agent holds: the sidebar chip names the agent',
      s.chip && /koan suite/.test(s.chipText), say(s));
check('...and no pill, no border, no corner dot there', !s.pill && !s.border && !s.corner, say(s));

// --- Take-over: the pill on that tab only.
const user = await newUserTab('User tab');
await show(user);
await sleep(800);
let ok = false;
r = await koan(['take-over'], { timeout: 90000, whileRunning: async printed => {
  ok = await allow(/ wants to control a tab$/, printed);
} });
const taken = r.out.split('\n').at(-1);
check('koan take-over: allowed', ok && r.code === 0 && !!taken, brief(r));
s = await indicator();
check('Taken-over tab shown: the pill says the agent controls this tab',
      s.pill && /koan suite is controlling this tab/.test(s.pillText), say(s));
check('...with Take back and Stop', s.pillButtons.includes('Take back') && s.pillButtons.includes('Stop'),
      say(s));
check('...and the accent border round the page', s.border, say(s));
check('...the sidebar chip stays, no corner dot', s.chip && !s.corner, say(s));

await show(plain);
s = await indicator();
check('Another tab shown: no pill and no border there (the user\'s complaint)', !s.pill && !s.border, say(s));
check('...the chip shows instead', s.chip && /koan suite/.test(s.chipText), say(s));

await show(user);
s = await indicator();
check('Back on the taken-over tab: the pill again', s.pill && /controlling this tab/.test(s.pillText), say(s));

// --- Compact mode hiding the sidebar: the corner dot (T10 in every layout).
// (setKoanPref() opens and closes a Settings tab: show the tab after it.)
await setKoanPref('koan.compact_mode.hide', 'sidebar');
await setKoanPref('koan.compact_mode.enabled', true);
await show(plain);
s = await indicator(2500);
check('Compact mode, sidebar hidden, on a tab no agent holds: the corner dot', s.corner, say(s));
check('...and no pill', !s.pill, say(s));
await show(user);
s = await indicator();
check('Compact mode, on the taken-over tab: the pill (no corner dot)', s.pill && !s.corner, say(s));
await setKoanPref('koan.compact_mode.enabled', false);
await show(plain);
s = await indicator(2500);
check('Compact mode off: the chip is back, the corner dot gone', s.chip && !s.corner, say(s));

// --- Release: the pill goes, the chip stays.
await show(user);
r = await koan(['release', taken]);
check('koan release', r.code === 0, brief(r));
s = await indicator();
check('Released, on that tab: no pill, no border, the chip', !s.pill && !s.border && s.chip, say(s));

// --- The agent's own tab, shown: the pill offers Take over.
r = await koan(['open', server.url('/p?t=Agent%20tab')]);
const agentTab = r.out.split('\n').at(-1);
check('koan open: an agent tab', r.code === 0 && /^[0-9A-Fa-f]{8,}/.test(agentTab), brief(r));
await sleep(1500);
const activated = await browserCall('Target.activateTarget', { targetId: agentTab });
s = await indicator();
if (activated.error) {
  skipped('the agent tab\'s pill', `Target.activateTarget: ${activated.error.message}`);
} else {
  check('Agent tab shown: the pill says the agent controls this tab',
        s.pill && /controlling this tab/.test(s.pillText), say(s));
  check('...with Take over, not Take back',
        s.pillButtons.includes('Take over') && !s.pillButtons.includes('Take back'), say(s));
}
await show(plain);

// --- End: nothing left, and the toast says so.
r = await koan(['end']);
check('koan end', r.code === 0, brief(r));
s = await indicator(800);
check('Disconnected: no chip, pill or corner dot', !s.chip && !s.pill && !s.corner, say(s));
check('...and the toast says the agent disconnected',
      s.toasts.some(t => /koan suite disconnected/.test(t)), JSON.stringify(s.toasts));

for (const id of [plain, user]) await browserCall('Target.closeTarget', { targetId: id }).catch(() => {});
server.close();
check('Koan is alive', await alive());
check('Nothing fatal in the log', !fatalInLog(), fatalInLog());
finish();
