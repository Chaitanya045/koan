// End-to-end test of the welcome (onboarding v2, chrome://koan-welcome;
// welcome.flags): on a first run the page covers the window; Get started,
// then each step with Next, and "Start browsing" closes it. Linux only (the
// Mac steps are scripts/mac/steps/welcome.md).
//
//   scripts/e2e/run.sh welcome
import { CDP_PORT, check, evalIn, finish, sleep, views } from './lib/koan.mjs';

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
check('...on the welcome step', await step() === 'welcome', await step());
check('...in shadcn\'s tokens', await js(
    `getComputedStyle(document.documentElement).getPropertyValue('--primary').trim() !== ''`));
check('...with Skip setup', await js(`!document.getElementById('welcome-skip').hidden`));
await next();
// Off macOS there is no shortcuts step.
check('Next: import (shortcuts are macOS only)', await step() === 'import', await step());
check('..."Start fresh" chosen', await js(`document.querySelector('input[name=import]:checked')?.value`) === 'none');
await next();
check('Next: search engine', await step() === 'search', await step());
check('...Brave chosen', await js(
    `document.querySelector('input[name=engine]:checked')?.closest('label')?.textContent.trim()`) === 'B' + 'Brave');
await next();
check('Next: done', await step() === 'done', await step());
await next();
await sleep(1000);
vs = await views();
check('"Start browsing" closes the welcome', !vs.some(v => v.cls === 'WebView' && v.path.at(-1) === 'BrowserView' && v.w === frame.w));
finish();
