// End-to-end audit of Koan's visual settings (docs/SETTINGS_AUDIT.md): each
// row is changed THROUGH the settings page (its own checkbox, menu, card or
// text area in chrome://settings/koan/...), what it should do is checked in
// the window (UI DevTools view tree) or a page (CDP), and the row is put
// back. Rows whose effect UI DevTools and CDP can't see on macOS (the
// window's blur, haptics, native menus) are skipped with the reason.
//
//   scripts/e2e/run.sh settings_effects_visual
//
// On macOS too (scripts/mac/suite/e2e.list): every key it sends is one of
// Koan's window shortcuts, which CDP keys reach; ⇧⌘B goes through the menu
// bar. Native fullscreen for the layout rows is #311's suites
// (fullscreen_multiple, fullscreen_single); this one checks the bookmarks
// bar there.
import {
  ACCEL, ALT, CTRL, IS_MAC, SHIFT, UI_PORT, browserCall, check, connect, evalIn, finish, front,
  key, onPage, pages, showPane, sleep, typeInto, viewMouse, viewTree, waitFor,
} from './lib/koan.mjs';
import { goFullscreen } from './lib/fullscreen.mjs';
import {
  KOAN_PID, browserWidgets, realInputWhyNot, realKey, skipped, startServer,
} from './lib/mac.mjs';
import { execFileSync } from 'node:child_process';

// --- A local server for the pages (no network needed).
const routes = {};
const server = await startServer(routes);
// Links for Glance, the background tab toast and Essentials' external links,
// with nothing at the top of the page (window dragging) and a recorder of
// the mouse events the page saw there.
routes['/'] = `<!doctype html><title>Settings effects</title>
<body style="margin: 0; font: 18px sans-serif; height: 1200px">
<p style="position: absolute; top: 200px; left: 40px">
  <a id="glance" href="${server.url('/p?t=Glanced')}"
     style="display: inline-block; width: 300px; height: 40px">glance me</a></p>
<p style="position: absolute; top: 260px; left: 40px">
  <a id="background" href="${server.url('/p?t=Background')}"
     style="display: inline-block; width: 300px; height: 40px">open me in the background</a></p>
<p style="position: absolute; top: 320px; left: 40px">
  <a id="external" target="_blank" rel="noopener"
     href="${server.url('/p?t=Elsewhere').replace('127.0.0.1', 'localhost')}"
     style="display: inline-block; width: 300px; height: 40px">another site</a></p>
<script>
  window.seen = [];
  for (const type of ['mousedown', 'mousemove', 'mouseup']) {
    addEventListener(type, e => {
      if (type !== 'mousemove' || e.buttons) seen.push(type.slice(5));
    }, true);
  }
</script>`;

const VK = { B: 66, D: 68, S: 83, ESC: 27 };

// --- Pages.
async function openTab(url, background = false) {
  const targetId = (await browserCall('Target.createTarget', { url, background })).result.targetId;
  const page = await waitFor(async () => (await pages()).find(p => p.id === targetId));
  await waitFor(() => evalIn(page, `document.readyState === 'complete'`));
  await sleep(500);
  return page;
}
const closeTab = page => page && browserCall('Target.closeTarget', { targetId: page.id });
// A fresh test page, in a renderer process started after the settings
// changed so far (Glance's trigger is read when the process starts).
let testPage = null;
async function freshTestPage() {
  await closeTab(testPage);
  // A different site each time (alternating hosts), so it never shares a
  // process with the page before.
  freshTestPage.n = (freshTestPage.n ?? 0) + 1;
  const url = server.url(`/?n=${freshTestPage.n}`);
  testPage = await openTab(freshTestPage.n % 2 ? url : url.replace('127.0.0.1', 'localhost'));
  await front(testPage);
  return testPage;
}
async function clickLink(page, id, modifiers = 0) {
  const box = await evalIn(page, `(() => {
    const r = document.getElementById('${id}').getBoundingClientRect();
    return { x: r.x + 20, y: r.y + r.height / 2 };
  })()`);
  await onPage(page, async c => {
    for (const type of ['mousePressed', 'mouseReleased']) {
      await c.send('Input.dispatchMouseEvent',
                   { type, x: box.x, y: box.y, button: 'left', clickCount: 1, modifiers });
    }
  });
  await sleep(1200);
}

// --- The settings page (a background tab: the window shows the test pages).
const settings = await openTab('chrome://settings/koan/looks', true);
await waitFor(() => evalIn(settings,
    `window.koanSettingsRoot?.getElementById('pane')?.childElementCount > 0`), 8000);
const row = pref => `koanSettingsRoot.querySelector('[data-pref="${pref}"]')`;
const prefOf = pref => evalIn(settings, `import('chrome://resources/js/cr.js').then(
    m => m.sendWithPromise('koanGetSettings')).then(s => s.prefs[${JSON.stringify(pref)}])`);
const optionsOf = pref => evalIn(settings, `import('chrome://resources/js/cr.js').then(
    m => m.sendWithPromise('koanGetSettings')).then(
        s => s.rows.find(r => r.pref === ${JSON.stringify(pref)})?.options.map(o => o.value))`);
async function pane(id, pref) {
  await evalIn(settings, showPane(id));
  await waitFor(() => evalIn(settings, `!!${row(pref)}`));
}
// Whether the row's control is greyed out, and whether its note shows.
const isDisabled = pref => evalIn(settings,
    `${row(pref)}?.querySelector('input, select, textarea, button')?.disabled`);
const noteShown = pref => evalIn(settings,
    `(() => { const n = koanSettingsRoot.querySelector('[data-note="${pref}"]');
              return !!n && !n.hidden; })()`);
// Clicks the row's checkbox (if it isn't `on` already); returns whether the
// pref then is `on`.
async function setCheckbox(pref, on) {
  const state = await evalIn(settings, `(() => {
    const input = ${row(pref)}?.querySelector('input');
    if (!input) return 'missing';
    if (input.disabled) return 'disabled';
    if (input.checked !== ${on}) input.click();
    return 'ok';
  })()`);
  return state === 'ok' && await waitFor(async () => await prefOf(pref) === on);
}
// Picks the row's option with value `value`, as the drop-down does.
async function setMenu(pref, value) {
  const index = (await optionsOf(pref))?.indexOf(value) ?? -1;
  const state = await evalIn(settings, `(() => {
    const select = ${row(pref)}?.querySelector('select');
    if (!select) return 'missing';
    if (select.disabled) return 'disabled';
    select.selectedIndex = ${index};
    select.dispatchEvent(new Event('change'));
    return 'ok';
  })()`);
  return index >= 0 && state === 'ok' && await waitFor(async () => await prefOf(pref) === value);
}
const card = label => `[...koanSettingsRoot.querySelectorAll('.layout-card')]
    .find(c => c.textContent.includes('${label}'))`;
async function pickLayout(label, single) {
  await evalIn(settings, `${card(label)}.click()`);
  return waitFor(async () => await prefOf('koan.single_toolbar') === single);
}

// --- The window (UI DevTools): the views of the first browser window.
async function win(index = 0) {
  const vs = await viewTree();
  const widgets = await browserWidgets(vs);
  const views = widgets[index]?.views ?? [];
  const find = cls => views.find(v => v.cls === cls);
  const shown = cls => views.find(v => v.cls === cls && v.shown && v.w > 0 && v.h > 0);
  return { vs, views, find, shown, count: widgets.length };
}
const sidebarWidth = async () => (await win()).find('VerticalTabStripRegionView')?.w ?? -1;
const cardY = async () => (await win()).shown('ContentsContainerView')?.ay;
// Polls until `fn(await win())` is truthy.
const waitWin = (fn, timeout = 5000) => waitFor(async () => fn(await win()), timeout);

testPage = await freshTestPage();

// =========================================================================
// Look and Feel > Appearance: Look, Mode, Base colour, Accent colour, theme.
// The page paints its root from the look's tokens (koan_tokens.css), so the
// look shows there; the window's own colours need frame pixels.
const bg = () => evalIn(settings, `getComputedStyle(document.documentElement).backgroundColor`);
const primary = () => evalIn(settings,
    `getComputedStyle(document.documentElement).getPropertyValue('--primary').trim()`);
check('Look: shadcn by default, with its near-black background (#09090b)',
      await prefOf('koan.look.style') === 'shadcn' &&
      await waitFor(async () => await bg() === 'rgb(9, 9, 11)'), await bg());
check('...the shadcn colour rows are enabled', await isDisabled('koan.look.mode') === false &&
      await isDisabled('koan.look.base') === false && await isDisabled('koan.look.accent') === false);
check('...and the Zen-only Dark theme style is greyed out, with its note',
      await isDisabled('koan.theme.dark_style') === true &&
      await noteShown('koan.theme.dark_style'));
check('Look: choosing Zen in the page applies it', await setMenu('koan.look.style', 'zen') &&
      await waitFor(async () => await bg() !== 'rgb(9, 9, 11)'), await bg());
check('...greys out the shadcn colour rows', await waitFor(async () =>
      await isDisabled('koan.look.mode') === true && await isDisabled('koan.look.base') === true &&
      await isDisabled('koan.look.accent') === true &&
      await isDisabled('koan.look.custom_css') === true));
check('...and enables Dark theme style, without the note',
      await isDisabled('koan.theme.dark_style') === false &&
      !(await noteShown('koan.theme.dark_style')));
await pane('tabs', 'koan.workspaces.tint');
check('...and "Show the active Space\'s theme" (Tab Management)',
      await isDisabled('koan.workspaces.tint') === false && !(await noteShown('koan.workspaces.tint')));
await pane('looks', 'koan.look.style');
check('Look: back to shadcn', await setMenu('koan.look.style', 'shadcn') &&
      await waitFor(async () => await bg() === 'rgb(9, 9, 11)'), await bg());
await pane('tabs', 'koan.workspaces.tint');
check('...where the Space theme row is greyed out, with its note',
      await waitFor(async () => await isDisabled('koan.workspaces.tint') === true) &&
      await noteShown('koan.workspaces.tint'));
await pane('looks', 'koan.look.style');
skipped('Dark theme style / Space theme: the window\'s Space colours',
        'need a Space with a theme (Theme... is in a native menu on macOS); ' +
        'settings.mjs checks the tint off macOS');

check('Mode: Light turns the page light', await setMenu('koan.look.mode', 'light') &&
      await waitFor(async () => await bg() === 'rgb(255, 255, 255)'), await bg());
check('...and Dark back', await setMenu('koan.look.mode', 'dark') &&
      await waitFor(async () => await bg() === 'rgb(9, 9, 11)'), await bg());
check('Base colour: Neutral (#0a0a0a)', await setMenu('koan.look.base', 'neutral') &&
      await waitFor(async () => await bg() === 'rgb(10, 10, 10)'), await bg());
await setMenu('koan.look.base', 'black');
const plain = await primary();
check('Accent colour: Blue changes the primary colour', await setMenu('koan.look.accent', 'blue') &&
      await waitFor(async () => await primary() !== plain), `${plain} -> ${await primary()}`);
check('...and None restores it', await setMenu('koan.look.accent', 'none') &&
      await waitFor(async () => await primary() === plain));
// The text area: Apply and Remove.
const paste = async text => {
  await evalIn(settings, `(() => {
    const r = ${row('koan.look.custom_css')};
    r.querySelector('textarea').value = ${JSON.stringify(text)};
    [...r.querySelectorAll('button')].find(b => b.textContent === 'Apply').click();
  })()`);
  await sleep(800);
};
await paste('body { color: red }');
check('Theme: a text that is no theme is refused, and said so',
      await prefOf('koan.look.custom_css') === '' && await evalIn(settings,
          `${row('koan.look.custom_css')}.querySelector('.theme-status').classList.contains('error')`));
await paste('.dark { --background: #0000ff; }');
check('Theme: a pasted theme applies', await waitFor(async () => await bg() === 'rgb(0, 0, 255)'),
      await bg());
await evalIn(settings, `[...${row('koan.look.custom_css')}.querySelectorAll('button')]
    .find(b => b.textContent === 'Remove').click()`);
check('...and Remove takes it away', await waitFor(async () =>
      await prefOf('koan.look.custom_css') === '' && await bg() === 'rgb(9, 9, 11)'), await bg());
skipped('Look rows: the window\'s own colours', IS_MAC
        ? 'need frame pixels, which only Linux\'s headless Ozone dumps (look.mjs)'
        : 'look.mjs checks the window\'s pixels');

// =========================================================================
// Browser Layout.
let w = await win();
const sidebarRight = v => v.find('VerticalTabStripRegionView').ax +
                          v.find('VerticalTabStripRegionView').w;
check('Layout: Only Sidebar by default, the URL pill in the sidebar',
      await prefOf('koan.single_toolbar') === true && !!w.shown('KoanUrlPill'));
check('Layout: "Sidebar and Top Toolbar" in the page shows the toolbar',
      await pickLayout('Sidebar and Top Toolbar', false) &&
      !!await waitWin(v => !v.shown('KoanUrlPill') && v.shown('ToolbarView')));
w = await win();
const top = w.shown('TopContainerView');
check('...above the page, beside the sidebar', top && top.ax >= sidebarRight(w) - 1,
      `toolbar x ${top?.ax}, sidebar right ${sidebarRight(w)}`);
const toolbarCardY = await cardY();
await key(testPage, VK.B, CTRL | ALT, 'b');
check('Layout: the page follows Ctrl+Alt+B',
      await waitFor(async () => await evalIn(settings,
          `${card('Only Sidebar')}.getAttribute('aria-checked')`) === 'true'));
check('...back to Only Sidebar: no toolbar, the pill in the sidebar',
      !!await waitWin(v => v.shown('KoanUrlPill')) && await cardY() < toolbarCardY,
      `card y ${await cardY()} (with the toolbar ${toolbarCardY})`);
skipped('Layout and What compact mode hides in macOS fullscreen',
        '#311\'s fullscreen_multiple and fullscreen_single');

// =========================================================================
// Compact mode: enable, what it hides, the flash popup, the toast.
const compact = 'koan.compact_mode.enabled';
check('Compact mode: the checkbox hides the sidebar', await setCheckbox(compact, true) &&
      await waitFor(async () => await sidebarWidth() === 0), `width ${await sidebarWidth()}`);
await key(testPage, VK.S, ACCEL, 's');
check('...the page follows the shortcut turning it off',
      await waitFor(() => evalIn(settings, `${row(compact)}.querySelector('input').checked === false`)) &&
      await waitFor(async () => await sidebarWidth() > 100));

// What it hides, in the Sidebar and Top Toolbar layout.
await pickLayout('Sidebar and Top Toolbar', false);
await waitWin(v => v.shown('ToolbarView'));
const withToolbar = await cardY();
await setCheckbox(compact, true);
const hides = async (value, sidebarHidden, toolbarHidden) => {
  const set = await setMenu('koan.compact_mode.hide', value);
  await sleep(1200);
  const width = await sidebarWidth(), y = await cardY();
  return { ok: set && (width === 0) === sidebarHidden && (y < withToolbar - 10) === toolbarHidden,
           detail: `sidebar ${width}, card y ${y} (toolbar shown: ${withToolbar})` };
};
for (const [value, label, sidebarHidden, toolbarHidden] of [
    ['toolbar', 'Hide Top bar: the toolbar goes, the sidebar stays', false, true],
    ['both', 'Hide Both: both go', true, true],
    ['sidebar', 'Hide Tab bar: the sidebar goes, the toolbar stays', true, false]]) {
  const r = await hides(value, sidebarHidden, toolbarHidden);
  check(`What compact mode hides: ${label}`, r.ok, r.detail);
}
await setCheckbox(compact, false);
await pickLayout('Only Sidebar', true);
await waitWin(v => v.shown('KoanUrlPill'));
// Only Sidebar has no top bar: every choice hides the sidebar (the row's note).
await setMenu('koan.compact_mode.hide', 'toolbar');
await setCheckbox(compact, true);
check('...with Only Sidebar, Hide Top bar hides the sidebar (as its note says)',
      await waitFor(async () => await sidebarWidth() === 0) &&
      await noteShown('koan.compact_mode.hide'), `width ${await sidebarWidth()}`);
await setMenu('koan.compact_mode.hide', 'sidebar');

// The flash popup: a selected tab pops the hidden sidebar up for a moment.
const flash = 'koan.compact_mode.toolbar_flash_popup';
const flashes = async () => {
  const tab = await openTab(`data:text/html,${encodeURIComponent('<title>Flash</title>')}`);
  const widths = [];
  for (let i = 0; i < 8; i++) {
    widths.push(await sidebarWidth());
    await sleep(100);
  }
  await sleep(1500);
  const after = await sidebarWidth();
  await closeTab(tab);
  await front(testPage);
  await sleep(1500);
  return { popped: Math.max(...widths) > 100, after, widths };
};
check('Flash popup: on in the page', await setCheckbox(flash, true));
let f = await flashes();
check('...a new tab pops the sidebar up, and it slides away again', f.popped && f.after === 0,
      `${f.widths.join(',')} then ${f.after}`);
await setCheckbox(flash, false);
f = await flashes();
check('Flash popup off: a new tab leaves the sidebar hidden', !f.popped, f.widths.join(','));

// The background tab toast.
const toast = 'koan.compact_mode.show_background_tab_toast';
const toastShown = async () => {
  const before = new Set((await pages()).map(p => p.id));
  await clickLink(testPage, 'background', ACCEL);
  const v = await win();
  const text = v.vs.filter(x => x.path.includes('KoanToastView') && x.cls === 'Label' && x.shown)
      .map(x => x.props.Text).join(' ');
  for (const p of (await pages()).filter(p => !before.has(p.id))) await closeTab(p);
  await sleep(2500);   // the toast goes
  await front(testPage);
  return text;
};
check('Background tab toast: on by default', await prefOf(toast) === true);
let text = await toastShown();
check('...a link opened in the background shows Zen\'s toast',
      text === 'New background tab opened!', text);
check('...off in the page', await setCheckbox(toast, false));
text = await toastShown();
check('...then no toast', text === '', text);
await setCheckbox(toast, true);
await setCheckbox(compact, false);
await waitFor(async () => await sidebarWidth() > 100);

// =========================================================================
// Show the bookmarks bar: below the toolbar with Sidebar and Top Toolbar;
// docked above the page with Only Sidebar (Zen's top bar, 34 high), beside
// the sidebar. (UI DevTools gives a view's bounds without its layer's
// transform: with Only Sidebar the top container is shifted up so only the
// bar shows, so there the bar's room is read from the card and the sidebar.)
const bookmarks = 'koan.bookmarks_bar.show';
const barOf = v => v.shown('BookmarkBarView');
async function checkBar(when, single) {
  const v = await waitWin(x => barOf(x) && x);
  const bar = v && barOf(v), card = v && v.shown('ContentsContainerView');
  const bv = v && v.shown('BrowserView');
  if (!single) {
    const toolbar = v && v.shown('ToolbarView');
    check(`${when}: the bar shows below the toolbar, above the page`,
          bar && toolbar && bar.h > 0 && bar.ay >= toolbar.ay + toolbar.h - 1 &&
          bar.ay + bar.h <= card.ay,
          `bar ${bar?.ay}+${bar?.h}, toolbar ${toolbar?.ay}+${toolbar?.h}, card ${card?.ay}`);
  } else {
    check(`${when}: the bar shows docked above the page, beside the sidebar`,
          bar && bar.h > 0 && card.ay - bv.ay >= bar.h && bar.ax >= sidebarRight(v) - 1,
          `bar ${bar?.ax},${bar?.h}, card top ${card && card.ay - bv.ay}, ` +
          `sidebar right ${v && sidebarRight(v)}`);
  }
}
await pickLayout('Sidebar and Top Toolbar', false);
await waitWin(v => v.shown('ToolbarView'));
const noBar = await cardY();
check('Bookmarks bar: none by default', !barOf(await win()));
check('...on in the page', await setCheckbox(bookmarks, true));
await checkBar('...Sidebar and Top Toolbar', false);
check('...the page moves down for it', await cardY() > noBar, `${await cardY()} (${noBar})`);
await pickLayout('Only Sidebar', true);
await waitWin(v => v.shown('KoanUrlPill'));
await checkBar('...Only Sidebar', true);
const singleWith = await cardY();
await setCheckbox(compact, true);
await sleep(1200);
await checkBar('...Only Sidebar in compact mode', true);
await setCheckbox(compact, false);
await waitFor(async () => await sidebarWidth() > 100);
// macOS fullscreen keeps it in both layouts (Koan's layout keeps the top
// container in the window, browser_view.cc.patch IsBookmarkBarVisible()).
if (IS_MAC) {
  for (const [label, single] of [['Only Sidebar', true], ['Sidebar and Top Toolbar', false]]) {
    await pickLayout(label, single);
    await sleep(1000);
    await goFullscreen(true, testPage.id);
    await checkBar(`...${label} in fullscreen`, single);
    await goFullscreen(false, testPage.id);
  }
  await pickLayout('Only Sidebar', true);
  await waitWin(v => v.shown('KoanUrlPill'));
} else {
  skipped('Bookmarks bar in fullscreen', 'macOS only (Koan keeps its card there)');
}
check('...off in the page: gone, the page back at the top',
      await setCheckbox(bookmarks, false) && !!await waitWin(v => !barOf(v)) &&
      await cardY() < singleWith, `${await cardY()} (with the bar ${singleWith})`);

// ⇧⌘B is View > Always Show Bookmarks Bar's key: run that menu item (the
// command, IDC_SHOW_BOOKMARK_BAR) as AppKit does, through the menu bar
// (System Events), or with a real ⇧⌘B; CDP keys never reach the menu bar.
async function menuShowBookmarksBar() {
  if (!IS_MAC || !KOAN_PID) return false;
  try {
    execFileSync('osascript', ['-e', `tell application "System Events"
      set p to first process whose unix id is ${KOAN_PID}
      set frontmost of p to true
      tell p to click (first menu item of menu 1 of menu bar item "View" of menu bar 1 ` +
        `whose name contains "Bookmarks Bar")
    end tell`], { stdio: 'ignore', timeout: 8000 });
    return true;
  } catch {
    return realKey('b', 'cs', 800);
  }
}
if (await menuShowBookmarksBar()) {
  check('⇧⌘B (View > Always Show Bookmarks Bar) turns Koan\'s setting on',
        await waitFor(async () => await prefOf(bookmarks) === true) &&
        !!await waitWin(v => barOf(v)));
  check('...the settings page shows it',
        await waitFor(() => evalIn(settings, `${row(bookmarks)}.querySelector('input').checked`)));
  await menuShowBookmarksBar();
  check('...and again off', await waitFor(async () => await prefOf(bookmarks) === false));
} else {
  skipped('⇧⌘B / View > Always Show Bookmarks Bar',
          IS_MAC ? `neither System Events nor real input can reach the menu bar (${realInputWhyNot()})`
                 : 'the macOS menu bar item');
}
await setCheckbox(bookmarks, false);

// =========================================================================
// Sidebar and tabs layout: the New Tab row, its place, tabs on the right.
const newTabRow = v => v.views.find(x => x.cls === 'KoanNewTabRow' && x.shown && x.h > 0);
const tabRows = v => v.views.filter(x => x.cls === 'TabView' && x.shown && x.h > 0 &&
                                         x.path.includes('UnpinnedTabContainerView'));
check('Show New Tab Button: off in the page hides the New Tab row',
      await setCheckbox('koan.tabs.show_newtab_vertical', false) &&
      !!await waitWin(v => !newTabRow(v)));
check('...on shows it', await setCheckbox('koan.tabs.show_newtab_vertical', true) &&
      !!await waitWin(v => newTabRow(v)));
w = await win();
check('Move the new tab button to the top: on by default, above the tabs',
      newTabRow(w) && tabRows(w).length && newTabRow(w).ay < tabRows(w)[0].ay,
      `row y ${newTabRow(w)?.ay}, first tab ${tabRows(w)[0]?.ay}`);
await setCheckbox('koan.view.show_newtab_button_top', false);
w = await waitWin(v => newTabRow(v) && tabRows(v).length &&
                       newTabRow(v).ay > tabRows(v).at(-1).ay && v);
check('...off in the page puts it below the tabs', !!w,
      `row y ${newTabRow(await win())?.ay}`);
await setCheckbox('koan.view.show_newtab_button_top', true);

const onRight = 'koan.tabs.on_right';
const stripOnRight = v => {
  const s = v.find('VerticalTabStripRegionView'), b = v.shown('BrowserView');
  return s && b && s.ax > b.ax + b.w / 2;
};
check('Tabs on the right: on in the page moves the sidebar to the right',
      await setCheckbox(onRight, true) && !!await waitWin(stripOnRight));
const windowsBefore = (await browserWidgets(await viewTree())).length;
const other = (await browserCall('Target.createTarget',
    { url: server.url('/p?t=Window'), newWindow: true })).result?.targetId;
await sleep(2500);
const inNew = await waitFor(async () => {
  const vs = await viewTree();
  const widgets = await browserWidgets(vs);
  if (widgets.length < 2) return null;
  const v = widgets.at(-1).views;
  const s = v.find(x => x.cls === 'VerticalTabStripRegionView');
  const b = v.find(x => x.cls === 'BrowserView' && x.shown);
  return s && b ? { right: s.ax > b.ax + b.w / 2 } : null;
}, 6000);
check('...a new window opens with it on the right too', inNew?.right === true,
      `${windowsBefore} window(s) before, ${JSON.stringify(inNew)}`);
if (other) await browserCall('Target.closeTarget', { targetId: other });
await sleep(1500);
check('...off moves it back to the left', await setCheckbox(onRight, false) &&
      !!await waitWin(v => !stripOnRight(v)));

// =========================================================================
// Window material, window dragging, haptics.
if (await evalIn(settings, `!!${row('glass_frame.enabled')}`)) {
  const before = await prefOf('glass_frame.enabled');
  check('Translucent window: the page switches it', await setCheckbox('glass_frame.enabled', !before) &&
        await setCheckbox('glass_frame.enabled', before));
  skipped('Translucent window: the blur', 'an NSVisualEffectView, outside the views tree');
} else {
  skipped('Translucent window', 'no row here (macOS 26 or later only)');
}

async function drag() {
  await front(testPage);
  await evalIn(testPage, 'seen.length = 0');
  let before;
  await onPage(testPage, async c => {
    const send = (type, y, extra = {}) =>
        c.send('Input.dispatchMouseEvent', { type, x: 300, y, button: 'left', ...extra });
    await send('mouseMoved', 10, { button: 'none' });
    await send('mousePressed', 10, { buttons: 1, clickCount: 1 });
    await sleep(300);
    for (let dy = 2; dy <= 40; dy += 2) await send('mouseMoved', 10 + dy, { buttons: 1 });
    await sleep(300);
    before = await evalIn(testPage, 'seen.join(" ")');
    await send('mouseReleased', 50, { buttons: 0, clickCount: 1 });
  });
  return before.replace(/(move ?)+/g, 'move ').trim();
}
const dragPref = 'koan.view.drag_window_from_content';
if (await evalIn(settings, `!!${row(dragPref)}`)) {
  check('Window dragging: on by default, a drag at the top of a page moves the window',
        await prefOf(dragPref) === true && await drag() === 'down move up');
  check('...off in the page, the page keeps the drag', await setCheckbox(dragPref, false) &&
        await drag() === 'down move');
  await setCheckbox(dragPref, true);
} else {
  skipped('Window dragging', 'off in chrome://flags');
}
skipped('Haptic feedback', 'a trackpad tick: nothing UI DevTools or CDP can see');

// =========================================================================
// Glance: enable, trigger, Essentials' external links, context menu search.
const glanceOpen = async (timeout = 2500) =>
  !!await waitFor(async () => (await viewTree()).some(v => v.cls === 'KoanGlanceView'), timeout);
async function closeGlance() {
  const page = (await pages()).find(p => p.url.includes('t=Glanced') || p.url.includes('t=Elsewhere'));
  if (page) await key(page, VK.ESC, 0, 'Escape');
  await waitFor(async () => !(await viewTree()).some(v => v.cls === 'KoanGlanceView'), 4000);
  await sleep(500);
}
check('Glance: off in the page', await setCheckbox('koan.glance.enabled', false));
check('...greys out its trigger', await isDisabled('koan.glance.activation_method') === true);
await freshTestPage();
await clickLink(testPage, 'glance', ALT);
check('...Alt+click in a page then opens no Glance', !(await glanceOpen(1500)));
check('Glance: on again', await setCheckbox('koan.glance.enabled', true));
await freshTestPage();
await clickLink(testPage, 'glance', ALT);
check('...Alt+click in a page opened since opens Glance', await glanceOpen());
await closeGlance();
check('Trigger method: Shift + Click in the page',
      await setMenu('koan.glance.activation_method', 'shift'));
await freshTestPage();
await clickLink(testPage, 'glance', SHIFT);
check('...Shift+click then opens Glance', await glanceOpen());
await closeGlance();
await setMenu('koan.glance.activation_method', 'alt');
await freshTestPage();

// Links from a pinned tab to another site open in Glance.
const external = 'koan.glance.open_essential_external_links';
await key(testPage, VK.D, ACCEL | SHIFT, 'd');   // pin the test tab
await sleep(1000);
await clickLink(testPage, 'external');
check('Pinned tabs\' links to other sites: open in Glance by default', await glanceOpen(),
      (await pages()).map(p => p.url).join(' '));
await closeGlance();
check('...off in the page', await setCheckbox(external, false));
const before = new Set((await pages()).map(p => p.id));
await clickLink(testPage, 'external');
const opened = (await pages()).filter(p => !before.has(p.id));
check('...then they open in a new tab', !(await glanceOpen(1000)) &&
      opened.some(p => p.url.includes('t=Elsewhere')), opened.map(p => p.url).join(' '));
for (const p of opened) await closeTab(p);
await setCheckbox(external, true);
await front(testPage);
await key(testPage, VK.D, ACCEL | SHIFT, 'd');   // unpin
await sleep(1000);
skipped('Open "Search for" from the context menu in Glance',
        IS_MAC ? 'the page\'s context menu is a native NSMenu on macOS'
               : 'needs the page\'s context menu (glance_activation.mjs)');

// =========================================================================
// URL bar: behaviour, replace new tab, copy URL button, glass URL bar.
await freshTestPage();
const floating = v => !!v.vs.find(x => x.cls === 'KoanUrlBarBackdrop' && x.shown);
async function openBar() {
  const pill = (await win()).shown('KoanUrlPill');
  if (pill) {
    await viewMouse(pill, 'mousePressed', 30, pill.h / 2);
    await viewMouse(pill, 'mouseReleased', 30, pill.h / 2);
    await sleep(1000);
  }
  return win();
}
// Types `text` into the open URL bar.
async function typeInBar(text) {
  const field = (await win()).views.find(x => x.cls === 'OmniboxViewViews' && x.shown);
  if (field) await typeInto(field, text);
  return win();
}
// Whether the URL bar is closed: the pill shows the page again (it is
// empty while the bar edits over it) and nothing floats.
const pillText = v => v.views.find(x => x.cls === 'Label' && x.path.at(-1) === 'KoanUrlPill')
    ?.props.Text;
const barClosed = v => !floating(v) && !!pillText(v);
// Esc to the window with the omnibox, pressed and released, as urlbar.mjs
// does (Zen: one Esc closes the bar; a second clears nothing), until the bar
// is closed.
async function windowEsc() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const has = (n, cls) => {
    const a = n.attributes ?? [];
    return a[a.indexOf('class') + 1] === cls || (n.children ?? []).some(c => has(c, cls));
  };
  const findWidget = n => n.nodeName === 'Widget' && has(n, 'OmniboxViewViews') ? n
      : (n.children ?? []).map(findWidget).find(Boolean);
  const widget = findWidget(doc);
  for (const type of widget ? ['keyPressed', 'keyReleased'] : []) {
    await ui.send('DOM.dispatchKeyEvent', { nodeId: widget.nodeId,
        event: { type, keyCode: VK.ESC, code: 0, flags: 0, key: 0, isChar: false } });
  }
  ui.close();
  await sleep(800);
}
async function closeBar() {
  for (let i = 0; i < 3 && !barClosed(await win()); i++) {
    await windowEsc();
  }
  await front(testPage);
}
const behavior = 'koan.urlbar.behavior';
check('URL bar Behavior: Floating only when typing by default',
      await prefOf(behavior) === 'floating-on-type');
check('...a click edits in the pill', !floating(await openBar()));
// Zen's test_Click_Shoudnt_FLoat_Urlbar: a bar a click opened never floats,
// not even once you type (urlbar.mjs); the keyboard's does ("+ New Tab"
// below).
check('...typing there keeps it in the pill (a click never floats it)',
      !floating(await typeInBar('k')));
await closeBar();
check('Behavior: Always floating, in the page', await setMenu(behavior, 'float'));
check('...a click floats it', floating(await openBar()));
await closeBar();
check('Behavior: Normal', await setMenu(behavior, 'normal'));
await openBar();
check('...typing keeps it in the pill', !floating(await typeInBar('k')));
await closeBar();
await setMenu(behavior, 'floating-on-type');

const replace = 'koan.urlbar.replace_newtab';
const pageCount = async () => (await pages()).length;
async function newTabRowClick() {
  const r = newTabRow(await win());
  await viewMouse(r, 'mousePressed', 20, r.h / 2);
  await viewMouse(r, 'mouseReleased', 20, r.h / 2);
  await sleep(1200);
}
let count = await pageCount();
await newTabRowClick();
check('Replace new tab: on by default, "+ New Tab" opens the URL bar, no tab',
      floating(await win()) && await pageCount() === count, `${count} -> ${await pageCount()}`);
await closeBar();
check('...off in the page', await setCheckbox(replace, false));
count = await pageCount();
await newTabRowClick();
const newTab = await waitFor(async () => (await pages()).length > count &&
    (await pages()).find(p => /newtab/.test(p.url)));
check('...then "+ New Tab" opens a tab', !!newTab, (await pages()).map(p => p.url).join(' '));
await closeTab(newTab);
await setCheckbox(replace, true);
await front(testPage);

const copyButton = v => v.views.find(x => x.cls === 'ImageButton' && x.shown &&
                                          x.path.at(-1) === 'KoanUrlPill' &&
                                          x.props.TooltipText === 'Copy URL');
// (Zen: only for a page, and not while the bar edits over the pill.)
check('Copy URL button: in the pill by default, for a page',
      !!await waitWin(v => barClosed(v) && copyButton(v)), pillText(await win()));
check('...off in the page removes it at once', await setCheckbox('koan.urlbar.show_copy_url', false) &&
      !!await waitWin(v => !copyButton(v)));
check('...on brings it back', await setCheckbox('koan.urlbar.show_copy_url', true) &&
      !!await waitWin(v => copyButton(v)));

const glassBar = 'koan.look.acrylic_urlbar';
const cardGlass = v => v.vs.find(x => x.cls === 'KoanUrlBarCardFill')?.props.Glass;
await setMenu(behavior, 'float');
check('Glass URL bar: on in the page', await setCheckbox(glassBar, true));
const glassOn = cardGlass(await openBar());
await closeBar();
check('...the floating card is glass', glassOn === 'true',
      `Glass ${glassOn} (false also with Reduce Transparency on, or the window inactive)`);
await setCheckbox(glassBar, false);
const glassOff = cardGlass(await openBar());
await closeBar();
check('...off: an opaque card', glassOff === 'false', `Glass ${glassOff}`);
await setMenu(behavior, 'floating-on-type');

// =========================================================================
// New tab page.
async function newTabHref() {
  const page = await openTab('chrome://newtab', true);
  const href = await waitFor(async () => {
    const h = await evalIn(page, 'location.href');
    return h && h !== 'chrome://newtab/' && h;
  });
  await closeTab(page);
  return href;
}
check('New tab page: Blank page by default (Koan\'s)', await newTabHref() === 'chrome://koan-newtab/');
check('...Chromium\'s, in the page', await setMenu('koan.newtab.page', 'chromium') &&
      (await newTabHref())?.startsWith('chrome://new-tab-page'));
await setMenu('koan.newtab.page', 'koan');

skipped('Every row after a restart', 'prefs are read when a window is made (the new-window ' +
        'check above); spaces_relaunch covers restarts');
await closeTab(testPage);
server.close();
finish();
