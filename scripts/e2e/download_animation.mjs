// End-to-end test of Zen's download animation against a real build: a new
// download sends a round download icon flying along an arc to the Library
// button at the bottom of the sidebar. In compact mode with the sidebar
// hidden (no Library button, and no downloads button in the default single
// toolbar) a tile with the Library icon slides in at the bottom of the
// sidebar's side instead. Both go away.
//
//   scripts/e2e/run.sh download_animation
import { rmSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';
import {
  CTRL, check, click, evalIn, finish, front, key, pages, saveShot, shownViews, sleep, views,
  ACCEL,
} from './lib/koan.mjs';

const [page] = await pages();
// Downloads land in the default folder: each gets a name of its own, to
// remove afterwards.
const names = [];
async function download() {
  const name = `koan-e2e-${process.pid}-${Date.now()}.txt`;
  names.push(name);
  await evalIn(page, `document.querySelector('#download').download = ${JSON.stringify(name)}`);
  await click(page, '#download');
  await sleep(100);
  return views();
}
const gone = async () => {
  await sleep(4000);
  const vs = await views();
  return !shownViews(vs, 'KoanDownloadArcView').length &&
         !shownViews(vs, 'KoanDownloadTileView').length;
};

// ---- The sidebar shows: the icon flies to the Library button.
let vs = await views();
check('The Library button shows at the bottom of the sidebar',
      shownViews(vs, 'KoanLibraryButton').length === 1);
vs = await download();
saveShot('download-library');
check('A download sends the icon flying', shownViews(vs, 'KoanDownloadArcView').length === 1);
check('...instead of Chrome\'s own download animation',
      !vs.some(v => /DownloadStartedAnimation/.test(v.cls)));
check('...to the Library button, so no tile',
      !shownViews(vs, 'KoanDownloadTileView').length);
check('The icon goes away afterwards', await gone());

// ---- Compact mode hides the sidebar: the tile instead.
await front(page);
await key(page, 83, ACCEL, 's');   // Ctrl+S: compact mode
await sleep(800);
vs = await download();
saveShot('download-start');
const tile = shownViews(vs, 'KoanDownloadTileView')[0];
check('With the sidebar hidden, the icon flies to a tile at the bottom left',
      shownViews(vs, 'KoanDownloadArcView').length === 1 && tile && tile.x < 100 &&
      tile.y > 600, tile && `${tile.x},${tile.y}`);
await sleep(500);
saveShot('download-arc');
check('Both are gone afterwards', await gone());

await sleep(500);
for (const name of names) {
  rmSync(join(homedir(), 'Downloads', name), { force: true });
}
finish();
