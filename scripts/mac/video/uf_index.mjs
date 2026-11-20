// Writes index.md for a run_flows.sh run: the one page the maintainer reads
// first. Per flow its result, steps, agent calls and time; per step its
// result, the first failing check, its time and koan calls, its key
// screenshot and its contact sheet; then every failure in full and the
// flows' INFO lines.
//
//   node uf_index.mjs <run dir> [--app <Koan.app>] > <run dir>/index.md
import fs from 'node:fs';
import path from 'node:path';

const args = process.argv.slice(2);
const runDir = args[0];
const app = args.includes('--app') ? args[args.indexOf('--app') + 1] : '';
const note = args.includes('--note') ? args[args.indexOf('--note') + 1] : '';
if (!runDir || !fs.existsSync(runDir)) {
  console.error('usage: uf_index.mjs <run dir> [--app <Koan.app>]');
  process.exit(2);
}
const flows = fs.readdirSync(runDir).filter(d => fs.existsSync(path.join(runDir, d, 'summary.json'))).sort();
const order = (process.env.UF_ORDER ?? '').split(',').filter(Boolean);
flows.sort((a, b) => (order.indexOf(a) + 1 || 99) - (order.indexOf(b) + 1 || 99) || a.localeCompare(b));
const cell = s => String(s ?? '').replace(/\|/g, '\\|').replace(/\n/g, ' ');
const link = (text, p) => (p ? `[${cell(text)}](${p.split(path.sep).map(encodeURIComponent).join('/')})` : '');
const secs = ms => (ms ? (ms / 1000).toFixed(1) : '0');
const verdict = ok => (ok === null ? 'SKIP' : ok ? 'PASS' : '**FAIL**');

// The contact sheet of each step: the first sheet whose range has its mark.
function sheetsOf(flowDir) {
  const tsv = path.join(flowDir, 'sheets', 'sheets.tsv');
  if (!fs.existsSync(tsv)) return [];
  return fs.readFileSync(tsv, 'utf8').trim().split('\n').slice(1).map(l => l.split('\t'))
      .map(([sheet, from, to, frames, maxPx, changed, marks]) => ({
        sheet: path.relative(flowDir, sheet.startsWith('/') ? sheet : path.join(flowDir, 'sheets', sheet)), from, to,
        changed: +changed, marks: (marks ?? '').split(',') }));
}

const lines = [];
const all = flows.map(f => ({ name: f, dir: path.join(runDir, f), s: JSON.parse(fs.readFileSync(path.join(runDir, f, 'summary.json'), 'utf8')) }));
const count = st => all.filter(x => x.s.status === st).length;
const steps = all.flatMap(x => x.s.steps);
lines.push(`# Koan user flows: ${path.basename(runDir)}`, '');
if (app) lines.push(`App: \`${app}\``, '');
if (note) lines.push(`> ${note}`, '');
lines.push(`**${all.length} flows: ${count('pass')} passed, ${count('fail')} failed, ${count('skip')} skipped, ` +
           `${count('stopped') + count('running')} stopped.** Steps: ${steps.filter(s => s.ok === true).length} passed, ` +
           `${steps.filter(s => s.ok === false).length} failed, ${steps.filter(s => s.ok === null).length} skipped.`, '');
lines.push('Read this page first. Open a screenshot or a sheet only for a FAIL, or where a step says to look. Test cases: ' +
           '`.agent/notes/user-flows.md`.', '');
lines.push('| flow | feature | result | steps (pass/fail/skip) | koan calls | agent s | flow s | video |', '|---|---|---|---|---|---|---|---|');
for (const { name, dir, s } of all) {
  const p = s.steps.filter(x => x.ok === true).length, f = s.steps.filter(x => x.ok === false).length;
  const k = s.steps.filter(x => x.ok === null).length;
  const result = s.status === 'pass' ? 'PASS' : s.status === 'skip' ? `SKIP: ${cell(s.stopped)}` :
      s.status === 'fail' ? '**FAIL**' : `**STOPPED**: ${cell(s.stopped)}`;
  const video = s.video?.file ? link(`${s.video.to}, ${s.video.ssim}`, `${name}/${s.video.file}`) :
      fs.existsSync(path.join(dir, 'video.720.mp4')) ? link('720p', `${name}/video.720.mp4`) : '';
  lines.push(`| [${name}](#${name}) | ${cell(s.feature)} | ${result} | ${p}/${f}/${k} | ${s.agent?.calls ?? 0} | ` +
             `${secs(s.agent?.ms)} | ${secs(s.wallMs)} | ${video} |`);
}
lines.push('');
for (const { name, dir, s } of all) {
  lines.push(`## ${name}`, '', `${cell(s.title)} (${cell(s.feature)}). Result: ${s.status.toUpperCase()}` +
             `${s.stopped ? ` (${cell(s.stopped)})` : ''}. ${s.steps.length} steps, ${s.agent?.calls ?? 0} koan calls ` +
             `(${secs(s.agent?.ms)} s of agent time), ${secs(s.wallMs)} s in all. Log: ${link('flow.log', `${name}/flow.log`)}`, '');
  if (!s.steps.length) continue;
  const sheets = sheetsOf(dir);
  lines.push('| step | what the user does | result | first failing check | s | koan calls | screenshot | sheet |',
             '|---|---|---|---|---|---|---|---|');
  for (const st of s.steps) {
    const bad = st.checks?.find(c => !c.ok);
    const shot = [...(st.shots ?? [])].reverse().find(x => x.path);
    const sheet = sheets.find(x => x.marks.includes(`step:${st.id}`));
    lines.push(`| ${st.id} | ${cell(st.title)}${st.needs?.length ? ` _(${st.needs.join(', ')})_` : ''} | ${verdict(st.ok)}` +
               `${st.ok === null ? `: ${cell(st.skipped)}` : ''} | ${bad ? cell(`${bad.name}${bad.detail ? ` -- ${bad.detail}` : ''}`).slice(0, 220) : ''} | ` +
               `${secs(st.ms)} | ${st.agent?.calls ?? 0} | ${shot ? link(shot.label, `${name}/${shot.path}`) : ''} | ` +
               `${sheet ? link(`${(+sheet.from).toFixed(1)} s${sheet.changed ? '' : ' (still)'}`, `${name}/${sheet.sheet}`) : ''} |`);
  }
  lines.push('');
  const failures = s.steps.filter(st => st.ok === false);
  if (failures.length) {
    lines.push('Failures:', '');
    for (const st of failures) {
      for (const c of st.checks.filter(x => !x.ok)) lines.push(`- ${st.id}: ${cell(c.name)}${c.detail ? ` -- \`${cell(c.detail).slice(0, 500)}\`` : ''}`);
    }
    lines.push('');
  }
  if (s.info?.length) {
    lines.push('Measured:', '', ...s.info.map(i => `- ${cell(i)}`), '');
  }
}
console.log(lines.join('\n'));
