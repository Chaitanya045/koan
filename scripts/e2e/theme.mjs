// End-to-end test of Zen's theme picker against a real build: the command
// palette's "Open Theme Picker" and a Space's "Edit Theme…" open the panel
// beside the sidebar; its layout and controls match Zen's; a preset gives
// exactly Zen's dot colours and window gradient; the colour actions, opacity
// slider and grain dial work; the theme is saved per Space when the panel
// closes and survives a restart; switching Spaces switches the gradient;
// the scheme buttons set the website appearance, which a theme that makes
// the window light or dark doesn't change (chrome:// pages follow the
// window); the field, slider and dial work from the keyboard; the Settings
// switch turns the themes off; the Night dark theme style applies.
//
//   scripts/e2e/run.sh theme
//
// Native views are read and driven through UI DevTools; keys go to a
// widget's node, so they take the path of real keys (Esc closes the panel,
// Enter runs a palette action).
import { readFileSync, writeFileSync } from 'node:fs';
import { spawn } from 'node:child_process';
import { CDP_PORT, UI_PORT, browserCall, connect, evalIn, pages, settingsCall, sleep,
         visiblePage, waitFor, check, finish } from './lib/koan.mjs';

const unquote = v => (typeof v === 'string' ? v.replace(/^"(.*)"$/, '$1') : v);

// Every widget and view as {node, cls, nodeId, shown, x, y, w, h, ax, ay,
// root, path, p}: x/y relative to the parent, ax/ay to the widget's root
// view; `p` holds all properties; `shown` includes the ancestors'.
async function tree() {
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

async function ui(method, params) {
  const c = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await c.send('DOM.enable');
  await c.send('DOM.getDocument', { depth: -1 });
  const r = await c.send(method, params);
  c.close();
  return r;
}

// A mouse event at (x, y) in `view`, sent to its root view like a real one,
// so the root view targets the view under it (menus, drags).
const mouse = (view, type, x, y, button = 'left') =>
  ui('DOM.dispatchMouseEvent', { nodeId: view.root.nodeId,
      event: { type, x: Math.round(view.ax + x), y: Math.round(view.ay + y), button,
               wheelDirection: 'none' } });

async function click(view, x = 10, y = 10, button = 'left', wait = 700) {
  await mouse(view, 'mousePressed', x, y, button);
  await mouse(view, 'mouseReleased', x, y, button);
  await sleep(wait);
}

async function drag(view, from, to) {
  await mouse(view, 'mousePressed', ...from);
  await mouse(view, 'mouseDragged', ...to);
  await mouse(view, 'mouseReleased', ...to);
  await sleep(600);
}

// A plain key (Enter, Esc, arrows) to a widget (its node), like a real key
// press.
async function widgetKey(widget, keyCode) {
  for (const type of ['keyPressed', 'keyReleased']) {
    await ui('DOM.dispatchKeyEvent', { nodeId: widget.nodeId,
        event: { type, keyCode, code: 0, flags: 0, key: 0, isChar: false } });
  }
}

// Node ids change with each document (the first keystroke floats the URL
// bar, adding views), so the omnibox is looked up for each character.
async function typeIntoOmnibox(text) {
  for (const ch of text) {
    const omnibox = shown(await tree(), 'OmniboxViewViews')[0];
    await ui('DOM.dispatchKeyEvent', { nodeId: omnibox.nodeId,
        event: { type: 'keyPressed', keyCode: 0, code: 0, flags: 0, key: ch.codePointAt(0), isChar: true } });
  }
}

// A shortcut, sent to the page: unhandled keys reach the browser's
// accelerators. Modifiers: Alt=1 Ctrl=2.
async function shortcut(code, modifiers, key = '') {
  const c = await connect((await visiblePage()).webSocketDebuggerUrl);
  const base = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers, key };
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
  c.close();
  await sleep(1500);
}

const VK = { TAB: 9, ENTER: 13, ESC: 27, END: 35, HOME: 36, LEFT: 37, RIGHT: 39, DOWN: 40,
             DELETE: 46, L: 76, N: 78 };
const CTRL = 2, ALT = 1;

const shown = (t, cls) => t.filter(v => v.cls === cls && v.shown);
// The browser window's widget: the first one holding a BrowserView.
const browserWidget = t => t.find(w => w.node === 'Widget' &&
  t.slice(t.indexOf(w) + 1).find(v => v.node === 'Widget' || v.cls === 'BrowserView')?.cls === 'BrowserView');
const button = (t, label) => shown(t, 'KoanThemePickerButton').find(b => b.p.Label === label);
const picker = t => shown(t, 'KoanThemePickerView')[0];
// The picker's bubble widget: the last widget before the picker view.
const pickerWidget = t => {
  const p = picker(t);
  return p && t.slice(0, t.indexOf(p)).filter(v => v.node === 'Widget').at(-1);
};

// The command palette: Ctrl+L, type the action, Enter. An action is the
// second row (below the search) until it has been used; then its whole name
// makes it the first.
const usedActions = new Set();
async function palette(action) {
  await shortcut(VK.L, CTRL, 'l');
  await typeIntoOmnibox(action);
  await sleep(1200);
  if (!usedActions.has(action)) {
    usedActions.add(action);
    await widgetKey(browserWidget(await tree()), VK.DOWN);
    await sleep(300);
  }
  await widgetKey(browserWidget(await tree()), VK.ENTER);
  await sleep(1500);
}

// A key to the picker's widget, which gives it to its focused view.
async function pickerKey(keyCode) {
  await widgetKey(pickerWidget(await tree()), keyCode);
  await sleep(300);
}

// "x,y" of the first (big) dot.
const bigDot = p => (p?.p.Dots ?? '').split(';')[0].split('@')[1]?.split(',').map(Number);

async function closePicker() {
  const t = await tree();
  const w = pickerWidget(t);
  if (w) await widgetKey(w, VK.ESC);
  await sleep(1000);
}

// What the showing page's prefers-color-scheme says.
let lastPage = '';
async function pagePrefersDark() {
  const page = await visiblePage();
  const c = await connect(page.webSocketDebuggerUrl);
  const r = await c.send('Runtime.evaluate', {
    expression: "matchMedia('(prefers-color-scheme: dark)').matches", returnByValue: true });
  c.close();
  lastPage = `${page.url}: ${r.result?.result?.value}`;
  return r.result?.result?.value;
}

// The page last asked, for failure details.
const appearance = async () => lastPage;

// Loads `url` in the showing tab.
async function navigateShowing(url) {
  const c = await connect((await visiblePage()).webSocketDebuggerUrl);
  await c.send('Page.navigate', { url });
  c.close();
  await sleep(1500);
}

async function setKoanPref(pref, value) {
  const tab = (await browserCall('Target.createTarget',
      { url: 'chrome://koan-settings', background: true })).result.targetId;
  await sleep(1500);
  const settings = (await pages()).find(page => page.id === tab);
  const ok = await settingsCall(settings, 'setPref', pref, value) !== false;
  await browserCall('Target.closeTarget', { targetId: tab });
  await sleep(800);
  return ok;
}

// Restarts Koan on the same profile, restoring the session.
const args = process.env.KOAN_CHROME_ARGS.split('\n').filter(Boolean);
const profile = args.find(a => a.startsWith('--user-data-dir=')).split('=')[1];
let relaunched = null;
process.on('exit', () => relaunched?.kill());
const browserUp = () => fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).then(() => true, () => false);
async function restart() {
  await browserCall('Browser.close');
  await waitFor(async () => !(await browserUp()), 20000);
  await sleep(2000);
  const file = `${profile}/Default/Preferences`;
  const prefs = JSON.parse(readFileSync(file, 'utf8'));
  prefs.session = { ...prefs.session, restore_on_startup: 1 };
  writeFileSync(file, JSON.stringify(prefs));
  relaunched = spawn(args[0], args.slice(1), { stdio: 'ignore' });
  await waitFor(browserUp, 30000);
  await waitFor(async () => (await pages()).length, 15000);
  await sleep(3000);
}

// Zen's colours for the analogous preset of page 2 (data-position 234,173,
// lightness 70), measured in Zen 1.23.3b, and the gradient Zen paints for
// it in a dark window.
const ZEN_DOTS = '234,123,137@234,173;234,204,123@219,217;233,124,229@210,133';
const ZEN_DARK_GRADIENT =
    'linear-gradient(-5deg, rgba(128, 74, 128, 1) 10%, transparent 80%), ' +
    'radial-gradient(circle at 95% 0%, rgba(129, 114, 75, 1) 0%, transparent 75%), ' +
    'radial-gradient(circle at 0% 0%, rgba(129, 73, 82, 1) 10%, transparent 70%)';

// Zen's values for the first preset of page 1 (#f4efdf, data-position
// 240,240, lightness 90) in a dark window at opacity 0.8 (the Space's,
// set above): its dot, the window's colour, and that it makes the window
// light (zen-should-be-dark-mode false); and the colour of that dot dragged
// to 200,180. Measured in Zen 1.23.3b.
const ZEN_LIGHT_DOT = '244,237,215@240,240';
const ZEN_LIGHT_BACKGROUND = 'rgba(200, 194, 177, 1)';
const ZEN_DRAGGED_DOT = '252,207,207@200,180';

await shortcut(VK.N, CTRL | ALT, 'n');

// The Space menu (right-click the Space in the switcher): Edit Theme….
{
  let t = await tree();
  await click(shown(t, 'KoanWorkspaceButton')[1], 5, 5, 'right', 1200);
  t = await tree();
  const items = shown(t, 'MenuItemView');
  check('Right-clicking a Space opens its menu', items.length >= 2,
        `${items.length} item(s); widgets ${t.filter(v => v.node === 'Widget').map(v => v.cls)}`);
  // The first item, Edit Theme….
  const first = items.sort((a, b) => a.ay - b.ay)[0];
  if (first) await click(first, 20, Math.round(first.h / 2), 'left', 1500);
  check('"Edit Theme…" opens the theme picker', !!picker(await tree()));
  await closePicker();
}

await palette('Open Theme Picker');
let t = await tree();
let p = picker(t);
check('The palette\'s "Open Theme Picker" opens the theme picker', !!p);
if (!p) finish();

check("The panel has Zen's size (358px wide inside 10px padding)",
      p.w === 358 && p.h === 499, `${p.w}x${p.h}`);
const field = shown(t, 'KoanThemeGradientField')[0];
check('The colour field is 358x358', field?.w === 358 && field?.h === 358,
      `${field?.w}x${field?.h}`);
check('Scheme buttons: Automatic, Light, Dark',
      ['Automatic', 'Light', 'Dark'].every(l => button(t, l)));
check('Without colours the colour actions are disabled',
      ['Add color', 'Remove color'].every(l => button(t, l)?.p.Enabled === 'false'));
check('"Click to add a color" shows in an empty field',
      shown(t, 'Label').some(l => l.p.Text === 'Click to add a color'));
const swatches = t.filter(v => v.cls === 'KoanThemeSwatch');
check("Five preset pages with Zen's 41 swatches", swatches.length === 41, `${swatches.length}`);
check('Opacity slider and grain dial',
      !!shown(t, 'KoanThemeOpacitySlider')[0] && !!shown(t, 'KoanThemeGrainDial')[0]);

await click(button(t, 'Dark'));
check('The Dark scheme button makes websites dark', await pagePrefersDark() === true);

await click(button(await tree(), 'Next colors'));
t = await tree();
p = picker(t);
check('The right arrow pages the presets', p.p.Page === '1', p.p.Page);
await click(t.find(v => v.cls === 'KoanThemeSwatch' && v.p.Index === '11'), 13, 13);
t = await tree();
p = picker(t);
check("A preset gives Zen's dots", p.p.Dots === ZEN_DOTS, p.p.Dots);
check('The preset uses the analogous harmony', p.p.Algorithm === 'analogous', p.p.Algorithm);
check("The window shows Zen's gradient for it", p.p.WindowBackground === ZEN_DARK_GRADIENT,
      p.p.WindowBackground);
check('The colour actions follow the dots (3: no add)',
      button(t, 'Add color')?.p.Enabled === 'false' &&
      button(t, 'Remove color')?.p.Enabled !== 'false');

await click(button(t, 'Remove color'));
t = await tree();
p = picker(t);
check('Remove drops a dot and re-harmonises (complementary)',
      p.p.Dots.split(';').length === 2 && p.p.Algorithm === 'complementary',
      `${p.p.Dots} ${p.p.Algorithm}`);
await click(button(t, 'Complementary'));
t = await tree();
p = picker(t);
check('The harmony button cycles (Analogous)', p.p.Algorithm === 'singleAnalogous', p.p.Algorithm);
await click(button(t, 'Add color'));
t = await tree();
p = picker(t);
check('Add brings a third dot', p.p.Dots.split(';').length === 3, p.p.Dots);

const slider = shown(t, 'KoanThemeOpacitySlider')[0];
await click(slider, slider.w - 6, Math.round(slider.h / 2));
t = await tree();
p = picker(t);
check('The opacity slider goes up to 0.8', Math.abs(+p.p.Opacity - 0.8) < 0.001, p.p.Opacity);

const dial = shown(t, 'KoanThemeGrainDial')[0];
// The handle sits at the top of the dial (8px margin, 73px dial).
await drag(dial, [44, 8], [dial.w - 2, 44]);
t = await tree();
p = picker(t);
check("Dragging the grain handle to 3 o'clock sets 25% grain", +p.p.Texture === 0.25, p.p.Texture);
const edited = p.p.Dots;

await closePicker();
check('Esc closes the panel', !picker(await tree()));

await shortcut(VK.LEFT, CTRL | ALT);
await palette('Open Theme Picker');
t = await tree();
p = picker(t);
check('"All tabs" has its own (default) theme',
      p?.p.Dots === '' && p?.p.WindowBackground === 'rgba(19, 19, 19, 1)',
      `${p?.p.Dots} ${p?.p.WindowBackground}`);

// The keyboard: Tab reaches the colour field first; Enter adds a colour in
// its middle, the arrows move it, Delete removes it.
await pickerKey(VK.TAB);
await pickerKey(VK.ENTER);
p = picker(await tree());
const middle = bigDot(p);
check('Tab, Enter: a colour in the middle of the field',
      middle?.every(c => Math.abs(c - 179) <= 1), p?.p.Dots);
await pickerKey(VK.RIGHT);
await pickerKey(VK.RIGHT);
p = picker(await tree());
check('...the arrows move it', bigDot(p)?.[0] === middle?.[0] + 8 && bigDot(p)?.[1] === middle?.[1],
      p?.p.Dots);
await pickerKey(VK.DELETE);
check('...and Delete removes it', picker(await tree())?.p.Dots === '');
await click(button(t, 'Light'));
check('The Light scheme button makes websites light', await pagePrefersDark() === false,
      await appearance());
p = picker(await tree());
check("A light window without a theme is Zen's #e9e9e9",
      p?.p.WindowBackground === 'rgba(233, 233, 233, 1)', p?.p.WindowBackground);
await click(button(await tree(), 'Dark'));
check('The Dark scheme button makes websites dark again', await pagePrefersDark() === true,
      await appearance());
await closePicker();

await shortcut(VK.RIGHT, CTRL | ALT);
// The Space's tab is a new-tab page, which is WebUI and coloured like the
// window; the website checks below need a website.
await navigateShowing('data:text/html,<title>Space 1</title>A website');
await palette('Open Theme Picker');
p = picker(await tree());
check('The Space kept its theme', p?.p.Dots === edited && +p?.p.Texture === 0.25,
      `${p?.p.Dots} ${p?.p.Texture}`);
check('Websites follow the Dark appearance', await pagePrefersDark() === true, await appearance());

// A theme that makes the window light: websites keep the appearance.
t = await tree();
await click(t.find(v => v.cls === 'KoanThemeSwatch' && v.p.Index === '0'), 13, 13);
p = picker(await tree());
check("A light preset: Zen's dot and window colour",
      p?.p.Dots === ZEN_LIGHT_DOT && p?.p.WindowBackground === ZEN_LIGHT_BACKGROUND,
      `${p?.p.Dots} ${p?.p.WindowBackground}`);
check('...and it makes the window light, as in Zen', p?.p.WindowMode === 'light', p?.p.WindowMode);
check('...while websites keep the Dark appearance', await pagePrefersDark() === true,
      await appearance());
await closePicker();

// chrome:// pages are coloured like the window, so they follow this theme
// (light), and the appearance (Dark) once Space themes are off in Settings.
// (In the showing tab: Chromium updates a hidden tab's colour scheme when
// it shows.)
await navigateShowing('chrome://version');
const webuiDark = async () => {
  const dark = await pagePrefersDark();
  return lastPage.startsWith('chrome://') ? dark : undefined;
};
check('A chrome:// page follows the window\'s light theme',
      await waitFor(async () => await webuiDark() === false), lastPage);
check('Settings switch Space themes off', await setKoanPref('koan.workspaces.tint', false));
check('...which takes the theme off the window (chrome:// pages turn dark)',
      await waitFor(async () => await webuiDark() === true), lastPage);
await setKoanPref('koan.workspaces.tint', true);
check('...and back on', await waitFor(async () => await webuiDark() === false), lastPage);
await navigateShowing('data:text/html,<title>Space 1</title>A website');
await palette('Open Theme Picker');

// Dragging the big dot.
await drag(shown(await tree(), 'KoanThemeGradientField')[0], [240, 240], [200, 180]);
p = picker(await tree());
check("Dragging the big dot moves it and gives Zen's colour there", p?.p.Dots === ZEN_DRAGGED_DOT,
      p?.p.Dots);

// The slider and the dial from the keyboard: Tab to the slider (Home sets
// its minimum), then the dial.
let found = false;
for (let i = 0; i < 20 && !found; i++) {
  await pickerKey(VK.TAB);
  await pickerKey(VK.HOME);
  found = Math.abs(+picker(await tree())?.p.Opacity - 0.25) < 0.001;
}
check('Tab reaches the opacity slider; Home sets 0.25', found);
await pickerKey(VK.RIGHT);
p = picker(await tree());
check('...Right adds 0.05', Math.abs(+p?.p.Opacity - 0.3) < 0.001, p?.p.Opacity);
await pickerKey(VK.TAB);
await pickerKey(VK.HOME);
await pickerKey(VK.LEFT);
p = picker(await tree());
check('The grain dial: Home, then Left wraps to the last step',
      +p?.p.Texture === 15 / 16, p?.p.Texture);
await pickerKey(VK.RIGHT);
await pickerKey(VK.RIGHT);
p = picker(await tree());
check('...and Right steps on (1/16)', +p?.p.Texture === 1 / 16, p?.p.Texture);
const saved = p?.p.Dots;
await closePicker();


// Settings > Look and Feel > Dark Theme Styles: Night Theme.
check('Settings take the dark theme style', await setKoanPref('koan.theme.dark_style', 1));
await shortcut(VK.LEFT, CTRL | ALT);
await palette('Open Theme Picker');
p = picker(await tree());
check('Night theme: a black window', p?.p.WindowBackground === 'rgba(0, 0, 0, 1)',
      p?.p.WindowBackground);
await closePicker();

// The Space's theme after a restart.
await restart();
await shortcut(VK.RIGHT, CTRL | ALT);
await palette('Open Theme Picker');
p = picker(await tree());
check('After a restart, the Space has its theme', !!saved && p?.p.Dots === saved,
      `${saved} / ${p?.p.Dots}`);
await closePicker();

finish();
