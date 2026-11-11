// End-to-end test of Koan's panes in chrome://settings against a real build:
// the page lists Koan's settings like Zen's preferences, changes apply live (layout,
// compact mode, Space themes), incognito windows and the page agree, the
// page follows changes made elsewhere, the handler refuses what the page
// can't set, and the shortcut editor rebinds, refuses conflicts, clears and
// resets keys, each checked by what the keys then do. On macOS too
// (scripts/mac/suite/e2e.list): Zen's "accel" keys are Cmd there, the page
// shows Zen's Mac notation ("⌘ S"), and every key it sends is one of Koan's
// window shortcuts, which CDP keys reach (never a main-menu-only key).
//
//   scripts/e2e/run.sh settings
//
// scripts/e2e/prefs/settings.json seeds a custom key (Open Downloads:
// Ctrl+Alt+R) that is another action's default (Toggle Reader Mode).
// With SHOTS=<dir>, also saves screenshots of the page (light and dark).
import {
  ALT, CTRL, SHIFT, browserCall, check, countViews, evalIn, finish, createSpace, front, key,
  menuMouse, onPage, openMenus, pages, readPref, settingsCall, sleep, viewEvent, viewMouse,
  viewTree, visiblePage, waitFor, windowTints, koanMenuItems, searchSettings, showPane,
  ACCEL, IS_MAC, NATIVE_MENUS, NATIVE_MENU_WHY,
} from './lib/koan.mjs';
import { skipped } from './lib/mac.mjs';
import fs from 'node:fs';

// What the page shows for a key: Zen's notation, the Mac's on macOS.
const shown = (linux, mac) => IS_MAC ? mac : linux;

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
  (await pages()).filter(p => p.url.startsWith('chrome://settings'));
const hotZones = () => countViews('KoanHotZone');
const navViews = () => countViews('KoanSidebarNavView');

// Ctrl+, (Cmd+, on macOS) opens the page (Zen's "Open Preferences").
let [other] = await pages();
await front(other);
await key(other, VK.COMMA, ACCEL, ',');
let [settings] = await waitFor(settingsPages);
check('Ctrl+, opens Koan\'s settings', settings !== undefined);
check('...and Koan\'s panes draw in it',
      await waitFor(() => evalIn(settings,
          `window.koanSettingsRoot?.getElementById('pane')?.childElementCount > 0`)));

const navItems = await evalIn(settings, koanMenuItems);
check('Chromium\'s settings menu lists Zen\'s panes first', JSON.stringify(navItems) ===
      JSON.stringify(['Look and Feel', 'Tab Management', 'Keyboard Shortcuts',
                      'Agent Control']),
      JSON.stringify(navItems));
check('...in chrome://settings, on Look and Feel',
      settings.url === 'chrome://settings/koan/looks', settings.url);
const headings = await evalIn(settings,
    `[...koanSettingsRoot.querySelectorAll('#pane h1, #pane h2')].map(e => e.textContent)`);
check('...Look and Feel shows the browser layout and compact mode groups',
      headings?.includes('Browser Layout') && headings?.includes('Show in compact view'),
      JSON.stringify(headings));
await shot(settings, 'settings-look-and-feel');
check(IS_MAC ? '...with the macOS-only Haptic feedback row' : '...and no macOS-only rows',
      await evalIn(settings,
          `!!koanSettingsRoot.querySelector('[data-pref="koan.haptic_feedback"]')`) === IS_MAC);

// The handler only sets prefs that have a row, with a value of their type.
check('setPref refuses a pref that has no row',
      await settingsCall(settings, 'koanSetPref', 'koan.workspaces.tint_applied', 5) === false);
check('...and a value of the wrong type',
      await settingsCall(settings, 'koanSetPref', 'koan.single_toolbar', 'x') === false);
check('...leaving the prefs unchanged',
      await readPref('koan.workspaces.tint_applied') === 0 &&
      await readPref('koan.single_toolbar') === true);

// Browser layout: the cards switch single / multiple toolbars live.
const card = label => `[...koanSettingsRoot.querySelectorAll('.layout-card')]
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
const compact = `koanSettingsRoot.querySelector('[data-pref="koan.compact_mode.enabled"] input')`;
await evalIn(settings, `${compact}.click()`);
check('The compact mode checkbox turns compact mode on',
      await waitFor(async () => await hotZones() === 2));
await evalIn(settings, `${compact}.click()`);
check('...and off', await waitFor(async () => await hotZones() === 1));

// What compact mode hides, its flash popup and tabs on the right.
const hide = `koanSettingsRoot.querySelector('[data-pref="koan.compact_mode.hide"] select')`;
check('The page has what compact mode hides, with Zen\'s choices',
      JSON.stringify(await evalIn(settings, `[...${hide}.options].map(o => o.text)`)) ===
      JSON.stringify(['Hide Tab bar', 'Hide Top bar', 'Hide Both']));
await evalIn(settings, `(() => { const s = ${hide}; s.selectedIndex = 2;
    s.dispatchEvent(new Event('change')); })()`);
check('...and choosing Hide Both sets it',
      await waitFor(async () => await readPref('koan.compact_mode.hide') === 'both'));
await settingsCall(settings, 'koanSetPref', 'koan.compact_mode.hide', 'sidebar');
check('The page has the flash popup setting, off by default',
      await evalIn(settings, `koanSettingsRoot.querySelector(
          '[data-pref="koan.compact_mode.toolbar_flash_popup"] input')?.checked`) === false);
const right = `koanSettingsRoot.querySelector('[data-pref="koan.tabs.on_right"] input')`;
await evalIn(settings, `${right}.click()`);
check('...and Tabs on the right, which sets it',
      await waitFor(async () => await readPref('koan.tabs.on_right') === true));
await evalIn(settings, `${right}.click()`);
await waitFor(async () => await readPref('koan.tabs.on_right') === false);

// Incognito windows use the same settings as the page and the other windows.
const before = new Set((await pages()).map(p => p.id));
await front(other);
await key(other, VK.P, ACCEL | SHIFT, 'p');
const incognito = await waitFor(async () => (await pages()).find(p => !before.has(p.id)));
check(`${shown('Ctrl+Shift+P', '⌘⇧P')} opens a private window`, incognito !== undefined);
await key(incognito, VK.S, ACCEL, 's');
check(`${shown('Ctrl+S', '⌘S')} in the private window turns compact mode on in both windows`,
      await waitFor(async () => await hotZones() === 4), `${await hotZones()} hot zones`);
check('...and the page shows it',
      await waitFor(() => evalIn(settings, `${compact}.checked`)));
await evalIn(settings, `${compact}.click()`);
check('Switching it off in the page turns it off in the private window too',
      await waitFor(async () => await hotZones() === 2), `${await hotZones()} hot zones`);
await browserCall('Target.closeTarget', { targetId: incognito.id });
await waitFor(async () => await hotZones() === 1);

// Tab Management.
await evalIn(settings, `${showPane('tabs')}`);
const rowLabels = await evalIn(settings,
    `[...koanSettingsRoot.querySelectorAll('#pane .row')].map(r => r.textContent)`);
// The rows named here, not a count: features add rows to the pane.
check('Tab Management has the tint, Zen\'s tab and pinned tab settings and the ' +
      'freeze switch',
      rowLabels?.some(l => l.startsWith('Show the active Space\'s theme')) &&
      rowLabels.some(l => l.startsWith('Close Tab Shortcut Behavior')) &&
      rowLabels.some(l => l.startsWith('When closing a tab, switch to the most recently used')) &&
      rowLabels.some(l => l.startsWith('Freeze background tabs')),
      JSON.stringify(rowLabels));

// The freeze switch is Chromium's own (local-state) pref: on by default,
// and the page can turn it off and back on.
const freeze = `koanSettingsRoot.querySelector('[data-pref="performance_tuning.tab_freezing.enabled"] input')`;
const freezeOn = await evalIn(settings, `${freeze}?.checked`);
const turnedOff = await settingsCall(settings, 'koanSetPref',
                                     'performance_tuning.tab_freezing.enabled', false);
const showsOff = await waitFor(() => evalIn(settings, `${freeze}?.checked === false || null`));
await settingsCall(settings, 'koanSetPref', 'performance_tuning.tab_freezing.enabled', true);
check('The freeze switch is on and the page can change it (a local-state pref)',
      freezeOn === true && turnedOff === true && showsOff === true,
      `on ${freezeOn}, set ${turnedOff}, shows off ${showsOff}`);

// Keyboard shortcuts.
await evalIn(settings, `${showPane('shortcuts')}`);
const value = id => evalIn(settings, `koanSettingsRoot.querySelector('#shortcut-${id}').value`);
const note = id => evalIn(settings, `koanSettingsRoot.querySelector('#shortcut-${id}')
    .parentElement.querySelector('.shortcut-note')?.textContent ?? null`);
const groups = await evalIn(settings,
    `[...koanSettingsRoot.querySelectorAll('.shortcut-group h2')].map(e => e.textContent)`);
check('Shortcuts are grouped as in Zen',
      groups?.[0] === 'Compact Mode' && groups?.[1] === 'Workspaces' &&
      groups?.[2] === 'Split View', JSON.stringify(groups));
check('...showing Zen\'s keys',
      await value('compact-mode-toggle') === shown('Ctrl+S', '⌘ S') &&
      await value('copy-url-markdown') === shown('Ctrl+Alt+⇧+C', '⌘ ⌥ ⇧ C') &&
      await value('open-settings') === shown('Ctrl+,', '⌘ ,'),
      `${await value('compact-mode-toggle')}, ${await value('copy-url-markdown')}`);
// The page switches to Zen's macOS notation (⌘ ⇧ C) on the browser's say-so.
const platform = await settingsCall(settings, 'koanGetSettings');
check(`The browser tells the page whether it is a Mac (${IS_MAC})`,
      platform?.shortcuts?.mac === IS_MAC, JSON.stringify(platform?.shortcuts?.mac));
// prefs/settings.json seeds Open Downloads with Ctrl+Alt+R, Toggle Reader
// Mode's default off macOS (Cmd+Option+R on macOS, where the two don't meet).
if (IS_MAC) {
  check('A seeded custom key shows (⌃ ⌥ R), next to the default it doesn\'t clash with',
        await value('downloads') === '⌃ ⌥ R' && await value('reader-mode') === '⌘ ⌥ R',
        `${await value('downloads')}, ${await value('reader-mode')}`);
} else {
  check('A custom key wins over a default with the same key (seeded)',
        await value('downloads') === 'Ctrl+Alt+R' && await value('reader-mode') === 'Not set',
        `${await value('downloads')}, ${await value('reader-mode')}`);
}
await shot(settings, 'settings-shortcuts');

const focus = id => evalIn(settings, `koanSettingsRoot.querySelector('#shortcut-${id}').focus()`);
await focus('compact-mode-toggle');
await key(settings, VK.J, CTRL | ALT, 'j');
const ctrlAltJ = shown('Ctrl+Alt+J', '⌃ ⌥ J');
check('Typing a key shows it', await value('compact-mode-toggle') === ctrlAltJ);
await key(settings, VK.ESC, 0, 'Escape');
check('...Escape saves it',
      await waitFor(async () => await value('compact-mode-toggle') === ctrlAltJ));

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
      await hotZones() === 1 && await value('compact-mode-show-sidebar') === ctrlAltJ);
await key(settings, VK.ESC, 0, 'Escape');
check('A key taken by another shortcut is refused with Zen\'s conflict message',
      await note('compact-mode-show-sidebar') ===
          'Conflicts with Compact Mode -> Toggle Compact Mode',
      JSON.stringify(await note('compact-mode-show-sidebar')));
check('...marked invalid, and the field shows the key that is bound',
      await evalIn(settings, `koanSettingsRoot.querySelector('#shortcut-compact-mode-show-sidebar')
          .classList.contains('invalid')`) &&
      await value('compact-mode-show-sidebar') === shown('Ctrl+Alt+S', '⌘ ⌥ S'));
// Ctrl+Alt+J (modifiers: Ctrl 4 | Alt 8) is Toggle Compact Mode's now.
check('The browser refuses a conflicting key itself',
      await settingsCall(settings, 'koanSetShortcut', 'mute', { key: VK.J, modifiers: 12 }) ===
          false);
check('...and a key without Ctrl, Alt or Win',
      await settingsCall(settings, 'koanSetShortcut', 'mute', { key: VK.Q, modifiers: 0 }) ===
          false);

// Tab switching keys can't be shortcuts (Koan's Ctrl+Tab settings use them).
check('...and a tab switching key (Ctrl+Tab)',
      await settingsCall(settings, 'koanSetShortcut', 'mute', { key: VK.TAB, modifiers: 4 }) ===
          false);
await focus('mute');
await key(settings, VK.TAB, CTRL, 'Tab');
await key(settings, VK.ESC, 0, 'Escape');
const ctrlM = shown('Ctrl+M', '⌃ M');
check('The editor refuses Ctrl+Tab, which switches tabs',
      await note('mute') === shown('Ctrl+Tab and Ctrl+Page Up/Down switch tabs',
                                   '⌃ Tab and ⌃ Page Up/Down switch tabs') &&
      await value('mute') === ctrlM, JSON.stringify(await note('mute')));

// Tab leaves a field unchanged; leaving it otherwise warns, like Zen.
await focus('mute');
await key(settings, VK.Q, CTRL | ALT, 'q');
await key(settings, VK.TAB, 0, 'Tab');
check('Tab leaves a shortcut unchanged',
      await value('mute') === ctrlM && await note('mute') === null);
// Leaving a field with keys typed saves them (Zen asks for Escape and warns
// "Unsaved shortcut!"; Koan saves them, since people click away: #90).
await focus('mute');
await key(settings, VK.Q, CTRL | ALT, 'q');
await evalIn(settings, 'koanSettingsRoot.activeElement?.blur()');
check('Leaving a field with keys typed saves them',
      await waitFor(async () => await value('mute') === shown('Ctrl+Alt+Q', '⌃ ⌥ Q')) &&
      await note('mute') === null,
      `${await value('mute')}, ${JSON.stringify(await note('mute'))}`);

// Backspace clears a shortcut: its key stops working.
await focus('toggle-single-toolbar');
await key(settings, VK.BACKSPACE, 0, 'Backspace');
await evalIn(settings, 'koanSettingsRoot.activeElement?.blur()');
check('Backspace clears a shortcut',
      await waitFor(async () => await value('toggle-single-toolbar') === 'Not set'));
await front(other);
await key(other, VK.B, CTRL | ALT, 'b');
await sleep(800);
check('...and its key does nothing any more (Ctrl+Alt+B)', await navViews() === 1);

// Reset to Default (confirmed) restores the defaults.
await evalIn(settings, 'window.confirm = () => true');
await evalIn(settings, `koanSettingsRoot.querySelector('#reset-shortcuts').click()`);
check('Reset to Default restores the defaults',
      await waitFor(async () =>
          await value('compact-mode-toggle') === shown('Ctrl+S', '⌘ S') &&
          await value('toggle-single-toolbar') === shown('Ctrl+Alt+B', '⌃ ⌥ B') &&
          await value('reader-mode') === shown('Ctrl+Alt+R', '⌘ ⌥ R')));
await key(other, VK.J, CTRL | ALT, 'j');
await sleep(800);
check('...the custom key stops working (Ctrl+Alt+J)', await hotZones() === 1);
await key(other, VK.B, CTRL | ALT, 'b');
check('...and the cleared one works again (Ctrl+Alt+B)',
      await waitFor(async () => await navViews() === 0));
await key(other, VK.B, CTRL | ALT, 'b');
await waitFor(async () => await navViews() === 1);

// Search: Chromium's one search box finds Koan's settings and shortcuts and
// Chromium's own.
await evalIn(settings, searchSettings('compact'));
const found = await waitFor(() => evalIn(settings,
    `koanSettingsRoot.querySelector('#pane').textContent`),
    t => t?.includes('Toggle Compact Mode'));
check('Search finds Koan\'s settings and shortcuts',
      found?.includes('Enable Koan\'s compact mode') && found?.includes('Toggle Compact Mode'));
// A section of the page in search results: shown, not hidden by the search.
const sectionFound = id => evalIn(settings, `(() => {
    const main = document.querySelector('settings-ui').shadowRoot
        .querySelector('#main').shadowRoot;
    const plugin = main.querySelector('[slot=view]#${id} > *');
    return !!plugin && !plugin.hasAttribute('hidden-by-search') &&
        plugin.checkVisibility();
  })()`);
const koanShown = () => evalIn(settings,
    `!koanSettingsRoot.host.hasAttribute('hidden-by-search')`);
// "cookies" is in Koan's agent settings and in Chromium's Privacy section.
await evalIn(settings, searchSettings('cookies'));
check('A search for both lists Koan\'s matches and Chromium\'s ("cookies")',
      await waitFor(async () => await koanShown() && await sectionFound('privacy') &&
          (await evalIn(settings, `koanSettingsRoot.getElementById('pane').textContent`))
              ?.includes('cookies')));
// "proxy" is only in Chromium's System section.
await evalIn(settings, searchSettings('proxy'));
check('...a Chromium-only search ("proxy") shows Chromium\'s and no Koan block',
      await waitFor(async () => !await koanShown() && await sectionFound('system')),
      `Koan shown ${await koanShown()}, System found ${await sectionFound('system')}`);
await evalIn(settings, searchSettings(''));

if (SHOTS) {
  await evalIn(settings, `${showPane('looks')}`);
  await shot(settings, 'settings-look-and-feel-dark', 'dark');
}

// Ctrl+, with the page open (on any pane) goes back to it.
await evalIn(settings, `${showPane('tabs')}`);
await front(other);
await key(other, VK.COMMA, ACCEL, ',');
await sleep(800);
check('Ctrl+, with the page open on another pane switches to it, no second tab',
      (await settingsPages()).length === 1 && (await visiblePage())?.id === settings.id,
      `${(await settingsPages()).length} settings tab(s)`);

// Space themes (the "tint" row): per window, so a Space's theme tints only
// its window and leaves the profile's colour alone; the switch applies live
// (scripts/e2e/theme.mjs checks the themes themselves).
// On macOS the Space header's menu is a native NSMenu that UI DevTools can't
// reach (NATIVE_MENUS); there the picker and themes are theme.mjs's.
if (NATIVE_MENUS) {
  skipped('Space themes and the tint row (Theme... in the Space header\'s menu)',
          NATIVE_MENU_WHY);
} else {
  const tint = `koanSettingsRoot.querySelector('[data-pref="koan.workspaces.tint"] input')`;
  await evalIn(settings, `${showPane('tabs')}`);
  const userColor = await readPref('browser.theme.user_color2');
  await front(other);
  // A new Space gets a theme with Theme... (the theme picker): Space
  // themes are per window, so the profile's colour stays.
  await createSpace(other, 'Themed');
  const header = (await viewTree()).find(v => v.cls === 'KoanSpaceHeaderView' && v.shown);
  for (const type of ['mousePressed', 'mouseReleased']) {
    await viewMouse(header, type, 20, 20, 'right');
  }
  await sleep(1000);
  const [menu] = openMenus(await viewTree());
  await menuMouse(menu, menu.items[2]);  // Theme...
  const swatch = await waitFor(async () => (await viewTree()).find(
      v => v.cls === 'KoanThemeSwatch' && v.shown));
  check('Theme... opens the theme picker', !!swatch);
  if (swatch) {
    for (const type of ['mousePressed', 'mouseReleased']) {
      await viewMouse(swatch, type, 13, 13);
    }
    await sleep(800);
    // Esc, in the picker, closes (and saves) it.
    await viewEvent(swatch, 'DOM.dispatchKeyEvent', { type: 'keyPressed', keyCode: 27,
      code: 0, flags: 0, key: 0, isChar: false });
    await sleep(800);
  }
  const seed = await waitFor(async () => (await windowTints()).find(t => t));
  check("A Space's theme tints its window and leaves the browser's colour alone",
        !!seed &&
        await readPref('browser.theme.user_color2') === userColor &&
        await readPref('koan.workspaces.tint_applied') === 0);
  await evalIn(settings, `${tint}.click()`);
  check('Switching Space themes off gives the window the user\'s colour at once',
        await waitFor(async () => await readPref('koan.workspaces.tint') === false &&
                                  (await windowTints()).every(t => !t)));
  await evalIn(settings, `${tint}.click()`);
  check('...and switching them on tints it again',
        await waitFor(async () => await readPref('koan.workspaces.tint') === true &&
                                  (await windowTints()).includes(seed)));
}

// One Settings page: a link to one of Chromium's sections opens it in the
// same page (Koan's panes hidden), Ctrl+, goes back to Koan's first pane in
// that tab, and the old chrome://koan-settings/<pane> opens the pane.
const sectionShown = id => evalIn(settings, `(() => {
    const main = document.querySelector('settings-ui').shadowRoot
        .querySelector('#main').shadowRoot;
    const view = main.querySelector('#${id}');
    return !!view && view.checkVisibility();
  })()`);
await browserCall('Target.activateTarget', { targetId: settings.id });
await onPage(settings, c => c.send('Page.navigate',
    { url: 'chrome://settings/appearance' }));
check('chrome://settings/appearance shows Chromium\'s Appearance in the page',
      await waitFor(() => sectionShown('appearance')) && !await sectionShown('koan'));
await front(other);
await key(other, VK.COMMA, ACCEL, ',');
check('...and Ctrl+, from there shows Koan\'s Look and Feel, in the same tab',
      await waitFor(() => sectionShown('koan')) &&
      (await settingsPages()).length === 1);
const legacy = (await browserCall('Target.createTarget',
    { url: 'chrome://koan-settings/shortcuts' })).result.targetId;
const legacyPage = await waitFor(async () => (await pages()).find(
    p => p.id === legacy && p.url.startsWith('chrome://settings')));
check('chrome://koan-settings/shortcuts opens chrome://settings/koan/shortcuts',
      legacyPage?.url === 'chrome://settings/koan/shortcuts', legacyPage?.url);
await browserCall('Target.closeTarget', { targetId: legacy });

// Koan's module runs inside Chromium's settings app: an error there stops the
// whole page (2026-11-12: a second copy of cr-iconset).
const log = process.env.KOAN_LOG ? fs.readFileSync(process.env.KOAN_LOG, 'utf8') : '';
const pageErrors = log.split('\n').filter(
    l => /Uncaught/.test(l) && /chrome:\/\/settings\//.test(l));
check('The browser\'s log has no uncaught error from chrome://settings',
      process.env.KOAN_LOG && pageErrors.length === 0, pageErrors.slice(0, 2).join(' | '));

finish();
