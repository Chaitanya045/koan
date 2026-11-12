// End-to-end test of native fullscreen with Zen's "Sidebar and Top Toolbar"
// layout (multiple toolbars, koan.single_toolbar off: the seeded prefs
// scripts/e2e/prefs/fullscreen_multiple.json), macOS only (elsewhere
// fullscreen has no card or top container to keep).
//
// Zen on macOS keeps its toolbars in native fullscreen (Firefox's mac default
// browser.fullscreen.autohide false) and its compact mode works there as in a
// window, so Koan's layout stays as in a window: the top container is never
// in Chromium's overlay widget (which only reveals it from the top edge).
//   - compact mode off: the toolbar is shown at the top of the window, next
//     to the sidebar, the page below it;
//   - compact mode hiding the sidebar: the toolbar stays, the sidebar is
//     hidden (its hot zone, and the screen's edge, reveal it), the page edge
//     to edge below the toolbar (zen.view.borderless-fullscreen);
//   - compact mode hiding the toolbar, or both: the top container waits in
//     Koan's overlay (Koan's compact controller reveals it at the top edge),
//     the page at the top of the screen;
//   - each applied live, in fullscreen;
//   - the page's own fullscreen (a video's) hides everything, and the toolbar
//     comes back when it ends;
//   - leaving fullscreen is as before.
// The top-edge and side-edge hovers need a real pointer: the Mac bench checks
// them (fullscreen_compact.mjs has the hot zones).
//
//   scripts/e2e/run.sh fullscreen_multiple
import {
  check, evalIn, finish, onPage, readPref, setKoanPref, sleep, viewTree, visiblePage,
  waitFor,
} from './lib/koan.mjs';
import { goFullscreen, isMac } from './lib/fullscreen.mjs';
import { startServer } from './lib/mac.mjs';

check('Multiple toolbars (the seeded profile)',
      (await readPref('koan.single_toolbar')) === false);

if (!(await isMac())) {
  console.log('(not macOS: no fullscreen checks)');
  finish();
}

const server = await startServer();
const page = await visiblePage();
await onPage(page, c => c.send('Page.navigate', { url: server.url('/p?t=Page') }));
await sleep(1500);

const findShown = (vs, cls) => vs.find(v => v.cls === cls && v.shown);

// The browser window's views: those of the widget that has the BrowserView.
async function windowViews() {
  const all = await viewTree();
  const widget = all.find(v => v.cls === 'BrowserView')?.widget;
  return { all, vs: all.filter(v => v.widget === widget), widget };
}

// Where the top container is: the BrowserView (the toolbar shown as in a
// window), Koan's overlay (hidden, revealed by Koan), or Chromium's overlay
// widget (another widget).
function topContainerIn({ all, widget }) {
  const top = all.find(v => v.cls === 'TopContainerView');
  if (!top) return 'none';
  if (top.widget !== widget) return `widget ${top.widget}`;
  if (top.path.includes('TopContainerOverlayView')) return 'Koan';
  return top.path.at(-1) === 'BrowserView' ? 'BrowserView' : top.path.join('>');
}

const largestPage = vs => vs.filter(v => v.cls === 'ContentsWebView' && v.shown)
    .sort((a, b) => b.w * b.h - a.w * a.h)[0];

// Compact mode off, or hiding the sidebar: the toolbar at the top of the
// window, the page below it; the sidebar beside it or hidden.
async function checkToolbarShown(when, { sidebarHidden }) {
  const tree = await windowViews();
  const { all, vs } = tree;
  const toolbar = findShown(vs, 'ToolbarView');
  const strip = vs.find(v => v.cls === 'VerticalTabStripRegionView');
  const web = largestPage(vs);
  check(`${when}: the top container is in the BrowserView`,
        topContainerIn(tree) === 'BrowserView', topContainerIn(tree));
  check(`${when}: no toolbar in another widget`,
        !all.some(v => v.cls === 'ToolbarView' && v.widget !== tree.widget));
  check(`${when}: the toolbar is shown at the top`,
        toolbar && toolbar.h > 0 && toolbar.ay === 0,
        toolbar && `y ${toolbar.ay}, ${toolbar.w}x${toolbar.h}`);
  if (sidebarHidden) {
    check(`${when}: the sidebar is hidden`, strip?.w === 0, `sidebar ${strip?.w}`);
    check(`${when}: its hot zone is there`, vs.some(v => v.cls === 'KoanHotZone'));
  } else {
    check(`${when}: the sidebar is shown`, strip?.shown && strip.w > 0, `sidebar ${strip?.w}`);
    check(`${when}: the toolbar is next to it`,
          toolbar && strip && toolbar.ax >= strip.ax + strip.w - 1,
          toolbar && strip && `toolbar x ${toolbar.ax}, sidebar ${strip.ax}+${strip.w}`);
  }
  check(`${when}: the page is below the toolbar`,
        web && toolbar && web.ay >= toolbar.ay + toolbar.h,
        web && toolbar && `page y ${web.ay}, toolbar ${toolbar.ay}+${toolbar.h}`);
}

// Compact mode hiding the toolbar (or both): Koan's overlay has it, the page
// at the top of the screen.
async function checkToolbarHidden(when, { sidebarHidden }) {
  const tree = await windowViews();
  const { vs } = tree;
  const strip = vs.find(v => v.cls === 'VerticalTabStripRegionView');
  const web = largestPage(vs);
  check(`${when}: the top container is in Koan's overlay`,
        topContainerIn(tree) === 'Koan', topContainerIn(tree));
  check(`${when}: the page is at the top of the screen`, web && web.ay === 0,
        web && `page y ${web.ay}`);
  check(`${when}: the sidebar is ${sidebarHidden ? 'hidden' : 'shown'}`,
        sidebarHidden ? strip?.w === 0 : strip?.shown && strip.w > 0, `sidebar ${strip?.w}`);
}

await checkToolbarShown('Windowed', { sidebarHidden: false });

await goFullscreen(true);
await checkToolbarShown('Fullscreen, compact mode off', { sidebarHidden: false });

// The page's own fullscreen: nothing of Koan's, the page fills the screen.
await evalIn(page, 'document.documentElement.requestFullscreen()', true);
await waitFor(() => evalIn(page, '!!document.fullscreenElement'), 5000);
await sleep(1500);
{
  const tree = await windowViews();
  const root = findShown(tree.vs, 'BrowserRootView');
  const web = largestPage(tree.vs);
  check('The page\'s fullscreen: no toolbar shown',
        !tree.all.some(v => v.cls === 'ToolbarView' && v.shown && v.h > 0),
        topContainerIn(tree));
  check('...the page fills the screen',
        web && root && web.ax === 0 && web.ay === 0 && web.w === root.w,
        web && `${web.ax},${web.ay} ${web.w}x${web.h} in ${root?.w}x${root?.h}`);
}
await evalIn(page, 'document.exitFullscreen()');
await waitFor(async () => !(await evalIn(page, '!!document.fullscreenElement')), 5000);
await sleep(1500);
await checkToolbarShown('Fullscreen, after the page\'s fullscreen', { sidebarHidden: false });

// Compact mode, each variant applied live.
await setKoanPref('koan.compact_mode.hide', 'sidebar');
await setKoanPref('koan.compact_mode.enabled', true);
await sleep(1500);
await checkToolbarShown('Fullscreen, compact mode hiding the sidebar', { sidebarHidden: true });
{
  const web = largestPage((await windowViews()).vs);
  check('...the page edge to edge (borderless fullscreen)', web && web.ax === 0,
        web && `page x ${web.ax}`);
}

await setKoanPref('koan.compact_mode.hide', 'toolbar');
await sleep(1500);
await checkToolbarHidden('Fullscreen, compact mode hiding the toolbar', { sidebarHidden: false });

await setKoanPref('koan.compact_mode.hide', 'both');
await sleep(1500);
await checkToolbarHidden('Fullscreen, compact mode hiding both', { sidebarHidden: true });

await setKoanPref('koan.compact_mode.enabled', false);
await sleep(1500);
await checkToolbarShown('Fullscreen, compact mode off again', { sidebarHidden: false });

// Single toolbar and back, live.
await setKoanPref('koan.single_toolbar', true);
await sleep(1500);
check('Fullscreen, single toolbar: the top container is in Koan\'s overlay',
      topContainerIn(await windowViews()) === 'Koan');
await setKoanPref('koan.single_toolbar', false);
await sleep(1500);
await checkToolbarShown('Fullscreen, multiple toolbars again', { sidebarHidden: false });

await goFullscreen(false);
await checkToolbarShown('Windowed again', { sidebarHidden: false });

// Entered with compact mode hiding the sidebar.
await setKoanPref('koan.compact_mode.hide', 'sidebar');
await setKoanPref('koan.compact_mode.enabled', true);
await sleep(1500);
await goFullscreen(true);
await checkToolbarShown('Fullscreen, entered with compact mode hiding the sidebar',
                        { sidebarHidden: true });
await goFullscreen(false);
await setKoanPref('koan.compact_mode.enabled', false);
await sleep(1500);
await checkToolbarShown('Windowed, compact mode off', { sidebarHidden: false });

server.close();
finish();
