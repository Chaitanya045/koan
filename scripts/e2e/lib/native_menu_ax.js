// The open native (NSMenu) context menu of a process, through Accessibility
// (System Events), for lib/mac.mjs nativeMenu(). JavaScript for Automation:
//
//   osascript -l JavaScript native_menu_ax.js '{"pid": 123, "op": "read"}'
//
// ops (each prints a JSON object):
//   probe   {ok}: whether System Events can read the process (its menu bar).
//   read    {open, items}: the open context menu's item titles (separators
//           are ''), or {open: false}.
//   click   {clicked} after pressing the item at `path`, a list of titles
//           (trimmed) or positions (separators count) from the top menu down
//           through submenus; else {missing, items} or {disabled}.
//   cancel  {cancelled}: closes the open menu (AXCancel).
//   dump    {app, focused, windows, hit}: the roles Accessibility sees (the
//           application's children, the focused element's, each window's
//           down to MENU_DEPTH, and what a hit test finds over the process's
//           menu-level windows), for failure details when no menu is found.
// `hit: false` in the argument turns the hit test below off.
// While an NSMenu is open its event-tracking loop runs no Chromium tasks, so
// CDP and UI DevTools can't answer; AppKit still answers Accessibility.
//
// Where the menu is: Chromium pops its context menus up for the window's
// content view (ui::ShowContextMenu(), [NSMenu popUpContextMenu:withEvent:
// forView:]), and AppKit makes the open menu an Accessibility child of that
// view; but Chromium's content view (BridgedContentView
// -accessibilityChildren) lists only the Views root, so no walk down from
// the application or its windows reaches the menu (the 2026-11-19 macOS run:
// the application's children AXWindow, AXWindow, AXMenuBar, no AXMenu, while
// the browser was held by the menu). So the menu is also found the way a
// pointer finds it: a hit test (AXUIElementCopyElementAtPosition) over each
// on-screen window of the process above the normal window layer (the menu's
// own window, kCGPopUpMenuWindowLevel), up through AXParent to the outermost
// AXMenu. That one is driven through the raw AXUIElement API (rawAx()).

// The raw Accessibility API (HIServices), for a menu System Events can't
// name: elements are AXUIElementRefs (CF types, which the bridge hands out
// as objects).
function rawAx(pid) {
  ObjC.import('ApplicationServices');
  ObjC.import('CoreGraphics');
  const obj = v => { try { return ObjC.castRefToObject(v); } catch (x) { return v; } };
  const call = (f, ...args) => { try { return f(...args); } catch (x) { return -1; } };
  const copy = (e, attr) => {
    if (!e) return null;
    const ref = Ref();
    let err = call($.AXUIElementCopyAttributeValue, e, attr, ref);
    if (err === -1) err = call($.AXUIElementCopyAttributeValue, e, $(attr), ref);
    return err === 0 ? ref[0] : null;
  };
  const value = (e, attr) => {
    const v = copy(e, attr);
    if (v === null || v === undefined) return undefined;
    try { return ObjC.unwrap(obj(v)); } catch (x) { return undefined; }
  };
  const role = e => String(value(e, 'AXRole') ?? '');
  const title = e => String(value(e, 'AXTitle') ?? '');
  const kids = e => {
    const v = value(e, 'AXChildren');
    return Array.isArray(v) ? v : [];
  };
  const app = $.AXUIElementCreateApplication(pid);
  const wide = $.AXUIElementCreateSystemWide();
  // The last hit test's AXError per root (-1: the call itself threw, i.e.
  // the bridge; -25211 kAXErrorAPIDisabled: this process isn't trusted for
  // Accessibility; -25205 kAXErrorCannotComplete), for the report.
  const errors = {};
  const hit = (x, y) => {
    for (const [name, root] of [['app', app], ['wide', wide]]) {
      const ref = Ref();
      const err = call($.AXUIElementCopyElementAtPosition, root, x, y, ref);
      errors[name] = err;
      if (err === 0 && ref[0]) return ref[0];
    }
    return null;
  };
  // The process's on-screen windows above the normal layer (0), in
  // front-to-back order: {x, y, w, h, layer} in screen points.
  const menuWindows = () => {
    let list = [];
    try {
      // kCGWindowListOptionOnScreenOnly, kCGNullWindowID.
      list = ObjC.deepUnwrap(obj($.CGWindowListCopyWindowInfo(1, 0))) || [];
    } catch (x) {}
    return list.filter(w => w.kCGWindowOwnerPID === pid && w.kCGWindowLayer > 0 &&
                            w.kCGWindowBounds)
        .map(w => ({ x: w.kCGWindowBounds.X, y: w.kCGWindowBounds.Y, w: w.kCGWindowBounds.Width,
                     h: w.kCGWindowBounds.Height, layer: w.kCGWindowLayer }));
  };
  const points = w => [[w.x + w.w / 2, w.y + Math.min(14, w.h / 2)], [w.x + w.w / 2, w.y + w.h / 2],
                       [w.x + w.w / 2, w.y + Math.max(w.h - 14, w.h / 2)]];
  // The roles from `e` up through AXParent (at most 10).
  const chain = e => {
    const out = [];
    for (let k = 0; e && k < 10; k++, e = copy(e, 'AXParent')) out.push({ e, role: role(e) });
    return out;
  };
  // The outermost AXMenu above the element at a menu window's points (a
  // submenu's AXParent is its item, whose parent is the menu above).
  const findMenu = () => {
    for (const w of menuWindows()) {
      for (const [x, y] of points(w)) {
        const up = chain(hit(x, y));
        let menu = null;
        for (const { e, role: r } of up) {
          if (r === 'AXMenu') menu = e;
          else if (r !== 'AXMenuItem') break;
        }
        if (menu) return menu;
      }
    }
    return null;
  };
  const report = () => {
    const wins = menuWindows();
    const at = wins.slice(0, 4).map(w => chain(hit(...points(w)[0])).map(c => c.role).join('<'));
    let trusted;
    try { trusted = !!$.AXIsProcessTrusted(); } catch (x) { trusted = `? (${x})`; }
    return {
      windows: wins.map(w => `${w.x},${w.y} ${w.w}x${w.h} layer ${w.layer}`),
      at,
      errors: { ...errors },
      trusted,
      focused: role(copy(wide, 'AXFocusedUIElement')),
    };
  };
  return {
    findMenu, report, role, title, kids,
    items: m => kids(m).filter(i => role(i) === 'AXMenuItem'),
    menus: i => kids(i).filter(c => role(c) === 'AXMenu'),
    enabled: i => value(i, 'AXEnabled') !== false,
    perform: (e, action) => call($.AXUIElementPerformAction, e, action) === 0,
  };
}

function run(argv) {
  const a = JSON.parse(argv[0]);
  const out = o => JSON.stringify(o);
  const se = Application('System Events');
  let p;
  try {
    p = se.applicationProcesses.whose({ unixId: a.pid })[0];
    p.name();
  } catch (e) {
    return out({ error: `no process ${a.pid}: ${e}` });
  }
  if (a.op === 'probe') {
    try {
      return out({ ok: p.menuBars().length > 0 });
    } catch (e) {
      return out({ error: String(e) });
    }
  }
  const role = e => { try { return e.role(); } catch (x) { return ''; } };
  const title = e => { try { return e.name() || ''; } catch (x) { return ''; } };
  const perform = (e, action) => {
    try {
      e.actions.byName(action).perform();
    } catch (x) {
      se.click(e);
    }
  };
  const kids = e => { try { return e.uiElements(); } catch (x) { return []; } };
  // A context menu is a child of the application (or of a window); else
  // the top menu above the focused item. AppKit makes a context menu the
  // child of the view it pops up for (popUpContextMenu:withEvent:forView:,
  // Chromium's window's content view), so windows are searched a few levels
  // down too.
  const MENU_DEPTH = 3;
  const menuBelow = (e, depth) => {
    for (const c of kids(e)) {
      if (role(c) === 'AXMenu') return c;
      if (depth > 1) {
        const m = menuBelow(c, depth - 1);
        if (m) return m;
      }
    }
    return null;
  };
  const focusedRole = () => {
    try { return role(p.attributes.byName('AXFocusedUIElement').value()); } catch (x) { return ''; }
  };
  if (a.op === 'dump') {
    const tree = (e, depth) => kids(e).map(c => (depth > 1 ? { [role(c)]: tree(c, depth - 1) }
                                                            : role(c)));
    let windows = [];
    try { windows = p.windows().map(w => tree(w, MENU_DEPTH - 1)); } catch (x) {}
    let hit;
    try { hit = a.hit === false ? 'off' : rawAx(a.pid).report(); } catch (x) { hit = { error: String(x) }; }
    return out({ app: kids(p).map(role), focused: focusedRole(), windows, hit });
  }
  const openMenu = () => {
    for (const e of kids(p)) if (role(e) === 'AXMenu') return e;
    try {
      let e = p.attributes.byName('AXFocusedUIElement').value();
      let menu = null;
      while (e && /^AXMenu(Item)?$/.test(role(e))) {
        if (role(e) === 'AXMenu') menu = e;
        e = e.attributes.byName('AXParent').value();
      }
      if (menu) return menu;
    } catch (x) {}
    try {
      for (const w of p.windows()) {
        const m = menuBelow(w, MENU_DEPTH);
        if (m) return m;
      }
    } catch (x) {}
    return null;
  };
  // The menu's driver: System Events' objects for a menu a walk found, the
  // raw API for one the hit test found (`via` says which, in every answer).
  const viaSe = {
    title, items: m => m.menuItems(), menus: i => i.menus(),
    enabled: i => i.enabled(), perform: (e, action) => { perform(e, action); return true; },
  };
  let A = viaSe, via = 'tree';
  let menu = openMenu();
  if (!menu && a.hit !== false) {
    try {
      const raw = rawAx(a.pid);
      menu = raw.findMenu();
      if (menu) { A = raw; via = 'hit test'; }
    } catch (x) {
      menu = null;
    }
  }
  if (!menu) return out({ open: false });
  const reply = o => out({ open: true, via, ...o });
  if (a.op === 'read') return reply({ items: A.items(menu).map(A.title) });
  if (a.op === 'cancel') {
    A.perform(menu, 'AXCancel');
    return reply({ cancelled: true });
  }
  let item = null;
  for (let k = 0; k < a.path.length; k++) {
    if (k > 0) {
      let subs = A.menus(item);
      if (!subs.length || !A.items(subs[0]).length) {
        A.perform(item, 'AXPress');  // Opens the submenu.
        delay(0.4);
        subs = A.menus(item);
      }
      if (!subs.length) return reply({ missing: a.path[k], items: [], why: 'no submenu' });
      menu = subs[0];
    }
    const list = A.items(menu);
    const want = a.path[k];
    item = typeof want === 'number' ? list[want]
                                    : list.find(i => A.title(i).trim() === want);
    if (!item) return reply({ missing: want, items: list.map(A.title) });
  }
  if (!item) return reply({ missing: null, items: [] });
  try {
    if (!A.enabled(item)) return reply({ disabled: A.title(item) });
  } catch (x) {}
  A.perform(item, 'AXPress');
  return reply({ clicked: A.title(item) });
}
