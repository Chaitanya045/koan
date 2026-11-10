// Helpers for the theme picker's end-to-end tests (theme.mjs,
// theme_glass.mjs): native views through UI DevTools, keys to a widget like
// real keys, the command palette, the picker and its window background.
import { CDP_PORT, UI_PORT, browserCall, connect, evalIn, pages, settingsCall, sleep, waitFor } from './koan.mjs';

export const unquote = v => (typeof v === 'string' ? v.replace(/^"(.*)"$/, '$1') : v);

// Every widget and view as {node, cls, nodeId, shown, x, y, w, h, ax, ay,
// root, path, p}: x/y relative to the parent, ax/ay to the widget's root
// view; `p` holds all properties; `shown` includes the ancestors'.
export async function tree() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const out = [];
  const walk = async (n, depth, parent, path) => {
    const a = n.attributes ?? [];
    // Widgets are named, views have a class.
    const cls = n.nodeName === 'Widget' ? a[a.indexOf('name') + 1]
                                        : a[a.indexOf('class') + 1];
    let self = parent;
    if (depth > 0 && (n.nodeName === 'View' || n.nodeName === 'Widget') && cls) {
      const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
      const p = {};
      for (const rule of r.result?.matchedCSSRules ?? []) {
        for (const prop of rule.rule.style.cssProperties) p[prop.name] = unquote(prop.value);
      }
      if (n.nodeName === 'Widget') {
        // A widget's views start a new tree.
        out.push({ node: 'Widget', cls, nodeId: n.nodeId, shown: true, path, p });
        self = null;
      } else {
        self = {
          node: 'View', cls, nodeId: n.nodeId, x: +p.X, y: +p.Y, w: +p.Width, h: +p.Height,
          shown: (parent ? parent.shown : true) && p.Visible !== 'false',
          ax: parent ? parent.ax + +p.X : 0, ay: parent ? parent.ay + +p.Y : 0, path, p,
        };
        self.root = parent ? parent.root : self;
        out.push(self);
      }
      path = [...path, cls];
    }
    for (const c of n.children ?? []) await walk(c, depth + 1, self, path);
  };
  await walk(doc, 0, null, []);
  ui.close();
  return out;
}

export async function ui(method, params) {
  const c = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await c.send('DOM.enable');
  await c.send('DOM.getDocument', { depth: -1 });
  const r = await c.send(method, params);
  c.close();
  return r;
}

// A mouse event at (x, y) in `view`, sent to its root view like a real one,
// so the root view targets the view under it (menus, drags).
export const mouse = (view, type, x, y, button = 'left') =>
  ui('DOM.dispatchMouseEvent', { nodeId: view.root.nodeId,
      event: { type, x: Math.round(view.ax + x), y: Math.round(view.ay + y), button,
               wheelDirection: 'none' } });

export async function click(view, x = 10, y = 10, button = 'left', wait = 700) {
  await mouse(view, 'mousePressed', x, y, button);
  await mouse(view, 'mouseReleased', x, y, button);
  await sleep(wait);
}

export async function drag(view, from, to) {
  await mouse(view, 'mousePressed', ...from);
  await mouse(view, 'mouseDragged', ...to);
  await mouse(view, 'mouseReleased', ...to);
  await sleep(600);
}

// A plain key (Enter, Esc, arrows) to a widget (its node), like a real key
// press.
export async function widgetKey(widget, keyCode) {
  for (const type of ['keyPressed', 'keyReleased']) {
    await ui('DOM.dispatchKeyEvent', { nodeId: widget.nodeId,
        event: { type, keyCode, code: 0, flags: 0, key: 0, isChar: false } });
  }
}

// Node ids change with each document (the first keystroke floats the URL
// bar, adding views), so the omnibox is looked up for each character.
export async function typeIntoOmnibox(text) {
  for (const ch of text) {
    const omnibox = shown(await tree(), 'OmniboxViewViews')[0];
    await ui('DOM.dispatchKeyEvent', { nodeId: omnibox.nodeId,
        event: { type: 'keyPressed', keyCode: 0, code: 0, flags: 0, key: ch.codePointAt(0), isChar: true } });
  }
}

// The tab on screen: the visible page that isn't the browser's own WebUI
// (the omnibox popup and other top-chrome pages are pages too).
export async function showingTab() {
  for (const page of await pages()) {
    if (!page.url.includes('.top-chrome') &&
        await evalIn(page, 'document.visibilityState') === 'visible') {
      return page;
    }
  }
  return null;
}

// The URL bar's suggestion rows, by their first line (the omnibox popup is
// WebUI: chrome://omnibox-popup.top-chrome), and the selected one.
export async function paletteRows() {
  const popup = (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .find(t => t.url.startsWith('chrome://omnibox-popup'));
  if (!popup) return { labels: [], selected: -1 };
  return (await evalIn(popup, `(() => {
    const matches = [];
    const walk = root => {
      for (const el of root.querySelectorAll('*')) {
        if (el.localName === 'cr-searchbox-match') matches.push(el);
        if (el.shadowRoot) walk(el.shadowRoot);
      }
    };
    walk(document);
    const shown = matches.filter(m => m.getClientRects().length);
    return {
      labels: shown.map(m => m.shadowRoot.getElementById('primaryText')?.textContent.trim() ?? ''),
      selected: shown.findIndex(m => m.hasAttribute('selected')),
    };
  })()`)) ?? { labels: [], selected: -1 };
}

// A shortcut, sent to the page: unhandled keys reach the browser's
// accelerators. Modifiers: Alt=1 Ctrl=2.
export async function shortcut(code, modifiers, key = '') {
  const c = await connect((await showingTab()).webSocketDebuggerUrl);
  const base = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers, key };
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
  c.close();
  await sleep(1500);
}

export const VK = { TAB: 9, ENTER: 13, ESC: 27, END: 35, HOME: 36, LEFT: 37, RIGHT: 39, DOWN: 40,
             DELETE: 46, L: 76, N: 78 };
export const CTRL = 2, ALT = 1;

export const shown = (t, cls) => t.filter(v => v.cls === cls && v.shown);
// The browser window's widget: the first one holding a BrowserView.
export const browserWidget = t => t.find(w => w.node === 'Widget' &&
  t.slice(t.indexOf(w) + 1).find(v => v.node === 'Widget' || v.cls === 'BrowserView')?.cls === 'BrowserView');
export const button = (t, label) => shown(t, 'KoanThemePickerButton').find(b => b.p.Label === label);
export const picker = t => shown(t, 'KoanThemePickerView')[0];
// The picker's bubble widget: the last widget before the picker view.
export const pickerWidget = t => {
  const p = picker(t);
  return p && t.slice(0, t.indexOf(p)).filter(v => v.node === 'Widget').at(-1);
};

// The command palette: Ctrl+L, type the action, select its row (by its
// label: where it is depends on what was used before), Enter.
export async function palette(action) {
  await shortcut(VK.L, CTRL, 'l');
  await typeIntoOmnibox(action);
  const target = (await waitFor(async () => {
    const { labels } = await paletteRows();
    return labels.indexOf(action) >= 0 && labels;
  })) || [];
  const index = target.indexOf(action);
  for (let i = 0; i < 10 && index >= 0; i++) {
    if ((await paletteRows()).selected === index) break;
    await widgetKey(browserWidget(await tree()), VK.DOWN);
    await sleep(300);
  }
  await widgetKey(browserWidget(await tree()), VK.ENTER);
  await sleep(1500);
}

// A key to the picker's widget, which gives it to its focused view.
export async function pickerKey(keyCode) {
  const widget = pickerWidget(await tree());
  if (widget) await widgetKey(widget, keyCode);
  await sleep(300);
}

// "x,y" of the first (big) dot.
export const bigDot = p => (p?.p.Dots ?? '').split(';')[0].split('@')[1]?.split(',').map(Number);

export async function closePicker() {
  const t = await tree();
  const w = pickerWidget(t);
  if (w) await widgetKey(w, VK.ESC);
  await sleep(1000);
}

// What the showing page's prefers-color-scheme says.
let lastPageText = '';
// The page pagePrefersDark() last asked, and its answer.
export const lastPage = () => lastPageText;
export async function pagePrefersDark() {
  const page = await showingTab();
  const c = await connect(page.webSocketDebuggerUrl);
  const r = await c.send('Runtime.evaluate', {
    expression: "matchMedia('(prefers-color-scheme: dark)').matches", returnByValue: true });
  c.close();
  lastPageText = `${page.url}: ${r.result?.result?.value}`;
  return r.result?.result?.value;
}

// The page last asked, for failure details.
export const appearance = async () => lastPageText;

// Loads `url` in the showing tab.
export async function navigateShowing(url) {
  const c = await connect((await showingTab()).webSocketDebuggerUrl);
  await c.send('Page.navigate', { url });
  c.close();
  await sleep(1500);
}

export async function setKoanPref(pref, value) {
  const tab = (await browserCall('Target.createTarget',
      { url: 'chrome://koan-settings', background: true })).result.targetId;
  await sleep(1500);
  const settings = (await pages()).find(page => page.id === tab);
  const ok = await settingsCall(settings, 'setPref', pref, value) !== false;
  await browserCall('Target.closeTarget', { targetId: tab });
  await sleep(800);
  return ok;
}
