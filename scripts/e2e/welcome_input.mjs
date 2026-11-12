// End-to-end test that the welcome (chrome://koan-welcome, #174/#181) takes
// clicks and keys on macOS: the c174e/f "no input" blocker came from AppKit
// (a second NSWindow that split key and main), so the checks that matter
// use the real keyboard and pointer (keypress2 to Koan's pid, ptr), which go
// through AppKit like a person's. Steps W-1r, W-1c and W-14b of
// scripts/mac/steps/welcome.md:
// - Tab moves the page's focus, arrows change a radio, Space picks it;
// - a click on Get started and on Next moves to the next step;
// - a click in the window's top band over the page reaches the page and
//   doesn't move the window (W-1c, the caption hit-test hook);
// - Esc ends the setup: the page and its view go, the browser stays.
// Each event is checked in the page (isTrusted events seen by its own
// listeners). Without real input, the same keys and clicks go through CDP
// (the page's side only) and the AppKit checks are reported as skipped.
// welcome_input.flags forces the first run. Run by scripts/mac/suite.sh.
import { CDP_PORT, check, evalIn, finish, onPage, sleep, viewTree, waitFor } from './lib/koan.mjs';
import { IS_MAC, alive, calibrate, fatalInLog, key, makeKey, realClick, realInput,
         realInputWhyNot, realKey, skipped, windowOf } from './lib/mac.mjs';

async function welcomePage() {
  return waitFor(async () => (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .find(t => t.url.startsWith('chrome://koan-welcome')), 15000);
}
const page = await welcomePage();
check('A first run shows chrome://koan-welcome', !!page);
if (!page) finish();
await sleep(2000);
const js = expression => evalIn(page, expression);
const step = () => js(`document.getElementById('welcome-card')?.getAttribute('step')`);
// What the page saw: trusted events by type, and the focused element.
await js(`(() => {
  window.__seen = { keydown: [], mousedown: 0, click: 0 };
  addEventListener('keydown', e => e.isTrusted && __seen.keydown.push(e.key), true);
  addEventListener('mousedown', e => { if (e.isTrusted) __seen.mousedown++; }, true);
  addEventListener('click', e => { if (e.isTrusted) __seen.click++; }, true);
  return 1;
})()`);
const seen = () => js('window.__seen');
const focused = () => js(`(() => { const a = document.activeElement;
    return a ? a.tagName + (a.id ? '#' + a.id : '') + (a.name ? '[' + a.name + ']' : '') +
               (a.value ? '=' + a.value : '') : ''; })()`);
// The centre of `selector` in the page.
const centre = selector => js(`(() => { const r = document.querySelector(${JSON.stringify(selector)})
    ?.getBoundingClientRect(); return r && { x: r.x + r.width / 2, y: r.y + r.height / 2 }; })()`);
const overlay = async () => (await viewTree()).find(
    v => v.cls === 'WebView' && v.shown && v.path.at(-1) === 'BrowserView');

const real = realInput();
let origin = null;
if (real) {
  // A page in a window that isn't key sees no mouse moves (and the window's
  // keys go elsewhere): make the welcome's window key first, through its
  // tab (the welcome's own view isn't a tab).
  const windowId = (await windowOf(page.id))?.windowId;
  const tabs = (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .filter(t => t.type === 'page' && !t.url.startsWith('chrome://koan-welcome'));
  let tab = null;
  for (const t of tabs) {
    if ((await windowOf(t.id))?.windowId === windowId) { tab = t; break; }
  }
  await makeKey(tab);
  console.log(`  (the welcome has the focus: ${await js('document.hasFocus()')})`);
  origin = await calibrate(page, await viewTree(), await overlay());
  check('Real pointer: the page saw the pointer move (calibration)', !!origin);
}
const realOk = () => real && origin && realInput();
// The page's point (x, y) on the screen: the overlay's place in the root
// view plus the root view's on the screen.
async function screenOf(p) {
  const o = await overlay();
  return { x: origin.x + o.ax + p.x, y: origin.y + o.ay + p.y };
}
const press = async (name, flags = '', mods = 0) =>
  (realOk() ? realKey(name, flags, 500) : key(page, name, mods, 500));
const clickAt = async selector => {
  const c = await centre(selector);
  if (!c) return false;
  if (realOk()) {
    const s = await screenOf(c);
    return realClick(s.x, s.y, 900);
  }
  await onPage(page, async cdp => {
    for (const type of ['mousePressed', 'mouseReleased']) {
      await cdp.send('Input.dispatchMouseEvent', { type, x: c.x, y: c.y, button: 'left', clickCount: 1 });
    }
  });
  await sleep(900);
  return true;
};
const via = realOk() ? 'real' : 'CDP';

// Keys: Tab moves the focus.
const f0 = await focused();
await press('Tab');
const f1 = await focused();
await press('Tab');
const f2 = await focused();
let s = await seen();
check(`Tab (${via}) reaches the page`, s.keydown.filter(k => k === 'Tab').length === 2,
      JSON.stringify(s.keydown));
check(`...and moves the focus`, f1 !== f0 || f2 !== f1, `${f0} -> ${f1} -> ${f2}`);

// Clicks: Get started moves to the next step.
const before = await step();
await clickAt('#welcome-card .step-foot .button.primary');
const after = await step();
s = await seen();
check(`A click (${via}) on "${before === 'welcome' ? 'Get started' : 'Next'}" moves on`,
      after !== before, `${before} -> ${after}`);
check('...the page saw a trusted click', s.click >= 1, JSON.stringify(s));

// Arrows on a radio group change the choice; Space keeps it.
{
  const radios = await js(`[...document.querySelectorAll('#welcome-card input[type=radio]')]
      .filter(r => r.offsetParent).map(r => r.name + '=' + r.value)`);
  if (radios?.length >= 2) {
    await js(`document.querySelector('#welcome-card input[type=radio]:checked')?.focus() ??
              document.querySelector('#welcome-card input[type=radio]')?.focus(); 1`);
    const c0 = await js(`document.querySelector('#welcome-card input[type=radio]:checked')?.value`);
    await press('ArrowDown');
    await press(' ');
    const c1 = await js(`document.querySelector('#welcome-card input[type=radio]:checked')?.value`);
    check(`ArrowDown (${via}) changes the radio on step "${after}"`, c0 !== c1, `${c0} -> ${c1}`);
    await press('ArrowUp');
  } else {
    skipped('radio keys', `no radio group on step "${after}"`);
  }
}

// W-1c: a click in the window's top band over the page reaches the page and
// doesn't drag the window (real pointer only: CDP clicks never hit-test the
// window's caption).
if (realOk()) {
  const w0 = (await windowOf(page.id))?.bounds;
  const md0 = (await seen()).mousedown;
  const o = await overlay();
  await realClick(origin.x + o.ax + o.w / 2, origin.y + o.ay + 12, 900);
  const w1 = (await windowOf(page.id))?.bounds;
  check('W-1c: a click in the top band reaches the page', (await seen()).mousedown > md0);
  check('...and the window did not move', w0 && w1 && w0.left === w1.left && w0.top === w1.top,
        `${JSON.stringify(w0)} -> ${JSON.stringify(w1)}`);
} else {
  skipped('W-1r / W-1c (AppKit input)', realInputWhyNot());
}

// Esc ends the setup.
await press('Escape', '', 0);
const ended = await waitFor(async () => !(await overlay()), 5000);
const stillThere = (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
    .some(t => t.url.startsWith('chrome://koan-welcome'));
check(`Esc (${via}) ends the setup`, !!ended && !stillThere);
check('Koan is alive', await alive());
check('Nothing fatal in the log', !fatalInLog(), fatalInLog());
if (!IS_MAC) skipped('macOS', 'the AppKit checks are macOS only');
finish();
