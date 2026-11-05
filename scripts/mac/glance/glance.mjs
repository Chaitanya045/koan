// The scripted part of scripts/mac/steps/glance.md's steps (PR #165: Glance commands, GL2/GL4).
// It drives a test Koan through the DevTools protocol (CDP_PORT), reads its
// views through UI DevTools (UI_PORT), and posts real keys and menu clicks
// on the Mac through the bench tools (../tools: keypress2, inputok.sh).
//
//   scripts/mac/glance/serve.sh &        # on the Mac, next to the test Koan
//   CDP_PORT=9341 UI_PORT=9340 PID=<test Koan pid> \
//     MAC='ssh -S /tmp/koan-cm.sock chaitanya@<mac>' TOOLS='~/work/koan-mac/tmp' \
//     node scripts/mac/glance/glance.mjs [step ...]
//
// MAC is the prefix that runs a command on the Mac. Leave it empty when this
// runs on the Mac itself. TOOLS is where the compiled tools are on the Mac.
// Every posted key first runs inputok.sh: the screen must be granted and
// the frontmost app must be PID, or the run stops.
// Without PID, keys go through CDP instead. That is not conclusive on macOS:
// CDP keys don't take the menu bar's key equivalents, and step 3 needs real
// keys. Menu steps are then skipped.
//
// Steps run: 1-10, 13, 16, 17, 18 (default: all of these; 18 minimises the
// window, so it goes last). Steps 11, 12 (discards), 14 (context menu), 15
// and 19 are manual: see scripts/mac/steps/glance.md.
import { execSync } from 'node:child_process';

const CDP_PORT = process.env.CDP_PORT;
const UI_PORT = process.env.UI_PORT;
const PID = process.env.PID;
const MAC = process.env.MAC ?? '';
const TOOLS = process.env.TOOLS ?? '~/work/koan-mac/tmp';
const PARENT = process.env.PARENT ?? 'http://127.0.0.1:8761/parent.html';
const GLANCE_HOST = 'localhost:8762';
const sleep = ms => new Promise(r => setTimeout(r, ms));

// ---------------------------------------------------------------- plumbing

async function connect(url) {
  const ws = new WebSocket(url);
  let id = 0;
  const pending = new Map();
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  };
  await new Promise((res, rej) => {
    ws.onopen = res;
    ws.onerror = () => rej(new Error('ws error ' + url));
    setTimeout(() => rej(new Error('ws timeout ' + url)), 4000);
  });
  const send = (method, params = {}) => new Promise(resolve => {
    pending.set(++id, resolve);
    ws.send(JSON.stringify({ id, method, params }));
    setTimeout(() => resolve({ timedOut: true }), 5000);
  });
  return { send, close: () => ws.close() };
}

const targets = async () =>
  (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .filter(t => t.type === 'page');
// Whether a page target is a tab: CDP finds a browser window only for a tab's
// WebContents (Browser.getWindowForTarget fails for the Glance page, which
// isn't one; a Glance expanded into a tab is one).
async function isTab(t) {
  const r = await browserCall('Browser.getWindowForTarget', { targetId: t.id });
  return !!r.result?.windowId;
}
async function classified() {
  const out = [];
  for (const t of await targets()) out.push({ ...t, tab: await isTab(t) });
  return out;
}
// The Glance page: on the Glance host, and not a tab.
const glancePage = async () =>
  (await classified()).find(t => !t.tab && t.url.includes(GLANCE_HOST));
const tabPages = async () => (await classified()).filter(t => t.tab);

async function browserCall(method, params = {}) {
  const v = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
  const c = await connect(v.webSocketDebuggerUrl);
  try { return await c.send(method, params); } finally { c.close(); }
}

async function onPage(page, fn) {
  const c = await connect(page.webSocketDebuggerUrl);
  try { return await fn(c); } finally { c.close(); }
}

async function evalIn(page, expression) {
  const r = await onPage(page, c => c.send('Runtime.evaluate',
      { expression, returnByValue: true, awaitPromise: true }));
  return r.result?.result?.value;
}

async function waitFor(fn, timeout = 5000) {
  const end = Date.now() + timeout;
  let value;
  while (!(value = await fn()) && Date.now() < end) await sleep(200);
  return value;
}

function mac(cmd) {
  const full = MAC ? `${MAC} ${JSON.stringify(cmd)}` : cmd;
  return execSync(full, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }).trim();
}

// macOS virtual key codes.
const K = { W: 13, R: 15, F: 3, L: 37, LBRACKET: 33, RBRACKET: 30, EQUAL: 24,
            MINUS: 27, ZERO: 29, C: 8, ESC: 53, RIGHT: 124, LEFT: 123, TWO: 19,
            RETURN: 36, T: 17, M: 46 };
// CDP fallback: Windows key codes and names for the same keys.
const CDP_KEYS = { 13: [87, 'w'], 15: [82, 'r'], 3: [70, 'f'], 37: [76, 'l'],
                   33: [219, '['], 30: [221, ']'], 24: [187, '='], 27: [189, '-'],
                   29: [48, '0'], 8: [67, 'c'], 53: [27, 'Escape'],
                   124: [39, 'ArrowRight'], 123: [37, 'ArrowLeft'], 19: [50, '2'],
                   36: [13, 'Enter'], 17: [84, 't'], 46: [77, 'm'] };

let cdpKeysUsed = false;
// A key press; `flags` as keypress2's: c ⌘, s ⇧, k ⌃, a ⌥.
async function key(code, flags = '', cdpTarget = null) {
  if (PID) {
    try {
      mac(`${TOOLS}/inputok.sh ${PID} && ${TOOLS}/keypress2 ${PID} ${code} ${flags || '-'}`);
    } catch {
      console.log('STOP  input not allowed (screen not granted, or the test Koan is not frontmost)');
      process.exit(3);
    }
  } else {
    cdpKeysUsed = true;
    const page = cdpTarget ?? (await glancePage()) ?? (await visibleTab());
    const [vk, name] = CDP_KEYS[code];
    const modifiers = (flags.includes('a') ? 1 : 0) | (flags.includes('k') ? 2 : 0) |
                      (flags.includes('c') ? 4 : 0) | (flags.includes('s') ? 8 : 0);
    await onPage(page, async c => {
      const base = { windowsVirtualKeyCode: vk, nativeVirtualKeyCode: code, modifiers, key: name };
      await c.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
      await c.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
    });
  }
  await sleep(700);
}

// Clicks a menu bar item of the test Koan (System Events). Skipped without PID.
function menu(menuName, item, submenu = null) {
  if (!PID) return null;
  const path = submenu
      ? `menu item "${item}" of menu "${submenu}" of menu item "${submenu}" of menu "${menuName}"`
      : `menu item "${item}" of menu "${menuName}"`;
  return mac(`osascript -e 'tell application "System Events" to tell (first process whose unix id is ${PID}) to click ${path} of menu bar 1'`);
}
function menuEnabled(menuName, item, submenu = null) {
  if (!PID) return null;
  const path = submenu
      ? `menu item "${item}" of menu "${submenu}" of menu item "${submenu}" of menu "${menuName}"`
      : `menu item "${item}" of menu "${menuName}"`;
  return mac(`osascript -e 'tell application "System Events" to tell (first process whose unix id is ${PID}) to get enabled of ${path} of menu bar 1'`) === 'true';
}

// Native views (UI DevTools): [{cls, shown, props}].
async function views() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const out = [];
  const walk = async (n, shown) => {
    const a = n.attributes ?? [];
    const cls = a[a.indexOf('class') + 1];
    if (n.nodeName === 'View' && cls) {
      const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
      const p = {};
      for (const rule of r.result?.matchedCSSRules ?? []) {
        for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
      }
      shown = shown && p.Visible !== 'false';
      out.push({ cls, shown, props: p });
    }
    for (const c of n.children ?? []) await walk(c, shown);
  };
  await walk(doc, true);
  ui.close();
  return out;
}
const shown = async cls => (await views()).some(v => v.cls === cls && v.shown);
// The card is open: UI DevTools shows its view, or (macOS, where the card is a
// top-level window of its own, which UI DevTools may not list) the Glance page
// is visible. A closed card's page is deleted at once, so it can't be the
// page of a card that is gone.
async function cardSignals() {
  const view = await shown('KoanGlanceView');
  const page = await glancePage();
  const visible = !!page && (await evalIn(page, 'document.visibilityState')) === 'visible';
  return { view, visible };
}
const cardOpen = async () => {
  const { view, visible } = await cardSignals();
  return view || visible;
};

async function visibleTab() {
  for (const page of await tabPages()) {
    if (await evalIn(page, 'document.visibilityState') === 'visible') return page;
  }
  return null;
}

// The parent page's target, without bringing it to front (no focus change).
const parentTarget = async () => (await tabPages()).find(t => t.url.startsWith(PARENT));

// The parent page, as the active tab (opened if needed).
async function parentTab() {
  let page = (await tabPages()).find(t => t.url.startsWith(PARENT));
  if (!page) {
    await browserCall('Target.createTarget', { url: PARENT });
    page = await waitFor(async () => (await tabPages()).find(t => t.url.startsWith(PARENT)));
  }
  await onPage(page, c => c.send('Page.bringToFront'));
  await sleep(500);
  return page;
}

// ⌥-clicks `#id` on the parent page (CDP mouse events are trusted input).
async function glance(id) {
  const page = await parentTab();
  const box = await evalIn(page,
      `(() => { const r = document.getElementById('${id}').getBoundingClientRect();
                return { x: r.x + 20, y: r.y + r.height / 2 }; })()`);
  await onPage(page, async c => {
    const base = { x: box.x, y: box.y, modifiers: 1, button: 'left', clickCount: 1 };
    await c.send('Input.dispatchMouseEvent', { ...base, type: 'mousePressed' });
    await c.send('Input.dispatchMouseEvent', { ...base, type: 'mouseReleased' });
  });
  await waitFor(cardOpen, 4000);
  return waitFor(glancePage, 4000);
}

// Clicks `selector` inside `page` (the Glance page) with trusted events.
async function clickIn(page, selector) {
  const box = await evalIn(page,
      `(() => { const r = document.querySelector(${JSON.stringify(selector)}).getBoundingClientRect();
                return { x: r.x + r.width / 2, y: r.y + r.height / 2 }; })()`);
  await onPage(page, async c => {
    const base = { x: box.x, y: box.y, button: 'left', clickCount: 1 };
    await c.send('Input.dispatchMouseEvent', { ...base, type: 'mousePressed' });
    await c.send('Input.dispatchMouseEvent', { ...base, type: 'mouseReleased' });
  });
  await sleep(1000);
}

async function closeCard() {
  for (let i = 0; i < 3 && await cardOpen(); i++) {
    await key(K.ESC);
    await sleep(600);
  }
}

let failures = 0;
async function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) {
    failures++;
    await dump();
  }
}
// The state after a failure: every page target (tab or not) and the card.
async function dump() {
  try {
    const list = (await classified()).map(t => `${t.tab ? 'tab ' : 'page'} ${t.url}`);
    const { view, visible } = await cardSignals();
    console.log(`      state: card view ${view ? 'shown' : 'not shown'} (UI DevTools), ` +
                `Glance page ${visible ? 'visible' : 'not visible'}; ${list.join(' | ')}`);
  } catch (e) {
    console.log(`      state: unreadable (${e.message})`);
  }
}

const timeOrigin = page => evalIn(page, 'performance.timeOrigin');
const parentUrl = async () => (await parentTab()).url;

// -------------------------------------------------------------------- steps

const steps = {
  async 1() {
    await parentTab();
    const tabsBefore = (await tabPages()).length;
    await glance('child');
    await check('1 Glance opens', await cardOpen());
    await key(K.W, 'c');
    await check('1 ⌘W closes the card', !!(await waitFor(async () => !(await cardOpen()), 3000)));
    await check('1 ⌘W keeps the tabs', (await tabPages()).length === tabsBefore,
          `${tabsBefore} -> ${(await tabPages()).length}`);
    // ⌘W twice in a row (as the tester pressed it, ~60 ms apart) closes the
    // card only, as in Zen: the card swallows ⌘W for its close animation's
    // time, also on macOS, where it closes at once.
    if (PID) {
      await glance('child');
      try {
        mac(`${TOOLS}/inputok.sh ${PID} && ${TOOLS}/keypress2 ${PID} ${K.W} c && ` +
            `${TOOLS}/keypress2 ${PID} ${K.W} c`);
      } catch {
        console.log('STOP  input not allowed');
        process.exit(3);
      }
      await sleep(1200);
      await check('1 ⌘W twice in a row closes only the card',
                  !(await cardOpen()) && (await tabPages()).length === tabsBefore,
                  `${tabsBefore} -> ${(await tabPages()).length}`);
    } else {
      await glance('child');
      await key(K.W, 'c');
      await key(K.W, 'c');  // during the close animation
      await sleep(800);
      await check('1 ⌘W twice (during the close) keeps the tabs',
                  (await tabPages()).length === tabsBefore);
    }
  },
  async 2() {
    if (!PID) return console.log('SKIP  2 (menu bar needs PID)');
    await parentTab();
    const tabsBefore = (await tabPages()).length;
    await glance('child');
    menu('File', 'Close Tab');
    await sleep(1200);
    await check('2 File > Close Tab closes the card', !(await cardOpen()));
    await check('2 File > Close Tab keeps the tabs', (await tabPages()).length === tabsBefore,
          `${tabsBefore} -> ${(await tabPages()).length}`);
  },
  async 3() {
    await parentTab();
    const tabsBefore = (await tabPages()).length;
    const page = await glance('keeper');
    await onPage(page, c => c.send('Page.bringToFront'));
    await key(K.W, 'c');
    await sleep(1000);
    await check('3 ⌘W closes the card even when the page eats keys', !(await cardOpen()),
          cdpKeysUsed ? 'CDP keys: not conclusive' : '');
    await check('3 tabs unchanged', (await tabPages()).length === tabsBefore);
    await closeCard();
  },
  async 4() {
    const parent = await parentTab();
    const parentOrigin = await timeOrigin(parent);
    let page = await glance('child');
    const before = await timeOrigin(page);
    await key(K.R, 'c');
    page = await waitFor(glancePage);
    const after = await waitFor(async () => {
      const t = await timeOrigin(page);
      return t && t !== before ? t : null;
    }, 4000);
    await check('4 ⌘R reloads the Glance page', !!after);
    await check('4 ⌘R leaves the parent', (await timeOrigin(parent)) === parentOrigin);
    if (PID) {
      const t = await timeOrigin(page);
      menu('View', 'Reload This Page');
      await check('4 View > Reload This Page reloads the Glance page',
            !!(await waitFor(async () => (await timeOrigin(page)) !== t, 4000)));
    }
    await closeCard();
  },
  async 5() {
    // A background tab first (a click on the parent page while the card is
    // open would close the card).
    const tab2 = `${PARENT}#second-${Date.now()}`;
    await browserCall('Target.createTarget', { url: tab2, background: true });
    const second = await waitFor(async () => (await tabPages()).find(t => t.url === tab2));
    const parent = await parentTab();
    const url = parent.url;
    let page = await glance('child');
    await clickIn(page, '#next');
    page = await waitFor(async () => {
      const p = await glancePage();
      return p?.url.endsWith('child2.html') ? p : null;
    });
    await check('5 a link in the Glance navigates it', !!page);
    // Noise: the background tab reloads (it resets the window's commands).
    if (second) await evalIn(second, 'location.reload()');
    await sleep(1500);
    if (PID) await check('5 History > Back is enabled for the Glance', menuEnabled('History', 'Back'));
    await key(K.LBRACKET, 'c');
    await check('5 ⌘[ goes back in the Glance, after a background reload',
          !!(await waitFor(async () => (await glancePage())?.url.endsWith('child.html'), 4000)));
    await key(K.RBRACKET, 'c');
    await check('5 ⌘] goes forward in the Glance',
          !!(await waitFor(async () => (await glancePage())?.url.endsWith('child2.html'), 4000)));
    await check('5 the parent stays', (await parentTab()).url === url);
    await closeCard();
    if (second) await browserCall('Target.closeTarget', { targetId: second.id });
  },
  async 6() {
    const parent = await parentTab();
    const parentDpr = await evalIn(parent, 'devicePixelRatio');
    let page = await glance('child');
    const dpr = await evalIn(page, 'devicePixelRatio');
    await key(K.EQUAL, 'c');
    await key(K.EQUAL, 'c');
    page = await glancePage();
    const zoomed = await evalIn(page, 'devicePixelRatio');
    await check('6 ⌘= zooms the Glance page', zoomed > dpr, `${dpr} -> ${zoomed}`);
    await check('6 the parent keeps its zoom', (await evalIn(parent, 'devicePixelRatio')) === parentDpr);
    await key(K.ZERO, 'c');
    await check('6 ⌘0 resets the Glance page', (await evalIn(page, 'devicePixelRatio')) === dpr);
    await key(K.MINUS, 'c');
    await check('6 ⌘- shrinks it', (await evalIn(page, 'devicePixelRatio')) < dpr);
    await key(K.ZERO, 'c');
    await closeCard();
  },
  async 7() {
    await glance('child');
    if (PID) await check('7 Edit > Find > Find… is disabled in a Glance', menuEnabled('Edit', 'Find…', 'Find') === false);
    await key(K.F, 'c');
    await check('7 ⌘F opens no find bar', !(await shown('FindBarView')));
    await closeCard();
    if (PID) await check('7 Find is enabled again after the card', menuEnabled('Edit', 'Find…', 'Find') === true);
  },
  async 8() {
    await parentTab();
    const tabsBefore = (await tabPages()).length;
    const url = (await parentTab()).url;
    // First: do typed keys reach a Glance page at all (macOS: the card's
    // window never becomes key)? Click its field, type "2".
    let typing = await glance('child');
    await clickIn(typing, '#field');
    await key(K.TWO);
    const value = await evalIn(typing, "document.getElementById('field').value");
    const focus = `glance hasFocus ${await evalIn(typing, 'document.hasFocus()')}, ` +
                  `parent hasFocus ${await evalIn(await parentTarget(), 'document.hasFocus()')}`;
    await check('8 typing in the Glance page reaches it', value === '2', `value "${value}"; ${focus}`);
    await closeCard();
    await glance('child');
    await key(K.L, 'c');
    await sleep(500);
    // The Glance address is selected: go to its end, before ".html", type 2.
    await key(K.RIGHT);
    for (let i = 0; i < 5; i++) await key(K.LEFT);
    await key(K.TWO);
    await key(K.RETURN);
    await check('8 ⌘L + Enter loads the edited address in the Glance',
          !!(await waitFor(async () => (await glancePage())?.url.endsWith('child2.html'), 4000)),
          cdpKeysUsed ? 'CDP keys reach the page, not the omnibox: not conclusive' : '');
    await check('8 the card stays a Glance', (await cardOpen()) && (await tabPages()).length === tabsBefore);
    await check('8 the parent stays', (await parentTab()).url === url);
    await key(K.L, 'c');
    await check('8 ⌘L again opens the URL bar', await shown('OmniboxViewViews'));
    await key(K.ESC);
    await check('8 Esc closes the URL bar, the card stays', await cardOpen());
    {
      const g = await glancePage();
      console.log(`INFO  8 after Esc: glance hasFocus ${g && await evalIn(g, 'document.hasFocus()')}, ` +
                  `parent hasFocus ${await evalIn(await parentTarget(), 'document.hasFocus()')}`);
    }
    await key(K.ESC);
    await check('8 a second Esc closes the card (the Glance had focus again)',
          !!(await waitFor(async () => !(await cardOpen()), 3000)));
    await closeCard();
  },
  async 9() {
    if (!PID) return console.log('SKIP  9 (clipboard needs the Mac)');
    await glance('child');
    await key(K.C, 'cs');
    await check('9 ⌘⇧C copies the Glance address', mac('pbpaste') === `http://${GLANCE_HOST}/child.html`);
    await closeCard();
    await key(K.C, 'cs');
    await check('9 after the card, ⌘⇧C copies the parent address', mac('pbpaste').startsWith(PARENT));
  },
  async 10() {
    let page = await glance('form');
    await check('10 the form page is in the card', !!page && await cardOpen());
    await clickIn(page, '#send');
    page = await waitFor(glancePage);
    await sleep(1000);
    await check('10 the POST loads in the card', !!page && await cardOpen());
    // The server counts the POSTs it answered (serve.sh's /posts).
    const posts = () => evalIn(page, "fetch('/posts').then(r => r.text()).then(Number)");
    const postsBefore = await posts();
    await key(K.R, 'c');
    await sleep(2000);
    page = await waitFor(glancePage);
    const postsAfter = await posts();
    await check('10 ⌘R on a POST page does not resubmit', postsAfter === postsBefore,
                `POSTs ${postsBefore} -> ${postsAfter}`);
    await check('10 the card is still there', await cardOpen());
    await closeCard();
  },
  async 13() {
    if (!PID) return console.log('SKIP  13 (needs the Mac)');
    await glance('child');
    await key(K.W, 'cs');  // ⌘⇧W: close the window
    await sleep(2000);
    let alive = true;
    try { mac(`kill -0 ${PID}`); } catch { alive = false; }
    // (A macOS app stays running without windows.)
    await check('13 ⌘⇧W with a Glance open: no crash', alive);
    console.log('INFO  13 ⌘Q with a Glance open, then relaunch: run by hand (scripts/mac/steps/glance.md step 13)');
  },
  // Koan's high-priority shortcuts before Chrome's reserved keys: ⌘⌥→ is
  // Koan's next Space, not Chrome's next tab. With one Space (a fresh
  // profile) it changes nothing: the card and the parent tab stay.
  async 16() {
    const other = await openBackgroundTab(`${PARENT}#other-${Date.now()}`);
    await glance('child');
    await key(K.RIGHT, 'ca');
    await sleep(800);
    const active = await visibleTab();
    await check("16 ⌘⌥→ in a Glance isn't Chrome's next tab (one Space: nothing)",
                (await cardOpen()) && !!active && active.url.startsWith(PARENT) &&
                    active.id !== other?.id,
                active ? active.url : 'no visible tab');
    await closeCard();
    if (other) await browserCall('Target.closeTarget', { targetId: other.id });
  },
  // Reserved commands that switch tabs close the card from inside its own
  // key handling (they are posted): no crash.
  async 17() {
    if (!PID) return console.log('SKIP  17 (needs the Mac)');
    const other = await openBackgroundTab(`${PARENT}#next-${Date.now()}`);
    await glance('child');
    await key(K.RBRACKET, 'cs');  // ⌘⇧] next tab
    await sleep(1200);
    let alive = true;
    try { mac(`kill -0 ${PID}`); } catch { alive = false; }
    const active = await visibleTab();
    await check('17 ⌘⇧] in a Glance: no crash, the card goes, the next tab shows',
                alive && !(await cardOpen()) && !!active && active.id !== (await parentTarget())?.id,
                active ? active.url : 'no visible tab');
    if (other) await browserCall('Target.closeTarget', { targetId: other.id });
    // ⌘⇧T: a closed tab comes back.
    const closedUrl = `${PARENT}#closed-${Date.now()}`;
    const closed = await openBackgroundTab(closedUrl);
    if (closed) await browserCall('Target.closeTarget', { targetId: closed.id });
    await sleep(800);
    await glance('child');
    await key(K.T, 'cs');  // ⌘⇧T reopen closed tab
    await sleep(1500);
    alive = true;
    try { mac(`kill -0 ${PID}`); } catch { alive = false; }
    await check('17 ⌘⇧T in a Glance: no crash, the card goes, the closed tab is back',
                alive && !(await cardOpen()) &&
                    (await tabPages()).some(t => t.url === closedUrl));
    const back = (await tabPages()).find(t => t.url === closedUrl);
    if (back) await browserCall('Target.closeTarget', { targetId: back.id });
  },
  // ⌘M (Window > Minimize) in a Glance minimises the window, as in Zen.
  // Last: the window is restored over CDP, and no key is sent after it.
  async 18() {
    if (!PID) return console.log('SKIP  18 (needs the Mac)');
    const parent = await parentTab();
    await glance('child');
    await key(K.M, 'c');
    await sleep(1200);
    const win = await browserCall('Browser.getWindowForTarget', { targetId: parent.id });
    const state = win.result?.bounds?.windowState;
    await check('18 ⌘M in a Glance minimises the window', state === 'minimized', `windowState ${state}`);
    if (win.result?.windowId) {
      await browserCall('Browser.setWindowBounds',
                        { windowId: win.result.windowId, bounds: { windowState: 'normal' } });
    }
    console.log('INFO  18 the window was restored over CDP; the card may still be open');
  },
};

// A new tab in the background (no activation), once its target is there.
async function openBackgroundTab(url) {
  await browserCall('Target.createTarget', { url, background: true });
  return waitFor(async () => (await tabPages()).find(t => t.url === url));
}

const wanted = process.argv.slice(2).length
    ? process.argv.slice(2)
    : ['1', '2', '3', '4', '5', '6', '7', '8', '9', '10', '13', '16', '17', '18'];
if (!PID) console.log('NOTE  no PID: keys go through CDP (not conclusive on macOS), menu steps skipped');
for (const s of wanted) {
  try {
    await steps[s]();
  } catch (e) {
    await check(`step ${s} ran`, false, e.message);
    await closeCard().catch(() => {});
  }
}
console.log(failures ? `${failures} FAILED` : 'ALL PASSED');
process.exit(failures ? 1 : 0);
