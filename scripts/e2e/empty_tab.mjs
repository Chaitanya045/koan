// End-to-end test of Zen's empty tab against a real build. Closing the last
// tab keeps the window and leaves the Space with an empty tab: no tab row in
// the sidebar and no second new tab. A page opened from the URL bar opens in
// a new tab (the empty tab never navigates), Ctrl+Tab and Cmd+1-9 skip the
// empty tab, and Cmd+W on the empty tab closes the window.
//
//   scripts/e2e/run.sh empty_tab
import {
  ACCEL, CTRL, SHIFT, check, finish, front, key, pages, shownViews, sleep, typeInto, viewTree,
  views, waitFor, browserCall, onPage, evalIn, skipOnMac,
} from './lib/koan.mjs';
skipOnMac();

const VK = { T: 84, RETURN: 13, TAB: 9, ONE: 49 };
// ACCEL (lib/koan.mjs): Cmd+1-9 select a tab on macOS (Ctrl+1-9 there
// switch Spaces).
const isShown = async page =>
    await evalIn(page, 'document.visibilityState') === 'visible';
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

// Cmd+T opens the URL bar in place of a new tab; what it opens goes into a
// NEW tab: the empty tab never navigates (Zen's UrlbarChildController).
const [page] = await pages();
await front(page);
await key(page, VK.T, CTRL, 't');
const omnibox = await waitFor(async () => (await viewTree()).find(
    v => v.cls === 'OmniboxViewViews' && v.shown));
await typeInto(omnibox, 'example.com/?opened', VK.RETURN);
await sleep(1500);
const after = await pages();
check('Opening a page from the empty state opens it in a new tab',
      after.some(p => p.url.includes('?opened')) &&
          after.some(p => p.url.startsWith('chrome://')),
      after.map(p => p.url).join(' '));
check('...and the sidebar lists just that one (the empty tab has no row)',
      await tabRows() === 1, String(await tabRows()));

// The hidden empty tab is out of Ctrl+Tab and Cmd+1-9 (#177): with it and the
// opened page in the Space, neither leaves the opened page.
const opened = after.find(p => p.url.includes('?opened'));
await key(opened, VK.TAB, CTRL, 'Tab');
await sleep(500);
check('Ctrl+Tab skips the hidden empty tab', await isShown(opened));
await key(opened, VK.TAB, CTRL | SHIFT, 'Tab');
await sleep(500);
check('...and so does Ctrl+Shift+Tab', await isShown(opened));
await key(opened, VK.ONE, ACCEL, '1');
await sleep(500);
check('Cmd+1 selects the first listed tab, not the empty tab',
      await isShown(opened));

// Cmd+W on the page that was opened: back to the empty state, then Cmd+W on
// it closes the window.
await key(after.find(p => p.url.includes('?opened')), 87, CTRL, 'w');
await sleep(1000);
check('Closing it shows the empty state again, with no row',
      await tabRows() === 0, String(await tabRows()));
// The empty tab that navigates in place (a bookmark, the Library, a dropped
// link, chrome.tabs.update) is an ordinary tab then: Back into the new tab
// page does not make it the empty tab again (no vanishing row, and Cmd+W
// closes the tab, not the whole window).
const emptyPage = (await pages())[0];
await onPage(emptyPage, c => c.send('Page.navigate', { url: 'https://example.com/?inplace' }));
await waitFor(async () => (await pages())[0]?.url.includes('?inplace'), 8000);
await evalIn((await pages())[0], 'history.back()');
await waitFor(async () => (await pages())[0]?.url.startsWith('chrome://'), 8000);
await sleep(800);
check('Back into the once-empty tab: it has its row (it is a tab now)',
      await tabRows() === 1, String(await tabRows()));
await key((await pages())[0], 87, CTRL, 'w');
await sleep(1500);
check('...and Cmd+W closes that tab only: the window stays, with the empty tab',
      (await pages()).length === 1 && await tabRows() === 0,
      `${(await pages()).length} page(s), ${await tabRows()} row(s)`);

const windowsBefore = (await pages()).length;
await key((await pages())[0], 87, CTRL, 'w');
await sleep(1500);
check('Cmd+W on the empty tab closes the window',
      (await pages()).length < windowsBefore || (await pages()).length === 0,
      String((await pages()).length));
finish();
