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
if (!video || !marksFile) {
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

const rec = JSON.parse(fs.readFileSync(`${video}.rec.json`, 'utf8'));
const marks = JSON.parse(fs.readFileSync(marksFile, 'utf8'));
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

const report = [];
let failed = false;
for (const [i, m] of acts.entries()) {
  const next = acts[i + 1];
  const t = vt(m.t);
  const end = Math.min(t + 1.7, next ? vt(next.t) - 0.1 : t + 1.7);
  if (t - 0.25 < 0 || end <= t + 0.3) continue;
  const { w, h, frames } = await decode(m.rect, t - 0.25, end);
  if (frames.length < 10) continue;
  const pre = frames[0].lum, settled = frames.at(-1).lum;
  // The pixels to watch, in a band at the top, the left or the bottom, away
  // from the pointer:
  // - outside the sidebar's columns, in every frame, those that are light
  //   both before the action and once it has ended;
  // - in the sidebar's columns (the left band is in them), where the sidebar
  //   slides and is dark on its way, only once the slide is over (0.5 s on),
  //   those that are light at the end: nothing may stay or flash black there.
  // (Its last columns are watched as any: the sidebar is dark there in the
  // reference with it out, and its icons are further in. Upstream's corner
  // stood right beside it.)
  const sidebarRight = Math.max(0, (m.sidebarW ?? 0) - 6);
  const path = m.pointer?.map(([px, py]) => [px - m.rect.x, py - m.rect.y]);
  const watch = [], watchLate = [];
  for (let y = 0; y < h; y++) {
    const rowBand = y < BAND || y >= h - BAND;
    for (let x = 0; x < w; x++) {
      if (!rowBand && x >= BAND) continue;
      if (path && distToSegment(x, y, path[0], path[1]) < POINTER_PAD) continue;
      const p = y * w + x;
      if (x >= sidebarRight) {
        if (pre[p] > LIGHT && settled[p] > LIGHT) watch.push(p);
      } else if (settled[p] > LIGHT) {
        watchLate.push(p);
      }
    }
  }
  const row = { label: m.label, round: m.round, t: +t.toFixed(3), frames: frames.length,
                watched: watch.length + watchLate.length, glitchFrames: 0, worst: null };
  for (const f of frames.slice(1, -1)) {
    let n = 0, x0 = w, y0 = h, x1 = -1, y1 = -1;
    const bands = new Set();
    const count = p => {
      if (f.lum[p] >= DARK) return;
      n++;
      const x = p % w, y = (p - x) / w;
      x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x); y1 = Math.max(y1, y);
      bands.add(y < BAND ? 'top' : y >= h - BAND ? 'bottom' : 'left');
    };
    watch.forEach(count);
    if (f.t >= t + 0.5) watchLate.forEach(count);
    if (n < MIN_PX) continue;
    row.glitchFrames++;
    if (!row.worst || n > row.worst.px) {
      row.worst = { t: +f.t.toFixed(3), px: n, bbox: [x0, y0, x1 - x0 + 1, y1 - y0 + 1], bands: [...bands] };
    }
  }
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
