// End-to-end test of native fullscreen with single toolbar (Zen's default
// layout) and the bookmarks bar default.
//
// Everywhere: no bookmarks bar, also on the new tab page, until the user
// asks for one (bookmark_bar.visibility_state is 2, "always hide").
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
//   - leaving fullscreen is as before.
// The top-edge hover itself is AppKit's and can't be simulated here: the Mac
// bench checks it with a real pointer (docs/REBASE.md, "Toolbar hidden in
// macOS fullscreen").
//
//   scripts/e2e/run.sh fullscreen_single
import { ALT, CDP_PORT, CTRL, browserCall, check, finish, key, readPref, sleep,
         views, visiblePage, waitFor } from './lib/koan.mjs';

const find = (vs, cls) => vs.find(v => v.cls === cls);
const findShown = (vs, cls) => vs.find(v => v.cls === cls && v.shown);
const VK = { B: 66 };

const visibility = await readPref('bookmark_bar.visibility_state');
const onAllTabs = await readPref('bookmark_bar.show_on_all_tabs');
check('The bookmarks bar is off by default, also on the new tab page',
      visibility === 2 && onAllTabs === false, `${visibility}, ${onAllTabs}`);

const version = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
if (!/Macintosh/.test(version['User-Agent'])) {
  console.log('(not macOS: skipping the fullscreen checks)');
  finish();
}

const setWindowState = async windowState => {
  const page = await visiblePage();
  const { windowId } = (await browserCall('Browser.getWindowForTarget',
                                          { targetId: page.id })).result;
  await browserCall('Browser.setWindowBounds', { windowId, bounds: { windowState } });
};
const windowState = async () => {
  const page = await visiblePage();
  const r = await browserCall('Browser.getWindowForTarget', { targetId: page.id });
  return r.result?.bounds?.windowState;
};

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
  const bookmarks = vs.filter(v => v.cls === 'BookmarkBarView' && v.shown && v.h > 0);
  check(`${when}: no bookmarks bar`, bookmarks.length === 0, `${bookmarks.length}`);
  return vs;
}

await checkKoanLayout('Windowed');

await setWindowState('fullscreen');
await waitFor(async () => (await windowState()) === 'fullscreen', 8000);
await sleep(2500);
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

await setWindowState('normal');
await waitFor(async () => (await windowState()) !== 'fullscreen', 8000);
await sleep(2500);
await checkKoanLayout('Windowed again');

finish();
