// Helpers for the folder e2e tests (folders.mjs, folders_drag.mjs): folder
// rows, menus and tabs through UI DevTools, on top of koan.mjs.
import { UI_PORT, browserCall, connect, pages, sleep } from './koan.mjs';

// A snapshot of the native view tree. Nodes get `parent` links.
export async function snapshot() {
  const c = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await c.send('DOM.enable');
  await c.send('CSS.enable');
  const doc = (await c.send('DOM.getDocument', { depth: -1 })).result.root;
  const byId = new Map();
  (function index(n, parent) {
    n.parent = parent;
    byId.set(n.nodeId, n);
    for (const child of n.children ?? []) index(child, n);
  })(doc, null);
  return { c, doc, byId };
}

export const cls = n => { const a = n.attributes ?? []; return a[a.indexOf('class') + 1]; };

export async function props(s, n) {
  const r = await s.c.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
  const p = {};
  for (const rule of r.result?.matchedCSSRules ?? []) {
    for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
  }
  return p;
}

export function findAll(s, re) {
  const out = [];
  (function walk(n) {
    if (n.nodeName === 'View' && re.test(cls(n) ?? '')) out.push(n);
    for (const c of n.children ?? []) walk(c);
  })(s.doc);
  return out;
}

// Whether `n` and all its view ancestors are visible.
export async function shown(s, n) {
  for (let v = n; v && v.nodeName === 'View'; v = v.parent) {
    if ((await props(s, v)).Visible === 'false') return false;
  }
  return true;
}

// `n`'s top-left in its root view's coordinates, and that root view.
export async function origin(s, n) {
  let x = 0, y = 0, v = n;
  // Only views: querying a widget or the document root can crash Koan.
  while (v && v.nodeName === 'View' && !/RootView$/.test(cls(v) ?? '')) {
    const p = await props(s, v);
    x += +p.X; y += +p.Y;
    v = v.parent;
  }
  if (!v || v.nodeName !== 'View') throw new Error(`no root view above ${cls(n)}`);
  return { root: v, x, y };
}

// A real click in `n`: mouse events to its root view, so targeting, context
// menus and menu controllers all run. Near the view's start (a menu item can
// be wider than its menu), like glance_split.mjs.
export async function click(s, n, button = 'left') {
  const { root, x, y } = await origin(s, n);
  const p = await props(s, n);
  const at = { x: Math.round(x + Math.min(p.Width / 2, 20)), y: Math.round(y + p.Height / 2) };
  for (const type of ['mousePressed', 'mouseReleased']) {
    await s.c.send('DOM.dispatchMouseEvent', {
      nodeId: root.nodeId, event: { type, ...at, button, wheelDirection: 'none' },
    });
  }
  await sleep(1000);
}

// Clicks the `nth` menu item tagged with element identifier `id`.
export async function clickMenuItem(id, nth = 0) {
  const s = await snapshot();
  const search = (await s.c.send('DOM.performSearch', { query: `id:${id}` })).result;
  let ok = false;
  if (search?.resultCount > nth) {
    const ids = (await s.c.send('DOM.getSearchResults', {
      searchId: search.searchId, fromIndex: 0, toIndex: search.resultCount })).result.nodeIds;
    const node = s.byId.get(ids[nth]);
    if (node) { await click(s, node); ok = true; }
  }
  if (!ok) {
    console.log(`  (menu item ${id}[${nth}] not found: search ${JSON.stringify(search)}, ` +
                `${findAll(s, /^MenuItemView$/).length} menu items, ` +
                `${findAll(s, /^MenuHostRootView$/).length} menus open)`);
  }
  s.c.close();
  await sleep(800);
  return ok;
}

export async function rows() {
  const s = await snapshot();
  const out = [];
  for (const n of findAll(s, /^KoanFolderRowView$/)) {
    const p = await props(s, n);
    const visible = (await shown(s, n)) && +p.Height > 1;
    out.push({ node: n, name: p.FolderName, collapsed: p.Collapsed === 'true',
               count: +p.TabCount, renaming: p.Renaming === 'true',
               x: +p.X, y: +p.Y, visible });
  }
  s.c.close();
  return out;
}

// Visible tab views in sidebar order, with their x and y in their container.
export async function visibleTabs() {
  const s = await snapshot();
  const out = [];
  for (const n of findAll(s, /^TabView$/)) {
    const p = await props(s, n);
    if (+p.Height > 1 && await shown(s, n)) out.push({ x: +p.X, y: +p.Y });
  }
  s.c.close();
  return out;
}

export async function withView(re, nth, fn) {
  const s = await snapshot();
  const n = findAll(s, re)[nth];
  const r = n ? await fn(s, n) : undefined;
  s.c.close();
  return n ? (r ?? true) : false;
}

// The first shown view matching `re`, then fn(s, view).
export async function withShown(re, fn) {
  const s = await snapshot();
  let found = null;
  for (const n of findAll(s, re)) {
    if (await shown(s, n)) { found = n; break; }
  }
  const r = found ? await fn(s, found) : undefined;
  s.c.close();
  return found ? (r ?? true) : false;
}

// Typing goes to the shown field (each folder row has a hidden one).
export async function typeInto(re, text) {
  return withShown(re, async (s, n) => {
    for (const ch of text) {
      await s.c.send('DOM.dispatchKeyEvent', { nodeId: n.nodeId,
        event: { type: 'keyPressed', keyCode: 0, code: 0, flags: 0, key: ch.codePointAt(0), isChar: true } });
    }
  });
}

export async function pressKey(re, keyCode) {
  return withShown(re, async (s, n) => {
    for (const type of ['keyPressed', 'keyReleased']) {
      await s.c.send('DOM.dispatchKeyEvent', { nodeId: n.nodeId,
        event: { type, keyCode, code: 0, flags: 0, key: 0, isChar: false } });
    }
  });
}

// Browser calls (CDP) give the browser a moment; activating a page.
export async function browserCdp(method, params) {
  const r = await browserCall(method, params);
  await sleep(800);
  return r;
}
export const activate = async t => browserCdp('Target.activateTarget', { targetId: t.id });

// Rows by name (the view tree's order is the order they were made in),
// shown ones first (another Space may have a folder of the same name).
export const rowNamed = (name, fn) => withView(/^KoanFolderRowView$/, 0, async (s) => {
  let hidden = null;
  for (const n of findAll(s, /^KoanFolderRowView$/)) {
    if ((await props(s, n)).FolderName !== name) continue;
    if (await shown(s, n)) return fn(s, n);
    hidden ??= n;
  }
  if (hidden) return fn(s, hidden);
});
export async function folderMenu(name, item) {
  await rowNamed(name, (s, n) => click(s, n, 'right'));
  return clickMenuItem(item);
}

export async function switcherLabels() {
  return withView(/^KoanWorkspaceButton$/, 0, async (s) => {
    const labels = [];
    for (const n of findAll(s, /^KoanWorkspaceButton$/)) {
      const p = await props(s, n);
      labels.push({ label: p.Label, name: p.TooltipText ?? p.Tooltip });
    }
    return labels;
  });
}
export async function switcher(match) {
  return withView(/^KoanWorkspaceButton$/, 0, async (s) => {
    for (const n of findAll(s, /^KoanWorkspaceButton$/)) {
      const p = await props(s, n);
      if (p.Label === match || (p.TooltipText ?? p.Tooltip) === match) {
        await click(s, n);
        return true;
      }
    }
    return false;
  });
}
// Shown tab views, top to bottom, with x in their container and their
// position in the root view.
export async function shownTabs(s) {
  const out = [];
  for (const n of findAll(s, /^TabView$/)) {
    const p = await props(s, n);
    if (+p.Height > 1 && await shown(s, n)) {
      const o = await origin(s, n);
      out.push({ node: n, x: +p.X, w: +p.Width, h: +p.Height, ax: o.x, ay: o.y, root: o.root });
    }
  }
  return out.sort((a, b) => a.ay - b.ay);
}
// Right-clicks the shown tab `pick` chooses (from shownTabs), then runs the
// tab menu's Move to Folder item `id` (nth).
export async function moveToFolder(pick, id, nth = 0) {
  await withView(/^TabView$/, 0, async (s) => {
    const t = pick(await shownTabs(s));
    if (t) await click(s, t.node, 'right');
  });
  await clickMenuItem('kKoanMoveToFolderItem');
  return clickMenuItem(id, nth);
}
export const loosest = ts => {
  const minX = Math.min(...ts.map(t => t.x));
  return ts.filter(t => t.x === minX);
};
// The pages of the shown Space's collapsed folders: activating one of them
// shows it under its folder.
export async function pagesInCollapsedFolders() {
  const counts = [];
  for (const page of await pages()) {
    await activate(page);
    await sleep(600);
    counts.push({ page, n: (await visibleTabs()).length });
  }
  const min = Math.min(...counts.map(c => c.n));
  const inFolders = counts.filter(c => c.n > min).map(c => c.page);
  const loose = counts.find(c => c.n === min)?.page;
  if (loose) await activate(loose);
  await sleep(600);
  return { inFolders, loose };
}
export async function pageKey(page, code, modifiers, key) {
  const c = await connect(page.webSocketDebuggerUrl);
  const base = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers, key };
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
  c.close();
  await sleep(1500);
}
