// Checks a compact_edge.mjs recording for black at the window's edges: in
// every frame of every transition (compact mode on and off, the sidebar
// peeked in and out), the window's top rows, left columns and bottom rows are
// sampled, and a pixel is a glitch when it is dark while it is light both
// just before the action and once the transition has ended. In the sidebar's
// own columns (it slides there, dark) only the end of the transition is
// checked, and the pointer's path is left out.
// Exits 1 if any frame has a glitch, so a run fails on the black edge.
//
//   node edge_check.mjs <video.mp4> <marks.json> [--band 24] [--dark 64]
//        [--light 150] [--min-px 4] [--out <report.json>]
//
// The video is record.sh's, of the whole screen (its <video>.rec.json maps
// the marks' wall clock to video time and screen points to video pixels).
// Each action mark of the flow has the window's rect, the sidebar's width and
// the pointer's path. Frames are decoded at one pixel per point, every frame
// (30 fps), from 0.25 s before the mark to the next action (or 1.7 s).
//
// Prints one line per action (frames, glitch frames, the worst one: video
// time, pixels, bbox in window points, the band), then PASS or FAIL.
import { spawn } from 'node:child_process';
import fs from 'node:fs';

const args = process.argv.slice(2);
const opt = (name, def) => {
  const i = args.indexOf(`--${name}`);
  return i >= 0 ? args[i + 1] : def;
};
const [video, marksFile] = args;
if (!args.includes('--self-test') && (!video || !marksFile)) {
  console.error('usage: edge_check.mjs <video.mp4> <marks.json> [--band 24] [--dark 64] [--light 150] [--min-px 4]');
  process.exit(2);
}
const BAND = Number(opt('band', 24));
const DARK = Number(opt('dark', 64));
const LIGHT = Number(opt('light', 150));
const MIN_PX = Number(opt('min-px', 4));
const POINTER_PAD = 32;
const FFMPEG = process.env.FFMPEG ?? '/opt/homebrew/bin/ffmpeg';
const FPS = 30;

const SELF_TEST = args.includes('--self-test');
const rec = SELF_TEST ? {} : JSON.parse(fs.readFileSync(`${video}.rec.json`, 'utf8'));
const marks = SELF_TEST ? { marks: [] } : JSON.parse(fs.readFileSync(marksFile, 'utf8'));
const scale = rec.scale ?? 2;
const vt = t => (t - rec.t0) / 1000;
const acts = marks.marks.filter(m => m.action && m.rect).sort((a, b) => a.t - b.t);

// Frames of `rect` (screen points) from `from` to `to` (video s), as
// luminance (0-255), one byte per point.
function decode(rect, from, to) {
  const even = n => Math.max(2, Math.floor(n / 2) * 2);
  const x = even((rect.x - rec.rect.x) * scale), y = even((rect.y - rec.rect.y) * scale);
  const pw = even(rect.w * scale), ph = even(rect.h * scale);
  const w = rect.w, h = rect.h;
  const ff = spawn(FFMPEG, ['-nostdin', '-v', 'error', '-ss', from.toFixed(3), '-to', to.toFixed(3),
                            '-i', video, '-vf', `fps=${FPS},crop=${pw}:${ph}:${x}:${y},scale=${w}:${h}:flags=area`,
                            '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1']);
  const size = w * h * 3;
  const frames = [];
  let pending = Buffer.alloc(0);
  return new Promise((resolve, reject) => {
    ff.stdout.on('data', chunk => {
      pending = Buffer.concat([pending, chunk]);
      while (pending.length >= size) {
        const rgb = pending.subarray(0, size);
        const lum = new Uint8Array(w * h);
        for (let i = 0, j = 0; i < lum.length; i++, j += 3) {
          lum[i] = (rgb[j] * 77 + rgb[j + 1] * 150 + rgb[j + 2] * 29) >> 8;
        }
        frames.push({ t: from + frames.length / FPS, lum });
        pending = pending.subarray(size);
      }
    });
    ff.stderr.on('data', d => process.stderr.write(d));
    ff.on('error', reject);
    ff.on('close', code => (code === 0 ? resolve({ w, h, frames }) : reject(new Error(`ffmpeg exited ${code}`))));
  });
}

// Distance from (px, py) to the segment a-b.
function distToSegment(px, py, [ax, ay], [bx, by]) {
  const dx = bx - ax, dy = by - ay;
  const len = dx * dx + dy * dy;
  const t = len ? Math.max(0, Math.min(1, ((px - ax) * dx + (py - ay) * dy) / len)) : 0;
  return Math.hypot(px - (ax + t * dx), py - (ay + t * dy));
}

// Where the sidebar is in a frame, measured: it is dark (Koan's dark
// sidebar over a light page) and slides in from (or out to) the window's
// left. Its columns are the run of dark columns, in the window's middle
// rows, that starts within BAND of the left edge; its rows, the dark rows of
// its middle column. {x0, x1, y0, y1} (inclusive, window points), or null
// when no sidebar shows.
function sidebarRect(lum, w, h, maxW) {
  const ys = [Math.floor(h * 0.35), Math.floor(h * 0.5), Math.floor(h * 0.65)];
  const darkCol = x => ys.filter(y => lum[y * w + x] < DARK).length >= 2;
  let x0 = -1;
  for (let x = 0; x < Math.min(w, BAND); x++) {
    if (darkCol(x)) { x0 = x; break; }
  }
  if (x0 < 0) return null;
  let x1 = x0;
  while (x1 + 1 < Math.min(w, maxW + BAND) && darkCol(x1 + 1)) x1++;
  // Its rows: from the middle up and down while a row is dark in one of
  // three of its columns, through light gaps shorter than GAP (its own text
  // and icons).
  const GAP = 12;
  const cols = [x0, Math.floor((x0 + x1) / 2), x1];
  const darkRow = y => cols.some(x => lum[y * w + x] < DARK);
  const extent = step => {
    let last = Math.floor(h / 2), gap = 0;
    for (let y = last; y >= 0 && y < h && gap < GAP; y += step) {
      if (darkRow(y)) { last = y; gap = 0; } else gap++;
    }
    return last;
  };
  return { x0, x1, y0: extent(-1), y1: extent(1) };
}

// One transition's frames: the glitch frames. The pixels watched are in a
// band at the top, the left and the bottom, away from the pointer's path:
// - outside the sidebar's columns, those light both before the action and
//   once it has ended, in every frame (the docked layout's card corner is
//   dark at one end, and not watched; its last 6 columns are watched as
//   these: dark at both ends with the sidebar out);
// - in its columns, where it slides, those light before or after (where no
//   sidebar is at one end), in each frame where the sidebar does not cover
//   them then (its measured rect, 2 points round it for its antialiased
//   edge and shadow). A notch beside the sliding sidebar, or a dark corner
//   above or below it, is caught; the sidebar itself never is.
// (Before 2026-11-20 the sidebar's columns were watched only from 0.5 s on,
// light at the end: the peek starts about 0.6 s after the pointer reaches
// the edge, so its own dark body and its text were taken for black.)
function checkFrames(frames, w, h, m) {
  const pre = frames[0].lum, settled = frames.at(-1).lum;
  const sidebarW = m.sidebarW ?? 0;
  const sidebarRight = Math.max(0, sidebarW - 6);
  const path = m.pointer?.map(([px, py]) => [px - m.rect.x, py - m.rect.y]);
  const watch = [], inSidebarCols = [];
  for (let y = 0; y < h; y++) {
    const rowBand = y < BAND || y >= h - BAND;
    for (let x = 0; x < w; x++) {
      if (!rowBand && x >= BAND) continue;
      if (path && distToSegment(x, y, path[0], path[1]) < POINTER_PAD) continue;
      const p = y * w + x;
      if (x >= sidebarRight) {
        if (pre[p] > LIGHT && settled[p] > LIGHT) watch.push(p);
      } else if (pre[p] > LIGHT || settled[p] > LIGHT) {
        inSidebarCols.push(p);
      }
    }
  }
  const row = { frames: frames.length, watched: watch.length + inSidebarCols.length, glitchFrames: 0, worst: null };
  // A run narrower than 8 points is the sidebar only while it moves (the
  // first or last sliver of a slide): one that keeps its width from frame
  // to frame is a dark column left at the edge, the glitch.
  const rects = frames.map(f => sidebarRect(f.lum, w, h, sidebarW));
  const width = r => (r ? r.x1 - r.x0 + 1 : 0);
  for (const [i, f] of frames.entries()) {
    if (i === 0 || i === frames.length - 1) continue;
    let s = rects[i];
    if (s && width(s) < 8 && width(rects[i - 1]) === width(s) && width(rects[i + 1]) === width(s)) s = null;
    const covered = p => {
      if (!s) return false;
      const x = p % w, y = (p - x) / w;
      return x >= s.x0 - 2 && x <= s.x1 + 2 && y >= s.y0 - 2 && y <= s.y1 + 2;
    };
    let n = 0, x0 = w, y0 = h, x1 = -1, y1 = -1;
    const bands = new Set();
    const count = p => {
      if (f.lum[p] >= DARK) return;
      n++;
      const x = p % w, y = (p - x) / w;
      x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x); y1 = Math.max(y1, y);
      bands.add(y < BAND ? 'top' : y >= h - BAND ? 'bottom' : 'left');
    };
    // (Outside its columns too: the peek's spring overshoots its resting
    // edge by a point or two.)
    for (const p of watch) if (!covered(p)) count(p);
    for (const p of inSidebarCols) if (!covered(p)) count(p);
    if (n < MIN_PX) continue;
    row.glitchFrames++;
    if (!row.worst || n > row.worst.px) {
      row.worst = { t: +f.t.toFixed(3), px: n, bbox: [x0, y0, x1 - x0 + 1, y1 - y0 + 1], bands: [...bands],
                    sidebar: s };
    }
  }
  return row;
}

// --self-test: synthetic frames, so that the check is shown to fail where
// it must: a dark sidebar sliding in over a light page passes; the same
// with a dark notch beside its top corner fails; so does a dark corner left
// in the top band after the sidebar is gone.
function selfTest() {
  const w = 400, h = 300, side = 120;
  const frame = (t, edge, notch) => {
    const lum = new Uint8Array(w * h).fill(240);
    for (let y = 8; y < h - 8; y++) {
      for (let x = 8; x < 8 + edge; x++) lum[y * w + x] = 20;
      // Light text in the sidebar.
      if (edge > 40 && y % 30 === 0) for (let x = 20; x < 30; x++) lum[y * w + x] = 230;
    }
    if (notch) for (let y = 0; y < 12; y++) for (let x = 8 + edge; x < 8 + edge + 10; x++) lum[y * w + x] = 0;
    return { t, lum };
  };
  const slide = notchAt => Array.from({ length: 30 }, (_, i) => {
    const edge = Math.round(side * Math.min(1, Math.max(0, (i - 5) / 18)));
    return frame(i / 30, edge, i === notchAt);
  });
  const m = { sidebarW: side + 8, rect: { x: 0, y: 0 } };
  const cases = [
    ['a sidebar sliding in', checkFrames(slide(-1), w, h, m), false],
    ['a notch beside it (a frame)', checkFrames(slide(15), w, h, m), true],
    ['a dark corner once it is in', checkFrames(slide(28), w, h, m), true],
    ['a sidebar sliding out', checkFrames(slide(-1).reverse(), w, h, m), false],
  ];
  let ok = true;
  for (const [name, row, glitch] of cases) {
    const pass = !!row.glitchFrames === glitch;
    ok &&= pass;
    console.log(`${pass ? 'ok  ' : 'FAIL'} self-test: ${name}: ${row.glitchFrames} glitch frame(s), ` +
                `expected ${glitch ? 'some' : 'none'}`);
  }
  return ok;
}
if (args.includes('--self-test')) process.exit(selfTest() ? 0 : 1);

const report = [];
let failed = false;
for (const [i, m] of acts.entries()) {
  const next = acts[i + 1];
  const t = vt(m.t);
  const end = Math.min(t + 1.7, next ? vt(next.t) - 0.1 : t + 1.7);
  if (t - 0.25 < 0 || end <= t + 0.3) continue;
  const { w, h, frames } = await decode(m.rect, t - 0.25, end);
  if (frames.length < 10) continue;
  const row = { label: m.label, round: m.round, t: +t.toFixed(3), ...checkFrames(frames, w, h, m) };
  if (row.glitchFrames) failed = true;
  report.push(row);
  console.log(`r${String(row.round).padStart(2, '0')} ${row.label.padEnd(12)} ${row.t.toFixed(3)}s ` +
              `${row.frames} frames, ${row.watched} px watched: ` +
              (row.glitchFrames ? `${row.glitchFrames} BLACK frame(s), worst ${JSON.stringify(row.worst)}` : 'ok'));
}
if (!report.length) {
  console.log('FAIL: no action could be checked (marks outside the video?)');
  process.exit(1);
}
const out = opt('out', null);
if (out) fs.writeFileSync(out, JSON.stringify({ band: BAND, dark: DARK, light: LIGHT, minPx: MIN_PX, report }, null, 1));
console.log(failed ? 'FAIL: black at the window\'s edge during a transition' : `PASS: ${report.length} transitions`);
process.exit(failed ? 1 : 0);
