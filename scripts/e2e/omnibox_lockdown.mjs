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
  CDP_PORT, CTRL, check, clickView, finish, front, key, pages, readPref, shownViews, sleep,
  views, viewTree, typeInto, visiblePage, waitFor, browserCall, evalIn, skipOnMac,
} from './lib/koan.mjs';
skipOnMac();

const VK = { T: 84, L: 76, ESC: 27 };
const MOD_SHIFT = 8;

// Every DevTools target, also the browser's own WebUI (the popup is one).
const targets = async () =>
    (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json());
const floating = async () => shownViews(await views(), 'KoanUrlBarBackdrop').length > 0;

const aiText = v => `${v.props?.Text ?? ''} ${v.props?.TooltipText ?? ''} ` +
                    `${v.props?.AccessibleName ?? ''} ${v.props?.PlaceholderText ?? ''}`;
// What must not be on any surface: AI Mode (chip, button, hint, row), the
// "Ask Google about this page" row (Lens / contextual search entry point) and
// the "+" button that adds context to AI Mode.
const AI_PATTERN = /AI Mode|Ask AI|Ask Google about this page|Gemini|Add (context|file|image)/i;
const aiOnScreen = async () => (await views()).filter(
    v => v.shown && (/AiMode|AIM|Gemini|AddContext|Composebox/i.test(v.cls) ||
                     AI_PATTERN.test(aiText(v))));

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
const aiAfterClick = await aiOnScreen();
check('A click on the sidebar URL bar shows no AI Mode, "Ask Google about this page" or "+" either',
      aiAfterClick.length === 0,
      aiAfterClick.map(v => `${v.cls}:${aiText(v).trim()}`).join(' | '));
// O2: Cmd+L while the bar is open in the pill floats it.
await key(page, VK.L, CTRL, 'l');
check('Cmd+L with the bar open in the pill floats it', await waitFor(floating),
      'no backdrop');
await key(page, VK.ESC, 0, 'Escape');
await sleep(500);

// O8, O9: Cmd+T lists the other open tabs at once (the active tab is not one
// to switch to, and a fresh profile has no top sites), each with Switch to
// Tab, and Enter on one switches to it instead of opening a duplicate.
const second = (await browserCall('Target.createTarget',
    { url: 'https://example.com/?omnibox-two' })).result.targetId;
await waitFor(async () => (await pages()).some(p => p.id === second && p.title));
await browserCall('Target.activateTarget', { targetId: page.id });
await sleep(500);
const tabCount = (await pages()).length;
const secondTitle = (await pages()).find(p => p.id === second)?.title ?? '';
await key(page, VK.T, CTRL, 't');
const tabRows = await waitFor(async () => {
  const rs = shownViews(await views(), 'OmniboxResultView');
  return rs.length >= 1 && rs;
});
const texts = (await views()).filter(v => v.cls === 'OmniboxTextView' && v.shown)
    .map(v => v.props?.Text ?? '');
console.log(`INFO  Cmd+T rows: ${texts.join(' | ')}`);
check('Cmd+T with an empty box lists the other open tab, local only',
      !!tabRows && texts.some(t => secondTitle && t.includes(secondTitle)),
      `title ${JSON.stringify(secondTitle)}; ${texts.join(' | ')}`);
const buttons = (await views()).filter(v => v.shown && /Button/.test(v.cls) &&
    v.path.some(c => c === 'OmniboxResultView'));
console.log(`INFO  buttons in the rows: ${buttons.map(v => v.cls).join(' ')}`);
check('...with a Switch to Tab button', buttons.length >= 1,
      buttons.map(v => v.cls).join(' '));
// Typing a character and deleting it again: the list is back (Zen shows the
// default list for an empty box, however it got empty).
const emptied = (await viewTree()).find(v => v.cls === 'OmniboxViewViews' && v.shown);
await typeInto(emptied, 'x', /*Backspace*/ 8);
const againRows = await waitFor(async () => {
  const rs = shownViews(await views(), 'OmniboxResultView');
  return rs.length >= 1 && rs;
});
check('Typing a character and deleting it lists the open tabs again',
      !!againRows, `${shownViews(await views(), 'OmniboxResultView').length} row(s)`);
// Enter on the first row (the second tab's): it switches, no duplicate.
await key(page, 40, 0, 'ArrowDown');
await key(page, 13, 0, 'Enter');
await sleep(1000);
check('Enter on the open tab\'s row switches to it: no new tab, that tab is active',
      (await pages()).length === tabCount && (await visiblePage())?.id === second,
      `${(await pages()).length} tab(s), was ${tabCount}`);
// O8 on about:blank (page classification BLANK, not OTHER): the list is there
// too, with the example.com tab and the first one.
const blank = (await browserCall('Target.createTarget', { url: 'about:blank' }))
    .result.targetId;
await sleep(1000);
await key((await pages()).find(p => p.id === blank) ?? page, VK.T, CTRL, 't');
const blankRows = await waitFor(async () => {
  const rs = shownViews(await views(), 'OmniboxResultView');
  return rs.length >= 1 && rs;
});
check('Cmd+T on an about:blank tab lists the open tabs too', !!blankRows,
      `${shownViews(await views(), 'OmniboxResultView').length} row(s)`);
await key(page, VK.ESC, 0, 'Escape');
await sleep(500);
await browserCall('Target.closeTarget', { targetId: blank });
await browserCall('Target.closeTarget', { targetId: second });
await sleep(500);
[page] = await pages();
await front(page);

// O6: no @gemini / @aimode shortcuts (the filter runs after the search engines
// load, off the notification: inside it, the browser died at startup).
await key(page, VK.L, CTRL, 'l');
await waitFor(floating);
const box = (await viewTree()).find(v => v.cls === 'OmniboxViewViews' && v.shown);
await typeInto(box, '@');
const atRows = (await views()).filter(v => v.cls === 'OmniboxTextView' && v.shown)
    .map(v => v.props?.Text ?? '');
console.log(`INFO  '@' lists: ${atRows.join(' | ')}`);
check('Typing @ lists no Gemini or AI Mode shortcut',
      atRows.length > 0 && !atRows.some(t => /gemini|aimode|ai mode/i.test(t)),
      atRows.join(' | '));
await key(page, VK.ESC, 0, 'Escape');
await sleep(500);

// O5, issue #178: no AI Mode on any surface the user sees. (1) The floating
// bar / New Tab overlay: no chip, button, hint or tooltip for it.
await key(page, VK.T, CTRL, 't');
await waitFor(floating);
await sleep(500);
const aiViews = await aiOnScreen();
check('The New Tab overlay has no AI Mode chip, button, hint or tooltip, and no "Ask Google about this page" row or "+" button',
      aiViews.length === 0,
      aiViews.map(v => `${v.cls}:${aiText(v).trim()}`).join(' | '));
await key(page, VK.ESC, 0, 'Escape');
await sleep(500);

// (2) Search Tabs (Shift+Cmd+A on macOS, Shift+Ctrl+A elsewhere): the tab
// search bubble is a WebUI page; none of its text mentions AI Mode.
await key(page, 65, MOD_SHIFT | CTRL, 'a');
const tabSearch = await waitFor(async () => (await pages()).find(
    p => p.url.startsWith('chrome://tab-search')));
if (tabSearch) {
  await sleep(800);
  const text = await evalIn(tabSearch, `(() => {
      const out = [];
      const walk = n => {
        if (n.nodeType === Node.TEXT_NODE) out.push(n.textContent);
        if (n.shadowRoot) walk(n.shadowRoot);
        for (const c of n.childNodes ?? []) walk(c);
        if (n.getAttribute) for (const a of ['aria-label', 'title', 'placeholder']) {
          const v = n.getAttribute(a); if (v) out.push(v);
        }
      };
      walk(document.documentElement);
      return out.join(' ');
    })()`);
  check('Search Tabs (Shift+Cmd+A) shows no AI Mode', !/AI Mode|Ask AI|Gemini/i.test(text ?? ''),
        (text ?? '').match(/.{0,30}(AI Mode|Ask AI|Gemini).{0,30}/i)?.[0] ?? '');
  await key(page, VK.ESC, 0, 'Escape');
  await sleep(500);
} else {
  console.log('INFO  no chrome://tab-search target found (the bubble is a popup; check by hand)');
}

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
