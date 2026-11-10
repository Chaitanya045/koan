// End-to-end test of the new tab page against a real build: chrome://newtab
// shows Koan's page, an empty page in the look's background (shadcn's
// near-black in the dark shadcn look; the theme's new tab page colour in the
// default Zen look) that loads nothing else and runs no script (like Zen's),
// follows the window's look, and is right after another new tab page closed;
// chrome://koan-settings lets the user choose Chromium's own page instead. (Ctrl+T opening the floating URL bar
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
check('...titled New Tab', await evalIn(tab.page, 'document.title') === 'New Tab');

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
// The default look is Zen: Chromium's new tab page colour of the window's
// provider (opaque; Partial against Zen's window gradient, ZEN_PARITY).
// chrome://theme/colors.css has it as --color-new-tab-page-background.
const css = await (async () => {
  const id = (await browserCall('Target.createTarget', {
      url: 'chrome://theme/colors.css?sets=ui,chrome', background: true })).result.targetId;
  await sleep(800);
  const page = (await pages()).find(p => p.id === id);
  const text = page && await evalIn(page, 'document.body?.innerText');
  await browserCall('Target.closeTarget', { targetId: id });
  return text;
})();
// #rrggbb, or #rrggbbaa (opaque in practice; an alpha below ff reads as rgba).
const hex = css?.match(/--color-new-tab-page-background:\s*#([0-9a-fA-F]{6})([0-9a-fA-F]{2})?/);
const [r, g, b] = hex ? [0, 2, 4].map(i => parseInt(hex[1].slice(i, i + 2), 16)) : [];
const alpha = hex?.[2] ? parseInt(hex[2], 16) : 255;
const wanted = hex && (alpha === 255 ? `rgb(${r}, ${g}, ${b})` : `rgba(${r}, ${g}, ${b},`);
// (The target opens in the window the user is in, as the new tab pages above
// do, so the colour is that window's.)
check('...in the Zen look\'s new tab page colour (the window provider\'s)',
      wanted ? (alpha === 255 ? facts?.background === wanted
                              : facts?.background?.startsWith(wanted))
             : /^rgb\(\d+, \d+, \d+\)$/.test(facts?.background ?? ''),
      `${facts?.background} vs ${wanted ?? css?.slice(0, 80)}`);
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

// It follows the window's look, live: shadcn dark is #09090b, shadcn light
// bright, and the open page loads again with the change.
await setKoanPref('koan.look.style', 'shadcn');
await setKoanPref('koan.look.mode', 'dark');
check('...follows a change of look to shadcn dark (#09090b) without a reload by hand',
      await waitFor(async () => await colour(tab) === 'rgb(9, 9, 11)'), await colour(tab));
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
await setKoanPref('koan.look.style', 'zen');
check('...and back to the Zen look\'s colour, the third change in a row',
      await waitFor(async () => await colour(tab) === before), await colour(tab));
await tab.close();

// Chromium's own page, chosen in Koan's settings.
const [first] = await pages();
await front(first);
const settings = (await browserCall('Target.createTarget',
    { url: 'chrome://koan-settings', background: true })).result.targetId;
const settingsPage = await waitFor(async () => (await pages()).find(p => p.id === settings));
await waitFor(() => evalIn(settingsPage, `document.querySelectorAll('.category').length > 0`));
const row = `document.querySelector('[data-pref="koan.newtab.page"]')`;
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
// model service.
const log = process.env.KOAN_LOG ? fs.readFileSync(process.env.KOAN_LOG, 'utf8') : '';
for (const [what, pattern] of [
    ['a refused chrome://newtab/ load', /incorrect profile type/],
    ['in-product help for tab groups', /IPH_TabGroups/],
    ['the on-device model service', /on_device_model/]]) {
  const lines = log.split('\n').filter(l => pattern.test(l));
  check(`The browser's log has nothing about ${what}`,
        process.env.KOAN_LOG && lines.length === 0, lines.slice(0, 2).join(' | '));
}

finish();
