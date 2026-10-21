// End-to-end test of workspaces (Zen's Spaces) against a real build: one
// Space from the start, creating them (Ctrl+Alt+N), cycling with wrap-around
// (Ctrl+Alt+Left/Right) and new tabs landing in the active Space. The rest
// of Spaces is in spaces.mjs.
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

// Bottom bar Space buttons as {label, name, active} in order, the visible
// tabs, the Space header's name and the Create-a-Space form's name field.
async function sidebar() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const buttons = [];
  let visibleTabs = 0;
  let header = null;
  let nameField = null;
  const props = async node => {
    const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: node.nodeId });
    const p = {};
    for (const rule of r.result?.matchedCSSRules ?? []) {
      for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
    }
    return p;
  };
  // A view is shown only if it and all its ancestors are visible.
  const walk = async (n, shown, inForm) => {
    const a = n.attributes ?? [];
    const cls = a[a.indexOf('class') + 1];
    if (n.nodeName === 'View' && cls) {
      const p = await props(n);
      shown = shown && p.Visible !== 'false';
      if (!shown) return;
      if (cls === 'KoanSpaceButton') {
        buttons.push({ label: p.Label ?? '', name: p.TooltipText ?? '',
                       active: p.Active === 'true' });
      } else if (cls === 'TabView' && +p.Height > 1) {
        visibleTabs++;
      } else if (cls === 'KoanSpaceHeaderView' && +p.Height > 0) {
        header = p.Name;
      } else if (cls === 'Textfield' && inForm) {
        nameField = n.nodeId;
      }
      inForm = inForm || cls === 'KoanSpaceCreationView';
    }
    for (const c of n.children ?? []) await walk(c, shown, inForm);
  };
  await walk(doc, true, false);
  ui.close();
  return { buttons, visibleTabs, header, nameField };
}

// Types `text` into the view and presses Enter (UI DevTools).
async function typeAndEnter(nodeId, text) {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('DOM.getDocument', { depth: -1 });
  for (const ch of text) {
    await ui.send('DOM.dispatchKeyEvent', { nodeId, event: {
      type: 'keyPressed', keyCode: ch.codePointAt(0), code: 0, flags: 0,
      key: ch.codePointAt(0), isChar: true } });
  }
  await ui.send('DOM.dispatchKeyEvent', { nodeId, event: {
    type: 'keyPressed', keyCode: 13, code: 0, flags: 0, key: 0, isChar: false } });
  ui.close();
  await new Promise(r => setTimeout(r, 1500));
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
const VK = { N: 78, T: 84, LEFT: 37, RIGHT: 39, RETURN: 13 };

let s = await sidebar();
check('The window starts in one Space', s.header === 'Space' &&
      s.buttons.length === 0 && s.visibleTabs === 2,
      JSON.stringify({ header: s.header, buttons: s.buttons.length, tabs: s.visibleTabs }));

// Ctrl+Alt+N opens Zen's Create-a-Space form; Enter creates the Space.
await key(VK.N, CTRL | ALT, 'n');
s = await sidebar();
check('Ctrl+Alt+N opens the Create-a-Space form', s.nameField !== null);
await typeAndEnter(s.nameField, 'Work');
s = await sidebar();
check('Creating a Space shows it, with its tab only',
      s.header === 'Work' && s.visibleTabs === 1 && s.buttons.length === 2 &&
      s.buttons[1].active, JSON.stringify(s));

// Ctrl+T opens the URL bar (Zen's replace-newtab); the tab opens with what
// is opened from it.
await key(VK.T, CTRL, 't');
await openFromUrlBar('about:blank');
s = await sidebar();
check('A new tab joins the active Space', s.visibleTabs === 2, `${s.visibleTabs} tab(s)`);

await key(VK.N, CTRL | ALT, 'n');
await typeAndEnter((await sidebar()).nameField, 'Play');
s = await sidebar();
check('A third Space goes after the active one',
      s.buttons.map(b => b.name).join() === 'Space,Work,Play' && s.buttons[2].active,
      s.buttons.map(b => b.name).join());

await key(VK.LEFT, CTRL | ALT);
s = await sidebar();
check('Ctrl+Alt+Left goes to the previous Space',
      s.header === 'Work' && s.visibleTabs === 2, JSON.stringify(s.header));
await key(VK.RIGHT, CTRL | ALT);
await key(VK.RIGHT, CTRL | ALT);
s = await sidebar();
check('Ctrl+Alt+Right past the last Space wraps around to the first',
      s.header === 'Space' && s.visibleTabs === 2, JSON.stringify(s.header));

console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
