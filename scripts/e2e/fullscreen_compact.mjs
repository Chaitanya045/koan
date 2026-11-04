// End-to-end test of native fullscreen with compact mode (macOS): Zen's
// `zen.view.borderless-fullscreen`, the page edge to edge at (0, 0), Koan's
// layout still there (the top container in Koan's overlay, no bookmarks bar
// whatever the imported profile says) and the sidebar and toolbar still
// revealed by Koan itself: the hot zones exist, and the pointer at the
// screen's edge (which can't be simulated here; the Mac bench moves a real
// one) is watched by the compact controller. Then a window closed in
// fullscreen with compact mode on.
//
//   scripts/e2e/run.sh fullscreen_compact
import { browserCall, check, finish, sleep, views } from './lib/koan.mjs';
import { alive, goFullscreen, isMac } from './lib/fullscreen.mjs';

if (!(await isMac())) {
  console.log('(not macOS: no fullscreen checks)');
  finish();
}

const find = (vs, cls) => vs.find(v => v.cls === cls);
const findShown = (vs, cls) => vs.find(v => v.cls === cls && v.shown);

async function checkCompact(when, fullscreen) {
  const vs = await views();
  const root = findShown(vs, 'BrowserRootView');
  const web = vs.filter(v => v.cls === 'ContentsWebView' && v.shown)
      .sort((a, b) => b.w * b.h - a.w * a.h)[0];
  const top = find(vs, 'TopContainerView');
  check(`${when}: the sidebar is hidden, its header stays`,
        find(vs, 'VerticalTabStripRegionView')?.w === 0 && findShown(vs, 'KoanSidebarNavButtons'),
        `sidebar ${find(vs, 'VerticalTabStripRegionView')?.w}`);
  check(`${when}: the top container is in Koan's overlay`,
        top?.path[0] === 'BrowserRootView' && top.path.includes('TopContainerOverlayView'));
  check(`${when}: no bookmarks bar`,
        !vs.some(v => v.cls === 'BookmarkBarView' && v.shown && v.h > 0));
  const hot = vs.filter(v => v.cls === 'KoanHotZone');
  check(`${when}: the sidebar's hot zone exists`, hot.length >= 1, `${hot.length}`);
  if (fullscreen) {
    check('Fullscreen: the page is edge to edge, at (0, 0)',
          web && root && web.x === 0 && web.y === 0 && web.w === root.w,
          web && `${web.x},${web.y} ${web.w}x${web.h} in ${root?.w}x${root?.h}`);
  } else {
    check(`${when}: the page is a card with a gutter`, web && web.x > 0 && web.y > 0,
          web && `${web.x},${web.y}`);
  }
}

await checkCompact('Windowed', false);
await goFullscreen(true);
await checkCompact('Fullscreen', true);
await goFullscreen(false);
await checkCompact('Windowed again', false);

// A second window in compact mode, fullscreen, closed there.
const second = (await browserCall('Target.createTarget',
    { url: 'about:blank', newWindow: true })).result.targetId;
await sleep(1500);
await goFullscreen(true, second);
await browserCall('Target.closeTarget', { targetId: second });
await sleep(2500);
check('Closing a compact-mode window in fullscreen: the browser lives', await alive());
await checkCompact('The first window after that', false);

finish();
