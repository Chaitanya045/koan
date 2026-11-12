// End-to-end test of the new tab page against a real build: chrome://newtab
// shows Koan's page, an empty page in the look's background (shadcn's
// near-black in the default look, dark shadcn; the theme's new tab page colour
// in the Zen look) that loads nothing else and runs no script (like Zen's),
// follows the window's look, and is right after another new tab page closed;
// Koan's settings (chrome://settings/koan/looks) let the user choose Chromium's own page instead. (Ctrl+T opening the floating URL bar
// rather than a tab is urlbar's; newtab_custom checks that a new tab page set
// by policy wins.)
//
//   scripts/e2e/run.sh newtab
//
// With SHOTS=<dir>, also saves a screenshot of the page.
import {
  browserCall, check, evalIn, finish, front, onPage, pages, readPref,
  setKoanPref, sleep, waitFor,
} from './lib/koan.mjs';
import fs from 'node:fs';

// Opens chrome://newtab in a background tab; returns its page and the URL it
// committed (chrome://newtab is rewritten to the page that shows), and closes
// it with `close()`.
async function openNewTab() {
  const targetId = (await browserCall('Target.createTarget',
      { url: 'chrome://newtab', background: true })).result.targetId;
  let page, href;
  await waitFor(async () => {
    page = (await pages()).find(p => p.id === targetId);
    href = page && await evalIn(page, 'location.href');
    return href && href !== 'about:blank' && href !== 'chrome://newtab/';
  });
  return { page, href, close: () => browserCall('Target.closeTarget', { targetId }) };
}

const koan = 'chrome://koan-newtab/';
let tab = await openNewTab();
check('chrome://newtab shows Koan\'s page', tab.href === koan, tab.href);
{
  const t = await evalIn(tab.page,
                         '({ title: document.title, lang: document.documentElement.lang })');
  // IDS_NEW_TAB_TITLE in the browser's locale: on macOS the system's
  // (en-GB says "New tab").
  check('...titled New Tab', /^New tab$/i.test(t?.title ?? ''), JSON.stringify(t));
}

const facts = await evalIn(tab.page, `({
    children: document.body.children.length,
    text: document.body.innerText,
    scripts: document.scripts.length,
    images: document.images.length,
    links: document.querySelectorAll('link, iframe, object, embed').length,
    resources: performance.getEntriesByType('resource').length,
    background: getComputedStyle(document.documentElement).backgroundColor,
    scheme: getComputedStyle(document.documentElement).colorScheme,
  })`);
check('The page is empty: no content, script or extra requests',
      facts && facts.children === 0 && facts.text === '' && facts.scripts === 0 &&
      facts.images === 0 && facts.links === 0 && facts.resources === 0,
      JSON.stringify(facts));
// The window provider's new tab page colour (opaque; in the Zen look Partial
// against Zen's window gradient, ZEN_PARITY): chrome://theme/colors.css has it
// as --color-new-tab-page-background. As a computed style: 'rgb(r, g, b)', or
// the start of 'rgba(r, g, b, a)'; null if the page doesn't say.
const providerColour = async (name = 'new-tab-page-background') => {
  const id = (await browserCall('Target.createTarget', {
      url: 'chrome://theme/colors.css?sets=ui,chrome', background: true })).result.targetId;
  await sleep(800);
  const page = (await pages()).find(p => p.id === id);
  const text = page && await evalIn(page, 'document.body?.innerText');
  await browserCall('Target.closeTarget', { targetId: id });
  // #rrggbb, or #rrggbbaa (opaque in practice; an alpha below ff reads as rgba).
  const hex = text?.match(new RegExp(`--color-${name}:\\s*#([0-9a-fA-F]{6})([0-9a-fA-F]{2})?`));
  if (!hex) return null;
  const [r, g, b] = [0, 2, 4].map(i => parseInt(hex[1].slice(i, i + 2), 16));
  return !hex[2] || parseInt(hex[2], 16) === 255 ? `rgb(${r}, ${g}, ${b})`
                                                 : `rgba(${r}, ${g}, ${b},`;
};
const providerBackground = () => providerColour();
const isBackground = (colour, wanted) => !!colour &&
    (wanted ? colour === wanted || (wanted.startsWith('rgba') && colour.startsWith(wanted))
            : /^rgb\(\d+, \d+, \d+\)$/.test(colour));
// The default look is shadcn's, dark: its card (#18181b) on the window's
// near-black sidebar (#09090b), which is the window provider's new tab page
// colour too. (The target opens in the window the user is in, as the new tab
// pages above do, so the colour is that window's.) Zen's empty tab is its
// content card, lighter than the window (#349: it was the window's #09090b,
// a black void).
const SHADCN_DARK = 'rgb(24, 24, 27)';
const wanted = await providerBackground();
check('...in the default look\'s colour, shadcn dark\'s card (#18181b), the window provider\'s',
      facts?.background === SHADCN_DARK && isBackground(facts?.background, wanted),
      `${facts?.background} vs ${wanted}`);
const frame = await providerColour('frame-active');
check('...which is not the window\'s own colour (the page stands out as a card)',
      !!frame && !isBackground(facts?.background, frame), `${facts?.background} vs ${frame}`);
if (process.env.SHOTS) {
  const r = await onPage(tab.page, c => c.send('Page.captureScreenshot', { format: 'png' }));
  fs.writeFileSync(`${process.env.SHOTS}/newtab.png`, Buffer.from(r.result.data, 'base64'));
}
await tab.close();

// The colour comes from the tab that asks: a second new tab page, closed
// again, leaves the first one right when it reloads (a page serving a colour
// remembered from the last request would be empty and white).
tab = await openNewTab();
const other = await openNewTab();
const colour = async t => evalIn(t.page,
    'getComputedStyle(document.documentElement).backgroundColor');
const before = await colour(tab);
await other.close();
await evalIn(tab.page, 'location.reload()');
await sleep(1500);
check('A new tab page reloaded after another one closed keeps its colour',
      await colour(tab) === before && before !== 'rgba(0, 0, 0, 0)', await colour(tab));

// It follows the window's look, live, and the open page loads again with the
// change: the Zen look is the theme's new tab page colour (the window
// provider's, not shadcn's), shadcn light bright, shadcn dark #18181b.
await setKoanPref('koan.look.mode', 'dark');
await setKoanPref('koan.look.style', 'zen');
const zen = await providerBackground();
check('...follows a change to the Zen look (the window provider\'s colour) ' +
          'without a reload by hand',
      await waitFor(async () => {
        const now = await colour(tab);
        return now !== SHADCN_DARK && isBackground(now, zen);
      }), `${await colour(tab)} vs ${zen}`);
await setKoanPref('koan.look.style', 'shadcn');
await setKoanPref('koan.look.mode', 'light');
check('...and to shadcn light (bright)',
      await waitFor(async () => {
        const m = /rgb\((\d+), (\d+), (\d+)\)/.exec(await colour(tab) ?? '');
        return m && +m[1] > 240 && +m[2] > 240 && +m[3] > 240;
      }), await colour(tab));
// And a third change, after two: the follow works every time, not once (each
// reload makes a new page and controller, which must keep what the last one
// knew).
await setKoanPref('koan.look.mode', 'dark');
check('...and back to shadcn dark, the third change in a row',
      await waitFor(async () => await colour(tab) === before) && before === SHADCN_DARK,
      await colour(tab));
await tab.close();

// Chromium's own page, chosen in Koan's settings.
const [first] = await pages();
await front(first);
const settings = (await browserCall('Target.createTarget',
    { url: 'chrome://settings/koan/looks', background: true })).result.targetId;
const settingsPage = await waitFor(async () => (await pages()).find(p => p.id === settings));
check('Koan\'s panes draw in chrome://settings (Look and Feel)',
      await waitFor(() => evalIn(settingsPage,
          `window.koanSettingsRoot?.getElementById('pane')?.childElementCount > 0`)));
const row = `koanSettingsRoot.querySelector('[data-pref="koan.newtab.page"]')`;
check('Koan\'s settings has a "New tab page" choice, Blank page by default',
      await evalIn(settingsPage, `${row}?.querySelector('select')?.selectedOptions[0]?.text`) ===
      'Blank page');
check('...with Chromium\'s new tab page as the other',
      JSON.stringify(await evalIn(settingsPage,
          `[...${row}.querySelectorAll('option')].map(o => o.text)`)) ===
      JSON.stringify(['Blank page', 'Chromium\'s new tab page']));
await evalIn(settingsPage, `(() => { const s = ${row}.querySelector('select');
    s.selectedIndex = 1; s.dispatchEvent(new Event('change')); })()`);
check('Choosing Chromium\'s page sets the pref',
      await waitFor(async () => await readPref('koan.newtab.page') === 'chromium'));
await browserCall('Target.closeTarget', { targetId: settings });

tab = await openNewTab();
check('...and chrome://newtab then shows it',
      tab.href?.startsWith('chrome://new-tab-page'), tab.href);
await tab.close();

await setKoanPref('koan.newtab.page', 'koan');
tab = await openNewTab();
check('Choosing Blank page again brings Koan\'s back', tab.href === koan, tab.href);
await tab.close();

// What the browser logged while new tabs opened and closed: nothing about a
// refused chrome://newtab/, in-product help for Spaces, or the on-device
// model service. (A promo asked for before the browser finished starting
// logs "Attempting to show IPH ...; IPH will not be shown" before upstream
// looks at whether the feature is on: Koan turns those off, so that line is
// not one; see koan_feature_overrides.cc, O13.)
const log = process.env.KOAN_LOG ? fs.readFileSync(process.env.KOAN_LOG, 'utf8') : '';
for (const [what, pattern] of [
    ['a refused chrome://newtab/ load', /incorrect profile type/],
    ['in-product help for tab groups', /IPH_TabGroups/],
    ['the on-device model service', /on_device_model/]]) {
  const lines = log.split('\n').filter(l => pattern.test(l) && !/IPH will not be shown/.test(l));
  check(`The browser's log has nothing about ${what}`,
        process.env.KOAN_LOG && lines.length === 0, lines.slice(0, 2).join(' | '));
}

finish();
