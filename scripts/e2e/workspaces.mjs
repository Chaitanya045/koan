// End-to-end test of workspaces against a real build: creating them
// (Ctrl+Alt+N), their icons in the switcher, cycling (Ctrl+Alt+Left/Right)
// and new tabs landing in the active workspace.
//
//   scripts/e2e/run.sh workspaces
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

// Switcher buttons as {label, tooltip} in order, plus visible tab views.
async function sidebar() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const buttons = [];
  let visibleTabs = 0;
  const props = async node => {
    const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: node.nodeId });
    const p = {};
    for (const rule of r.result?.matchedCSSRules ?? []) {
      for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
    }
    return p;
  };
  // A view is shown only if it and all its ancestors are visible (tabs of a
  // hidden workspace keep Visible=true inside their hidden group view).
  const walk = async (n, shown) => {
    const a = n.attributes ?? [];
    const cls = a[a.indexOf('class') + 1];
    let p = null;
    if (['KoanWorkspaceButton', 'TabView', 'TabGroupView'].includes(cls)) {
      p = await props(n);
      shown = shown && p.Visible !== 'false';
    }
    if (cls === 'KoanWorkspaceButton') {
      buttons.push({ label: p.Label, tooltip: p.TooltipText ?? p.Tooltip });
    } else if (cls === 'TabView' && shown && +p.Height > 1) {
      visibleTabs++;
    }
    for (const c of n.children ?? []) await walk(c, shown);
  };
  await walk(doc, true);
  ui.close();
  return { buttons, visibleTabs };
}

async function key(code, modifiers, keyName = '') {
  const page = (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .filter(t => t.type === 'page')[0];
  const c = await connect(page.webSocketDebuggerUrl);
  const base = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers, key: keyName };
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
  c.close();
  await sleep(1500);
}

// Types `text` into the (focused) omnibox and presses Enter, via UI DevTools.
async function openFromUrlBar(text) {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const findNode = n => {
    const a = n.attributes ?? [];
    if (a[a.indexOf('class') + 1] === 'OmniboxViewViews') return n;
    for (const c of n.children ?? []) {
      const f = findNode(c);
      if (f) return f;
    }
    return null;
  };
  const nodeId = findNode(doc).nodeId;
  const event = e => ui.send('DOM.dispatchKeyEvent',
                             { nodeId, event: { code: 0, flags: 0, ...e } });
  for (const ch of text) {
    await event({ type: 'keyPressed', keyCode: 0, key: ch.codePointAt(0), isChar: true });
  }
  await sleep(500);
  await event({ type: 'keyPressed', keyCode: 13, key: 0, isChar: false });
  await event({ type: 'keyReleased', keyCode: 13, key: 0, isChar: false });
  ui.close();
  await sleep(1500);
}

let failures = 0;
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
}

const CTRL = 2, ALT = 1;
const VK = { N: 78, T: 84, LEFT: 37, RIGHT: 39 };

let s = await sidebar();
check('Switcher shows All and New', s.buttons.length === 2 &&
      s.buttons[0].label === '∞' && s.buttons[1].label === '+',
      s.buttons.map(b => b.label).join(' '));

await key(VK.N, CTRL | ALT, 'n');
s = await sidebar();
const first = s.buttons[1];
check('Ctrl+Alt+N creates a workspace with an emoji icon',
      s.buttons.length === 3 && first?.label === '\u{1F30A}',
      JSON.stringify(first));
check('The tooltip shows the name without the icon',
      first?.tooltip === 'Space 1', JSON.stringify(first?.tooltip));
check('The new workspace shows only its tab', s.visibleTabs === 1, `${s.visibleTabs} tab(s)`);

// Ctrl+T opens the URL bar (Zen's replace-newtab); the tab opens with what
// is opened from it.
await key(VK.T, CTRL, 't');
await openFromUrlBar('about:blank');
s = await sidebar();
check('A new tab joins the active workspace', s.visibleTabs === 2, `${s.visibleTabs} tab(s)`);

await key(VK.N, CTRL | ALT, 'n');
s = await sidebar();
check('A second workspace gets the next icon', s.buttons[2]?.label === '\u{1F33F}',
      s.buttons.map(b => b.label).join(' '));

await key(VK.LEFT, CTRL | ALT);
s = await sidebar();
check('Ctrl+Alt+Left goes back to the first workspace', s.visibleTabs === 2,
      `${s.visibleTabs} tab(s)`);
await key(VK.LEFT, CTRL | ALT);
s = await sidebar();
check('Ctrl+Alt+Left past the first shows all tabs', s.visibleTabs === 5,
      `${s.visibleTabs} tab(s)`);

console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
