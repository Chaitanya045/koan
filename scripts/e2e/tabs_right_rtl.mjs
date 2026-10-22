// End-to-end test of "Tabs on the right" in a right-to-left UI
// (tabs_right_rtl.flags). The setting is logical, like the rest of the UI:
// in RTL the sidebar is normally on the right of the screen, and "on the
// right" (the trailing side) puts it on the left. Checked on the window's
// frame dump, as views report logical bounds. The page is red. The Library
// opens beside the sidebar, so on the left too.
//
//   scripts/e2e/run.sh tabs_right_rtl
import { readdirSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import {
  ALT, SHIFT, boundsOf, check, finish, key, sleep, visiblePage, windowFrame,
} from './lib/koan.mjs';

await sleep(3000);
const frame = windowFrame();
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
const dir = process.env.KOAN_DUMP_DIR;
for (const f of readdirSync(dir)) rmSync(join(dir, f), { force: true });
await key(await visiblePage(), 76, ALT | SHIFT, 'L');
await sleep(2500);
const library = (await boundsOf('KoanLibraryView'))[0];
const frames = readdirSync(dir).length;
const open = frames ? windowFrame() : frame;
// The Library covers the sidebar from the window's edge: between the
// sidebar's edge and its own.
const at = Math.floor((edge + (library?.w ?? 0)) / 2);
const inLibrary = open.pixel(at, y);
const page = open.pixel(open.width - 120, y);
check('...and the Library opens beside it, on the left too',
      !isRed(inLibrary) && isRed(page),
      `page from x ${edge}; at x ${at} ${inLibrary}, right ${page}; ` +
      `library ${JSON.stringify(library)}, ${frames} new frame(s)`);

finish();
