// End-to-end test of "Tabs on the right" in a right-to-left UI
// (tabs_right_rtl.flags). The setting is logical, like the rest of the UI:
// in RTL the sidebar is normally on the right of the screen, and "on the
// right" (the trailing side) puts it on the left. Checked on the window's
// pixels, as views report logical bounds: Linux's headless Ozone frame dump,
// on macOS a capture of the real window (lib/mac.mjs captureWindow(); the
// .flags' "# macOS: run" leaves the dump flag out there), both read in the
// window's points. The page
// is red. The Library opens beside the sidebar, so on the left too.
//
//   scripts/e2e/run.sh tabs_right_rtl
import { readdirSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import {
  ALT, IS_MAC, SHIFT, boundsOf, check, finish, key, sleep, visiblePage, windowFrame,
} from './lib/koan.mjs';
import { captureWindow, skip } from './lib/mac.mjs';

await sleep(3000);
const page = await visiblePage();
// The window's latest frame: on Linux the biggest dump, on macOS a capture
// now. (On Linux the dumps are cleared before the Library opens, so a new
// frame shows it; `frames` counts them.)
const dir = process.env.KOAN_DUMP_DIR;
// (A capture is in pixels, `scale` per point: read it in points.)
let captureError = '';
async function shot() {
  if (!IS_MAC) return windowFrame();
  const { frame: f, error } = await captureWindow(page);
  if (!f) {
    captureError = error;
    return null;
  }
  const px = v => Math.round(v * f.scale);
  return {
    width: Math.round(f.width / f.scale), height: Math.round(f.height / f.scale),
    pixel: (x, y) => f.pixel(Math.min(f.width - 1, px(x)), Math.min(f.height - 1, px(y))),
  };
}
const frame = await shot();
if (!frame) skip(`can't capture the window: ${captureError}`);
const isRed = ([r, g, b]) => r > 200 && g < 60 && b < 60;
const y = Math.floor(frame.height / 2);
const left = frame.pixel(120, y), right = frame.pixel(frame.width - 120, y);
check('With tabs "on the right" in RTL, the sidebar is on the left of the screen',
      !isRed(left) && isRed(right), `left ${left}, right ${right}`);

// Where the page starts, then the Library (Alt+Shift+L): from the window's
// left edge over the sidebar and the start of the page, the page still
// showing on the right.
let edge = 0;
while (edge < frame.width && !isRed(frame.pixel(edge, y))) edge++;
if (!IS_MAC) {
  for (const f of readdirSync(dir)) rmSync(join(dir, f), { force: true });
}
await key(page, 76, ALT | SHIFT, 'L');
await sleep(2500);
const library = (await boundsOf('KoanLibraryView'))[0];
const frames = IS_MAC ? 1 : readdirSync(dir).length;
const open = (frames ? await shot() : null) ?? frame;
// The Library covers the sidebar from the window's edge: between the
// sidebar's edge and its own.
const at = Math.floor((edge + (library?.w ?? 0)) / 2);
const inLibrary = open.pixel(at, y);
const pageRight = open.pixel(open.width - 120, y);
check('...and the Library opens beside it, on the left too',
      !isRed(inLibrary) && isRed(pageRight),
      `page from x ${edge}; at x ${at} ${inLibrary}, right ${pageRight}; ` +
      `library ${JSON.stringify(library)}, ${IS_MAC ? 'a new capture' : `${frames} new frame(s)`}`);

finish();
