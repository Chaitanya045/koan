// End-to-end test of window sync (#75: #242, #244, #248, #258, #259, #261,
// #264) with two windows, A and B, on local pages (no network):
// - a new window joins with the Spaces, the pinned tabs, the Essentials and
//   the folders of A, as placeholders: one live page per item (CDP), in A;
// - a title change in A shows in B's placeholder row (P1);
// - selecting the item in B moves the page to B, state kept (part 3 step 2);
// - closing a pinned tab in A closes it in B (C1), unpinning one in A closes
//   it in B (#264 step 3);
// - a folder rename in A shows in B, a collapse in A does not (#264 step 1),
//   and a third window starts with it collapsed (#264 step 2);
// - a Space made in A shows in B's bottom bar (#244 step 1);
// - nothing fatal in the log, Koan alive throughout.
// Run by scripts/mac/suite.sh (or scripts/e2e/run.sh window_sync).
//
// Windows open through CDP (Target.createTarget), which navigates to their
// URL in a new window: with Zen's open-link-in-new-unsynced-window on, that
// is a New Blank Window, never synced. prefs/window_sync.json turns it off
// (koan.window_sync.open_links_in_blank_window), so they join like a New
// Window does.
//
// On macOS the tab and folder context menus are native NSMenus
// (MenuRunnerImplMac), which UI DevTools can't see or click, and Close
// Window is the main menu's: the Essential is pinned and window C closed by
// the test_helper extension (window_sync.flags), and the folder steps, which
// need its menus, are skipped.
import { browserCall, check, connect, evalIn, finish, pages, sleep, tabMenuCommand, typeInto,
         viewEvent, viewTree, waitFor } from './lib/koan.mjs';
import { clickMenuItem } from './lib/folders.mjs';
import { ACCEL, ALT, CTRL, IS_MAC, SHIFT, alive, askTestHelper, clickThroughRoot, closeWindowOf,
         fatalInLog, key, openWindow, skipped, startServer, widgetWithTab, windowOf,
         windows } from './lib/mac.mjs';

const server = await startServer();
const U = t => server.url(`/p?t=${encodeURIComponent(t)}`);
const PIN1 = U('Pin one'), PIN2 = U('Pin two'), PIN3 = U('Pin three'), FOLDER_TAB = U('Folder tab');
const ESS = U('Essential one'), A_ONLY = U('Window A');

const NATIVE_MENUS = IS_MAC;
// CDP lists a placeholder too (a tab whose contents have no renderer, with
// the page's history copied, like a discarded tab), at the page's URL. Only
// a live page answers an evaluation; a placeholder's never does.
async function isLive(page) {
  const c = await connect(page.webSocketDebuggerUrl);
  const r = await Promise.race([
    c.send('Runtime.evaluate', { expression: '1', returnByValue: true }), sleep(1500)]);
  c.close();
  return r?.result?.result?.value === 1;
}
const livePages = async url => {
  const live = [];
  for (const p of (await pages()).filter(p => p.url === url)) {
    if (await isLive(p)) live.push(p);
  }
  return live;
};
const pageByUrl = async url => (await livePages(url))[0];
async function newTab(url) {
  await browserCall('Target.createTarget', { url, background: false });
  return waitFor(() => pageByUrl(url), 8000);
}
// A's and B's widgets, told apart by a normal (never synced) tab of each.
const widgetA = vs => widgetWithTab('Window A', vs);
const widgetB = vs => widgetWithTab('Window B', vs);
const rowsIn = (w, pred) => (w?.views ?? []).filter(v => v.cls === 'KoanTabRow' && pred(v.props));
const rowFor = (w, url) => rowsIn(w, p => p.PinnedUrl === url)[0];
const folderRows = w => (w?.views ?? []).filter(v => v.cls === 'KoanFolderRowView')
    .map(v => ({ v, name: v.props.FolderName, collapsed: v.props.Collapsed === 'true',
                 count: +v.props.TabCount }));

// --- Window A: two pinned tabs, an Essential, a folder of a pinned tab.
const a = await newTab(A_ONLY);
const aWindow = (await windowOf(a.id)).windowId;
for (const url of [PIN1, PIN2, PIN3, FOLDER_TAB]) {
  const page = await newTab(url);
  await key(page, 'd', ACCEL | SHIFT, 1200);  // Toggle Pin Tab
}
let vs = await viewTree();
let A = await widgetA(vs);
check('A: four pinned tabs', [PIN1, PIN2, PIN3, FOLDER_TAB].every(u => rowFor(A, u)),
      rowsIn(A, p => !!p.PinnedUrl).map(r => r.props.PinnedUrl).join(' '));
const essential = await newTab(ESS);
if (NATIVE_MENUS) {
  // An extension's pin, which makes an Essential as the menu item does.
  await askTestHelper(essential, 'pin-tab', 1500);
  check('A: an Essential (pinned by an extension)',
        rowsIn(await widgetA(), p => p.Essential === 'true').length >= 1);
} else {
  check('A: "Add to Essentials" on a tab',
        await tabMenuCommand({ title: 'Essential one' }, 'kKoanAddToEssentialsMenuItem'));
}
// The folder: the tab menu's Move to Folder > New Folder, named "Reading".
if (NATIVE_MENUS) {
  skipped('A: a folder "Reading" holds the pinned tab', 'needs the native tab menu');
} else {
  const made = await tabMenuCommand({ title: 'Folder tab' }, 'kKoanTabNewFolderItem',
                                    { via: 'kKoanMoveToFolderItem' });
  const field = await waitFor(async () => (await viewTree()).find(
      v => v.cls === 'KoanFolderNameField' && v.shown), 4000);
  if (field) await typeInto(field, 'Reading', 13);
  await sleep(800);
  const f = folderRows(await widgetA());
  check('A: a folder "Reading" holds the pinned tab', made && f.some(r => r.name === 'Reading' && r.count === 1),
        JSON.stringify(f.map(r => [r.name, r.count])));
}
await evalIn(await pageByUrl(PIN1), 'window.__koanLive = "from A"; 1');

// --- Window B joins.
const b = await openWindow(U('Window B'));
check('A second window opened', (await windows()).length === 2);
await sleep(2500);
vs = await viewTree();
let B = await widgetB(vs);
check('B: has the pinned tabs as rows', [PIN1, PIN2, PIN3].every(u => rowFor(B, u)),
      rowsIn(B, p => !!p.PinnedUrl).map(r => r.props.PinnedUrl).join(' '));
check('B: has the Essential', rowsIn(B, p => p.Essential === 'true').length >= 1);
if (!NATIVE_MENUS) {
  check('B: has the folder "Reading"', folderRows(B).some(r => r.name === 'Reading'),
        JSON.stringify(folderRows(B).map(r => r.name)));
}
check('B: never got A\'s normal tab (only pinned items sync)',
      !(B?.views ?? []).some(v => v.cls === 'TabTitle' && v.props.Text === 'Window A'));
check('One live page per synced item (B\'s rows are placeholders)',
      (await livePages(PIN1)).length === 1 && (await livePages(ESS)).length === 1,
      `${(await livePages(PIN1)).length} / ${(await livePages(ESS)).length}`);
{
  const w = await windowOf((await pageByUrl(PIN1)).id);
  check('...and it is in A', w?.windowId === aWindow, `window ${w?.windowId}, A ${aWindow}`);
}

// --- P1: a title change in A shows in B's row.
await evalIn(await pageByUrl(PIN1), 'document.title = "Pin one renamed"; 1');
const titled = await waitFor(async () => {
  const w = await widgetB();
  const row = rowFor(w, PIN1);
  return row && w.views.some(v => v.cls === 'TabTitle' && v.props.Text === 'Pin one renamed' &&
                                  v.ay >= row.ay - 2 && v.ay <= row.ay + row.h);
}, 4000);
check('P1: a title change in A shows in B\'s placeholder row', !!titled);

// --- The page moves to B when its row is selected there, state kept.
{
  vs = await viewTree();
  B = await widgetB(vs);
  const row = rowFor(B, PIN1);
  if (row) await clickThroughRoot(vs, row);
  await sleep(2000);
  const page = await pageByUrl(PIN1);
  const w = page && await windowOf(page.id);
  check('Selecting the row in B moves the page to B', w?.windowId === b.windowId,
        `window ${w?.windowId}, B ${b.windowId}`);
  check('...the same page, not a reload', page && await evalIn(page, 'window.__koanLive') === 'from A');
  check('...still one live page', (await livePages(PIN1)).length === 1);
}

// --- C1: a pinned tab closed in A closes in B.
{
  const page = await pageByUrl(PIN2);
  await browserCall('Target.closeTarget', { targetId: page.id });
  const gone = await waitFor(async () => !rowFor(await widgetB(), PIN2), 4000);
  // If not: did A put it back (a reconcile before its report), and is B's a
  // placeholder or a page?
  check('C1: a pinned tab closed in A goes from B', !!gone,
        gone ? '' : `A has it again: ${!!rowFor(await widgetA(), PIN2)}, ` +
                    `live pages: ${(await livePages(PIN2)).length}`);
  check('...and stays closed in A', !rowFor(await widgetA(), PIN2));
}
// #264 step 3: unpinning in A keeps it in A as a normal tab, closes it in B.
{
  // Toggle Pin Tab acts on the window's active tab.
  const pin3 = await pageByUrl(PIN3);
  await browserCall('Target.activateTarget', { targetId: pin3.id });
  await sleep(500);
  await key(pin3, 'd', ACCEL | SHIFT, 1500);
  const gone = await waitFor(async () => !rowFor(await widgetB(), PIN3), 4000);
  check('Unpinning in A closes it in B', !!gone);
  check('...and A keeps it as a normal tab', !!(await pageByUrl(PIN3)));
}

// --- Folders: a rename syncs, a collapse doesn't (#264).
if (NATIVE_MENUS) {
  skipped('Folders: a rename in A syncs to B, a collapse doesn\'t, a third window starts collapsed',
          'needs the native folder menu');
} else {
  vs = await viewTree();
  A = await widgetA(vs);
  const folder = folderRows(A).find(r => r.name === 'Reading');
  if (folder) {
    await clickThroughRoot(vs, folder.v, { button: 'right' });
    await clickMenuItem('kKoanFolderRenameItem');
    const field = await waitFor(async () => (await viewTree()).find(
        v => v.cls === 'KoanFolderNameField' && v.shown), 4000);
    if (field) {
      // Select all, then the new name.
      for (let i = 0; i < 'Reading'.length; i++) {
        await viewEvent(field, 'DOM.dispatchKeyEvent',
                        { type: 'keyPressed', keyCode: 8, code: 0, flags: 0, key: 0, isChar: false });
      }
      await typeInto(field, 'Later', 13);
    }
  }
  const renamed = await waitFor(async () => folderRows(await widgetB()).some(r => r.name === 'Later'), 4000);
  check('A folder renamed in A is renamed in B', !!renamed,
        JSON.stringify(folderRows(await widgetB()).map(r => r.name)));
  vs = await viewTree();
  A = await widgetA(vs);
  const row = folderRows(A).find(r => r.name === 'Later');
  if (row) await clickThroughRoot(vs, row.v);  // a click toggles it
  await sleep(1500);
  const inA = folderRows(await widgetA()).find(r => r.name === 'Later');
  const inB = folderRows(await widgetB()).find(r => r.name === 'Later');
  check('Collapsing it in A', inA?.collapsed === true);
  check('...leaves B expanded', inB && !inB.collapsed);
  const c = await openWindow(U('Window C'));
  await sleep(2500);
  const C = await widgetWithTab('Window C');
  const inC = folderRows(C).find(r => r.name === 'Later');
  check('A third window starts with it collapsed (as saved)', inC?.collapsed === true,
        JSON.stringify(folderRows(C).map(r => [r.name, r.collapsed])));
  // Close Window (C also holds the synced placeholders, so closing its one
  // normal tab would leave it open).
  const cPage = (await pages()).find(p => p.id === c.targetId);
  if (cPage) await closeWindowOf(cPage, 1500);
  check('Window C closed', !!(await waitFor(async () =>
      !(await windows()).some(w => w.windowId === c.windowId), 8000)));
}

// --- #244 step 1: a Space made in A shows in B's bottom bar.
{
  await key(await pageByUrl(A_ONLY), 'n', CTRL | ALT, 1200);  // Create a Space
  const field = await waitFor(async () => (await viewTree()).find(
      v => v.cls === 'Textfield' && v.shown && v.path.includes('KoanSpaceCreationView')), 4000);
  if (field) await typeInto(field, 'Synced', 13);
  await sleep(1500);
  const spaces = async w => (w?.views ?? []).filter(v => v.cls === 'KoanSpaceButton')
      .map(v => v.props.TooltipText);
  const inB = await waitFor(async () => (await spaces(await widgetB())).includes('Synced'), 4000);
  check('A Space created in A shows in B', !!field && !!inB,
        JSON.stringify(await spaces(await widgetB())));
}

check('Koan is alive', await alive());
check('Nothing fatal in the log', !fatalInLog(), fatalInLog());
check('Two windows still open', (await windows()).length === 2);
server.close();
finish();
