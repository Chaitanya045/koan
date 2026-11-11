// End-to-end test that Zen's mono window controls hold still under the
// pointer on macOS: right after launch, the real pointer (ptr) goes onto the
// traffic lights, stays, wiggles 1 px, and leaves, a few times. The dots view
// (KoanMonoTrafficLightsView) logs each change of what it shows (its .flags
// turn on that VLOG); a steady hover is one change on the way in ("no dots":
// AppKit's buttons) and one on the way out ("dots"), and none while the
// pointer stays. The flicker this guards against was a loop: the view hid
// itself under the pointer, which ended its own hover (an exit), which showed
// it again (an enter), as fast as the run loop turned. Mac only, real input
// only. Run by scripts/mac/suite.sh.
import { check, finish, pages, sleep } from './lib/koan.mjs';
import { IS_MAC, alive, countInLog, fatalInLog, ptr, realInput, realInputWhyNot, skip,
         windowOf } from './lib/mac.mjs';

if (!IS_MAC) skip('macOS only (AppKit traffic lights)');
if (!realInput()) skip(`needs the real pointer: ${realInputWhyNot()}`);

const page = (await pages()).find(p => p.type === 'page');
const w = page && await windowOf(page.id);
check('A browser window', !!w?.bounds);
if (!w?.bounds) finish();
const { left, top } = w.bounds;
// The yellow light's centre (window points; AppKit centres the lights in the
// sidebar's 44pt top row, the middle one about 42pt from the left), and a
// point well away from them over the page.
const ON = [left + 42, top + 22];
const AWAY = [left + 400, top + 300];

const changes = () => countInLog(/KoanMonoTrafficLights: (no )?dots$/m);
// Right after launch: no settling time before the first hover.
await ptr('move', ...AWAY, 100);
await sleep(300);
check('The dots show at launch', changes() >= 1, `${changes()} changes logged`);
for (let round = 1; round <= 5; round++) {
  const before = changes();
  await ptr('move', ...ON, 250);
  await sleep(1200);
  const inside = changes() - before;
  // Still, then a 1 px wiggle inside the lights.
  await ptr('move', ON[0] + 1, ON[1], 40);
  await ptr('move', ON[0], ON[1] + 1, 40);
  await sleep(800);
  const held = changes() - before;
  await ptr('move', ...AWAY, 250);
  await sleep(800);
  const total = changes() - before;
  check(`Round ${round}: one change on hover, none while held, one on leaving`,
        inside === 1 && held === 1 && total === 2,
        `in ${inside}, held ${held}, total ${total}`);
}
check('Koan alive', await alive());
check('Nothing fatal in the log', !fatalInLog(), fatalInLog());
finish();
