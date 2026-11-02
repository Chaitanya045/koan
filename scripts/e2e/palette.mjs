// End-to-end test of the command palette (Zen's actions in the URL bar)
// against a real build.
//
//   scripts/e2e/run.sh palette
//
// Expects Koan running off-screen with UI DevTools on UI_PORT and the
// DevTools protocol on CDP_PORT (run.sh starts it, with the profile seeded by
// scripts/e2e/prefs/palette.json: "New Split View" was passed over five
// times; Ctrl+T opens a tab). Text is typed into the omnibox as character
// events; Tab, Enter, arrows and Esc go to the browser window's widget, so
// they take the same path as real keys (pre-target handlers, focus manager,
// focused view). The rows are read from Chromium's WebUI popup. At the end Koan is restarted on
// the same profile to check the opt-outs. Prints one PASS/FAIL line per
// check; exits non-zero on any failure.
import { spawn } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
import { createSpace, visiblePage } from './lib/koan.mjs';

const UI_PORT = process.env.UI_PORT;
const CDP_PORT = process.env.CDP_PORT;
const sleep = ms => new Promise(r => setTimeout(r, ms));

// --- protocol helpers --------------------------------------------------------
async function connect(url) {
  const ws = new WebSocket(url);
  let id = 0;
  const pending = new Map();
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  };
  // Fails instead of hanging when the browser is gone.
  await new Promise((resolve, reject) => {
    ws.onopen = resolve;
    ws.onerror = () => reject(new Error(`can't connect to ${url} (did Koan crash?)`));
  });
  const send = (method, params = {}) => new Promise(resolve => {
    pending.set(++id, resolve);
    ws.send(JSON.stringify({ id, method, params }));
    setTimeout(() => resolve({ timedOut: true }), 5000);
  });
  return { send, close: () => ws.close() };
}

// Polls `get` until `ok` holds for its value (or the time is up), and
// returns the last value.
async function waitFor(get, ok, ms = 6000) {
  let value;
  for (const end = Date.now() + ms; ;) {
    value = await get();
    if (ok(value) || Date.now() > end) return value;
    await sleep(250);
  }
}

// The native UI: the browser window's widget node, the omnibox and every
// view as {cls, shown, w, h}.
async function ui() {
  const c = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await c.send('DOM.enable');
  await c.send('CSS.enable');
  const doc = (await c.send('DOM.getDocument', { depth: -1 })).result.root;
  const out = { widget: null, omnibox: null, views: [] };
  const hasBrowserView = n => (n.children ?? []).some(
      ch => (ch.attributes ?? []).includes('BrowserView') || hasBrowserView(ch));
  const walk = async (n, shown) => {
    const a = n.attributes ?? [];
    const cls = a[a.indexOf('class') + 1];
    if (n.nodeName === 'Widget' && !out.widget && hasBrowserView(n)) out.widget = n.nodeId;
    if (n.nodeName === 'View' && cls) {
      const r = await c.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
      const p = {};
      for (const rule of r.result?.matchedCSSRules ?? []) {
        for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
      }
      shown = shown && p.Visible !== 'false';
      out.views.push({ cls, shown, w: +p.Width, h: +p.Height });
      if (cls === 'OmniboxViewViews' && !out.omnibox) out.omnibox = { nodeId: n.nodeId, text: p.Text };
    }
    for (const ch of n.children ?? []) await walk(ch, shown);
  };
  await walk(doc, true);
  c.close();
  return out;
}

async function targets() {
  return (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json();
}

// Tabs, not the browser's own WebUI (the popup, the side panel...).
async function pages() {
  return (await targets()).filter(t => t.type === 'page' && !t.url.includes('.top-chrome'));
}

async function evalIn(page, expression) {
  const c = await connect(page.webSocketDebuggerUrl);
  const r = await c.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  c.close();
  return r.result?.result?.value;
}

// The suggestions, from Chromium's WebUI popup (chrome://omnibox-popup), as
// {texts: [primary, secondary], selected, search, a11y, icon (the start of
// its icon URL)}. The palette's rows are those with a PNG data icon (ours()).
async function rows() {
  const popup = (await targets()).find(t => t.url.startsWith('chrome://omnibox-popup'));
  if (!popup) return [];
  return (await evalIn(popup, `(() => {
    const matches = [];
    const walk = root => {
      for (const el of root.querySelectorAll('*')) {
        if (el.localName === 'cr-searchbox-match') matches.push(el);
        if (el.shadowRoot) walk(el.shadowRoot);
      }
    };
    walk(document);
    // mojo_base.mojom.String16 may arrive as {data}, url.mojom.Url as {url}.
    const s16 = v => typeof v === 'string' ? v : String.fromCharCode(...(v?.data ?? []));
    return matches.filter(m => m.getClientRects().length).map(m => {
      const text = id => m.shadowRoot.getElementById(id)?.textContent.trim() ?? '';
      return {
        texts: [text('primaryText'), text('secondaryText')].filter(Boolean),
        selected: m.hasAttribute('selected'),
        search: !!m.match?.isSearchType,
        a11y: s16(m.match?.a11yLabel),
        icon: String(m.match?.iconUrl?.url ?? m.match?.iconUrl ?? '').slice(0, 22),
      };
    });
  })()`)) ?? [];
}

const ours = rs => rs.filter(r => r.icon === 'data:image/png;base64,');
const label = row => row?.texts[0];
const indexOf = (rs, name) => rs.findIndex(r => label(r) === name);
const rowsText = rs => rs.map(r => r.texts.join(' | ')).join(' / ');

async function uiKey(nodeId, event) {
  const c = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await c.send('DOM.enable');
  await c.send('DOM.getDocument', { depth: -1 });
  for (const type of event.isChar ? ['keyPressed'] : ['keyPressed', 'keyReleased']) {
    await c.send('DOM.dispatchKeyEvent', { nodeId, event: { code: 0, flags: 0, key: 0, isChar: false, ...event, type } });
  }
  c.close();
}

// A key for the browser window, like a real key press.
async function press(keyCode, flags = 0) {
  await uiKey((await ui()).widget, { keyCode, flags });
}

// The omnibox's node in a UI DevTools document. Node ids are handed out in
// tree order on each DOM.getDocument, and the first keystroke can move the
// omnibox (Zen's URL bar floats on typing, adding a view before it): look it
// up in the session that uses it.
const omniboxNode = n => {
  const a = n.attributes ?? [];
  if (n.nodeName === 'View' && a[a.indexOf('class') + 1] === 'OmniboxViewViews') return n.nodeId;
  for (const c of n.children ?? []) {
    const id = omniboxNode(c);
    if (id) return id;
  }
  return null;
};

// Types into the omnibox, and waits for the box to show it.
async function type(text) {
  for (const ch of text) {
    const c = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
    await c.send('DOM.enable');
    const doc = (await c.send('DOM.getDocument', { depth: -1 })).result.root;
    await c.send('DOM.dispatchKeyEvent', { nodeId: omniboxNode(doc),
        event: { type: 'keyPressed', keyCode: 0, code: 0, flags: 0,
                 key: ch.codePointAt(0), isChar: true } });
    c.close();
  }
  await waitFor(ui, u => u.omnibox?.text?.endsWith(text));
}

// Types `text` and waits for the popup to have settled on rows `ok` accepts.
async function suggest(text, ok) {
  await type(text);
  return waitFor(rows, rs => rs.length > 0 && ok(rs));
}

// Sends a key press to the active page; unhandled keys reach the browser's
// accelerators. Modifiers: Alt=1 Ctrl=2 Shift=8.
async function pageKey(code, modifiers, keyName = '') {
  const c = await connect((await pages())[0].webSocketDebuggerUrl);
  const base = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers, key: keyName };
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
  c.close();
}

async function openUrlBar() {
  await pageKey(VK.L, CTRL, 'l');
  await waitFor(ui, u => u.omnibox?.text !== undefined);
  await sleep(300);
}

async function closeUrlBar() {
  await press(VK.ESC);
  await press(VK.ESC);
  await waitFor(rows, rs => rs.length === 0, 3000);
}

// Reads the clipboard by pasting (Ctrl+V) into a fresh textarea on the page.
async function clipboard() {
  const page = (await pages())[0];
  await evalIn(page, `(() => {
    const t = document.createElement('textarea');
    t.id = 'koan-paste';
    document.body.append(t);
    t.focus();
  })()`);
  await pageKey(86, 2, 'v');
  return waitFor(() => evalIn(page, `document.getElementById('koan-paste').value`), Boolean, 3000);
}

const compactOn = async () => (await ui()).views.find(v => v.cls === 'VerticalTabStripRegionView')?.w === 0;

// The key the settings page shows for a shortcut (koan_shortcut_table).
async function settingsShortcut(id) {
  const v = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
  const b = await connect(v.webSocketDebuggerUrl);
  const { targetId } = (await b.send('Target.createTarget',
      { url: 'chrome://koan-settings/', background: true })).result;
  const page = await waitFor(async () => (await targets()).find(t => t.id === targetId), Boolean);
  const value = await waitFor(() => evalIn(page, `(() => {
    document.querySelector('#category-shortcuts')?.click();
    return document.querySelector('#shortcut-${id}')?.value;
  })()`), Boolean);
  await b.send('Target.closeTarget', { targetId });
  b.close();
  return value;
}

// --- checks ------------------------------------------------------------------
let failures = 0;
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
}

const CTRL = 2, ALT = 1;
const VK = { TAB: 9, ENTER: 13, ESC: 27, DOWN: 40, BACKSPACE: 8, L: 76, N: 78, T: 84 };

const compactKey = await settingsShortcut('compact-mode-toggle');

// Typed text suggests the matching action right after the first suggestion,
// with its shortcut, its icon and a full accessible name.
await openUrlBar();
let rs = await suggest('toggle compact', rs => indexOf(rs, 'Toggle Compact Mode') >= 0);
let row = rs[indexOf(rs, 'Toggle Compact Mode')];
check('Typing suggests the matching action', !!row, rowsText(rs));
check('...right below the first suggestion', indexOf(rs, 'Toggle Compact Mode') === 1,
      `row ${indexOf(rs, 'Toggle Compact Mode')}`);
check('...with its shortcut as in the settings page', row?.texts[1] === compactKey,
      `${JSON.stringify(row?.texts)} vs ${compactKey}`);
check('...its own icon', row?.icon === 'data:image/png;base64,', JSON.stringify(row?.icon));
check('...and the shortcut in its accessible name',
      row?.a11y.includes('Toggle Compact Mode') && row?.a11y.includes(compactKey),
      JSON.stringify(row?.a11y));
await closeUrlBar();
await openUrlBar();
rs = await suggest('new', rs => ours(rs).length > 0);
check('At most three actions while typing', ours(rs).length > 0 && ours(rs).length <= 3,
      rowsText(ours(rs)));
await closeUrlBar();

// Down and Enter run it.
await openUrlBar();
await suggest('toggle compact', rs => indexOf(rs, 'Toggle Compact Mode') === 1);
await press(VK.DOWN);
check('Down selects the action', (await waitFor(rows, rs => rs[1]?.selected))[1]?.selected === true);
await press(VK.ENTER);
check('Enter runs it (compact mode on)', await waitFor(compactOn, Boolean));

// Once used, it comes first, but Enter on part of its name still searches
// (it would open a site whose address starts the same); its whole name runs
// it.
await openUrlBar();
rs = await suggest('toggle compact', rs => indexOf(rs, 'Toggle Compact Mode') >= 0);
check('A used action stays a suggestion, not the default, for part of its name',
      indexOf(rs, 'Toggle Compact Mode') === 1 && !ours([rs[0]]).length, rowsText(rs));
await closeUrlBar();
await openUrlBar();
rs = await suggest('toggle compact mode', rs => label(rs[0]) === 'Toggle Compact Mode');
check('...but its whole name makes it the default', label(rs[0]) === 'Toggle Compact Mode',
      rowsText(rs));
await press(VK.ENTER);
check('...and Enter runs it (compact mode off)', await waitFor(compactOn, on => !on) === false);

// Addresses still open.
await openUrlBar();
await suggest('example.com/?typed', rs => rs[0]?.texts[0]?.includes('example.com'));
await press(VK.ENTER);
const url = await waitFor(async () => (await pages())[0]?.url, u => u?.endsWith('?typed'));
check('A typed address opens as usual', /^https?:\/\/example\.com\/\?typed$/.test(url), url);

// Short or URL-like text suggests no actions.
await openUrlBar();
rs = await suggest('to', () => true);
check('Two characters suggest no actions', ours(rs).length === 0, rowsText(ours(rs)));
await closeUrlBar();
await openUrlBar();
rs = await suggest('print:x', () => true);
check('Protocol-like text suggests no actions', ours(rs).length === 0, rowsText(ours(rs)));
await closeUrlBar();

// A passed-over action moves down (seeded: "New Split View" five times):
// without that it would come first, as it does in Zen's list.
await openUrlBar();
rs = await suggest('new s', rs => indexOf(rs, 'New Space') >= 0 && indexOf(rs, 'New Split View') >= 0);
// ...and below the search suggestions, if the search engine sent any.
const firstSearch = rs.findIndex((r, i) => i > 1 && r.search);
check('A passed-over action moves down',
      indexOf(rs, 'New Space') === 1 && indexOf(rs, 'New Split View') > 1 &&
      (firstSearch < 0 || firstSearch < indexOf(rs, 'New Split View')), rowsText(rs));
await closeUrlBar();

// Tab in an empty URL bar lists every action; Enter runs the first only once
// two characters are typed; Tab again leaves.
await openUrlBar();
await press(VK.BACKSPACE);
await press(VK.TAB);
let u = await waitFor(ui, u => u.omnibox?.text === '>');
check('Tab in an empty URL bar switches to actions (">")', u.omnibox?.text === '>',
      JSON.stringify(u.omnibox?.text));
rs = await waitFor(rows, rs => ours(rs).length >= 6);
check('...listing actions', ours(rs).length >= 6, rowsText(rs));
check('...none of which Enter would run yet', !ours([rs[0]]).length, rowsText(rs));
await press(VK.TAB);
u = await waitFor(ui, u => u.omnibox?.text === '');
check('A second Tab leaves', u.omnibox?.text === '', JSON.stringify(u.omnibox?.text));
await press(VK.TAB);
rs = await suggest('c', rs => indexOf(rs, 'Copy Current URL') >= 0);
check('One character doesn\'t pick an action', !ours([rs[0]]).length, rowsText(rs));
// The best match, most used first (Zen's order): Toggle Compact Mode.
rs = await suggest('o', rs => ours([rs[0]]).length > 0);
check('Two do', ours([rs[0]]).length > 0, rowsText(rs));
rs = await suggest('py', rs => label(rs[0]) === 'Copy Current URL');
const pageUrl = (await pages())[0].url;
await press(VK.ENTER);
await sleep(500);
const clip = await clipboard();
check('...and Enter runs it (URL copied)', clip === pageUrl, JSON.stringify(clip));

// Availability follows the state, like Zen's: only the other appearances.
await openUrlBar();
rs = await suggest('>dark mode', rs => label(rs[0]) === 'Switch to Dark Mode');
check('"Switch to Dark Mode" is offered', label(rs[0]) === 'Switch to Dark Mode', rowsText(rs));
await press(VK.ENTER);
await sleep(800);
await openUrlBar();
rs = await suggest('>mode', rs => indexOf(rs, 'Switch to Light Mode') >= 0);
check('...and not once dark', indexOf(rs, 'Switch to Dark Mode') < 0 &&
      indexOf(rs, 'Switch to Light Mode') >= 0, rowsText(rs));
await closeUrlBar();

// "Focus on" another Space, found by its name, announced with it.
const tabsShown = async () => (await ui()).views.filter(v => v.cls === 'TabView' && v.shown && v.h > 1).length;
await createSpace(await visiblePage(), 'Space 1');  // Zen's Create form
await waitFor(tabsShown, n => n === 1);
await pageKey(VK.T, CTRL, 't');         // a second tab in it
await waitFor(tabsShown, n => n === 2);
await createSpace(await visiblePage(), 'Space 2');  // now active
await waitFor(tabsShown, n => n === 1);
await openUrlBar();
rs = await suggest('space 1', rs => rs.some(r => label(r) === 'Focus on'));
const focus = rs.findIndex(r => label(r) === 'Focus on' && r.texts[1]?.includes('Space 1'));
check('"Focus on" the other Space is suggested', focus > 0, rowsText(rs));
check('...announced with the Space\'s name', rs[focus]?.a11y.includes('Focus on') &&
      rs[focus]?.a11y.includes('Space 1'), JSON.stringify(rs[focus]?.a11y));
for (let i = 0; i < focus; i++) await press(VK.DOWN);
await waitFor(rows, rs => rs[focus]?.selected);
await press(VK.ENTER);
check('...and Enter switches to it', await waitFor(tabsShown, n => n === 2) === 2);

// Opt-outs, after a restart on the same profile: the pref stops the typed
// suggestions but not the ">" list; the flag stops both.
const args = process.env.KOAN_CHROME_ARGS.split('\n').filter(Boolean);
const profile = args.find(a => a.startsWith('--user-data-dir=')).split('=')[1];
let relaunched = null;
process.on('exit', () => relaunched?.kill());
async function restart(extraArgs, editPrefs) {
  const v = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
  const b = await connect(v.webSocketDebuggerUrl);
  await b.send('Browser.close');
  b.close();
  await waitFor(() => fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).then(() => true, () => false),
                up => !up, 20000);
  await sleep(2000);
  const file = `${profile}/Default/Preferences`;
  const prefs = JSON.parse(readFileSync(file, 'utf8'));
  editPrefs(prefs);
  writeFileSync(file, JSON.stringify(prefs));
  relaunched?.kill();
  relaunched = spawn(args[0], [...args.slice(1), ...extraArgs, 'https://example.com/'], { stdio: 'ignore' });
  await waitFor(() => fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).then(() => true, () => false),
                Boolean, 30000);
  await waitFor(async () => (await pages()).length, Boolean, 15000);
  await sleep(3000);
}

await restart([], prefs => {
  prefs.koan ??= {};
  prefs.koan.command_palette = { ...prefs.koan.command_palette, suggestions: false };
});
await openUrlBar();
rs = await suggest('toggle compact', () => true);
check('With koan.command_palette.suggestions off, typing suggests no actions',
      ours(rs).length === 0, rowsText(ours(rs)));
await closeUrlBar();
await openUrlBar();
rs = await suggest('>toggle', rs => ours(rs).length > 0);
check('...but ">" still lists them', ours(rs).length > 0, rowsText(rs));
await closeUrlBar();

await restart(['--disable-features=KoanCommandPalette'], () => {});
await openUrlBar();
rs = await suggest('>toggle', () => true);
check('With the koan-command-palette flag off, there are none', ours(rs).length === 0,
      rowsText(ours(rs)));

console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
