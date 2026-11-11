// End-to-end test that an old profile can't make Chromium's vertical tabs
// state show up in Koan (audit tests 1, 2, 6, 12, 13, 14; #154). The seeded
// profile (prefs/vertical_tabs_old_profile.json) has everything a profile
// that was Chrome's, or Koan's before it owned the sidebar, can have:
// vertical tabs off, the strip collapsed to its 56 wide rail and saved so,
// "expand tabs on hover" off, the side panel on the right.
//
// - the window is Koan's vertical sidebar, expanded at Zen's width, with no
//   icon rail, no horizontal strip and no Chromium toolbar;
// - the three prefs read as their defaults afterwards, and writing them again
//   from chrome://settings (the Tab strip position dropdown, Expand tabs on
//   hover) changes nothing in the window, and they snap back;
// - the compact key (Ctrl+S here, Cmd+S on the Mac) toggles compact mode and never leaves a
//   rail, with the URL bar focused too;
// - Shift+Cmd+A (tab search) opens Koan's URL bar, not Chromium's bubble.
//
//   scripts/e2e/run.sh vertical_tabs_old_profile
import { CTRL, SHIFT, browserCall, check, finish, key, readPref, setChromePref, sleep, views,
         visiblePage } from './lib/koan.mjs';
import { VK, alive, checkKoanSidebar, leakLogged, press, railShown, region,
         tabSearchBubbleShown } from './lib/vertical_tabs.mjs';

let vs = await views();
checkKoanSidebar('Old profile', {}, vs);
{
  const bv = vs.find(v => v.cls === 'BrowserView' && v.shown);
  check('...on the left, whatever side the side panel is on (Koan\'s own pref is the switch)',
        region(vs)?.x === bv?.x, `${region(vs)?.x} vs ${bv?.x}`);
}

// Test 1 and 2: the prefs are back at their defaults.
check('vertical_tabs.enabled is back to true', await readPref('vertical_tabs.enabled') === true);
check('vertical_tabs.collapsed_state is back to false',
      await readPref('vertical_tabs.collapsed_state') === false);
check('vertical_tabs.expand_on_hover is back to true',
      await readPref('vertical_tabs.expand_on_hover') === true);

// Test 12: chrome://settings writes them again; nothing changes in the window.
for (const [pref, value] of [['vertical_tabs.enabled', false],
                             ['vertical_tabs.expand_on_hover', false],
                             ['vertical_tabs.collapsed_state', true]]) {
  await setChromePref(pref, value);
  await sleep(1200);
  vs = await views();
  checkKoanSidebar(`After chrome://settings sets ${pref}`, {}, vs);
  check(`...and ${pref} snaps back`,
        await readPref(pref) === !value, `${await readPref(pref)}`);
}

// Test 13: the compact key; Shift+Cmd+L is Koan's inspector (#90), not the sidebar's.
await press(VK.S, CTRL, 's', 1500);
vs = await views();
checkKoanSidebar('Ctrl+S', { hidden: true }, vs);
await press(VK.S, CTRL, 's', 1500);
vs = await views();
checkKoanSidebar('Ctrl+S again', {}, vs);
// ...with the URL bar focused (Ctrl+L).
await press(VK.L, CTRL, 'l', 800);
await press(VK.S, CTRL, 's', 1500);
vs = await views();
check('The compact key with the URL bar focused: no icon rail', !railShown(vs));
await press(VK.S, CTRL, 's', 1500);
await key(await visiblePage(), 27, 0, 'Escape');
await sleep(500);
checkKoanSidebar('After the toggles', {}, await views());

// Test 14: tab search is Koan's URL bar (the CDP command is IDC_TAB_SEARCH,
// the one Shift+Cmd+A runs).
await browserCall('Browser.executeBrowserCommand', { commandId: 'openTabSearch' });
await sleep(1000);
vs = await views();
check('Tab search shows no Chromium bubble', !tabSearchBubbleShown(vs));
check('...it opens Koan\'s URL bar', vs.some(v => v.cls === 'KoanUrlBarBackdrop' && v.shown));
// Zen's search tabs is the omnibox in its tabs scope: the "Tabs" chip, not the text "@tabs ".
check('...in the @tabs keyword mode (the Tabs chip)', vs.some(v => v.cls === 'SelectedKeywordView' && v.shown));
await key(await visiblePage(), 27, 0, 'Escape');
await sleep(500);

check('Koan still answers', await alive());
check('The leak detector is silent', !leakLogged());
finish();
