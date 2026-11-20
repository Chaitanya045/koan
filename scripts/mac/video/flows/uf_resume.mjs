// User flow: the person interrupts an agent, and it carries on (#408). Test
// cases UF-RES-* in .agent/notes/user-flows.md. All the person's input is
// real (clicks and keys through the gates): a click in the agent's tab
// pauses it; leaving the tab resumes it about 5 s after their last input
// and `koan wait --resumed` hands the agent a fresh snapshot; a batch the
// person interrupts stops where it was and says so; staying idle on the tab
// shows the pill's countdown, then it resumes and the old refs are stale;
// Keep paused holds until Hand back; a password or card field holds; the
// Settings rows (on/off, 2-60 s) change what happens.
import fs from 'node:fs';
import { settingsCall, showPane } from '../../../e2e/lib/koan.mjs';
import { CLI, begin, brief, browserCall, clickElement, clickPill, end, evalIn, goToAgentTab, indicator, keyPress,
         koan, lastLine, loc, openAnchor, pageById, pair, parkPointer, recalibrate, refOf, sayIndicator, show,
         skipFlow, sleep, step, tabEntry, tabIdOk, waitFor } from './uf_lib.mjs';

// The auto-resume switch's control, or what a click on it should hit when
// the checkbox itself is drawn as a toggle of no size.
const SWITCH = `(() => { const i = koanSettingsRoot.querySelector('[data-pref="koan.agent_control.auto_resume"] input');
    const r = i?.getBoundingClientRect(); return !i || r.width > 4 ? i : (i.closest('label') ?? i.parentElement); })()`;
const fixtures = await begin('uf_resume', 'The person interrupts the agent; it carries on when they stop', '#408');
if (!CLI || !fs.existsSync(CLI)) skipFlow(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);
const anchor = await openAnchor(fixtures.url('/p?t=Plain%20tab'));
const plain = anchor.id;
await parkPointer();
let form = null, formPage = null;
const state = async () => (await tabEntry(form)) ?? {};
// The agent's tab is in its own Space: the person goes there with real
// clicks on the chip and its "Go to <title> (UF agent)" (never CDP, which
// Koan holds for a tab an agent holds). Already in front, the menu has no
// item for it and the pill shows: that's fine too.
const front = async () => {
  formPage = await pageById(form);
  const g = await goToAgentTab(formPage?.title ?? '');
  const ok = g.ok || (!g.item && (await indicator(200)).pill);
  if (!ok) console.log(`  (front: ${g.why}; ${JSON.stringify(g.items)})`);
  formPage = await pageById(form);
  await recalibrate(formPage);
  return { ok, why: `${g.why}; ${JSON.stringify(g.items)}` };
};
// The person clicks in the agent's page (a real click), optionally types.
const use = async (selector, typed) => {
  const c = await clickElement(formPage, `document.querySelector(${JSON.stringify(selector)})`, 300);
  if (typed) await keyPress(typed, '', 200);
  return c;
};

await step('UF-RES-1', 'The person pairs the agent; it opens the checkout page', async t => {
  const { r, approval } = await pair(t, 'UF agent');
  t.check('paired with a real click on Allow', r.code === 0 && approval?.real, approval?.why ?? brief(r));
  const o = await koan(['open', fixtures.url('/secret'), '--task', 'res']);
  form = lastLine(o);
  t.check('koan open: the agent\'s tab', o.code === 0 && tabIdOk(form), brief(o));
  await sleep(1500);
}, { needs: ['real input'] });

let typedAt = 0;
await step('UF-RES-2', 'The person clicks in the agent\'s tab and types: it pauses (user-input)', async t => {
  const f = await front();
  t.check('the agent\'s tab in front: real clicks on the chip and "Go to Checkout (UF agent)"', f.ok, f.why);
  const c = await use('#name', 'a');
  typedAt = Date.now();
  t.check('a real click in the Name field and a key', c.ok, c.why);
  const e = await state();
  t.check('koan tabs: paused, reason user-input, not sticky, with a resume time',
          e.state === 'paused' && e.pause?.pauseReason === 'user-input' && !e.pause?.sticky &&
          typeof e.pause?.resumesAt === 'number', JSON.stringify(e));
  const s = await indicator(900);
  t.check('the pill offers Keep paused and Hand back', s.buttons.includes('Keep paused') && s.buttons.includes('Hand back'),
          sayIndicator(s));
  await t.shot('paused');
}, { needs: ['real input'] });

await step('UF-RES-3', 'The person leaves the tab: about 5 s later the agent resumes with a fresh snapshot', async t => {
  const waiting = koan(['wait', form, '--resumed', '--timeout', '30s', '--json'], { timeout: 40000 });
  await sleep(400);
  await show(plain);
  const w = await waiting;
  const took = (Date.now() - typedAt) / 1000;
  let j = null;
  try { j = JSON.parse(w.out); } catch {}
  t.check('koan wait --resumed returns, exit 0', w.code === 0 && j?.ok, brief(w));
  t.check('Koan.resumed {reason: user-idle} about 5 s after the last key (4.5-9 s)',
          j?.resumed?.reason === 'user-idle' && took >= 4.5 && took <= 9, `${took.toFixed(1)} s; ${w.out.slice(0, 200)}`);
  t.check('...it says the page changed (the typed key), not navigated',
          j?.resumed?.changed === true && j?.resumed?.navigated === false, JSON.stringify(j?.resumed));
  t.check('...with a fresh interactive snapshot', /textbox "Name"/.test(j?.snapshot ?? ''), (j?.snapshot ?? '').slice(0, 160));
});

await step('UF-RES-4', 'A batch the person interrupts stops where it was, and says how to go on', async t => {
  let r = await koan(['act', form, 'navigate', fixtures.url('/signup/1')]);
  await front();
  const steps = [];
  for (let i = 0; i < 6; i++) {
    steps.push('fill', loc('textbox', 'First name'), `first${i}`, 'fill', loc('textbox', 'Last name'), `last${i}`);
  }
  const batch = koan(['act', form, ...steps, 'fill', loc('textbox', 'Email'), 'end@example.com'], {
    whileRunning: async () => {
      // 13 steps, each settling for 250 ms at least: the click lands about
      // a third of the way in.
      await sleep(1200);
      await use('h1');
    } });
  r = await batch;
  t.check('the batch stopped at the person\'s click (Koan.paused), saying which steps did not run',
          r.code !== 0 && /^stopped at \d+\/13 .*Koan\.paused/m.test(r.out) && /^not run: /m.test(r.out), r.out.slice(-300));
  t.check('...and tells the agent to run `koan wait <tab> --resumed`', /koan wait .* --resumed/.test(r.out + r.err),
          (r.out + r.err).slice(-200));
  const email = await evalIn(formPage, 'document.getElementById("email").value');
  t.check('nothing was typed after the pause (the Email step never ran)', email === '', JSON.stringify(email));
  await t.shot('batch-stopped');
}, { needs: ['real input'] });

await step('UF-RES-5', 'The person stays on the tab, idle: the countdown, then it resumes; old refs are stale', async t => {
  const snap = await koan(['snapshot', form, '--interactive']);
  const old = refOf(snap.out, /textbox "Email"/);
  await use('h1');
  t.check('paused again by a real click', (await state()).state === 'paused');
  await sleep(1500);
  const counting = await waitFor(async () => {
    const s = await indicator(300);
    return /continues in \d+s/.test(s.pillText) && s;
  }, 9000);
  t.check('the pill counts down: "continues in Ns · Keep paused · Hand back"',
          !!counting && counting.buttons.includes('Keep paused') && counting.buttons.includes('Hand back'),
          counting ? sayIndicator(counting) : 'no countdown');
  await t.shot('countdown');
  t.check('...then it resumes by itself', !!(await waitFor(async () => (await state()).state === 'driven', 9000)));
  const r = await koan(['act', form, 'fill', old ?? '@0', 'x']);
  t.check('a ref from before the pause: Koan.staleRef ("from before the user used this tab")',
          r.code !== 0 && /Koan\.staleRef: .*before the user used this tab/.test(r.err + r.out), brief(r));
}, { needs: ['real input'] });

await step('UF-RES-6', 'Keep paused (a real click) holds until Hand back (a real click)', async t => {
  await use('h1');
  await sleep(1200);
  const k = await clickPill('Keep paused');
  t.check('a real click on Keep paused', k.ok, k.why);
  const e = await state();
  t.check('sticky: keep-paused, no resume time', e.state === 'paused' && e.pause?.sticky === 'keep-paused' &&
          !e.pause?.resumesAt, JSON.stringify(e));
  const w = await koan(['wait', form, '--resumed', '--timeout', '20s']);
  t.check('koan wait --resumed exits 1 at once: "the user is handling this tab (keep-paused)"',
          w.code === 1 && /handling this tab \(keep-paused\)/.test(w.err) && w.ms < 5000, brief(w));
  await show(plain);
  await sleep(8000);
  t.check('away for 8 s: still paused', (await state()).state === 'paused');
  await front();
  const h = await clickPill('Hand back');
  t.check('a real click on Hand back', h.ok, h.why);
  t.check('...the agent has the tab again', !!(await waitFor(async () => (await state()).state === 'driven', 4000)));
}, { needs: ['real input'] });

for (const [id, selector, what] of [['UF-RES-7', '#pass', 'password'], ['UF-RES-8', '#card', 'card number']]) {
  await step(id, `A click into the ${what} field holds the pause until Hand back`, async t => {
    let r = await koan(['act', form, 'navigate', fixtures.url('/secret')]);
    await front();
    const c = await use(selector);
    t.check(`a real click into the ${what} field`, c.ok, c.why);
    const e = await waitFor(async () => { const x = await state(); return x.pause?.sticky && x; }, 4000);
    t.check('sticky: secret-field', /secret/.test(e?.pause?.sticky ?? ''), JSON.stringify(e));
    await t.shot('held');
    await show(plain);
    await sleep(7000);
    t.check('away for 7 s: still paused', (await state()).state === 'paused');
    await front();
    await clickPill('Hand back');
    r = await waitFor(async () => (await state()).state === 'driven', 4000);
    t.check('Hand back gives it to the agent', !!r);
  }, { needs: ['real input'] });
}

await step('UF-RES-9', 'Settings > Agent Control: "Continues after" 2 s, then auto-resume off', async t => {
  const id = (await browserCall('Target.createTarget', { url: 'chrome://settings/koan/agents' })).result?.targetId;
  const settings = await waitFor(() => pageById(id), 8000);
  await evalIn(settings, showPane('agents'));
  await waitFor(() => evalIn(settings, `!!koanSettingsRoot?.querySelector('[data-pref="koan.agent_control.auto_resume"]')`), 8000);
  const info = await settingsCall(settings, 'koanGetSettings');
  const row = pref => info?.rows?.find(r => r.pref === pref);
  const on = row('koan.agent_control.auto_resume'), secs = row('koan.agent_control.auto_resume_seconds');
  t.check('the two rows are there', !!on && !!secs, JSON.stringify(info?.rows?.filter(r => /auto_resume/.test(r.pref))));
  t.check('"Continues after" offers 2, 3, 5, 10, 15, 20, 30, 45 and 60 s (no 0, no unlimited)',
          JSON.stringify(secs?.options?.map(o => +o.value)) === JSON.stringify([2, 3, 5, 10, 15, 20, 30, 45, 60]),
          JSON.stringify(secs?.options));
  await recalibrate(settings);
  await t.shot('rows');
  // The menu's native popup can't be driven here: its value is set as the
  // page does on a pick (the change event).
  await evalIn(settings, `(() => { const s = koanSettingsRoot.querySelector('[data-pref="koan.agent_control.auto_resume_seconds"] select');
      s.selectedIndex = ${JSON.stringify(secs?.options?.map(o => +o.value) ?? [])}.indexOf(2); s.dispatchEvent(new Event('change')); })()`);
  const two = await waitFor(async () => (await settingsCall(settings, 'koanGetSettings'))?.prefs?.['koan.agent_control.auto_resume_seconds'] === 2, 3000);
  t.check('set to 2 s', !!two);
  // A fresh page: RES-8 left the focus in the card field, and a click out
  // of it is still held as secret-field (the PR's open question).
  await koan(['act', form, 'navigate', fixtures.url('/secret')]);
  await front();
  await use('h1', 'b');
  const at = Date.now();
  const waiting = koan(['wait', form, '--resumed', '--timeout', '20s', '--json'], { timeout: 30000 });
  await sleep(200);
  await show(plain);
  const w = await waiting;
  const took = (Date.now() - at) / 1000;
  t.check('left: it resumes about 2 s after the last key (1.5-4.5 s)', w.code === 0 && took >= 1.5 && took <= 4.5,
          `${took.toFixed(1)} s; ${brief(w)}`);
  // Off, with a real click on its checkbox.
  await show(id);
  await recalibrate(settings);
  const c = await clickElement(settings, SWITCH, 600);
  t.check('a real click turns auto-resume off', c.ok &&
          (await settingsCall(settings, 'koanGetSettings'))?.prefs?.['koan.agent_control.auto_resume'] === false, c.why ?? '');
  await front();
  await use('h1');
  await show(plain);
  await sleep(8000);
  t.check('off: left for 8 s, it stays paused (waits for Hand back)', (await state()).state === 'paused');
  await front();
  await clickPill('Hand back');
  t.check('Hand back still works', !!(await waitFor(async () => (await state()).state === 'driven', 4000)));
  await show(id);
  await clickElement(settings, SWITCH, 600);
  await browserCall('Target.closeTarget', { targetId: id }).catch(() => {});
}, { needs: ['real input'] });

await step('UF-RES-10', 'The agent ends', async t => {
  const r = await koan(['end']);
  t.check('koan end', r.code === 0, brief(r));
});
end();
