// End-to-end test of native fullscreen with single toolbar (Zen's default
// layout), and of the bookmarks bar being off.
//
// Everywhere: no bookmarks bar, also on the new tab page and in a profile
// whose bookmarks were imported (the seeded prefs
// scripts/e2e/prefs/fullscreen_single.json are what an import leaves:
// bookmark_bar.show_on_all_tabs and an "always show" visibility state).
//
// On macOS (a build whose user agent says Macintosh; elsewhere fullscreen has
// no card or top container to move): in native fullscreen Koan's layout
// stays and nothing of Chromium's top chrome shows.
//   - the top container is still in Koan's overlay, in the window's own
//     widget, not in Chromium's overlay widget (which reveals it from the top
//     edge, with a title bar strip);
//   - the sidebar's top row has the navigation buttons after the toggle;
//   - no bookmarks bar, no toolbar above the page;
//   - Ctrl+Alt+B (multiple toolbars) hands the top container to Chromium's
//     overlay widget, and the next one takes it back;
//   - the URL bar open when fullscreen ends is closed;
//   - a second window made fullscreen and closed there: the browser lives;
//   - leaving fullscreen is as before.
// The top-edge hover itself is AppKit's and can't be simulated here: the Mac
// bench checks it with a real pointer (see docs/REBASE.md, "Toolbar hidden in
// macOS fullscreen").
//
//   scripts/e2e/run.sh fullscreen_single
import { ALT, CTRL, browserCall, check, finish, key, pages, readPref, sleep,
         views, visiblePage, waitFor } from './lib/koan.mjs';
import { alive, goFullscreen, isMac, primary } from './lib/fullscreen.mjs';

const find = (vs, cls) => vs.find(v => v.cls === cls);
const findShown = (vs, cls) => vs.find(v => v.cls === cls && v.shown);
const VK = { B: 66, L: 76 };

const barShown = vs => vs.some(v => v.cls === 'BookmarkBarView' && v.shown && v.h > 0);

// The user's setting is what decides, not Chromium's (an import turned that on).
check('The bookmarks bar is off by default', (await readPref('koan.bookmarks_bar.show')) === false);
check('...though Chromium\'s own setting says on (an import)',
      (await readPref('bookmark_bar.show_on_all_tabs')) === true);
check('Imported bookmarks: no bookmarks bar', !barShown(await views()));
const ntp = (await browserCall('Target.createTarget', { url: 'chrome://newtab' })).result.targetId;
await sleep(2000);
check('...also on the new tab page', !barShown(await views()));
await browserCall('Target.closeTarget', { targetId: ntp });
await sleep(500);

if (!(await isMac())) {
  console.log('(not macOS: skipping the fullscreen checks)');
  finish();
}

// The top container is in the window's own widget (Koan's overlay) rather
// than in another one, whose root view isn't the browser's.
const topContainerIn = vs => {
  const top = find(vs, 'TopContainerView');
  return top && top.path[0] === 'BrowserRootView' &&
         top.path.includes('TopContainerOverlayView') ? 'Koan' : 'Chromium';
};

async function checkKoanLayout(when) {
  const vs = await views();
  const strip = findShown(vs, 'VerticalTabStripRegionView');
  const nav = findShown(vs, 'KoanSidebarNavButtons');
  check(`${when}: the top container is in Koan's overlay`,
        topContainerIn(vs) === 'Koan', topContainerIn(vs));
  check(`${when}: the sidebar's top row has the navigation buttons`,
        strip && nav && nav.path.includes('VerticalTabStripTopContainer') &&
            nav.x > strip.x + 20 && nav.x + nav.w <= strip.x + strip.w,
        nav && `${nav.x}+${nav.w} in ${strip?.x}+${strip?.w}`);
  check(`${when}: no bookmarks bar`, !barShown(vs));
  return vs;
}

await checkKoanLayout('Windowed');

await goFullscreen(true);
const vs = await checkKoanLayout('Fullscreen');
const contents = findShown(vs, 'MultiContentsView');
check('Fullscreen: the page starts at the top gutter, no strip above it',
      contents && contents.y <= 8, contents && `${contents.y}`);
check('Fullscreen: no other widget has a toolbar',
      vs.filter(v => v.cls === 'ToolbarView' && v.path[0] !== 'BrowserRootView').length === 0);

// Multiple toolbars: Chromium hosts the top container in its overlay widget
// (revealed at the top edge); single toolbar takes it back.
await key(await visiblePage(), VK.B, CTRL | ALT, 'b');
await sleep(1500);
check('Fullscreen, multiple toolbars: Chromium\'s overlay widget has the top container',
      topContainerIn(await views()) === 'Chromium', topContainerIn(await views()));
await key(await visiblePage(), VK.B, CTRL | ALT, 'b');
await sleep(1500);
await checkKoanLayout('Fullscreen, single toolbar again');

// The URL bar open while fullscreen ends: closed, the top container hidden.
await key(await visiblePage(), VK.L, await primary(), 'l');
await sleep(800);
await goFullscreen(false);
const after = await checkKoanLayout('Windowed again, after the URL bar was open');
check('...with the URL bar closed',
      !after.some(v => (v.cls === 'KoanUrlBarBackdrop' || v.cls === 'KoanUrlBarCardDecoration') && v.shown));

// The same toggle with the URL bar open when fullscreen starts.
await key(await visiblePage(), VK.L, await primary(), 'l');
await sleep(800);
await goFullscreen(true);
const entered = await checkKoanLayout('Fullscreen, entered with the URL bar open');
check('...with the URL bar closed',
      !entered.some(v => (v.cls === 'KoanUrlBarBackdrop' || v.cls === 'KoanUrlBarCardDecoration') && v.shown));
await goFullscreen(false);

// A second window, fullscreen, closed there.
const second = (await browserCall('Target.createTarget',
    { url: 'about:blank', newWindow: true })).result.targetId;
await sleep(1500);
await goFullscreen(true, second);
const both = await views();
check('A second window in fullscreen keeps Koan\'s layout',
      both.filter(v => v.cls === 'KoanSidebarNavButtons' && v.shown).length >= 1);
await browserCall('Target.closeTarget', { targetId: second });
await sleep(2500);
check('Closing a window in fullscreen: the browser lives', await alive());
await waitFor(async () => (await pages()).length >= 1, 3000);
await checkKoanLayout('The first window after that');

finish();
