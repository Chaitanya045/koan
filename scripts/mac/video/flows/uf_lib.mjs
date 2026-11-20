// What the user flows (uf_*.mjs, .agent/notes/user-flows.md) share, on top
// of common.mjs (the gates, the marks, real input):
// - steps: step(id, title, fn) writes an action mark (frames.sh cuts there),
//   runs fn with a context whose check(name, ok, detail) records what the
//   user would see, and whose shot(label) takes a screenshot of our Koan's
//   window's rectangle (screencapture -R, then compress.sh: within
//   1920x1080, PNG). A step
//   with no check, or a check that failed, or one that threw, is a FAIL;
//   nothing passes silently;
// - the summary: $UF_OUT/summary.json, written after every step and on
//   exit (also when a gate stops the flow): per step pass/fail, checks,
//   timings, screenshots, and the agent's koan calls and wall time;
// - the fixture pages (scripts/e2e/pages/uf_*.html and a few endpoints: a
//   504, a server that never answers, a slow fetch, the signup's /done);
// - where things are on the screen: the browser window's root view (the
//   real pointer, calibrate()), another widget's origin (a dialog, the
//   agent pill), an element in the page;
// - real clicks on views and page elements, real keys (all through
//   common.mjs's gates);
// - the agent: the real `koan` CLI from the app bundle (KOAN_CLI) with the
//   flow's own HOME (KOAN_AGENT_HOME) against our Koan's profile, every
//   call counted; pairing and take-over approved with a REAL click on the
//   dialog's Allow (a UI DevTools press only as a fallback, which fails the
//   step: it would skip the macOS hit test).
//
// Environment (run_flows.sh sets it): launch.sh's KOAN_PID UI_PORT CDP_PORT
// KOAN_PROFILE KOAN_LOG KOAN_MAC_TOOLS KOAN_INPUT, plus KOAN_CLI,
// KOAN_AGENT_HOME, UF_OUT (this flow's output dir) and MARKS.
import { execFileSync, spawn } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { UI_PORT, browserCall, connect, decodePng, evalIn, onPage, pages, sleep, viewTree,
         waitFor } from '../../../e2e/lib/koan.mjs';
import { KOAN_PID, calibrate, clickNativeMenuItem, closeNativeMenu, makeKey, realInput, rootMouse, rootOf,
         waitNativeMenu, windowOf } from '../../../e2e/lib/mac.mjs';
import { TOOLS, clickAt, finish as commonFinish, keyPress, mark, out, pointer, save, setRound,
         startFlow } from './common.mjs';

const here = path.dirname(new URL(import.meta.url).pathname);
export const ROOT = path.resolve(here, '../../../..');
const COMPRESS = path.join(ROOT, 'scripts/mac/video/compress.sh');
export const CLI = process.env.KOAN_CLI;
export const AGENT_HOME = process.env.KOAN_AGENT_HOME;
export const PROFILE = process.env.KOAN_PROFILE;
export { keyPress, mark, pointer, sleep, viewTree, waitFor, evalIn, pages, browserCall };

// --- The summary.
let UF_OUT = null;
const summary = {
  flow: null, title: null, feature: null, startedAt: Date.now(), endedAt: null, wallMs: 0,
  status: 'running', stopped: null, steps: [],
  agent: { calls: 0, ms: 0, commands: [] }, info: [],
};
let current = null;
let shotN = 0;
function writeSummary() {
  if (!UF_OUT) return;
  summary.endedAt = Date.now();
  summary.wallMs = summary.endedAt - summary.startedAt;
  fs.writeFileSync(path.join(UF_OUT, 'summary.json'), JSON.stringify(summary, null, 1));
}
process.on('exit', code => {
  if (summary.status === 'running') {
    summary.status = out.stopped ? 'stopped' : code === 0 ? 'pass' : 'fail';
    summary.stopped = out.stopped ?? (code === 0 ? null : `exited ${code}`);
  }
  writeSummary();
});

// Starts the flow: the output dir, the gates (common.mjs startFlow: our
// Koan on the CDP port, the user away, real input), the fixture server.
// Returns the fixtures ({origin, url(path), port}).
export async function begin(name, title, feature) {
  UF_OUT = process.env.UF_OUT ?? path.resolve(`${name}-${Date.now()}`);
  fs.mkdirSync(path.join(UF_OUT, 'shots'), { recursive: true });
  process.env.MARKS ??= path.join(UF_OUT, 'marks.json');
  Object.assign(summary, { flow: name, title, feature });
  writeSummary();
  startFlow(name);
  return startFixtures();
}
export function info(line) {
  summary.info.push(line);
  console.log(`INFO  ${line}`);
  writeSummary();
}
// Skips the whole flow (exit 77): the feature isn't in this build, or a
// tool is missing.
export function skipFlow(why) {
  summary.status = 'skip';
  summary.stopped = why;
  console.log(`SKIP  ${summary.flow}: ${why}`);
  writeSummary();
  end(77);
}

// step(id, title, fn, {needs}): `needs` says what the step needs from the
// bench ('real input', 'recording'), for the catalogue and the index.
export async function step(id, title, fn, { needs = [] } = {}) {
  setRound(summary.steps.length + 1);
  const rec = { id, title, needs, ok: false, skipped: null, checks: [], shots: [], startedAt: Date.now(),
                ms: 0, agent: { calls: 0, ms: 0 }, detail: '' };
  summary.steps.push(rec);
  current = rec;
  mark(`step:${id}`, { title }, true);
  console.log(`--- ${id}: ${title}`);
  const ctx = {
    check(name, ok, detail = '') {
      const c = { name, ok: !!ok, detail: String(detail ?? '').slice(0, 600) };
      rec.checks.push(c);
      console.log(`  ${c.ok ? 'ok  ' : 'FAIL'} ${name}${c.ok || !c.detail ? '' : ` -- ${c.detail}`}`);
      writeSummary();
      return c.ok;
    },
    skip(why) {
      rec.skipped = why;
    },
    shot: (label, opts) => shot(rec, label, opts),
    info: line => {
      rec.detail = rec.detail ? `${rec.detail}; ${line}` : line;
      console.log(`  info ${line}`);
    },
  };
  try {
    await fn(ctx);
  } catch (e) {
    ctx.check(`the step ran to its end`, false, String(e?.stack ?? e).split('\n').slice(0, 3).join(' | '));
  }
  rec.ms = Date.now() - rec.startedAt;
  if (rec.skipped && rec.checks.every(c => c.ok)) {
    rec.ok = null;
    console.log(`SKIP  ${id}: ${rec.skipped}`);
  } else {
    if (!rec.checks.length) ctx.check('the step checked something', false, 'no check ran');
    rec.ok = rec.checks.every(c => c.ok);
    const first = rec.checks.find(c => !c.ok);
    console.log(rec.ok ? `PASS  ${id} (${(rec.ms / 1000).toFixed(1)} s)`
                       : `FAIL  ${id}: ${first.name}${first.detail ? ` -- ${first.detail}` : ''}`);
  }
  mark(`step-end:${id}`, { ok: rec.ok });
  current = null;
  writeSummary();
  return rec.ok;
}
// Ends the flow: PASS only if every step passed (skips aside) and at least
// one ran.
export function end(code) {
  commonFinish();
  fixtureServer?.close();
  if (code === undefined) {
    const ran = summary.steps.filter(s => s.ok !== null);
    code = ran.length && ran.every(s => s.ok) ? 0 : 1;
    summary.status = code === 0 ? 'pass' : 'fail';
  }
  const failed = summary.steps.filter(s => s.ok === false).map(s => s.id);
  console.log(`${summary.flow}: ${summary.steps.length} steps, ${failed.length} failed` +
              `${failed.length ? ` (${failed.join(', ')})` : ''}; agent: ${summary.agent.calls} koan calls, ` +
              `${(summary.agent.ms / 1000).toFixed(1)} s`);
  writeSummary();
  process.exit(code);
}

// --- Screenshots: the window as the screen has it, compressed for review.
// The window's rectangle of the screen (`screencapture -R`), not the window
// alone (-l): the agent pill, its border, the corner dot, toasts and
// dialog sheets are child windows of their own, which a capture of the
// browser window would leave out. The flows' gates keep our Koan in front.
let anchorId = null;
export const setAnchor = id => { anchorId = id; };
async function windowBounds(targetId = anchorId) {
  return targetId ? (await windowOf(targetId).catch(() => null))?.bounds : null;
}
// shot(rec, label, {pixels, rect, overview}): the window (or `rect`, screen
// points) to shots/NN-<step>-<label>.png within 1920x1080 (overview: 720).
// With `pixels`, also returns the capture decoded at full resolution
// ({frame, scale}: device pixels per point, rect: the captured rect).
async function shot(rec, label, { pixels = false, rect = null, overview = false } = {}) {
  const b = await windowBounds();
  const area = rect ?? (b && { x: b.left, y: b.top, w: b.width, h: b.height });
  const name = `${String(++shotN).padStart(2, '0')}-${rec.id}-${label}`.replace(/[^\w.-]+/g, '_');
  const raw = path.join(UF_OUT, 'shots', `${name}.raw.png`);
  const dst = path.join(UF_OUT, 'shots', `${name}.png`);
  try {
    if (!area) throw new Error('no window bounds');
    const how = `rect ${area.x},${area.y},${area.w},${area.h}`;
    execFileSync('screencapture', ['-x', `-R${area.x},${area.y},${area.w},${area.h}`, raw],
                 { stdio: 'ignore', timeout: 15000 });
    execFileSync('sips', ['--matchTo', '/System/Library/ColorSync/Profiles/sRGB Profile.icc', raw],
                 { stdio: 'ignore', timeout: 15000 });
    let result = { path: path.relative(UF_OUT, dst), label, how };
    if (pixels) {
      const frame = decodePng(raw);
      result = { ...result, frame, scale: frame.width / area.w, rect: area };
    }
    execFileSync(COMPRESS, ['-o', dst, ...(overview ? ['--720'] : []), raw], { stdio: 'ignore', timeout: 60000 });
    if (!process.env.UF_KEEP_RAW) fs.rmSync(raw, { force: true });
    rec.shots.push({ path: result.path, label, how });
    mark(`shot:${label}`, { path: result.path });
    writeSummary();
    return result;
  } catch (e) {
    const why = String(e.message ?? e).split('\n')[0];
    rec.shots.push({ path: null, label, error: why });
    console.log(`  (screenshot ${label} failed: ${why})`);
    return { path: null, error: why };
  }
}
export const shotOutside = (label, opts) => shot(current ?? { id: 'flow', shots: [] }, label, opts);

// Pixel helpers over shot({pixels}) results: `r` is in screen points.
// The mean colour and how many pixels differ from `ref` (an [r,g,b]) by more
// than `tol` in any channel.
export function regionStats(s, r, ref = null, tol = 40) {
  const { frame, scale, rect } = s;
  const x0 = Math.max(0, Math.round((r.x - rect.x) * scale)), y0 = Math.max(0, Math.round((r.y - rect.y) * scale));
  const x1 = Math.min(frame.width, Math.round((r.x + r.w - rect.x) * scale));
  const y1 = Math.min(frame.height, Math.round((r.y + r.h - rect.y) * scale));
  let n = 0, off = 0;
  const sum = [0, 0, 0];
  for (let y = y0; y < y1; y++) {
    for (let x = x0; x < x1; x++) {
      const p = frame.pixel(x, y);
      sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2];
      n++;
      if (ref && Math.max(Math.abs(p[0] - ref[0]), Math.abs(p[1] - ref[1]), Math.abs(p[2] - ref[2])) > tol) off++;
    }
  }
  return { n, off, mean: n ? sum.map(v => Math.round(v / n)) : null };
}
// Pixels that differ between two shots of the same rect, inside `r`; with
// `lines` ('rows' or 'cols'), also the rows (columns) of `r` with any pixel
// changed (a thin strip across a border of several lines of colour).
export function diffPixels(a, b, r, tol = 24, lines = null) {
  const { scale, rect } = a;
  let n = 0, changed = 0;
  const hit = new Map();
  const x0 = Math.round((r.x - rect.x) * scale), y0 = Math.round((r.y - rect.y) * scale);
  const x1 = Math.round((r.x + r.w - rect.x) * scale), y1 = Math.round((r.y + r.h - rect.y) * scale);
  for (let y = Math.max(0, y0); y < Math.min(a.frame.height, b.frame.height, y1); y++) {
    for (let x = Math.max(0, x0); x < Math.min(a.frame.width, b.frame.width, x1); x++) {
      const p = a.frame.pixel(x, y), q = b.frame.pixel(x, y);
      n++;
      const line = lines === 'cols' ? x : y;
      if (!hit.has(line)) hit.set(line, false);
      if (Math.max(Math.abs(p[0] - q[0]), Math.abs(p[1] - q[1]), Math.abs(p[2] - q[2])) > tol) {
        changed++;
        hit.set(line, true);
      }
    }
  }
  return { n, changed, lines: hit.size, linesChanged: [...hit.values()].filter(Boolean).length };
}

// --- Fixture pages.
let fixtureServer = null;
export const signups = [];
const PAGES = path.join(ROOT, 'scripts/e2e/pages');
const page = name => fs.readFileSync(path.join(PAGES, name), 'utf8');
export async function startFixtures() {
  fixtureServer = http.createServer((req, res) => {
    const u = new URL(req.url, 'http://x');
    const send = (status, html, type = 'text/html; charset=utf-8') => {
      res.writeHead(status, { 'content-type': type, 'cache-control': 'no-store' });
      res.end(html);
    };
    if (/^\/signup\/[123]$/.test(u.pathname)) return send(200, page('uf_signup.html'));
    if (u.pathname === '/signup/done') {
      signups.push(Object.fromEntries(u.searchParams));
      const who = (u.searchParams.get('first') ?? '').replace(/[^\w -]/g, '');
      return send(200, `<!doctype html><title>Welcome</title><h1>Welcome, ${who}</h1>` +
                       '<p role=status>Your account is ready.</p>');
    }
    if (u.pathname === '/blank') return send(200, '<!doctype html><title>Blank</title><body style="background:#fff">');
    if (u.pathname === '/canvas') return send(200, page('uf_canvas.html'));
    if (u.pathname === '/secret') return send(200, page('uf_secret.html'));
    if (u.pathname === '/watch') return send(200, page('agent_capture_watch.html'));
    if (u.pathname === '/504') return send(504, '<!doctype html><title>Gateway</title><h1>Upstream timed out</h1>');
    if (u.pathname === '/hang') return;  // never answers (until the server closes)
    if (u.pathname === '/slow-data') {
      setTimeout(() => send(200, 'Loaded late', 'text/plain'), 1500);
      return;
    }
    const t = (u.searchParams.get('t') ?? 'Page').replace(/[^\w .-]/g, '');
    const bg = (u.searchParams.get('b') ?? '#ffffff').replace(/[^#\w]/g, '');
    send(200, `<!doctype html><title>${t}</title><body style="margin:0;background:${bg};` +
              `font:24px -apple-system,sans-serif"><h1 style="margin:40px">${t}</h1>` +
              '<p style="margin:40px">A plain page for the user flows.</p>');
  });
  fixtureServer.keepAliveTimeout = 1000;
  await new Promise(r => fixtureServer.listen(0, '127.0.0.1', r));
  const origin = `http://127.0.0.1:${fixtureServer.address().port}`;
  return { origin, url: p => `${origin}${p}`, port: fixtureServer.address().port };
}

// --- The browser window and the screen.
export const findPage = async test => (await pages()).find(test);
export const pageById = async id => (await pages()).find(p => p.id === id);
// Opens `url` as the anchor tab (the launch's about:blank closed), makes its
// window key and measures where its root view is on the screen.
let rootDelta = null;  // the root view's origin minus the window's, points
export async function openAnchor(url) {
  const id = (await browserCall('Target.createTarget', { url })).result?.targetId;
  const anchor = await waitFor(() => pageById(id), 8000);
  for (const p of await pages()) {
    if (p.url === 'about:blank') await browserCall('Target.closeTarget', { targetId: p.id });
  }
  if (!anchor) throw new Error('no anchor page');
  setAnchor(anchor.id);
  await sleep(1200);
  await recalibrate(anchor);
  const b = await windowBounds();
  out.window = b && { x: b.left, y: b.top, w: b.width, h: b.height };
  save();
  return anchor;
}
export async function recalibrate(page) {
  await makeKey(page);
  const vs = await viewTree();
  const w = await windowBounds(page.id);
  mark('calibrate', {}, true);
  const o = await calibrate(page, vs);
  if (o && w) rootDelta = { dx: o.x - w.left, dy: o.y - w.top };
  mark('calibrated', { rootDelta });
  return rootDelta;
}
// The browser window's widget (the one with a BrowserView).
const browserWidget = vs => vs.find(v => v.cls === 'BrowserView')?.widget;
// A widget's origin on the screen (points), from UI DevTools: the nth widget
// named `wname` (viewTree's wname and wn).
async function widgetOrigin(wname, wn) {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  try {
    await ui.send('DOM.enable');
    await ui.send('CSS.enable');
    const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
    const found = [];
    const walk = n => {
      const a = n.attributes ?? [];
      if (n.nodeName === 'Widget' && (a[a.indexOf('name') + 1] ?? '') === wname) found.push(n);
      for (const c of n.children ?? []) walk(c);
    };
    walk(doc);
    const node = found[wn ?? 0];
    if (!node) return null;
    const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: node.nodeId });
    const p = {};
    for (const rule of r.result?.matchedCSSRules ?? []) {
      for (const prop of rule.rule.style.cssProperties) p[prop.name.toLowerCase()] = prop.value;
    }
    return Number.isFinite(+p.x) && Number.isFinite(+p.y) ? { x: +p.x, y: +p.y } : null;
  } finally {
    ui.close();
  }
}
// Where `view` (from viewTree()) is on the screen: {x, y, w, h, via}.
// The browser window's views from the measured root origin; other widgets'
// (dialogs, the pill, the chip's menu button) from the widget's origin.
export async function viewRect(view, vs) {
  vs ??= await viewTree();
  if (view.widget === browserWidget(vs) && rootDelta) {
    const b = await windowBounds();
    if (b) return { x: b.left + rootDelta.dx + view.ax, y: b.top + rootDelta.dy + view.ay, w: view.w, h: view.h,
                    via: 'root' };
  }
  const o = await widgetOrigin(view.wname, view.wn);
  return o && { x: o.x + view.ax, y: o.y + view.ay, w: view.w, h: view.h, via: 'widget' };
}
const centre = r => [Math.round(r.x + r.w / 2), Math.round(r.y + r.h / 2)];
// A real click at the centre of `view`; {ok, at, via} or {ok: false, why}.
export async function clickView(view, vs, settle = 700) {
  const r = await viewRect(view, vs);
  if (!r) return { ok: false, why: `no screen position for ${view.cls} (widget ${view.wname})` };
  const b = await windowBounds();
  const [x, y] = centre(r);
  // A view of ours is on the window, or near it (a dialog sheet, a menu).
  if (b && (x < b.left - 400 || y < b.top - 400 || x > b.left + b.width + 400 || y > b.top + b.height + 400)) {
    return { ok: false, why: `${view.cls} at ${x},${y} is nowhere near the window (${JSON.stringify(b)})` };
  }
  await clickAt(x, y);
  await sleep(settle);
  return { ok: true, at: [x, y], via: r.via };
}
export async function hoverView(view, vs, ms = 200) {
  const r = await viewRect(view, vs);
  if (!r) return false;
  await pointer('move', ...centre(r), ms);
  await sleep(400);
  return true;
}
// The page's content view in the browser window: the biggest shown
// ContentsWebView.
export const webviewOf = vs => vs.filter(v => v.cls === 'ContentsWebView' && v.shown && v.w > 100 &&
                                              v.widget === browserWidget(vs))
    .sort((a, b) => b.w * b.h - a.w * a.h)[0];
// Where an element of `page` (shown in the window) is on the screen:
// `expr` is a JS expression giving the element (scrolled into view first).
export async function elementRect(page, expr, vs) {
  vs ??= await viewTree();
  const web = webviewOf(vs);
  const b = await windowBounds();
  const r = await evalIn(page, `(() => { const e = (${expr}); if (!e) return null;
      e.scrollIntoView({block: 'center', inline: 'center'});
      const r = e.getBoundingClientRect(); return {x: r.x, y: r.y, w: r.width, h: r.height}; })()`);
  if (!r || !web || !b || !rootDelta) return null;
  return { x: b.left + rootDelta.dx + web.ax + r.x, y: b.top + rootDelta.dy + web.ay + r.y, w: r.w, h: r.h };
}
export async function clickElement(page, expr, settle = 700) {
  const r = await elementRect(page, expr);
  if (!r) return { ok: false, why: `no element or no calibration for ${expr}` };
  await clickAt(...centre(r));
  await sleep(settle);
  return { ok: true, at: centre(r) };
}
// Moves the real pointer over the page, away from the sidebar and top bar
// (upstream keeps reveals out under the real pointer).
export async function parkPointer() {
  const b = await windowBounds();
  if (b) await pointer('move', Math.round(b.left + b.width * 0.62), Math.round(b.top + b.height * 0.7), 150);
  await sleep(300);
}
export const show = async targetId => {
  await browserCall('Target.activateTarget', { targetId });
  await sleep(600);
};
export async function newTab(url, background = false) {
  const id = (await browserCall('Target.createTarget', { url, background })).result?.targetId;
  await waitFor(async () => (await pageById(id))?.url?.startsWith('http'), 8000);
  await sleep(800);
  return id;
}

// --- The agent: the real koan CLI, counted.
const agentEnv = home => ({ ...process.env, HOME: home ?? AGENT_HOME, KOAN_USER_DATA_DIR: PROFILE });
export function koan(args, { timeout = 60000, whileRunning, home, input } = {}) {
  const t0 = Date.now();
  return new Promise(resolve => {
    const child = spawn(CLI, args, { env: agentEnv(home), stdio: [input === undefined ? 'ignore' : 'pipe',
                                                                    'pipe', 'pipe'] });
    let o = '', e = '';
    child.stdout.on('data', d => (o += d));
    child.stderr.on('data', d => (e += d));
    if (input !== undefined) child.stdin.end(input);
    const timer = setTimeout(() => child.kill('SIGTERM'), timeout);
    const running = whileRunning?.(() => o + e).catch(err => console.log(`  (${err.message})`));
    child.on('close', async code => {
      clearTimeout(timer);
      await running;
      const ms = Date.now() - t0;
      summary.agent.calls++;
      summary.agent.ms += ms;
      summary.agent.commands.push({ args: args.slice(0, 6).map(a => String(a).slice(0, 60)), code, ms });
      if (current) {
        current.agent.calls++;
        current.agent.ms += ms;
      }
      resolve({ code, out: o.trim(), err: e.trim(), ms });
    });
  });
}
export const spawnKoan = (args, home) => spawn(CLI, args, { env: agentEnv(home), stdio: ['ignore', 'pipe', 'pipe'] });
export const brief = r => `exit ${r.code}; ${(r.err || r.out).split('\n')[0].slice(0, 200)}`;
export const lastLine = r => r.out.split('\n').at(-1);
export const refOf = (text, re) => text.split('\n').find(l => re.test(l))?.match(/@\d+/)?.[0];
export const loc = (role, name) => `loc:role=${role}[name="${name}"]`;
export const tabIdOk = id => /^[0-9A-Fa-f]{8,}$/.test(id ?? '');

// Approves the dialog whose title matches `title` with a REAL click on its
// Allow button, once koan says it waits in Koan. The dialog is shot first
// (`ctx.shot`). Not polled with UI DevTools after the click: the indicator
// makes widgets then, and a widget made while a UI DevTools session starts
// crashes the browser on macOS (lib/agent.mjs). `done(printed)` says the
// command got its answer. Falls back to a UI DevTools press, which the
// caller reports as a failure ({real: false, why}).
export async function approve(ctx, title, printed, done, { shotLabel = 'dialog', inspect } = {}) {
  if (!await waitFor(() => / in Koan\.\.\./.test(printed()), 30000)) {
    return { real: false, approved: false, why: 'koan never said it waits in Koan' };
  }
  await sleep(1000);
  const found = await waitFor(async () => {
    const vs = await viewTree();
    const label = vs.find(v => v.shown && title.test(v.props.Text ?? ''));
    if (!label) return null;
    const button = vs.find(v => v.widget === label.widget && v.shown && v.cls === 'MdTextButton' &&
                                v.props.Text === 'Allow');
    return button && { vs, button, dialog: vs.filter(v => v.widget === label.widget) };
  }, 20000);
  if (!found) return { real: false, approved: false, why: 'no dialog with that title and an Allow button' };
  const seen = inspect?.(found.dialog);
  if (ctx) await ctx.shot(shotLabel);
  // Dialogs ignore a click right after they show (InputEventActivationProtector).
  await sleep(1500);
  let why = '';
  if (realInput()) {
    const c = await clickView(found.button, found.vs, 300);
    if (c.ok) {
      if (await waitFor(() => done(printed()), 10000)) return { real: true, approved: true, at: c.at, seen };
      why = `a real click at ${c.at} (${c.via}) did not answer the dialog in 10 s`;
    } else {
      why = c.why;
    }
  } else {
    why = 'real input is off';
  }
  // The fallback: the press through the dialog's root view (skips AppKit's
  // hit test; the step fails on it).
  const vs = await viewTree();
  const button = vs.find(v => v.shown && v.cls === 'MdTextButton' && v.props.Text === 'Allow' &&
                              v.widget === found.button.widget);
  if (!button) return { real: false, approved: !!done(printed()), why: `${why}; the dialog is gone` };
  await rootMouse(rootOf({ views: vs.filter(v => v.widget === button.widget) }), ['mousePressed', 'mouseReleased'],
                  button.ax + button.w / 2, button.ay + button.h / 2);
  const ok = await waitFor(() => done(printed()), 10000);
  return { real: false, approved: !!ok, why: `${why}; approved through UI DevTools instead`, seen };
}
// Pairs the flow's agent (every capability by default, as `koan pair`
// asks): {r, approval}.
export async function pair(ctx, label = 'UF agent', extra = []) {
  await waitFor(() => fs.existsSync(path.join(PROFILE, 'AgentControl', 'agent.json')), 15000);
  let approval = null;
  const r = await koan(['pair', '--label', label, ...extra], {
    timeout: 120000,
    whileRunning: async printed => {
      approval = await approve(ctx, /^Allow a program to control Koan\?$/, printed, t => /paired as/.test(t), {
        // The capabilities are check rows ("Also list my tabs", ...:
        // DialogModelCheckbox, a views::Checkbox).
        inspect: dialog => ({
          toggles: dialog.filter(v => v.shown && /Toggle|Checkbox/.test(v.cls)).length,
          labels: dialog.filter(v => v.shown && v.cls === 'Label').map(v => v.props.Text).filter(Boolean),
        }) });
    } });
  return { r, approval };
}
// koan take-over of the tab in front, approved with a real click.
export async function takeOver(ctx) {
  let approval = null;
  const r = await koan(['take-over'], {
    timeout: 120000,
    whileRunning: async printed => {
      approval = await approve(ctx, / wants to control a tab$/, printed,
                               t => /^[0-9A-Fa-f]{8,}$/m.test(t), { shotLabel: 'take-over-dialog' });
    } });
  return { r, approval, tab: lastLine(r) };
}
export async function tabEntry(id) {
  const t = await koan(['tabs', '--json']);
  try {
    return JSON.parse(t.out).tabs.find(e => e.targetId === id || e.targetId?.startsWith(id)) ?? null;
  } catch {
    return null;
  }
}

// --- What the indicator shows (read after a change has settled: see
// approve()).
export async function indicator(settle = 1500) {
  await sleep(settle);
  const vs = await viewTree();
  const shown = cls => vs.filter(v => v.cls === cls && v.shown);
  const pill = shown('KoanAgentIndicatorView')[0];
  const pillText = pill ? vs.filter(v => v.widget === pill.widget && v.shown && v.cls === 'Label')
                            .map(v => v.props.Text).join(' ') : '';
  const pillButtons = pill ? vs.filter(v => v.widget === pill.widget && v.shown && v.cls === 'LabelButton') : [];
  const chip = shown('KoanAgentChipButton')[0];
  const chipText = chip ? vs.filter(v => v.widget === chip.widget && v.shown && v.cls === 'Label' &&
                                         v.path.includes('KoanAgentChipButton')).map(v => v.props.Text).join(' ')
                        : '';
  return {
    vs, pill: !!pill, pillView: pill, pillText, pillButtons, buttons: pillButtons.map(v => v.props.Text),
    border: shown('KoanAgentBorderView')[0] ?? null,
    chip: chip && chip.w > 0 ? chip : null, chipText, chipTip: chip?.props.TooltipText ?? '',
    corner: shown('KoanAgentCornerDot')[0] ?? null,
    toasts: vs.filter(v => v.shown && v.cls === 'Label' &&
                           /connected|disconnected|paused/.test(v.props.Text ?? '')).map(v => v.props.Text),
  };
}
export const sayIndicator = s => JSON.stringify({ pill: s.pill, pillText: s.pillText, buttons: s.buttons,
                                                  border: !!s.border, chip: !!s.chip, chipText: s.chipText,
                                                  corner: !!s.corner, toasts: s.toasts });
// A real click on the pill's button labelled `text`.
export async function clickPill(text) {
  const vs = await viewTree();
  const button = vs.find(v => v.shown && v.cls === 'LabelButton' && v.props.Text === text &&
                              v.wname === 'KoanAgentIndicator');
  if (!button) return { ok: false, why: `no "${text}" in the pill` };
  return clickView(button, vs, 900);
}

// How the person reaches an agent's tab (its own tabs are in the agent's
// Space, design 2026-11-10): a real click on the sidebar chip, then a real
// click on the menu's "Go to <title> (<agent>...)", which switches to the
// tab's Space and selects it. Never CDP: Koan holds a CDP activation of a
// tab an agent holds (#232) as a "focus" prompt. With the sidebar hidden,
// `from` is the corner dot (KoanAgentCornerDot), which opens the same menu.
// {ok, why, items}.
export async function goToAgentTab(title, settle = 900, from = 'KoanAgentChipButton') {
  const vs = await viewTree();
  const chip = vs.find(v => v.cls === from && v.shown && v.w > 0);
  if (!chip) return { ok: false, why: `no ${from} on screen`, items: [] };
  const c = await clickView(chip, vs, 300);
  if (!c.ok) return { ok: false, why: `chip: ${c.why}`, items: [] };
  const menu = await waitNativeMenu(true, undefined, 4000);
  const items = menu?.items ?? [];
  if (!menu) return { ok: false, why: 'the chip\'s menu did not open', items };
  const want = `Go to ${title} (`;
  const item = items.find(i => (i ?? '').startsWith(want));
  if (!item) {
    await closeNativeMenu();
    return { ok: false, why: `no "${want}..." item`, items };
  }
  const picked = await clickNativeMenuItem(item, { click: async (x, y) => { await clickAt(x, y); return true; } });
  await sleep(settle);
  return { ok: picked.ok, why: picked.detail, items, item };
}

// The page's rates (uf_canvas.html's window.__rates(from, to)) through the
// harness's own DevTools connection, which takes no capturer.
export async function rates(targetId, from, to) {
  const p = await pageById(targetId);
  const v = p && await evalIn(p, `window.__rates(${from}, ${to})`);
  const secs = (to - from) / 1000;
  return v && { ...v, rafPerS: +(v.raf / secs).toFixed(1), timersPerS: +(v.timers / secs).toFixed(1) };
}
export const visibility = async targetId => {
  const p = await pageById(targetId);
  return p ? evalIn(p, 'document.visibilityState') : 'missing';
};

// Koan's power assertions for agents (`pmset -g assertions`), by type.
export const WAKE_REASON = 'Koan: an agent is working';
export function koanAssertions() {
  try {
    return execFileSync('pmset', ['-g', 'assertions'], { encoding: 'utf8' }).split('\n')
        .filter(l => l.includes(`"${WAKE_REASON}"`))
        .map(l => (l.match(/\b(Prevent\w+|NoIdleSleepAssertion|NoDisplaySleepAssertion)\b/) ?? [])[1] ?? 'unknown');
  } catch {
    return null;
  }
}
export function lowPowerOnBattery() {
  try {
    return /'Battery Power'/.test(execFileSync('pmset', ['-g', 'batt'], { encoding: 'utf8' })) &&
           /lowpowermode\s+1/.test(execFileSync('pmset', ['-g'], { encoding: 'utf8' }));
  } catch {
    return false;
  }
}

// Our own window over Koan's (scripts/mac/tools/cover.m; it quits when its
// stdin closes or after `secs`).
export async function cover(b, secs = 60) {
  let bin = TOOLS && `${TOOLS}/cover`;
  if (!bin || !fs.existsSync(bin)) {
    bin = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'uf-cover-')), 'cover');
    execFileSync('clang', ['-fobjc-arc', '-framework', 'AppKit', '-o', bin,
                           path.join(ROOT, 'scripts/mac/tools/cover.m')], { stdio: 'ignore', timeout: 120000 });
  }
  const child = spawn(bin, [b.left, b.top, b.width, b.height, secs].map(String), { stdio: ['pipe', 'pipe', 'ignore'] });
  let said = '';
  child.stdout.on('data', d => (said += d));
  const ready = await waitFor(() => /ready/.test(said), 10000);
  return { ready: !!ready, close: () => { child.stdin.end(); child.kill('SIGTERM'); } };
}
export { windowBounds, KOAN_PID };
