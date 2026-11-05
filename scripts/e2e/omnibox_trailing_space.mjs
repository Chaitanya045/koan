// A typed URL that ends in "%20" (issue #166): the URL-what-you-typed match
// from the history URL provider had contents with a trailing space, and
// AutocompleteResult::AppendMatches() DCHECKed (SanitizeString(contents) !=
// contents), killing a DCHECK build. Types the URLs one key at a time, in the
// Cmd+T bar, and stops there; the browser must still be alive and the log free
// of "Check failed". Fixed by the sanitize patches listed in docs/REBASE.md
// ("Omnibox match contents sanitized").
//
//   scripts/e2e/run.sh omnibox_trailing_space
//
// The log check reads run.sh's KOAN_LOG. The Mac rows (visiting the URL, Cmd+L
// on it, retyping it, the query and ref cases) are in
// scripts/mac/steps/omnibox_trailing_space.md.
import fs from 'node:fs';
import { primary } from './lib/fullscreen.mjs';
import {
  check, finish, front, key, pages, shownViews, sleep, typeInto,
  viewTree, views, waitFor,
} from './lib/koan.mjs';

const VK = { T: 84, ESC: 27 };
const [page] = await pages();
await front(page);
const MOD = await primary();

for (const url of ['example.com/a%20', 'data:text/html,a%20', 'example.com/a%20b%20',
                 'example.com/?q=a%20', 'example.com/a#b%20', 'view-source:example.com/a%20']) {
  await key(page, VK.T, MOD, 't');
  const box = await waitFor(async () =>
      (await viewTree()).find(v => v.cls === 'OmniboxViewViews' && v.shown));
  check(`Cmd+T opens the bar for ${url}`, !!box);
  await typeInto(box, url);
  await sleep(500);
  const alive = await pages().then(p => p.length > 0, () => false);
  check(`Typing ${url} one key at a time leaves the browser running`, alive);
  const rows = shownViews(await views(), 'OmniboxResultView');
  console.log(`INFO  ${rows.length} suggestion row(s) for ${url}`);
  await key(page, VK.ESC, 0, 'Escape');
  await sleep(500);
}

const log = process.env.KOAN_LOG ? fs.readFileSync(process.env.KOAN_LOG, 'utf8') : '';
const bad = log.split('\n').filter(l => /:FATAL:|Check failed/.test(l));
check('The browser log has no FATAL or "Check failed"',
      !!process.env.KOAN_LOG && bad.length === 0, bad.slice(0, 2).join(' | '));
finish();
