// Koan regression pass on the Mac. env: BUILD=<label> APP_PATH=<app on the Mac> [OVERRIDE_USER_AWAY=1 only when the maintainer says so]
// Steps: launch (fresh profile, logs captured) / new tab, Cmd+T, Cmd+L, Esc / close last tab / Cmd+S x6 / fullscreen x2 + Cmd+T in fullscreen /
//        Incognito / Spaces (create, switch) / small window / theme picker (not automated yet) / quit. Then scan the log and compare with the previous run.
import { execSync } from 'node:child_process';
import { mkdirSync, writeFileSync, readFileSync, existsSync } from 'node:fs';
import { sleep, pages, browserCall, waitFor } from './lib/koan.mjs';
import { fviews } from './fv.mjs';
const { BUILD = 'build', APP_PATH, KEYSET = 'base' } = process.env; const OVERRIDE_USER_AWAY = '';  // the presence check can no longer be switched off  // KEYSET=base: Ctrl+S compact, Ctrl+Shift+P private (plain develop); KEYSET=cmd: Cmd+S, Cmd+Shift+P (#90 and later)
// Where the Mac is: KOAN_MAC_HOST, KOAN_MAC_USER, KOAN_MAC_KEY from the environment or scripts/mac/local.env
// (see local.env.example). Nothing about the Mac is kept in the repo.
const localEnv = (() => {
  try {
    return Object.fromEntries(readFileSync(new URL('../local.env', import.meta.url), 'utf8').split('\n')
        .map(l => l.replace(/\s+#.*$/, '').trim()).filter(l => /^KOAN_MAC_\w+=/.test(l))
        .map(l => [l.slice(0, l.indexOf('=')), l.slice(l.indexOf('=') + 1)]));
  } catch { return {}; }
})();
const macSetting = k => {
  const v = process.env[k] || localEnv[k];
  if (!v) {
    console.error(`vtabs.mjs: ${k} is not set. Set KOAN_MAC_HOST, KOAN_MAC_USER and KOAN_MAC_KEY in the environment or in scripts/mac/local.env (copy local.env.example).`);
    process.exit(2);
  }
  if (v.startsWith('<')) {
    console.error(`vtabs.mjs: ${k} is still the placeholder ${v}. Put the real value in the environment or in scripts/mac/local.env.`);
    process.exit(2);
  }
  return v;
};
const MAC_TARGET = `${macSetting('KOAN_MAC_USER')}@${macSetting('KOAN_MAC_HOST')}`;
const MAC_KEY = macSetting('KOAN_MAC_KEY').replace(/^~/, process.env.HOME);
const MAC_HOME = `/Users/${macSetting('KOAN_MAC_USER')}`;
const M = process.env.KOAN_MAC_ROOT || localEnv.KOAN_MAC_ROOT || `${MAC_HOME}/work/koan-mac`;
const SSH = ['ssh', '-i', MAC_KEY, '-o', 'BatchMode=yes', '-o', 'ControlPath=/tmp/koan-cm.sock', MAC_TARGET];
const rawMac = (cmd, t = 90000) => execSync([...SSH, JSON.stringify(cmd)].join(' '), { timeout: t }).toString().trim();
// Input guard (after the 15:12 collision with the tester): activate/unmin need the tester's grant (tmp/screen-granted, written by me on an explicit "screen free", removed on "testing"); keypress2/ptr also need MY Koan pid to be the frontmost app. Otherwise the run ends at once (cleanup removes only my own pass- Koans).
const mac = (cmd, t = 90000) => {
  let pre = '';
  // Every check that applies, not the first: a line with a pointer move and key presses needs the grant and the frontmost check.
  const needGrant = /\/tmp\/(activate|unmin)\b/.test(cmd) || /\/tmp\/ptr (move|pos)\b/.test(cmd);  // a pure move (also the final cursor restore, after my Koan is gone) needs the grant but not the frontmost check
  const needFront = /\/tmp\/keypress2\b/.test(cmd) || /\/tmp\/ptr (?!move\b|pos\b)/.test(cmd);
  if (needGrant) pre += `${M}/tmp/grantok.sh || exit 7; `;
  if (needFront) { let p = 0; try { p = PID; } catch {} pre += `${M}/tmp/inputok.sh ${p || 0} || exit 8; `; }
  try { return rawMac(pre + cmd, t); }
  catch (e) { if (pre && (e.status === 7 || e.status === 8)) { console.log(`STOP  input refused (${e.status === 7 ? 'the screen is not granted' : 'my Koan is not the frontmost app'}): no key or pointer event was sent. Ending the run.`); process.exit(4); } throw e; }
};
const stamp = new Date().toISOString().replace(/[-:T]/g, '').slice(0, 14);
const RD = `${M}/tmp/regress/${BUILD}-${stamp}`, LOCAL = `${process.env.HOME}/work/koan/.cache/mac-review/regress/${BUILD}-${stamp}`;
mkdirSync(LOCAL, { recursive: true });
const idle = () => Number(mac(`${M}/tmp/idle.sh`));
const sinceUser = () => Number(mac(`${M}/tmp/usergate.sh`));
const gate0 = sinceUser();
if (gate0 < 600) { console.log(`REFUSED: user input ${gate0}s ago (not mine); a run starts after 600 s without user input`); process.exit(3); }
try { rawMac(`${M}/tmp/grantok.sh`); } catch { console.log('REFUSED: the tester has not granted the screen (no "screen free" yet)'); process.exit(3); }
const RUN_START = Number(mac('date +%s'));
// start: the user is away today (the maintainer's word); the mid-run check below compares idle with my own last input
mac(`mkdir -p ${RD}; touch ${M}/tmp/regress-start.mark; ls ${MAC_HOME}/Library/Logs/DiagnosticReports | grep -i '^Koan' | sort > ${RD}/ips-before.txt`);
let PID; const steps = [];
process.on('exit', () => { try { mac(`${M}/tmp/cleanup.sh`, 120000); } catch {} });
const alive = () => { try { return mac(`ps -p ${PID} -o pid=`) !== ''; } catch { return false; } };
const newIps = () => mac(`ls ${MAC_HOME}/Library/Logs/DiagnosticReports | grep -i '^Koan' | sort | comm -13 ${RD}/ips-before.txt - || true`).split('\n').filter(Boolean);
const front = () => mac(`${M}/tmp/front.sh`);
let aborted = false, lastAct = Date.now();
// Posted key events reset HIDIdleTime too (measured: Cmd+L -> idle 1 s), so "the user is back" means: idle is lower than the time since my own last key.
const userBack = () => Number(mac(`cat ${M}/tmp/last-user-input.ts`)) > RUN_START;  // an input that was not mine since this run started
const key = async (code, flags = '') => { if (aborted) return false; if (!OVERRIDE_USER_AWAY && userBack()) { aborted = true; console.log('ABORT: the user is back (HID idle lower than the time since my last key)'); return false; } for (let i = 0; i < 4; i++) { try { mac(`${M}/tmp/activate ${PID}`); } catch {} await sleep(700); if (/Koan/.test(front())) { mac(`${M}/tmp/keypress2 ${PID} ${code} '${flags}'`); lastAct = Date.now(); return true; } await sleep(800); } return false; };
const K = { T: 17, L: 37, ESC: 53, S: 1, F: 3, P: 35, W: 13, N: 45, Q: 12, LEFT: 123, RIGHT: 124, RETURN: 36, N2: 19 };
const COMPACT = KEYSET === 'cmd' ? 'c' : 'k', PRIVATE = KEYSET === 'cmd' ? 'cs' : 'ks';
const sidebarW = async () => { try { const v = (await fviews()).find(x => x.cls === 'VerticalTabStripRegionView' && x.shown); return v ? v.w : 0; } catch { return -1; } };
let n = 0;
const shot = async tag => { try { mac(`mkdir -p ${RD}; ${M}/tmp/cap.sh ${RD}/${String(n++).padStart(2, '0')}-${tag}.png`); } catch {} };
let stopReason = null;
const cleanupAll = () => { try { return mac(`${M}/tmp/cleanup.sh`, 120000); } catch (e) { return `cleanup failed: ${String(e.message ?? e).slice(0, 80)}`; } };
async function guardBefore(name) {
  if (aborted) return stopReason ?? 'aborted earlier';
  if (userBack()) { aborted = true; return 'the user is back (HID idle lower than the time since my last input)'; }
  const prob = [];
  try {
    if (/UserNotificationCenter/.test(mac(`${M}/tmp/winall`))) prob.push('a system dialog (UserNotificationCenter) is on screen');
    for (let i = 0; i < 3; i++) { try { mac(`${M}/tmp/activate ${PID}`); } catch {} await sleep(500); if (/Koan/.test(front())) break; if (i === 2) prob.push(`Koan is not frontmost (${front().slice(0, 24)})`); }
    // The welcome (chrome://koan-welcome) is a WebView in its own widget over the window.
    const vs = await fviews().catch(() => null);
    if (vs && process.env.ONBOARD !== '1' && vs.some(v => v.shown && v.cls === 'WebView' && v.path.length <= 2)) prob.push('the onboarding overlay is on screen');
    mac(`mkdir -p ${RD}/look; ${M}/tmp/cap.sh ${RD}/look/${String(n).padStart(2, '0')}-before-${name.replace(/\W+/g, '-').slice(0, 24)}.png`);
  } catch (e) { prob.push(`look failed: ${String(e.message ?? e).slice(0, 80)}`); }
  if (prob.length) { aborted = true; stopReason = prob.join('; '); console.log(`STOP  before "${name}": ${stopReason}`); console.log(cleanupAll()); return stopReason; }
  return null;
}
async function step(name, fn, settle = 1500) {
  const t0 = Date.now(); let note = '', ok = true;
  if (!alive()) { steps.push({ name, skipped: 'koan not running' }); console.log(`SKIP  ${name} (Koan not running)`); return; }
  { const bad = await guardBefore(name); if (bad) { steps.push({ name, ok: false, note: `STOPPED: ${bad}` }); console.log(`SKIP  ${name} (stopped: ${bad})`); return; } }
  try { note = (await fn()) ?? ''; } catch (e) { ok = false; note = String(e.message ?? e).slice(0, 160); }
  await sleep(settle); const a = alive(), ips = newIps(); await shot(name.replace(/\W+/g, '-').slice(0, 24));
  const s = { name, ok: ok && a && ips.length === 0 && !aborted, alive: a, newIps: ips, note, ms: Date.now() - t0 }; steps.push(s);
  console.log(`${s.ok ? 'PASS' : 'FAIL'}  ${name}  alive=${a} newIps=${ips.length}${note ? '  ' + note : ''}`);
}
const waitMine = async () => { for (let i = 0; i < 60; i++) { const n = Number(mac(`ps -Ao command | grep -c '[M]acOS/Koan -.*user-data-dir=${M}/tmp/pass-' || true`)); if (n === 0) return; await sleep(1500); } };
await waitMine(); await sleep(1500);
const { MODE = 'main', FLAGS = '', PREFS = '' } = process.env;
if (PREFS) { const b64 = Buffer.from(PREFS).toString('base64'); mac(`rm -rf ${M}/tmp/pass-R-profile; mkdir -p ${M}/tmp/pass-R-profile/Default; echo ${b64} | base64 -D > ${M}/tmp/pass-R-profile/Default/Preferences`); }
mac(`cd ${M}; ${PREFS ? 'KEEP_PROFILE=1 ' : ''}NO_ONBOARD=1 KOAN_EXTRA_FLAGS='--vmodule=koan_compact_mode_controller=1 ${FLAGS}' APP='${APP_PATH}' nohup ./pass-launch.sh R 600 '' > logs/pass-R.out 2>&1 < /dev/null &`);
await waitFor(async () => { try { return (await pages()).length > 0; } catch { return false; } }, 40000);
PID = mac(`cat ${M}/tmp/koan-pass.pid`); const LOG = mac(`cat ${M}/tmp/last-log.txt`);
const winId = async () => (await browserCall('Browser.getWindowForTarget', { targetId: (await pages())[0].id })).result.windowId;
await step('launch + maximise', async () => { await browserCall('Browser.setWindowBounds', { windowId: await winId(), bounds: { windowState: 'maximized' } }); }, 3000);

// ---- vertical tabs lockdown checks (#154/#155): the invariant of scripts/e2e/lib/vertical_tabs.mjs on the Mac ----
const need = (cond, msg) => { if (!cond) throw new Error(msg); };
const region = vs => vs.find(v => v.cls === 'VerticalTabStripRegionView');
async function inv(when, { hidden = false } = {}) {
  const vs = await fviews(); const r = region(vs);
  need(!!r, `${when}: no VerticalTabStripRegionView`);
  need(!vs.some(v => v.cls === 'TabStripRegionView' && v.shown && v.h > 0), `${when}: a horizontal TabStripRegionView is shown`);
  const w = r.shown ? r.w : 0;
  need(!(w > 0 && w < 120), `${when}: the 56-wide icon rail is shown (strip ${w} wide)`);
  need(hidden ? w === 0 : (w >= 163 && w <= 500), `${when}: strip ${w} wide, expected ${hidden ? '0 (compact)' : '163..500'}`);
  need(!vs.some(v => v.cls === 'ToolbarView' && v.shown && v.h > 0 && !v.path.includes('TopContainerOverlayView')), `${when}: Chromium's toolbar is drawn`);
  return `${when}: ${w} wide`;
}
const settle = async (ms = 1800) => { await sleep(ms); };
const rapid = (n, gapMs, code = K.S, flags = COMPACT) => mac(Array(n).fill(`${M}/tmp/keypress2 ${PID} ${code} '${flags}'; sleep ${gapMs / 1000}`).join('; '), 90000);
const prime = async () => { mac(`${M}/tmp/activate ${PID}`); await sleep(900); };
await step('launch + maximise', async () => { await browserCall('Browser.setWindowBounds', { windowId: await winId(), bounds: { windowState: 'maximized' } }); }, 3000);
if (MODE === 'old' || MODE === 'main') {
  await step(`T1/T2/T6: the profile starts with Koan's expanded sidebar, no rail (mode ${MODE})`, async () => inv('start'));
}
if (MODE === 'main') {
  await step('T13: the compact key (Cmd+S) x6 toggles compact mode, never a rail', async () => { let o = []; for (let i = 0; i < 6; i++) { if (!(await key(K.S, COMPACT))) return 'key skipped'; await settle(); o.push(await inv(`press ${i + 1}`, { hidden: i % 2 === 0 })); } return o.join(' | '); });
  // Shift+Cmd+L is Koan's inspector (Zen's key_inspectorMac, #90), not the sidebar button's: it must open the inspector and leave the sidebar as it is, never the rail.
  await step('T13b: Shift+Cmd+L opens the inspector and leaves the sidebar alone, no rail', async () => { const a = await inv('before', { hidden: false }); if (!(await key(37, 'cs'))) return 'key skipped'; await settle(); const b = await inv('after Shift+Cmd+L', { hidden: false }); await key(37, 'cs'); await settle(); return a + ' | ' + b; });
  await step('T14: Shift+Cmd+A opens the URL bar, not Chromium tab search', async () => { if (!(await key(0, 'cs'))) return 'key skipped'; await sleep(1200); const vs = await fviews(); need(!vs.some(v => v.shown && /TabSearch/.test(v.cls) && v.cls !== 'TabSearchButton'), 'Chromium tab search bubble shown'); const bar = vs.some(v => v.shown && /UrlBar/.test(v.cls)); await key(K.ESC); await sleep(600); return `URL bar view shown: ${bar}`; });
  await step('T21a: compact toggled twice within ~250 ms ends expanded (Zen width)', async () => { await prime(); rapid(2, 60); await settle(2200); return inv('after 2 quick toggles'); });
  await step('T21b: compact toggled 3 times within ~250 ms ends hidden (width 0)', async () => { await prime(); rapid(3, 60); await settle(2200); const r = await inv('after 3 quick toggles', { hidden: true }); await key(K.S, COMPACT); await settle(); return r + ' | ' + await inv('back', { hidden: false }); });
  await step('T21c: compact toggled 20 times in 10 s ends expanded, no rail', async () => { await prime(); rapid(20, 450); await settle(2500); return inv('after 20 toggles'); });
  await step('T21d: 20 toggles then close the window mid-animation (Cmd+Shift+W)', async () => { await prime(); rapid(19, 120); mac(`${M}/tmp/keypress2 ${PID} ${K.W} 'cs'`); await sleep(3500); if (!alive()) throw new Error('Koan died when the window closed during the toggles'); return 'alive'; });
  await step('new window for the fullscreen checks', async () => { await key(K.N, 'c'); await sleep(2500); return inv('new window'); });
  await step('T22: fullscreen in/out, compact off then on: no rail', async () => { let o = []; for (const compactOn of [false, true]) { if (compactOn) { await key(K.S, COMPACT); await settle(); } await key(K.F, 'ck'); await sleep(3500); o.push(await inv(`fullscreen (compact ${compactOn ? 'on' : 'off'})`, { hidden: compactOn }).catch(e => `fs: ${e.message}`)); await key(K.F, 'ck'); await sleep(3500); o.push(await inv(`windowed (compact ${compactOn ? 'on' : 'off'})`, { hidden: compactOn })); } await key(K.S, COMPACT); await settle(); return o.join(' | '); });
}
if (MODE === 'nocompact') {
  await step('T20: with KoanCompactMode disabled, Ctrl+S and Shift+Cmd+L do nothing; no rail', async () => { const a = await inv('start'); await key(K.S, COMPACT); await settle(); await key(37, 'cs'); await settle(); return a + ' | ' + await inv('after the keys'); });
}
if (MODE === 'pinrev') {
  // Crash: BrowserAnimationController::GroupData::UpdateCurrentValues, CHECK(current_motion_), an intermittent crash ~375 ms after
  // "compact mode on, sidebar hidden" in the crash-path step "compact reveal/hide by pin, then reversed half-way" (c165r). Cause: a
  // keep-expanded lock held when the sidebar's collapse ends (taken during the collapse: the pin key, the hot zone under the pointer,
  // a bubble hold, a flash; or held from before it: the Create-a-Space form) makes upstream start its expand animation from inside
  // the collapse's ended notification, which then cleared the new motion. #176: TakeExpandedLock() waits for the collapse, and a
  // small patch to BrowserAnimationController keeps the new motion.
  // Rounds: compact on; the pin key d1 ms later (0..400: before, during and after the collapse); the pin key again d2 ms later (the
  // reversal, mid-reveal); then the strip must be out only while the pointer is at the edge, and back to hidden (a lock leak leaves it
  // out) after the pointer leaves; compact off must give the expanded strip back.
  const PIN = KEYSET === 'cmd' ? 'ca' : 'ka';
  const kp = (code, flags) => `${M}/tmp/keypress2 ${PID} ${code} '${flags}'`;
  const ptr = (x, y, ms = 50) => `${M}/tmp/ptr move ${x} ${y} ${ms}`;
  const timings = [[0, 150], [50, 150], [100, 120], [150, 150], [200, 100], [250, 180], [300, 150], [350, 150], [400, 150], [120, 250]];
  // Every failure carries the end of koan.log (lock taken / refused / let go, hot zone, settled, compact mode on / off lines).
  const logTail = (n = 40) => { try { return mac(`tail -n ${n} ${LOG}`); } catch { return ''; } };
  const needLog = (cond, msg) => { if (!cond) throw new Error(`${msg}\n--- koan.log tail ---\n${logTail()}`); };
  const compactEvents = () => Number(mac(`grep -a -c 'Koan: compact mode \\(on\\|off\\)' ${LOG} || true`));
  // One compact key press; the log must show that it toggled compact mode (a key that a focused field swallows would shift the parity
  // of the whole step, and the sidebar's width alone can't tell: it is out with compact mode off and with a held lock).
  const toggle = async (what) => {
    const before = compactEvents();
    mac(kp(K.S, COMPACT), 30000);
    await sleep(1500);
    needLog(compactEvents() === before + 1, `${what}: the compact key did not toggle compact mode`);
  };
  await prime();
  for (const park of [false, true]) {
    await step(`T27: compact on, pin at +d1 ms, pin again at +d2 ms (reversal), compact off; pointer ${park ? 'at the left edge, wiggled during the collapse' : 'away'}`, async () => {
      let n = 0;
      for (let round = 0; round < 3; round++) {
        for (const [d1, d2] of timings) {
          // The pointer first, in a call of its own (it needs the grant, the keys need the frontmost check).
          mac(ptr(park ? 3 : 700, 400), 30000);
          // A 1-2 px move at +d1 makes the hot zone see a hover while the collapse runs.
          const wiggle = park ? `${ptr(4, 400, 16)}; ` : '';
          const peeksBefore = Number(mac(`grep -a -c 'Koan: peek sidebar key' ${LOG} || true`));
          mac(`${kp(K.S, COMPACT)}; sleep ${d1 / 1000}; ${wiggle}${kp(K.S, PIN)}; sleep ${d2 / 1000}; ${kp(K.S, PIN)}; sleep 1.2`, 30000);
          n++;
          // Both pin keys must have reached the app: one that was dropped leaves the sidebar legitimately pinned (not a leak).
          const peeks = Number(mac(`grep -a -c 'Koan: peek sidebar key' ${LOG} || true`)) - peeksBefore;
          needLog(peeks === 2, `round ${n}: ${peeks} of the 2 pin keys reached the sidebar's peek handler (a dropped key is the driver's, not a lock leak)`);
          needLog(alive(), `Koan died in round ${n} (compact on, pin at +${d1} ms, again +${d2} ms, pointer ${park ? 'at the edge' : 'away'})`);
          if (park) {
            needLog((await sidebarW()) >= 163, `round ${n}: the pointer is at the edge but the sidebar is not out (${await sidebarW()} wide)`);
            mac(ptr(700, 400), 30000);
            await sleep(900);
          }
          needLog((await sidebarW()) === 0, `round ${n}: the sidebar is not hidden after the pin was toggled twice and the pointer left (${await sidebarW()} wide): a lock leaked`);
          mac(`${kp(K.S, COMPACT)}; sleep 1.0`, 30000);
          needLog(alive(), `Koan died turning compact off in round ${n}`);
          needLog((await sidebarW()) >= 163, `round ${n}: compact off, the sidebar is ${await sidebarW()} wide`);
        }
      }
      needLog(Number(mac(`grep -a -c 'Check failed: current_motion_' ${LOG} || true`)) === 0, 'CHECK(current_motion_) in the log');
      return `${n} rounds, alive`;
    });
  }
  // A lock held from before the collapse: the Create-a-Space form opened from the pinned sidebar keeps its lock while compact mode
  // goes off and on again (the form only closes with Esc or Create); the collapse then ends with the lock held.
  await step('T28: Create-a-Space form open from the pinned sidebar, compact off and on twice, then Esc', async () => {
    for (let round = 0; round < 5; round++) {
      const r = `round ${round + 1}`;
      mac(ptr(700, 400), 30000);
      await toggle(`${r}: compact on`);
      mac(`${kp(K.S, PIN)}; sleep 0.8`, 30000);
      // Create a Space is Ctrl+Opt+N on the Mac whatever the keyset (koan_shortcut_table.cc), not Cmd+Opt+N.
      mac(`${kp(K.N, 'ka')}; sleep 0.8`, 30000);
      needLog(alive(), `${r}: Koan died opening the form`);
      // No form, no lock, and the step would pass without the fix.
      needLog((await fviews()).some(v => v.cls === 'KoanSpaceCreationView' && v.shown), `${r}: the Create-a-Space form is not shown (Ctrl+Opt+N did not open it): no lock is held`);
      // Compact off (the strip expands, the form's lock stays), on (the collapse ends with the lock held), off, on.
      await toggle(`${r}: compact off`);
      needLog((await sidebarW()) >= 163, `${r}: compact off, the sidebar is ${await sidebarW()} wide`);
      await toggle(`${r}: compact on (the collapse ends with the form's lock held)`);
      needLog(alive(), `${r}: Koan died when the collapse ended with the form's lock held`);
      needLog((await sidebarW()) >= 163, `${r}: the form's lock is held, so the collapsed sidebar must stay out over the page, but it is ${await sidebarW()} wide`);
      await toggle(`${r}: compact off`);
      await toggle(`${r}: compact on (second collapse with the form open)`);
      needLog(alive(), `${r}: Koan died in the second collapse with the form open`);
      needLog((await sidebarW()) >= 163, `${r}: after the second compact-on the sidebar is ${await sidebarW()} wide with the form open`);
      // Esc closes the form (its lock goes: the sidebar hides), then compact off.
      mac(`${kp(K.ESC, '')}; sleep 0.8`, 30000);
      needLog(alive(), `${r}: Koan died closing the form`);
      await toggle(`${r}: compact off`);
      needLog((await sidebarW()) >= 163, `${r}: compact off at the end, the sidebar is ${await sidebarW()} wide`);
    }
    needLog(Number(mac(`grep -a -c 'Check failed: current_motion_' ${LOG} || true`)) === 0, 'CHECK(current_motion_) in the log');
    return '5 rounds, alive';
  });
  // Fullscreen (#171): the pointer at the left edge while compact goes on: the sidebar slides out at the end of the collapse, with no
  // hot zone (it has no width there) to tell, and goes again when the pointer leaves.
  await step('T29: native fullscreen, the pointer at the left edge as compact goes on: the sidebar comes out; away: it goes', async () => {
    if (!(await key(K.F, 'ck'))) return 'key skipped';
    await settle(2500);
    for (let round = 0; round < 3; round++) {
      mac(ptr(700, 400), 30000);
      // The edge zone is 2 px wide: the pointer goes to x=1 (the compact mode sees it there), and wiggles to x=0 during the collapse.
      mac(`${ptr(1, 400, 50)}; ${kp(K.S, COMPACT)}; sleep 0.15; ${ptr(0, 400, 16)}; sleep 1.5`, 30000);
      needLog(alive(), `Koan died in fullscreen round ${round + 1}`);
      needLog((await sidebarW()) >= 163, `fullscreen round ${round + 1}: the pointer is at the edge but the sidebar is not out (${await sidebarW()} wide)`);
      mac(ptr(700, 400), 30000);
      await sleep(1200);
      needLog((await sidebarW()) === 0, `fullscreen round ${round + 1}: the sidebar stays out with the pointer away (${await sidebarW()} wide)`);
      mac(`${kp(K.S, COMPACT)}; sleep 1.2`, 30000);
    }
    await key(K.F, 'ck');
    await settle(2500);
    return '3 rounds';
  });
}
// T24: the leak detector stays silent
await step('T24: no "Koan: the vertical tab strip" line, no FATAL, no "Spaces:" in the log', async () => { const n = k => Number(mac(`grep -a -c '${k}' ${LOG} || true`)); const v = n('Koan: the vertical tab strip'), f = n(':FATAL:'), sp = n('Spaces:'); need(v === 0 && f === 0 && sp === 0, `vertical-tab lines ${v}, FATAL ${f}, Spaces: ${sp}`); return 'clean'; });
if (MODE === 'old') {
  // read the prefs back after a graceful quit (Preferences are written on exit)
  try { mac(`${M}/tmp/killkoan.sh ${PID}`); await sleep(6000); } catch {}
  const back = mac(`python3 -c "import json;d=json.load(open('${M}/tmp/pass-R-profile/Default/Preferences'));v=d.get('vertical_tabs',{});print(v.get('enabled'),v.get('collapsed_state'),v.get('expand_on_hover'),d.get('koan',{}).get('compact_mode',{}).get('enabled'))" || true`);
  const ok = /^True False True/.test(back) || /^True False/.test(back);
  steps.push({ name: `T1/T2/T3: prefs written back (enabled, collapsed_state, expand_on_hover, compact): ${back}`, ok, alive: false, newIps: [], note: back });
  console.log(`${ok ? 'PASS' : 'FAIL'}  prefs written back: ${back}`);
}
const cleanupReport = cleanupAll(); console.log(`== cleanup: ${cleanupReport.split('\n')[0]}`); if (stopReason) console.log(`== RUN STOPPED: ${stopReason}`);
const stillRunning = false; 
const scan = mac(`${M}/tmp/scanlog.sh ${LOG} ${M}/tmp/regress-start.mark`);
const count = re => Number(mac(`grep -a -c '${re}' ${LOG} || true`));
const counts = { FATAL: count(':FATAL:'), CHECK: count('Check failed'), ERROR: count(':ERROR:'), newIps: newIps().length, failed: steps.filter(s => s.ok === false).length };
const prevF = `${process.env.HOME}/work/koan/.cache/mac-review/regress/last-${BUILD.split('-')[0]}.json`;
const prev = existsSync(prevF) ? JSON.parse(readFileSync(prevF, 'utf8')) : null;
writeFileSync(`${LOCAL}/steps.json`, JSON.stringify({ build: BUILD, app: APP_PATH, log: LOG, counts, steps }, null, 1)); writeFileSync(`${LOCAL}/scan.txt`, scan);
mac(`cp ${LOG} ${RD}/koan.log; cp ${M}/tmp/regress-start.mark ${RD}/ 2>/dev/null; true`);
execSync(`scp -q -r -i ${MAC_KEY} -o ControlPath=/tmp/koan-cm.sock "${MAC_TARGET}:${RD}/*" ${LOCAL}/ 2>/dev/null || true`);
writeFileSync(prevF, JSON.stringify(counts));
if (aborted && !stopReason) console.log('== RUN ABORTED: the user came back; results are incomplete');
console.log(`\n== ${BUILD}: ${steps.filter(s => s.ok).length} passed, ${counts.failed} failed, ${steps.filter(s => s.skipped).length} skipped`);
console.log(`== counts  FATAL ${counts.FATAL}  CHECK ${counts.CHECK}  ERROR ${counts.ERROR}  new .ips ${counts.newIps}` + (prev ? `   (previous: FATAL ${prev.FATAL} CHECK ${prev.CHECK} ERROR ${prev.ERROR} .ips ${prev.newIps})` : '   (no previous run)'));
console.log(`== saved ${LOCAL}  and ${RD} on the Mac\n${scan}`);
