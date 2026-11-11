// The node side of frames.sh (Homebrew's ffmpeg on the bench has no
// drawtext, so the timestamps are drawn here).
//
//   node sheet.mjs plan <marks.json> <video.rec.json> <before> <after> <crop>
//     Prints one tab-separated line per range to cut: from, to (video
//     seconds), a label, the crop (x:y:w:h in video pixels, or "-"). Ranges
//     are [mark - before, mark + after] around every action mark, merged when
//     they overlap. <crop> is "-", "sidebar" (the sidebar's rect from the
//     marks, padded) or x,y,w,h in points within the recorded window.
//
//   node sheet.mjs stamp <w> <h> <fps> <from> <tile> <prefix> <rec.json|-> <marks.json|->
//     Reads rgb24 frames (w x h) on stdin and writes them to stdout with the
//     video time drawn at the top left, and (with the marks) the round of the
//     latest action mark and the seconds since it: "12.345s r03 +0.43". Appends to <prefix>diff.tsv one line per
//     frame: the pixels that changed since the previous frame (any channel
//     off by more than $DIFF_THRESHOLD, default 24: H.264 noise stays under
//     it) and their bounding box; and to <prefix>sheets.tsv one line per
//     contact sheet (each <tile> frames, e.g. 16 for 4x4).
import fs from 'node:fs';

const [cmd, ...a] = process.argv.slice(2);
const readJson = f => (f && f !== '-' && fs.existsSync(f) ? JSON.parse(fs.readFileSync(f, 'utf8')) : null);

if (cmd === 'plan') {
  const [marksFile, recFile, before, after, cropArg] = a;
  const marks = readJson(marksFile), rec = readJson(recFile);
  if (!marks || !rec) throw new Error('plan: need the marks and the .rec.json');
  const vt = t => (t - rec.t0) / 1000;
  const acts = marks.marks.filter(m => m.action).map(m => ({ ...m, v: vt(m.t) }))
      .filter(m => m.v > -Number(after)).sort((x, y) => x.v - y.v);
  const ranges = [];
  for (const m of acts) {
    const from = Math.max(0, m.v - Number(before)), to = m.v + Number(after);
    const last = ranges.at(-1);
    if (last && from <= last.to) {
      last.to = Math.max(last.to, to);
      last.n++;
    } else {
      ranges.push({ from, to, label: `r${String(m.round ?? 0).padStart(2, '0')}-${m.label}`, n: 1 });
    }
  }
  // The crop, in video pixels.
  let crop = '-';
  const s = rec.scale ?? 2;
  const toPx = (x, y, w, h) => {
    const even = n => Math.max(2, Math.floor(n / 2) * 2);
    return `${even(x * s)}:${even(y * s)}:${even(w * s)}:${even(h * s)}`;
  };
  if (cropArg === 'sidebar') {
    const sb = marks.sidebar;
    if (!sb) throw new Error('plan: the marks have no sidebar rect');
    const x = Math.max(0, sb.x - rec.rect.x - 8), y = Math.max(0, sb.y - rec.rect.y - 8);
    crop = toPx(x, y, Math.min(sb.w + 16, rec.rect.w - x), Math.min(sb.h + 16, rec.rect.h - y));
  } else if (cropArg && cropArg !== '-') {
    const [x, y, w, h] = cropArg.split(',').map(Number);
    crop = toPx(x, y, w, h);
  }
  for (const r of ranges) {
    console.log([r.from.toFixed(3), r.to.toFixed(3), r.n > 1 ? `${r.label}+${r.n - 1}` : r.label, crop].join('\t'));
  }
  process.exit(0);
}

if (cmd !== 'stamp') throw new Error(`unknown command ${cmd}`);
const [W, H, FPS, FROM, TILE] = a.slice(0, 5).map(Number);
const [prefix, recFile, marksFile] = a.slice(5);
const rec = readJson(recFile), marks = readJson(marksFile);
const THRESH = Number(process.env.DIFF_THRESHOLD ?? 24);
const size = W * H * 3;

// A 5x7 font: what the stamps use.
const FONT = {
  0: '01110 10001 10011 10101 11001 10001 01110', 1: '00100 01100 00100 00100 00100 00100 01110',
  2: '01110 10001 00001 00010 00100 01000 11111', 3: '11110 00001 00001 01110 00001 00001 11110',
  4: '00010 00110 01010 10010 11111 00010 00010', 5: '11111 10000 11110 00001 00001 10001 01110',
  6: '00110 01000 10000 11110 10001 10001 01110', 7: '11111 00001 00010 00100 01000 01000 01000',
  8: '01110 10001 10001 01110 10001 10001 01110', 9: '01110 10001 10001 01111 00001 00010 01100',
  '.': '00000 00000 00000 00000 00000 01100 01100', ':': '00000 01100 01100 00000 01100 01100 00000',
  s: '00000 00000 01111 10000 01110 00001 11110', w: '00000 00000 10001 10001 10101 10101 01010',
  r: '00000 00000 10110 11001 10000 10000 10000', '+': '00000 00100 00100 11111 00100 00100 00000',
  '-': '00000 00000 00000 11111 00000 00000 00000', ' ': '00000 00000 00000 00000 00000 00000 00000',
};
// Font pixels: ~2 screen pixels each once the sheet's tile is ~360 px wide.
const U = Math.max(2, Math.round((2 * W) / 360));
function stamp(buf, text) {
  const cw = 6 * U, ch = 9 * U;
  const bw = text.length * cw + U, bh = ch;
  for (let y = 0; y < Math.min(bh, H); y++) {
    buf.fill(0, y * W * 3, (y * W + Math.min(bw, W)) * 3);
  }
  [...text].forEach((c, i) => {
    const rows = (FONT[c] ?? FONT[' ']).split(' ');
    rows.forEach((row, ry) => [...row].forEach((bit, rx) => {
      if (bit !== '1') return;
      for (let dy = 0; dy < U; dy++) {
        const y = U + ry * U + dy;
        const x0 = U + i * cw + rx * U;
        if (y >= H || x0 + U > W) continue;
        buf.fill(255, (y * W + x0) * 3, (y * W + x0 + U) * 3);
      }
    }));
  });
}

const actionMarks = (marks?.marks ?? []).filter(m => m.action && rec)
    .map(m => ({ v: (m.t - rec.t0) / 1000, round: m.round ?? 0, label: `r${m.round ?? 0}:${m.label}` }));
const diffOut = fs.openSync(`${prefix}diff.tsv`, 'a');
const sheetsOut = fs.openSync(`${prefix}sheets.tsv`, 'a');
let prev = null, n = 0, pending = Buffer.alloc(0);
let sheet = { first: 0, max: 0, changed: 0, frames: 0 };
const MIN_PX = Number(process.env.MIN_CHANGED_PX ?? 1200);

function flushSheet() {
  if (!sheet.frames) return;
  const k = Math.floor((n - 1) / TILE);
  const t1 = FROM + sheet.first / FPS, t2 = FROM + (n - 1) / FPS;
  const inside = actionMarks.filter(m => m.v >= t1 - 0.5 / FPS && m.v <= t2 + 0.5 / FPS).map(m => m.label);
  fs.writeSync(sheetsOut, [`${prefix}sheet_${String(k).padStart(3, '0')}.png`, t1.toFixed(3), t2.toFixed(3),
                           sheet.frames, sheet.max, sheet.changed, inside.join(',') || '-'].join('\t') + '\n');
  sheet = { first: n, max: 0, changed: 0, frames: 0 };
}

function frame(buf) {
  const v = FROM + n / FPS;
  let changed = 0, x1 = W, y1 = H, x2 = -1, y2 = -1;
  if (prev) {
    for (let y = 0; y < H; y++) {
      const row = y * W * 3;
      for (let x = 0; x < W; x++) {
        const i = row + x * 3;
        if (Math.abs(buf[i] - prev[i]) > THRESH || Math.abs(buf[i + 1] - prev[i + 1]) > THRESH ||
            Math.abs(buf[i + 2] - prev[i + 2]) > THRESH) {
          changed++;
          if (x < x1) x1 = x;
          if (x > x2) x2 = x;
          if (y < y1) y1 = y;
          if (y > y2) y2 = y;
        }
      }
    }
  }
  prev = Buffer.from(buf);
  const wall = rec ? Math.round(rec.t0 + v * 1000) : null;
  fs.writeSync(diffOut, [n, v.toFixed(3), wall ?? '-', changed, (100 * changed / (W * H)).toFixed(3),
                         changed ? `${x1},${y1},${x2 - x1 + 1},${y2 - y1 + 1}` : '-'].join('\t') + '\n');
  // The video time, and the round and time since the latest action mark.
  const last = actionMarks.findLast(m => m.v <= v + 0.5 / FPS);
  stamp(buf, `${v.toFixed(3)}s${last ? ` r${String(last.round).padStart(2, '0')} +${(v - last.v).toFixed(2)}` : ''}`);
  const ok = process.stdout.write(buf);
  sheet.frames++;
  sheet.max = Math.max(sheet.max, changed);
  if (changed >= MIN_PX) sheet.changed++;
  n++;
  if (n % TILE === 0) flushSheet();
  return ok;
}

// Frames in, stamped frames out, waiting for the encoder when it lags (pipes
// are asynchronous on macOS: without that the frames pile up in memory).
for await (const chunk of process.stdin) {
  pending = pending.length ? Buffer.concat([pending, chunk]) : chunk;
  while (pending.length >= size) {
    const buf = Buffer.from(pending.subarray(0, size));
    pending = pending.subarray(size);
    if (!frame(buf)) await new Promise(r => process.stdout.once('drain', r));
  }
}
flushSheet();
fs.closeSync(diffOut);
fs.closeSync(sheetsOut);
