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
import { ACCEL, browserCall, check, finish, key, pages, setKoanPref, sleep, typeInto,
         viewTree } from '../../e2e/lib/koan.mjs';
import { KOAN_PID, browserWidgets, startServer, widgetWithTab, windowOf } from '../../e2e/lib/mac.mjs';

const spec = JSON.parse(fs.readFileSync(new URL('./regions.json', import.meta.url), 'utf8'));
const OUT = process.env.KOAN_SHOTS;
const WINID = process.env.KOAN_WINID;
fs.mkdirSync(OUT, { recursive: true });

// One plain page, nothing that moves, in the window Koan started with: a
// tab of its own there, then the start-up tabs closed. (Not a window of its
// own with the first one's tabs closed: like Zen (browser.tabs.
// closeWindowWithLastTab off), Koan keeps a window whose last tab closes, on
// its empty tab, so a second, unsized window would stay behind, and it is
// the bigger one (#285).)
const server = await startServer();
const TITLE = 'Reference';
const url = server.url(`/p?t=${TITLE}&b=%23f4f4f5`);
const others = (await pages()).filter(p => p.type === 'page');
await browserCall('Target.createTarget', { url });
await sleep(2500);
for (const p of others) await browserCall('Target.closeTarget', { targetId: p.id }).catch(() => {});
await sleep(1500);
const page = (await pages()).find(p => p.url === url);
const { windowId } = await windowOf(page.id);
const BOUNDS = { left: 60, top: 80, width: 1200, height: 760 };
await browserCall('Browser.setWindowBounds', { windowId, bounds: { windowState: 'normal' } });
await browserCall('Browser.setWindowBounds', { windowId, bounds: BOUNDS });
await sleep(1500);
const placed = (await browserCall('Browser.getWindowBounds', { windowId })).result?.bounds;
check('One browser window, at the size and place it was given',
      (await browserWidgets()).length === 1 && placed?.left === BOUNDS.left &&
          placed.top === BOUNDS.top && placed.width === BOUNDS.width &&
          placed.height === BOUNDS.height,
      `${(await browserWidgets()).length} window(s), ${JSON.stringify(placed)}`);

// The page's browser window, never another one (a window left on its empty
// tab, a bigger one): its CGWindowID by its place and size (CG global
// coordinates and CDP's window bounds are both points from the top left of
// the main display), and its widget by the page's tab.
function windowNumber() {
  const lines = execFileSync(WINID, [String(KOAN_PID)]).toString().trim().split('\n');
  const near = (a, b) => Math.abs(a - b) <= 2;
  const w = lines.map(l => l.split(' ').map(Number)).find(
      f => f[5] === 0 && near(f[1], BOUNDS.left) && near(f[2], BOUNDS.top) &&
           near(f[3], BOUNDS.width) && near(f[4], BOUNDS.height));
  return { id: w?.[0], size: w ? [w[3], w[4]] : null, all: lines.join('; ') };
}
const pageWidget = async vs => widgetWithTab(TITLE, vs);

// The capture is the window's own frame: a child window (a popup, the URL
// bar's card) that sticks out of it makes the image bigger than the window,
// and the regions land in the wrong place.
function pngSize(file) {
  const b = fs.readFileSync(file);
  return [b.readUInt32BE(16), b.readUInt32BE(20)];
}

async function regionsNow(names) {
  const vs = await viewTree();
  const win = await pageWidget(vs);
  if (!win) return { root: null, regions: {}, same: {}, outside: [] };
  const root = win.views[0];
  const out = {};
  const same = {};
  const outside = [];
  for (const name of names) {
    const r = spec.regions[name];
    if (r.same) same[name] = r.same;
    if (r.rect) {
      let [x, y, w, h] = r.rect;
      if (x < 0) x = root.w + x;
      if (y < 0) y = root.h + y;
      out[name] = [x, y, w, h];
      continue;
    }
    const v = win.views.find(x => x.cls === r.view && x.shown && x.w > 0 && x.h > 0);
    if (!v) {
      // In a widget of its own (a child window on macOS): its bounds aren't
      // in the window's root view, so it can't be placed in the capture.
      const elsewhere = vs.find(x => x.cls === r.view && x.shown && x.w > 0 && x.h > 0);
      if (elsewhere) outside.push(name);
      continue;
    }
    const pad = r.pad ?? 0;
    out[name] = [v.ax - pad, v.ay - pad, v.w + 2 * pad, v.h + 2 * pad];
  }
  return { root: [root.w, root.h], regions: out, same, outside };
}

// The omnibox of the page's window.
const omnibox = async () =>
  (await pageWidget(await viewTree()))?.views.find(v => v.cls === 'OmniboxViewViews' && v.shown);
// Types into it a character at a time, finding it again for each: UI DevTools
// numbers nodes in tree order, so the suggestions' widget, which opens with
// the first character, moves the omnibox's node id (only the first
// character reached it, #285).
async function typeInOmnibox(text) {
  for (const ch of text) {
    const field = await omnibox();
    if (!field) return false;
    await typeInto(field, ch);
  }
  return true;
}
// A setup returns true when it leaves the window as it should be captured
// (the focus where it put it), else the page is activated after it.
const setups = {
  default: async () => setKoanPref('koan.compact_mode.enabled', false),
  compact: async () => setKoanPref('koan.compact_mode.enabled', true),
  // Zen's floating URL bar in place of a new tab, with suggestions: one card.
  'url-card': async () => {
    await setKoanPref('koan.compact_mode.enabled', false);
    await browserCall('Target.activateTarget', { targetId: page.id });
    await sleep(1500);
    await key(page, 84, ACCEL, 't');
    await sleep(800);
    await typeInOmnibox('koan');
    await sleep(1500);
    const field = await omnibox();
    check('url-card: the omnibox has all of what was typed', field?.props.Text === 'koan',
          JSON.stringify(field?.props.Text));
    return true;
  },
};
const teardowns = {
  'url-card': async () => {
    const field = await omnibox();
    if (field) await typeInto(field, '', 27);  // Esc closes the bar.
  },
};
for (const [state, names] of Object.entries(spec.states)) {
  if (!(await setups[state]?.())) {
    // Back to the page, the pointer's hover gone, animations done.
    await browserCall('Target.activateTarget', { targetId: page.id });
    await sleep(2500);
  }
  const { id, size, all } = windowNumber();
  check(`${state}: found the window (winid)`, !!id, id ? '' : all);
  if (!id) {
    await teardowns[state]?.();
    continue;
  }
  const png = path.join(OUT, `${state}.png`);
  execFileSync('screencapture', ['-x', '-o', `-l${id}`, png]);
  const info = await regionsNow(names);
  fs.writeFileSync(path.join(OUT, `${state}.json`), JSON.stringify(info, null, 1));
  const [pw, ph] = pngSize(png);
  check(`${state}: the capture is the window, nothing sticks out of it`,
        info.root && Math.abs(pw / size[0] - ph / size[1]) < 0.01 &&
            info.root[0] === size[0] && info.root[1] === size[1],
        `capture ${pw}x${ph} px, window ${size.join('x')} pt, root view ${info.root?.join('x')}`);
  const missing = names.filter(n => !info.regions[n]);
  check(`${state}: captured, every region found`, fs.existsSync(png) && !missing.length,
        missing.length ? `missing ${missing.join(', ')}` +
            (info.outside.length ? ` (in a widget of their own: ${info.outside.join(', ')})` : '')
          : '');
  await teardowns[state]?.();
}
await setKoanPref('koan.compact_mode.enabled', false);
server.close();
finish();
