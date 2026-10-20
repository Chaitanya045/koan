// End-to-end test of Zen's "Collapsed Sidebar" layout: collapsing the
// sidebar to icons leaves single toolbar for a toolbar above the page, and
// expanding it (the sidebar toggle, or Ctrl+Alt+B) brings single toolbar
// back.
//
//   scripts/e2e/run.sh collapsed_sidebar
import { ALT, CTRL, check, finish, key, mouseOn, sleep, views, visiblePage }
    from './lib/koan.mjs';

const find = (vs, cls) => vs.find(v => v.cls === cls);
const findShown = (vs, cls) => vs.find(v => v.cls === cls && v.shown);
// A key on the page showing; unhandled keys reach the browser's shortcuts.
const press = async (code, modifiers, name) => {
  await key(await visiblePage(), code, modifiers, name);
  await sleep(900);
};
const clickOn = async view => {
  await mouseOn(view, 'mousePressed');
  await mouseOn(view, 'mouseReleased');
  await sleep(1000);
};

const VK = { B: 66 };
const toggleOf = vs => vs.find(v => v.shown && v.path.at(-1) === 'VerticalTabStripTopContainer' &&
                                    v.cls !== 'KoanSidebarNavButtons' && v.w > 0);

let vs = await views();
check('Single toolbar by default', findShown(vs, 'KoanSidebarNavButtons') &&
      find(vs, 'TopContainerView')?.path.includes('TopContainerOverlayView'));

await clickOn(toggleOf(vs));
await sleep(800);
vs = await views();
let strip = findShown(vs, 'VerticalTabStripRegionView');
check('The sidebar toggle collapses the sidebar to icons', strip?.w === 56, `width ${strip?.w}`);
let top = findShown(vs, 'TopContainerView');
check('...with the toolbar above the page (Zen\'s collapsed layout)',
      top?.path.at(-1) === 'BrowserView' && top.x >= strip.x + strip.w,
      `toolbar ${top?.path.at(-1)} at ${top?.x}`);
check('...and no navigation in the sidebar',
      !find(vs, 'KoanSidebarNavButtons') && !find(vs, 'KoanSidebarNavView'));

await press(VK.B, CTRL | ALT, 'b');
await sleep(800);
vs = await views();
check('Ctrl+Alt+B expands it into single toolbar again',
      findShown(vs, 'VerticalTabStripRegionView')?.w > 56 &&
      findShown(vs, 'KoanSidebarNavButtons') &&
      find(vs, 'TopContainerView')?.path.includes('TopContainerOverlayView'));

await clickOn(toggleOf(vs));
await sleep(800);
await clickOn(toggleOf(await views()));
await sleep(800);
vs = await views();
check('Collapsing and expanding with the toggle comes back to single toolbar',
      findShown(vs, 'VerticalTabStripRegionView')?.w > 56 && findShown(vs, 'KoanSidebarNavButtons'));

finish();
