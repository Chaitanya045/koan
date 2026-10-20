// End-to-end test of Zen's URL bar against a real build: the behaviour
// setting (edit in the pill, float on the first keystroke), Ctrl+T opening
// the URL bar instead of a tab (replace-newtab), the pill's copy-URL button
// and toast, and its site panel button.
//
//   scripts/e2e/run.sh urlbar
//   PREFS='{"koan":{"urlbar":{"behavior":"float"}}}' BEHAVIOR=float \
//     scripts/e2e/run.sh urlbar
//
// BEHAVIOR (default floating-on-type) is the koan.urlbar.behavior the
// profile was started with. With SHOTS=<dir> (see run.sh) it saves
// screenshots of each state there.
import { copyFileSync, readdirSync, statSync } from 'node:fs';

const UI_PORT = process.env.UI_PORT;
const CDP_PORT = process.env.CDP_PORT;
const BEHAVIOR = process.env.BEHAVIOR || 'floating-on-type';
const SHOTS = process.env.SHOTS;
const sleep = ms => new Promise(r => setTimeout(r, ms));

async function connect(url) {
  const ws = new WebSocket(url);
  let id = 0;
  const pending = new Map();
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  };
  await new Promise(r => (ws.onopen = r));
  const send = (method, params = {}) => new Promise(resolve => {
    pending.set(++id, resolve);
    ws.send(JSON.stringify({ id, method, params }));
    setTimeout(() => resolve({ timedOut: true }), 5000);
  });
  return { send, close: () => ws.close() };
}

// Every native view as {cls, nodeId, shown, x, y, w, h, path, p}; `shown`
// includes the ancestors' visibility, `path` is the ancestor class names,
// `p` the view's properties.
async function views() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const out = [];
  const walk = async (n, depth, shown, path) => {
    const a = n.attributes ?? [];
    const cls = a[a.indexOf('class') + 1];
    if (depth > 0 && n.nodeName === 'View' && cls) {
      const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
      const p = {};
      for (const rule of r.result?.matchedCSSRules ?? []) {
        for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
      }
      shown = shown && p.Visible !== 'false';
      out.push({ cls, nodeId: n.nodeId, shown, x: +p.X, y: +p.Y, w: +p.Width, h: +p.Height, path, p });
      path = [...path, cls];
    }
    for (const c of n.children ?? []) await walk(c, depth + 1, shown, path);
  };
  await walk(doc, 0, true, []);
  ui.close();
  return out;
}

const find = (vs, cls) => vs.find(v => v.cls === cls);
const shown = (vs, cls) => vs.some(v => v.cls === cls && v.shown);

async function uiEvent(view, method, event) {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('DOM.getDocument', { depth: -1 });
  await ui.send(method, { nodeId: view.nodeId, event });
  ui.close();
}

async function mouse(view, type) {
  await uiEvent(view, 'DOM.dispatchMouseEvent',
                { type, x: 10, y: 10, button: 'left', wheelDirection: 'none' });
}

async function click(view) {
  await mouse(view, 'mousePressed');
  await mouse(view, 'mouseReleased');
  await sleep(1000);
}

async function viewKey(view, keyCode) {
  for (const type of ['keyPressed', 'keyReleased']) {
    await uiEvent(view, 'DOM.dispatchKeyEvent',
                  { type, keyCode, code: 0, flags: 0, key: 0, isChar: false });
  }
  await sleep(800);
}

// Sends a key through the window holding the omnibox (its focus manager,
// then the focused view and its event handlers), like a real key press.
// flags: ui::EventFlags (Ctrl = 4).
async function windowKey(keyCode, flags = 0) {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const has = (n, cls) => {
    const a = n.attributes ?? [];
    return a[a.indexOf('class') + 1] === cls || (n.children ?? []).some(c => has(c, cls));
  };
  const findWidget = n => n.nodeName === 'Widget' && has(n, 'OmniboxViewViews') ? n
      : (n.children ?? []).map(findWidget).find(Boolean);
  const widget = findWidget(doc);
  for (const type of ['keyPressed', 'keyReleased']) {
    await ui.send('DOM.dispatchKeyEvent', { nodeId: widget.nodeId,
        event: { type, keyCode, code: 0, flags, key: 0, isChar: false } });
  }
  ui.close();
  await sleep(800);
}

// Types into a textfield the way an input method would.
async function type(view, text) {
  for (const ch of text) {
    await uiEvent(view, 'DOM.dispatchKeyEvent',
                  { type: 'keyPressed', keyCode: 0, code: 0, flags: 0,
                    key: ch.codePointAt(0), isChar: true });
  }
  await sleep(1000);
}

async function pages() {
  return (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .filter(t => t.type === 'page');
}

// Sends a key press to the active page. Modifiers: Alt=1 Ctrl=2 Shift=8.
async function key(code, modifiers, keyName = '') {
  const page = (await pages())[0];
  const c = await connect(page.webSocketDebuggerUrl);
  const base = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers, key: keyName };
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
  c.close();
  await sleep(1200);
}

async function evalIn(page, expression) {
  const c = await connect(page.webSocketDebuggerUrl);
  const r = await c.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  c.close();
  return r.result?.result?.value;
}

// Reads the clipboard by pasting (Ctrl+V) into a fresh textarea on the page.
async function clipboard() {
  const page = (await pages())[0];
  await evalIn(page, `(() => {
    const t = document.createElement('textarea');
    t.id = 'koan-paste';
    document.body.append(t);
    t.focus();
  })()`);
  await key(86, 2, 'v');
  return evalIn(page, `(() => { const t = document.getElementById('koan-paste');
                                const v = t.value; t.remove(); return v; })()`);
}

// Copies the window's latest off-screen frame (the largest dump file).
function shot(name) {
  if (!SHOTS) return;
  const dir = `${SHOTS}/frames`;
  const frames = readdirSync(dir).filter(f => f.endsWith('.png'))
      .map(f => `${dir}/${f}`).sort((a, b) => statSync(b).size - statSync(a).size);
  if (frames.length) copyFileSync(frames[0], `${SHOTS}/urlbar-${BEHAVIOR}-${name}.png`);
}

let failures = 0;
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
}

const CTRL = 2;
const VK = { T: 84, ESC: 27, ENTER: 13 };
const WINDOW_WIDTH = 1280;
// Zen: min(window width / 1.5, 750), centred.
const FLOAT_WIDTH = Math.min(Math.floor(WINDOW_WIDTH * 2 / 3), 750);

const bar = vs => find(vs, 'TopContainerView');
const isFloating = vs => bar(vs)?.w === FLOAT_WIDTH &&
    bar(vs).x === (WINDOW_WIDTH - FLOAT_WIDTH) / 2 && shown(vs, 'KoanUrlBarBackdrop');
const omniboxText = vs => find(vs, 'OmniboxViewViews')?.p.Text ?? '';

// Zen: one Esc closes the bar.
const closeBar = () => windowKey(VK.ESC);

// --- The pill ----------------------------------------------------------------
let vs = await views();
const pill = find(vs, 'KoanUrlPill');
const pillLabel = vs.find(v => v.cls === 'Label' && v.path.at(-1) === 'KoanUrlPill');
check('The pill shows the site, like Zen', pillLabel?.p.Text === 'example.com',
      JSON.stringify(pillLabel?.p.Text));
const pillButtons = () => vs.filter(v => v.path.at(-1) === 'KoanUrlPill' && v.cls === 'ImageButton');
check('...with the site panel button, and no copy button until hovered',
      pillButtons().length === 2 && !pillButtons()[0].shown && pillButtons()[1].shown,
      pillButtons().map(b => `${b.p.TooltipText}:${b.shown}`).join(' '));
await mouse(pill, 'mouseEntered');
await sleep(500);
vs = await views();
const copy = pillButtons()[0];
check('Hovering the pill shows the copy-URL button', copy?.shown, copy?.p.TooltipText);
await click(copy);
vs = await views();
const toast = find(vs, 'KoanToastView');
check('...which shows Zen\'s toast', toast?.shown, toast && `${toast.w}x${toast.h}`);
shot('toast');
const copied = await clipboard();
check('...and copies the URL', copied === 'https://example.com/', JSON.stringify(copied));
await sleep(3000);
vs = await views();
check('The toast goes away by itself', !shown(vs, 'KoanToastView'));
await mouse(find(vs, 'KoanUrlPill'), 'mouseExited');

await click(pillButtons()[1]);
vs = await views();
check('The site panel button opens the page info bubble', shown(vs, 'PageInfoBubbleView'));
const closeInfo = vs.find(v => v.path.includes('PageInfoBubbleView') && v.shown &&
    /close/i.test(`${v.p.TooltipText ?? ''} ${v.p.AccessibleName ?? ''}`));
if (closeInfo) await click(closeInfo);

// --- Editing (the behaviour setting) ------------------------------------------
vs = await views();
await click(find(vs, 'KoanUrlPill'));
vs = await views();
const pillNow = find(vs, 'KoanUrlPill');
if (BEHAVIOR === 'float') {
  check('Clicking the pill floats the URL bar ("float")', isFloating(vs),
        `${bar(vs)?.w}x${bar(vs)?.h} at ${bar(vs)?.x},${bar(vs)?.y}`);
} else {
  check('Clicking the pill edits the address in the pill',
        bar(vs)?.x === pillNow.x && bar(vs)?.w === pillNow.w && !isFloating(vs),
        `bar ${bar(vs)?.x}+${bar(vs)?.w}, pill ${pillNow.x}+${pillNow.w}`);
  check('...with the address to edit', omniboxText(vs).includes('example.com'),
        JSON.stringify(omniboxText(vs)));
  const toolbarChildren = vs.filter(v => v.path.at(-1) === 'ToolbarView' && v.shown);
  check('...showing just the location bar',
        toolbarChildren.length === 1 && toolbarChildren[0].cls === 'LocationBarView',
        toolbarChildren.map(v => v.cls).join(' '));
  shot('in-pill');
}
await type(find(vs, 'OmniboxViewViews'), 'wiki');
vs = await views();
if (BEHAVIOR === 'normal') {
  check('Typing keeps the bar in place ("normal"), widened over the page',
        bar(vs)?.x === pillNow.x && bar(vs)?.w > pillNow.w && !isFloating(vs),
        `${bar(vs)?.w}x${bar(vs)?.h} at ${bar(vs)?.x},${bar(vs)?.y}`);
} else {
  check('Typing floats the URL bar, centred and Zen-sized', isFloating(vs),
        `${bar(vs)?.w}x${bar(vs)?.h} at ${bar(vs)?.x},${bar(vs)?.y}`);
}
shot('typing');
await closeBar();
vs = await views();
check('Esc closes it, and the toolbar is back to normal',
      bar(vs)?.w === WINDOW_WIDTH && !shown(vs, 'KoanUrlBarBackdrop') &&
      vs.filter(v => v.path.at(-1) === 'ToolbarView' && v.shown).length > 1,
      `width ${bar(vs)?.w}`);
check('...and the pill shows the site again',
      vs.find(v => v.cls === 'Label' && v.path.at(-1) === 'KoanUrlPill')?.p.Text === 'example.com');

// --- Ctrl+T: the URL bar in place of a new tab ---------------------------------
const tabsBefore = (await pages()).length;
await key(VK.T, CTRL, 't');
vs = await views();
const newTabPlaced = BEHAVIOR === 'normal' ? bar(vs)?.x === pillNow.x : isFloating(vs);
check('Ctrl+T opens the URL bar' + (BEHAVIOR === 'normal' ? ' in the pill' : ', floating'),
      newTabPlaced, `${bar(vs)?.w}x${bar(vs)?.h} at ${bar(vs)?.x},${bar(vs)?.y}`);
check('...empty', omniboxText(vs) === '', JSON.stringify(omniboxText(vs)));
check('...without creating a tab', (await pages()).length === tabsBefore);
shot('new-tab');
await key(VK.T, CTRL, 't');
vs = await views();
check('Ctrl+T again closes it', bar(vs)?.w === WINDOW_WIDTH &&
      (await pages()).length === tabsBefore, `width ${bar(vs)?.w}`);

// The sidebar's "+ New Tab" row does the same as Ctrl+T.
await click(find(await views(), 'KoanNewTabRow'));
vs = await views();
check('The New Tab row opens the URL bar too, without a tab',
      (BEHAVIOR === 'normal' ? bar(vs)?.x === pillNow.x : isFloating(vs)) &&
          omniboxText(vs) === '' && (await pages()).length === tabsBefore,
      `${bar(vs)?.w} at ${bar(vs)?.x}, ${(await pages()).length} tab(s)`);
await closeBar();

await key(VK.T, CTRL, 't');
vs = await views();
await type(find(vs, 'OmniboxViewViews'), 'koan');
if (BEHAVIOR === 'normal') {
  await closeBar();
} else {
  await click(find(vs, 'KoanUrlBarBackdrop'));
}
vs = await views();
check('A click outside (or Esc) closes it without creating a tab',
      bar(vs)?.w === WINDOW_WIDTH && (await pages()).length === tabsBefore,
      `width ${bar(vs)?.w}, ${(await pages()).length} tab(s)`);
check('...and the pill shows the page again',
      vs.find(v => v.cls === 'Label' && v.path.at(-1) === 'KoanUrlPill')?.p.Text === 'example.com');
if (BEHAVIOR !== 'normal') {
  await key(VK.T, CTRL, 't');
  vs = await views();
  check('Ctrl+T within 45 s brings the unfinished search back', omniboxText(vs) === 'koan',
        JSON.stringify(omniboxText(vs)));
  await closeBar();
}

const firstUrl = (await pages())[0].url;
await key(VK.T, CTRL, 't');
vs = await views();
const omnibox = find(vs, 'OmniboxViewViews');
// Ctrl+A: replace any unfinished search.
await windowKey(65, 4);
await type(omnibox, 'example.com/?typed');
await viewKey(omnibox, VK.ENTER);
await sleep(1000);
const after = await pages();
check('Opening a result opens it in a new tab', after.length === tabsBefore + 1 &&
      after.some(p => p.url.endsWith('?typed')), after.map(p => p.url).join(' '));
check('...and leaves the page it was opened from alone',
      after.some(p => p.url === firstUrl), firstUrl);
vs = await views();
check('...and the URL bar is closed', bar(vs)?.w === WINDOW_WIDTH, `width ${bar(vs)?.w}`);

console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
