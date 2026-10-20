// End-to-end test of Zen's sidebar tab list: the "+ New Tab" row above the
// unpinned tabs (opening tabs at the top; no "+" button at the bottom), the
// separator with Zen's Clear button that closes the unpinned tabs (keeping
// the selected one and the workspace, undone with Ctrl+Shift+T), the "Tabs
// Closed!" toast, Zen's tab row and Essentials tile sizes, the collapsed
// sidebar, and switching to horizontal tabs and back.
//
//   scripts/e2e/run.sh sidebar_tabs
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
// includes the ancestors' visibility, x/y are in the window, `path` is the
// ancestor class names.
async function views() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const out = [];
  const walk = async (n, depth, shown, path, ox, oy) => {
    const a = n.attributes ?? [];
    const cls = a[a.indexOf('class') + 1];
    if (depth > 0 && n.nodeName === 'View' && cls) {
      const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
      const p = {};
      for (const rule of r.result?.matchedCSSRules ?? []) {
        for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
      }
      shown = shown && p.Visible !== 'false';
      ox += +p.X;
      oy += +p.Y;
      out.push({ cls, nodeId: n.nodeId, shown, x: ox, y: oy, w: +p.Width, h: +p.Height, path });
      path = [...path, cls];
    }
    for (const c of n.children ?? []) await walk(c, depth + 1, shown, path, ox, oy);
  };
  await walk(doc, 0, true, [], 0, 0);
  ui.close();
  return out;
}

const find = (vs, cls) => vs.find(v => v.cls === cls);

// The size of the first view of class `cls`, without walking every view's
// styles (the toast only lives for two seconds).
async function sizeOf(cls) {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  let node = null;
  const walk = n => {
    const a = n.attributes ?? [];
    if (!node && a[a.indexOf('class') + 1] === cls) node = n;
    (n.children ?? []).forEach(walk);
  };
  walk(doc);
  let size = null;
  if (node) {
    const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: node.nodeId });
    const p = {};
    for (const rule of r.result?.matchedCSSRules ?? []) {
      for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
    }
    size = { w: +p.Width, h: +p.Height, visible: p.Visible !== 'false' };
  }
  ui.close();
  return size;
}

// Sends native mouse events to a view (UI DevTools).
async function mouse(view, types) {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('DOM.getDocument', { depth: -1 });
  for (const type of types) {
    const pressing = type === 'mousePressed' || type === 'mouseReleased';
    await ui.send('DOM.dispatchMouseEvent', {
      nodeId: view.nodeId,
      event: { type, x: 10, y: 10, button: pressing ? 'left' : 'none',
               wheelDirection: 'none' },
    });
  }
  ui.close();
  await sleep(1000);
}
const click = view => mouse(view, ['mousePressed', 'mouseReleased']);

// A view's properties (UI DevTools), e.g. a Label's Text.
async function props(view) {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  await ui.send('DOM.getDocument', { depth: -1 });
  const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: view.nodeId });
  ui.close();
  const p = {};
  for (const rule of r.result?.matchedCSSRules ?? []) {
    for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
  }
  return p;
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

// Sets a browser pref through chrome://settings (settingsPrivate).
async function setPref(name, value) {
  const version = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
  const browser = await connect(version.webSocketDebuggerUrl);
  const { targetId } = (await browser.send('Target.createTarget',
                                           { url: 'chrome://settings' })).result;
  await sleep(2000);
  const target = (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .find(t => t.id === targetId);
  const page = await connect(target.webSocketDebuggerUrl);
  const r = await page.send('Runtime.evaluate', {
    expression: `new Promise(done => chrome.settingsPrivate.setPref(
        ${JSON.stringify(name)}, ${JSON.stringify(value)}, '', done))`,
    awaitPromise: true,
  });
  page.close();
  await browser.send('Target.closeTarget', { targetId });
  browser.close();
  await sleep(2000);
  return r.result?.result?.value;
}

const pages = async () =>
  (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .filter(t => t.type === 'page').length;

let failures = 0;
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
}

const ALT = 1, CTRL = 2, SHIFT = 8;
const VK = { D: 68, N: 78, T: 84, LEFT: 37 };
// The views inside the pinned (Essentials) or the unpinned tab container.
const inContainer = (vs, cls, container) =>
  vs.filter(v => v.cls === cls && v.shown && v.path.includes(container));
const workspaceButtons = vs => vs.filter(v => v.cls === 'KoanWorkspaceButton' && v.shown);

// Two tabs with the local test page, nothing pinned.
{
  const [first] = (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .filter(t => t.type === 'page');
  const version = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
  const browser = await connect(version.webSocketDebuggerUrl);
  await browser.send('Target.createTarget', { url: `${first.url}#two` });
  browser.close();
  await sleep(1500);
}
let vs = await views();
const rows = find(vs, 'KoanSidebarTabRowsView');
check('The rows sit in the tab strip',
      rows?.shown && rows.path.at(-1) === 'TabStripView' && rows.h > 0,
      JSON.stringify(rows && { shown: rows.shown, parent: rows.path.at(-1), h: rows.h }));
const bottomPlus = find(vs, 'NewTabButton');
check('...and no "+" button at the bottom of the sidebar',
      bottomPlus && !bottomPlus.shown,
      JSON.stringify(bottomPlus && { shown: bottomPlus.shown }));
let clear = find(vs, 'KoanClearTabsRow');
let newTab = find(vs, 'KoanNewTabRow');
let clearButton = find(vs, 'KoanClearButton');
check('The separator (22px) with Clear at its end, above the New Tab row (36px)',
      clear?.shown && clear.h === 22 && newTab?.shown && newTab.h === 36 &&
      clear.y + clear.h === newTab.y && clearButton?.shown && clearButton.w > 0 &&
      clearButton.x + clearButton.w === clear.x + clear.w,
      JSON.stringify({ clear: clear && [clear.y, clear.h], newTab: newTab && [newTab.y, newTab.h] }));
const tabs = inContainer(vs, 'TabView', 'UnpinnedTabContainerView');
check('Tab rows are 36px tall', tabs.length === 2 && tabs.every(t => t.h === 36),
      tabs.map(t => t.h).join(','));
check('...below the New Tab row', tabs[0]?.y >= newTab?.y + newTab?.h,
      `first tab y ${tabs[0]?.y}, row bottom ${newTab?.y + newTab?.h}`);
const closeButtons = vs.filter(v => v.cls === 'TabCloseButton' &&
                                   v.path.includes('UnpinnedTabContainerView'));
check('No close button until a tab is hovered, the active one included',
      closeButtons.length === 2 && closeButtons.every(b => !b.shown || b.w === 0),
      closeButtons.map(b => `${b.shown}/${b.w}`).join(','));

// Clear shows as a line until the pointer is over the sidebar or the
// button. (Synthetic events reach the button but not the sidebar's own
// enter/exit, so only the button's hover is driven here.)
check('Clear is a line until hovered', (await props(clearButton)).Revealed === 'false');
await mouse(clearButton, ['mouseEntered']);
check('...and shows while hovered', (await props(clearButton)).Revealed === 'true');
await mouse(clearButton, ['mouseExited']);
check('...and is a line again after', (await props(clearButton)).Revealed === 'false');

let before = await pages();
await click(newTab);
vs = await views();
// The test pages are titled "Sidebar tabs"; the new tab isn't. (Views are
// listed in creation order; sort them as they show, top to bottom.)
const titles = [];
for (const v of vs.filter(v => v.cls === 'TabTitle' && v.shown &&
                         v.path.includes('UnpinnedTabContainerView'))
                  .sort((a, b) => a.y - b.y)) {
  titles.push((await props(v)).Text);
}
check('The New Tab row opens a tab at the top, like Zen',
      (await pages()) === before + 1 && titles.length === 3 &&
      titles[0] !== 'Sidebar tabs' && titles.slice(1).every(t => t === 'Sidebar tabs'),
      `${before} -> ${await pages()} page(s), titles ${JSON.stringify(titles)}`);

// Pin the new tab (Ctrl+Shift+D, Zen's shortcut): an Essentials tile.
await key(VK.D, CTRL | SHIFT, 'd');
vs = await views();
const tiles = inContainer(vs, 'TabView', 'PinnedTabContainerView');
check('Pinned tabs are 46px Essentials tiles', tiles.length === 1 && tiles[0].h === 46,
      tiles.map(t => `${t.w}x${t.h}`).join(','));
clear = find(vs, 'KoanClearTabsRow');
check('...and the separator sits below them',
      clear?.shown && tiles[0] && clear.y >= tiles[0].y + tiles[0].h,
      `tile bottom ${tiles[0]?.y + tiles[0]?.h}, separator ${clear?.y}`);

// Clear: the selected tab is the pinned one, so both test page tabs go.
await click(find(vs, 'KoanClearButton'));
const toast = await sizeOf('KoanToastView');
check('Clear closes the unpinned tabs', (await pages()) === 1, `${await pages()} page(s)`);
check('...and toasts "Tabs Closed!"', toast?.visible && toast.h === 42 && toast.w > 0,
      JSON.stringify(toast));
vs = await views();
clear = find(vs, 'KoanClearTabsRow');
check('No separator without unpinned tabs', clear && !clear.shown,
      JSON.stringify(clear && { shown: clear.shown }));
await key(VK.T, CTRL | SHIFT, 't');
check('Ctrl+Shift+T brings a closed tab back', (await pages()) === 2,
      `${await pages()} page(s)`);

// Two new tabs; Clear keeps the selected one, like Zen.
await click(find(await views(), 'KoanNewTabRow'));
await click(find(await views(), 'KoanNewTabRow'));
before = await pages();
await click(find(await views(), 'KoanClearButton'));
check('Clear keeps the selected tab', before === 4 && (await pages()) === 2,
      `${before} -> ${await pages()}`);

// Collapsed, upstream's separator is the only line.
const collapse = (await views()).find(v => v.cls === 'TopContainerButton' && v.shown &&
                                           v.path.includes('VerticalTabStripTopContainer'));
await click(collapse);
await sleep(1000);
vs = await views();
clear = find(vs, 'KoanClearTabsRow');
check('Collapsed: no Clear row (upstream draws the line)', clear && !clear.shown,
      JSON.stringify(clear && { shown: clear.shown }));
await click(collapse);
await sleep(1000);
check('Expanded again: the Clear row is back', find(await views(), 'KoanClearTabsRow')?.shown);

// Horizontal tabs and back: the rows go with the vertical tab strip and come
// back with it; tab changes in between are fine.
check('Switching to horizontal tabs', (await setPref('vertical_tabs.enabled', false)) === true);
vs = await views();
check('...removes the rows', !find(vs, 'KoanSidebarTabRowsView')?.shown);
before = await pages();
await key(VK.T, CTRL, 't');
check('...and tabs still open', (await pages()) === before + 1, `${before} -> ${await pages()}`);
check('...and back', (await setPref('vertical_tabs.enabled', true)) === true);
vs = await views();
check('Back to vertical tabs: the rows are back',
      find(vs, 'KoanSidebarTabRowsView')?.shown && find(vs, 'KoanNewTabRow')?.shown &&
      !find(vs, 'NewTabButton')?.shown);

// In a workspace with an Essential selected, Clear empties the workspace but
// keeps it, with a fresh tab.
await key(VK.N, CTRL | ALT, 'n');
await click(find(await views(), 'KoanNewTabRow'));
vs = await views();
const spaces = workspaceButtons(vs).length;
await click(inContainer(vs, 'TabView', 'PinnedTabContainerView')[0]);
await click(find(await views(), 'KoanClearButton'));
vs = await views();
const left = inContainer(vs, 'TabView', 'UnpinnedTabContainerView');
check('Clear in a workspace keeps the workspace',
      workspaceButtons(vs).length === spaces && left.length === 1,
      `${spaces} -> ${workspaceButtons(vs).length} switcher buttons, ${left.length} tab(s) left`);

// In the "all tabs" view, Clear leaves the workspaces' tabs alone.
await key(VK.LEFT, CTRL | ALT);
const button = find(await views(), 'KoanClearButton');
if (button?.shown) {
  await click(button);
}
vs = await views();
check('Clear in the all-tabs view keeps every workspace',
      workspaceButtons(vs).length === spaces,
      `${spaces} -> ${workspaceButtons(vs).length} switcher buttons`);

console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
