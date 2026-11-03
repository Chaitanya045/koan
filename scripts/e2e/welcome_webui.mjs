// End-to-end test of the welcome screen as chrome://koan-welcome (Zen's
// welcome flow, koan-welcome-webui; welcome_webui.flags): on a first run the
// page fills the window; Start, then Zen's pages with the keyboard (one Tab
// stop per list, letters jump), and "Dive in!" closes it and pins the chosen
// Essentials.
//
//   scripts/e2e/run.sh welcome_webui
import { CDP_PORT, check, evalIn, finish, key, saveShot, sleep, views } from './lib/koan.mjs';

// The welcome page's DevTools target (a WebView's page).
async function welcomePage() {
  for (let i = 0; i < 20; ++i) {
    const targets = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json();
    const page = targets.find(t => t.url.startsWith('chrome://koan-welcome'));
    if (page) {
      return page;
    }
    await sleep(500);
  }
  return null;
}

const page = await welcomePage();
check('A first run shows chrome://koan-welcome', !!page);
if (!page) {
  finish();
}
const js = expression => evalIn(page, expression);
const heading = () => js(`document.querySelector('.zen-welcome-text:not([exiting]) h1')?.textContent`);
const checkedName = () => js(`document.querySelector('.zen-welcome-page:not([exiting]) ` +
                             `input[type=radio]:checked')?.closest('label')?.textContent.trim()`);
const click = async selector => {
  await js(`document.querySelector(${JSON.stringify(selector)})?.click()`);
  await sleep(900);
};
const next = () => click('#zen-welcome-page-sidebar-buttons .primary');

let vs = await views();
const overlay = vs.find(v => v.cls === 'WebView' && v.path.length <= 2);
const frame = vs.find(v => v.cls === 'BrowserView');
check('...over the whole window', overlay && frame && overlay.w === frame.w && overlay.h === frame.h,
      `${overlay?.w}x${overlay?.h} of ${frame?.w}x${frame?.h}`);
check('...with Zen\'s title', (await js(`document.getElementById('zen-welcome-title').textContent`))
      === 'Welcome toa calmer internet');
check('The window is Zen\'s first-run size, 1200 wide (the screen is 1280x800)',
      frame?.w === 1200, `${frame?.w}x${frame?.h}`);
check('Zen\'s 11px root, which its rem sizes are of',
      await js(`getComputedStyle(document.documentElement).fontSize`) === '11px');
await sleep(3500);
saveShot('webui-start');
check('The start button has the keyboard',
      await js(`document.activeElement?.id`) === 'zen-welcome-start-button');
await click('#zen-welcome-start-button');
await sleep(600);

check('Start shows the import page', await heading() === 'Bring your stuff along.', await heading());
const card = await js(`(() => { const r = document.getElementById('zen-welcome-pages')
    .getBoundingClientRect(); return [r.width / innerWidth, r.height / innerHeight]; })()`);
check('...in Zen\'s 86% x 82% card', Math.abs(card[0] - 0.86) < 0.01 &&
      Math.abs(card[1] - 0.82) < 0.01, card.join(' x '));
check('...titled at Zen\'s 33px / 700', await js(
    `(s => s.fontSize + ' ' + s.fontWeight)(getComputedStyle(` +
    `document.querySelector('.zen-welcome-text h1')))`) === '33px 700');
check('...with "NO, start fresh." chosen first', (await checkedName())?.startsWith('NO'),
      await checkedName());
saveShot('webui-import');
await next();

check('Next: colours', await heading() === 'Your space, your colors.', await heading());
check('...the theme picker\'s panel with nine colours', await js(
    `document.querySelectorAll('.zen-welcome-theme-picker .zen-welcome-color').length`) === 9);
check('...and its auto / light / dark buttons', await js(
    `document.querySelectorAll('.zen-theme-picker-scheme input').length`) === 3);
await click('.zen-welcome-color:nth-child(5) input');
check('...a click picks one', await js(
    `document.querySelector('input[name=zen-welcome-color]:checked')?.value`) !== undefined);
saveShot('webui-colors');
await next();

check('Next: the search engine', await heading() === 'Pick your search engine.', await heading());
const engines = await js(`[...document.querySelectorAll('[page=search] .zen-welcome-option')]
    .map(o => o.textContent.trim())`);
check('...a list of engines', engines?.length >= 2, engines?.join(', '));
check('...each with an icon or a letter tile, no keycap column', await js(
    `[...document.querySelectorAll('[page=search] .engine-icon-wrapper')]
       .every(w => w.querySelector('img, .engine-icon-letter'))`));
// The keyboard: focus the chosen engine, type another's first letter.
const initial = n => n.slice(0, 1).toLowerCase();
const current = await checkedName();
const target = engines?.find(n => n !== current && /[a-z]/.test(initial(n)) &&
                                  engines.filter(m => initial(m) === initial(n)).length === 1);
await js(`document.querySelector('[page=search] input:checked')?.focus()`);
if (target) {
  await key(page, initial(target).toUpperCase().charCodeAt(0), 0, initial(target));
  await sleep(400);
  check(`...typing ${initial(target).toUpperCase()} picks ${target}`, await checkedName() === target,
        await checkedName());
}
saveShot('webui-search');
await next();

check('Next: Essentials', await heading() === 'Choose the apps you use most.', await heading());
check('...nine apps with Zen\'s icons', await js(
    `[...document.querySelectorAll('.zen-welcome-essential')]
       .filter(b => getComputedStyle(b).backgroundImage.includes('/favicons/')).length`) === 9);
await click('.zen-welcome-essential[data-url="https://obsidian.md"]');
await click('.zen-welcome-essential[data-url="https://github.com"]');
saveShot('webui-essentials');
await next();

check('Next: default browser', await heading() === 'Make Koan your default?', await heading());
await next();
check('Next: done', await heading() === 'All set? Let’s get rolling!', await heading());
saveShot('webui-done');
await click('#zen-welcome-page-sidebar-buttons .primary');
await sleep(1500);

vs = await views();
check('"Dive in!" closes the welcome screen',
      !vs.some(v => v.cls === 'WebView' && v.path.length <= 2 && v.w === frame.w));
const pinned = vs.filter(v => v.cls === 'TabView' && v.path.includes('PinnedTabContainerView'));
check('...and pins the chosen Essentials', pinned.length === 2, `${pinned.length} pinned tabs`);
finish();
