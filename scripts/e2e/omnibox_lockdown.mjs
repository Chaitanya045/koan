// End-to-end test of the omnibox lockdown (.agent/notes/upstream/tab-groups-
// omnibox-fullscreen.md, section 2; O1-O10, O13, O15): the suggestions popup
// is Chromium's Views one, Cmd+L floats and a click on the pill doesn't,
// nothing opens the bar by itself, the empty bar lists open tabs and top
// sites, and Google's AI defaults are off.
//
//   scripts/e2e/run.sh omnibox_lockdown
//
// The log checks need run.sh's KOAN_LOG. A Mac run: its checks that read
// native views (class names) are from the Linux harness and need validating
// once on the Mac.
import fs from 'node:fs';
import {
  CDP_PORT, CTRL, check, clickView, finish, front, key, pages, readPref,
  shownViews, sleep, views, waitFor, browserCall,
} from './lib/koan.mjs';

const VK = { T: 84, L: 76, ESC: 27 };

// Every DevTools target, also the browser's own WebUI (the popup is one).
const targets = async () =>
    (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json());
const floating = async () => shownViews(await views(), 'KoanUrlBarBackdrop').length > 0;

let [page] = await pages();
await front(page);

// O1: the Views popup, not WebUI (and O15's DCHECK didn't fire at startup).
await key(page, VK.L, CTRL, 'l');
check('Cmd+L opens the floating card (the keyboard floats)', await waitFor(floating),
      'no backdrop');
const rows = shownViews(await views(), 'OmniboxResultView');
console.log(`INFO  ${rows.length} suggestion row(s) after Cmd+L`);
check('The suggestions are Views rows, with no chrome://omnibox-popup WebUI target',
      !(await targets()).some(t => t.url.startsWith('chrome://omnibox-popup')),
      (await targets()).map(t => t.url).filter(u => u.startsWith('chrome://')).join(' '));
await key(page, VK.ESC, 0, 'Escape');
await sleep(500);
check('Esc closes it', !(await floating()));

// O2: a click on the pill edits in the pill.
await clickView('KoanUrlPill');
await sleep(800);
check('A click on the pill edits in the pill: not floating', !(await floating()));
await key(page, VK.ESC, 0, 'Escape');
await sleep(500);

// O8: Cmd+T lists open tabs at once.
await key(page, VK.T, CTRL, 't');
await sleep(1000);
const tabRows = shownViews(await views(), 'OmniboxResultView');
check('Cmd+T with an empty box lists open tabs (and top sites), local only',
      tabRows.length >= 1, `${tabRows.length} row(s)`);
await key(page, VK.ESC, 0, 'Escape');
await sleep(500);

// O3: a new tab page doesn't open the bar by itself.
const before = (await pages()).length;
const target = (await browserCall('Target.createTarget', { url: 'chrome://newtab' }))
    .result.targetId;
await sleep(2000);
check('A new tab page does not open the URL bar by itself', !(await floating()));
await browserCall('Target.closeTarget', { targetId: target });
check('...and leaves no extra tab', (await pages()).length === before);

// O7: Zen's defaults for upstream prefs.
check('Remote search suggestions are off by default (Zen)',
      await readPref('search.suggest_enabled') === false);
check('No AI Mode button in the omnibox by default',
      await readPref('omnibox.show_ai_mode_omnibox_button') === false);
check('AI Mode is disallowed by its policy pref default',
      await readPref('omnibox.ai_mode_settings') === 1);

// The log: none of what the lockdown removes. (Not the "Attempting to show IPH
// ... before browser initialization complete" lines: they are logged for any
// promo asked for at startup, whatever its feature's state.)
const log = process.env.KOAN_LOG ? fs.readFileSync(process.env.KOAN_LOG, 'utf8') : '';
for (const [what, pattern] of [
    ['Chromium\'s WebUI omnibox popup being on (O15)', /WebUI omnibox popup is on/],
    ['the on-device model service (O5)', /on_device_model/]]) {
  const lines = log.split('\n').filter(l => pattern.test(l));
  check(`The browser's log has nothing about ${what}`,
        process.env.KOAN_LOG && lines.length === 0, lines.slice(0, 2).join(' | '));
}
finish();
