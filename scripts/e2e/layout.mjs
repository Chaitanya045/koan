// End-to-end test of Zen's default layout against a real build: single
// toolbar (no top toolbar; navigation buttons and a URL pill in the
// sidebar), the floating URL bar (Ctrl+L or a click on the pill), and
// switching to the multiple-toolbars layout (Ctrl+Alt+B).
//
//   scripts/e2e/run.sh layout
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

async function viewKey(view, keyCode) {
  for (const type of ['keyPressed', 'keyReleased']) {
    await uiEvent(view, 'DOM.dispatchKeyEvent',
                  { type, keyCode, code: 0, flags: 0, key: 0, isChar: false });
  }
  await sleep(800);
}

async function click(view) {
  for (const type of ['mousePressed', 'mouseReleased']) {
    await uiEvent(view, 'DOM.dispatchMouseEvent',
                  { type, x: 10, y: 10, button: 'left', wheelDirection: 'none' });
  }
  await sleep(1000);
}

// Sends a key press to the active page. Modifiers: Alt=1 Ctrl=2 Shift=8.
async function key(code, modifiers, keyName = '') {
  const page = (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .filter(t => t.type === 'page')[0];
  const c = await connect(page.webSocketDebuggerUrl);
  const base = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers, key: keyName };
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
  c.close();
  await sleep(1200);
}

let failures = 0;
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
}

const CTRL = 2, ALT = 1;
const VK = { L: 76, B: 66, ESC: 27 };
const WINDOW_WIDTH = 1280;
// Zen: min(window width / 1.5, 750), centred.
const FLOAT_WIDTH = Math.min(Math.floor(WINDOW_WIDTH * 2 / 3), 750);

let vs = await views();
const buttons = find(vs, 'KoanSidebarNavButtons');
check('Single toolbar by default: navigation buttons in the sidebar top row',
      buttons?.shown && buttons.path.includes('VerticalTabStripTopContainer'),
      JSON.stringify(buttons && { shown: buttons.shown, parent: buttons.path.at(-1) }));
const pill = find(vs, 'KoanSidebarNavView');
check('...a URL pill below it', pill?.shown && pill.h > 0, `height ${pill?.h}`);
const top = find(vs, 'TopContainerView');
check('...and no top toolbar in the layout',
      top?.path.includes('TopContainerOverlayView'), top?.path.at(-1));

await key(VK.L, CTRL, 'l');
vs = await views();
let bar = find(vs, 'TopContainerView');
check('Ctrl+L floats the URL bar, centred and Zen-sized',
      bar?.w === FLOAT_WIDTH && bar.x === (WINDOW_WIDTH - FLOAT_WIDTH) / 2,
      `${bar?.w}x${bar?.h} at ${bar?.x},${bar?.y}`);
const toolbarChildren = vs.filter(v => v.path.at(-1) === 'ToolbarView' && v.shown);
check('...showing just the location bar',
      toolbarChildren.length === 1 && toolbarChildren[0].cls === 'LocationBarView',
      toolbarChildren.map(v => v.cls).join(' '));

const omnibox = find(vs, 'OmniboxViewViews');
await viewKey(omnibox, VK.ESC);
await viewKey(omnibox, VK.ESC);
vs = await views();
bar = find(vs, 'TopContainerView');
check('Esc closes the floating URL bar', bar?.w === WINDOW_WIDTH, `width ${bar?.w}`);
check('...and the toolbar is back to normal',
      vs.filter(v => v.path.at(-1) === 'ToolbarView' && v.shown).length > 1);

const pillButton = vs.find(v => v.path.at(-1) === 'KoanSidebarNavView' && v.cls === 'LabelButton');
await click(pillButton);
vs = await views();
bar = find(vs, 'TopContainerView');
check('Clicking the URL pill floats the URL bar', bar?.w === FLOAT_WIDTH, `width ${bar?.w}`);
await viewKey(find(vs, 'OmniboxViewViews'), VK.ESC);
await viewKey(find(vs, 'OmniboxViewViews'), VK.ESC);

await key(VK.B, CTRL | ALT, 'b');
vs = await views();
check('Ctrl+Alt+B switches to multiple toolbars',
      !find(vs, 'KoanSidebarNavView') && !find(vs, 'KoanSidebarNavButtons') &&
      find(vs, 'TopContainerView')?.path.at(-1) === 'BrowserView');
await key(VK.L, CTRL, 'l');
vs = await views();
bar = find(vs, 'TopContainerView');
// The toolbar sits beside the sidebar there, so it is narrower than the
// window, but it stays in place.
check('...where Ctrl+L uses the toolbar in place',
      bar?.path.at(-1) === 'BrowserView' && bar.w > FLOAT_WIDTH && bar.y === 0,
      `${bar?.w}x${bar?.h} at ${bar?.x},${bar?.y}`);
await key(VK.B, CTRL | ALT, 'b');
vs = await views();
check('Ctrl+Alt+B switches back', find(vs, 'KoanSidebarNavView')?.shown === true);

console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
