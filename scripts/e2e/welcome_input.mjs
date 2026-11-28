// End-to-end test that the welcome (chrome://koan-welcome, #174/#181) takes
// clicks and keys on macOS: the c174e/f "no input" blocker came from AppKit
// (a second NSWindow that split key and main), so the checks that matter
// use the real keyboard and pointer (keypress2 to Koan's pid, ptr), which go
// through AppKit like a person's. Steps W-1r, W-1c and W-14b of
// scripts/mac/steps/welcome.md:
// - the keyboard stays on the page when the tab under it is focused;
// - Tab moves the page's focus and wraps within the page, arrows change a
//   radio, Space picks it;
// - a click on Get started and on Next moves to the next step;
// - a click in the window's top band over the page reaches the page and
//   doesn't move the window (W-1c, the caption hit-test hook);
// - in the Zen look (local_state/welcome_input.json; welcome.mjs has the
//   shadcn look, without it), Zen's colours step: the theme picker opens
//   over its box on the card and keeps the keyboard on the page; Back, Next
//   and Back again close it and open it again with the theme kept;
// - Esc ends the setup: the page and its view go, the browser stays, and
//   the first Space has the theme picked on the colours step.
// Each event is checked in the page (isTrusted events seen by its own
// listeners). Without real input, the same keys and clicks go through CDP
// (the page's side only) and the AppKit checks are reported as skipped.
// welcome_input.flags forces the first run. Run by scripts/mac/suite.sh.
import { centerOf, half, CDP_PORT, check, evalIn, finish, onPage, sleep, viewTree, waitFor } from './lib/koan.mjs';
import { button as pickerButton, click as clickView, closePicker, palette, picker, tree }
  from './lib/theme_picker.mjs';
import { IS_MAC, alive, calibrate, fatalInLog, key, makeKey, realClick, realInput,
         realInputWhyNot, realKey, realPointer, skipped, windowOf } from './lib/mac.mjs';

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
const centre = async selector => {
  const r = await js(`(() => { const r = document.querySelector(${JSON.stringify(selector)})
      ?.getBoundingClientRect(); return r && { x: r.x, y: r.y, width: r.width, height: r.height }; })()`);
  return r && centerOf(r);
};
const overlay = async () => (await viewTree()).find(
    v => v.cls === 'WebView' && v.shown && v.path.at(-1) === 'BrowserView');

const real = realInput();
let origin = null;
// The window's tab under the welcome (see below).
let tab = null;
const windowBounds = async () =>
  (tab ? (await windowOf(tab.id).catch(() => null))?.bounds : null) ??
  (await windowOf(page.id).catch(() => null))?.bounds;
if (real) {
  // A page in a window that isn't key sees no mouse moves (and the window's
  // keys go elsewhere): make the welcome's window key first, through its
  // tab. The welcome's own view isn't a tab, and Browser.getWindowForTarget
  // doesn't know it (so its window is the tab's: a first run has one).
  tab = (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .find(t => t.type === 'page' && !t.url.startsWith('chrome://koan-welcome')) ?? null;
  // (Page.bringToFront also gives the tab's page the keyboard, under the
  // welcome, as BrowserView::RestoreFocus() does on the window's first
  // activation: the welcome takes it back, KoanWelcome::OnDidChangeFocus().)
  await makeKey(tab);
  const back = await waitFor(() => js('document.hasFocus()'), 3000);
  check('The welcome takes the keyboard back from the tab under it', !!back);
  // Where a real move lands if not on the welcome: the tab under it (its
  // native view above the welcome's), or nowhere in Koan (the point is off).
  const tabMove = tab && (async expression => (await onPage(tab, c => c.send('Runtime.evaluate',
      { expression, returnByValue: true }))).result?.result?.value);
  await tabMove?.(`window.__koanTabMove = null; addEventListener('mousemove', e => {
      window.__koanTabMove = { sx: e.screenX, sy: e.screenY }; }, true); 1`);
  const o = await overlay();
  origin = await calibrate(page, await viewTree(), o, tab);
  check('Real pointer: the page saw the pointer move (calibration)', !!origin,
        origin ? '' : JSON.stringify({ pointer: realPointer(),
                                       window: await windowBounds(),
                                       welcome: o && { x: o.ax, y: o.ay, w: o.w, h: o.h },
                                       tabSawIt: await tabMove?.('window.__koanTabMove') }));
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
// Tab wraps within the page, as in a modal dialog: past its last control
// it went on to the browser's views under it (BODY here), and the URL bar
// that then opened over the page took the next click.
check('...and keeps it in the page', !!f2 && f2 !== 'BODY' && await js('document.hasFocus()'), f2);

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
  const w0 = await windowBounds();
  const md0 = (await seen()).mousedown;
  const o = await overlay();
  await realClick(origin.x + o.ax + half(o.w), origin.y + o.ay + 12, 900);
  const w1 = await windowBounds();
  check('W-1c: a click in the top band reaches the page', (await seen()).mousedown > md0);
  check('...and the window did not move', w0 && w1 && w0.left === w1.left && w0.top === w1.top,
        `${JSON.stringify(w0)} -> ${JSON.stringify(w1)}`);
} else {
  skipped('W-1r / W-1c (AppKit input)', realInputWhyNot());
}

// Zen's colours step (the Zen look), between import and search.
const go = async label => {
  await js(`[...document.querySelectorAll('#welcome-card .step-foot .button')]
      .find(b => b.textContent.trim() === ${JSON.stringify(label)})?.click(); 1`);
  await sleep(700);
};
for (let i = 0; i < 4 && await step() !== 'colors' && await step() !== 'search'; ++i) await go('Next');
check('The Zen look has Zen\'s colours step after import', await step() === 'colors', await step());
const dots = await js(`[...document.querySelectorAll('#welcome-steps li')]
    .filter(li => !li.hidden).map(li => li.dataset.step).join(',')`);
check('...with its step dot', dots === `welcome,${IS_MAC ? 'shortcuts,' : ''}import,colors,search,done`, dots);
check('...Zen\'s title and description', await js(`document.getElementById('welcome-title')?.textContent`) ===
      'Your space, your colors.' && await js(`document.querySelector('#welcome-card .step-head p')?.textContent`) ===
      'Personalize your browser by giving each space its own unique color identity.');
check('...and Back and Next only (Zen\'s page has Next)', await js(`[...document.querySelectorAll(
    '#welcome-card .step-foot .button')].map(b => b.textContent.trim()).join(',')`) === 'Back,Next');
const pickerOpen = () => waitFor(async () => picker(await tree()), 4000);
const pickerGone = () => waitFor(async () => !picker(await tree()), 4000);
let p = await pickerOpen();
check('The theme picker opens on the step', !!p);
// Over the box (Zen's "overlap": the panel's top-left at the box's).
{
  const box = await js(`(() => { const r = document.getElementById('welcome-colors-anchor')
      ?.getBoundingClientRect(); return r && [r.x, r.y, r.width, r.height].map(Math.round); })()`);
  const o = await overlay();
  const [x, y, w, h] = (p?.p.PanelInWindow ?? '').split(',').map(Number);
  check('...over its box on the card', box && o && Math.abs(x - (o.ax + box[0])) <= 1 &&
        Math.abs(y - (o.ay + box[1])) <= 1 && w === box[2] && h === box[3],
        `panel ${p?.p.PanelInWindow}; box ${box} in a page at ${o?.ax},${o?.ay}`);
}
check('...and the page keeps the keyboard', await js('document.hasFocus()'));
// A preset (the right arrow's page, as theme.mjs).
await clickView(pickerButton(await tree(), 'Next colors'));
await clickView((await tree()).find(v => v.cls === 'KoanThemeSwatch' && v.p.Index === '11'), 13, 13);
p = picker(await tree());
const picked = { dots: p?.p.Dots, background: p?.p.WindowBackground };
check('A preset in the picker gives the Space its dots', !!picked.dots, picked.dots);
await go('Back');
check('Back leaves the step...', await step() !== 'colors', await step());
check('...and closes the picker (Zen\'s fade)', !!await pickerGone());
await go('Next');
p = await pickerOpen();
check('Next opens it again, with the theme kept', p?.p.Dots === picked.dots, `${p?.p.Dots} / ${picked.dots}`);
await go('Next');
check('Next moves on to the search engine...', await step() === 'search', await step());
check('...and closes the picker', !!await pickerGone());
await go('Back');
check('Back on the colours step opens it again', await step() === 'colors' && !!await pickerOpen(), await step());

// Esc ends the setup.
await press('Escape', '', 0);
const ended = await waitFor(async () => !(await overlay()), 5000);
const stillThere = (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
    .some(t => t.url.startsWith('chrome://koan-welcome'));
check(`Esc (${via}) ends the setup`, !!ended && !stillThere);
check('...and closes the theme picker', !!await pickerGone());
await palette('Open Theme Picker');
p = picker(await tree());
check('The first Space has the theme picked on the colours step',
      !!picked.dots && p?.p.Dots === picked.dots && p?.p.WindowBackground === picked.background,
      `${p?.p.Dots} ${p?.p.WindowBackground} / ${picked.dots} ${picked.background}`);
await closePicker();
check('Koan is alive', await alive());
check('Nothing fatal in the log', !fatalInLog(), fatalInLog());
if (!IS_MAC) skipped('macOS', 'the AppKit checks are macOS only');
finish();
