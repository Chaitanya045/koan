// End-to-end test of Zen's drag-to-hide: dragging the sidebar's edge until
// it collapses hides it in compact mode and shows the "Sidebar hidden!"
// toast once; leaving compact mode brings the sidebar back expanded.
//
//   scripts/e2e/run.sh sidebar_drag
import { CTRL, check, finish, key, mouseOn, sleep, views, visiblePage }
    from './lib/koan.mjs';

const find = (vs, cls) => vs.find(v => v.cls === cls);
const findShown = (vs, cls) => vs.find(v => v.cls === cls && v.shown);
// A key on the page showing; unhandled keys reach the browser's shortcuts.
const press = async (code, modifiers, name) => {
  await key(await visiblePage(), code, modifiers, name);
  await sleep(900);
};

const VK = { S: 83 };

// Drags the sidebar's edge 260 DIP to the left, calling `midway` (if
// given) once it has collapsed but before the button is released.
async function dragSidebarEdgeLeft(midway) {
  const vs = await views();
  const resize = vs.find(v => v.cls === 'ResizeArea' && v.shown &&
                              v.path.includes('VerticalTabStripRegionView'));
  await mouseOn(resize, 'mousePressed', 2, 300);
  for (const dx of [-60, -120, -200, -260]) {
    await mouseOn(resize, 'mouseDragged', 2 + dx, 300);
  }
  await sleep(800);
  await midway?.();
  await mouseOn(resize, 'mouseReleased', 2 - 260, 300);
  await sleep(1200);
}

let vs = await views();
const bv = findShown(vs, 'BrowserView');
check('The sidebar starts expanded', findShown(vs, 'VerticalTabStripRegionView')?.w >= 200);

await dragSidebarEdgeLeft(async () => {
  // Zen decides on release: until then the layout stays as it was.
  const mid = await views();
  check('Mid-drag, the layout is still single toolbar',
        find(mid, 'KoanSidebarNavButtons') &&
        find(mid, 'TopContainerView')?.path.includes('TopContainerOverlayView'));
});
vs = await views();
check('Dragging its edge to zero hides it in compact mode',
      find(vs, 'VerticalTabStripRegionView')?.w === 0 &&
      vs.some(v => v.cls === 'KoanHotZone' && v.shown && v.h > bv.h / 2));
const toast = find(vs, 'KoanToastView');
const texts = vs.filter(v => v.path.includes('KoanToastView') && v.cls === 'Label');
check('...with Zen\'s toast saying how to bring it back (the shortcut in a key cap)',
      toast && texts.length === 3, `${texts.length} labels`);
await sleep(6500);
check('...which goes away after 5 seconds', !find(await views(), 'KoanToastView'));

await press(VK.S, CTRL, 's');
check('Ctrl+S brings the sidebar back expanded',
      findShown(await views(), 'VerticalTabStripRegionView')?.w >= 200);

await dragSidebarEdgeLeft();
vs = await views();
check('Dragging again hides it, without the toast this time',
      find(vs, 'VerticalTabStripRegionView')?.w === 0 && !find(vs, 'KoanToastView'));
await press(VK.S, CTRL, 's');

finish();
