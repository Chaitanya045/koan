// End-to-end test of Zen's download animation against a real build: a new
// download sends a round download icon flying along an arc, and (with the
// downloads button hidden, as in the default single-toolbar layout) the
// downloads tile slides in at the bottom of the sidebar's side; both go
// away.
//
//   scripts/e2e/run.sh download_animation
import { rmSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';
import { check, click, evalIn, finish, pages, saveShot, shownViews, sleep,
         views } from './lib/koan.mjs';

const [page] = await pages();
// The download lands in the default folder: give it a name of its own, to
// remove afterwards.
const name = `koan-e2e-${process.pid}-${Date.now()}.txt`;
await evalIn(page, `document.querySelector('#download').download = ${JSON.stringify(name)}`);

await click(page, '#download');
await sleep(100);
saveShot('download-start');
let vs = await views();
const arc = shownViews(vs, 'KoanDownloadArcView')[0];
const tile = shownViews(vs, 'KoanDownloadTileView')[0];
check('A download sends the icon flying', !!arc);
check('...instead of Chrome\'s own download animation',
      !vs.some(v => /DownloadStartedAnimation/.test(v.cls)));
check('...to the downloads tile at the bottom left (no downloads button)',
      tile && tile.x < 100 && tile.y > 600, tile && `${tile.x},${tile.y}`);
await sleep(500);
saveShot('download-arc');
await sleep(3500);
vs = await views();
check('Both are gone afterwards',
      !shownViews(vs, 'KoanDownloadArcView').length &&
      !shownViews(vs, 'KoanDownloadTileView').length);
await sleep(500);
rmSync(join(homedir(), 'Downloads', name), { force: true });
finish();
