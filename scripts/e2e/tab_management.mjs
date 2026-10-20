// End-to-end test of Zen's Tab Management settings against a real build:
// Back in a tab with no history closes it and returns to its opener;
// closing a tab selects the most recently used one (also in workspaces and
// past collapsed groups); Ctrl+Tab can stay within the Essentials or the
// other tabs, defers to Chromium's recently used order, and leaves popups
// alone. Each with its setting on and off (set through
// chrome://koan-settings' handler).
//
//   scripts/e2e/run.sh tab_management
import {
  ALT, CTRL, SHIFT, browserCall, check, click, clickView, evalIn, finish,
  front, key, onPage, pages, settingsCall, sleep, views, waitFor,
} from './lib/koan.mjs';

const VK = { T: 84, W: 87, D: 68, N: 78, TAB: 9, LEFT: 37, PGUP: 33, PGDN: 34 };

const [opener] = await pages();

// The settings page, in a window of its own so it doesn't count as a tab.
const settingsId = (await browserCall('Target.createTarget',
    { url: 'chrome://koan-settings', newWindow: true })).result.targetId;
const settings = await waitFor(async () => (await pages()).find(p => p.id === settingsId));
await waitFor(() => evalIn(settings, `document.querySelectorAll('.category').length > 0`));
const setPref = async (pref, value) => {
  const ok = await settingsCall(settings, 'setPref', pref, value);
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
await key(child, VK.LEFT, ALT, 'ArrowLeft');
check('Back in a tab with no history closes it',
      await waitFor(async () => !await exists(child)));
check('...and returns to the tab that opened it', await waitFor(() => visible(opener)));

check('The setting can be switched off',
      await setPref('koan.tabs.close_on_back_with_no_history', false));
child = await openChild();
await waitFor(() => visible(child));
await key(child, VK.LEFT, ALT, 'ArrowLeft');
await sleep(1500);
check('...then Back there does nothing', await exists(child));
await browserCall('Target.closeTarget', { targetId: child.id });
await waitFor(async () => !await exists(child));
await setPref('koan.tabs.close_on_back_with_no_history', true);

// New tabs (Ctrl+T, so no opener decides) titled `name`, at the end.
async function newTab(name, from = opener) {
  const before = new Set((await pages()).map(p => p.id));
  await key(from, VK.T, CTRL, 't');
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
await key(d, VK.W, CTRL, 'w');
let now = await waitFor(() => activeOf([opener, b, c]));
check('Closing a tab selects the most recently used one',
      now?.id === b.id, now ? await title(now) : 'none');

check('The setting can be switched off',
      await setPref('koan.tabs.select_recently_used_on_close', false));
d = await newTab('D');
await front(b);
await front(d);
await key(d, VK.W, CTRL, 'w');
now = await waitFor(() => activeOf([opener, b, c]));
check('...then closing selects the neighbour, as in Chrome',
      now?.id === c.id, now ? await title(now) : 'none');
await setPref('koan.tabs.select_recently_used_on_close', true);

// Ctrl+Page Down/Up within the Essentials or the other tabs (Zen's default:
// off). Ctrl+Tab follows Chromium's recently used order here: the profile
// is seeded with it on (scripts/e2e/prefs/tab_management.json).
await front(opener);
await key(opener, VK.D, CTRL | SHIFT, 'd');  // Pin: an Essential.
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

// Chromium's "Ctrl+Tab in recently used order" wins for Ctrl+Tab, and the
// page says so under Koan's row. Chromium ranks tabs by window focus, which
// a headless session doesn't have, so check that Koan's cycling (which would
// go to B, skipping the Essential) yields.
await front(c);
await key(c, VK.TAB, CTRL, 'Tab');
await sleep(800);
now = await activeOf([opener, b, c]);
check('With Chromium\'s recently used Ctrl+Tab on, Koan\'s cycling yields to it',
      now?.id !== b.id, now ? await title(now) : 'none');
await evalIn(settings, `document.querySelector('#category-tabs').click()`);
check('...and the Koan row notes it, like Zen',
      await evalIn(settings, `(() => {
        const n = document.querySelector('[data-note="koan.tabs.ctrl_tab_within_kind"]');
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

// Tabs shown in the sidebar (collapsed and hidden ones have no height).
const shownTabs = async () =>
  (await views()).filter(v => v.cls === 'TabView' && v.shown && v.h > 1).length;
const allTabsBase = await shownTabs();

// Workspaces: closing picks among the workspace's tabs and the Essentials.
async function newWorkspace(name, from) {
  const before = new Set((await pages()).map(p => p.id));
  await key(from, VK.N, CTRL | ALT, 'n');
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
await key(w3, VK.W, CTRL, 'w');
const remaining = () => [opener, b, c, w1, w2];
now = await waitFor(() => activeOf(remaining()));
check('In a workspace, closing selects its most recently used tab',
      now?.id === w1.id, now ? await title(now) : 'none');
const w4 = await newTab('W4', w1);
await front(opener);
await front(w4);
await key(w4, VK.W, CTRL, 'w');
now = await waitFor(() => activeOf(remaining()));
check('...or an Essential, if that was used last',
      now?.id === opener.id, now ? await title(now) : 'none');
// (Closing a group's last tab asks "Delete group?" upstream; the test
// profile answers "don't ask again", tab_groups.deletion.skip_dialog_on_close_tab.)
const x1 = await newWorkspace('X1', opener);
await key(x1, VK.W, CTRL, 'w');
now = await waitFor(() => activeOf(remaining()));
check('Closing the last tab of a workspace selects the most recently used Essential',
      now?.id === opener.id, now ? await title(now) : 'none');

// Collapsed groups are skipped: in "All tabs", collapse workspace 1.
// From the first workspace, Ctrl+Alt+Left shows all tabs.
for (let i = 0; i < 2 && await shownTabs() !== allTabsBase + 2; i++) {
  await key(opener, VK.LEFT, CTRL | ALT, 'ArrowLeft');
  await sleep(500);
}
check('All tabs are shown again', await shownTabs() === allTabsBase + 2,
      `${await shownTabs()} tabs`);
await front(b);
await front(w1);
await front(c);
await clickView('TabGroupHeaderView');
check('Clicking the workspace\'s header collapses it',
      await waitFor(async () => await shownTabs() === allTabsBase),
      `${await shownTabs()} tabs`);
await key(c, VK.W, CTRL, 'w');
now = await waitFor(() => activeOf([opener, b, w1, w2]));
check('Closing skips tabs in a collapsed group',
      now?.id === b.id, now ? await title(now) : 'none');

finish();
