// End-to-end test that without compact mode (the KoanCompactMode feature off,
// scripts/e2e/vertical_tabs_no_compact.flags) the sidebar's entry points still
// can't collapse it to Chromium's icon rail (audit tests 13 and 20; #154):
// with nothing to hide the sidebar for, the sidebar button and Shift+Cmd+L do
// nothing, and a drag below the snap width snaps back.
//
//   scripts/e2e/run.sh vertical_tabs_no_compact
import { CTRL, SHIFT, check, finish, mouseOn, sleep, views } from './lib/koan.mjs';
import { VK, alive, checkKoanSidebar, leakLogged, press, railShown, region }
    from './lib/vertical_tabs.mjs';

const toggleOf = vs => vs.find(v => v.shown && v.path.at(-1) === 'VerticalTabStripTopContainer' &&
                                    v.cls !== 'KoanSidebarNavButtons' && v.w > 0);

let vs = await views();
// (Without KoanCompactMode there is no single toolbar either: Koan's overlay
// layouts are one feature, so Chromium's toolbar is drawn above the page.)
const WITHOUT = { toolbar: true };
checkKoanSidebar('Without compact mode', WITHOUT, vs);
const width = region(vs).w;

await press(VK.L, CTRL | SHIFT, 'L', 1500);
vs = await views();
checkKoanSidebar('Shift+Cmd+L', WITHOUT, vs);
check('...the sidebar is as wide as before', region(vs).w === width, `${region(vs).w} vs ${width}`);

const button = toggleOf(vs);
check('The sidebar button is there', !!button);
if (button) {
  await mouseOn(button, 'mousePressed');
  await mouseOn(button, 'mouseReleased');
  await sleep(1500);
}
vs = await views();
checkKoanSidebar('The sidebar button', WITHOUT, vs);
check('...the sidebar is as wide as before', region(vs).w === width, `${region(vs).w} vs ${width}`);

// A drag below the snap width.
const resize = vs.find(v => v.cls === 'ResizeArea' && v.shown &&
                            v.path.includes('VerticalTabStripRegionView'));
await mouseOn(resize, 'mousePressed', 2, 300);
for (const dx of [-60, -120, -200, -260]) {
  await mouseOn(resize, 'mouseDragged', 2 + dx, 300);
  await sleep(150);
  check(`Mid-drag (${dx}) no rail`, !railShown(await views()));
}
await mouseOn(resize, 'mouseReleased', 2 - 260, 300);
await sleep(1500);
vs = await views();
checkKoanSidebar('After the drag', WITHOUT, vs);
check('...it snapped back to its width', region(vs).w === width, `${region(vs).w} vs ${width}`);

check('Koan still answers', await alive());
check('The leak detector is silent', !leakLogged());
finish();
