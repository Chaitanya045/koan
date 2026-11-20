// User flow: agent tabs keep rendering in the background (#403, #407). Test
// cases UF-RND-* in .agent/notes/user-flows.md. An agent watches an
// animating chart (scripts/e2e/pages/uf_canvas.html) in its background tab
// while Koan's window is covered by another app's window (our own,
// scripts/mac/tools/cover.m) and while it is minimised;
// the person's own copy of the page in a background tab stays throttled.
// The page's frame and timer rates are read over the harness's own DevTools
// connection (no capturer). `pmset -g assertions` has "Koan: an agent is
// working" while a hold is on and not after; the chip's tooltip says
// "keeping the display awake". Holds end with `koan end` and with Stop (a
// real click on the pill), and once a wait is over.
import fs from 'node:fs';
import path from 'node:path';
import { AGENT_HOME, CLI, WAKE_REASON, begin, brief, browserCall, clickPill, cover, end, goToAgentTab, indicator,
         info, koan, koanAssertions, lastLine, lowPowerOnBattery, openAnchor, pair, parkPointer, rates,
         show, skipFlow, sleep, spawnKoan, step, tabIdOk, viewTree, visibility, waitFor, windowBounds }
    from './uf_lib.mjs';
import { benchCannotMinimise, minimiseWithButton, unminimise } from '../../../e2e/lib/mac.mjs';
import { clickAt } from './common.mjs';

const LIVE = 25, THROTTLED = 2;
const fixtures = await begin('uf_render', 'Agent tabs keep animating while the window is covered or minimised',
                             '#403 #407');
if (!CLI || !fs.existsSync(CLI)) skipFlow(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);
const anchor = await openAnchor(fixtures.url('/p?t=Your%20tab'));
await parkPointer();
const saving = lowPowerOnBattery();
const wanted = saving ? /PreventUserIdleSystemSleep|NoIdleSleep/ : /PreventUserIdleDisplaySleep|NoDisplaySleep/;
const pngs = [];
const pngDiffers = (a, b) => fs.existsSync(a) && fs.existsSync(b) && !fs.readFileSync(a).equals(fs.readFileSync(b));
let agentA = null, agentB = null, userBg = null;
// A window of `ms` from now, after `after` ms: the page's rates in it.
async function measure(id, ms, after = 0) {
  await sleep(after);
  const from = Date.now();
  await sleep(ms);
  return rates(id, from, Date.now());
}

await step('UF-RND-1', 'The person pairs the agent; it opens the chart in a background tab', async t => {
  const { r, approval } = await pair(t, 'UF agent');
  t.check('paired with a real click on Allow', r.code === 0 && approval?.real, approval?.why ?? brief(r));
  const o = await koan(['open', fixtures.url('/canvas?agent=a'), '--task', 'a']);
  agentA = lastLine(o);
  t.check('koan open: the agent\'s chart tab', o.code === 0 && tabIdOk(agentA), brief(o));
  // The person's own copy on another site (its own renderer), in the background.
  userBg = (await browserCall('Target.createTarget',
      { url: `http://localhost:${fixtures.port}/canvas?user=1`, background: true })).result?.targetId;
  await sleep(2000);
  await show(anchor.id);
  t.check('the person\'s copy is a background tab (hidden)', await visibility(userBg) === 'hidden',
          await visibility(userBg));
  t.check('no Koan assertion before any hold', (koanAssertions() ?? ['?']).length === 0, JSON.stringify(koanAssertions()));
}, { needs: ['real input'] });

let covered = null;
await step('UF-RND-2', 'Window covered: the agent\'s screenshots still move, and `koan watch` keeps it live', async t => {
  const b = await windowBounds();
  covered = await cover(b, 90);
  t.check('another app\'s window covers Koan\'s (the front tab went hidden)', covered.ready &&
          !!(await waitFor(async () => await visibility(anchor.id) === 'hidden', 8000)), await visibility(anchor.id));
  await t.shot('covered', { rect: { x: b.left - 30, y: b.top - 30, w: b.width + 60, h: b.height + 60 }, overview: true });
  for (const i of [1, 2]) {
    const f = path.join(AGENT_HOME, `covered-${i}.png`);
    const r = await koan(['screenshot', agentA, '--size', 'mini', '-o', f]);
    if (r.code === 0) pngs.push(f);
    await sleep(500);
  }
  t.check('two mini screenshots of the agent\'s tab, 0.5 s apart, differ (the chart moved)',
          pngs.length === 2 && pngDiffers(pngs[0], pngs[1]), pngs.join(', '));
  const watch = spawnKoan(['watch', agentA]);
  let said = '';
  watch.stderr.on('data', d => (said += d));
  const w = await measure(agentA, 8000, 1000);
  t.check(`koan watch, no command: the agent tab renders (> ${LIVE} frames/s)`, w && w.rafPerS > LIVE,
          JSON.stringify(w));
  const u = await rates(userBg, Date.now() - 8000, Date.now());
  t.check(`the person's own background copy stays throttled (< ${THROTTLED} frames/s)`, u && u.rafPerS < THROTTLED,
          JSON.stringify(u));
  const held = koanAssertions();
  t.check(`pmset: "${WAKE_REASON}" (${saving ? 'idle sleep only: Low Power Mode on battery' : 'display sleep'})`,
          !!held && held.some(x => wanted.test(x)) && (!saving || !held.some(x => /Display/.test(x))), JSON.stringify(held));
  const s = await indicator(300);
  t.check(`the chip's tooltip ${saving ? 'does not say' : 'says'} "keeping the display awake"`,
          saving ? !/keeping the display awake/.test(s.chipTip) : /keeping the display awake/.test(s.chipTip), s.chipTip);
  info(`covered: agent tab ${w?.rafPerS} frames/s (longest gap ${w?.gap} ms), person's tab ${u?.rafPerS}`);
  watch.kill('SIGTERM');
  const quit = await Promise.race([new Promise(r => (watch.exitCode !== null ? r(true) : watch.on('close', () => r(true)))),
                                   sleep(3000).then(() => false)]);
  t.check('koan watch ends on SIGTERM', quit, said.split('\n')[0]);
  covered.close();
  // (The person's copy has the same title as the agent's tabs: closed
  // now, so that UF-RND-6 can tell the agent's tabs went.)
  await browserCall('Target.closeTarget', { targetId: userBg }).catch(() => {});
  userBg = null;
  await waitFor(async () => await visibility(anchor.id) === 'visible', 8000);
}, { needs: ['real input', 'recording'] });

await step('UF-RND-3', '`koan end` of the task releases the hold at once', async t => {
  const r = await koan(['end', '--task', 'a']);
  t.check('koan end --task a', r.code === 0, brief(r));
  const gone = await waitFor(() => (koanAssertions() ?? ['?']).length === 0, 6000);
  t.check('pmset: no Koan assertion within 6 s', !!gone, JSON.stringify(koanAssertions()));
});

await step('UF-RND-4', 'Minimised: an agent waiting with --until keeps its tab live', async t => {
  const o = await koan(['open', fixtures.url('/canvas?agent=b'), '--task', 'b']);
  agentB = lastLine(o);
  t.check('koan open: a second chart tab', o.code === 0 && tabIdOk(agentB), brief(o));
  await sleep(1500);
  // Nothing minimises while the session is locked (as it is while the
  // person is away): asked first, so that a minimise that would stall never
  // leaves the window half-minimised for the steps after.
  const cannot = benchCannotMinimise();
  if (cannot) return t.skip(cannot);
  // A real click on the window's minimise button (CDP's
  // Browser.setWindowBounds minimized did not minimise it here, cycle 19).
  const m = await minimiseWithButton({ click: async (x, y) => { await clickAt(x, y); return true; } });
  t.check('a real click on the window\'s minimise button', m.ok, m.why);
  const min = await waitFor(async () => await visibility(anchor.id) === 'hidden', 8000);
  t.check('the window is minimised (the front tab went hidden)', !!min, await visibility(anchor.id));
  // A wait that never comes true: the CLI holds the tab while it waits.
  const waiting = koan(['act', agentB, 'press', 'Tab', '--until', 'text~never-on-this-page', '--timeout', '10']);
  const w = await measure(agentB, 7000, 2000);
  const r = await waiting;
  t.check(`while the agent waits: the tab renders (> ${LIVE} frames/s)`, w && w.rafPerS > LIVE, JSON.stringify(w));
  t.check('...and the wait ends as it should (Koan.untilTimeout)', r.code !== 0 && /untilTimeout/.test(r.err + r.out), brief(r));
  info(`minimised, waiting: agent tab ${w?.rafPerS} frames/s`);
  unminimise();
  await waitFor(async () => await visibility(anchor.id) === 'visible', 8000);
  await show(anchor.id);
}, { needs: ['real input'] });

await step('UF-RND-5', 'Nothing held: the agent tab is throttled again a few seconds after its last command', async t => {
  const w = await measure(agentB, 4000, 4500);
  t.check(`5 s after the wait, no command: < ${THROTTLED} frames/s`, w && w.rafPerS < THROTTLED, JSON.stringify(w));
  const gone = await waitFor(() => (koanAssertions() ?? ['?']).length === 0, 4000);
  t.check('pmset: no Koan assertion', !!gone, JSON.stringify(koanAssertions()));
});

await step('UF-RND-6', 'Stop (a real click on the pill) while a watch runs: everything is released', async t => {
  const watch = spawnKoan(['watch', agentB]);
  let ended = false;
  watch.on('close', () => { ended = true; });
  await sleep(2500);
  t.check('a hold is on: pmset has the assertion', (koanAssertions() ?? []).some(x => wanted.test(x)),
          JSON.stringify(koanAssertions()));
  // In the agent's Space: the chip's menu, with real clicks.
  const g = await goToAgentTab('Live chart');
  t.check('the agent\'s tab is shown: real clicks on the chip and "Go to Live chart (UF agent)"', g.ok,
          `${g.why}; ${JSON.stringify(g.items)}`);
  await parkPointer();
  const s = await indicator();
  t.check('its pill has Stop', s.buttons.includes('Stop'), JSON.stringify(s.buttons));
  await t.shot('pill-stop');
  const c = await clickPill('Stop');
  t.check('a real click on Stop', c.ok, c.why);
  const gone = await waitFor(() => (koanAssertions() ?? ['?']).length === 0, 4000);
  t.check('pmset: the assertion is gone within 4 s', !!gone, JSON.stringify(koanAssertions()));
  t.check('the watch ended', !!(await waitFor(() => ended, 5000)));
  const closed = await waitFor(async () => !(await viewTree()).some(v => v.cls === 'TabTitle' && v.shown &&
                                                                         v.props.Text === 'Live chart'), 5000);
  t.check('the agent\'s tabs closed', !!closed);
  await show(anchor.id);
  await t.shot('stopped');
}, { needs: ['real input'] });

if (userBg) await browserCall('Target.closeTarget', { targetId: userBg }).catch(() => {});
end();
