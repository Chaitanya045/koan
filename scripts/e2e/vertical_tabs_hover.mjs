// End-to-end test of compact mode's reveal on a profile that turned
// "Expand tabs on hover" off, and that upstream's hover hints stay away
// (audit tests 3 and 23, #154). Compact mode is on from the seeded prefs
// (prefs/vertical_tabs_hover.json).
//
// - the pref is back at true, so the edge hot zone still reveals the sidebar
//   and the revealed sidebar still holds (upstream's expand-on-hover keeps it
//   out while the pointer is over it; the pointer-over-the-sidebar half needs a
//   real pointer and is in .agent/notes/vertical-tabs-tests.md, test 3);
// - compact mode hides the sidebar by collapsing the strip, which upstream
//   answers 3 seconds later with the "expand tabs on hover" hint: no hint
//   shows, 4 seconds and 11 seconds after (the 10 minute timer of the hint
//   while collapsed is covered in the notes);
// - the window is never anything but Koan's sidebar.
//
//   scripts/e2e/run.sh vertical_tabs_hover
import { check, finish, mouseOn, readPref, sleep, views } from './lib/koan.mjs';
import { alive, checkKoanSidebar, leakLogged, promoBubbleShown, region, sampleWidths }
    from './lib/vertical_tabs.mjs';

let vs = await views();
checkKoanSidebar('Compact mode on an old profile', { hidden: true }, vs);
check('vertical_tabs.expand_on_hover is back to true',
      await readPref('vertical_tabs.expand_on_hover') === true);

const bv = vs.find(v => v.cls === 'BrowserView' && v.shown);
const zone = vs.find(v => v.cls === 'KoanHotZone' && v.shown && v.h > bv.h / 2);
check('The sidebar has its hot zone', !!zone);

await mouseOn(zone, 'mouseEntered');
const widths = await sampleWidths(1200);
check('The hot zone reveals the sidebar', Math.max(...widths) >= 200, widths.join(', '));
await mouseOn(zone, 'mouseExited');
await sleep(1500);
check('...and it slides away again', region(await views())?.w === 0);

await sleep(4000);
check('No hint about expanding tabs on hover, 4 s after compact mode hid the sidebar',
      !promoBubbleShown(await views()));
await sleep(7000);
check('...nor 11 s after', !promoBubbleShown(await views()));
checkKoanSidebar('After the wait', { hidden: true }, await views());

check('Koan still answers', await alive());
check('The leak detector is silent', !leakLogged());
finish();
