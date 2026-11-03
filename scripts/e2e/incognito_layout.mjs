// End-to-end test that an Incognito window has Zen's default layout like any
// other: single toolbar, the toolbar above the page slid away (in the
// overlay) and the navigation in the sidebar, not a stock Chromium toolbar on
// top.
//
//   scripts/e2e/run.sh incognito_layout
import { CTRL, SHIFT, check, finish, key, sleep, views, visiblePage }
    from './lib/koan.mjs';

const find = (vs, cls) => vs.find(v => v.cls === cls);
const findShown = (vs, cls) => vs.find(v => v.cls === cls && v.shown);
const windows = vs => {
  const roots = vs.flatMap((v, i) => (v.cls === 'BrowserRootView' ? [i] : []));
  return roots.map((start, n) => vs.slice(start, roots[n + 1]));
};
const VK = { N: 78 };

// Ctrl+Shift+N opens an Incognito window.
await key(await visiblePage(), VK.N, CTRL | SHIFT, 'N');
await sleep(2500);
const all = windows(await views());
check('Ctrl+Shift+N opens a second window', all.length === 2, `${all.length} windows`);
const incognito = all.at(-1);
const toolbar = find(incognito, 'TopContainerView');
check('The Incognito window has the toolbar away in the overlay (single toolbar)',
      toolbar?.path.includes('TopContainerOverlayView'), toolbar?.path.at(-1));
check('...with the navigation in the sidebar',
      !!findShown(incognito, 'KoanSidebarNavButtons'));
const strip = findShown(incognito, 'VerticalTabStripRegionView');
const page = findShown(incognito, 'ContentsContainerView');
check('...and the page reaching up to the gutter, no toolbar row above it',
      strip?.w >= 200 && page?.y <= 8, `sidebar ${strip?.w}, page y ${page?.y}`);

finish();
