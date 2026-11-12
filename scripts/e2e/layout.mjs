// End-to-end test of Zen's default layout against a real build: single
// toolbar (no top toolbar; navigation buttons and a URL pill in the
// sidebar), the URL bar (Ctrl+L floats; a click on the pill edits in the
// pill, see urlbar.mjs for the rest), and
// switching to the multiple-toolbars layout (Ctrl+Alt+B).
//
//   scripts/e2e/run.sh layout
//
// On macOS (scripts/mac/suite.sh) Cmd+L, the main menu's Open Location, is a
// real key (lib/mac.mjs menuKey(); a CDP key never reaches the menu bar);
// with real input off its steps are SKIPs.
import { ACCEL, sendKey } from './lib/koan.mjs';
import { menuKey, menuKeyWhyNot, skipped } from './lib/mac.mjs';
const UI_PORT = process.env.UI_PORT;
const CDP_PORT = process.env.CDP_PORT;
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

// Every native view as {cls, nodeId, shown, x, y, w, h, path}; `shown`
// includes the ancestors' visibility, `path` is the ancestor class names.
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
      out.push({ cls, nodeId: n.nodeId, shown, x: +p.X, y: +p.Y, w: +p.Width, h: +p.Height, path });
      path = [...path, cls];
    }
    for (const c of n.children ?? []) await walk(c, depth + 1, shown, path);
  };
  await walk(doc, 0, true, []);
  ui.close();
  return out;
}

const find = (vs, cls) => vs.find(v => v.cls === cls);

// Sends a native event to a view (UI DevTools).
async function uiEvent(view, method, event) {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('DOM.getDocument', { depth: -1 });
  await ui.send(method, { nodeId: view.nodeId, event });
  ui.close();
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

async function click(view) {
  for (const type of ['mousePressed', 'mouseReleased']) {
    await uiEvent(view, 'DOM.dispatchMouseEvent',
                  { type, x: 10, y: 10, button: 'left', wheelDirection: 'none' });
  }
  await sleep(1000);
}

const firstPage = async () =>
  (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .filter(t => t.type === 'page')[0];
// Sends a key press to the active page. Modifiers: Alt=1 Ctrl=2 Shift=8.
async function key(code, modifiers, keyName = '') {
  const c = await connect((await firstPage()).webSocketDebuggerUrl);
  await sendKey(c, code, modifiers, keyName);
  c.close();
  await sleep(1200);
}
// Cmd+L (Ctrl+L off macOS): whether it went.
async function openLocation() {
  return menuKey(await firstPage(), 'l', 'c', () => key(VK.L, ACCEL, 'l'), 1200);
}

let failures = 0;
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
}

const CTRL = 2, ALT = 1;
const VK = { L: 76, B: 66, ESC: 27 };

let vs = await views();
// The browser view's width: 1280 in run.sh's window, less on a Mac screen
// that can't fit it (the suite's 1280-wide window comes up about 1248 wide
// there), as urlbar.mjs measures it.
const WINDOW_WIDTH = find(vs, 'BrowserView')?.w || 1280;
const WINDOW_HEIGHT = find(vs, 'BrowserView')?.h || 800;
// The floating card in the default look, shadcn's (#296;
// KoanUrlBarController::CardBounds()): shadcn's command dialog, 55% of the
// window wide, centred, a little above the middle. (Zen's look, min(window
// width / 1.5, 750) and centred, is checked by urlbar.mjs.)
const FLOAT_WIDTH = Math.floor(WINDOW_WIDTH * 55 / 100);
const FLOAT_X = Math.floor((WINDOW_WIDTH - FLOAT_WIDTH) / 2);
const isFloatingCard = bar => bar?.w === FLOAT_WIDTH && bar.x === FLOAT_X && bar.y > 0 &&
    bar.y + bar.h / 2 < WINDOW_HEIGHT / 2;
const buttons = find(vs, 'KoanSidebarNavButtons');
check('Single toolbar by default: navigation buttons in the sidebar top row',
      buttons?.shown && buttons.path.includes('VerticalTabStripTopContainer'),
      JSON.stringify(buttons && { shown: buttons.shown, parent: buttons.path.at(-1) }));
const pill = find(vs, 'KoanSidebarNavView');
check('...a URL pill below it', pill?.shown && pill.h > 0, `height ${pill?.h}`);
const top = find(vs, 'TopContainerView');
check('...and no top toolbar in the layout',
      top?.path.includes('TopContainerOverlayView'), top?.path.at(-1));
// Like Zen, no strip is kept for the window buttons: the page card starts
// at the thin gutter, and a hot zone along the top edge reveals the buttons.
const card = vs.find(v => v.cls === 'ContentsContainerView' && v.shown);
check('...the page reaches the top of the window (8px gutter)', card?.y === 8, `y ${card?.y}`);
check('...with a hot zone for the window buttons along the top edge',
      vs.filter(v => v.cls === 'KoanHotZone' && v.shown).length === 1);

let bar;
if (await openLocation()) {
  vs = await views();
  bar = find(vs, 'TopContainerView');
  check('Ctrl+L opens the floating URL bar (Zen: the keyboard floats), the default look\'s ' +
            'card: 55% of the window, centred, above the middle',
        isFloatingCard(bar),
        `${bar?.w}x${bar?.h} at ${bar?.x},${bar?.y} in ${WINDOW_WIDTH}x${WINDOW_HEIGHT}`);
  const toolbarChildren = vs.filter(v => v.path.at(-1) === 'ToolbarView' && v.shown);
  check('...showing just the location bar',
        toolbarChildren.length === 1 && toolbarChildren[0].cls === 'LocationBarView',
        toolbarChildren.map(v => v.cls).join(' '));

  const omnibox = find(vs, 'OmniboxViewViews');
  await uiEvent(omnibox, 'DOM.dispatchKeyEvent',
                { type: 'keyPressed', keyCode: 0, code: 0, flags: 0, key: 'w'.codePointAt(0), isChar: true });
  await sleep(1000);
  vs = await views();
  bar = find(vs, 'TopContainerView');
  check('...and typing keeps it so', isFloatingCard(bar),
        `${bar?.w}x${bar?.h} at ${bar?.x},${bar?.y}`);
  await windowKey(VK.ESC);
  vs = await views();
  bar = find(vs, 'TopContainerView');
  check('Esc closes the URL bar', bar?.w === WINDOW_WIDTH, `width ${bar?.w}`);
  check('...and the toolbar is back to normal',
        vs.filter(v => v.path.at(-1) === 'ToolbarView' && v.shown).length > 1);
} else {
  skipped('Ctrl+L opens the floating URL bar (and typing, Esc)', menuKeyWhyNot());
  vs = await views();
}

const urlPill = find(vs, 'KoanUrlPill');
await click(find(vs, 'KoanUrlPill'));
vs = await views();
bar = find(vs, 'TopContainerView');
check('Clicking the URL pill opens the URL bar there (Zen: a click never floats it)',
      bar?.x === urlPill?.x && bar?.w === urlPill?.w, `${bar?.w} at ${bar?.x}`);
await uiEvent(find(vs, 'OmniboxViewViews'), 'DOM.dispatchKeyEvent',
              { type: 'keyPressed', keyCode: 0, code: 0, flags: 0, key: 'w'.codePointAt(0), isChar: true });
await sleep(1000);
vs = await views();
bar = find(vs, 'TopContainerView');
check('...and typing keeps it in place, not floating',
      bar?.x === urlPill?.x && bar?.x !== FLOAT_X,
      `${bar?.w} at ${bar?.x}`);
await windowKey(VK.ESC);

await key(VK.B, CTRL | ALT, 'b');
vs = await views();
check('Ctrl+Alt+B switches to multiple toolbars',
      !find(vs, 'KoanSidebarNavView') && !find(vs, 'KoanSidebarNavButtons') &&
      find(vs, 'TopContainerView')?.path.at(-1) === 'BrowserView');
if (await openLocation()) {
  vs = await views();
  bar = find(vs, 'TopContainerView');
  // The toolbar sits beside the sidebar there, so it is narrower than the
  // window, but it stays in place.
  check('...where Ctrl+L uses the toolbar in place',
        bar?.path.at(-1) === 'BrowserView' && bar.w > FLOAT_WIDTH && bar.y === 0,
        `${bar?.w}x${bar?.h} at ${bar?.x},${bar?.y}`);
} else {
  skipped('...where Ctrl+L uses the toolbar in place', menuKeyWhyNot());
}
await key(VK.B, CTRL | ALT, 'b');
vs = await views();
check('Ctrl+Alt+B switches back', find(vs, 'KoanSidebarNavView')?.shown === true);

console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
