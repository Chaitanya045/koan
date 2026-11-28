// End-to-end test of what Settings > Tab Management > Profiles shows of what
// is kept with each profile (#477's last item): its proxy (#445) and the
// extensions that are off in it (#473). The profile starts with two profiles,
// Work and Home, each used by a Space (prefs/settings_profiles.json); Work
// has a proxy, from --koan-container-proxy (settings_profiles.flags: there is
// no setting for it yet), and the test_helper extension is installed.
// - Work's row: Proxy socks5://127.0.0.1:1080, All extensions on; Home's:
//   Direct, All extensions on;
// - Change… on Work's row opens the Space menu's Extensions… dialog; turning
//   the extension off there and pressing Done shows it (name and icon) in
//   Work's row at once, Home's row unchanged;
// - Change… on Home's row: the extension off there too, then on again;
// - the rows fit the page (no sideways scroll) in the shadcn and Zen looks,
//   light and dark;
// - after a start with --koan-container-proxy=Work=none, Work's row says
//   Direct, and the extension is still off in Work;
// - nothing fatal in the log.
// Also the recorded walk-through of the PR: with PRTEST_STEPS set (the
// pr-test prtest.sh), each step() is a caption and LOOK_MS (default 0) a
// pause after it.
//
//   scripts/e2e/run.sh settings_profiles
import fs from 'node:fs';
import { spawn } from 'node:child_process';
import { openSync } from 'node:fs';
import { CDP_PORT, browserCall, check, connect, evalIn, finish, pages, settingsCall, sleep,
         viewMouse, viewTree, waitFor } from './lib/koan.mjs';
import { fatalInLog } from './lib/mac.mjs';

const LOOK_MS = Number(process.env.LOOK_MS || 0);
function step(text) {
  console.log(`STEP  ${text}`);
  if (process.env.PRTEST_STEPS) {
    fs.appendFileSync(process.env.PRTEST_STEPS, JSON.stringify({ t: Date.now(), text }) + '\n');
  }
}
const look = (ms = LOOK_MS) => sleep(ms);

const WORK = 'user:sworkprofile';
const HOME = 'user:shomeprofile';
const HELPER = 'Koan e2e test helper';

const settingsPage = async () =>
  (await pages()).find(p => p.url.startsWith('chrome://settings/koan/tabs'));

async function openProfiles() {
  await browserCall('Target.createTarget', { url: 'chrome://settings/koan/tabs' });
  const page = await waitFor(settingsPage, 10000);
  await waitFor(() => evalIn(page, `!!koanSettingsRoot?.getElementById('group-profiles')`), 10000);
  // The group on screen, as a user scrolls to it.
  await evalIn(page, `koanSettingsRoot.getElementById('group-profiles').scrollIntoView()`);
  return page;
}

// What `id`'s row shows: {proxy, extensions: [{name, icon}], all, change}.
const row = (page, id) => evalIn(page, `(() => {
  const root = koanSettingsRoot;
  const proxy = root.getElementById('profile-proxy-${id}');
  const ext = root.getElementById('profile-extensions-${id}');
  if (!proxy || !ext) return null;
  const button = ext.querySelector('button.profile-change');
  return {
    proxy: proxy.textContent,
    extensions: [...ext.querySelectorAll('.profile-extension')].map(e => ({
      name: e.textContent,
      icon: e.querySelector('img')?.complete && e.querySelector('img').naturalWidth > 0,
    })),
    all: ext.firstChild?.nodeType === Node.TEXT_NODE ? ext.firstChild.textContent : '',
    change: button ? { text: button.textContent, disabled: button.disabled } : null,
  };
})()`);

// Whether the Profiles group fits the page: nothing wider than its column.
const fits = page => evalIn(page, `(() => {
  const group = koanSettingsRoot.getElementById('group-profiles');
  const width = group.getBoundingClientRect().right + 1;
  return [...group.querySelectorAll('*')].every(e => e.getBoundingClientRect().right <= width) &&
      document.scrollingElement.scrollWidth <= document.scrollingElement.clientWidth;
})()`);

const shown = (vs, cls) => vs.filter(v => v.cls === cls && v.shown);
const click = async view => {
  await viewMouse(view, 'mousePressed', Math.round(view.w / 2), Math.round(view.h / 2));
  await viewMouse(view, 'mouseReleased', Math.round(view.w / 2), Math.round(view.h / 2));
};
const dialogToggle = async () => shown(await viewTree(), 'ToggleButton')
    .find(v => v.path.includes('BubbleDialogModelHostContentsView'));
const doneButton = async () => shown(await viewTree(), 'MdTextButton')
    .find(v => v.props.Text === 'Done' && v.path.includes('DialogClientView'));

// Change… on `id`'s row, then the extension's switch to `on`, then Done.
async function setExtension(page, id, name, on) {
  step(`Change… on ${name}'s row opens the Space menu's Extensions… dialog`);
  await evalIn(page, `koanSettingsRoot.getElementById('profile-extensions-${id}')
      .querySelector('button.profile-change').click()`, true);
  const toggle = await waitFor(dialogToggle, 5000);
  check(`Change… on ${name}'s row opens the Extensions dialog with the extension's switch`,
        !!toggle);
  await look();
  if (!toggle) return;
  const isOn = toggle.props.IsOn ?? toggle.props.On;
  if (String(isOn) !== String(on)) {
    step(`Turn ${HELPER} ${on ? 'on' : 'off'} in ${name}, then Done`);
    await click(toggle);
    await look();
  }
  const done = await waitFor(doneButton, 3000);
  await click(done);
  await waitFor(async () => !await dialogToggle(), 5000);
  await sleep(500);
}

// --- First run: Work has a proxy from the switch.
let settings = await openProfiles();
step('Settings › Tab Management › Profiles: Work has a proxy, Home has none');
let work = await waitFor(async () => {
  const r = await row(settings, WORK);
  return r?.proxy === 'socks5://127.0.0.1:1080' && r;
}, 5000) || await row(settings, WORK);
let home = await row(settings, HOME);
await look(LOOK_MS * 2);
check('Work\'s row shows its proxy', work?.proxy === 'socks5://127.0.0.1:1080',
      JSON.stringify(work));
check('...and All extensions on', work?.all === 'All extensions on' &&
      work?.extensions.length === 0, JSON.stringify(work));
check('Home\'s row shows Direct and All extensions on',
      home?.proxy === 'Direct' && home?.all === 'All extensions on', JSON.stringify(home));
check('Each row has Change… for the extensions, on (a Space uses each profile)',
      work?.change?.text === 'Change…' && !work.change.disabled &&
      home?.change?.text === 'Change…' && !home.change.disabled,
      JSON.stringify([work?.change, home?.change]));

// --- Turn the extension off in Work: the row follows at once.
await setExtension(settings, WORK, 'Work', false);
work = await waitFor(async () => {
  const r = await row(settings, WORK);
  return r?.extensions.length === 1 && r;
}, 5000) || await row(settings, WORK);
step(`Work's row now lists ${HELPER} as off, with its icon`);
await look(LOOK_MS * 2);
check('After Done, Work\'s row lists the extension as off, by name',
      work?.extensions.length === 1 && work.extensions[0].name === HELPER, JSON.stringify(work));
check('...with its icon', work?.extensions[0]?.icon === true, JSON.stringify(work));
check('...and still its proxy', work?.proxy === 'socks5://127.0.0.1:1080', JSON.stringify(work));
home = await row(settings, HOME);
check('Home\'s row is unchanged', home?.proxy === 'Direct' &&
      home?.all === 'All extensions on', JSON.stringify(home));

// --- Home: off, then on again.
await setExtension(settings, HOME, 'Home', false);
home = await waitFor(async () => {
  const r = await row(settings, HOME);
  return r?.extensions.length === 1 && r;
}, 5000);
check('Turned off in Home: Home\'s row lists it', !!home, JSON.stringify(await row(settings, HOME)));
await look();
await setExtension(settings, HOME, 'Home', true);
home = await waitFor(async () => {
  const r = await row(settings, HOME);
  return r?.all === 'All extensions on' && r;
}, 5000);
step('On again in Home: All extensions on');
await look();
check('Turned on again in Home: All extensions on', !!home,
      JSON.stringify(await row(settings, HOME)));

// --- Both looks, light and dark: the rows fit the page.
const set = (name, value) => settingsCall(settings, 'koanSetPref', name, value);
for (const style of ['shadcn', 'zen']) {
  for (const mode of ['light', 'dark']) {
    await set('koan.look.style', style);
    await set('koan.look.mode', mode);
    await sleep(800);
    await evalIn(settings, `koanSettingsRoot.getElementById('group-profiles').scrollIntoView()`);
    step(`The rows in the ${style === 'zen' ? 'Zen' : 'shadcn'} look, ${mode}`);
    await look();
    check(`The Profiles rows fit the page (${style}, ${mode})`, await fits(settings));
    const colors = await evalIn(settings, `(() => {
      const label = koanSettingsRoot.querySelector('.profile-detail-label');
      const value = koanSettingsRoot.getElementById('profile-proxy-${WORK}');
      return [getComputedStyle(label).color, getComputedStyle(value).color];
    })()`);
    check(`...and the label is muted next to the value (${style}, ${mode})`,
          colors && colors[0] !== colors[1], JSON.stringify(colors));
  }
}
await set('koan.look.style', 'shadcn');
await set('koan.look.mode', 'dark');

// --- A start with the proxy removed (the switch's "none").
const args = process.env.KOAN_CHROME_ARGS.split('\n').filter(Boolean)
    .map(a => a.startsWith('--koan-container-proxy=') ? '--koan-container-proxy=Work=none' : a);
let relaunched = null;
process.on('exit', () => relaunched?.kill());
const up = async () => !!await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).catch(() => null);
step('Quit, and start Koan with --koan-container-proxy=Work=none');
{
  const v = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
  const browser = await connect(v.webSocketDebuggerUrl);
  await browser.send('Browser.close');
  browser.close();
  for (let i = 0; i < 60 && await up(); i++) await sleep(500);
  await sleep(2000);
  const log = process.env.KOAN_LOG ? openSync(process.env.KOAN_LOG, 'a') : 'ignore';
  relaunched = spawn(args[0], args.slice(1), { stdio: ['ignore', log, log] });
  for (let i = 0; i < 60 && !await up(); i++) await sleep(500);
  await sleep(5000);
}
settings = await openProfiles();
work = await waitFor(async () => {
  const r = await row(settings, WORK);
  return r?.proxy === 'Direct' && r;
}, 5000) || await row(settings, WORK);
step('Work\'s row: Direct now, the extension still off');
await look(LOOK_MS * 2);
check('After the start, Work\'s row says Direct', work?.proxy === 'Direct', JSON.stringify(work));
check('...and the extension is still off in Work', work?.extensions.length === 1 &&
      work.extensions[0].name === HELPER, JSON.stringify(work));

check('Nothing fatal in the log', !fatalInLog(), fatalInLog());
relaunched?.kill();
finish();
