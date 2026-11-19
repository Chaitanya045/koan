// Helpers for the e2e scripts that drive agent control (#180) with the `koan`
// CLI, the way an agent does (agent_indicator; agent_cli has its own copy of
// koan() and allow(), and can move to these). macOS only: the CLI's holder
// needs the Mac's peer resolver. KOAN_CLI, KOAN_PROFILE and KOAN_AGENT_HOME
// are set by scripts/mac/suite.sh.
import { spawn } from 'node:child_process';
import { sleep, viewTree, waitFor } from './koan.mjs';
import { rootMouse, rootOf } from './mac.mjs';

export const CLI = process.env.KOAN_CLI;
export const PROFILE = process.env.KOAN_PROFILE;
export const AGENT_HOME = process.env.KOAN_AGENT_HOME;
const env = { ...process.env, HOME: AGENT_HOME, KOAN_USER_DATA_DIR: PROFILE };

// Runs `koan args...`; resolves {code, out, err}. `whileRunning` runs once
// the command started (e.g. to answer a dialog it waits for), with a
// function that returns what the command printed so far; koan() resolves
// once both are done. `extraEnv` adds to its environment (PATH for a stand-in
// agent-browser).
export function koan(args, { timeout = 60000, whileRunning, extraEnv } = {}) {
  return new Promise(resolve => {
    const child = spawn(CLI, args, { env: { ...env, ...extraEnv }, stdio: ['ignore', 'pipe', 'pipe'] });
    let out = '', err = '';
    child.stdout.on('data', d => (out += d));
    child.stderr.on('data', d => (err += d));
    const timer = setTimeout(() => child.kill('SIGTERM'), timeout);
    const running = whileRunning?.(() => out + err)
        .catch(e => console.log(`  (${e.message})`));
    child.on('close', async code => {
      clearTimeout(timer);
      await running;
      resolve({ code, out: out.trim(), err: err.trim() });
    });
  });
}
// Starts `koan args...` that runs until it is killed (`koan watch`).
export const spawnKoan = args => spawn(CLI, args, { env, stdio: ['ignore', 'pipe', 'pipe'] });
export const brief = r => `exit ${r.code}; ${(r.err || r.out).split('\n')[0].slice(0, 160)}`;

// Presses the Allow button of the dialog whose title matches `title`, once
// the command (`printed`) says it waits for the user in Koan; not before (on
// macOS a widget made while a UI DevTools session is between its DOM.enable
// and DOM.getDocument crashes the browser, and each poll opens a session).
// Returns whether it pressed it; `allowWhy()` says why not.
let why = '';
export const allowWhy = () => why;
export async function allow(title, printed) {
  why = 'koan never said it waits in Koan';
  if (!await waitFor(() => / in Koan\.\.\./.test(printed()), 30000)) return false;
  why = 'no dialog with that title and an Allow button within 20s';
  await sleep(1000);
  const found = await waitFor(async () => {
    const vs = await viewTree();
    const label = vs.find(v => v.shown && title.test(v.props.Text ?? ''));
    if (!label) return null;
    const button = vs.find(v => v.widget === label.widget && v.shown && v.cls === 'MdTextButton' &&
                                v.props.Text === 'Allow');
    return button && { vs, button };
  }, 20000);
  if (!found) return false;
  why = '';
  // Dialogs ignore a click right after they show (InputEventActivationProtector).
  await sleep(1500);
  const vs = await viewTree();
  const button = vs.find(v => v.nodeId === found.button.nodeId) ?? found.button;
  const root = rootOf({ views: vs.filter(v => v.widget === button.widget) });
  await rootMouse(root, ['mousePressed', 'mouseReleased'], button.ax + button.w / 2,
                  button.ay + button.h / 2);
  return true;
}
