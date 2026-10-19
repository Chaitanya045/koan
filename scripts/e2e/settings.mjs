// End-to-end test of chrome://koan-settings against a real build: the page
// lists Koan's settings like Zen's preferences, changes apply live (layout,
// compact mode), the page follows changes made elsewhere (Ctrl+Alt+B), and
// the shortcut editor rebinds, detects conflicts, clears and resets keys.
//
//   scripts/e2e/run.sh settings
//
// With SHOTS=<dir>, also saves screenshots of the page (light and dark).
const UI_PORT = process.env.UI_PORT;
const CDP_PORT = process.env.CDP_PORT;
const SHOTS = process.env.SHOTS;
const sleep = ms => new Promise(r => setTimeout(r, ms));

async function connect(url) {
  const ws = new WebSocket(url);
  let id = 0;
  const pending = new Map();
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  };
  await new Promise(r => (ws.onopen = r));
  const send = (method, params = {}) => new Promise(resolve => {
    pending.set(++id, resolve);
    ws.send(JSON.stringify({ id, method, params }));
    setTimeout(() => resolve({ timedOut: true }), 5000);
  });
  return { send, close: () => ws.close() };
}

async function pages() {
  return (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .filter(t => t.type === 'page');
}

// Every native view as {cls, shown, w, h}; `shown` includes the ancestors'.
async function views() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const out = [];
  const walk = async (n, depth, shown) => {
    const a = n.attributes ?? [];
    const cls = a[a.indexOf('class') + 1];
    if (depth > 0 && n.nodeName === 'View' && cls) {
      const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
      const p = {};
      for (const rule of r.result?.matchedCSSRules ?? []) {
        for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
      }
      shown = shown && p.Visible !== 'false';
      out.push({ cls, shown, w: +p.Width, h: +p.Height });
    }
    for (const c of n.children ?? []) await walk(c, depth + 1, shown);
  };
  await walk(doc, 0, true);
  ui.close();
  return out;
}

const shownViews = (vs, cls) => vs.filter(v => v.cls === cls && v.shown);

async function onPage(page, fn) {
  const c = await connect(page.webSocketDebuggerUrl);
  try {
    return await fn(c);
  } finally {
    c.close();
  }
}

async function evalIn(page, expression) {
  const r = await onPage(page, c => c.send('Runtime.evaluate',
      { expression, returnByValue: true, awaitPromise: true }));
  return r.result?.result?.value;
}

async function front(page) {
  await onPage(page, c => c.send('Page.bringToFront'));
  await sleep(800);
}

// Sends a key press to `page`. Modifiers: Alt=1 Ctrl=2 Meta=4 Shift=8.
async function key(page, code, modifiers, keyName) {
  await onPage(page, async c => {
    const base = { windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers,
                   key: keyName };
    await c.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
    await c.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
  });
  await sleep(1200);
}

async function shot(page, name) {
  if (!SHOTS) return;
  const r = await onPage(page, c => c.send('Page.captureScreenshot', { format: 'png' }));
  const fs = await import('node:fs');
  fs.writeFileSync(`${SHOTS}/${name}.png`, Buffer.from(r.result.data, 'base64'));
}

let failures = 0;
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
}

const ALT = 1, CTRL = 2;
const VK = { B: 66, J: 74, S: 83, ESC: 27, BACKSPACE: 8, COMMA: 188 };

// Open the page in the first tab; the second (example.com) stays for keys.
let [settings, other] = await pages();
await onPage(settings, async c => {
  await c.send('Page.enable');
  await c.send('Page.navigate', { url: 'chrome://koan-settings' });
});
await sleep(2500);
await front(settings);
[settings] = (await pages()).filter(p => p.url.startsWith('chrome://koan-settings'));

const navItems = await evalIn(settings,
    `[...document.querySelectorAll('#categories .category')].map(c => c.textContent.trim())`);
check('The page lists Zen\'s panes', JSON.stringify(navItems) ===
      JSON.stringify(['Look and Feel', 'Tab Management', 'Keyboard Shortcuts']),
      JSON.stringify(navItems));
const headings = await evalIn(settings,
    `[...document.querySelectorAll('#pane h1, #pane h2')].map(e => e.textContent)`);
check('...Look and Feel shows the browser layout and compact mode groups',
      headings?.includes('Browser Layout') && headings?.includes('Show in compact view'),
      JSON.stringify(headings));
await shot(settings, 'settings-look-and-feel');

// Browser layout: the cards switch single / multiple toolbars live.
const card = label => `[...document.querySelectorAll('.layout-card')]
    .find(c => c.textContent.includes('${label}'))`;
check('Only Sidebar is selected by default',
      await evalIn(settings, `${card('Only Sidebar')}.getAttribute('aria-checked')`) === 'true');
await evalIn(settings, `${card('Sidebar and Top Toolbar')}.click()`);
await sleep(1200);
let vs = await views();
check('Choosing "Sidebar and Top Toolbar" switches to multiple toolbars',
      shownViews(vs, 'KoanSidebarNavView').length === 0);
check('...and the card is selected',
      await evalIn(settings, `${card('Sidebar and Top Toolbar')}.getAttribute('aria-checked')`) === 'true');
await key(settings, VK.B, CTRL | ALT, 'b');
check('The page follows a change made with a shortcut (Ctrl+Alt+B)',
      await evalIn(settings, `${card('Only Sidebar')}.getAttribute('aria-checked')`) === 'true');
vs = await views();
check('...back to single toolbar', shownViews(vs, 'KoanSidebarNavView').length === 1);

// Compact mode.
const compact = `document.querySelector('[data-pref="koan.compact_mode.enabled"] input')`;
await evalIn(settings, `${compact}.click()`);
await sleep(1200);
vs = await views();
check('The compact mode checkbox turns compact mode on',
      vs.find(v => v.cls === 'VerticalTabStripRegionView')?.w === 0 &&
      shownViews(vs, 'KoanHotZone').length === 2);
await evalIn(settings, `${compact}.click()`);
await sleep(1200);
vs = await views();
check('...and off', shownViews(vs, 'KoanHotZone').length === 1);

// Workspaces (Tab Management).
await evalIn(settings, `document.querySelector('#category-tabs').click()`);
const tint = `document.querySelector('[data-pref="koan.workspaces.tint"] input')`;
check('Tab Management shows the workspace tint, on by default',
      await evalIn(settings, `${tint}.checked`) === true);
await evalIn(settings, `${tint}.click()`);
await sleep(800);
check('...and it can be switched off', await evalIn(settings, `${tint}.checked`) === false);
await evalIn(settings, `${tint}.click()`);

// Keyboard shortcuts.
await evalIn(settings, `document.querySelector('#category-shortcuts').click()`);
const value = id => evalIn(settings, `document.querySelector('#shortcut-${id}').value`);
const groups = await evalIn(settings,
    `[...document.querySelectorAll('.shortcut-group h2')].map(e => e.textContent)`);
check('Shortcuts are grouped as in Zen',
      groups?.[0] === 'Compact Mode' && groups?.[1] === 'Workspaces' &&
      groups?.[2] === 'Split View', JSON.stringify(groups));
check('...showing Zen\'s keys', await value('compact-mode-toggle') === 'Ctrl+S' &&
      await value('copy-url-markdown') === 'Ctrl+Alt+⇧+C',
      `${await value('compact-mode-toggle')}, ${await value('copy-url-markdown')}`);
await shot(settings, 'settings-shortcuts');

const focus = id => evalIn(settings, `document.querySelector('#shortcut-${id}').focus()`);
await focus('compact-mode-toggle');
await key(settings, VK.J, CTRL | ALT, 'j');
check('Typing a key shows it', await value('compact-mode-toggle') === 'Ctrl+Alt+J');
await key(settings, VK.ESC, 0, 'Escape');
check('...Escape saves it', await value('compact-mode-toggle') === 'Ctrl+Alt+J');

await front(other);
await key(other, VK.J, CTRL | ALT, 'j');
vs = await views();
check('The new key works at once (Ctrl+Alt+J: compact mode on)',
      shownViews(vs, 'KoanHotZone').length === 2);
await key(other, VK.J, CTRL | ALT, 'j');
vs = await views();
check('...and off again', shownViews(vs, 'KoanHotZone').length === 1);

await front(settings);
await focus('compact-mode-show-sidebar');
await key(settings, VK.J, CTRL | ALT, 'j');
vs = await views();
check('While editing, a key that is a shortcut reaches the editor',
      shownViews(vs, 'KoanHotZone').length === 1 &&
      await value('compact-mode-show-sidebar') === 'Ctrl+Alt+J');
await key(settings, VK.ESC, 0, 'Escape');
const note = await evalIn(settings,
    `document.querySelector('#shortcut-compact-mode-show-sidebar').parentElement
         .querySelector('.shortcut-note')?.textContent`);
check('A key taken by another shortcut is refused with Zen\'s conflict message',
      note === 'Conflicts with Compact Mode -> Toggle Compact Mode', JSON.stringify(note));
check('...and marked invalid', await evalIn(settings,
    `document.querySelector('#shortcut-compact-mode-show-sidebar').classList.contains('invalid')`));

await focus('mute');
await key(settings, VK.BACKSPACE, 0, 'Backspace');
await evalIn(settings, `document.activeElement.blur()`);
await sleep(500);
check('Backspace clears a shortcut', await value('mute') === 'Not set');

await evalIn(settings, `document.querySelector('#reset-shortcuts').click()`);
await sleep(800);
check('Reset to Default restores the defaults',
      await value('compact-mode-toggle') === 'Ctrl+S' && await value('mute') === 'Ctrl+M',
      `${await value('compact-mode-toggle')}, ${await value('mute')}`);

// Search.
await evalIn(settings, `(() => { const s = document.querySelector('#search');
    s.value = 'compact'; s.dispatchEvent(new Event('input')); })()`);
const found = await evalIn(settings, `document.querySelector('#pane').textContent`);
check('Find in Settings searches settings and shortcuts',
      found?.includes('Enable Koan\'s compact mode') && found?.includes('Toggle Compact Mode'));

if (SHOTS) {
  await evalIn(settings, `document.querySelector('#category-looks').click()`);
  await onPage(settings, c => c.send('Emulation.setEmulatedMedia',
      { features: [{ name: 'prefers-color-scheme', value: 'dark' }] }));
  await sleep(300);
  await shot(settings, 'settings-look-and-feel-dark');
}

// Ctrl+, opens the settings page (Zen's "Open Preferences").
await front(other);
const before = (await pages()).filter(p => p.url.startsWith('chrome://koan-settings')).length;
await key(other, VK.COMMA, CTRL, ',');
const after = (await pages()).filter(p => p.url.startsWith('chrome://koan-settings')).length;
check('Ctrl+, opens Koan\'s settings', after === before + 1, `${before} -> ${after}`);

console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
