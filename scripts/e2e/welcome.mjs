// End-to-end test of the welcome (onboarding v2, chrome://koan-welcome;
// welcome.flags): on a first run the page covers the window; Get started,
// then each step with Next, and "Start browsing" closes it. The shortcuts
// step shows on macOS only (koan_welcome_shortcuts.cc: no rows elsewhere).
// The Mac's input checks are welcome_input.mjs and
// scripts/mac/steps/welcome.md.
//
//   scripts/e2e/run.sh welcome
import { CDP_PORT, IS_MAC, check, evalIn, finish, shownViews, sleep, views } from './lib/koan.mjs';

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
const step = () => js(`document.getElementById('welcome-card')?.getAttribute('step')`);
const click = async selector => {
  await js(`document.querySelector(${JSON.stringify(selector)})?.click()`);
  await sleep(500);
};
const next = () => click('#welcome-card .step-foot .button.primary');

await sleep(1500);
let vs = await views();
const overlay = vs.find(v => v.cls === 'WebView' && v.path.at(-1) === 'BrowserView');
const frame = vs.find(v => v.cls === 'BrowserView');
check('...over the whole window', overlay && frame && overlay.w === frame.w && overlay.h === frame.h,
      `${overlay?.w}x${overlay?.h} of ${frame?.w}x${frame?.h}`);
// In the window itself, the last of its views (the c174e/f no-input
// regression came from a second, child window holding the page).
const frameDepth = frame ? frame.path.length + 1 : -1;
const frameChildren = vs.filter(v => v.path.length === frameDepth && v.path.at(-1) === 'BrowserView');
check('...in the browser window, above its other views', overlay && frameChildren.at(-1) === overlay,
      frameChildren.map(v => v.cls).join(','));
check('...and the URL bar did not open over it (the empty tab at startup)',
      shownViews(vs, 'KoanUrlBarBackdrop').length === 0);
check('...on the welcome step', await step() === 'welcome', await step());
check('...in shadcn\'s tokens', await js(
    `getComputedStyle(document.documentElement).getPropertyValue('--primary').trim() !== ''`));
check('...with Skip setup', await js(`!document.getElementById('welcome-skip').hidden`));
await next();
if (IS_MAC) {
  check('Next: shortcuts (macOS)', await step() === 'shortcuts', await step());
  check('...Zen\'s shortcuts chosen', await js(
      `document.querySelector('input[name=shortcut-preset]:checked')?.value`) === 'zen',
      await js(`document.querySelector('input[name=shortcut-preset]:checked')?.value`));
  await next();
  check('Next: import', await step() === 'import', await step());
} else {
  // Off macOS there is no shortcuts step.
  check('Next: import (shortcuts are macOS only)', await step() === 'import', await step());
}
check('..."Start fresh" chosen', await js(`document.querySelector('input[name=import]:checked')?.value`) === 'none');
await next();
check('Next: search engine', await step() === 'search', await step());
check('...Brave chosen', await js(
    `document.querySelector('input[name=engine]:checked')?.closest('label')?.textContent.trim()`) === 'Brave');
// The engines' logos (Simple Icons, welcome_icons/engine-*.svg), not a letter
// (#349), where Koan has one: Brave's and Google's at least, whatever the
// region; the others keep their initial.
const marks = await js(`[...document.querySelectorAll('input[name=engine]')].map(input => {
  const label = input.closest('label');
  const mark = label.querySelector('.tile .icon.mark');
  return { name: label.textContent.trim(), mark: mark?.dataset.icon ?? null,
           masked: !!mark && getComputedStyle(mark).maskImage.includes('engine-') };
})`);
const brave = marks?.find(m => m.mark === 'engine-brave');
check('...with Brave\'s logo on its tile', !!brave?.masked, JSON.stringify(marks));
check('...and Google\'s on Google\'s', !!marks?.find(m => m.mark === 'engine-google' && m.masked),
      JSON.stringify(marks));
const markSvg = await js(`fetch('icons/engine-brave.svg').then(r => r.text())`);
check('...a served SVG', /<svg[^>]*viewBox/.test(markSvg ?? ''), markSvg?.slice(0, 80));
await next();
check('Next: done', await step() === 'done', await step());
check('...which says "Enjoy browsing."', await js(
    `document.querySelector('#welcome-card p')?.textContent.trim()`) === 'Enjoy browsing.',
    await js(`document.querySelector('#welcome-card p')?.textContent.trim()`));
await next();
await sleep(1000);
vs = await views();
check('"Start browsing" closes the welcome', !vs.some(v => v.cls === 'WebView' && v.path.at(-1) === 'BrowserView' && v.w === frame.w));
finish();
