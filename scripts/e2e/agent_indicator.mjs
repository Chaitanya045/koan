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
// - auto-resume (design section 9.2, user decision 2026-11-20), with real
//   input (CGEvent clicks and keys, never UI DevTools clicks: the real-click
//   rule): a real click in the agent's page pauses it; switching to another
//   tab gives Koan.resumed {reason: "user-idle"} after about 5 s and `koan
//   wait --resumed` returns with a snapshot; staying idle on the tab shows
//   "continues in Ns" in the pill, then resumes, and the old refs are stale;
//   a real click on Keep paused means no resume until a real click on Hand
//   back; focus in a password field means no resume;
// - koan end: chip, pill and dot all gone, and the "disconnected" toast.
// local_state/agent_indicator.json turns Agent ready on. macOS only. Run by
// scripts/mac/suite.sh.
import fs from 'node:fs';
import path from 'node:path';
import { browserCall, check, connect, evalIn, finish, pages, setKoanPref, sleep, viewTree,
         waitFor } from './lib/koan.mjs';
import { AGENT_HOME, CLI, PROFILE, allow, allowWhy, brief, koan } from './lib/agent.mjs';
import { IS_MAC, alive, calibrate, fatalInLog, makeKey, realClick, realInput, realInputWhyNot,
         realKey, selectTabRow, skip, skipped, startServer } from './lib/mac.mjs';

if (!IS_MAC) skip('agent control\'s CLI needs macOS (its holder resolves peers with LOCAL_PEERTOKEN)');
if (!CLI || !fs.existsSync(CLI)) skip(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);
if (!PROFILE || !AGENT_HOME) skip('KOAN_PROFILE and KOAN_AGENT_HOME are set by scripts/mac/suite.sh');

// The auto-resume page: a text field, a password field and a button.
const server = await startServer({
  '/form': `<!doctype html><title>Form</title><body style="margin:0">
    <h1>Form</h1>
    <input id="name" aria-label="Name" style="position:absolute;left:40px;top:120px;width:300px;height:40px">
    <input id="pass" type="password" aria-label="Password"
           style="position:absolute;left:40px;top:200px;width:300px;height:40px">
    <button id="go" style="position:absolute;left:40px;top:280px">Go</button></body>`,
});

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
  titles.set(id, title);
  await sleep(1500);
  return id;
}
// Each tab's title, for show().
const titles = new Map();
// Shows a tab as the user does, a click on its row in the sidebar (lib/mac.mjs
// selectTabRow(): CDP's activation of a tab an agent holds is held); where the
// row isn't there (the sidebar hidden), CDP's activation.
// Returns how: 'row', or the titles of the rows there were.
async function show(targetId) {
  if (titles.has(targetId) && await selectTabRow(titles.get(targetId))) return 'row';
  await browserCall('Target.activateTarget', { targetId });
  const rows = (await viewTree()).filter(v => v.cls === 'TabTitle').map(v => `${v.props.Text}:${v.shown}`);
  return `no row titled "${titles.get(targetId)}" (rows ${rows.join(', ')}); CDP's activation`;
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
// A command that connects (koan status only reads: "connection: none").
await koan(['tabs']);
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
titles.set(agentTab, 'Agent tab');
const how = await show(agentTab);
s = await indicator();
check('Agent tab shown: the pill says the agent controls this tab',
      s.pill && /controlling this tab/.test(s.pillText), `${say(s)}; shown by ${how}`);
check('...with Take over, not Take back',
      s.pillButtons.includes('Take over') && !s.pillButtons.includes('Take back'), say(s));
await show(plain);

// --- Auto-resume (design section 9.2), with real input only.
// Where a widget is on the screen (UI DevTools: a widget's bounds are its
// window's, in screen points), for a real click on a view of it.
async function widgetOrigin(name) {
  const ui = await connect(`ws://127.0.0.1:${process.env.UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  let found = null;
  const walk = n => {
    const a = n.attributes ?? [];
    if (!found && n.nodeName === 'Widget' && a[a.indexOf('name') + 1] === name) found = n;
    for (const c of n.children ?? []) walk(c);
  };
  walk(doc);
  let origin = null;
  if (found) {
    const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: found.nodeId });
    const p = {};
    for (const rule of r.result?.matchedCSSRules ?? []) {
      for (const prop of rule.rule.style.cssProperties) p[prop.name.toLowerCase()] = prop.value;
    }
    origin = { x: +p.x, y: +p.y };
  }
  ui.close();
  return origin;
}
// A real click on the pill's button labelled `text`.
async function clickPillButton(text) {
  const vs = await viewTree();
  const button = vs.find(v => v.shown && v.cls === 'LabelButton' && v.props.Text === text &&
                              v.wname === 'KoanAgentIndicator');
  const at = button && await widgetOrigin('KoanAgentIndicator');
  if (!button || !at) return false;
  return realClick(at.x + button.ax + button.w / 2, at.y + button.ay + button.h / 2, 900);
}
// The tab's entry in koan tabs --json.
async function tabEntry(id) {
  const t = await koan(['tabs', '--json']);
  try {
    return JSON.parse(t.out).tabs.find(e => e.targetId === id) ?? null;
  } catch {
    return null;
  }
}
const pageOf = async id => (await pages()).find(p => p.id === id);

if (!realInput()) {
  skipped('auto-resume', `needs real input (real clicks in the page and on the pill): ${realInputWhyNot()}`);
} else {
  r = await koan(['open', server.url('/form')]);
  const form = r.out.split('\n').at(-1);
  check('koan open: the form tab', r.code === 0 && /^[0-9A-Fa-f]{8,}/.test(form), brief(r));
  await sleep(1500);
  titles.set(form, 'Form');
  await show(form);
  const formPage = await pageOf(form);
  await makeKey(formPage);
  const origin = await calibrate(formPage, await viewTree());
  check('Real pointer: calibrated on the form tab', !!origin);
  // A real click in the page at the element `selector`.
  const clickIn = async selector => {
    const box = await evalIn(formPage, `(() => { const r = document.querySelector('${selector}')
        .getBoundingClientRect(); return {x: r.x + r.width / 2, y: r.y + r.height / 2}; })()`);
    const vs = await viewTree();
    const web = vs.filter(v => v.cls === 'ContentsWebView' && v.shown && v.w > 100)
        .sort((a, b) => b.w * b.h - a.w * a.h)[0];
    return realClick(origin.x + web.ax + box.x, origin.y + web.ay + box.y, 700);
  };
  const paused = async () => (await tabEntry(form))?.state === 'paused';

  if (origin) {
    // 1. A real click pauses; typing makes the page changed.
    const before = await koan(['snapshot', form, '--interactive']);
    const oldRef = before.out.match(/textbox "Name" (@\d+)/)?.[1];
    await clickIn('#name');
    await realKey('a', '', 0);
    // The wait runs from the user's last input (leaving is not activity).
    const typed = Date.now();
    await sleep(500);
    let entry = await tabEntry(form);
    check('A real click in the agent\'s page pauses it (user-input, not sticky)',
          entry?.state === 'paused' && entry.pause?.pauseReason === 'user-input' && !entry.pause?.sticky,
          JSON.stringify(entry));
    check('...and Koan.listTabs says when it resumes', typeof entry?.pause?.resumesAt === 'number',
          JSON.stringify(entry));
    s = await indicator();
    check('...the pill offers Keep paused and Hand back',
          s.pillButtons.includes('Keep paused') && s.pillButtons.includes('Hand back'), say(s));

    // 2. The user leaves the tab: Koan.resumed user-idle after ~5 s, and koan
    // wait --resumed returns with a snapshot.
    const waiting = koan(['wait', form, '--resumed', '--timeout', '30s', '--json'], { timeout: 40000 });
    await sleep(500);
    await show(plain);
    const w = await waiting;
    const took = (Date.now() - typed) / 1000;
    let json = null;
    try { json = JSON.parse(w.out); } catch {}
    check('Left the tab: koan wait --resumed returns (exit 0)', w.code === 0 && json?.ok, brief(w));
    check('...with Koan.resumed {reason: user-idle} after about 5 s',
          json?.resumed?.reason === 'user-idle' && took >= 4.5 && took <= 9, `${took.toFixed(1)} s; ${w.out.slice(0, 300)}`);
    check('...pausedMs, url, title, navigated: false, changed: true (the typed key)',
          json?.resumed?.pausedMs > 4000 && /\/form$/.test(json?.resumed?.url ?? '') &&
          json?.resumed?.title === 'Form' && json?.resumed?.navigated === false &&
          json?.resumed?.changed === true, JSON.stringify(json?.resumed));
    check('...and a fresh interactive snapshot of the page as the user left it',
          /textbox "Name"/.test(json?.snapshot ?? ''), (json?.snapshot ?? '').slice(0, 200));

    // 3. The user stays on the tab, idle: the countdown, then the resume;
    // the refs from before the pause are stale.
    await show(form);
    await makeKey(formPage);
    await koan(['snapshot', form, '--interactive']);
    await clickIn('#go');
    check('A real click on the page pauses it again', await paused());
    // (Not while the pause's toast comes up: see indicator().)
    await sleep(1500);
    const counting = await waitFor(async () => {
      const now = await indicator(300);
      return /continues in \d+s/.test(now.pillText) && now;
    }, 9000);
    check('Idle on the tab: the pill counts down ("continues in Ns")', !!counting,
          counting ? say(counting) : '');
    check('...with Keep paused and Hand back',
          counting && counting.pillButtons.includes('Keep paused') &&
          counting.pillButtons.includes('Hand back'), counting ? say(counting) : '');
    check('...then the tab resumes by itself',
          !!(await waitFor(async () => (await tabEntry(form))?.state === 'driven', 9000)));
    if (oldRef) {
      const stale = await koan(['act', form, 'click', oldRef]);
      check('...and a ref from before the pause answers Koan.staleRef',
            stale.code !== 0 && /Koan\.staleRef: .*before the user used this tab/.test(stale.err + stale.out),
            brief(stale));
    } else {
      skipped('the stale ref after a resume', `no textbox ref in: ${before.out.slice(0, 160)}`);
    }

    // 4. Keep paused (a real click on it): no resume until Hand back.
    await clickIn('#go');
    check('Paused again', await paused());
    await sleep(1200);
    check('A real click on Keep paused', await clickPillButton('Keep paused'));
    entry = await tabEntry(form);
    check('...makes the pause sticky (keep-paused), no resume time',
          entry?.state === 'paused' && entry.pause?.sticky === 'keep-paused' && !entry.pause?.resumesAt,
          JSON.stringify(entry));
    const sticky = await koan(['wait', form, '--resumed', '--timeout', '20s']);
    check('...koan wait --resumed exits 1 at once: the user is handling this tab',
          sticky.code === 1 && /handling this tab \(keep-paused\)/.test(sticky.err), brief(sticky));
    await show(plain);
    await sleep(8000);
    check('...left alone for 8 s, it stays paused', await paused());
    await show(form);
    await makeKey(formPage);
    await sleep(800);
    check('A real click on Hand back', await clickPillButton('Hand back'));
    check('...gives the tab back (driven)',
          !!(await waitFor(async () => (await tabEntry(form))?.state === 'driven', 3000)));

    // 5. Focus in a password field: no resume.
    await clickIn('#pass');
    entry = await waitFor(async () => {
      const e = await tabEntry(form);
      return e?.pause?.sticky && e;
    }, 4000);
    check('A real click into a password field: the pause is sticky (secret-field)',
          entry?.pause?.sticky === 'secret-field', JSON.stringify(entry));
    await show(plain);
    await sleep(8000);
    check('...left alone for 8 s, it stays paused', await paused());
    await show(form);
    await makeKey(formPage);
    await sleep(800);
    await clickPillButton('Hand back');
    await waitFor(async () => (await tabEntry(form))?.state === 'driven', 3000);
  }
  await show(plain);
}

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
