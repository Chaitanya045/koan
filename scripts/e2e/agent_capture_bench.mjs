// Measures the one capture path (2026-11-20; .agent/notes/agent-control.md
// section 12, "One capture path"): Koan.captureScaled, which copies the
// frame the compositor already drew and scales it on the GPU, against #409's
// way, CDP's Page.captureScreenshot with clip and scale (device emulation:
// the page is laid out and drawn again at the new scale, then restored).
// Through koan-relay (agent-browser's way in, a raw CDP client of the
// facade), on a local page (pages/agent_capture_watch.html) that counts
// every resize, visualViewport resize, devicePixelRatio and media query
// change it could see:
//   - an agent tab (in the background, under its lease): both ways, at
//     micro, mini, normal and full, REPS times each: wall time per capture
//     (median and worst), bytes, pixels and image tokens;
//   - a user's tab, taken over (Koan.takeOver, its prompt allowed): the
//     captures, and CDP's clip refused there (Koan.notPermitted);
//   - what the page saw: nothing during captureScaled, on either tab (the
//     checks); what CDP's emulation made it see (INFO).
// No model is called. Prints a table, a RESULT line (JSON), and writes the
// JSON to $KOAN_BENCH_OUT if set. Fails if captureScaled is not the size
// planned, if the page saw anything during it, or if CDP's clip is allowed on
// the user's tab.
// Run on the Mac (not in the default list):
//   scripts/mac/suite.sh --skip-cxx --skip-visual --all-e2e --only agent_capture_bench <Koan.app>
// local_state/agent_capture_bench.json turns Agent ready on for the profile.
import fs from 'node:fs';
import path from 'node:path';
import { AGENT_HOME, CLI, PROFILE, allow, allowWhy, brief, koan } from './lib/agent.mjs';
import { browserCall, check, evalIn, finish, pages, sleep, waitFor } from './lib/koan.mjs';
import { IS_MAC, alive, fatalInLog, skip, startServer } from './lib/mac.mjs';

if (!IS_MAC) skip('agent control\'s CLI needs macOS (its holder resolves peers with LOCAL_PEERTOKEN)');
if (!CLI || !fs.existsSync(CLI)) skip(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);
if (!PROFILE || !AGENT_HOME) skip('KOAN_PROFILE and KOAN_AGENT_HOME are set by scripts/mac/suite.sh');

const REPS = +(process.env.KOAN_BENCH_REPS ?? 5);
// The CLI's sizes (koan_cli_screenshot.cc, CaptureParams()).
const SIZES = { micro: [320, 1], mini: [640, 1], normal: [1568, 1], full: [2576, 4] };
const tokensOf = (w, h) => Math.min(4784, Math.max(1, Math.ceil(w * h / 784)));
const median = xs => [...xs].sort((a, b) => a - b)[Math.floor(xs.length / 2)];

const server = await startServer({
  '/watch': fs.readFileSync(new URL('./pages/agent_capture_watch.html', import.meta.url), 'utf8'),
});
await waitFor(() => fs.existsSync(path.join(PROFILE, 'AgentControl', 'agent.json')), 15000);

// koan-relay with a stand-in agent-browser that hands the test the relay's
// URL (as agent_lease.mjs does), and a CDP client over its WebSocket.
const relay = await (async () => {
  const bin = fs.mkdtempSync(path.join(AGENT_HOME, 'ab-'));
  const urlFile = path.join(bin, 'cdp');
  fs.writeFileSync(path.join(bin, 'agent-browser'),
                   `#!/bin/sh\numask 077\nprintf %s "$AGENT_BROWSER_CDP" > '${urlFile}'\n`, { mode: 0o700 });
  let relayAllowed = false;
  const a = await koan(['agent-browser', '--session', 'capture', 'get', 'url'], {
    timeout: 90000, extraEnv: { PATH: `${bin}:${process.env.PATH}` },
    whileRunning: async printed => { relayAllowed = await allow(/^Allow .*control Koan\?$/, printed); } });
  const url = fs.existsSync(urlFile) ? fs.readFileSync(urlFile, 'utf8').trim() : '';
  fs.rmSync(urlFile, { force: true });
  check('koan-relay: approved and started (stand-in agent-browser)',
        a.code === 0 && /^ws:\/\/127\.0\.0\.1:/.test(url), `${relayAllowed ? '' : allowWhy()}; ${brief(a)}`);
  if (!url) return null;
  const ws = new WebSocket(url);
  if (!await new Promise(resolve => { ws.onopen = () => resolve(true); ws.onerror = () => resolve(false); })) {
    return null;
  }
  let nextId = 1;
  const waiting = new Map();
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && waiting.has(m.id)) {
      waiting.get(m.id)(m);
      waiting.delete(m.id);
    }
  };
  const send = (method, params = {}, sessionId) => new Promise(resolve => {
    const id = nextId++;
    waiting.set(id, resolve);
    ws.send(JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) }));
    setTimeout(() => waiting.has(id) && (waiting.delete(id), resolve({ error: { message: 'timeout' } })),
               30000);
  });
  return { ws, send };
})();
if (!relay) {
  server.close();
  finish();
}

const attach = async targetId =>
  (await relay.send('Target.attachToTarget', { targetId, flatten: true })).result?.sessionId;
const pageOf = async targetId => waitFor(async () => (await pages()).find(p => p.id === targetId), 10000);
const seen = async page => JSON.parse(await evalIn(page, 'JSON.stringify(window.koanSeen)') ?? 'null');
const noneSeen = s => !!s && s.resize === 0 && s.viewport === 0 && s.dpr === 0 && s.media === 0;
const bytesOf = b64 => (b64 ? Buffer.from(b64, 'base64').length : 0);

// One way, one size, REPS times: {ms: [..], bytes, width, height, tokens, error}.
async function measure(take) {
  const ms = [];
  let last = null;
  for (let i = 0; i < REPS; i++) {
    const t0 = performance.now();
    last = await take();
    ms.push(performance.now() - t0);
    if (last.error) break;
  }
  const { width = 0, height = 0, bytes = 0, error, via } = last ?? {};
  return { ms: median(ms), worst: Math.max(...ms), width, height, bytes, tokens: tokensOf(width, height),
           error, via };
}

const scaled = (targetId, [longEdge, scale]) => async () => {
  const a = await relay.send('Koan.captureScaled', { targetId, longEdge, scale, format: 'png' });
  return a.error ? { error: a.error.data ?? a.error.message }
                 : { width: a.result.width, height: a.result.height, bytes: bytesOf(a.result.data),
                     via: a.result.via };
};
// #409's way: the viewport from the page, then a clip and a scale.
const cdp = (sessionId, [longEdge, perCssMax]) => async () => {
  const m = (await relay.send('Page.getLayoutMetrics', {}, sessionId)).result;
  const css = m?.cssVisualViewport;
  if (!css) return { error: 'no layout metrics' };
  const device = m.visualViewport.clientWidth / css.clientWidth;
  const natural = Math.min(perCssMax, device);
  let fit = Math.min(1, longEdge / (Math.max(css.clientWidth, css.clientHeight) * natural));
  const pixels = css.clientWidth * css.clientHeight * (natural * fit) ** 2, max = 4784 * 784;
  if (pixels > max) fit *= Math.sqrt(max / pixels);
  const perCss = natural * fit, zoom = css.zoom ?? 1;
  const a = await relay.send('Page.captureScreenshot', {
    format: 'png',
    clip: { x: css.pageX * zoom, y: css.pageY * zoom, width: css.clientWidth * zoom,
            height: css.clientHeight * zoom, scale: perCss / device } }, sessionId);
  if (a.error) return { error: a.error.data ?? a.error.message };
  const png = Buffer.from(a.result.data, 'base64');
  return { width: png.readUInt32BE(16), height: png.readUInt32BE(20), bytes: png.length };
};

const rows = [];
const row = (tab, way, size, m) => {
  rows.push({ tab, way, size, ...m });
  console.log(`INFO  ${tab.padEnd(6)} ${way.padEnd(14)} ${size.padEnd(7)} ` +
              `${m.error ? `error ${m.error}` : `${m.ms.toFixed(1).padStart(7)} ms (worst ${m.worst.toFixed(1)})  ` +
               `${m.width}×${m.height}  ${String(m.bytes).padStart(8)} B  ~${m.tokens} tokens` +
               `${m.via ? `  via ${m.via}` : ''}`}`);
};

// --- An agent tab, in the background under its lease.
const agent = (await relay.send('Target.createTarget', { url: server.url('/watch'), background: true }))
    .result?.targetId;
const agentSession = agent && await attach(agent);
const agentPage = agent && await pageOf(agent);
check('An agent tab with the watch page, attached', !!agentSession && !!agentPage, `${agent} ${agentSession}`);
if (agentSession && agentPage) {
  await sleep(1500);
  const view = JSON.parse(await evalIn(agentPage, 'JSON.stringify({ w: innerWidth, h: innerHeight, ' +
                                                  'dpr: devicePixelRatio })'));
  for (const [size, spec] of Object.entries(SIZES)) {
    const m = await measure(scaled(agent, spec));
    row('agent', 'captureScaled', size, m);
    check(`agent tab, captureScaled ${size}: an image from the surface, at most ${spec[0]} on its long side`,
          !m.error && m.via === 'surface' && Math.max(m.width, m.height) <= spec[0] && m.bytes > 0,
          JSON.stringify(m));
  }
  const after = await seen(agentPage);
  check('agent tab: the page saw no resize, devicePixelRatio or media query change during captureScaled',
        noneSeen(after) && after.dprAtLoad === view.dpr, JSON.stringify(after));
  for (const [size, spec] of Object.entries(SIZES)) row('agent', 'cdp clip+scale', size, await measure(cdp(agentSession, spec)));
  console.log(`INFO  agent tab: what the page saw during CDP's clip and scale: ${JSON.stringify(await seen(agentPage))}`);
}

// --- A user's tab, taken over: the harness opens it in front, the relay
// asks for it, the prompt is allowed.
const user = (await browserCall('Target.createTarget', { url: server.url('/watch') })).result?.targetId;
await sleep(1500);
await browserCall('Target.activateTarget', { targetId: user });
await sleep(800);
let allowed = false;
const [granted] = await Promise.all([
  relay.send('Koan.takeOver', { targetId: user }),
  (async () => { allowed = await allow(/ wants to control a tab$/, () => ' in Koan...'); })(),
]);
const userSession = !granted.error && await attach(user);
const userPage = user && await pageOf(user);
check('A user tab with the watch page, taken over (its prompt allowed)', allowed && !!userSession && !!userPage,
      `${allowed ? '' : allowWhy()} ${JSON.stringify(granted.error ?? '')}`);
if (userSession && userPage) {
  for (const [size, spec] of Object.entries(SIZES)) {
    const m = await measure(scaled(user, spec));
    row('user', 'captureScaled', size, m);
    const agentRow = rows.find(r => r.tab === 'agent' && r.way === 'captureScaled' && r.size === size);
    check(`user tab, captureScaled ${size}: from the surface, the agent tab's size`,
          !m.error && m.via === 'surface' && (!agentRow || (Math.abs(agentRow.width - m.width) <= 2 &&
                                                             Math.abs(agentRow.height - m.height) <= 2)),
          JSON.stringify(m));
  }
  const after = await seen(userPage);
  check('user tab: the page saw no resize, devicePixelRatio or media query change during captureScaled',
        noneSeen(after), JSON.stringify(after));
  const refused = await cdp(userSession, SIZES.mini)();
  check('user tab: CDP\'s clip and scale (device emulation) stays refused (Koan.notPermitted)',
        refused.error === 'Koan.notPermitted', JSON.stringify(refused));
  await relay.send('Koan.release', { targetId: user });
}

console.log(`RESULT ${JSON.stringify(rows)}`);
if (process.env.KOAN_BENCH_OUT) fs.writeFileSync(process.env.KOAN_BENCH_OUT, JSON.stringify(rows, null, 2));
for (const size of Object.keys(SIZES)) {
  const a = rows.find(r => r.tab === 'agent' && r.way === 'captureScaled' && r.size === size);
  const b = rows.find(r => r.tab === 'agent' && r.way === 'cdp clip+scale' && r.size === size);
  if (a && b && !a.error && !b.error) {
    console.log(`INFO  ${size}: captureScaled ${a.ms.toFixed(1)} ms vs CDP ${b.ms.toFixed(1)} ms ` +
                `(${(b.ms / a.ms).toFixed(1)}×), ${a.bytes} vs ${b.bytes} bytes`);
  }
}
await relay.send('Koan.endTask', {});
relay.ws.close();
if (user) await browserCall('Target.closeTarget', { targetId: user }).catch(() => {});
await koan(['end']);
check('Koan is alive', await alive());
check('Nothing fatal in the log', !fatalInLog(), fatalInLog());
server.close();
finish();
