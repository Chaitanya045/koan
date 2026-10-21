// End-to-end test of "Tabs on the right" in a right-to-left UI
// (tabs_right_rtl.flags). The setting is logical, like the rest of the UI:
// in RTL the sidebar is normally on the right of the screen, and "on the
// right" (the trailing side) puts it on the left. Checked on the window's
// frame dump, as views report logical bounds. The page is red.
//
//   scripts/e2e/run.sh tabs_right_rtl
import { check, finish, sleep, windowFrame } from './lib/koan.mjs';

await sleep(3000);
const frame = windowFrame();
const isRed = ([r, g, b]) => r > 200 && g < 60 && b < 60;
const y = Math.floor(frame.height / 2);
const left = frame.pixel(120, y), right = frame.pixel(frame.width - 120, y);
check('With tabs "on the right" in RTL, the sidebar is on the left of the screen',
      !isRed(left) && isRed(right), `left ${left}, right ${right}`);

finish();
