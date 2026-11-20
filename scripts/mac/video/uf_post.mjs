// The checks run_flows.sh runs on a user flow's recording once Koan has
// quit, added to the flow's summary.json as steps of their own:
//
//   node uf_post.mjs edge <flow dir> <video>
//     edge_check.mjs (the black-edge check of compact mode) on the flow's
//     marks: step UF-CMP-5.
//   node uf_post.mjs still <flow dir> <video>
//     every pair of marks with `still: {from, to}` and `stillRect` (points in
//     the window): no pixel of that rect may change in any frame between
//     them (a flicker, a resize, a re-layout). A mark with `control: true`
//     is the opposite: the flow changed the page there on purpose, and the
//     check must see it, which shows the check can fail. Steps <id>.still.
//   node uf_post.mjs video <flow dir> <compress.sh --check line>
//     records the compressed video's size and SSIM in the summary.
//   node uf_post.mjs missing <flow dir> <flow> <exit code>
//     a flow that died before writing its summary gets a failing one.
import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const here = path.dirname(new URL(import.meta.url).pathname);
const FFMPEG = process.env.FFMPEG ?? (fs.existsSync('/opt/homebrew/bin/ffmpeg') ? '/opt/homebrew/bin/ffmpeg' : 'ffmpeg');
const [cmd, dir, arg, arg2] = process.argv.slice(2);
const summaryFile = path.join(dir ?? '.', 'summary.json');
const readSummary = () => JSON.parse(fs.readFileSync(summaryFile, 'utf8'));
function addStep(s, rec) {
  s.steps.push({ needs: ['recording'], shots: [], agent: { calls: 0, ms: 0 }, detail: '', startedAt: Date.now(), ms: 0,
                 skipped: null, ...rec });
  const ran = s.steps.filter(x => x.ok !== null);
  if (s.status === 'pass' && ran.some(x => x.ok === false)) s.status = 'fail';
  fs.writeFileSync(summaryFile, JSON.stringify(s, null, 1));
  const failed = rec.checks.find(c => !c.ok);
  console.log(rec.ok ? `PASS  ${rec.id}` : `FAIL  ${rec.id}: ${failed?.name} -- ${failed?.detail}`);
}

if (cmd === 'missing') {
  if (!fs.existsSync(summaryFile)) {
    fs.writeFileSync(summaryFile, JSON.stringify({
      flow: arg, title: arg, feature: '', status: 'fail', stopped: `the flow exited ${arg2} before writing a summary` +
      ' (see flow.log)', steps: [], agent: { calls: 0, ms: 0, commands: [] }, info: [], wallMs: 0 }, null, 1));
  }
  process.exit(0);
}
if (!fs.existsSync(summaryFile)) {
  console.error(`uf_post.mjs: no ${summaryFile}`);
  process.exit(2);
}

if (cmd === 'video') {
  const s = readSummary();
  const f = (arg ?? '').split('\t');
  s.video = f[0] === 'check' ? { file: path.relative(dir, f[1]), bytesBefore: +f[2], bytesAfter: +f[3], from: f[4], to: f[5],
                                 ssim: f[6], verdict: f[8] } : { line: arg };
  fs.writeFileSync(summaryFile, JSON.stringify(s, null, 1));
  process.exit(0);
}

if (cmd === 'edge') {
  const video = arg;
  const report = path.join(dir, 'edge_check.json');
  const r = spawnSync(process.execPath, [path.join(here, 'edge_check.mjs'), video, path.join(dir, 'marks.json'),
                                         '--out', report], { encoding: 'utf8', timeout: 600000 });
  fs.writeFileSync(path.join(dir, 'edge_check.log'), `${r.stdout ?? ''}${r.stderr ?? ''}`);
  const lines = (r.stdout ?? '').trim().split('\n');
  // The check's own self-test first: it must still see a notch beside the
  // sliding sidebar, and not take the sidebar for one.
  const self = spawnSync(process.execPath, [path.join(here, 'edge_check.mjs'), '--self-test'],
                         { encoding: 'utf8', timeout: 60000 });
  const s = readSummary();
  addStep(s, { id: 'UF-CMP-5', title: 'The recording: no black at the window\'s edges in any frame of any transition',
               ok: r.status === 0 && self.status === 0, checks: [{
                 name: 'edge_check.mjs --self-test: it catches a synthetic notch and passes a sliding sidebar',
                 ok: self.status === 0, detail: (self.stdout ?? '').trim().split('\n').join(' | ') }, {
                 name: 'edge_check.mjs: PASS (every frame of every compact-on/off and peek-in/out)', ok: r.status === 0,
                 detail: r.status === 0 ? lines.at(-1) : `${lines.filter(l => /BLACK|FAIL/.test(l)).slice(0, 4).join(' | ')} ` +
                         `(exit ${r.status}; edge_check.log)` }] });
  process.exit(0);
}

if (cmd === 'still') {
  const video = arg;
  const rec = JSON.parse(fs.readFileSync(`${video}.rec.json`, 'utf8'));
  const marks = JSON.parse(fs.readFileSync(path.join(dir, 'marks.json'), 'utf8'));
  const scale = rec.scale ?? 2;
  const vt = t => (t - rec.t0) / 1000;
  const THRESH = 24, MAX_PX = 40, CONTROL_MIN_PX = 400;
  // The rect's frames at one pixel per point, as rgb24.
  const decode = (r, from, to) => new Promise((resolve, reject) => {
    const even = n => Math.max(2, Math.floor(n / 2) * 2);
    const ff = spawn(FFMPEG, ['-nostdin', '-v', 'error', '-ss', from.toFixed(3), '-to', to.toFixed(3), '-i', video, '-vf',
                              `fps=30,crop=${even(r.w * scale)}:${even(r.h * scale)}:${even(r.x * scale)}:${even(r.y * scale)},` +
                              `scale=${r.w}:${r.h}:flags=area`, '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1']);
    const size = r.w * r.h * 3, frames = [];
    let pending = Buffer.alloc(0);
    ff.stdout.on('data', c => {
      pending = Buffer.concat([pending, c]);
      while (pending.length >= size) {
        frames.push(Buffer.from(pending.subarray(0, size)));
        pending = pending.subarray(size);
      }
    });
    ff.on('error', reject);
    ff.on('close', code => (code === 0 ? resolve(frames) : reject(new Error(`ffmpeg exited ${code}`))));
  });
  const changed = (a, b) => {
    let n = 0;
    for (let i = 0; i < a.length; i += 3) {
      if (Math.abs(a[i] - b[i]) > THRESH || Math.abs(a[i + 1] - b[i + 1]) > THRESH || Math.abs(a[i + 2] - b[i + 2]) > THRESH) n++;
    }
    return n;
  };
  const s = readSummary();
  for (const m of marks.marks.filter(x => x.still && x.stillRect)) {
    const r = m.stillRect;
    const id = `${s.steps.find(x => x.startedAt <= m.still.from && x.startedAt + x.ms >= m.still.to - 50)?.id ?? 'flow'}.still`;
    let frames = [];
    let error = '';
    try {
      frames = await decode(r, Math.max(0, vt(m.still.from)), vt(m.still.to));
    } catch (e) {
      error = e.message;
    }
    let worst = 0, worstAt = -1, drift = 0;
    for (let i = 1; i < frames.length; i++) {
      const c = changed(frames[i - 1], frames[i]);
      if (c > worst) [worst, worstAt] = [c, i];
      drift = Math.max(drift, changed(frames[0], frames[i]));
    }
    const at = worstAt >= 0 ? `${(vt(m.still.from) + worstAt / 30).toFixed(3)} s` : '-';
    const detail = `${frames.length} frames of ${r.w}x${r.h} pt at ${r.x},${r.y}; most changed in one frame ${worst} px ` +
                   `(at ${at}), most off the first frame ${drift} px${error ? `; ${error}` : ''}`;
    const ok = !error && frames.length > 5 && (m.control ? worst >= CONTROL_MIN_PX : worst <= MAX_PX && drift <= MAX_PX);
    addStep(s, {
      id: m.control ? `${id}-control` : id,
      title: m.control ? 'Control: the still check sees a change made on purpose' :
                         'The recording: the person\'s page did not change at all during the agent\'s captures',
      ok, checks: [{ name: m.control ? `a change of at least ${CONTROL_MIN_PX} px is seen` :
                           `no frame changed more than ${MAX_PX} px (no flicker, resize or re-layout)`, ok, detail }],
    });
  }
  process.exit(0);
}

console.error('usage: uf_post.mjs edge|still <flow dir> <video> | video <flow dir> <line> | missing <flow dir> <flow> <code>');
process.exit(2);
