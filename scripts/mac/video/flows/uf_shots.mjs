// User flow: the agent's screenshots (#409, #410). Test cases UF-SHT-* in
// .agent/notes/user-flows.md. The agent takes every size of screenshot of
// its own tab (micro, mini, normal, full: the pixels, the output line, the
// tokens), clips an element by @ref and by locator, gets one in the same
// answer as an act; then it takes over the person's tab (a real click on
// Allow) and does the same there, through the same capture path
// (Koan.captureScaled): the page (scripts/e2e/pages/agent_capture_watch.html)
// counts every resize, devicePixelRatio and media query change it could
// see, and they must stay 0. No flicker on the person's tab: the captures
// run inside a "still" mark (the page's rect in the window, its duration),
// and run_flows.sh checks the recording there: no pixel of the page may
// change (UF-SHT-6), next to a control mark where the page does change (so
// the check is shown to be able to fail).
import fs from 'node:fs';
import path from 'node:path';
import { AGENT_HOME, CLI, begin, brief, end, evalIn, info, koan, lastLine, mark, newTab, openAnchor, pageById,
         pair, parkPointer, refOf, show, skipFlow, sleep, step, tabIdOk, takeOver, viewRect, viewTree,
         webviewOf, windowBounds } from './uf_lib.mjs';

const fixtures = await begin('uf_shots', 'Screenshot sizes and clips, on the agent\'s tab and a taken-over tab',
                             '#409 #410');
if (!CLI || !fs.existsSync(CLI)) skipFlow(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);
const anchor = await openAnchor(fixtures.url('/p?t=Your%20tab'));
await parkPointer();
const SIZES = ['micro', 'mini', 'normal', 'full'];
const LINE = /^saved (.+) \((\d+)×(\d+), ~(\d+) image tokens, 1 px = ([\d.]+) CSS px\)$/m;
const pngSize = f => {
  const b = fs.existsSync(f) ? fs.readFileSync(f) : Buffer.alloc(0);
  return b.subarray(1, 4).toString() === 'PNG' ? [b.readUInt32BE(16), b.readUInt32BE(20)] : null;
};
const near = (got, want) => !!got && Math.abs(got[0] - want[0]) <= 2 && Math.abs(got[1] - want[1]) <= 2;
// koan_agent_capture.cc's sizing (#409's math): never larger than the
// display draws, fitted to the long edge, then to 4,784 tokens of area.
const fitted = (w, h, perCss, edge) => {
  let f = Math.min(1, edge / (Math.max(w, h) * perCss));
  const px = w * h * (perCss * f) ** 2, max = 4784 * 784;
  if (px > max) f *= Math.sqrt(max / px);
  return [Math.max(1, Math.round(w * perCss * f)), Math.max(1, Math.round(h * perCss * f))];
};
const wantOf = v => ({ micro: fitted(v.w, v.h, Math.min(1, v.dpr), 320), mini: fitted(v.w, v.h, Math.min(1, v.dpr), 640),
                       normal: fitted(v.w, v.h, Math.min(1, v.dpr), 1568), full: fitted(v.w, v.h, v.dpr, 2576) });
const viewOf = async id => JSON.parse(await evalIn(await pageById(id), 'JSON.stringify({ w: visualViewport.width, ' +
    'h: visualViewport.height, dpr: devicePixelRatio, b: document.getElementById("press").getBoundingClientRect() })') ?? 'null');
let agentTab = null, taken = null;

async function sizes(t, tab, who) {
  const v = await viewOf(tab);
  t.check(`${who}: the page says its viewport and display scale`, !!v?.w, JSON.stringify(v));
  if (!v?.w) return null;
  const want = wantOf(v), tokens = {};
  for (const size of SIZES) {
    const f = path.join(AGENT_HOME, `${who.replace(/\W+/g, '-')}-${size}.png`);
    const r = await koan(['screenshot', tab, '--size', size, '-o', f]);
    const got = pngSize(f), line = r.out.match(LINE);
    tokens[size] = line ? +line[4] : NaN;
    t.check(`${who}: --size ${size} is ${want[size].join('×')} on this ${v.dpr}× display, as its line says`,
            r.code === 0 && near(got, want[size]) && !!line && +line[2] === got[0] && +line[3] === got[1],
            `${brief(r)}; the PNG is ${got?.join('×')}`);
  }
  t.check(`${who}: tokens grow with the size, micro at most a tenth of full`,
          tokens.micro < tokens.mini && tokens.mini < tokens.normal && tokens.normal <= tokens.full &&
          tokens.micro * 10 <= tokens.full, JSON.stringify(tokens));
  info(`${who}: image tokens ${JSON.stringify(tokens)}`);
  return v;
}
async function clips(t, tab, who, v) {
  const { b } = v;
  const left = Math.max(0, b.x - 8), top = Math.max(0, b.y - 8);
  const cw = Math.min(v.w, b.x + b.width + 8) - left, ch = Math.min(v.h, b.y + b.height + 8) - top;
  const f = path.join(AGENT_HOME, `${who.replace(/\W+/g, '-')}-clip.png`);
  let r = await koan(['screenshot', tab, '--clip', 'loc:role=button[name="Press me"]', '-o', f]);
  t.check(`${who}: --clip by locator is the button's box with 8 px around it, at device pixels`,
          r.code === 0 && near(pngSize(f), [cw * v.dpr, ch * v.dpr]), `${brief(r)}; ${pngSize(f)}`);
  const snap = await koan(['snapshot', tab, '--interactive']);
  const ref = refOf(snap.out, /button "Press me"/);
  r = await koan(['screenshot', tab, '--clip', ref ?? '@0', '--size', 'normal', '-o', f]);
  t.check(`${who}: --clip ${ref} --size normal: CSS pixels`, r.code === 0 && near(pngSize(f), [cw, ch]),
          `${brief(r)}; ${pngSize(f)}`);
  r = await koan(['screenshot', tab, '--clip', `${Math.round(v.w) + 50},0,20,20`, '-o', f]);
  t.check(`${who}: a clip outside the viewport: Koan.offScreen`, r.code !== 0 && /Koan\.offScreen/.test(r.err), brief(r));
}

await step('UF-SHT-1', 'The person pairs the agent; it opens a page', async t => {
  const { r, approval } = await pair(t, 'UF agent');
  t.check('paired with a real click on Allow', r.code === 0 && approval?.real, approval?.why ?? brief(r));
  const o = await koan(['open', fixtures.url('/watch')]);
  agentTab = lastLine(o);
  t.check('koan open', o.code === 0 && tabIdOk(agentTab), brief(o));
  await sleep(1500);
}, { needs: ['real input'] });

await step('UF-SHT-2', 'On its own tab: micro, mini, normal and full', async t => {
  await sizes(t, agentTab, 'agent tab');
});

await step('UF-SHT-3', 'On its own tab: clips, and a screenshot in the same answer as an act', async t => {
  const v = await viewOf(agentTab);
  if (v) await clips(t, agentTab, 'agent tab', v);
  const snap = await koan(['snapshot', agentTab, '--interactive']);
  const ref = refOf(snap.out, /button "Press me"/);
  const f = path.join(AGENT_HOME, 'act-micro.png');
  const r = await koan(['act', agentTab, 'click', ref ?? '@0', '--screenshot', 'micro', '-o', f]);
  const line = r.out.match(LINE);
  t.check('act click --screenshot micro: "ok", then the screenshot\'s line, in one answer',
          r.code === 0 && /^ok$/m.test(r.out) && !!line && Math.max(...(pngSize(f) ?? [999])) <= 320, r.out);
});

let watchRect = null;
await step('UF-SHT-4', 'The agent takes over the person\'s tab (a real click) and takes every size there', async t => {
  const user = await newTab(fixtures.url('/watch?user=1'));
  await show(user);
  await parkPointer();
  const res = await takeOver(t);
  taken = res.tab;
  t.check('take-over approved with a real click on Allow', res.r.code === 0 && tabIdOk(taken) && res.approval?.real,
          res.approval?.why ?? brief(res.r));
  await sleep(1500);
  const vs = await viewTree();
  const web = webviewOf(vs);
  const r = web && await viewRect(web, vs);
  const b = await windowBounds();
  if (r && b) watchRect = { x: Math.round(r.x - b.left) + 4, y: Math.round(r.y - b.top) + 60, w: Math.round(r.w) - 8,
                            h: Math.round(r.h) - 120 };
  // The person's page must not change at all while the agent captures it.
  const t0 = Date.now();
  mark('capture-user-tab', { stillRect: watchRect }, true);
  const v = await sizes(t, taken, 'taken-over tab');
  if (v) await clips(t, taken, 'taken-over tab', v);
  mark('capture-user-tab-end', { stillRect: watchRect, still: { from: t0, to: Date.now() } });
  const seen = JSON.parse(await evalIn(await pageById(taken), 'JSON.stringify(window.koanSeen)') ?? 'null');
  t.check('the person\'s page saw no resize, no devicePixelRatio change and no media query change',
          !!seen && seen.resize === 0 && seen.viewport === 0 && seen.dpr === 0 && seen.media === 0 &&
          seen.dprAtLoad === v?.dpr, JSON.stringify(seen));
  await t.shot('after-captures');
}, { needs: ['real input', 'recording'] });

await step('UF-SHT-5', 'The control for the flicker check: the page changes on purpose', async t => {
  const p = await pageById(taken);
  const t0 = Date.now();
  mark('still-control', { stillRect: watchRect, control: true }, true);
  for (const c of ['#fde2e2', '#e2f0fd', '#ffffff']) {
    await evalIn(p, `document.body.style.background = '${c}'`);
    await sleep(400);
  }
  mark('still-control-end', { stillRect: watchRect, control: true, still: { from: t0, to: Date.now() } });
  t.check('the page changed colour three times (for the recording)', true);
  const r = await koan(['release', taken]);
  t.check('koan release', r.code === 0, brief(r));
  await show(anchor.id);
  const e = await koan(['end']);
  t.check('koan end', e.code === 0, brief(e));
}, { needs: ['recording'] });

end();
