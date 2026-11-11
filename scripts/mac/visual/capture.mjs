// Captures Koan's window in fixed states for the visual checks (no model
// looks at it: scripts/mac/visual/diff.py compares the crops with the
// reference images). Run by scripts/mac/suite.sh like an e2e script, against
// its fresh test profile:
//   KOAN_SHOTS=<dir>  where <state>.png and <state>.json go
//   KOAN_WINID=<path> the winid tool (scripts/mac/tools/winid.m)
// For each state in regions.json: the window at a fixed size and place on a
// plain local page, `screencapture -x -o -l<window id>` (no shadow, no
// sound), and the named regions' rectangles in the window's root view,
// in points, from UI DevTools.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { browserCall, check, finish, pages, setKoanPref, sleep, viewTree }
    from '../../e2e/lib/koan.mjs';
import { KOAN_PID, browserWidgets, startServer, windowOf } from '../../e2e/lib/mac.mjs';

const spec = JSON.parse(fs.readFileSync(new URL('./regions.json', import.meta.url), 'utf8'));
const OUT = process.env.KOAN_SHOTS;
const WINID = process.env.KOAN_WINID;
fs.mkdirSync(OUT, { recursive: true });

// One plain page, nothing that moves.
const server = await startServer();
const url = server.url('/p?t=Reference&b=%23f4f4f5');
const others = (await pages()).filter(p => p.type === 'page');
await browserCall('Target.createTarget', { url, newWindow: true });
await sleep(2500);
// One window only (the new one comes first: macOS keeps running without
// windows, but the profile's start-up tabs should not show).
for (const p of others) await browserCall('Target.closeTarget', { targetId: p.id }).catch(() => {});
await sleep(1500);
const page = (await pages()).find(p => p.url === url);
const { windowId } = await windowOf(page.id);
await browserCall('Browser.setWindowBounds', { windowId, bounds: { windowState: 'normal' } });
await browserCall('Browser.setWindowBounds',
                  { windowId, bounds: { left: 60, top: 80, width: 1200, height: 760 } });
await sleep(1500);

function windowNumber() {
  const lines = execFileSync(WINID, [String(KOAN_PID)]).toString().trim().split('\n');
  // The biggest normal-layer window is the browser window.
  const w = lines.map(l => l.split(' ').map(Number)).find(f => f[5] === 0);
  return w?.[0];
}

async function regionsNow(names) {
  const vs = await viewTree();
  const [win] = await browserWidgets(vs);
  const root = win.views[0];
  const out = {};
  for (const name of names) {
    const r = spec.regions[name];
    if (r.rect) {
      let [x, y, w, h] = r.rect;
      if (x < 0) x = root.w + x;
      if (y < 0) y = root.h + y;
      out[name] = [x, y, w, h];
      continue;
    }
    const v = win.views.find(x => x.cls === r.view && x.shown && x.w > 0 && x.h > 0);
    if (!v) continue;
    const pad = r.pad ?? 0;
    out[name] = [v.ax - pad, v.ay - pad, v.w + 2 * pad, v.h + 2 * pad];
  }
  return { root: [root.w, root.h], regions: out };
}

const setups = {
  default: async () => setKoanPref('koan.compact_mode.enabled', false),
  compact: async () => setKoanPref('koan.compact_mode.enabled', true),
};
for (const [state, names] of Object.entries(spec.states)) {
  await setups[state]?.();
  // Back to the page, the pointer's hover gone, animations done.
  await browserCall('Target.activateTarget', { targetId: page.id });
  await sleep(2500);
  const id = windowNumber();
  check(`${state}: found the window (winid)`, !!id);
  if (!id) continue;
  const png = path.join(OUT, `${state}.png`);
  execFileSync('screencapture', ['-x', '-o', `-l${id}`, png]);
  const info = await regionsNow(names);
  fs.writeFileSync(path.join(OUT, `${state}.json`), JSON.stringify(info, null, 1));
  const missing = names.filter(n => !info.regions[n]);
  check(`${state}: captured, every region found`, fs.existsSync(png) && !missing.length,
        missing.length ? `missing ${missing.join(', ')}` : '');
}
await setKoanPref('koan.compact_mode.enabled', false);
server.close();
finish();
