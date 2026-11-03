// End-to-end test of Zen's empty tab against a real build. Closing the last
// tab keeps the window and leaves the Space with an empty tab: no tab row in
// the sidebar and no second new tab. The next page opened from the URL bar
// replaces it, so exactly one tab is left.
//
//   scripts/e2e/run.sh empty_tab
import {
  CTRL, check, finish, front, key, pages, shownViews, sleep, typeInto, viewTree,
  views, waitFor, browserCall,
} from './lib/koan.mjs';

const VK = { T: 84, RETURN: 13 };
const tabRows = async () =>
    shownViews(await views(), 'TabView').filter(t => t.h > 1).length;

check('The Space starts with two tab rows', await tabRows() === 2,
      String(await tabRows()));

// Close every tab, the way closing a tab does.
for (const page of await pages()) {
  await browserCall('Target.closeTarget', { targetId: page.id });
  await sleep(600);
}
await waitFor(async () => (await pages()).length === 1, 8000);
const left = await pages();
check('Closing the last tab keeps the window, with one (empty) tab',
      left.length === 1, left.map(p => p.url).join(' '));
check('...showing the new tab page, in the window the user is in',
      left[0]?.url.startsWith('chrome://'), left[0]?.url);
check('...with no tab row in the sidebar', await tabRows() === 0,
      String(await tabRows()));
check('...and the "+ New Tab" row still there',
      shownViews(await views(), 'KoanNewTabRow').length === 1);

// The URL bar opens in place of a new tab; what it opens replaces the empty tab.
const [page] = await pages();
await front(page);
await key(page, VK.T, CTRL, 't');
const omnibox = await waitFor(async () => (await viewTree()).find(
    v => v.cls === 'OmniboxViewViews' && v.shown));
await typeInto(omnibox, 'example.com/?replaced', VK.RETURN);
await sleep(1500);
const after = await pages();
check('Opening a page from the empty state leaves exactly one tab',
      after.length === 1 && after[0].url.includes('?replaced'),
      after.map(p => p.url).join(' '));
check('...now with one tab row', await tabRows() === 1, String(await tabRows()));
finish();
