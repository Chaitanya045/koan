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
// While an NSMenu is open its event-tracking loop runs no Chromium tasks, so
// CDP and UI DevTools can't answer; AppKit still answers Accessibility.
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
  // A context menu is a child of the application (or of a window); else
  // the top menu above the focused item.
  const openMenu = () => {
    for (const e of p.uiElements()) if (role(e) === 'AXMenu') return e;
    try {
      let e = p.attributes.byName('AXFocusedUIElement').value();
      let menu = null;
      while (e && /^AXMenu(Item)?$/.test(role(e))) {
        if (role(e) === 'AXMenu') menu = e;
        e = e.attributes.byName('AXParent').value();
      }
      if (menu) return menu;
    } catch (x) {}
    for (const w of p.windows()) {
      for (const e of w.uiElements()) if (role(e) === 'AXMenu') return e;
    }
    return null;
  };
  let menu = openMenu();
  if (!menu) return out({ open: false });
  if (a.op === 'read') return out({ open: true, items: menu.menuItems().map(title) });
  if (a.op === 'cancel') {
    perform(menu, 'AXCancel');
    return out({ open: true, cancelled: true });
  }
  let item = null;
  for (let k = 0; k < a.path.length; k++) {
    if (k > 0) {
      let subs = item.menus();
      if (!subs.length || !subs[0].menuItems().length) {
        perform(item, 'AXPress');  // Opens the submenu.
        delay(0.4);
        subs = item.menus();
      }
      if (!subs.length) return out({ open: true, missing: a.path[k], items: [], why: 'no submenu' });
      menu = subs[0];
    }
    const list = menu.menuItems();
    const want = a.path[k];
    item = typeof want === 'number' ? list[want]
                                    : list.find(i => title(i).trim() === want);
    if (!item) return out({ open: true, missing: want, items: list.map(title) });
  }
  if (!item) return out({ open: true, missing: null, items: [] });
  try {
    if (!item.enabled()) return out({ open: true, disabled: title(item) });
  } catch (x) {}
  perform(item, 'AXPress');
  return out({ open: true, clicked: title(item) });
}
