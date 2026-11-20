// User flow: an agent finds Koan and pairs (#397). Test cases UF-ONB-* in
// .agent/notes/user-flows.md. The person at the Mac approves the pairing
// with a real click, an agent reads the guide, the person sets up agents in
// Settings with real clicks (under KOAN_AGENT_SETUP_HOME, never the real
// ~/.claude), the agent finds `koan` through the link, and Remove takes it
// all out. Run by run_flows.sh (Agent ready on through its Local State).
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { AGENT_HOME, CLI, PROFILE, ROOT, begin, brief, browserCall, clickElement, end, evalIn, info, koan,
         openAnchor, pageById, pair, parkPointer, skipFlow, sleep, step, waitFor } from './uf_lib.mjs';
import { showPane } from '../../../e2e/lib/koan.mjs';

const fixtures = await begin('uf_onboarding', 'An agent finds Koan, pairs, and is set up from Settings', '#397');
const SETUP = process.env.KOAN_AGENT_SETUP_HOME;
if (!CLI || !fs.existsSync(CLI)) skipFlow(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);
if (!SETUP || !AGENT_HOME) skipFlow('KOAN_AGENT_SETUP_HOME and KOAN_AGENT_HOME come from run_flows.sh');
const anchor = await openAnchor(fixtures.url('/p?t=Home'));
await parkPointer();

await step('UF-ONB-1', 'The person approves `koan pair` with a real click; everything is asked for', async t => {
  const { r, approval } = await pair(t, 'UF agent');
  t.check('the approval dialog showed and a REAL click on Allow answered it', approval?.real && approval.approved,
          approval?.why ?? JSON.stringify(approval));
  t.check('koan pair: paired', r.code === 0 && /paired as/.test(r.out), brief(r));
  t.check('a plain pair asks for, and gets, every capability',
          /allowed: space, observe, read, drive, keep-awake$/m.test(r.out), r.out.split('\n').at(-1));
  const seen = approval?.seen;
  t.check('the dialog shows a check row per capability (list tabs, look at the tab, control tabs, keep awake)',
          (seen?.toggles ?? 0) >= 4, JSON.stringify(seen));
  await t.shot('paired');
}, { needs: ['real input'] });

await step('UF-ONB-2', 'The agent asks `koan status`: it tells the truth', async t => {
  const r = await koan(['status']);
  t.check('Koan running, Agent ready', /running, Agent ready/.test(r.out), brief(r));
  t.check('paired: yes (Koan knows the pairing)', /^paired: yes$/m.test(r.out), r.out);
});

await step('UF-ONB-3', 'A stale pairing (Koan forgot it) is cleaned up, never "several paired clients"', async t => {
  const real = fs.realpathSync(PROFILE);
  // Pinned to this agent as the good one is (koan pair pins it): koan
  // prefers this agent's own pairings (ChooseClient(), #397), so an
  // unpinned record would never be tried, nor removed (as in agent_cli).
  // And for the profile as the good one names it (`koan` compares the
  // path as Koan gave it: the bench's ~/Work, not realpath's ~/work).
  const clients = path.join(AGENT_HOME, '.koan', 'clients');
  let pinned, profileDir = real;
  for (const id of fs.existsSync(clients) ? fs.readdirSync(clients) : []) {
    try {
      const c = JSON.parse(fs.readFileSync(path.join(clients, id, 'client.json'), 'utf8'));
      if (c.pinned) [pinned, profileDir] = [c.pinned, c.userDataDir ?? real];
    } catch {}
  }
  t.check('the good pairing is pinned to this agent', !!pinned);
  const stale = home => {
    const id = crypto.randomBytes(16).toString('hex');
    const dir = path.join(home, '.koan', 'clients', id);
    fs.mkdirSync(dir, { recursive: true, mode: 0o700 });
    fs.writeFileSync(path.join(dir, 'key'), crypto.randomBytes(32).toString('base64'), { mode: 0o600 });
    fs.writeFileSync(path.join(dir, 'client.json'), JSON.stringify({ clientId: id, label: 'stale',
                                                                     userDataDir: profileDir, pairedAt: Date.now() + 3600e3,
                                                                     ...(pinned ? { pinned } : {}) }),
                     { mode: 0o600 });
    return dir;
  };
  let dir = stale(AGENT_HOME);
  let r = await koan(['status']);
  t.check('koan status removes the newer stale record and still says paired: yes',
          /^paired: yes$/m.test(r.out) && !fs.existsSync(dir), brief(r));
  dir = stale(AGENT_HOME);
  r = await koan(['tabs']);
  t.check('a command removes it too and runs as the good pairing (no --client)',
          r.code === 0 && !fs.existsSync(dir) && !/several paired clients/.test(r.err), brief(r));
  const lone = fs.mkdtempSync(path.join(AGENT_HOME, 'lone-'));
  dir = stale(lone);
  r = await koan(['status'], { home: lone });
  t.check('a HOME with only a forgotten pairing: "paired: no (Koan forgot this pairing; run koan pair)"',
          /^paired: no \(Koan forgot this pairing; run koan pair\)$/m.test(r.out), brief(r));
});

await step('UF-ONB-4', 'The agent reads the guide (`koan skill`) and it matches this Koan', async t => {
  const r = await koan(['skill']);
  t.check('koan skill prints the guide', r.code === 0 && r.out.length > 2000, brief(r));
  t.check('...which covers the snapshot loop, batches, choose, take-over, wait --resumed and screenshots',
          ['--snapshot', '--interactive', 'choose', 'take-over', 'wait', '--resumed', 'micro']
              .every(s => r.out.includes(s)),
          ['--snapshot', '--interactive', 'choose', 'take-over', 'wait', '--resumed', 'micro']
              .filter(s => !r.out.includes(s)).join(', ') || 'all there');
  t.check('...and ends with its end line', /\[koan:skill\] end of guide v\d+\s*$/.test(r.out), r.out.slice(-120));
  t.check('no version notice (the app\'s koan matches the running Koan)', !/\[koan:notice\]/.test(r.out + r.err),
          (r.out + r.err).split('\n').find(l => /koan:notice/.test(l)) ?? '');
  const repo = fs.readFileSync(path.join(ROOT, 'skills/koan-browser/SKILL.md'), 'utf8');
  t.check('the repo\'s entry skill tells an agent to run `koan skill`', /koan skill/.test(repo));
  info(`koan skill: ${r.out.length} characters (~${Math.round(r.out.length / 4)} tokens)`);
});

// Settings > Agent Control, in our window; its buttons clicked for real.
let settingsId = null;
const root = sel => `koanSettingsRoot?.querySelector(${JSON.stringify(sel)})`;
const skillOf = dir => path.join(SETUP, dir, 'koan-browser', 'SKILL.md');
const link = path.join(SETUP, '.local/bin/koan');
let settings = null;

await step('UF-ONB-5', 'The person clicks Settings > Agent Control > Set Up Agents', async t => {
  for (const dir of ['.claude', '.codex']) fs.mkdirSync(path.join(SETUP, dir), { recursive: true });
  settingsId = (await browserCall('Target.createTarget', { url: 'chrome://settings/koan/agents' })).result?.targetId;
  settings = await waitFor(() => pageById(settingsId), 8000);
  await evalIn(settings, showPane('agents'));
  const label = await waitFor(() => evalIn(settings, `${root('#agent-setup-button')}?.textContent ?? ''`), 15000);
  t.check('the Set Up Agents button is on the Agent Control page', /^Set Up Agents$/.test(label || ''), `${label}`);
  t.check('nothing was written before the click', !fs.existsSync(skillOf('.claude/skills')) && !fs.existsSync(link));
  await t.shot('before');
  const c = await clickElement(settings, root('#agent-setup-button'), 300);
  t.check('a real click on it', c.ok, c.why);
  const said = await waitFor(() => evalIn(settings, `${root('#agent-setup-status')}?.textContent ?? ''`)
      .then(s => /Set up\.|Could|left/.test(s) && s), 15000);
  t.check('the page says "Set up."', said === 'Set up.', `${said}`);
  await sleep(500);
  await t.shot('set-up');
  const repo = fs.readFileSync(path.join(ROOT, 'skills/koan-browser/SKILL.md'), 'utf8');
  t.check('Claude Code got the repo\'s koan-browser skill (~/.claude/skills)',
          fs.existsSync(skillOf('.claude/skills')) && fs.readFileSync(skillOf('.claude/skills'), 'utf8') === repo);
  t.check('Codex got it in ~/.agents/skills', fs.existsSync(skillOf('.agents/skills')));
  t.check('nothing for Cursor, which is not installed', !fs.existsSync(path.join(SETUP, '.cursor')));
  let to = '';
  try { to = fs.readlinkSync(link); } catch {}
  t.check('~/.local/bin/koan links to the koan in this app',
          !!to && fs.realpathSync(link) === fs.realpathSync(CLI), to);
  const text = await evalIn(settings, `koanSettingsRoot?.getElementById('pane')?.textContent ?? ''`);
  t.check('the page lists what it set up (Claude Code, Codex)', /Claude Code/.test(text) && /Codex/.test(text),
          String(text).slice(0, 300));
}, { needs: ['real input'] });

await step('UF-ONB-6', 'A new agent finds Koan through the link and its skill', async t => {
  const r = await koan(['status']);
  const viaLink = fs.existsSync(link) && await new Promise(res => {
    const c = spawn(link, ['status'], { env: { ...process.env, HOME: AGENT_HOME, KOAN_USER_DATA_DIR: PROFILE } });
    let o = '';
    c.stdout.on('data', d => (o += d));
    c.on('close', code => res({ code, out: o }));
    c.on('error', e => res({ code: -1, out: String(e) }));
  });
  t.check('~/.local/bin/koan status runs and says paired: yes', viaLink && viaLink.code === 0 &&
          /^paired: yes$/m.test(viaLink.out), JSON.stringify(viaLink).slice(0, 200));
  t.check('the same answer as the app\'s koan', /^paired: yes$/m.test(r.out), brief(r));
  const skill = fs.existsSync(skillOf('.claude/skills')) ? fs.readFileSync(skillOf('.claude/skills'), 'utf8') : '';
  t.check('the installed skill says to run `koan skill` and names the CLI\'s full paths',
          /run `koan skill`/.test(skill) && /Koan\.app\/Contents\/Resources\/bin\/koan/.test(skill));
});

await step('UF-ONB-7', 'The person clicks Remove: Koan\'s skills and link go, nothing else', async t => {
  const keep = path.join(SETUP, '.claude', 'skills', 'mine', 'SKILL.md');
  fs.mkdirSync(path.dirname(keep), { recursive: true });
  fs.writeFileSync(keep, '# my own skill\n');
  const c = await clickElement(settings, root('#agent-setup-remove'), 300);
  t.check('a real click on Remove', c.ok, c.why);
  const gone = await waitFor(() => !fs.existsSync(skillOf('.claude/skills')) && !fs.existsSync(link), 10000);
  t.check('the skills and the link are gone', !!gone &&
          !fs.existsSync(skillOf('.agents/skills')));
  t.check('the person\'s own skill is left alone', fs.existsSync(keep));
  await sleep(400);
  await t.shot('removed');
}, { needs: ['real input'] });

await step('UF-ONB-8', 'The agent ends its session', async t => {
  if (settingsId) await browserCall('Target.closeTarget', { targetId: settingsId }).catch(() => {});
  const r = await koan(['end']);
  t.check('koan end', r.code === 0, brief(r));
  await browserCall('Target.activateTarget', { targetId: anchor.id });
});

end();
