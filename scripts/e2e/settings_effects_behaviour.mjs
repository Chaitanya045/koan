// End-to-end test of Koan's behaviour settings and of Chromium's settings
// rows that Koan's layout overrides (docs/SETTINGS_AUDIT.md, "Behaviour and
// Chromium rows"), against a real build. Each row is changed THROUGH the
// settings page (its checkbox or menu in chrome://settings/koan/<pane>, as a
// click does), and then its effect is checked where it can be driven:
// - every behaviour row: the page sets the pref both ways (each option of a
//   menu), the control shows a value set elsewhere, and a sub-option greys
//   out with its parent (Strict under Agent ready);
// - Agent ready: the agent gateway's discovery file comes and goes;
// - Close tab on Back with no history: the sidebar's Back button is enabled
//   in a child tab and closes it, back to its opener; off, Back is off;
// - Switch to the most recently used tab on close: on, the tab shown last;
//   off, the neighbour (tabs shown by clicking their sidebar rows, as a user
//   does: a CDP bringToFront isn't a gesture, so upstream keeps the openers);
// - Ctrl+Tab: ignore pending tabs, within Essentials or the Space's tabs
//   (CDP keys, and on the Mac bench real keys too: Chrome reserves the
//   cycling keys on macOS, so a real key takes another path than a CDP one;
//   with both off, Chrome's own Next Tab is reached by a real key only),
//   and Chromium's recently used order with Koan's note under the rows;
// - Close Tab Shortcut Behavior, every option, on an Essential (Cmd+W is the
//   main menu's on macOS: real input only);
// - Spaces wrap around (Next / Previous Space shortcuts), and natural
//   scrolling (a sideways wheel over the sidebar, not on macOS);
// - Chromium's Appearance rows Koan overrides are hidden (bookmarks bar, tab
//   search button, auto-pin new tab groups, hover cards, tab strip
//   position), and its Mode menu and Koan's Look > Mode follow a change of
//   the other (nothing is written at start: websites follow the scheme);
// - Restore pinned tabs to their pinned URL: after a quit and a start, with
//   the setting on and off.
// Steps that can't be driven say SKIP with the reason. The test_helper
// extension (settings_effects_behaviour.flags) pins, unloads, closes and
// lists tabs (lib/mac.mjs askTestHelper()); the macOS suite loads it.
//
// The settings page opens in a window of its own and closes again after each
// change, so it is never one of the tabs the steps count, and new tabs open
// in the test's window.
//
//   scripts/mac/suite.sh (scripts/mac/suite/e2e.list)
//   scripts/e2e/run.sh settings_effects_behaviour
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {
  ACCEL, ALT, CDP_PORT, CTRL, IS_MAC, UI_PORT, browserCall, check, click, connect, evalIn,
  finish, front, key, onPage, pages, readPref, setChromePref, setKoanPref, settingsCall,
  showPane, sleep, tabMenuCommand, viewTree, visiblePage, waitFor,
} from './lib/koan.mjs';
import {
  askTestHelper, clickThroughRoot, createSpace, fatalInLog, realInput, realInputWhyNot,
  realKey, skipped, spaceButtons, startServer, tabViewTitled,
} from './lib/mac.mjs';

const VK = { W: 87, TAB: 9, PGDN: 34, LEFT: 37, RIGHT: 39 };
const J = JSON.stringify;

// ---- Pages: the opener links to a child (target=_blank keeps the opener).
const server = await startServer({
  '/opener': `<!doctype html><title>Opener</title>
      <a id="child" href="/p?t=Child" target="_blank"
         style="display:block;font-size:48px">Open a child tab</a>`,
});
const url = t => server.url(`/p?t=${encodeURIComponent(t)}`);
const pageAt = async u => (await pages()).find(p => p.type === 'page' && p.url === u);

// The test's window: its first page, made a 127.0.0.1 page (test_helper
// answers only those).
let anchor = (await pages()).find(p => p.type === 'page');
await onPage(anchor, c => c.send('Page.navigate', { url: url('Anchor') }));
await waitFor(async () => (await pageAt(url('Anchor'))) !== undefined, 8000);
anchor = await pageAt(url('Anchor'));

// A new tab in the test's window, in front unless `background`.
async function openTab(title, background = false) {
  await front(anchor);
  const before = new Set((await pages()).map(p => p.id));
  await browserCall('Target.createTarget', { url: url(title), background });
  const page = await waitFor(async () => (await pages()).find(
      p => !before.has(p.id) && p.url === url(title)), 8000);
  await sleep(500);
  return page;
}

// The asking page's window's tabs, in order, from test_helper (null if it
// isn't loaded).
async function listTabs(asker = anchor) {
  const r = await onPage(asker, c => c.send('Runtime.evaluate', {
    expression: `globalThis.chrome?.runtime?.sendMessage ? chrome.runtime.sendMessage(
        'efmifboglkpejppaiocdcajnkhbfoofo', 'list-tabs').catch(() => null) : null`,
    awaitPromise: true, returnByValue: true }));
  const tabs = r.result?.result?.value;
  return Array.isArray(tabs) ? tabs : null;
}
const titleOf = tab => tab ? new URL(tab.url).searchParams.get('t') ?? tab.url : 'none';
const activeTitle = async () => titleOf((await listTabs())?.find(t => t.active));
const visible = page => evalIn(page, 'document.visibilityState').then(v => v === 'visible');
const exists = async page => (await pages()).some(p => p.id === page.id);
const hasHelper = !!await listTabs();

// ---- The settings page.
let settings = null;
let settingsWindow = null;
async function openSettings() {
  if (settings) return settings;
  settingsWindow = (await browserCall('Target.createTarget',
      { url: 'chrome://settings/koan/tabs', newWindow: true })).result.targetId;
  settings = await waitFor(async () => (await pages()).find(p => p.id === settingsWindow), 8000);
  await waitFor(() => evalIn(settings,
      `window.koanSettingsRoot?.getElementById('pane')?.childElementCount > 0`), 8000);
  return settings;
}
async function closeSettings() {
  if (!settings) return;
  await browserCall('Target.closeTarget', { targetId: settingsWindow });
  settings = null;
  await sleep(800);
  await front(anchor);
}

await openSettings();
const info = await settingsCall(settings, 'koanGetSettings');
check('The settings page opens with Koan\'s rows', (info?.rows?.length ?? 0) > 0);
const rowOf = pref => info?.rows.find(r => r.pref === pref);
const paneOf = pref => info?.groups.find(g => g.id === rowOf(pref)?.group)?.pane;
const rowSelector = pref => `[data-pref=${J(pref)}]`;

async function showPaneOf(pref) {
  await evalIn(settings, showPane(paneOf(pref)));
  return waitFor(() => evalIn(settings,
      `!!koanSettingsRoot.querySelector(${J(rowSelector(pref))})`), 4000);
}
// What the row's control shows: a checkbox's state, a menu's option value.
async function shown(pref) {
  await showPaneOf(pref);
  return evalIn(settings, `(() => {
    const input = koanSettingsRoot.querySelector(${J(rowSelector(pref))})
        ?.querySelector('input, select');
    if (!input) return null;
    if (input.type === 'checkbox') return input.checked;
    return ${J(rowOf(pref)?.options.map(o => o.value) ?? [])}[input.selectedIndex] ?? null;
  })()`);
}
async function isDisabled(pref) {
  await showPaneOf(pref);
  return evalIn(settings, `!!koanSettingsRoot.querySelector(${J(rowSelector(pref))})
      ?.querySelector('input, select')?.disabled`);
}
// The pref's value as the browser has it (profile or local state).
const prefValue = async pref =>
  (await settingsCall(settings, 'koanGetSettings'))?.prefs?.[pref];
// Changes the row the way the user does, in the open settings page: clicks
// the checkbox, or picks the option in the menu. Returns null, or what went
// wrong.
async function setRow(pref, value) {
  const row = rowOf(pref);
  if (!row) return 'no row';
  if (!await showPaneOf(pref)) return 'not on its pane';
  const r = await evalIn(settings, `(() => {
    const input = koanSettingsRoot.querySelector(${J(rowSelector(pref))})
        ?.querySelector('input, select');
    if (!input) return 'no control';
    if (input.disabled) return 'disabled';
    if (input.type === 'checkbox') {
      if (input.checked !== ${J(value)}) input.click();
      return null;
    }
    const i = ${J(row.options.map(o => o.value))}.indexOf(${J(value)});
    if (i < 0) return 'no such option';
    input.selectedIndex = i;
    input.dispatchEvent(new Event('change'));
    return null;
  })()`);
  if (r) return r;
  return await waitFor(async () => await prefValue(pref) === value, 3000)
      ? null : `the pref is ${J(await prefValue(pref))}`;
}
// Opens the page, changes the row, closes the page.
async function change(pref, value) {
  await openSettings();
  const r = await setRow(pref, value);
  await closeSettings();
  if (r) console.log(`  (setting ${pref} to ${J(value)}: ${r})`);
  return !r;
}

// ---- 1. Every behaviour row: the page sets it, and back.
const CHECKBOXES = [
  'koan.updates.check_enabled', 'koan.updates.show_update_notification',
  'koan.tabs.close_on_back_with_no_history', 'koan.tabs.ctrl_tab_skip_pending',
  'koan.tabs.ctrl_tab_within_kind', 'koan.tabs.select_recently_used_on_close',
  'koan.workspaces.wrap_around_navigation', 'koan.workspaces.natural_scroll',
  'koan.pinned_tabs.restore_to_pinned_url', 'koan.tabs.open_pinned_in_new_tab',
  'performance_tuning.tab_freezing.enabled',
  'koan.agent_control.enabled', 'koan.agent_control.strict',
  'koan.agent_control.use.logins', 'koan.agent_control.use.passwords',
  'koan.agent_control.use.autofill', 'koan.agent_control.use.extensions',
  'koan.agent_control.use.history', 'koan.agent_control.limit_take_over_to_site',
  'koan.agent_control.low_power_halves',
];
const MENUS = [
  'koan.workspaces.scroll_modifier_key', 'koan.pinned_tabs.close_shortcut_behavior',
  'koan.agent_control.max_leased_tabs', 'koan.agent_control.max_prompts_per_minute',
  'koan.agent_control.max_auth_failures',
];
const agentReadyBefore = await prefValue('koan.agent_control.enabled');
// Strict greys out without Agent ready (its parent): on for the round trip.
await setRow('koan.agent_control.enabled', true);
for (const pref of CHECKBOXES) {
  if (!rowOf(pref)) {
    skipped(`"${pref}" both ways`, 'the row is not shown in this build (its feature is off, ' +
            'or agent control is off by policy)');
    continue;
  }
  const before = await prefValue(pref);
  const r = await setRow(pref, !before) ?? await setRow(pref, before);
  check(`Settings: "${rowOf(pref).label.slice(0, 60)}" switches both ways`, !r, r ?? '');
}
for (const pref of MENUS) {
  const row = rowOf(pref);
  if (!row) {
    skipped(`"${pref}" options`, 'the row is not shown in this build');
    continue;
  }
  const before = await prefValue(pref);
  const failed = [];
  for (const option of row.options) {
    const r = await setRow(pref, option.value);
    if (r) failed.push(`${option.value}: ${r}`);
  }
  await setRow(pref, before);
  check(`Settings: "${row.label.slice(0, 60)}" takes each of its ${row.options.length} options`,
        !failed.length, failed.join('; '));
}

// The control shows a value set elsewhere (another settings tab): a profile
// pref, a local-state pref and a menu.
for (const [pref, value] of [['koan.tabs.ctrl_tab_skip_pending', true],
                             ['koan.updates.show_update_notification', false],
                             ['koan.pinned_tabs.close_shortcut_behavior', 'switch']]) {
  const before = await prefValue(pref);
  await setKoanPref(pref, value);
  check(`Settings: "${rowOf(pref)?.label.slice(0, 40)}" shows a change made elsewhere`,
        await waitFor(async () => await shown(pref) === value, 3000), J(await shown(pref)));
  await setKoanPref(pref, before);
}

// Strict is a sub-option of Agent ready (a local-state pref), greyed out
// while it is off, like Zen's dependent rows.
if (rowOf('koan.agent_control.strict')) {
  await setRow('koan.agent_control.enabled', false);
  check('Settings: Strict greys out while Agent ready is off',
        await waitFor(() => isDisabled('koan.agent_control.strict'), 3000));
  await setRow('koan.agent_control.enabled', true);
  check('...and is back once Agent ready is on',
        await waitFor(async () => !await isDisabled('koan.agent_control.strict'), 3000));
}

// ---- 2. Agent ready: the gateway listens (its discovery file), or not.
const args = (process.env.KOAN_CHROME_ARGS ?? '').split('\n').filter(Boolean);
const userDataDir = args.find(a => a.startsWith('--user-data-dir='))
    ?.split('=').slice(1).join('=');
if (!userDataDir || !rowOf('koan.agent_control.enabled')) {
  skipped('Agent ready starts and stops the gateway',
          userDataDir ? 'no Agent ready row' : 'no --user-data-dir in KOAN_CHROME_ARGS');
} else {
  const discovery = path.join(userDataDir, 'AgentControl', 'agent.json');
  const listening = await waitFor(() => fs.existsSync(discovery), 8000);
  if (!listening && !IS_MAC) {
    skipped('Agent ready starts and stops the gateway',
            'the gateway did not start here (agent control is macOS\'s: agent_cli.mjs)');
  } else {
    check('Agent ready on: the agent gateway listens (AgentControl/agent.json)', listening);
    await setRow('koan.agent_control.enabled', false);
    check('Agent ready off: it stops (the file goes)',
          await waitFor(() => !fs.existsSync(discovery), 8000));
  }
  await setRow('koan.agent_control.enabled', agentReadyBefore ?? false);
}
skipped('Strict, What agents can use and Limits: their effect',
        'needs an agent: scripts/e2e/agent_cli.mjs drives them with the koan CLI');
skipped('Check for updates / Show a notification after Koan was updated: their effect',
        'needs GitHub and a newer build; koan_update_logic_unittest has the logic');
skipped('Freeze background tabs: its effect',
        'tabs freeze after minutes in the background; scripts/e2e/power.mjs checks freezing');
skipped('The Keyboard Shortcuts pane (rebind, conflicts, reset, each by what the key then does)',
        'scripts/e2e/settings.mjs');
await closeSettings();

// ---- 3. Close the tab on Back with no history.
// The test window's views: the widget that shows the tab titled `title`.
async function windowViews(title) {
  const vs = await viewTree();
  const label = vs.find(v => v.cls === 'TabTitle' && v.shown && v.props.Text === title);
  return label ? vs.filter(v => v.widget === label.widget) : [];
}
const opener = await openTab('Opener');
await onPage(opener, c => c.send('Page.navigate', { url: server.url('/opener') }));
await waitFor(async () => await evalIn(opener, 'document.title') === 'Opener');
async function openChild() {
  await front(opener);
  const before = new Set((await pages()).map(p => p.id));
  await click(opener, '#child');
  return waitFor(async () => (await pages()).find(p => !before.has(p.id) && p.type === 'page'),
                 6000);
}
async function backButton() {
  const vs = await windowViews('Opener');
  return { vs, button: vs.find(v => v.shown && v.props.TooltipText === 'Back' &&
                                    v.path.includes('KoanSidebarNavButtons')) };
}

await change('koan.tabs.close_on_back_with_no_history', true);
const child = await openChild();
check('A link opens a child tab', !!child && await waitFor(() => visible(child)));
let { vs, button } = await backButton();
if (!child || !button) {
  skipped('Close tab on Back with no history', child ? 'no sidebar Back button' : 'no child tab');
} else {
  check('Close on Back on (default): Back is enabled in a child tab with no history',
        button.props.Enabled !== 'false', button.props.Enabled);
  await change('koan.tabs.close_on_back_with_no_history', false);
  await front(child);
  ({ vs, button } = await backButton());
  check('...off: Back is disabled there (the setting applies at once)',
        button?.props.Enabled === 'false', button?.props.Enabled);
  await change('koan.tabs.close_on_back_with_no_history', true);
  await front(child);
  ({ vs, button } = await backButton());
  await clickThroughRoot(vs, button);
  check('...on again: Back closes the child tab',
        await waitFor(async () => !await exists(child), 5000));
  check('...and shows the tab that opened it', await waitFor(() => visible(opener), 5000));
}

// ---- 4. Switch to the most recently used tab on close.
// test_helper closes the tab (chrome.tabs.remove: the strip picks the next
// tab as for Cmd+W), or Ctrl+W off macOS.
async function closeTab(page) {
  if (hasHelper) {
    const tab = (await listTabs(page))?.find(t => t.asker);
    return !!tab && await askTestHelper(page, { close: [tab.id] }, 1500);
  }
  if (IS_MAC) return false;
  await key(page, VK.W, CTRL, 'w');
  await sleep(1000);
  return true;
}
// Shows `page` as the user does, by clicking its row in the sidebar (a user
// gesture: Chromium forgets the tabs' openers then, which a CDP bringToFront
// doesn't, and an opener comes before the recently used tab); `front()` if
// the row isn't on screen.
// Returns whether `title` is then the active tab.
async function show(page, title) {
  const vs = await viewTree();
  const row = tabViewTitled(vs, title);
  if (row) {
    await clickThroughRoot(vs, row);
  } else {
    await front(page);
  }
  return !!await waitFor(async () => await activeTitle() === title, 3000);
}
// The window's tabs, for a failure's detail: title, * if active, and the
// order Chromium last showed them in (1 = most recently).
async function strip() {
  const tabs = await listTabs() ?? [];
  const order = [...tabs].sort((x, y) => y.lastAccessed - x.lastAccessed);
  return tabs.map(t => `${titleOf(t)}${t.active ? '*' : ''}${t.pinned ? '(pinned)' : ''}` +
                       `#${order.indexOf(t) + 1}`).join(' ');
}
// The recently used tab and the neighbour must differ, and neither may be
// the tab made last before the closing one: A is shown before D, while C is
// both D's neighbour and the tab made just before it.
const a = await openTab('A');
const b = await openTab('B');
const c = await openTab('C');
let d = await openTab('D');
let shownFirst = await show(a, 'A');
let shownThen = await show(d, 'D');
let before = await strip();
if (!shownFirst || !shownThen) {
  check('MRU on close: A and then D are shown (sidebar clicks)', false, before);
} else if (!await closeTab(d)) {
  skipped('Switch to the most recently used tab on close', 'test_helper is not loaded');
} else {
  check('MRU on close on (default): closing D shows A, the tab shown before it',
        await waitFor(() => visible(a), 4000), `before: ${before}; after: ${await strip()}`);
  await change('koan.tabs.select_recently_used_on_close', false);
  d = await openTab('D');
  shownFirst = await show(a, 'A');
  shownThen = await show(d, 'D');
  before = await strip();
  await closeTab(d);
  check('...off: closing D shows its neighbour C, as in Chrome',
        shownFirst && shownThen && await waitFor(() => visible(c), 4000),
        `before: ${before}; after: ${await strip()}`);
  await change('koan.tabs.select_recently_used_on_close', true);
}

// ---- 5. Ctrl+Tab: pending tabs, Essentials vs the Space's, recently used.
// The strip: [Essential E] Anchor ... A B C X, X unloaded.
async function pin(page, title) {
  if (hasHelper) return askTestHelper(page, 'pin-tab', 1500);
  if (IS_MAC) return false;
  await tabMenuCommand({ title }, 'kKoanAddToEssentialsMenuItem');
  return true;
}
const e = await openTab('E');
const pinned = await pin(e, 'E');
const x = await openTab('X');
await front(c);
const unload = async () => {
  await front(c);
  return hasHelper && await askTestHelper(c, { discard: [url('X')] }, 1500) &&
      !!(await listTabs())?.find(t => t.url === url('X'))?.discarded;
};
// The tab a cycling key leads to from `from`: a CDP key, or with `real`
// (keypress2's modifier flags) a real one.
async function cycle(from, code, modifiers, name, real) {
  await front(from);
  if (real) {
    if (!await realKey(name, real, 800)) return null;
  } else {
    await key(from, code, modifiers, name);
    await sleep(800);
  }
  return activeTitle();
}
if (!pinned || !hasHelper) {
  skipped('Ctrl+Tab settings', 'needs test_helper to pin, unload and list tabs (its .flags)');
} else {
  await change('koan.tabs.ctrl_tab_skip_pending', false);
  await change('koan.tabs.ctrl_tab_within_kind', false);
  check('X is unloaded', await unload());
  // With both off Koan passes the key on to Chrome's own Next Tab, which on
  // macOS runs from the key-equivalent path only: a real key there (a CDP
  // key leaves the tab as it is, seen on the Mac).
  let to;
  if (!IS_MAC || realInput()) {
    to = IS_MAC ? await cycle(c, 0, 0, 'PageDown', 'k') : await cycle(c, VK.PGDN, CTRL, 'PageDown');
    check('Ctrl+PgDn by default goes to the next tab, an unloaded one too', to === 'X', to);
  } else {
    skipped('Ctrl+PgDn by default goes to the next tab (Chrome\'s Next Tab)',
            `a CDP key doesn't reach Chrome's own Next Tab on macOS; ${realInputWhyNot()}`);
  }
  check('X is unloaded again', await unload());
  await change('koan.tabs.ctrl_tab_skip_pending', true);
  to = await cycle(c, VK.PGDN, CTRL, 'PageDown');
  check('"Ignore Pending tabs" on: Ctrl+PgDn skips the unloaded tab (round to the Essential)',
        to === 'E', to);
  await change('koan.tabs.ctrl_tab_skip_pending', false);
  await change('koan.tabs.ctrl_tab_within_kind', true);
  const essential = await pageAt(url('E'));
  to = await cycle(essential, VK.PGDN, CTRL, 'PageDown');
  check('"Within Essential or Workspace tabs" on: from an Essential, Ctrl+PgDn stays there',
        to === 'E', to);
  // From C to X (which loads), then on from X.
  await cycle(c, VK.PGDN, CTRL, 'PageDown');
  to = await cycle(await pageAt(url('X')) ?? c, VK.PGDN, CTRL, 'PageDown');
  check('...and from the Space\'s last tab it wraps to the Space\'s first, past the Essential',
        to !== 'E' && to !== 'X', to);
  // A real key takes macOS's key-equivalent path, where Chrome runs its
  // reserved Next Tab before normal priority accelerators: Koan's cycling
  // keys are high priority there (koan_shortcuts.cc).
  if (realInput()) {
    to = await cycle(essential, 0, 0, 'PageDown', 'k');
    check('...also for a real Ctrl+PgDn on the Mac (it stays on the Essential)', to === 'E', to);
    to = await cycle(essential, 0, 0, 'Tab', 'k');
    check('...and a real Ctrl+Tab', to === 'E', to);
  } else {
    skipped('A real Ctrl+PgDn / Ctrl+Tab follows the settings', realInputWhyNot());
  }

  // Chromium's "Ctrl+Tab in recently used order" (chrome://settings >
  // Appearance) wins for Ctrl+Tab, and Koan's rows say so (Zen's warning).
  await setChromePref('browser.ctrl_tab_mru', true);
  await front(anchor);
  await front(b);
  to = await cycle(c, VK.TAB, CTRL, 'Tab');
  check('Chromium\'s recently used Ctrl+Tab on: Ctrl+Tab goes to the tab shown before',
        to === 'B', to);
  await openSettings();
  await showPaneOf('koan.tabs.ctrl_tab_within_kind');
  const note = `koanSettingsRoot.querySelector('[data-note="koan.tabs.ctrl_tab_within_kind"]')`;
  check('...and Koan\'s Ctrl+Tab row notes it, like Zen',
        await evalIn(settings, `(n => !!n && !n.hidden &&
            n.textContent.startsWith('Ctrl+Tab will cycle'))(${note})`));
  await setChromePref('browser.ctrl_tab_mru', false);
  check('...and drops the note once it is off again',
        await waitFor(() => evalIn(settings, `${note}?.hidden !== false`), 3000));
  await setRow('koan.tabs.ctrl_tab_within_kind', false);
  await closeSettings();
}

// ---- 5b. Open other sites from a pinned tab's URL bar in a new tab.
// Another site: localhost, where the pages are 127.0.0.1.
const otherSite = `http://localhost:${server.port}/p?t=Other`;
async function typeInUrlBar(from, text) {
  await front(from);
  const vs = await viewTree();
  const pill = vs.find(v => v.cls === 'KoanUrlPill' && v.shown);
  if (!pill) return false;
  await clickThroughRoot(vs, pill);
  const field = await waitFor(async () => (await viewTree()).find(
      v => v.cls === 'OmniboxViewViews' && v.shown), 4000);
  if (!field) return false;
  // Each key to the field found again: the suggestions change the view tree,
  // so a node id from before names another view.
  const toField = async event => {
    const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
    await ui.send('DOM.enable');
    const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
    const find = n => {
      const attrs = n.attributes ?? [];
      if (n.nodeName === 'View' && attrs[attrs.indexOf('class') + 1] === 'OmniboxViewViews') {
        return n.nodeId;
      }
      for (const child of n.children ?? []) {
        const found = find(child);
        if (found) return found;
      }
      return null;
    };
    const nodeId = find(doc);
    if (nodeId) await ui.send('DOM.dispatchKeyEvent', { nodeId, event });
    ui.close();
  };
  for (const ch of text) {
    await toField({ type: 'keyPressed', keyCode: 0, code: 0, flags: 0,
                    key: ch.codePointAt(0), isChar: true });
  }
  await sleep(1000);
  await toField({ type: 'keyPressed', keyCode: 13, code: 0, flags: 0, key: 0, isChar: false });
  await sleep(1500);
  return true;
}
const essentialE = await pageAt(url('E'));
if (!pinned || !essentialE) {
  skipped('Open other sites from a pinned tab in a new tab', 'no Essential');
} else if (!await typeInUrlBar(essentialE, otherSite)) {
  skipped('Open other sites from a pinned tab in a new tab', 'no sidebar URL bar on screen');
} else {
  const opened = await waitFor(() => pageAt(otherSite), 5000);
  check('"Open other sites from a pinned tab" on (default): another site opens in a new tab',
        !!opened && !!await pageAt(url('E')), opened ? 'E navigated' : 'no new tab');
  if (opened) await browserCall('Target.closeTarget', { targetId: opened.id });
  await sleep(800);
  await change('koan.tabs.open_pinned_in_new_tab', false);
  await typeInUrlBar(await pageAt(url('E')), otherSite);
  const navigated = await waitFor(async () => !await pageAt(url('E')) && pageAt(otherSite), 5000);
  check('...off: the pinned tab goes there itself', !!navigated);
  if (navigated) {
    await onPage(navigated, cdp => cdp.send('Page.navigate', { url: url('E') }));
    await waitFor(() => pageAt(url('E')), 5000);
  }
  await change('koan.tabs.open_pinned_in_new_tab', true);
}

// ---- 6. Close Tab Shortcut Behavior, each option, on a new Essential.
// Cmd+W is the main menu's on macOS (CDP keys never reach it): real input.
const EXPECT = {
  'reset': t => t?.active && t.url === url('Close-reset'),
  'switch': t => t && !t.active && !t.discarded && t.url.endsWith('&moved'),
  'reset-switch': t => t && !t.active && t.url === url('Close-reset-switch'),
  'unload-switch': t => t && !t.active && t.discarded,
  'reset-unload-switch': t => t && !t.active && t.discarded,
  'close': t => !t,
};
if (!pinned || !hasHelper || (IS_MAC && !realInput())) {
  skipped('Close Tab Shortcut Behavior (each option)',
          IS_MAC && !realInput() ? `Cmd+W is a main-menu key; ${realInputWhyNot()}`
                                 : 'needs test_helper (its .flags)');
} else {
  for (const option of Object.keys(EXPECT)) {
    await change('koan.pinned_tabs.close_shortcut_behavior', option);
    const name = `Close-${option}`;
    const page = await openTab(name);
    await pin(page, name);
    await onPage(page, cdp => cdp.send('Page.navigate', { url: `${url(name)}&moved` }));
    await sleep(1000);
    const now = await pageAt(`${url(name)}&moved`);
    await front(now);
    if (IS_MAC) {
      await realKey('w', 'c', 1500);
    } else {
      await key(now, VK.W, CTRL, 'w');
    }
    const ok = await waitFor(async () => EXPECT[option]((await listTabs())
        ?.find(t => t.pinned && t.url.startsWith(url(name)))), 5000);
    const tab = (await listTabs())?.find(t => t.pinned && t.url.startsWith(url(name)));
    check(`Close Tab Shortcut Behavior "${option}" does what it says on an Essential`, ok,
          J(tab && { url: titleOf(tab), active: tab.active, discarded: tab.discarded }));
  }
  await change('koan.pinned_tabs.close_shortcut_behavior', 'reset-unload-switch');
}

// ---- 7. Spaces: wrap around with the Next / Previous Space shortcuts.
// The test's window is the only one here (the settings page is closed).
const spacesOfMain = async () => {
  const s = spaceButtons(await viewTree());
  return { names: s.map(n => n.name), active: s.find(n => n.active)?.name };
};
const moveSpace = async forward => {
  const page = await visiblePage();
  await key(page, forward ? VK.RIGHT : VK.LEFT, ACCEL | ALT, forward ? 'ArrowRight' : 'ArrowLeft');
  await sleep(1500);
  return (await spacesOfMain()).active;
};
await front(a);
const created = await createSpace(a, 'Two') &&
    await waitFor(async () => (await spacesOfMain()).names.length >= 2, 5000);
if (!created) {
  check('Create a second Space', false, J(await spacesOfMain()));
} else {
  // The new Space's tab, a page of its own.
  await onPage(await visiblePage(), cdp => cdp.send('Page.navigate', { url: url('S2') }));
  await sleep(1000);
  const { names } = await spacesOfMain();
  const [first, last] = [names[0], names.at(-1)];
  // change() ends on the test's first tab, in the first Space: each step
  // starts from the Space it names. Clicking the Space's button gets there
  // whatever wrap-around says.
  const goTo = async name => {
    const vs = await viewTree();
    const button = spaceButtons(vs).find(n => n.name === name);
    if (button) await clickThroughRoot(vs, button.view);
    await sleep(1200);
    return (await spacesOfMain()).active === name;
  };
  await change('koan.workspaces.wrap_around_navigation', true);
  check('At the last Space', await goTo(last), (await spacesOfMain()).active);
  let at = await moveSpace(true);
  check('Wrap around on (default): Next Space from the last goes to the first', at === first, at);
  at = await moveSpace(false);
  check('...and Previous Space from the first to the last', at === last, at);
  await change('koan.workspaces.wrap_around_navigation', false);
  await goTo(last);
  at = await moveSpace(true);
  check('Wrap around off: Next Space from the last stays there', at === last, at);
  await goTo(first);
  at = await moveSpace(false);
  check('...and Previous Space from the first stays there', at === first, at);
  await change('koan.workspaces.wrap_around_navigation', true);

  // Natural scrolling: a sideways wheel over the sidebar's Space header.
  if (IS_MAC) {
    skipped('Natural scrolling and the Space-switch modifier key',
            'need native wheel events, which UI DevTools can\'t send on macOS');
  } else {
    const wheel = async direction => {
      const all = await viewTree();
      const header = all.find(v => v.cls === 'KoanSpaceHeaderView' && v.shown && v.h > 0);
      const root = header && all.find(v => v.widget === header.widget && v.path.length === 0);
      if (!root) return null;
      const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
      await ui.send('DOM.enable');
      await ui.send('DOM.getDocument', { depth: -1 });
      await ui.send('DOM.dispatchMouseEvent', { nodeId: root.nodeId, event: {
        type: 'mouseWheel', x: header.ax + 30, y: header.ay + 10, button: 'none',
        wheelDirection: direction } });
      ui.close();
      await sleep(1500);
      return (await spacesOfMain()).active;
    };
    // At the first Space: a wheel to the right goes on to the second;
    // natural scrolling turns it round (to the last, wrapping).
    at = await wheel('right');
    check('A sideways wheel over the sidebar goes to the next Space', at === names[1], at);
    await wheel('left');
    await change('koan.workspaces.natural_scroll', true);
    at = await wheel('right');
    check('...natural scrolling on: the same wheel goes the other way', at === last, at);
    await change('koan.workspaces.natural_scroll', false);
    skipped('The Space-switch modifier key', 'UI DevTools wheel events carry no modifier keys; ' +
            'koan_space_navigation_unittest has the key test');
  }
  // Back to the first Space, where the tabs above are.
  for (let i = 0; i < names.length && (await spacesOfMain()).active !== first; i++) {
    await moveSpace(false);
  }
}

// ---- 8. Chromium's rows that Koan's layout overrides.
const appearanceId = (await browserCall('Target.createTarget',
    { url: 'chrome://settings/appearance', newWindow: true })).result.targetId;
const appearance = await waitFor(async () => (await pages()).find(p => p.id === appearanceId),
                                 8000);
const DEEP = `const find = (root, s) => root.querySelector(s) ??
    [...root.querySelectorAll('*')].map(el => el.shadowRoot && find(el.shadowRoot, s))
        .find(Boolean) ?? null;`;
// Whether `selector` (through every shadow root) is shown.
const state = selector => evalIn(appearance, `(() => { ${DEEP}
  const el = find(document, ${J(selector)});
  return !el ? 'absent' : el.checkVisibility() ? 'shown' : 'hidden';
})()`);
check('Chromium\'s Appearance section draws',
      await waitFor(async () => await state('#colorSchemeModeSelect') === 'shown', 8000));
for (const [selector, what] of [
    ['#showBookmarksBar', 'Show bookmarks bar (Koan\'s own row decides)'],
    ['#bookmarksBarVisibilityDropdown', 'Bookmarks bar visibility (Koan\'s own row decides)'],
    ['#showTabSearchButton', 'Show tab search button (the sidebar has none)'],
    ['#showOrganizerPanelButton', 'Show tab search button (the sidebar has none)'],
    ['#autoPinNewTabGroups', 'Auto-pin new tab groups (Spaces are never saved groups)'],
    ['#hoverCardImagesToggle', 'Hover card previews (the sidebar shows no hover cards)'],
    ['#hoverCardMemoryUsageToggle', 'Hover card memory usage (no hover cards)'],
    ['#tabStripPosition', 'Tab strip position (always the sidebar)']]) {
  const s = await state(selector);
  check(`Chromium's Appearance: "${what}" is not shown`, s !== 'shown', s);
}

// Mode: a change of Chromium's menu or of Koan's Look > Mode becomes the
// other's (the shadcn style, Koan's default; in the Zen style Chromium's
// alone decides). Nothing is written at start: a fresh profile keeps
// Chromium's scheme (Device), which web pages' prefers-color-scheme follows.
const SCHEME = 'browser.theme.color_scheme2';
await openSettings();
const [lookStyle, lookMode] = [await prefValue('koan.look.style'), await prefValue('koan.look.mode')];
await closeSettings();
// Before this test changes a mode (section 1 doesn't touch the look).
check('At start Koan leaves Chromium\'s colour scheme alone (websites keep Device)',
      await readPref(SCHEME) === 0, J(await readPref(SCHEME)));
if (lookStyle !== 'shadcn') {
  skipped('Chromium\'s Mode and Koan\'s Look > Mode', `the look is ${lookStyle}, not shadcn`);
} else {
  await change('koan.look.mode', 'light');
  check('Koan\'s Mode Light sets Chromium\'s colour scheme to Light',
        await waitFor(async () => await readPref(SCHEME) === 1, 4000), J(await readPref(SCHEME)));
  const picked = await evalIn(appearance, `(() => { ${DEEP}
    const select = find(document, '#colorSchemeModeSelect');
    const option = [...(select?.options ?? [])].find(o => /dark/i.test(o.textContent));
    if (!option) return false;
    select.value = option.value;
    select.dispatchEvent(new Event('change'));
    return true;
  })()`);
  await sleep(1000);
  await openSettings();
  check('Chromium\'s Mode menu Dark sets Koan\'s Mode to Dark',
        picked && await waitFor(async () => await prefValue('koan.look.mode') === 'dark', 4000),
        J(await prefValue('koan.look.mode')));
  check('...and Koan\'s row shows it', await shown('koan.look.mode') === 'dark',
        J(await shown('koan.look.mode')));
  await setRow('koan.look.mode', lookMode ?? 'dark');
  await closeSettings();
}
await browserCall('Target.closeTarget', { targetId: appearanceId });
await sleep(800);

// ---- 9. Restore pinned tabs to their pinned URL, after a quit and a start.
let relaunched = null;
process.on('exit', () => relaunched?.kill());
const up = async () => !!await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).catch(() => null);
async function quit() {
  const v = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
  const browser = await connect(v.webSocketDebuggerUrl);
  await browser.send('Browser.close');
  browser.close();
  for (let i = 0; i < 60 && await up(); i++) await sleep(500);
  await sleep(2000);
}
async function start() {
  relaunched?.kill();
  // Its log goes after the first Koan's, for fatalInLog().
  const log = process.env.KOAN_LOG ? fs.openSync(process.env.KOAN_LOG, 'a') : 'ignore';
  relaunched = spawn(args[0], args.slice(1), { stdio: ['ignore', log, log] });
  for (let i = 0; i < 60 && !await up(); i++) await sleep(500);
  // The restore, the window sync join and their updates.
  await sleep(6000);
}
// Essential E's URL after a start, from test_helper, asked by a page of the
// restored window that loaded (the one in front).
async function essentialAfterStart() {
  anchor = await waitFor(async () => {
    for (const p of await pages()) {
      if (p.type === 'page' && p.url.startsWith(server.url('/p')) &&
          await evalIn(p, 'document.visibilityState') === 'visible') return p;
    }
    return null;
  }, 10000);
  const tab = anchor && (await listTabs(anchor))?.find(
      t => t.pinned && (t.url === url('E') || t.url === `${url('E')}&moved`));
  return tab?.url ?? null;
}
async function moveEssential() {
  const page = await pageAt(url('E')) ?? await pageAt(`${url('E')}&moved`);
  if (!page) return false;
  await onPage(page, cdp => cdp.send('Page.navigate', { url: `${url('E')}&moved` }));
  return !!await waitFor(() => pageAt(`${url('E')}&moved`), 5000);
}
if (!pinned || !hasHelper || !args.length) {
  skipped('Restore pinned tabs to their pinned URL after a restart',
          !args.length ? 'no KOAN_CHROME_ARGS to start Koan again'
                       : 'needs test_helper to list the restored tabs (its .flags)');
} else if (!await moveEssential()) {
  check('Essential E, moved to another page, to restart with', false);
} else {
  await change('koan.pinned_tabs.restore_to_pinned_url', true);
  await quit();
  await start();
  let restored = await essentialAfterStart();
  check('Restore to the pinned URL on: the Essential comes back at its pinned URL',
        restored === url('E'), restored ?? 'not found');
  await change('koan.pinned_tabs.restore_to_pinned_url', false);
  // Shown, so it loads and can be moved again.
  await askTestHelper(anchor, { activate: url('E') }, 2500);
  check('...moved again', await moveEssential());
  await quit();
  await start();
  restored = await essentialAfterStart();
  check('...off: it comes back where it was', restored === `${url('E')}&moved`,
        restored ?? 'not found');
}

check('Nothing fatal in the log', !fatalInLog(), fatalInLog());
if (relaunched) await quit();
server.close();
finish();
