// User flow: the agent indicator's layers (#398). Test cases UF-IND-* in
// .agent/notes/user-flows.md. An agent connects (the chip, a toast), takes
// over the person's tab (approved with a real click: the pill and the accent
// border on that tab only, checked in the pixels too), the person opens the
// chip's menu with a real click and goes to the tab from it, sees the dot on
// the agent's own tab, hides the sidebar with compact mode (the corner dot),
// uses the taken-over tab (paused, a toast), hands it back, and the agent
// ends (the disconnected toast).
import fs from 'node:fs';
import { setKoanPref } from '../../../e2e/lib/koan.mjs';
import { CLI, begin, brief, clickElement, clickPill, diffPixels, end, goToAgentTab, indicator, keyPress, koan,
         lastLine, newTab, openAnchor, pageById, pair, parkPointer, recalibrate, regionStats, sayIndicator, show,
         skipFlow, sleep, step, tabEntry, tabIdOk, takeOver, viewRect, viewTree, waitFor } from './uf_lib.mjs';

const fixtures = await begin('uf_indicator', 'The agent indicator: pill, border, chip, corner dot, toasts', '#398');
if (!CLI || !fs.existsSync(CLI)) skipFlow(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);
const plainUrl = fixtures.url('/p?t=Plain%20tab');
const anchor = await openAnchor(plainUrl);
const plain = anchor.id;
await parkPointer();
let user = null, taken = null, agentTab = null;

await step('UF-IND-1', 'An agent connects: the sidebar chip names it, with a toast; no pill anywhere', async t => {
  const { r, approval } = await pair(t, 'UF agent');
  t.check('paired with a real click on Allow', r.code === 0 && approval?.real, approval?.why ?? brief(r));
  await show(plain);
  const tabs = await koan(['tabs']);
  t.check('koan tabs connects', tabs.code === 0, brief(tabs));
  const s = await indicator(1100);
  t.check('a toast says "UF agent connected"', s.toasts.some(x => /UF agent connected/.test(x)), sayIndicator(s));
  await t.shot('connected');
  const s2 = await indicator(2000);
  t.check('the chip names the agent', !!s2.chip && /UF agent/.test(s2.chipText), sayIndicator(s2));
  t.check('no pill, no border, no corner dot on a tab no agent holds', !s2.pill && !s2.border && !s2.corner,
          sayIndicator(s2));
}, { needs: ['real input'] });

await step('UF-IND-2', 'The agent takes over the person\'s tab: pill and accent border on that tab only', async t => {
  user = await newTab(fixtures.url('/p?t=Your%20tab'));
  await show(user);
  await parkPointer();
  const vs0 = await viewTree();
  const container = vs0.filter(v => v.cls === 'ContentsContainerView' && v.shown).sort((a, b) => b.w * b.h - a.w * a.h)[0];
  const box = container && await viewRect(container, vs0);
  const before = await t.shot('before', { pixels: true });
  const res = await takeOver(t);
  taken = res.tab;
  t.check('take-over approved with a real click on Allow', res.r.code === 0 && tabIdOk(taken) && res.approval?.real,
          res.approval?.why ?? brief(res.r));
  const s = await indicator();
  t.check('the pill says "UF agent is controlling this tab"', s.pill && /UF agent is controlling this tab/.test(s.pillText),
          sayIndicator(s));
  t.check('...with Take back and Stop', s.buttons.includes('Take back') && s.buttons.includes('Stop'), sayIndicator(s));
  t.check('...the border view is up, the chip stays, no corner dot', !!s.border && !!s.chip && !s.corner, sayIndicator(s));
  const after = await t.shot('pill', { pixels: true });
  if (box && before.frame && after.frame) {
    // The left and bottom 3 pt inside the page's card: the page before, the
    // border after (the accent and a contrasting line inside it, so a row
    // of the left strip, or a column of the bottom one, changes wherever
    // either shows). The bottom strip stops halfway: the pill sits in the
    // bottom trailing corner.
    const strips = [{ x: box.x, y: box.y + 40, w: 3, h: box.h - 80, by: 'rows' },
                    { x: box.x + 40, y: box.y + box.h - 3, w: box.w / 2 - 40, h: 3, by: 'cols' }];
    const d = strips.map(r => diffPixels(before, after, r, 24, r.by));
    const share = d.map(x => (x.lines ? x.linesChanged / x.lines : 0));
    t.check('the pixels: the accent border goes round the page (left and bottom edges changed)',
            share.every(x => x > 0.5), `changed share left ${share[0].toFixed(2)}, bottom ${share[1].toFixed(2)}`);
  } else {
    t.check('the pixels: the page card and both screenshots', false, `box ${JSON.stringify(box)}; ${before.error ?? ''} ${after.error ?? ''}`);
  }
}, { needs: ['real input', 'screen recording'] });

await step('UF-IND-3', 'Another tab: no pill and no border there, the chip instead', async t => {
  await show(plain);
  const s = await indicator();
  t.check('no pill and no border on the other tab (the user\'s complaint)', !s.pill && !s.border, sayIndicator(s));
  t.check('the chip shows', !!s.chip && /UF agent/.test(s.chipText), sayIndicator(s));
  await t.shot('other-tab');
});

await step('UF-IND-4', 'The person opens the chip\'s menu (real click) and goes to the agent\'s tab', async t => {
  const g = await goToAgentTab('Your tab');
  t.check('its menu: "Go to Your tab (UF agent)", a separator, "Stop all agents"',
          JSON.stringify(g.items) === JSON.stringify(['Go to Your tab (UF agent)', '', 'Stop all agents']),
          JSON.stringify(g.items));
  t.check('a real click on the chip, then on "Go to Your tab (UF agent)"', g.ok, g.why);
  const s = await indicator();
  t.check('...the taken-over tab is in front, with its pill', s.pill && /controlling this tab/.test(s.pillText),
          sayIndicator(s));
  t.check('...nothing waits (the chip has no count)', !/waiting/.test(s.chipText), sayIndicator(s));
  await t.shot('went-to');
}, { needs: ['real input'] });

await step('UF-IND-5', 'The agent\'s own tab: a dot on its sidebar row; shown, the pill offers Take over', async t => {
  const r = await koan(['open', fixtures.url('/p?t=Agent%20tab'), '--task', 'ind']);
  agentTab = lastLine(r);
  t.check('koan open: the agent\'s tab', r.code === 0 && tabIdOk(agentTab), brief(r));
  await sleep(1500);
  // In the agent's own Space: the person goes there from the chip's menu.
  const g = await goToAgentTab('Agent tab');
  t.check('the chip\'s menu lists "Go to Agent tab (UF agent)"; a real click on it', g.ok,
          `${g.why}; ${JSON.stringify(g.items)}`);
  await parkPointer();
  const s = await indicator();
  t.check('the pill says the agent controls this tab, with Take over (not Take back)',
          s.pill && /controlling this tab/.test(s.pillText) && s.buttons.includes('Take over') &&
          !s.buttons.includes('Take back'), sayIndicator(s));
  t.check('...and nothing waits (the chip has no count)', !/waiting/.test(s.chipText), sayIndicator(s));
  const shot = await t.shot('agent-tab', { pixels: true });
  const vs = s.vs;
  const title = vs.find(v => v.cls === 'TabTitle' && v.shown && v.props.Text === 'Agent tab');
  const row = title && vs.filter(v => v.cls === 'TabView' && v.shown && v.widget === title.widget &&
                                      v.ax <= title.ax && v.ay <= title.ay && v.ax + v.w > title.ax &&
                                      v.ay + v.h > title.ay).sort((a, b) => a.w * a.h - b.w * b.h)[0];
  const rr = row && await viewRect(row, vs);
  if (rr && shot.frame) {
    // The dot is in the row's top trailing corner; the close button (if
    // any) is centred, so the bottom trailing corner is the control.
    const bg = regionStats(shot, { x: rr.x + rr.w * 0.6, y: rr.y + 2, w: 10, h: 4 }).mean;
    const top = regionStats(shot, { x: rr.x + rr.w - 14, y: rr.y + 1, w: 12, h: 11 }, bg);
    const bottom = regionStats(shot, { x: rr.x + rr.w - 14, y: rr.y + rr.h - 12, w: 12, h: 11 }, bg);
    t.check('the pixels: a dot in the row\'s top trailing corner (more off-background pixels than the bottom corner)',
            top.off - bottom.off >= 8, `top ${top.off}, bottom ${bottom.off} of ${top.n} px; background ${bg}`);
  } else {
    t.check('the agent tab\'s sidebar row and a screenshot', false, `row ${!!row}; ${shot.error ?? ''}`);
  }
}, { needs: ['screen recording'] });

await step('UF-IND-6', 'Compact mode hides the sidebar: the corner dot stands in for the chip', async t => {
  await setKoanPref('koan.compact_mode.hide', 'sidebar');
  await show(plain);
  const p = await pageById(plain);
  await recalibrate(p);
  await keyPress('s', 'c', 1500);
  let s = await indicator(1500);
  t.check('⌘S (a real key): compact mode, the sidebar hidden, the corner dot on a plain tab',
          !!s.corner && !s.pill, sayIndicator(s));
  await t.shot('corner-dot');
  // The sidebar is hidden: the corner dot's menu is the chip's.
  const g = await goToAgentTab('Your tab', 900, 'KoanAgentCornerDot');
  t.check('a real click on the corner dot, then on "Go to Your tab (UF agent)"', g.ok,
          `${g.why}; ${JSON.stringify(g.items)}`);
  s = await indicator();
  t.check('on the taken-over tab: the pill, no corner dot', s.pill && !s.corner, sayIndicator(s));
  await show(plain);
  await keyPress('s', 'c', 1500);
  s = await indicator(1500);
  t.check('⌘S again: the chip is back, the corner dot gone', !!s.chip && !s.corner, sayIndicator(s));
}, { needs: ['real input'] });

await step('UF-IND-7', 'The person uses the taken-over tab: paused, with a toast; Hand back (real click)', async t => {
  const g = await goToAgentTab('Your tab');
  t.check('the taken-over tab, from the chip\'s menu (real clicks)', g.ok, `${g.why}; ${JSON.stringify(g.items)}`);
  const p = await pageById(taken);
  await recalibrate(p);
  const c = await clickElement(p, 'document.querySelector("h1")', 300);
  t.check('a real click in the page', c.ok, c.why);
  const s = await indicator(900);
  t.check('a toast: "UF agent paused: you used its tab"', s.toasts.some(x => /UF agent paused: you used its tab/.test(x)),
          sayIndicator(s));
  t.check('the pill says it is paused because you used the tab, with Hand back',
          /paused: you used this tab/.test(s.pillText) && s.buttons.includes('Hand back'), sayIndicator(s));
  await t.shot('paused');
  const e = await tabEntry(taken);
  t.check('koan tabs: the tab is paused (user-input)', e?.state === 'paused', JSON.stringify(e));
  const h = await clickPill('Hand back');
  t.check('a real click on Hand back', h.ok, h.why);
  const back = await waitFor(async () => (await tabEntry(taken))?.state === 'driven', 4000);
  t.check('the agent has the tab again', !!back, JSON.stringify(await tabEntry(taken)));
}, { needs: ['real input'] });

await step('UF-IND-8', 'Release and end: the pill goes, then the chip, with "disconnected"', async t => {
  let r = await koan(['release', taken]);
  t.check('koan release', r.code === 0, brief(r));
  await show(taken);
  let s = await indicator();
  t.check('released: no pill, no border, the chip stays', !s.pill && !s.border && !!s.chip, sayIndicator(s));
  r = await koan(['end']);
  t.check('koan end', r.code === 0, brief(r));
  s = await indicator(900);
  t.check('a toast: "UF agent disconnected"', s.toasts.some(x => /UF agent disconnected/.test(x)), sayIndicator(s));
  await t.shot('disconnected');
  s = await indicator(2500);
  t.check('no chip, pill or corner dot left', !s.chip && !s.pill && !s.corner, sayIndicator(s));
});

end();
