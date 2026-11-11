// End-to-end test of Zen's compact mode flash popup
// (zen.view.compact.toolbar-flash-popup; seeded on with compact mode by
// prefs/compact_flash.json): selecting a tab briefly pops up the hidden
// sidebar, which slides away again on its own.
//
//   scripts/e2e/run.sh compact_flash
import { boundsOf, browserCall, check, finish, sleep } from './lib/koan.mjs';
import { checkSlidesAway } from './lib/mac.mjs';

const width = async () => (await boundsOf('VerticalTabStripRegionView'))[0]?.w;

check('Compact mode hides the sidebar', (await width()) === 0);

// A new, selected tab. Not Ctrl+T: Zen's URL bar takes that over (the tab
// only opens once an address is entered), and no tab is selected meanwhile.
// Not about:blank either: a blank page is Zen's empty tab, which holds the
// sidebar out for as long as it is selected (`zen-has-empty-tab`), so the
// sidebar would never slide away.
await browserCall('Target.createTarget',
    { url: `data:text/html,${encodeURIComponent('<title>Flash</title>')}` });
await sleep(150);
const shown = await width();
check('Opening a tab pops the sidebar up', shown > 100, `width ${shown}`);
await sleep(1800);
const after = await width();
await checkSlidesAway(check, '...and it slides away again', after === 0, `width ${after}`);

finish();
