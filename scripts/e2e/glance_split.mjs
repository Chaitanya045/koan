// End-to-end test of Zen's Glance and split view details against a real
// build: the card's size and buttons, an activation click in the Glance page
// replacing it, the link context menu's "Open Link in Glance" (also inside
// a Glance page) and "Split Link to New Tab", Esc while typing, Glance
// dropping on a tab switch, the split-view limit toast, the tab context
// menu's "Split Out Tab", and a Glance expanded while it grows.
//
//   scripts/e2e/run.sh glance_split
//
// Glance only opens http(s) links, so the test serves its own pages. Menu
// items are picked by position and checked by what they do: UI DevTools
// can't read their labels (MenuItemView paints its title). The labels are
// in the PR's screenshots.
//
// glance_split.flags dumps frames (KOAN_DUMP_DIR), to look at the page.
import http from 'node:http';

import { windowFrame } from './lib/koan.mjs';

const UI_PORT = process.env.UI_PORT;
const CDP_PORT = process.env.CDP_PORT;
const sleep = ms => new Promise(r => setTimeout(r, ms));

const link = (id, href, top) =>
  `<a id="${id}" href="${href}" style="position: absolute; left: 200px; ` +
  `top: ${top}px; display: block; width: 300px; height: 40px">${id}</a>`;
const PAGES = {
  '/a': link('glance', '/b', 300) + link('solid', '/green', 20),
  '/b': link('next', '/c', 200) + '<input id="field" style="position: absolute; top: 300px">',
  '/c': '<p>c</p>',
  '/green': '',
};
const server = http.createServer((req, res) => {
  res.writeHead(200, { 'content-type': 'text/html' });
  const background = req.url === '/green' ? '#00ff00' : 'white';
  res.end(`<!doctype html><body style="font: 20px sans-serif; margin: 0; ` +
          `height: 100vh; background: ${background}">${PAGES[req.url] ?? ''}</body>`);
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const BASE = `http://127.0.0.1:${server.address().port}`;

async function connect(url) {
  const ws = new WebSocket(url);
  let id = 0;
  const pending = new Map();
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  };
  await new Promise((resolve, reject) => {
    ws.onopen = resolve;
    ws.onerror = reject;
    setTimeout(() => reject(new Error(`no connection to ${url}`)), 5000);
  });
  const send = (method, params = {}) => new Promise(resolve => {
    pending.set(++id, resolve);
    ws.send(JSON.stringify({ id, method, params }));
    setTimeout(() => resolve({ timedOut: true }), 5000);
  });
  return { send, close: () => ws.close() };
}

const targets = async () =>
  (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json());
const tabs = async () => (await targets()).filter(t => t.type === 'page');

async function withPage(target, fn) {
  const c = await connect(target.webSocketDebuggerUrl);
  try { return await fn(c); } finally { c.close(); }
}

// Every native view as {cls, nodeId, shown, x, y, w, h, ax, ay, root}:
// x/y relative to the parent view, ax/ay relative to the widget's root view.
async function views() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const out = [];
  const walk = async (n, depth, parent) => {
    const a = n.attributes ?? [];
    const cls = a[a.indexOf('class') + 1];
    let self = parent;
    if (depth > 0 && n.nodeName === 'View' && cls) {
      const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
      const p = {};
      for (const rule of r.result?.matchedCSSRules ?? []) {
        for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
      }
      self = {
        cls, nodeId: n.nodeId, x: +p.X, y: +p.Y, w: +p.Width, h: +p.Height,
        shown: (parent ? parent.shown : true) && p.Visible !== 'false',
        ax: (parent ? parent.ax : 0) + (parent ? +p.X : 0),
        ay: (parent ? parent.ay : 0) + (parent ? +p.Y : 0),
        parent: parent?.nodeId ?? null,
      };
      self.root = parent ? parent.root : self;
      out.push(self);
    } else if (n.nodeName !== 'View') {
      self = null;  // A widget or window: its views start a new tree.
    }
    for (const c of n.children ?? []) await walk(c, depth + 1, self);
  };
  await walk(doc, 0, null);
  ui.close();
  return out;
}

const shown = (vs, cls) => vs.filter(v => v.cls === cls && v.shown && v.h > 0);
const inside = (vs, ancestorCls) => {
  const ids = new Set(vs.filter(v => v.cls === ancestorCls).map(v => v.nodeId));
  let grew = true;
  while (grew) {
    grew = false;
    for (const v of vs) {
      if (!ids.has(v.nodeId) && ids.has(v.parent)) { ids.add(v.nodeId); grew = true; }
    }
  }
  return vs.filter(v => ids.has(v.nodeId) && v.cls !== ancestorCls);
};

// Clicks inside `view` the way a real click arrives: at its root view, so
// the root view targets it (context menus, menu items).
async function clickView(view, button = 'left') {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('DOM.getDocument', { depth: -1 });
  const x = Math.round(view.ax + Math.min(view.w / 2, 20));
  const y = Math.round(view.ay + view.h / 2);
  for (const type of ['mousePressed', 'mouseReleased']) {
    await ui.send('DOM.dispatchMouseEvent', {
      nodeId: view.root.nodeId,
      event: { type, x, y, button, wheelDirection: 'none' },
    });
  }
  ui.close();
  await sleep(1200);
}

// The open menu's items, top to bottom.
async function menuItems() {
  const vs = await views();
  return shown(vs, 'MenuItemView').sort((a, b) => a.ay - b.ay);
}

// Clicks (or right-clicks) the element with `id` in `target`'s page.
async function clickLink(target, id, modifiers, button = 'left') {
  await withPage(target, async c => {
    const r = await c.send('Runtime.evaluate', {
      expression: `JSON.stringify(document.getElementById('${id}').getBoundingClientRect())`,
      returnByValue: true,
    });
    const box = JSON.parse(r.result.result.value);
    const base = {
      x: box.x + 10, y: box.y + 10, modifiers, button, clickCount: 1,
      buttons: button === 'right' ? 2 : 1,
    };
    await c.send('Input.dispatchMouseEvent', { ...base, type: 'mousePressed' });
    await c.send('Input.dispatchMouseEvent', { ...base, type: 'mouseReleased', buttons: 0 });
  });
  await sleep(2000);
}

async function keyTo(target, code, modifiers, key = '') {
  await withPage(target, async c => {
    const base = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers, key };
    await c.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
    await c.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
  });
  await sleep(1500);
}

let failures = 0;
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
}

const ALT = 1;
const glanceOpen = async () => shown(await views(), 'KoanGlanceView').length === 1;
const pageAt = async path => (await tabs()).find(t => t.url === `${BASE}${path}`);

// Start from one tab showing /a.
const [first, ...rest] = await tabs();
const browserWs = (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json())
    .webSocketDebuggerUrl;
const browser = await connect(browserWs);
for (const t of rest) await browser.send('Target.closeTarget', { targetId: t.id });
await withPage(first, c => c.send('Page.navigate', { url: `${BASE}/a` }));
await sleep(2000);
let tab = await pageAt('/a');
// The test's pages: its tabs, plus the Glance page while it is open (other
// page targets, like the omnibox popup's WebUI, come and go).
const pages = async () => (await tabs()).filter(t => t.url.startsWith(BASE)).length;
const urls = async () => (await tabs()).filter(t => t.url.startsWith(BASE))
    .map(t => t.url.replace(BASE, '')).join(' ');

// The card: 80% of the contents width, their full height, centred; Zen's
// button column 12px right of it, 27px down.
await clickLink(tab, 'glance', ALT);
let vs = await views();
const overlay = shown(vs, 'KoanGlanceView')[0];
const card = shown(inside(vs, 'KoanGlanceView'), 'WebView')[0];
const cardBox = vs.find(v => v.nodeId === card?.parent);
const buttons = shown(inside(vs, 'KoanGlanceView'), 'ImageButton');
check('Alt+click opens Glance', !!overlay && !!card);
if (overlay && cardBox) {
  check('The card is 80% wide, full height, centred',
        Math.abs(cardBox.w - overlay.w * 0.8) <= 1 && cardBox.h === overlay.h &&
            Math.abs(cardBox.x - (overlay.w - cardBox.w) / 2) <= 1,
        `card ${cardBox.w}x${cardBox.h}+${cardBox.x}, overlay ${overlay.w}x${overlay.h}`);
  const column = vs.find(v => v.nodeId === buttons[0]?.parent);
  check('Three buttons beside the card, Zen-sized',
        buttons.length === 3 && buttons.every(b => b.w === 34 && b.h === 34) &&
            column?.x === cardBox.x + cardBox.w + 12 && column?.y === 27,
        `${buttons.length} at ${column?.x},${column?.y}`);
}

// In compact mode the card sits over the contents area all the same.
await keyTo(tab, 83, 2, 's');  // Ctrl+S
await clickLink(tab, 'glance', ALT);
vs = await views();
{
  const compactOverlay = shown(vs, 'KoanGlanceView')[0];
  const compactCard = vs.find(v => v.nodeId ===
      shown(inside(vs, 'KoanGlanceView'), 'WebView')[0]?.parent);
  const page = shown(vs, 'ContentsWebView').find(v => v.w > 300);
  check('In compact mode Glance covers the page, its card 80% wide',
        !!compactOverlay && !!compactCard && !!page &&
            Math.abs(compactCard.w - compactOverlay.w * 0.8) <= 1 &&
            Math.abs(compactOverlay.w - page.w) <= 16,
        `overlay ${compactOverlay?.w}, card ${compactCard?.w}, page ${page?.w}`);
}
const compactGlance = (await targets()).find(t => t.url === `${BASE}/b`);
if (compactGlance) {
  await keyTo(compactGlance, 27, 0, 'Escape');
}
await keyTo(tab, 83, 2, 's');  // Ctrl+S: compact mode off
await clickLink(tab, 'glance', ALT);

// An activation click in the Glance page shows that link in the card.
const glancePage = (await targets()).find(t => t.url === `${BASE}/b`);
if (glancePage) {
  await clickLink(glancePage, 'next', ALT);
}
check('Alt+click in the Glance page replaces its page',
      (await glanceOpen()) && (await targets()).some(t => t.url === `${BASE}/c`) &&
          (await pages()) === 2,
      (await targets()).map(t => t.url.replace(BASE, '')).join(' '));
const replaced = (await targets()).find(t => t.url === `${BASE}/c`);
if (replaced) {
  await keyTo(replaced, 27, 0, 'Escape');
}
check('Esc closes Glance', !(await glanceOpen()));

// A link's context menu inside the Glance page: "Open Link in Glance" shows
// the link in the card (the page isn't a tab; the menu used to crash).
await clickLink(tab, 'glance', ALT);
let inGlance = (await targets()).find(t => t.url === `${BASE}/b`);
if (inGlance) {
  await clickLink(inGlance, 'next', 0, 'right');
  const items = await menuItems();
  const separators = shown(await views(), 'MenuSeparator').map(s => s.ay);
  const section = items.filter(i => !separators.length || i.ay < Math.min(...separators));
  if (section.at(-1)) {
    await clickView(section.at(-1));
  }
}
const shownInCard = (await targets()).find(t => t.url === `${BASE}/c`);
check('"Open Link in Glance" in the Glance page shows the link in the card',
      (await glanceOpen()) && !!shownInCard && (await pages()) === 2,
      `glance ${await glanceOpen()}, pages ${await urls()}`);
if (shownInCard) {
  await keyTo(shownInCard, 27, 0, 'Escape');
}

// The card's own menu while the card closes (the page calls
// window.close()): "Open Link in Glance" opens a new card for the tab, and
// closing that one is fine.
await clickLink(tab, 'glance', ALT);
inGlance = (await targets()).find(t => t.url === `${BASE}/b`);
if (inGlance) {
  await clickLink(inGlance, 'next', 0, 'right');
  const items = await menuItems();
  const separators = shown(await views(), 'MenuSeparator').map(s => s.ay);
  const section = items.filter(i => !separators.length || i.ay < Math.min(...separators));
  await withPage(inGlance, c => c.send('Runtime.evaluate', { expression: 'window.close()' }));
  if (section.at(-1)) {
    await clickView(section.at(-1));
  }
}
const reopened = (await targets()).find(t => t.url === `${BASE}/c`);
check('"Open Link in Glance" from a closing card opens a new card',
      (await glanceOpen()) && !!reopened, `pages ${await urls()}`);
if (reopened) {
  await keyTo(reopened, 27, 0, 'Escape');
}
check('...which closes with Esc', !(await glanceOpen()) && (await pages()) === 1,
      `pages ${await urls()}`);

// Popups without a user gesture are blocked in a Glance page, as in a tab.
await clickLink(tab, 'glance', ALT);
inGlance = (await targets()).find(t => t.url === `${BASE}/b`);
if (inGlance) {
  await withPage(inGlance, c => c.send('Runtime.evaluate',
      { expression: "window.open('/c'); window.open('/c', '_blank', 'popup')" }));
  await sleep(1500);
  check('A Glance page can\'t open popups without a gesture',
        !(await targets()).some(t => t.url === `${BASE}/c`), `pages ${await urls()}`);
  await keyTo(inGlance, 27, 0, 'Escape');
}

// Esc while typing in the Glance page asks first (Zen): the card stays until
// a second Esc.
await clickLink(tab, 'glance', ALT);
inGlance = (await targets()).find(t => t.url === `${BASE}/b`);
if (inGlance) {
  await withPage(inGlance, c => c.send('Runtime.evaluate',
      { expression: "document.getElementById('field').focus()" }));
  await sleep(500);
  await keyTo(inGlance, 27, 0, 'Escape');
  check('Esc while typing keeps Glance open', await glanceOpen());
  await keyTo(inGlance, 27, 0, 'Escape');
  check('...a second Esc closes it', !(await glanceOpen()));
}

// Glance belongs to its tab: switching tabs drops it.
const other = (await browser.send('Target.createTarget',
    { url: `${BASE}/c`, background: true })).result.targetId;
await sleep(1000);
await clickLink(tab, 'glance', ALT);
await browser.send('Target.activateTarget', { targetId: other });
await sleep(1000);
check('Switching tabs drops Glance', !(await glanceOpen()));
await browser.send('Target.closeTarget', { targetId: other });
await browser.send('Target.activateTarget', { targetId: tab.id });
await sleep(1000);

// Link context menu: "Open Link in Glance" ends the link section (as in
// Zen); "Split Link to New Tab" is upstream's split item, before "Open link
// in incognito window".
async function linkMenuItem(name) {
  await clickLink(tab, 'glance', 0, 'right');
  const items = await menuItems();
  const separators = shown(await views(), 'MenuSeparator').map(s => s.ay);
  const section = items.filter(i => !separators.length || i.ay < Math.min(...separators));
  console.log(`  link menu: ${items.length} item(s), ${section.length} in the link section`);
  const item = section.at(name === 'glance' ? -1 : -3);
  if (item) {
    await clickView(item);
  }
  return !!item;
}
check('The link context menu opens', await linkMenuItem('glance'));
const fromMenu = (await targets()).find(t => t.url === `${BASE}/b`);
check('"Open Link in Glance" opens the link in Glance',
      (await glanceOpen()) && !!fromMenu && (await pages()) === 2,
      `glance ${await glanceOpen()}, pages ${await urls()}`);
if (fromMenu) {
  await keyTo(fromMenu, 27, 0, 'Escape');
}

await linkMenuItem('split');
vs = await views();
check('"Split Link to New Tab" splits the tab with the link',
      shown(vs, 'SplitTabView').length === 1 && (await pages()) === 2,
      `${shown(vs, 'SplitTabView').length} split(s), pages ${await urls()}`);

// Glance over a split: "Split Glance" would be a third panel, so Zen's limit
// toast shows and the page opens as a tab instead.
tab = await pageAt('/b');
if (tab) {
  await clickLink(tab, 'next', ALT);
  vs = await views();
  const split = shown(inside(vs, 'KoanGlanceView'), 'ImageButton')[2];
  if (split) {
    await clickView(split);
  }
  vs = await views();
  check('Split Glance over a split shows the limit toast and opens a tab',
        !(await glanceOpen()) && shown(vs, 'KoanToastView').length === 1 &&
            shown(vs, 'SplitTabView').length === 1 &&
            (await pages()) === 3,
      `glance ${await glanceOpen()}, toast ${shown(vs, 'KoanToastView').length}, ` +
          `pages ${await urls()}`);
}

// Tab context menu of a split tab: "Split Out Tab" (after "New tab below")
// separates the split.
vs = await views();
const wasSplit = shown(vs, 'SplitTabView').length === 1;
const splitTab = inside(vs, 'SplitTabView').find(v => v.cls === 'TabView' && v.shown);
if (splitTab) {
  await clickView(splitTab, 'right');
  const items = await menuItems();
  console.log(`  tab menu: ${items.length} item(s)`);
  if (items[1]) {
    await clickView(items[1]);
  }
}
check('"Split Out Tab" separates the split',
      wasSplit && shown(await views(), 'SplitTabView').length === 0);

// Expand a Glance while it still grows: the page in its new tab fills the
// tab, also after the window is resized (the grow animation's transform
// used to stay on the page).
for (const t of (await tabs()).filter(t => t.url === `${BASE}/a`).slice(0, 1)) {
  await browser.send('Target.activateTarget', { targetId: t.id });
  await sleep(1000);
  await withPage(t, async c => {
    const r = await c.send('Runtime.evaluate', {
      expression: `JSON.stringify(document.getElementById('solid').getBoundingClientRect())`,
      returnByValue: true,
    });
    const box = JSON.parse(r.result.result.value);
    const base = { x: box.x + 10, y: box.y + 10, modifiers: ALT, button: 'left',
                   clickCount: 1, buttons: 1 };
    await c.send('Input.dispatchMouseEvent', { ...base, type: 'mousePressed' });
    await c.send('Input.dispatchMouseEvent', { ...base, type: 'mouseReleased', buttons: 0 });
    // Ctrl+O at once, from the tab (the overlay is still growing).
    const key = { windowsVirtualKeyCode: 79, nativeVirtualKeyCode: 79, modifiers: 2, key: 'o' };
    await c.send('Input.dispatchKeyEvent', { ...key, type: 'rawKeyDown' });
    await c.send('Input.dispatchKeyEvent', { ...key, type: 'keyUp' });
  });
}
await sleep(1500);
const green = (await tabs()).find(t => t.url === `${BASE}/green`);
const { windowId } = (await browser.send('Browser.getWindowForTarget',
    { targetId: green?.id })).result ?? {};
if (windowId !== undefined) {
  await browser.send('Browser.setWindowBounds',
      { windowId, bounds: { width: 1180, height: 760 } });
}
await sleep(2000);
vs = await views();
const page = shown(vs, 'ContentsWebView').find(v => v.w > 300);
let samples = [];
if (page && process.env.KOAN_DUMP_DIR) {
  const frame = windowFrame();
  samples = [[0.5, 0.5], [0.9, 0.9], [0.1, 0.9]].map(([fx, fy]) =>
      frame.pixel(Math.round(page.ax + page.w * fx), Math.round(page.ay + page.h * fy)));
}
check('A Glance expanded while it grows fills its tab after a resize',
      !(await glanceOpen()) && !!green && samples.length === 3 &&
          samples.every(([r, g, b]) => r < 40 && g > 215 && b < 40),
      `page ${page?.w}x${page?.h}, pixels ${JSON.stringify(samples)}`);

browser.close();
server.close();
console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
