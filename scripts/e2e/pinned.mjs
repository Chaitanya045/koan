// End-to-end test of Zen's pinned tabs and Essentials against a real build:
// pinning to the Space (Ctrl+Shift+D), the pinned URL and the reset button
// ("Back to pinned url"), every close behaviour, the context menu (Add to /
// Remove from Essentials, Change Label…, Reset, Replace, Unload), pinned
// tabs surviving Close Other Tabs, Close Tabs Below and Back with no
// history, reopening a closed pinned tab, pinned tabs belonging to their
// Space while Essentials show in all, split views pinned and unpinned as one
// unit, and everything kept across restarts.
//
//   scripts/e2e/run.sh pinned
//
// The profile (prefs/pinned.json) turns off Zen's replace-newtab, so a new
// Space (Ctrl+Alt+N) opens with a tab.
import {
  browserCall, evalIn, settingsCall, tabMenuCommand, waitFor,
} from './lib/koan.mjs';

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

// UI DevTools session. Node ids are only valid within one session, so each
// step opens its own and reads the whole tree.
async function uiSession() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const nodes = [];
  const byId = new Map();
  // Every view as {cls, nodeId, p (properties), shown, path, parent, ax, ay};
  // ax/ay is the position within the view's widget.
  const walk = async (n, parent, shown, path, ox, oy) => {
    const a = n.attributes ?? [];
    const cls = a[a.indexOf('class') + 1];
    let node = null;
    if (n.nodeName === 'View' && cls) {
      const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
      const p = {};
      for (const rule of r.result?.matchedCSSRules ?? []) {
        for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
      }
      shown = shown && p.Visible !== 'false';
      node = { cls, nodeId: n.nodeId, p, shown, path, parent,
               ax: ox + +p.X, ay: oy + +p.Y, w: +p.Width, h: +p.Height };
      nodes.push(node);
      byId.set(n.nodeId, node);
      path = [...path, cls];
      ox = node.ax;
      oy = node.ay;
    } else if (n.nodeName === 'Widget') {
      parent = null;
      ox = 0;
      oy = 0;
    }
    for (const c of n.children ?? []) await walk(c, node ?? parent, shown, path, ox, oy);
  };
  await walk(doc, null, true, [], 0, 0);
  const find = async query => {
    const s = (await ui.send('DOM.performSearch', { query })).result;
    if (!s?.resultCount) return [];
    const r = await ui.send('DOM.getSearchResults',
                            { searchId: s.searchId, fromIndex: 0, toIndex: s.resultCount });
    return (r.result?.nodeIds ?? []).map(id => byId.get(id)).filter(Boolean);
  };
  const mouse = async (node, type, x, y, button = 'left') =>
    ui.send('DOM.dispatchMouseEvent',
            { nodeId: node.nodeId, event: { type, x, y, button, wheelDirection: 'none' } });
  const keyEvent = async (node, event) =>
    ui.send('DOM.dispatchKeyEvent', { nodeId: node.nodeId, event });
  return { ui, nodes, find, mouse, keyEvent, close: () => ui.close() };
}

// The tab rows in the order they show (top to bottom, then left to right
// for Essentials): views of split tabs come after the others in the tree.
async function rows() {
  const s = await uiSession();
  s.close();
  return s.nodes.filter(n => n.cls === 'KoanTabRow')
      .sort((a, b) => a.ay - b.ay || a.ax - b.ax);
}

// The tab row (KoanTabRow) whose pinned or current URL is `url`.
const rowOf = (list, url) => list.find(r => r.p.PinnedUrl === url);
// The tab view a row decorates and its title.
const tabViewOf = (s, row) => row.parent;
const titleOf = (s, tabView) =>
  s.nodes.find(n => n.parent === tabView && n.cls === 'TabTitle');

async function pages() {
  return (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .filter(t => t.type === 'page');
}

async function evaluate(page, expression) {
  const c = await connect(page.webSocketDebuggerUrl);
  const r = await c.send('Runtime.evaluate', { expression, returnByValue: true });
  c.close();
  return r.result?.result?.value;
}

// Pages not to touch: DevTools on an unloaded tab would wake it up.
const unloaded = new Set();

// The page shown in the active tab, by its visibility.
async function activePage() {
  for (const page of await pages()) {
    if (unloaded.has(page.url)) continue;
    if (await evaluate(page, 'document.visibilityState') === 'visible') return page;
  }
  return null;
}

async function navigate(page, url) {
  const c = await connect(page.webSocketDebuggerUrl);
  await c.send('Page.navigate', { url });
  c.close();
  await sleep(2500);
}

// Sends a key press to the active page; unhandled keys reach the browser's
// accelerators. Modifiers: Alt=1 Ctrl=2 Shift=8.
async function key(code, modifiers, keyName = '') {
  const page = await activePage() ?? (await pages())[0];
  const c = await connect(page.webSocketDebuggerUrl);
  const base = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers, key: keyName };
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
  c.close();
  await sleep(1500);
}

// Right-clicks `tabView` and clicks the menu item with element id `item`.
// Returns the item's view (null if the menu doesn't have it).
async function menuCommand(tabView, item, { click = true } = {}) {
  let s = await uiSession();
  const view = s.nodes.find(n => n.nodeId === tabView.nodeId) ??
      s.nodes.find(n => n.cls === 'TabView' && n.ax === tabView.ax && n.ay === tabView.ay);
  for (const type of ['mousePressed', 'mouseReleased']) {
    await s.mouse(view, type, 20, 10, 'right');
  }
  s.close();
  await sleep(1500);
  s = await uiSession();
  const [found] = await s.find(`id:${item}`);
  const menu = s.nodes.find(n => n.cls === 'MenuItemView')
  let root = found ?? menu;
  while (root?.parent) root = root.parent;
  if (found && click) {
    const x = found.ax + 20, y = found.ay + Math.floor(found.h / 2);
    await s.mouse(root, 'mousePressed', x, y);
    await s.mouse(root, 'mouseReleased', x, y);
  } else if (root) {
    // A click outside the menu closes it.
    await s.mouse(root, 'mousePressed', -20, -20);
    await s.mouse(root, 'mouseReleased', -20, -20);
  }
  s.close();
  await sleep(1500);
  return found ?? null;
}

let failures = 0;
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
}

const ALT = 1, CTRL = 2, SHIFT = 8;
const VK = { D: 68, W: 87, T: 84, N: 78, V: 86, LEFT: 37, ENTER: 13, PAGE_UP: 33 };

const pageByUrl = async url => (await pages()).find(p => p.url === url);
const isVisible = async page =>
  page && await evalIn(page, 'document.visibilityState') === 'visible';
const activate = async page => {
  await browserCall('Target.activateTarget', { targetId: page.id });
  await sleep(1500);
};
// Sends a key press to `page` (not "the active page").
async function keyTo(page, code, modifiers, keyName = '') {
  const c = await connect(page.webSocketDebuggerUrl);
  const base = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers, key: keyName };
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
  c.close();
  await sleep(1500);
}
const rowsWith = async url => (await rows()).filter(r => r.p.PinnedUrl === url);

// --- Pin the active tab to its Space (Ctrl+Shift+D). ---
const first = await activePage();
const PIN_URL = first.url;
const AWAY_URL = `${PIN_URL.split('#')[0]}${PIN_URL.includes('?') ? '&' : '?'}away`;
await key(VK.D, CTRL | SHIFT, 'd');
let list = await rows();
let pinned = rowOf(list, PIN_URL);
check('Ctrl+Shift+D pins the tab to its Space (not an Essential)',
      pinned?.p.PinnedInSpace === 'true' &&
      !pinned.path.includes('PinnedTabContainerView'),
      JSON.stringify(pinned && { url: pinned.p.PinnedUrl, path: pinned.path.slice(-3) }));
check('...and no reset button at its pinned URL', pinned && !pinned.shown);

// --- Navigating away shows the reset button. ---
await navigate(first, AWAY_URL);
list = await rows();
pinned = rowOf(list, PIN_URL);
check('Away from its pinned URL, the reset button shows',
      pinned?.p.AwayFromPinnedUrl === 'true' && pinned.shown && pinned.w > 0,
      JSON.stringify(pinned && { away: pinned.p.AwayFromPinnedUrl, shown: pinned.shown, w: pinned.w }));

// Hovering it shows "Back to pinned url" under the label.
let s = await uiSession();
pinned = rowOf(s.nodes.filter(n => n.cls === 'KoanTabRow'), PIN_URL);
await s.mouse(pinned, 'mouseMoved', 10, 10);
s.close();
await sleep(500);
s = await uiSession();
let sublabel = s.nodes.find(n => n.cls === 'KoanTabSublabel' && n.shown);
check('Hovering the reset button shows "Back to pinned url"',
      sublabel?.p.Text === 'Back to pinned url', JSON.stringify(sublabel?.p.Text));

// Clicking it goes back to the pinned URL.
pinned = rowOf(s.nodes.filter(n => n.cls === 'KoanTabRow'), PIN_URL);
await s.mouse(pinned, 'mousePressed', 10, 10);
await s.mouse(pinned, 'mouseReleased', 10, 10);
s.close();
await sleep(2500);
list = await rows();
pinned = rowOf(list, PIN_URL);
let urls = (await pages()).map(p => p.url);
check('Clicking it goes back to the pinned URL',
      urls.includes(PIN_URL) && !urls.includes(AWAY_URL) && !pinned?.shown,
      JSON.stringify(urls));

// --- Ctrl+W on a pinned tab: reset, unload and switch, not close. ---
await navigate((await pages()).find(p => p.url === PIN_URL), AWAY_URL);
const tabsBefore = (await rows()).length;
await key(VK.W, CTRL, 'w');
unloaded.add(PIN_URL);
unloaded.add(AWAY_URL);
await sleep(3000);
list = await rows();
pinned = rowOf(list, PIN_URL);
const active = await activePage();
check('Ctrl+W on a pinned tab keeps it', list.length === tabsBefore,
      `${list.length} of ${tabsBefore} tabs`);
check('...switches to another tab', active && active.url !== AWAY_URL &&
      active.url !== PIN_URL, active?.url);
check('...resets it to its pinned URL', pinned?.p.AwayFromPinnedUrl === 'false',
      JSON.stringify(pinned?.p.AwayFromPinnedUrl));
check('...and unloads it', pinned?.p.Discarded === 'true',
      JSON.stringify(pinned?.p.Discarded));

// --- The context menu: Add to Essentials with Zen's "n / 12" badge. ---
s = await uiSession();
const otherRow = s.nodes.filter(n => n.cls === 'KoanTabRow')
    .find(r => r.p.PinnedUrl === '');
const otherTab = otherRow && tabViewOf(s, otherRow);
s.close();
const added = otherTab &&
    await menuCommand(otherTab, 'kKoanAddToEssentialsMenuItem');
check('The menu of a normal tab has "Add to Essentials"', !!added);
list = await rows();
const essential = list.find(r => r.path.includes('PinnedTabContainerView'));
check('Add to Essentials moves the tab to the Essentials',
      !!essential && essential.p.PinnedUrl !== '',
      JSON.stringify(list.map(r => ({ url: r.p.PinnedUrl, path: r.path.slice(-2) }))));

// Ctrl+W on an Essential (the active tab) keeps it too, and switches to the
// pinned tab, which loads again.
const countBefore = list.length;
const essentialUrl = (await activePage())?.url;
await key(VK.W, CTRL, 'w');
unloaded.clear();
unloaded.add(essentialUrl);
await sleep(2000);
list = await rows();
check('Ctrl+W on an Essential keeps it', list.length === countBefore &&
      list.some(r => r.path.includes('PinnedTabContainerView')),
      `${list.length} of ${countBefore} tabs`);

// --- Change Label… edits the label in place. ---
s = await uiSession();
pinned = rowOf(s.nodes.filter(n => n.cls === 'KoanTabRow'), PIN_URL);
const pinnedTab = pinned && tabViewOf(s, pinned);
s.close();
const rename = pinnedTab && await menuCommand(pinnedTab, 'kKoanChangeLabelMenuItem');
check('The menu of a pinned tab has "Change Label…"', !!rename);
s = await uiSession();
const editor = s.nodes.find(n => n.cls === 'KoanTabLabelEditor');
if (editor) {
  for (const ch of 'Mail') {
    await s.keyEvent(editor, { type: 'keyPressed', keyCode: ch.charCodeAt(0), code: 0,
                               flags: 0, key: ch.charCodeAt(0), isChar: true });
  }
  for (const type of ['keyPressed', 'keyReleased']) {
    await s.keyEvent(editor, { type, keyCode: VK.ENTER, code: 0, flags: 0, key: 0, isChar: false });
  }
}
s.close();
await sleep(1500);
s = await uiSession();
pinned = rowOf(s.nodes.filter(n => n.cls === 'KoanTabRow'), PIN_URL);
const title = pinned && titleOf(s, tabViewOf(s, pinned));
s.close();
check('Change Label… renames the tab', !!editor && title?.p.Text === 'Mail',
      JSON.stringify(title?.p.Text));


// --- Menu: Reset Pinned Tab, Replace with Current URL, Unload. ---
let a = await pageByUrl(PIN_URL);
await navigate(a, AWAY_URL);
s = await uiSession();
let aTab = tabViewOf(s, rowOf(s.nodes.filter(n => n.cls === 'KoanTabRow'), PIN_URL));
s.close();
await tabMenuCommand(aTab, 'kKoanResetPinnedTabMenuItem');
await sleep(1500);
check('"Reset Pinned Tab" goes back to the pinned URL',
      !!await pageByUrl(PIN_URL) && !await pageByUrl(AWAY_URL),
      JSON.stringify((await pages()).map(p => p.url)));
await navigate(await pageByUrl(PIN_URL), AWAY_URL);
await tabMenuCommand(aTab, 'kKoanReplaceWithCurrentUrlMenuItem',
                     { via: 'kKoanEditPinnedUrlSubmenu' });
check('"Edit Pinned URL" > "Replace with Current URL" makes it the pinned URL',
      (await rowsWith(AWAY_URL)).length === 1, JSON.stringify((await rows()).map(r => r.p.PinnedUrl)));
await navigate(await pageByUrl(AWAY_URL), PIN_URL);
await tabMenuCommand(aTab, 'kKoanReplaceWithCurrentUrlMenuItem',
                     { via: 'kKoanEditPinnedUrlSubmenu' });
await tabMenuCommand(aTab, 'kKoanUnloadMenuItem');
pinned = rowOf(await rows(), PIN_URL);
check('"Unload" unloads it and switches away',
      pinned?.p.Discarded === 'true' && pinned.p.PinnedUrl === PIN_URL,
      JSON.stringify(pinned && { discarded: pinned.p.Discarded, url: pinned.p.PinnedUrl }));
await activate(await pageByUrl(PIN_URL));
unloaded.clear();

// --- Reset doesn't ignore the fragment (hash-routed apps). ---
const FRAG_URL = `${PIN_URL}#inbox`;
await navigate(await pageByUrl(PIN_URL), FRAG_URL);
await keyTo(await pageByUrl(FRAG_URL), VK.W, CTRL, 'w');
await sleep(2500);
pinned = rowOf(await rows(), PIN_URL);
check('Ctrl+W resets a pinned tab that only changed its fragment',
      pinned?.p.Url === PIN_URL && pinned.p.Discarded === 'true',
      JSON.stringify(pinned && { url: pinned.p.Url, discarded: pinned.p.Discarded }));
await activate(await pageByUrl(PIN_URL));

// --- Back with no history never closes a pinned tab. ---
const OPENED_URL = `${PIN_URL}?opened`;
await evalIn(await pageByUrl(PIN_URL), `window.open(${JSON.stringify(OPENED_URL)}); 1`, true);
await sleep(2500);
let c = await pageByUrl(OPENED_URL);
await keyTo(c, VK.D, CTRL | SHIFT, 'd');
check('A tab opened from a pinned tab can be pinned too',
      (await rowsWith(OPENED_URL))[0]?.p.PinnedInSpace === 'true');
await keyTo(c, VK.LEFT, ALT, 'ArrowLeft');
await sleep(1500);
check('Back with no history keeps a pinned tab open (no back-to-opener)',
      !!await pageByUrl(OPENED_URL), JSON.stringify((await pages()).map(p => p.url)));

// --- Close Other Tabs and Close Tabs Below leave pinned tabs. ---
for (const q of ['?d', '?e']) {
  await browserCall('Target.createTarget', { url: `${PIN_URL}${q}`, background: true });
}
await sleep(2500);
s = await uiSession();
const normal = s.nodes.filter(n => n.cls === 'KoanTabRow' && n.p.PinnedUrl === '' &&
                                   n.path.includes('UnpinnedTabContainerView'));
s.close();
await tabMenuCommand(tabViewOf(null, normal[0]), 'kKoanCloseOtherTabsMenuItem');
await sleep(1500);
list = await rows();
check('Close Other Tabs closes the other normal tabs only',
      list.filter(r => r.p.PinnedUrl === '').length === 1 &&
      (await rowsWith(PIN_URL)).length === 1 && (await rowsWith(OPENED_URL)).length === 1 &&
      list.some(r => r.path.includes('PinnedTabContainerView')),
      JSON.stringify(list.map(r => r.p.PinnedUrl)));
s = await uiSession();
aTab = tabViewOf(s, rowOf(s.nodes.filter(n => n.cls === 'KoanTabRow'), PIN_URL));
s.close();
await tabMenuCommand(aTab, 'kKoanCloseTabsBelowMenuItem');
await sleep(1500);
list = await rows();
check('Close Tabs Below on a pinned tab leaves the pinned tabs below it',
      list.filter(r => r.p.PinnedUrl === '').length === 0 &&
      (await rowsWith(OPENED_URL)).length === 1,
      JSON.stringify(list.map(r => r.p.PinnedUrl)));

// --- Each of Zen's close behaviours (Settings > Pinned Tabs). ---
const settingsId = (await browserCall('Target.createTarget',
    { url: 'chrome://koan-settings', newWindow: true })).result.targetId;
const settings = await waitFor(async () => (await pages()).find(p => p.id === settingsId));
await waitFor(() => evalIn(settings, `document.querySelectorAll('.category').length > 0`));
const setBehavior = async value => {
  await settingsCall(settings, 'setPref', 'koan.pinned_tabs.close_shortcut_behavior', value);
  await sleep(300);
};
const OPENED_AWAY = `${OPENED_URL}&away`;
// Ctrl+W on the pinned tab C away from its pinned URL; returns what follows.
async function closeAway(behavior) {
  await setBehavior(behavior);
  c = await pageByUrl(OPENED_URL) ?? await pageByUrl(OPENED_AWAY);
  await activate(c);
  await navigate(c, OPENED_AWAY);
  c = await pageByUrl(OPENED_AWAY);
  await keyTo(c, VK.W, CTRL, 'w');
  await sleep(2500);
  const row = (await rowsWith(OPENED_URL))[0];
  const discarded = row?.p.Discarded === 'true';
  // Never attach to an unloaded tab: that would load it.
  const active = !discarded &&
      await isVisible(await pageByUrl(row?.p.Url ?? OPENED_URL));
  return { url: row?.p.Url, discarded, active };
}
let r = await closeAway('reset');
check('"Reset URL": stays on the tab, at its pinned URL',
      r.url === OPENED_URL && r.active && !r.discarded, JSON.stringify(r));
r = await closeAway('switch');
check('"Switch to next tab": switches, the tab keeps its page',
      r.url === OPENED_AWAY && !r.active && !r.discarded, JSON.stringify(r));
r = await closeAway('reset-switch');
check('"Reset URL and switch": switches, the tab is back at its pinned URL',
      r.url === OPENED_URL && !r.active && !r.discarded, JSON.stringify(r));
r = await closeAway('unload-switch');
check('"Unload and switch": switches and unloads, keeping its page',
      r.url === OPENED_AWAY && !r.active && r.discarded, JSON.stringify(r));
r = await closeAway('close');
check('"Close tab": closes it', (await rowsWith(OPENED_URL)).length === 0,
      JSON.stringify((await rows()).map(t => t.p.PinnedUrl)));
await keyTo(await pageByUrl(PIN_URL), VK.T, CTRL | SHIFT, 't');
await sleep(1500);
check('...and Ctrl+Shift+T brings it back, still pinned',
      (await rowsWith(OPENED_URL))[0]?.p.PinnedInSpace === 'true',
      JSON.stringify((await rows()).map(t => t.p.PinnedUrl)));
await setBehavior('reset-unload-switch');
await browserCall('Target.closeTarget', { targetId: settingsId });
await sleep(1000);

// --- A split view is one unit: never half pinned, kept whole by every pin
// change (pin, unpin, Add to / Remove from Essentials). ---
const X = n => `${PIN_URL}?x${n}`;
// Only the pinned tabs in the Space first (the close behaviours above may
// have opened a new tab page), so x1, once pinned, is followed by x2.
for (const page of await pages()) {
  if (!page.url.startsWith('https://example.com')) {
    await browserCall('Target.closeTarget', { targetId: page.id });
  }
}
for (const n of [1, 2, 3, 4, 5]) {
  await browserCall('Target.createTarget', { url: X(n), background: true });
}
await sleep(2500);
// The row of the tab showing `url`, and the tab view it decorates.
const rowShowing = async url => (await rows()).find(r => r.p.Url === url);
const tabShowing = async url => ({ nodeId: (await rowShowing(url))?.parent?.nodeId });
// (One UI DevTools session at a time: it takes a single client.)
const statesOf = async urls => {
  const list = await rows();
  return urls.map(url => {
    const r = list.find(t => t.p.Url === url);
    return r && { pinned: r.p.PinnedInSpace === 'true',
                  essential: r.p.Essential === 'true', split: r.p.Split === 'true' };
  });
};
const allAre = (states, want) => states.every(st => st &&
    Object.entries(want).every(([k, v]) => st[k] === v));
// The Space's tabs in order (no Essentials): whether each is pinned. Pinned
// tabs must come first.
const pinnedRunOk = async () => {
  const flags = (await rows()).filter(r => r.path.includes('UnpinnedTabContainerView'))
      .map(r => r.p.PinnedInSpace === 'true');
  return flags.indexOf(false) === -1 || !flags.slice(flags.indexOf(false)).includes(true);
};
const sidebarOrder = async () => (await rows())
    .filter(r => r.path.includes('UnpinnedTabContainerView')).map(r => r.p.Url);

// Pin x1, then split it with the normal tab after it (Ctrl+Alt+V): both pinned.
await activate(await pageByUrl(X(1)));
await keyTo(await pageByUrl(X(1)), VK.D, CTRL | SHIFT, 'd');
await keyTo(await pageByUrl(X(1)), VK.V, CTRL | ALT, 'v');
await sleep(1000);
let st = await statesOf([X(1), X(2)]);
check('Splitting a pinned tab with a normal one pins the split view as one',
      allAre(st, { pinned: true, split: true }), JSON.stringify(st));
// Split x4 + x5 (normal tabs below x3) and pin them: they go right after the
// pinned split, which stays whole (upstream CHECKs a move between its tabs).
await activate(await pageByUrl(X(4)));
await keyTo(await pageByUrl(X(4)), VK.V, CTRL | ALT, 'v');
await keyTo(await pageByUrl(X(4)), VK.D, CTRL | SHIFT, 'd');
await sleep(1000);
st = await statesOf([X(1), X(2), X(4), X(5)]);
let order = (await sidebarOrder()).filter(u => u.includes('?x'));
check('Pinning a split view next to a pinned split keeps both whole, pinned in order',
      allAre(st, { pinned: true, split: true }) &&
      JSON.stringify(order) === JSON.stringify([X(1), X(2), X(4), X(5), X(3)]),
      JSON.stringify({ st, order }));
// Unpin x1's split: both leave the pinned tabs, together, below them.
await activate(await pageByUrl(X(1)));
await keyTo(await pageByUrl(X(1)), VK.D, CTRL | SHIFT, 'd');
await sleep(1000);
st = await statesOf([X(1), X(2)]);
order = (await sidebarOrder()).filter(u => u.includes('?x'));
check('Unpinning a split view unpins both and moves them below the pinned tabs',
      allAre(st, { pinned: false, split: true }) && await pinnedRunOk() &&
      JSON.stringify(order) === JSON.stringify([X(4), X(5), X(1), X(2), X(3)]),
      JSON.stringify({ st, order }));
// Add to Essentials / Remove from Essentials take the split along, whole.
await tabMenuCommand(await tabShowing(X(1)), 'kKoanAddToEssentialsMenuItem');
st = await statesOf([X(1), X(2)]);
check('"Add to Essentials" on a split view makes both Essentials, still split',
      allAre(st, { essential: true, split: true }), JSON.stringify(st));
await tabMenuCommand(await tabShowing(X(1)), 'kKoanRemoveFromEssentialsMenuItem');
st = await statesOf([X(1), X(2)]);
check('...and "Remove from Essentials" makes both normal tabs again, still split',
      allAre(st, { essential: false, pinned: false, split: true }) && await pinnedRunOk(),
      JSON.stringify({ st, order: await sidebarOrder() }));
for (const n of [1, 2, 3, 4, 5]) {
  const page = await pageByUrl(X(n));
  if (page) await browserCall('Target.closeTarget', { targetId: page.id });
}
await sleep(1500);

// --- A tab moved among the pinned tabs is pinned where it is, as Zen pins in
// place (Ctrl+Shift+PageUp moves it up past the last pinned tab). ---
const Y = n => `${PIN_URL}?y${n}`;
for (const n of [1, 2]) {
  await browserCall('Target.createTarget', { url: Y(n), background: true });
}
await sleep(2500);
await activate(await pageByUrl(Y(1)));
await keyTo(await pageByUrl(Y(1)), VK.D, CTRL | SHIFT, 'd');
await activate(await pageByUrl(Y(2)));
await keyTo(await pageByUrl(Y(2)), VK.PAGE_UP, CTRL | SHIFT, 'PageUp');
await sleep(1500);
st = await statesOf([Y(2), Y(1)]);
order = (await sidebarOrder()).filter(u => u.includes('?y'));
check('A tab moved among the pinned tabs is pinned in place',
      allAre(st, { pinned: true }) &&
      JSON.stringify(order) === JSON.stringify([Y(2), Y(1)]),
      JSON.stringify({ st, order }));
for (const n of [1, 2]) {
  const page = await pageByUrl(Y(n));
  if (page) await browserCall('Target.closeTarget', { targetId: page.id });
}
await sleep(1500);

// --- Pinned tabs belong to their Space; Essentials show in every Space. ---
const essentialRow = async () =>
  (await rows()).find(t => t.path.includes('PinnedTabContainerView'));
await keyTo(await pageByUrl(PIN_URL), VK.N, CTRL | ALT, 'n');  // Space 1, with a new tab.
let space = (await pages()).find(p => !p.url.startsWith('https://example.com'));
await navigate(space, `${PIN_URL}?space1`);
await keyTo(await pageByUrl(`${PIN_URL}?space1`), VK.D, CTRL | SHIFT, 'd');
// Whether the tab whose pinned URL is `url` shows in the sidebar.
const tabShown = async url => !!(await rowsWith(url))[0]?.parent?.shown;
const essentialShown = async () => !!(await essentialRow())?.parent?.shown;
check('A tab pinned in a Space shows there', await tabShown(`${PIN_URL}?space1`));
check('...tabs pinned outside it don\'t', !await tabShown(PIN_URL));
check('...and the Essential does', await essentialShown());
await keyTo(await pageByUrl(`${PIN_URL}?space1`), VK.N, CTRL | ALT, 'n');  // Space 2.
check('In another Space, the first Space\'s pinned tab is hidden',
      !await tabShown(`${PIN_URL}?space1`));
check('...and the Essential still shows', await essentialShown());
// Back to all tabs for the rest.
for (let i = 0; i < 2; i++) {
  await keyTo(await pageByUrl(`${PIN_URL}?space1`), VK.LEFT, CTRL | ALT);
}

// --- After a restart: pinned URL, label and Essentials are kept. ---
const { spawn } = await import('node:child_process');
const { readFileSync, writeFileSync } = await import('node:fs');
let browser = null;
process.on('exit', () => browser?.kill('SIGKILL'));
const cdpUp = async () => {
  try { await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`); return true; } catch { return false; }
};
async function quit() {
  const { webSocketDebuggerUrl } =
      await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
  const c = await connect(webSocketDebuggerUrl);
  c.send('Browser.close');
  for (let i = 0; i < 60 && await cdpUp(); i++) await sleep(500);
  c.close();
  await sleep(1000);
}
// run.sh's command line for Koan, without the start URLs (the session
// brings the tabs back).
const chromeArgs = (process.env.KOAN_CHROME_ARGS ?? '').split('\n').filter(Boolean);
const profileDir = chromeArgs.find(a => a.startsWith('--user-data-dir='))
    ?.slice('--user-data-dir='.length);
async function relaunch() {
  browser = spawn(chromeArgs[0], chromeArgs.slice(1), { stdio: 'ignore' });
  for (let i = 0; i < 60 && !await cdpUp(); i++) await sleep(500);
  await sleep(5000);
}

if (profileDir) {
  // A reset of an unloaded tab, done once it is shown, isn't done again on
  // the next start: unload the pinned tab away from its pinned URL, reset it
  // from the menu (it stays unloaded), select it (it loads the pinned URL).
  await activate(await pageByUrl(PIN_URL));
  await navigate(await pageByUrl(PIN_URL), AWAY_URL);
  await tabMenuCommand(await tabShowing(AWAY_URL), 'kKoanUnloadMenuItem');
  unloaded.add(AWAY_URL);
  await tabMenuCommand(await tabShowing(AWAY_URL), 'kKoanResetPinnedTabMenuItem');
  pinned = await rowShowing(AWAY_URL);
  check('"Reset Pinned Tab" on an unloaded tab leaves it unloaded',
        pinned?.p.Discarded === 'true', JSON.stringify(pinned?.p.Discarded));
  await activate(await pageByUrl(AWAY_URL));
  unloaded.clear();
  await sleep(1500);
  check('...and selecting it goes to the pinned URL', !!await rowShowing(PIN_URL),
        JSON.stringify((await rows()).map(t => t.p.Url)));

  // An Essential's duplicate is an Essential whose pinned URL is the page it
  // was duplicated on, kept across the restart while it is away from it.
  const ESSENTIAL_PINNED = (await essentialRow()).p.PinnedUrl;
  const DUP_URL = `${ESSENTIAL_PINNED}&dup`;
  await activate(await pageByUrl(ESSENTIAL_PINNED));
  await navigate(await pageByUrl(ESSENTIAL_PINNED), DUP_URL);
  const pagesBefore = new Set((await pages()).map(p => p.id));
  await tabMenuCommand(await tabShowing(DUP_URL), 'kKoanDuplicateMenuItem');
  await sleep(1500);
  const dup = (await pages()).find(p => !pagesBefore.has(p.id));
  const dupRow = (await rows()).filter(r => r.p.Essential === 'true' &&
                                            r.p.PinnedUrl === DUP_URL);
  check('Duplicating an Essential makes an Essential pinned on that page',
        !!dup && dupRow.length === 1, JSON.stringify((await rows()).map(r => r.p.PinnedUrl)));
  if (dup) await navigate(dup, `${DUP_URL}&away`);
  // The original back at its pinned URL for the checks below.
  await navigate(await pageByUrl(DUP_URL), ESSENTIAL_PINNED);
  await navigate((await pages()).find(p => p.url === PIN_URL), AWAY_URL);
  // The Essential away from its pinned URL too.
  const ESSENTIAL_URL = ESSENTIAL_PINNED;
  await activate(await pageByUrl(ESSENTIAL_URL));
  await navigate(await pageByUrl(ESSENTIAL_URL), `${ESSENTIAL_URL}&away`);
  // The window's own session data (koan_session_data.h): a random value the
  // test switch (pinned.flags) gave it, which only a restore brings back.
  const windowData = async () => (await rowsWith(PIN_URL))[0]?.p.WindowTestData;
  const WINDOW_DATA = await windowData();
  check('A window has its own session data', !!WINDOW_DATA, JSON.stringify(WINDOW_DATA));
  const before = (await rows()).map(t => `${t.p.PinnedUrl}|${t.p.Url}`).sort();
  await sleep(3000);  // The session is saved on a timer.
  await quit();
  await relaunch();
  const after = (await rows()).map(t => `${t.p.PinnedUrl}|${t.p.Url}`).sort();
  check('A restart brings back the same tabs, none twice',
        JSON.stringify(before) === JSON.stringify(after),
        `${JSON.stringify(before)} -> ${JSON.stringify(after)}`);
  const essentialWith = async url => (await rows()).find(
      t => t.path.includes('PinnedTabContainerView') && t.p.PinnedUrl === url);
  const essentialAfter = await essentialWith(ESSENTIAL_URL);
  check('...the Essential keeps its pinned URL while away from it',
        essentialAfter?.p.PinnedUrl === ESSENTIAL_URL &&
        essentialAfter.p.AwayFromPinnedUrl === 'true',
        JSON.stringify(essentialAfter && { pinned: essentialAfter.p.PinnedUrl,
                                           away: essentialAfter.p.AwayFromPinnedUrl }));
  s = await uiSession();
  pinned = rowOf(s.nodes.filter(n => n.cls === 'KoanTabRow'), PIN_URL);
  const restoredTitle = pinned && titleOf(s, tabViewOf(s, pinned));
  const restoredEssential = s.nodes.find(n => n.cls === 'KoanTabRow' &&
                                              n.path.includes('PinnedTabContainerView'));
  s.close();
  check('After a restart the pinned tab keeps its pinned URL, and its page ' +
        '(the reset done before isn\'t done again)',
        pinned?.p.PinnedInSpace === 'true' && pinned.p.AwayFromPinnedUrl === 'true' &&
        pinned.p.Url === AWAY_URL,
        JSON.stringify(pinned && { away: pinned.p.AwayFromPinnedUrl, url: pinned.p.Url,
                                   inSpace: pinned.p.PinnedInSpace }));
  const dupAfter = (await rows()).find(r => r.p.Url === `${DUP_URL}&away`);
  check('...and the Essential\'s duplicate keeps the pinned URL it got',
        dupAfter?.p.Essential === 'true' && dupAfter.p.PinnedUrl === DUP_URL,
        JSON.stringify(dupAfter && { pinned: dupAfter.p.PinnedUrl, essential: dupAfter.p.Essential }));
  check('...and its label', restoredTitle?.p.Text === 'Mail',
        JSON.stringify(restoredTitle?.p.Text));
  check('...and the Essential stays an Essential', !!restoredEssential,
        JSON.stringify(restoredEssential?.p.PinnedUrl));
  check('...and the window its session data', await windowData() === WINDOW_DATA,
        JSON.stringify([await windowData(), WINDOW_DATA]));

  // With restore-to-pinned-URL on, the pinned tab comes back at its pinned URL.
  await quit();
  const prefsFile = `${profileDir}/Default/Preferences`;
  const prefs = JSON.parse(readFileSync(prefsFile, 'utf8'));
  prefs.koan ??= {};
  prefs.koan.pinned_tabs = { ...prefs.koan.pinned_tabs, restore_to_pinned_url: true };
  writeFileSync(prefsFile, JSON.stringify(prefs));
  await relaunch();
  list = await rows();
  pinned = rowOf(list, PIN_URL);
  check('With restore_to_pinned_url, it comes back at its pinned URL',
        pinned?.p.AwayFromPinnedUrl === 'false' && pinned.p.Url === PIN_URL,
        JSON.stringify(pinned && { away: pinned.p.AwayFromPinnedUrl, url: pinned.p.Url }));
  const essentialRestored = await essentialWith(ESSENTIAL_URL);
  check('...and so does the Essential', essentialRestored?.p.Url === ESSENTIAL_URL,
        JSON.stringify(essentialRestored?.p.Url));
  check('After a second restart the window still has its session data',
        await windowData() === WINDOW_DATA, JSON.stringify([await windowData(), WINDOW_DATA]));

  // A closed window reopened with Ctrl+Shift+T gets its data back, and keeps
  // it across a restart. (Two tabs: a closed one-tab window comes back as a
  // tab.)
  const W_URL = n => `${PIN_URL}?window${n}`;
  const w1 = (await browserCall('Target.createTarget',
                                { url: W_URL(1), newWindow: true })).result.targetId;
  await sleep(2500);
  const wPage = (await pages()).find(p => p.id === w1);
  await evalIn(wPage, `window.open(${JSON.stringify(W_URL(2))}); 1`, true);
  await sleep(2500);
  const dataOf = async url => (await rows()).find(t => t.p.Url === url)?.p.WindowTestData;
  const OTHER_DATA = await dataOf(W_URL(1));
  check('A new window has session data of its own',
        !!OTHER_DATA && OTHER_DATA !== WINDOW_DATA && await dataOf(W_URL(2)) === OTHER_DATA,
        JSON.stringify([OTHER_DATA, await dataOf(W_URL(2)), WINDOW_DATA]));
  await keyTo(await pageByUrl(W_URL(2)), VK.W, CTRL | SHIFT, 'w');  // Close Window
  await sleep(2000);
  check('...it closes', !await pageByUrl(W_URL(1)) && !await pageByUrl(W_URL(2)),
        JSON.stringify((await pages()).map(p => p.url)));
  // From a loaded tab of the main window (a key to a tab that isn't loaded
  // yet goes nowhere).
  await activate(await pageByUrl(PIN_URL));
  await keyTo(await pageByUrl(PIN_URL), VK.T, CTRL | SHIFT, 't');
  await sleep(2500);
  check('...and Ctrl+Shift+T reopens it with its session data',
        await dataOf(W_URL(1)) === OTHER_DATA,
        JSON.stringify([await dataOf(W_URL(1)), OTHER_DATA,
                        (await pages()).map(p => p.url)]));
  await sleep(3000);  // The session is saved on a timer.
  await quit();
  await relaunch();
  check('...which it keeps across a restart', await dataOf(W_URL(1)) === OTHER_DATA &&
        await windowData() === WINDOW_DATA,
        JSON.stringify([await dataOf(W_URL(1)), OTHER_DATA, await windowData()]));
}

console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
