// End-to-end test of the command palette (Zen's actions in the URL bar)
// against a real build.
//
//   scripts/e2e/run.sh palette
//
// Expects Koan running off-screen with UI DevTools on UI_PORT and the
// DevTools protocol on CDP_PORT (run.sh starts it). Text is typed into the
// omnibox as character events; Tab, Enter, arrows and Esc go to the browser
// window's widget, so they take the same path as real keys (pre-target
// handlers, focus manager, focused view). Prints one PASS/FAIL line per
// check; exits non-zero on any failure.
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

// The native UI: the browser window's widget node, the omnibox, every view
// as {cls, shown, w, h, p}, and the suggestion rows (popupRows()).
async function ui() {
  const c = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await c.send('DOM.enable');
  await c.send('CSS.enable');
  const doc = (await c.send('DOM.getDocument', { depth: -1 })).result.root;
  const props = async node => {
    const r = await c.send('CSS.getMatchedStylesForNode', { nodeId: node.nodeId });
    const p = {};
    for (const rule of r.result?.matchedCSSRules ?? []) {
      for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
    }
    return p;
  };
  const out = { widget: null, omnibox: null, rows: [], views: [] };
  const hasBrowserView = n => (n.children ?? []).some(
      ch => (ch.attributes ?? []).includes('BrowserView') || hasBrowserView(ch));
  const walk = async (n, shown) => {
    const a = n.attributes ?? [];
    const cls = a[a.indexOf('class') + 1];
    if (n.nodeName === 'Widget' && !out.widget && hasBrowserView(n)) out.widget = n.nodeId;
    if (n.nodeName === 'View' && cls) {
      const p = await props(n);
      shown = shown && p.Visible !== 'false';
      out.views.push({ cls, shown, w: +p.Width, h: +p.Height, p });
      if (cls === 'OmniboxViewViews' && !out.omnibox) out.omnibox = { nodeId: n.nodeId, text: p.Text };
    }
    for (const ch of n.children ?? []) await walk(ch, shown);
  };
  await walk(doc, true);
  c.close();
  out.rows = await popupRows();
  return out;
}

// The suggestions, from Chromium's WebUI popup (chrome://omnibox-popup):
// {texts: [primary, secondary], selected, icon: the start of its icon URL}.
async function popupRows() {
  const targets = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json();
  const popup = targets.find(t => t.url.startsWith('chrome://omnibox-popup'));
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
    return matches.filter(m => m.getClientRects().length).map(m => {
      const text = id => m.shadowRoot.getElementById(id)?.textContent.trim() ?? '';
      return {
        texts: [text('primaryText'), text('secondaryText')].filter(Boolean),
        selected: m.hasAttribute('selected'),
        // url.mojom.Url arrives as {url}.
        icon: String(m.match?.iconUrl?.url ?? m.match?.iconUrl ?? '').slice(0, 22),
      };
    });
  })()`)) ?? [];
}

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
  await sleep(700);
}

// Types into the omnibox.
async function type(text) {
  const { omnibox } = await ui();
  for (const ch of text) await uiKey(omnibox.nodeId, { keyCode: 0, key: ch.codePointAt(0), isChar: true });
  await sleep(1000);
}

// Tabs, not the browser's own WebUI (the popup, the side panel...).
async function pages() {
  return (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .filter(t => t.type === 'page' && !t.url.includes('.top-chrome'));
}

// Sends a key press to the active page; unhandled keys reach the browser's
// accelerators. Modifiers: Alt=1 Ctrl=2 Shift=8.
async function pageKey(code, modifiers, keyName = '') {
  const c = await connect((await pages())[0].webSocketDebuggerUrl);
  const base = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers, key: keyName };
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
  await c.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
  c.close();
  await sleep(1200);
}

async function evalIn(page, expression) {
  const c = await connect(page.webSocketDebuggerUrl);
  const r = await c.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  c.close();
  return r.result?.result?.value;
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
  return evalIn(page, `(() => { const t = document.getElementById('koan-paste');
                                const v = t.value; t.remove(); return v; })()`);
}

// --- checks ------------------------------------------------------------------
let failures = 0;
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
}

const CTRL = 2, ALT = 1;
const VK = { TAB: 9, ENTER: 13, ESC: 27, DOWN: 40, BACKSPACE: 8, L: 76, N: 78, T: 84 };
const label = row => row?.texts[0];
const rowsText = rows => rows.map(r => r.texts.join(' | ')).join(' / ');
const compactOn = vs => vs.find(v => v.cls === 'VerticalTabStripRegionView')?.w === 0;

async function openUrlBar() {
  await pageKey(VK.L, CTRL, 'l');
}
async function closeUrlBar() {
  await press(VK.ESC);
  await press(VK.ESC);
}

// Typed text suggests matching actions right after the first suggestion.
await openUrlBar();
await type('toggle compact');
let u = await ui();
let action = u.rows.findIndex(r => label(r) === 'Toggle Compact Mode');
check('Typing suggests the matching action', action >= 0, rowsText(u.rows));
check('...right below the first suggestion', action === 1, `row ${action}`);
check('...with its shortcut', u.rows[action]?.texts.includes('Ctrl+S'),
      JSON.stringify(u.rows[action]?.texts));
check('...and its own icon', u.rows[action]?.icon === 'data:image/png;base64,',
      JSON.stringify(u.rows[action]?.icon));

// Enter on it runs it.
await press(VK.DOWN);
u = await ui();
check('Down selects the action', u.rows[1]?.selected === true);
await press(VK.ENTER);
await sleep(800);
u = await ui();
check('Enter runs it (compact mode on)', compactOn(u.views));

// Once used, it comes first and Enter runs it (Zen's results learner).
await openUrlBar();
await type('toggle compact');
u = await ui();
check('A used action becomes the first suggestion', label(u.rows[0]) === 'Toggle Compact Mode',
      rowsText(u.rows));
await press(VK.ENTER);
await sleep(800);
u = await ui();
check('...and Enter runs it (compact mode off)', !compactOn(u.views));

// Short or URL-like text suggests no actions.
await openUrlBar();
await type('to');
u = await ui();
check('Two characters suggest no actions', !u.rows.some(r => label(r) === 'Toggle Compact Mode'),
      rowsText(u.rows));
await closeUrlBar();
await openUrlBar();
await type('print:x');
u = await ui();
check('Protocol-like text suggests no actions', !u.rows.some(r => label(r) === 'Print'),
      rowsText(u.rows));
await closeUrlBar();

// Tab in an empty URL bar lists every action; typing filters; Enter runs
// the first.
await openUrlBar();
await press(VK.BACKSPACE);
await press(VK.TAB);
u = await ui();
check('Tab in an empty URL bar switches to actions (">")', u.omnibox?.text === '>',
      JSON.stringify(u.omnibox?.text));
check('...listing actions', u.rows.filter(r => r.texts.length).length >= 6 &&
      label(u.rows[0]) === 'Toggle Compact Mode', rowsText(u.rows));
await type('copy');
u = await ui();
check('Typing filters the actions', label(u.rows[0]) === 'Copy Current URL', rowsText(u.rows));
await press(VK.ENTER);
await sleep(800);
const clip = await clipboard();
check('Enter runs the first one (URL copied)', clip === 'https://example.com/', JSON.stringify(clip));

// Availability follows the state, like Zen's: only the other appearances.
await openUrlBar();
await type('>dark mode');
u = await ui();
check('"Switch to Dark Mode" is offered', label(u.rows[0]) === 'Switch to Dark Mode', rowsText(u.rows));
await press(VK.ENTER);
await sleep(800);
await openUrlBar();
await type('>mode');
u = await ui();
check('...and not once dark', !u.rows.some(r => label(r) === 'Switch to Dark Mode') &&
      u.rows.some(r => label(r) === 'Switch to Light Mode'), rowsText(u.rows));
await closeUrlBar();

// "Focus on" another Space, found by its name.
await pageKey(VK.N, CTRL | ALT, 'n');   // 🌊 Space 1
await pageKey(VK.T, CTRL, 't');         // a second tab in it
await pageKey(VK.N, CTRL | ALT, 'n');   // 🌿 Space 2, now active
await openUrlBar();
await type('space 1');
u = await ui();
const focus = u.rows.findIndex(r => label(r) === 'Focus on' && r.texts.some(t => t.includes('Space 1')));
check('"Focus on" the other Space is suggested', focus > 0, rowsText(u.rows));
for (let i = 0; i < focus; i++) await press(VK.DOWN);
await press(VK.ENTER);
await sleep(1000);
u = await ui();
const tabs = u.views.filter(v => v.cls === 'TabView' && v.shown && v.h > 1).length;
check('...and Enter switches to it', tabs === 2, `${tabs} tab(s) shown`);

console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
