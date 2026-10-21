// End-to-end test of chrome://koan-settings against a real build: the page
// lists Koan's settings like Zen's preferences, changes apply live (layout,
// compact mode, workspace tint), incognito windows and the page agree, the
// page follows changes made elsewhere, the handler refuses what the page
// can't set, and the shortcut editor rebinds, refuses conflicts, clears and
// resets keys, each checked by what the keys then do.
//
//   scripts/e2e/run.sh settings
//
// scripts/e2e/prefs/settings.json seeds a custom key (Open Downloads:
// Ctrl+Alt+R) that is another action's default (Toggle Reader Mode).
// With SHOTS=<dir>, also saves screenshots of the page (light and dark).
import {
  ALT, CTRL, SHIFT, browserCall, check, countViews, evalIn, finish,
  createSpace, front, key, menuMouse, onPage, openMenus, pages, readPref,
  settingsCall, sleep, viewMouse, viewTree, visiblePage, waitFor,
} from './lib/koan.mjs';

const SHOTS = process.env.SHOTS;
const VK = { B: 66, J: 74, P: 80, Q: 81, S: 83, TAB: 9, ESC: 27, BACKSPACE: 8,
             COMMA: 188, N: 78, LEFT: 37 };

async function shot(page, name, colorScheme = 'light') {
  if (!SHOTS) return;
  // Emulation lasts as long as the session, so capture in the same one.
  const r = await onPage(page, async c => {
    await c.send('Emulation.setEmulatedMedia',
        { features: [{ name: 'prefers-color-scheme', value: colorScheme }] });
    await sleep(300);
    return c.send('Page.captureScreenshot', { format: 'png' });
  });
  const fs = await import('node:fs');
  fs.writeFileSync(`${SHOTS}/${name}.png`, Buffer.from(r.result.data, 'base64'));
}

const settingsPages = async () =>
  (await pages()).filter(p => p.url.startsWith('chrome://koan-settings'));
const hotZones = () => countViews('KoanHotZone');
const navViews = () => countViews('KoanSidebarNavView');

// Ctrl+, opens the page (Zen's "Open Preferences").
let [other] = await pages();
await front(other);
await key(other, VK.COMMA, CTRL, ',');
let [settings] = await waitFor(settingsPages);
check('Ctrl+, opens Koan\'s settings', settings !== undefined);
await waitFor(() => evalIn(settings, `document.querySelectorAll('.category').length > 0`));

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

// The handler only sets prefs that have a row, with a value of their type.
check('setPref refuses a pref that has no row',
      await settingsCall(settings, 'setPref', 'koan.workspaces.tint_applied', 5) === false);
check('...and a value of the wrong type',
      await settingsCall(settings, 'setPref', 'koan.single_toolbar', 'x') === false);
check('...leaving the prefs unchanged',
      await readPref('koan.workspaces.tint_applied') === 0 &&
      await readPref('koan.single_toolbar') === true);

// Browser layout: the cards switch single / multiple toolbars live.
const card = label => `[...document.querySelectorAll('.layout-card')]
    .find(c => c.textContent.includes('${label}'))`;
const checked = label => evalIn(settings, `${card(label)}.getAttribute('aria-checked')`);
check('Only Sidebar is selected by default', await checked('Only Sidebar') === 'true');
await evalIn(settings, `${card('Sidebar and Top Toolbar')}.click()`);
check('Choosing "Sidebar and Top Toolbar" switches to multiple toolbars',
      await waitFor(async () => await navViews() === 0));
check('...and the card is selected', await checked('Sidebar and Top Toolbar') === 'true');
await key(settings, VK.B, CTRL | ALT, 'b');
check('The page follows a change made with a shortcut (Ctrl+Alt+B)',
      await waitFor(async () => await checked('Only Sidebar') === 'true'));
check('...back to single toolbar', await waitFor(async () => await navViews() === 1));

// Compact mode.
const compact = `document.querySelector('[data-pref="koan.compact_mode.enabled"] input')`;
await evalIn(settings, `${compact}.click()`);
check('The compact mode checkbox turns compact mode on',
      await waitFor(async () => await hotZones() === 2));
await evalIn(settings, `${compact}.click()`);
check('...and off', await waitFor(async () => await hotZones() === 1));

// What compact mode hides, its flash popup and tabs on the right.
const hide = `document.querySelector('[data-pref="koan.compact_mode.hide"] select')`;
check('The page has what compact mode hides, with Zen\'s choices',
      JSON.stringify(await evalIn(settings, `[...${hide}.options].map(o => o.text)`)) ===
      JSON.stringify(['Hide Tab bar', 'Hide Top bar', 'Hide Both']));
await evalIn(settings, `(() => { const s = ${hide}; s.selectedIndex = 2;
    s.dispatchEvent(new Event('change')); })()`);
check('...and choosing Hide Both sets it',
      await waitFor(async () => await readPref('koan.compact_mode.hide') === 'both'));
await settingsCall(settings, 'setPref', 'koan.compact_mode.hide', 'sidebar');
check('The page has the flash popup setting, off by default',
      await evalIn(settings, `document.querySelector(
          '[data-pref="koan.compact_mode.toolbar_flash_popup"] input')?.checked`) === false);
const right = `document.querySelector('[data-pref="koan.tabs.on_right"] input')`;
await evalIn(settings, `${right}.click()`);
check('...and Tabs on the right, which sets it',
      await waitFor(async () => await readPref('koan.tabs.on_right') === true));
await evalIn(settings, `${right}.click()`);
await waitFor(async () => await readPref('koan.tabs.on_right') === false);

// Incognito windows use the same settings as the page and the other windows.
const before = new Set((await pages()).map(p => p.id));
await front(other);
await key(other, VK.P, CTRL | SHIFT, 'p');
const incognito = await waitFor(async () => (await pages()).find(p => !before.has(p.id)));
check('Ctrl+Shift+P opens a private window', incognito !== undefined);
await key(incognito, VK.S, CTRL, 's');
check('Ctrl+S in the private window turns compact mode on in both windows',
      await waitFor(async () => await hotZones() === 4), `${await hotZones()} hot zones`);
check('...and the page shows it',
      await waitFor(() => evalIn(settings, `${compact}.checked`)));
await evalIn(settings, `${compact}.click()`);
check('Switching it off in the page turns it off in the private window too',
      await waitFor(async () => await hotZones() === 2), `${await hotZones()} hot zones`);
await browserCall('Target.closeTarget', { targetId: incognito.id });
await waitFor(async () => await hotZones() === 1);

// Tab Management.
await evalIn(settings, `document.querySelector('#category-tabs').click()`);
const rowLabels = await evalIn(settings,
    `[...document.querySelectorAll('#pane .row')].map(r => r.textContent)`);
check('Tab Management has the tint, Zen\'s tab settings and the freeze switch',
      rowLabels?.length === 6 &&
      rowLabels.some(l => l.startsWith('When closing a tab, switch to the most recently used')) &&
      rowLabels.some(l => l.startsWith('Freeze background tabs')),
      JSON.stringify(rowLabels));

// The freeze switch is Chromium's own (local-state) pref: on by default,
// and the page can turn it off and back on.
const freeze = `document.querySelector('[data-pref="performance_tuning.tab_freezing.enabled"] input')`;
const freezeOn = await evalIn(settings, `${freeze}?.checked`);
const turnedOff = await settingsCall(settings, 'setPref',
                                     'performance_tuning.tab_freezing.enabled', false);
const showsOff = await waitFor(() => evalIn(settings, `${freeze}?.checked === false || null`));
await settingsCall(settings, 'setPref', 'performance_tuning.tab_freezing.enabled', true);
check('The freeze switch is on and the page can change it (a local-state pref)',
      freezeOn === true && turnedOff === true && showsOff === true,
      `on ${freezeOn}, set ${turnedOff}, shows off ${showsOff}`);

// Keyboard shortcuts.
await evalIn(settings, `document.querySelector('#category-shortcuts').click()`);
const value = id => evalIn(settings, `document.querySelector('#shortcut-${id}').value`);
const note = id => evalIn(settings, `document.querySelector('#shortcut-${id}')
    .parentElement.querySelector('.shortcut-note')?.textContent ?? null`);
const groups = await evalIn(settings,
    `[...document.querySelectorAll('.shortcut-group h2')].map(e => e.textContent)`);
check('Shortcuts are grouped as in Zen',
      groups?.[0] === 'Compact Mode' && groups?.[1] === 'Workspaces' &&
      groups?.[2] === 'Split View', JSON.stringify(groups));
check('...showing Zen\'s keys', await value('compact-mode-toggle') === 'Ctrl+S' &&
      await value('copy-url-markdown') === 'Ctrl+Alt+⇧+C' &&
      await value('open-settings') === 'Ctrl+,',
      `${await value('compact-mode-toggle')}, ${await value('copy-url-markdown')}`);
check('A custom key wins over a default with the same key (seeded)',
      await value('downloads') === 'Ctrl+Alt+R' && await value('reader-mode') === 'Not set',
      `${await value('downloads')}, ${await value('reader-mode')}`);
await shot(settings, 'settings-shortcuts');

const focus = id => evalIn(settings, `document.querySelector('#shortcut-${id}').focus()`);
await focus('compact-mode-toggle');
await key(settings, VK.J, CTRL | ALT, 'j');
check('Typing a key shows it', await value('compact-mode-toggle') === 'Ctrl+Alt+J');
await key(settings, VK.ESC, 0, 'Escape');
check('...Escape saves it',
      await waitFor(async () => await value('compact-mode-toggle') === 'Ctrl+Alt+J'));

await front(other);
await key(other, VK.J, CTRL | ALT, 'j');
check('The new key works at once (Ctrl+Alt+J: compact mode on)',
      await waitFor(async () => await hotZones() === 2));
await key(other, VK.J, CTRL | ALT, 'j');
check('...and off again', await waitFor(async () => await hotZones() === 1));

await front(settings);
await focus('compact-mode-show-sidebar');
await key(settings, VK.J, CTRL | ALT, 'j');
await sleep(800);
check('While editing, a key that is a shortcut reaches the editor',
      await hotZones() === 1 && await value('compact-mode-show-sidebar') === 'Ctrl+Alt+J');
await key(settings, VK.ESC, 0, 'Escape');
check('A key taken by another shortcut is refused with Zen\'s conflict message',
      await note('compact-mode-show-sidebar') ===
          'Conflicts with Compact Mode -> Toggle Compact Mode',
      JSON.stringify(await note('compact-mode-show-sidebar')));
check('...marked invalid, and the field shows the key that is bound',
      await evalIn(settings, `document.querySelector('#shortcut-compact-mode-show-sidebar')
          .classList.contains('invalid')`) &&
      await value('compact-mode-show-sidebar') === 'Ctrl+Alt+S');
// Ctrl+Alt+J (modifiers: Ctrl 4 | Alt 8) is Toggle Compact Mode's now.
check('The browser refuses a conflicting key itself',
      await settingsCall(settings, 'setShortcut', 'mute', { key: VK.J, modifiers: 12 }) ===
          false);
check('...and a key without Ctrl, Alt or Win',
      await settingsCall(settings, 'setShortcut', 'mute', { key: VK.Q, modifiers: 0 }) ===
          false);

// Tab switching keys can't be shortcuts (Koan's Ctrl+Tab settings use them).
check('...and a tab switching key (Ctrl+Tab)',
      await settingsCall(settings, 'setShortcut', 'mute', { key: VK.TAB, modifiers: 4 }) ===
          false);
await focus('mute');
await key(settings, VK.TAB, CTRL, 'Tab');
await key(settings, VK.ESC, 0, 'Escape');
check('The editor refuses Ctrl+Tab, which switches tabs',
      await note('mute') === 'Ctrl+Tab and Ctrl+Page Up/Down switch tabs' &&
      await value('mute') === 'Ctrl+M', JSON.stringify(await note('mute')));

// Tab leaves a field unchanged; leaving it otherwise warns, like Zen.
await focus('mute');
await key(settings, VK.Q, CTRL | ALT, 'q');
await key(settings, VK.TAB, 0, 'Tab');
check('Tab leaves a shortcut unchanged',
      await value('mute') === 'Ctrl+M' && await note('mute') === null);
await focus('mute');
await key(settings, VK.Q, CTRL | ALT, 'q');
await evalIn(settings, 'document.activeElement.blur()');
check('Leaving without Escape shows Zen\'s "Unsaved shortcut!" note',
      (await note('mute'))?.startsWith('Unsaved shortcut!') && await value('mute') === 'Ctrl+M');

// Backspace clears a shortcut: its key stops working.
await focus('toggle-single-toolbar');
await key(settings, VK.BACKSPACE, 0, 'Backspace');
await evalIn(settings, 'document.activeElement.blur()');
check('Backspace clears a shortcut',
      await waitFor(async () => await value('toggle-single-toolbar') === 'Not set'));
await front(other);
await key(other, VK.B, CTRL | ALT, 'b');
await sleep(800);
check('...and its key does nothing any more (Ctrl+Alt+B)', await navViews() === 1);

// Reset to Default (confirmed) restores the defaults.
await evalIn(settings, 'window.confirm = () => true');
await evalIn(settings, `document.querySelector('#reset-shortcuts').click()`);
check('Reset to Default restores the defaults',
      await waitFor(async () => await value('compact-mode-toggle') === 'Ctrl+S' &&
                                await value('toggle-single-toolbar') === 'Ctrl+Alt+B' &&
                                await value('reader-mode') === 'Ctrl+Alt+R'));
await key(other, VK.J, CTRL | ALT, 'j');
await sleep(800);
check('...the custom key stops working (Ctrl+Alt+J)', await hotZones() === 1);
await key(other, VK.B, CTRL | ALT, 'b');
check('...and the cleared one works again (Ctrl+Alt+B)',
      await waitFor(async () => await navViews() === 0));
await key(other, VK.B, CTRL | ALT, 'b');
await waitFor(async () => await navViews() === 1);

// Search.
await evalIn(settings, `(() => { const s = document.querySelector('#search');
    s.value = 'compact'; s.dispatchEvent(new Event('input')); })()`);
const found = await evalIn(settings, `document.querySelector('#pane').textContent`);
check('Find in Settings searches settings and shortcuts',
      found?.includes('Enable Koan\'s compact mode') && found?.includes('Toggle Compact Mode'));

if (SHOTS) {
  await evalIn(settings, `document.querySelector('#category-looks').click()`);
  await shot(settings, 'settings-look-and-feel-dark', 'dark');
}

// Ctrl+, with the page open (on any pane) goes back to it.
await evalIn(settings, `document.querySelector('#category-tabs').click()`);
await front(other);
await key(other, VK.COMMA, CTRL, ',');
await sleep(800);
check('Ctrl+, with the page open on another pane switches to it, no second tab',
      (await settingsPages()).length === 1 && (await visiblePage())?.id === settings.id,
      `${(await settingsPages()).length} settings tab(s)`);

// Workspace tint, live: off restores the user's colour, on tints again.
const tint = `document.querySelector('[data-pref="koan.workspaces.tint"] input')`;
await waitFor(() => evalIn(settings, `!!document.querySelector('#category-tabs')`));
await evalIn(settings, `document.querySelector('#category-tabs').click()`);
await front(other);
// A new Space has Zen's default theme; Edit Theme... gives it a colour.
await createSpace(other, 'Tinted');
const header = (await viewTree()).find(v => v.cls === 'KoanSpaceHeaderView' && v.shown);
for (const type of ['mousePressed', 'mouseReleased']) {
  await viewMouse(header, type, 20, 20, 'right');
}
await sleep(1000);
let [menu] = openMenus(await viewTree());
await menuMouse(menu, menu.items[2]);  // Edit Theme...
[menu] = openMenus(await viewTree());
await menuMouse(menu, menu.items[1]);  // Blue
const seed = await waitFor(() => readPref('koan.workspaces.tint_applied'));
check('A Space with a colour tints the browser',
      seed && await readPref('browser.theme.user_color2') === seed, `seed ${seed}`);
await evalIn(settings, `${tint}.click()`);
check('Switching the tint off restores the browser\'s colour at once',
      await waitFor(async () => !await readPref('koan.workspaces.tint_applied')) &&
      await readPref('browser.theme.user_color2') !== seed);
await evalIn(settings, `${tint}.click()`);
check('...and switching it on tints it again',
      await waitFor(async () => await readPref('browser.theme.user_color2') === seed));

finish();
