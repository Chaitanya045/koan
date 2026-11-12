// End-to-end test of Zen's Tab Management settings against a real build:
// Back in a tab with no history closes it and returns to its opener;
// closing a tab selects the most recently used one (also in Spaces, and
// past collapsed groups in a private window, which has tab groups instead
// of Spaces); Ctrl+Tab can stay within the Essentials or the other tabs,
// defers to Chromium's recently used order, and leaves popups alone. Each
// with its setting on and off (set through Koan's settings'
// handler).
//
// On macOS (#270): ⌘T, ⌘W and Ctrl+Tab are main-menu keys there, which CDP
// keys never reach, so they are real keys (lib/mac.mjs menuKey(), with
// KOAN_INPUT=real). Without real input the checks of a ⌘W or Ctrl+Tab skip
// (the tab is then closed through CDP, so the steps after it run), and ⌘T's
// tabs come from Target.createTarget (also at the end, in the shown Space).
// Back is Cmd+Left, Chromium's group keys Cmd+Ctrl, a private window
// Cmd+Shift+P (Koan's). The tab's menu is a native NSMenu there, so the
// test_helper extension (tab_management.mac.flags) pins the opener for
// "Add to Essentials" (an Essential is a pinned tab); the opener is a copy
// of the local page served on 127.0.0.1, which test_helper hears.
//
//   scripts/e2e/run.sh tab_management
import fs from 'node:fs';
import {
  ACCEL, ALT, CTRL, IS_MAC, META, NATIVE_MENUS, SHIFT, browserCall, check, click, clickView,
  createSpace, evalIn, finish, front, key, onPage, pages, settingsCall, sleep, tabMenuCommand,
  views, viewMouse, viewTree, waitFor, showPane,
} from './lib/koan.mjs';
import { askTestHelper, menuKey as macMenuKey, menuKeyWhyNot, skipped, startServer }
    from './lib/mac.mjs';

const VK = { T: 84, W: 87, D: 68, N: 78, P: 80, X: 88, TAB: 9, LEFT: 37, PGUP: 33, PGDN: 34 };
// Back (Alt+Left; Cmd+Left on macOS) and Chromium's tab group keys
// (Shift+Alt; Cmd+Ctrl on macOS).
const BACK = IS_MAC ? META : ALT;
const GROUP = IS_MAC ? META | CTRL : SHIFT | ALT;

const [opener] = await pages();
if (NATIVE_MENUS) {
  const html = fs.readFileSync(new URL('./pages/tab_management.html', import.meta.url), 'utf8');
  const server = await startServer({ '/tab_management.html': html });
  await onPage(opener, c => c.send('Page.navigate', { url: server.url('/tab_management.html') }));
  await waitFor(async () => await evalIn(opener, 'document.readyState') === 'complete' &&
                            await evalIn(opener, 'location.protocol') === 'http:');
}

// Ctrl+T / Ctrl+W / Ctrl+Tab on `page`: on macOS a real ⌘T / ⌘W / ⌃⇥
// (keypress2 `flags`), the main menu's keys. Whether it was sent.
const menuKey = (page, code, modifiers, name, flags) =>
  macMenuKey(page, name, flags, () => key(page, code, modifiers, name), 1000);
// Ctrl+W on `tab`. Without real input on macOS the check `name` skips and
// the tab is closed through CDP, for the steps after it; false then.
async function closeTab(tab, name) {
  if (await menuKey(tab, VK.W, CTRL, 'w', 'c')) return true;
  skipped(name, menuKeyWhyNot());
  await browserCall('Target.closeTarget', { targetId: tab.id });
  await sleep(1500);
  return false;
}

// The settings page, in a window of its own so it doesn't count as a tab.
const settingsId = (await browserCall('Target.createTarget',
    { url: 'chrome://settings/koan/looks', newWindow: true })).result.targetId;
const settings = await waitFor(async () => (await pages()).find(p => p.id === settingsId));
await waitFor(() => evalIn(settings, `koanSettingsRoot?.getElementById('pane')?.childElementCount > 0`));
const setPref = async (pref, value) => {
  const ok = await settingsCall(settings, 'koanSetPref', pref, value);
  await sleep(300);
  return ok;
};

const exists = async page => (await pages()).some(p => p.id === page.id);
const visible = page => evalIn(page, 'document.visibilityState').then(v => v === 'visible');
// Of `tabs`, the one that is showing.
async function activeOf(tabs) {
  for (const tab of tabs) {
    if (await visible(tab)) return tab;
  }
  return null;
}
const title = page => evalIn(page, 'document.title');

async function openChild() {
  await front(opener);
  const before = new Set((await pages()).map(p => p.id));
  await click(opener, '#child');
  return waitFor(async () => (await pages()).find(p => !before.has(p.id)));
}

// Close on Back with no history (Zen's default: on).
let child = await openChild();
check('A link opens a child tab', child !== undefined);
await waitFor(() => visible(child));
await key(child, VK.LEFT, BACK, 'ArrowLeft');
check('Back in a tab with no history closes it',
      await waitFor(async () => !await exists(child)));
check('...and returns to the tab that opened it', await waitFor(() => visible(opener)));

check('The setting can be switched off',
      await setPref('koan.tabs.close_on_back_with_no_history', false));
child = await openChild();
await waitFor(() => visible(child));
await key(child, VK.LEFT, BACK, 'ArrowLeft');
await sleep(1500);
check('...then Back there does nothing', await exists(child));
await browserCall('Target.closeTarget', { targetId: child.id });
await waitFor(async () => !await exists(child));
await setPref('koan.tabs.close_on_back_with_no_history', true);

// New tabs (Ctrl+T, so no opener decides; the profile turns off Zen's
// replace-newtab so it opens one right away) titled `name`, at the end.
async function newTab(name, from = opener) {
  const before = new Set((await pages()).map(p => p.id));
  if (!await menuKey(from, VK.T, CTRL, 't', 'c')) {
    await browserCall('Target.createTarget', { url: 'about:blank' });
  }
  const tab = await waitFor(async () => (await pages()).find(p => !before.has(p.id)));
  await onPage(tab, c => c.send('Page.navigate',
      { url: `data:text/html,${encodeURIComponent(`<title>${name}</title>`)}` }));
  await waitFor(async () => await title(tab) === name);
  return tab;
}

// Close the most recently used tab, not a neighbour (Zen's default: on).
await front(opener);
const b = await newTab('B');
const c = await newTab('C');
let d = await newTab('D');
await front(b);
await front(d);
let now;
if (await closeTab(d, 'Closing a tab selects the most recently used one')) {
  now = await waitFor(() => activeOf([opener, b, c]));
  check('Closing a tab selects the most recently used one',
        now?.id === b.id, now ? await title(now) : 'none');
}

check('The setting can be switched off',
      await setPref('koan.tabs.select_recently_used_on_close', false));
d = await newTab('D');
await front(b);
await front(d);
if (await closeTab(d, '...then closing selects the neighbour, as in Chrome')) {
  now = await waitFor(() => activeOf([opener, b, c]));
  check('...then closing selects the neighbour, as in Chrome',
        now?.id === c.id, now ? await title(now) : 'none');
}
await setPref('koan.tabs.select_recently_used_on_close', true);

// Ctrl+Page Down/Up within the Essentials or the other tabs (Zen's default:
// off). Ctrl+Tab follows Chromium's recently used order here: the profile
// is seeded with it on (scripts/e2e/prefs/tab_management.json).
await front(opener);
// An Essential (Ctrl+Shift+D would pin it to its workspace instead).
if (NATIVE_MENUS) {
  check('The opener becomes an Essential (macOS: test_helper pins it)',
        await askTestHelper(opener, 'pin-tab', 1500));
} else {
  await tabMenuCommand({ title: await title(opener) }, 'kKoanAddToEssentialsMenuItem');
}
await front(c);
await key(c, VK.PGDN, CTRL, 'PageDown');
now = await waitFor(() => activeOf([opener, b, c]));
check('Ctrl+PgDn from the last tab goes on to the Essentials by default',
      now?.id === opener.id, now ? await title(now) : 'none');

check('"Ctrl+Tab cycles within Essential or Workspace tabs only" can be on',
      await setPref('koan.tabs.ctrl_tab_within_kind', true));
await front(c);
await key(c, VK.PGDN, CTRL, 'PageDown');
now = await waitFor(() => activeOf([opener, b, c]));
check('...then Ctrl+PgDn skips the Essentials', now?.id === b.id,
      now ? await title(now) : 'none');
await key(b, VK.PGUP, CTRL, 'PageUp');
now = await waitFor(() => activeOf([opener, b, c]));
check('...both ways (Ctrl+PgUp)', now?.id === c.id, now ? await title(now) : 'none');
await front(opener);
await key(opener, VK.PGDN, CTRL, 'PageDown');
await sleep(800);
check('...and from an Essential stays among the Essentials',
      (await activeOf([opener, b, c]))?.id === opener.id);

// Chromium's "Ctrl+Tab in recently used order" wins for Ctrl+Tab over
// "within Essential or Workspace tabs only", and the page says so under
// Koan's row. Since #214 Koan walks that order itself, as Zen's list: the
// Space's tabs and the Essentials, most recently shown first. The tab shown
// before C is the Essential (above), which cycling within kind would skip
// for B. (Should the session give the tabs no activation times, the order
// falls back to the strip's, where the Essential is first too.)
await front(c);
const CTRL_TAB_MRU =
    'With Chromium\'s recently used Ctrl+Tab on, Ctrl+Tab goes to the last tab shown';
if (await menuKey(c, VK.TAB, CTRL, 'Tab', 'k')) {
  now = await waitFor(async () => {
    const tab = await activeOf([opener, b, c]);
    return tab && tab.id !== c.id ? tab : null;
  });
  check(CTRL_TAB_MRU, now?.id === opener.id, now ? await title(now) : 'none');
} else {
  skipped(CTRL_TAB_MRU, menuKeyWhyNot());
}
await evalIn(settings, `${showPane('tabs')}`);
check('...and the Koan row notes it, like Zen',
      await evalIn(settings, `(() => {
        const n = koanSettingsRoot.querySelector('[data-note="koan.tabs.ctrl_tab_within_kind"]');
        return n && !n.hidden && n.textContent.startsWith('Ctrl+Tab will cycle by recently used');
      })()`));

// Windows without tab switching (popups) leave the keys to the page.
const popupBefore = new Set((await pages()).map(p => p.id));
await evalIn(opener,
    `window.open('tab_management.html?popup', '', 'popup,width=500,height=400')`, true);
const popup = await waitFor(async () => (await pages()).find(p => !popupBefore.has(p.id)));
await waitFor(() => evalIn(popup, 'Array.isArray(window.keys)'));
await key(popup, VK.PGDN, CTRL, 'PageDown');
await key(popup, VK.TAB, CTRL, 'Tab');
check('A popup window passes Ctrl+PgDn and Ctrl+Tab to its page',
      await waitFor(() => evalIn(popup, 'window.keys.length === 2')),
      JSON.stringify(await evalIn(popup, 'window.keys')));
await browserCall('Target.closeTarget', { targetId: popup.id });
await setPref('koan.tabs.ctrl_tab_within_kind', false);

// Spaces: closing picks among the Space's tabs and the Essentials.
async function newWorkspace(name, from) {
  const before = new Set((await pages()).map(p => p.id));
  await createSpace(from, name);
  const tab = await waitFor(async () => (await pages()).find(p => !before.has(p.id)));
  await onPage(tab, c => c.send('Page.navigate',
      { url: `data:text/html,${encodeURIComponent(`<title>${name}</title>`)}` }));
  await waitFor(async () => await title(tab) === name);
  return tab;
}
await front(c);
const w1 = await newWorkspace('W1', c);
const w2 = await newTab('W2', w1);
const w3 = await newTab('W3', w2);
await front(w2);
await front(w1);
await front(w3);
const remaining = () => [opener, b, c, w1, w2];
if (await closeTab(w3, 'In a Space, closing selects its most recently used tab')) {
  now = await waitFor(() => activeOf(remaining()));
  check('In a Space, closing selects its most recently used tab',
        now?.id === w1.id, now ? await title(now) : 'none');
}
const w4 = await newTab('W4', w1);
await front(opener);
await front(w4);
if (await closeTab(w4, '...or an Essential, if that was used last')) {
  now = await waitFor(() => activeOf(remaining()));
  check('...or an Essential, if that was used last',
        now?.id === opener.id, now ? await title(now) : 'none');
}
// (Closing a group's last tab asks "Delete group?" upstream; the test
// profile answers "don't ask again", tab_groups.deletion.skip_dialog_on_close_tab.)
const x1 = await newWorkspace('X1', opener);
const LAST_OF_SPACE = 'Closing the last tab of a Space selects the most recently used Essential';
if (await closeTab(x1, LAST_OF_SPACE)) {
  now = await waitFor(() => activeOf(remaining()));
  check(LAST_OF_SPACE,
        now?.id === opener.id, now ? await title(now) : 'none');
}

// A private window has no Spaces, so Chromium's tab groups: a focused group
// keeps its header, and closing a tab skips collapsed groups.
const opened = async action => {
  const before = new Set((await pages()).map(p => p.id));
  await action();
  return waitFor(async () => (await pages()).find(p => !before.has(p.id)));
};
// Group headers on screen (a Space's is hidden: it has no height).
const shownHeaders = async () => (await viewTree())
    .filter(v => v.cls === 'TabGroupHeaderView' && v.shown && v.h > 0);
const headers = async () => (await shownHeaders()).length;
const p1 = await opened(() => key(opener, VK.P, ACCEL | SHIFT, 'p'));
// Chromium's Shift+Alt+P: a new tab in a new group (in a Spaces window it
// would be a new Space).
const g1 = await opened(() => key(p1, VK.P, GROUP, 'p'));
check('A private window has tab groups', !!g1 && await headers() === 1,
      `${await headers()} group header(s)`);
await key(g1, VK.X, GROUP, 'x');  // focus the group
await sleep(1000);
check('...and a focused group keeps its header', await headers() === 1,
      `${await headers()} group header(s)`);
await key(g1, VK.X, GROUP, 'x');  // past the only group: unfocus
await sleep(1000);
await front(p1);
// (A private tab: Target.createTarget can't stand in for ⌘T here, so the
// check skips without real input.)
let sent = false;
const p2 = await opened(async () => { sent = await menuKey(p1, VK.T, CTRL, 't', 'c'); });
if (!sent) {
  skipped('Closing skips tabs in a collapsed group', menuKeyWhyNot());
} else {
  await front(p1);
  await front(g1);
  await front(p2);
  const [groupHeader] = await shownHeaders();
  await viewMouse(groupHeader, 'mousePressed', 10, 10);  // collapse
  await viewMouse(groupHeader, 'mouseReleased', 10, 10);
  await sleep(1000);
  if (await menuKey(p2, VK.W, CTRL, 'w', 'c')) {
    now = await waitFor(() => activeOf([p1, g1]));
    check('Closing skips tabs in a collapsed group', now?.id === p1.id,
          now?.id === g1.id ? 'the collapsed group\'s tab' : 'none');
  } else {
    skipped('Closing skips tabs in a collapsed group', menuKeyWhyNot());
  }
}

finish();
