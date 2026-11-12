// Helpers for the folder e2e test (folders.mjs): folder
// rows, menus and tabs through UI DevTools, on top of koan.mjs. On macOS the
// tab and folder menus are native NSMenus (NATIVE_MENUS): they open with the
// same right-click and are read and clicked through Accessibility
// (lib/mac.mjs nativeMenu()).
import { NATIVE_MENUS, UI_PORT, browserCall, connect, pages, sendKey, sleep } from './koan.mjs';
import {
  KOAN_PID, closeNativeMenu, nativeMenuReport, pickNativeMenu, ptr, realInput, waitNativeMenu,
  windowOf,
} from './mac.mjs';

// Koan's pid, for the native menus (folders.mjs restarts Koan).
let menuPid = KOAN_PID;
export const setMenuPid = pid => { menuPid = pid; };

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
  const event = type => ({ nodeId: root.nodeId, event: { type, ...at, button, wheelDirection: 'none' } });
  if (button === 'right' && NATIVE_MENUS) return openNativeMenu(event, at);
  for (const type of ['mousePressed', 'mouseReleased']) {
    await s.c.send('DOM.dispatchMouseEvent', event(type));
  }
  await sleep(1000);
}

// macOS: the native menu the last right-click opened, {items, c, release},
// or null. With real input it is a real right-click at `at` on the screen
// (the window has no title bar: its bounds' origin is the root view's).
// Else a UI DevTools press, which is answered only once the menu closes (its
// tracking loop runs no Chromium tasks), so it isn't waited for; its release
// goes to the root view once the menu has closed, as the press and release
// do elsewhere.
let nativeOpen = null;
async function openNativeMenu(event, at) {
  if (nativeOpen) await endNativeMenu();
  let c = null;
  // (Real input is Koan's first pid's only: the gate checks that pid.)
  const bounds = realInput() && menuPid === KOAN_PID &&
      (await windowOf((await pages())[0]?.id).catch(() => null))?.bounds;
  const real = !!bounds && await ptr('rclick', bounds.left + at.x, bounds.top + at.y);
  if (!real) {
    c = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
    await c.send('DOM.getDocument', { depth: -1 });
    c.send('DOM.dispatchMouseEvent', event('mousePressed'));
  }
  const menu = await waitNativeMenu(true, menuPid);
  // After a real right-click the pointer leaves the sidebar again (resting on
  // a collapsed folder would open its search).
  const away = !c && [bounds.left + bounds.width - 60, bounds.top + bounds.height / 2];
  nativeOpen = { items: menu?.items ?? null, c, release: event('mouseReleased'), away };
  if (!menu) {
    console.log(`  (no native menu opened by ${real ? `a real right-click at ${
        Math.round(bounds.left + at.x)},${Math.round(bounds.top + at.y)}` : 'a UI DevTools press'}: ` +
                `${await nativeMenuReport(menuPid)})`);
    await endNativeMenu();
  }
}
async function endNativeMenu() {
  const open = nativeOpen;
  nativeOpen = null;
  if (!open) return;
  await closeNativeMenu(menuPid);
  if (open.c) {
    await open.c.send('DOM.dispatchMouseEvent', open.release);
    open.c.close();
  }
  if (open.away) await ptr('move', ...open.away.map(Math.round), 150);
  await sleep(500);
}

// The native menus' items by element identifier (as the Views menus elsewhere
// tag them, koan_folder_controller.cc): a title, or for the items of a list
// (folders, Spaces) their position in the submenu; `submenu` for the items
// that open one.
const NATIVE_ITEMS = {
  kKoanMoveToFolderItem: { submenu: 'Move to Folder' },
  kKoanTabFolderItem: { nth: true },
  kKoanTabNewFolderItem: { title: 'New Folder' },
  kKoanTabRemoveFromFolderItem: { title: 'Remove from Folder' },
  kKoanFolderRenameItem: { title: 'Rename Folder…' },
  kKoanFolderUnloadItem: { title: 'Unload All Tabs' },
  kKoanFolderNewSubfolderItem: { title: 'New Subfolder…' },
  kKoanFolderChangeSpaceItem: { submenu: 'Change Space' },
  kKoanFolderSpaceItem: { nth: true },
  kKoanFolderConvertItem: { title: 'Convert folder to Space' },
  kKoanFolderUnpackItem: { title: 'Unpack Folder' },
  kKoanFolderDeleteItem: { title: 'Delete Folder' },
};

// clickMenuItem() in an open native menu: a submenu item is checked and
// remembered, the next item is pressed through it.
async function clickNativeMenuItem(id, nth) {
  const spec = NATIVE_ITEMS[id];
  const open = nativeOpen;
  if (!open || !spec) {
    console.log(`  (menu item ${id}[${nth}]: ${open ? 'not a native menu item' : 'no native menu open'})`);
    await endNativeMenu();
    return false;
  }
  if (spec.submenu) {
    const ok = !!open.items?.some(t => t.trim() === spec.submenu);
    if (!ok) {
      console.log(`  (no "${spec.submenu}" in the native menu: ${JSON.stringify(open.items)})`);
      await endNativeMenu();
    }
    open.submenu = ok ? spec.submenu : null;
    return ok;
  }
  const path = [...(open.submenu ? [open.submenu] : []), spec.nth ? nth : spec.title];
  const r = await pickNativeMenu(path, menuPid);
  if (!r.ok) console.log(`  (native menu item ${id}[${nth}] not pressed: ${r.detail})`);
  await endNativeMenu();
  await sleep(300);
  return r.ok;
}

// Clicks the `nth` menu item tagged with element identifier `id`.
export async function clickMenuItem(id, nth = 0) {
  if (nativeOpen) return clickNativeMenuItem(id, nth);
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

// The bottom bar's Space buttons, left to right (hidden, so none, while
// there is one Space): name, and whether it's the shown Space.
export async function spaceButtons() {
  const s = await snapshot();
  const out = [];
  for (const n of findAll(s, /^KoanSpaceButton$/)) {
    if (!(await shown(s, n))) continue;
    const p = await props(s, n);
    out.push({ name: p.TooltipText ?? '', active: p.Active === 'true',
               x: (await origin(s, n)).x });
  }
  s.c.close();
  return out.sort((a, b) => a.x - b.x);
}
// Shows the Space named `name` (its bottom bar button).
export async function switcher(name) {
  return withView(/^KoanSpaceButton$/, 0, async (s) => {
    for (const n of findAll(s, /^KoanSpaceButton$/)) {
      if ((await props(s, n)).TooltipText === name && await shown(s, n)) {
        await click(s, n);
        return true;
      }
    }
    return false;
  });
}
// The bottom bar's "+": Zen's Create menu.
export async function openCreateMenu() {
  return withView(/^ImageButton$/, 0, async (s) => {
    for (const n of findAll(s, /^ImageButton$/)) {
      if ((await props(s, n)).TooltipText === 'Create New' && await shown(s, n)) {
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
// shows it under its folder. (Pages of other Spaces show those; skipped.)
export async function pagesInCollapsedFolders() {
  const shownSpace = async () => (await spaceButtons()).find(b => b.active)?.name;
  const space = await shownSpace();
  const counts = [];
  for (const page of await pages()) {
    await activate(page);
    await sleep(600);
    if (await shownSpace() !== space) continue;
    counts.push({ page, n: (await visibleTabs()).length });
  }
  const min = Math.min(...counts.map(c => c.n));
  const inFolders = counts.filter(c => c.n > min).map(c => c.page);
  const loose = counts.find(c => c.n === min)?.page;
  if (loose) await activate(loose);
  await sleep(600);
  return { inFolders, loose };
}
// A key press to `page` (Windows key code `code`; lib/koan.mjs sendKey()
// sends the Mac key code on macOS).
export async function pageKey(page, code, modifiers, key) {
  const c = await connect(page.webSocketDebuggerUrl);
  await sendKey(c, code, modifiers, key);
  c.close();
  await sleep(1500);
}
