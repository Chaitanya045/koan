// End-to-end test of Zen's download animation against a real build: a new
// download sends a round download icon flying along an arc, and (with the
// downloads button hidden, as in the default single-toolbar layout) the
// downloads tile slides in at the bottom of the sidebar's side; both go
// away.
//
//   scripts/e2e/run.sh download_animation
import { mkdirSync, mkdtempSync, rmSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { browserCall, check, click, finish, pages, saveShot, shownViews, sleep,
         views } from './lib/koan.mjs';

const [page] = await pages();
// Downloads go to a scratch folder beside the test profiles.
const profiles = join(dirname(fileURLToPath(import.meta.url)), '../../.test-profiles');
mkdirSync(profiles, { recursive: true });
const dir = mkdtempSync(join(profiles, 'downloads-'));
await browserCall('Browser.setDownloadBehavior', { behavior: 'allow', downloadPath: dir });

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
rmSync(dir, { recursive: true, force: true });
finish();
