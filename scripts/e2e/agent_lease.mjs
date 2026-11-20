// End-to-end test of the agent tab's keep-alive lease (#180, design
// .agent/notes/agent-control.md section 8; src/browser/agent/*lease*) on the
// Mac: an agent's background tab renders at full speed while an agent works
// in it, and the user's own background tabs stay throttled.
//
// The test page (/lease) runs a requestAnimationFrame counter, a CSS
// animation, a setInterval(..., 16) counter and a canvas that changes every
// frame; its heading says "raf=<n> timers=<n>", and it logs the time of every
// frame and timer tick. Three readings:
// - the agent's: `koan snapshot` (the heading) and `koan screenshot`, the
//   commands an agent runs. Each one holds the lease's capturer, and for 3 s
//   after (the hold-off), so a command every 0.6 s keeps the tab live;
// - the test's: the page's log over the harness's own DevTools connection
//   (Runtime.evaluate takes no capturer and shows nothing), for the exact
//   rates in a window, and for tabs the agent must not touch (the user's);
// - the CPU: `ps` CPU time of the tab's renderer over the window (the
//   renderer is found by a 0.4 s busy loop run in the page).
// Cases (a summary table at the end): the agent's background tab with the
// user's tab in front; the same with Koan's window covered by another app's
// window (scripts/mac/tools/cover.m, this test's own) and minimised (both
// need the Mac to itself: real input on, see suite.sh); release (3 s after
// the last command the tab is throttled again); the cap (6 agent tabs, the
// cap of 4 from local_state/agent_lease.json, halved on battery in Low Power
// Mode: exactly that many live, the others' commands wait); the user's own
// background tab with the same page, throttled throughout (the battery
// guarantee). Then the holds (the gaps #403 found), with Koan's
// window covered: a screencast through koan-relay (agent-browser's way in;
// a stand-in agent-browser takes the relay's URL) that is no longer acked,
// and `koan watch <tab>`, each keep the tab rendering for more than 10 s
// after the last command, and release it when they end (stopScreencast; the
// watch killed, its keep-alive lapsing). `pmset -g assertions` shows Koan's
// "Koan: an agent is working" assertion while a hold is on (display sleep,
// or idle sleep only on battery in Low Power Mode) and not after, and the
// chip's tooltip says "keeping the display awake". Last, Stop (the pill's
// button) ends everything at once: the assertion goes, the screencast's
// connection and the watch end, the agent's tabs close. Other Desktops, a
// locked screen, display sleep and Low Power Mode are
// scripts/mac/steps/agent_lease.md.
// macOS only (the CLI's holder needs the Mac's peer resolver). Run by
// scripts/mac/suite.sh.
import { execFileSync, spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { AGENT_HOME, CLI, PROFILE, allow, allowWhy, brief, koan, spawnKoan } from './lib/agent.mjs';
import { browserCall, check, decodePng, evalIn, finish, front, pages, sleep, viewTree, waitFor }
    from './lib/koan.mjs';
import { IS_MAC, KOAN_PID, alive, fatalInLog, realInput, realInputWhyNot, rootMouse, rootOf,
         selectTabRow, setWindowState, skip, skipped, startServer, windowOf } from './lib/mac.mjs';

if (!IS_MAC) skip('the agent CLI needs macOS (its holder resolves peers with LOCAL_PEERTOKEN)');
if (!CLI || !fs.existsSync(CLI)) skip(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);
if (!PROFILE || !AGENT_HOME) skip('KOAN_PROFILE and KOAN_AGENT_HOME are set by scripts/mac/suite.sh');

// Thresholds. Live: Chromium's frame rate (60 Hz, 30 under Energy Saver,
// which throttles every frame sink) and the 16 ms timer unthrottled (about
// 60/s). Throttled: no frames (a hidden widget has no BeginMainFrame) and
// background timer throttling (one wake-up a second).
const LIVE_PER_S = 25;
const THROTTLED_RAF_PER_S = 2;
const THROTTLED_TIMERS_PER_S = 3;

const LEASE_PAGE = `<!doctype html><title>Lease</title>
<style>
  body { margin: 0; font: 14px -apple-system, sans-serif; background: #fff }
  #c { position: absolute; left: 0; top: 0 }
  #box { position: absolute; left: 0; top: 140px; width: 40px; height: 40px; background: #e33;
         animation: slide 1s linear infinite alternate }
  @keyframes slide { from { transform: translateX(0) } to { transform: translateX(360px) } }
  h1 { position: absolute; top: 200px; left: 0; margin: 0; font-size: 20px }
</style>
<canvas id=c width=240 height=120></canvas><div id=box></div><h1 id=h>raf=0 timers=0</h1>
<script>
  const MAX = 30000, log = { raf: [], timer: [], vis: [], frozen: 0 };
  let raf = 0, timers = 0;
  const now = () => performance.timeOrigin + performance.now();
  const push = (list, t) => { list.push(t); if (list.length > MAX) list.splice(0, list.length - MAX); };
  const h = document.getElementById('h'), ctx = document.getElementById('c').getContext('2d');
  const show = () => { h.textContent = 'raf=' + raf + ' timers=' + timers; };
  function frame() {
    raf++;
    push(log.raf, now());
    ctx.fillStyle = 'hsl(' + (raf * 7) % 360 + ' 80% 50%)';
    ctx.fillRect(0, 0, 240, 120);
    ctx.fillStyle = '#000';
    ctx.font = '40px sans-serif';
    ctx.fillText(String(raf), 20, 75);
    show();
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
  setInterval(() => { timers++; push(log.timer, now()); show(); }, 16);
  document.addEventListener('visibilitychange', () => log.vis.push([now(), document.visibilityState]));
  document.addEventListener('freeze', () => log.frozen++);
  // Counts in [from, to) (epoch ms) and the longest gap between frames there.
  window.__lease = (from, to) => {
    const inside = list => list.filter(t => t >= from && t < to);
    const frames = inside(log.raf);
    let gap = frames.length ? Math.max(frames[0] - from, to - frames.at(-1)) : to - from;
    for (let i = 1; i < frames.length; i++) gap = Math.max(gap, frames[i] - frames[i - 1]);
    return { raf: frames.length, timers: inside(log.timer).length, gap: Math.round(gap),
             visibility: document.visibilityState, frozen: log.frozen, total: raf,
             width: innerWidth };
  };
</script>`;
const server = await startServer({ '/lease': LEASE_PAGE });
// The user's copy is on another site (localhost), so that it never shares a
// renderer process (and its priority vote) with the agent's tabs.
const userUrl = `http://localhost:${server.port}/lease?n=user`;
const agentUrl = n => server.url(`/lease?n=${n}`);

const rows = [];
const fmt = v => (v === undefined || v === null ? '-' : typeof v === 'number' ? v.toFixed(1) : v);
function row(name, { shots, raf, timers, cpu, ok }) {
  rows.push({ name, shots: shots === undefined ? '-' : shots ? 'yes' : 'no', raf, timers, cpu,
              verdict: ok === undefined ? 'info' : ok ? 'PASS' : 'FAIL' });
}

// --- Pairing (Allow pressed in Koan's dialog, as agent_cli does).
await waitFor(() => fs.existsSync(path.join(PROFILE, 'AgentControl', 'agent.json')), 15000);
let allowed = false;
let r = await koan(['pair', '--label', 'koan lease'], { timeout: 90000, whileRunning: async printed => {
  allowed = await allow(/^Allow a program to control Koan\?$/, printed);
} });
check('koan pair: allowed in Koan and paired', allowed && r.code === 0 && /paired as/.test(r.out),
      `${allowWhy()}; ${brief(r)}`);
if (r.code !== 0) {
  skipped('the lease cases', 'not paired');
  server.close();
  finish();
}

// --- The pages. The user's tab (the suite's start page) stays in front.
const frontPage = (await pages()).find(p => p.url.endsWith('/agent_lease.html')) ?? (await pages())[0];
const pageFor = async url => waitFor(async () => (await pages()).find(p => p.url === url), 10000);
await browserCall('Target.createTarget', { url: userUrl, background: true });
const userPage = await pageFor(userUrl);
r = await koan(['open', agentUrl('a')]);
const tabA = r.out.split('\n').at(-1);
check('koan open: the agent\'s tab', r.code === 0 && !!tabA, brief(r));
const agentA = await pageFor(agentUrl('a'));
await front(frontPage);
const userCreated = Date.now();
const windowId = (await windowOf(frontPage.id))?.windowId;
const visibility = page => evalIn(page, 'document.visibilityState');
check('Setup: the user\'s tab is in front, the agent\'s and the user\'s copy are background tabs',
      !!agentA && !!userPage && await visibility(frontPage) === 'visible' &&
      await visibility(agentA) === 'hidden' && await visibility(userPage) === 'hidden',
      `agent ${agentA ? await visibility(agentA) : 'missing'}, user copy ${
        userPage ? await visibility(userPage) : 'missing'}`);
if (!agentA || !userPage) {
  server.close();
  finish();
}

// The page's rates over [from, to) through the harness's connection.
async function rates(page, from, to) {
  const v = await evalIn(page, `window.__lease(${from}, ${to})`);
  const secs = (to - from) / 1000;
  return v && { ...v, rafPerS: v.raf / secs, timersPerS: v.timers / secs };
}

// The renderer of `page`: the one whose CPU time grows by a busy loop in it.
async function rendererOf(page) {
  const cpu = async () => new Map(((await browserCall('SystemInfo.getProcessInfo')).result?.processInfo ?? [])
      .filter(p => p.type === 'renderer').map(p => [p.id, p.cpuTime]));
  const before = await cpu();
  await evalIn(page, '(() => { const end = performance.now() + 400; while (performance.now() < end); })()');
  const after = await cpu();
  let best = 0, grew = 0;
  for (const [pid, t] of after) {
    if (before.has(pid) && t - before.get(pid) > grew) [best, grew] = [pid, t - before.get(pid)];
  }
  return grew > 0.05 ? best : 0;
}
// CPU seconds and priority per pid (`ps`; its TIME is [[h:]m:]s.ss).
function psSample(pids) {
  const out = {};
  if (!pids.length) return out;
  try {
    const text = execFileSync('ps', ['-o', 'pid=,time=,pri=', '-p', pids.join(',')], { encoding: 'utf8' });
    for (const line of text.trim().split('\n')) {
      const [pid, time, pri] = line.trim().split(/\s+/);
      out[pid] = { secs: time.split(':').reduce((a, part) => a * 60 + Number(part), 0), pri };
    }
  } catch {}
  return out;
}
const cpuPercent = (a, b, pid, ms) =>
  a[pid] && b[pid] ? ((b[pid].secs - a[pid].secs) * 100000) / ms : undefined;

const pidA = await rendererOf(agentA);
const pidUser = await rendererOf(userPage);
console.log(`INFO  renderers: agent tab ${pidA || 'unknown'}, user's copy ${pidUser || 'unknown'}` +
            ` (Koan ${KOAN_PID})`);

// An agent working in `tab` for about `ms`: a snapshot every 0.6 s, and
// three screenshots 0.5 s apart in the middle. Returns the agent's reading
// (from the snapshots' headings), the screenshots' verdict, the harness's
// rates for the window between the first and the last snapshot, and the CPU.
const SHOT_REGIONS = { canvas: [0, 0, 240, 120], animation: [0, 140, 400, 40] };
async function agentWorks(tab, page, ms, { label, others = [] } = {}) {
  const snaps = [], shots = [], errors = [];
  const snap = async () => {
    const s = await koan(['snapshot', tab], { timeout: 30000 });
    const m = s.out.match(/raf=(\d+) timers=(\d+)/);
    if (s.code !== 0 || !m) errors.push(brief(s));
    else snaps.push({ t: Date.now(), raf: Number(m[1]), timers: Number(m[2]) });
  };
  const shot = async i => {
    const file = path.join(AGENT_HOME, `lease-${label}-${i}.png`);
    const s = await koan(['screenshot', tab, '-o', file], { timeout: 30000 });
    if (s.code === 0 && fs.existsSync(file)) shots.push(file);
    else errors.push(brief(s));
  };
  await snap();
  const from = Date.now();
  const pids = [pidA, pidUser, ...others].filter(Boolean);
  const cpu0 = psSample(pids);
  for (let i = 0; i < 3; i++) {
    await shot(i);
    await sleep(500);
  }
  while (Date.now() - from < ms) {
    await sleep(600);
    await snap();
  }
  const to = Date.now();
  const cpu1 = psSample(pids);
  const first = snaps[0], last = snaps.at(-1);
  const secs = first && last && last.t > first.t ? (last.t - first.t) / 1000 : 0;
  const agent = secs ? { rafPerS: (last.raf - first.raf) / secs,
                         timersPerS: (last.timers - first.timers) / secs } : null;
  // Each region must change from one screenshot to the next.
  let differ = shots.length === 3;
  const width = (await rates(page, from, to))?.width || 0;
  try {
    const frames = shots.map(decodePng);
    const scale = width ? frames[0].width / width : 1;
    const digest = (f, [x, y, w, h]) => {
      let sum = 0;
      for (let j = 0; j < h; j += 4) {
        for (let i = 0; i < w; i += 4) {
          const [cr, cg, cb] = f.pixel(Math.floor((x + i) * scale), Math.floor((y + j) * scale));
          sum = (sum * 31 + cr * 7 + cg * 3 + cb) % 1000000007;
        }
      }
      return sum;
    };
    for (const region of Object.values(SHOT_REGIONS)) {
      const d = frames.map(f => digest(f, region));
      differ &&= d[0] !== d[1] && d[1] !== d[2];
    }
  } catch (e) {
    differ = false;
    errors.push(`screenshots: ${e.message}`);
  }
  return { agent, differ, harness: await rates(page, from, to), cpu0, cpu1, ms: to - from, errors,
           from, to };
}

// The rates of `page` while nothing touches it for `ms`.
async function idle(page, ms) {
  const from = Date.now();
  const pids = [pidA, pidUser].filter(Boolean);
  const cpu0 = psSample(pids);
  await sleep(ms);
  const to = Date.now();
  return { harness: await rates(page, from, to), cpu: cpuPercent(cpu0, psSample(pids), pidA, to - from) };
}

const userRows = [];
async function liveCase(name) {
  const w = await agentWorks(tabA, agentA, 3500, { label: name.replace(/\W+/g, '-') });
  const ok = !!w.agent && w.agent.rafPerS > LIVE_PER_S && w.agent.timersPerS > LIVE_PER_S &&
             w.harness?.rafPerS > LIVE_PER_S && w.differ;
  check(`${name}: the agent's tab renders at full speed (rAF and timers > ${LIVE_PER_S}/s, ` +
        `screenshots differ in the canvas and the CSS animation)`, ok,
        `agent: rAF ${fmt(w.agent?.rafPerS)}/s, timers ${fmt(w.agent?.timersPerS)}/s; page log: rAF ` +
        `${fmt(w.harness?.rafPerS)}/s, longest frame gap ${w.harness?.gap} ms, ${w.harness?.visibility}; ` +
        `screenshots differ: ${w.differ}${w.errors.length ? `; ${w.errors.join(' | ')}` : ''}`);
  const cpu = cpuPercent(w.cpu0, w.cpu1, pidA, w.ms);
  row(name, { shots: w.differ, raf: w.agent?.rafPerS, timers: w.agent?.timersPerS, cpu, ok });
  // The user's copy in the same window: throttled.
  const u = await rates(userPage, w.from, w.to);
  const uOk = !!u && u.rafPerS < THROTTLED_RAF_PER_S && u.timersPerS < THROTTLED_TIMERS_PER_S;
  check(`${name}: the user's own background tab stays throttled meanwhile`, uOk,
        `rAF ${fmt(u?.rafPerS)}/s, timers ${fmt(u?.timersPerS)}/s, ${u?.visibility}`);
  userRows.push([`user's tab during: ${name}`, { raf: u?.rafPerS, timers: u?.timersPerS,
                 cpu: cpuPercent(w.cpu0, w.cpu1, pidUser, w.ms), ok: uOk }]);
  console.log(`INFO  ${name}: renderer priority (ps pri) agent ${w.cpu1[pidA]?.pri ?? '-'}, ` +
              `user's copy ${w.cpu1[pidUser]?.pri ?? '-'}`);
  return w;
}

async function releaseCase(name) {
  // The hold-off is 3 s after the last command.
  await sleep(3500);
  const i = await idle(agentA, 5000);
  const ok = !!i.harness && i.harness.rafPerS < THROTTLED_RAF_PER_S &&
             i.harness.timersPerS < THROTTLED_TIMERS_PER_S;
  check(`${name}: 3 s after the last command the agent's tab is throttled again`, ok,
        `rAF ${fmt(i.harness?.rafPerS)}/s, timers ${fmt(i.harness?.timersPerS)}/s, ${i.harness?.visibility}`);
  row(name, { raf: i.harness?.rafPerS, timers: i.harness?.timersPerS, cpu: i.cpu, ok });
}

// --- The agent's tab before its first command: a never-shown background tab.
{
  const i = await idle(agentA, 3000);
  row('agent tab before any command', { raf: i.harness?.rafPerS, timers: i.harness?.timersPerS,
                                         cpu: i.cpu });
}

// --- 1. A background tab, the user's tab in front.
await liveCase('Background tab');
await releaseCase('Release (3 s idle)');

// --- The holds' tools. Koan's power assertions for agents, by type
// (`pmset -g assertions` lists each with the reason it was taken with).
const WAKE_REASON = 'Koan: an agent is working';
function koanAssertions() {
  try {
    return execFileSync('pmset', ['-g', 'assertions'], { encoding: 'utf8' }).split('\n')
        .filter(line => line.includes(`"${WAKE_REASON}"`))
        .map(line => (line.match(/\b(Prevent\w+|NoIdleSleepAssertion|NoDisplaySleepAssertion)\b/) ?? [])[1]
                     ?? 'unknown');
  } catch {
    return null;
  }
}
let saving = false;
try {
  saving = /'Battery Power'/.test(execFileSync('pmset', ['-g', 'batt'], { encoding: 'utf8' })) &&
           /lowpowermode\s+1/.test(execFileSync('pmset', ['-g'], { encoding: 'utf8' }));
} catch {}
// On battery in Low Power Mode: idle sleep only, never the display.
const wantedAssertion = saving ? /PreventUserIdleSystemSleep|NoIdleSleep/
                               : /PreventUserIdleDisplaySleep|NoDisplaySleep/;
async function chipTooltip() {
  const vs = await viewTree();
  return vs.find(v => v.cls === 'KoanAgentChipButton' && v.shown)?.props.TooltipText ?? '';
}
function checkAssertionHeld(name) {
  const held = koanAssertions();
  check(`${name}: pmset shows Koan's assertion "${WAKE_REASON}" (${saving ? 'idle sleep only, ' +
        'Low Power Mode on battery' : 'display sleep'})`,
        !!held && held.some(type => wantedAssertion.test(type)) &&
        (!saving || !held.some(type => /Display/.test(type))), `assertions: ${JSON.stringify(held)}`);
}
async function checkAssertionGone(name) {
  // The assertion follows the 3 s hold-off; the 1 s tick adds up to a second.
  const gone = await waitFor(() => koanAssertions()?.length === 0, 6000);
  check(`${name}: pmset shows no Koan assertion any more`, !!gone,
        `assertions: ${JSON.stringify(koanAssertions())}`);
}
// The page's rates over a window that starts `after` ms past the last
// command and lasts `ms`.
async function heldWindow(page, lastCommand, { after = 1000, ms = 11000 } = {}) {
  await sleep(Math.max(0, lastCommand + after - Date.now()));
  const from = Date.now();
  await sleep(ms);
  return rates(page, from, Date.now());
}

// koan-relay, as `koan agent-browser` starts it, with a stand-in
// agent-browser that hands the test the relay's URL (AGENT_BROWSER_CDP; a
// 0600 file, never printed). The relay is a client of its own: approved
// once.
const relay = await (async () => {
  const bin = fs.mkdtempSync(path.join(AGENT_HOME, 'ab-'));
  const urlFile = path.join(bin, 'cdp');
  fs.writeFileSync(path.join(bin, 'agent-browser'),
                   `#!/bin/sh\numask 077\nprintf %s "$AGENT_BROWSER_CDP" > '${urlFile}'\n`, { mode: 0o700 });
  const relayClients = path.join(AGENT_HOME, '.koan', 'relay', 'clients');
  const paired = fs.existsSync(relayClients) && fs.readdirSync(relayClients).length > 0;
  let relayAllowed = paired;
  const a = await koan(['agent-browser', '--session', 'lease', 'get', 'url'], {
    timeout: 90000, extraEnv: { PATH: `${bin}:${process.env.PATH}` },
    whileRunning: paired ? undefined : async printed => {
      relayAllowed = await allow(/^Allow .*control Koan\?$/, printed);
    } });
  const url = fs.existsSync(urlFile) ? fs.readFileSync(urlFile, 'utf8').trim() : '';
  fs.rmSync(urlFile, { force: true });
  check('koan-relay: approved and started for the screencast cases (stand-in agent-browser)',
        a.code === 0 && relayAllowed && /^ws:\/\/127\.0\.0\.1:/.test(url),
        `${paired ? 'paired before' : allowWhy()}; ${brief(a)}`);
  if (!url) return null;
  // A CDP client over the relay's WebSocket (Node's own).
  const ws = new WebSocket(url);
  const opened = await new Promise(resolve => {
    ws.onopen = () => resolve(true);
    ws.onerror = () => resolve(false);
  });
  if (!opened) return null;
  let nextId = 1;
  const waiting = new Map(), listeners = [];
  const state = { closed: false };
  ws.onclose = () => { state.closed = true; };
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && waiting.has(m.id)) {
      waiting.get(m.id)(m);
      waiting.delete(m.id);
    } else if (m.method) {
      for (const l of listeners) l(m);
    }
  };
  const send = (method, params = {}, sessionId) => new Promise(resolve => {
    const id = nextId++;
    waiting.set(id, resolve);
    ws.send(JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) }));
    setTimeout(() => waiting.has(id) && (waiting.delete(id), resolve({ error: { message: 'timeout' } })),
               30000);
  });
  return { ws, send, listeners, state };
})();

// A screencast of a new relay tab: started, its first two frames acked,
// then nothing more is sent. Returns the tab's page, its session and when
// the last command went.
async function startScreencast(n) {
  if (!relay) return null;
  const created = await relay.send('Target.createTarget', { url: agentUrl(n), background: true });
  const targetId = created.result?.targetId;
  const attached = targetId && await relay.send('Target.attachToTarget', { targetId, flatten: true });
  const sessionId = attached?.result?.sessionId;
  const page = sessionId && await pageFor(agentUrl(n));
  if (!page) return null;
  let frames = 0, acked = 0, lastCommand = Date.now();
  relay.listeners.push(m => {
    if (m.method !== 'Page.screencastFrame' || m.sessionId !== sessionId) return;
    frames++;
    if (acked < 2) {
      acked++;
      lastCommand = Date.now();
      relay.send('Page.screencastFrameAck', { sessionId: m.params.sessionId }, sessionId);
    }
  });
  const started = await relay.send('Page.startScreencast', { format: 'jpeg', quality: 20 }, sessionId);
  lastCommand = Date.now();
  await waitFor(() => acked >= 2, 5000);
  return { page, sessionId, started: !started.error, lastCommand: () => lastCommand,
           frames: () => frames };
}

// Case: a screencast keeps the tab rendering with nothing sent for > 10 s.
async function screencastCase(name) {
  const sc = await startScreencast('s');
  if (!sc?.started) {
    check(`${name}: a screencast started over koan-relay`, false, relay ? 'no tab or no session' : 'no relay');
    return;
  }
  const v = await heldWindow(sc.page, sc.lastCommand());
  const ok = !!v && v.rafPerS > LIVE_PER_S && v.timersPerS > LIVE_PER_S;
  check(`${name}: a screencast holds the tab: it renders for 11 s from 1 s after the last command ` +
        `(rAF and timers > ${LIVE_PER_S}/s)`, ok,
        `rAF ${fmt(v?.rafPerS)}/s, timers ${fmt(v?.timersPerS)}/s, longest frame gap ${v?.gap} ms, ` +
        `${v?.visibility}`);
  row(name, { raf: v?.rafPerS, timers: v?.timersPerS, ok });
  checkAssertionHeld(name);
  const tip = await chipTooltip();
  check(`${name}: the chip's tooltip says "keeping the display awake"${saving ? ' (not in Low Power Mode)' : ''}`,
        saving ? !/keeping the display awake/.test(tip) : /keeping the display awake/.test(tip), tip);
  // Stopped: released after the hold-off.
  await relay.send('Page.stopScreencast', {}, sc.sessionId);
  await sleep(4500);
  const i = await idle(sc.page, 4000);
  const released = !!i.harness && i.harness.rafPerS < THROTTLED_RAF_PER_S &&
                   i.harness.timersPerS < THROTTLED_TIMERS_PER_S;
  check(`${name}: Page.stopScreencast releases it (throttled again after the hold-off)`, released,
        `rAF ${fmt(i.harness?.rafPerS)}/s, timers ${fmt(i.harness?.timersPerS)}/s`);
  row(`${name}, stopped`, { raf: i.harness?.rafPerS, timers: i.harness?.timersPerS, ok: released });
  await checkAssertionGone(`${name}, stopped`);
}

// Case: `koan watch <tab>` keeps the tab rendering while it runs; killed,
// its keep-alive lapses (at most 30 s: renewed every 10 s for 30 s).
async function watchCase(name) {
  const watch = spawnKoan(['watch', tabA]);
  let said = '';
  watch.stderr.on('data', d => (said += d));
  // It resolves the tab and takes the keep-alive before it streams.
  await sleep(2000);
  const v = await heldWindow(agentA, Date.now() - 1000, { after: 1000, ms: 11000 });
  const ok = !!v && v.rafPerS > LIVE_PER_S && v.timersPerS > LIVE_PER_S;
  check(`${name}: \`koan watch <tab>\` holds the tab: it renders for 11 s with no command ` +
        `(rAF and timers > ${LIVE_PER_S}/s)`, ok,
        `rAF ${fmt(v?.rafPerS)}/s, timers ${fmt(v?.timersPerS)}/s, longest frame gap ${v?.gap} ms` +
        `${said ? `; koan watch: ${said.split('\n')[0]}` : ''}`);
  row(name, { raf: v?.rafPerS, timers: v?.timersPerS, ok });
  checkAssertionHeld(name);
  watch.kill('SIGKILL');
  await sleep(31000);
  const i = await idle(agentA, 4000);
  const released = !!i.harness && i.harness.rafPerS < THROTTLED_RAF_PER_S &&
                   i.harness.timersPerS < THROTTLED_TIMERS_PER_S;
  check(`${name}: killed, its keep-alive lapses within 30 s (throttled again)`, released,
        `rAF ${fmt(i.harness?.rafPerS)}/s, timers ${fmt(i.harness?.timersPerS)}/s`);
  row(`${name}, lapsed`, { raf: i.harness?.rafPerS, timers: i.harness?.timersPerS, ok: released });
  await checkAssertionGone(`${name}, lapsed`);
}

// --- 2 and 3: the window covered by another app's window, and minimised.
// Both change what is on the screen: only with the Mac to itself.
if (!realInput() || windowId === undefined) {
  skipped('Covered window and Minimised window', realInput() ? 'no window id' : realInputWhyNot());
} else {
  // Our own window over Koan's (scripts/mac/tools/cover.m).
  let cover = null;
  try {
    const bin = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'koan-cover-')), 'cover');
    execFileSync('clang', ['-fobjc-arc', '-framework', 'AppKit', '-o', bin,
                           new URL('../mac/tools/cover.m', import.meta.url).pathname],
                 { stdio: 'ignore', timeout: 120000 });
    const b = (await windowOf(frontPage.id)).bounds;
    cover = spawn(bin, [b.left, b.top, b.width, b.height, 90].map(String),
                  { stdio: ['pipe', 'pipe', 'ignore'] });
    let said = '';
    cover.stdout.on('data', d => (said += d));
    await waitFor(() => /ready/.test(said), 10000);
  } catch (e) {
    console.log(`  (no cover window: ${e.message})`);
  }
  // macOS reports the occlusion; Chromium applies it 1 s later.
  const covered = cover && await waitFor(async () => await visibility(frontPage) === 'hidden', 8000);
  check('Covered window: another app\'s window covers Koan\'s (its shown tab went hidden)', !!covered,
        cover ? `the front tab is ${await visibility(frontPage)}` : 'the cover tool did not start');
  if (covered) {
    await liveCase('Covered window');
    await screencastCase('Covered window, screencast');
    await watchCase('Covered window, koan watch');
  }
  if (cover) {
    cover.stdin.end();
    cover.kill('SIGTERM');
  }
  await waitFor(async () => await visibility(frontPage) === 'visible', 8000);

  // Minimised (Browser.setWindowBounds, as Window > Minimize).
  await setWindowState(windowId, 'minimized');
  const minimised = await waitFor(async () => await visibility(frontPage) === 'hidden', 8000);
  check('Minimised window: Koan\'s window is minimised (its shown tab went hidden)', !!minimised,
        `the front tab is ${await visibility(frontPage)}`);
  // By code a background tab under a capturer has its own compositor
  // (BrowserCompositorMac with no parent layer), so minimising the window
  // must not stop it: the PR's analysis.
  if (minimised) await liveCase('Minimised window');
  await setWindowState(windowId, 'normal');
  await waitFor(async () => await visibility(frontPage) === 'visible', 8000);
  await front(frontPage);
  await releaseCase('Release after the window cases');
}

// --- 4. The cap: 6 agent tabs. The limit is 4 (local_state), half of it on
// battery with Low Power Mode on (low_power_halves, on by default).
{
  let battery = false, lowPower = false;
  try {
    battery = /'Battery Power'/.test(execFileSync('pmset', ['-g', 'batt'], { encoding: 'utf8' }));
    lowPower = /lowpowermode\s+1/.test(execFileSync('pmset', ['-g'], { encoding: 'utf8' }));
  } catch {}
  const cap = battery && lowPower ? 2 : 4;
  console.log(`INFO  power: ${battery ? 'battery' : 'AC'}, Low Power Mode ${lowPower ? 'on' : 'off'}: ` +
              `expected cap ${cap}`);
  const tabs = [{ tab: tabA, page: agentA }];
  for (const n of ['b', 'c', 'd', 'e', 'f']) {
    const o = await koan(['open', agentUrl(n)]);
    tabs.push({ tab: o.out.split('\n').at(-1), page: await pageFor(agentUrl(n)), opened: o.code === 0 });
  }
  check('Cap: 6 agent tabs open', tabs.every(t => t.page && t.opened !== false),
        tabs.map(t => (t.page ? 'ok' : 'missing')).join(' '));
  await front(frontPage);
  // Every tab idle past its hold-off.
  await sleep(4000);
  const held = tabs.slice(0, cap), queued = tabs.slice(cap);
  await Promise.all(held.map(t => koan(['snapshot', t.tab], { timeout: 30000 })));
  const queuedAt = Date.now();
  const waiting = queued.map(t => koan(['snapshot', t.tab], { timeout: 60000 })
      .then(s => ({ ...s, ms: Date.now() - queuedAt })));
  await sleep(300);
  const from = Date.now();
  await sleep(2000);
  const to = Date.now();
  const live = [];
  for (const t of tabs) {
    const v = t.page && await rates(t.page, from, to);
    live.push(!!v && v.rafPerS > LIVE_PER_S);
    console.log(`INFO  cap: tab ${tabs.indexOf(t) + 1} rAF ${fmt(v?.rafPerS)}/s, ` +
                `timers ${fmt(v?.timersPerS)}/s`);
  }
  const liveCount = live.filter(Boolean).length;
  const ok = liveCount === cap && live.slice(0, cap).every(Boolean);
  check(`Cap: exactly ${cap} agent tabs live (the first ${cap}), the other ${6 - cap} throttled`, ok,
        `live: ${live.map(l => (l ? 1 : 0)).join('')}`);
  row(`Cap ${cap}: ${liveCount} of 6 tabs live`, { ok });
  const done = await Promise.all(waiting);
  check('Cap: the queued tabs\' commands waited for a free lease and then ran (never failed)',
        done.every(s => s.code === 0 && /raf=\d+/.test(s.out) && s.ms > 2000),
        done.map(s => `${brief(s)} after ${s.ms} ms`).join(' | '));
}

// --- 5. The user's own background tab, from its creation to now.
{
  const u = await rates(userPage, userCreated, Date.now());
  const ok = !!u && u.rafPerS < 0.2 && u.timersPerS < THROTTLED_TIMERS_PER_S &&
             u.visibility === 'hidden';
  check('User\'s background tab: throttled throughout (no frames, timers throttled, hidden)', ok,
        `${u?.raf} frames, timers ${fmt(u?.timersPerS)}/s over ${Math.round((Date.now() - userCreated) / 1000)} s`);
  for (const [name, values] of userRows) row(name, values);
  row('user\'s tab, whole test', { raf: u?.rafPerS, timers: u?.timersPerS, ok });
}

// --- End: Stop (the pill's button on the agent's tab) while a screencast
// and a watch hold tabs: everything is released at once.
{
  const sc = await startScreencast('stop');
  const watch = spawnKoan(['watch', tabA]);
  let watchEnded = false;
  watch.on('close', () => (watchEnded = true));
  await sleep(2500);
  check('Stop: before it, Koan holds its assertion for the screencast and the watch',
        (koanAssertions() ?? []).length > 0, `assertions: ${JSON.stringify(koanAssertions())}`);
  // Shown as the user does (CDP's bringToFront of a tab an agent holds is
  // held): its row, under a title of its own.
  await evalIn(agentA, "document.title = 'Lease A'");
  await sleep(500);
  if (!(await selectTabRow('Lease A'))) await front(agentA);
  // The pill's widgets are made as the tab shows; read the tree once settled.
  await sleep(1500);
  const vs = await viewTree();
  const pill = vs.find(v => v.cls === 'KoanAgentIndicatorView' && v.shown);
  const stop = pill && vs.find(v => v.widget === pill.widget && v.shown && v.cls === 'LabelButton' &&
                                   v.props.Text === 'Stop');
  if (stop) {
    const root = rootOf({ views: vs.filter(v => v.widget === stop.widget) });
    await rootMouse(root, ['mousePressed', 'mouseReleased'], stop.ax + stop.w / 2, stop.ay + stop.h / 2);
  }
  check('Stop: the pill on the agent\'s tab has Stop, and it was pressed', !!stop, pill ? 'no Stop' : 'no pill');
  // No lease is left: no hold-off.
  const gone = await waitFor(() => koanAssertions()?.length === 0, 3000);
  check('Stop: the assertion goes at once', !!gone, `assertions: ${JSON.stringify(koanAssertions())}`);
  const closed = await waitFor(async () =>
    !(await pages()).some(p => p.url.startsWith(server.url('/lease'))), 8000);
  check('Stop: the agents\' tabs closed (nothing of theirs renders any more)', !!closed,
        (await pages()).filter(p => p.url.startsWith(server.url('/lease'))).map(p => p.url).join(' '));
  await waitFor(() => relay?.state.closed !== false && watchEnded, 8000);
  check('Stop: the screencast\'s connection (koan-relay) and `koan watch` ended',
        (!relay || relay.state.closed) && watchEnded && !!sc,
        `relay closed: ${relay?.state.closed}, watch ended: ${watchEnded}, screencast: ${!!sc?.started}`);
  if (!watchEnded) watch.kill('SIGKILL');
  row('Stop: assertion gone, tabs closed', { ok: !!gone && !!closed });
}
check('Koan alive, nothing fatal in its log', await alive() && !fatalInLog(), fatalInLog());

console.log('\nSUMMARY  case | screenshots differ | rAF/s | timers/s | renderer CPU % | verdict');
for (const x of rows) {
  console.log(`SUMMARY  ${x.name} | ${x.shots} | ${fmt(x.raf)} | ${fmt(x.timers)} | ${fmt(x.cpu)} | ${x.verdict}`);
}
server.close();
finish();
