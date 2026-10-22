// End-to-end test of Zen-style tab folders against a real build: creating
// them (the sidebar's "+" menu and the tab menu's "Move to Folder"),
// renaming, collapsing (a collapsed folder still shows its active tab),
// subfolders, unpacking, search, unloading, converting to a Space, changing
// Space, deleting (a Space emptied that way survives), Clear keeping folder
// tabs, Remove from Folder (dragging: folders_drag.mjs), the order of pinned
// tabs and folders, a closed folder tab reopening in its folder; then that
// folders come back after restarting (session restore, and once more from
// the rebuilt session file), and closing a window mid-rename.
//
//   scripts/e2e/run.sh folders
//
// Menus are driven through UI DevTools: their items are found by element
// identifier (DOM.performSearch "id:...") and clicked through the menu
// widget's root view, like a real click.
import { spawn } from 'node:child_process';
import { CDP_PORT, check, evalIn, finish, pages, sleep } from './lib/koan.mjs';
import {
  activate, browserCdp, click, clickMenuItem, cls, findAll, folderMenu, loosest,
  moveToFolder, origin, pageKey, pagesInCollapsedFolders, pressKey, props, rowNamed,
  rows, shown, shownTabs, switcher, switcherLabels, typeInto, visibleTabs, withView,
} from './lib/folders.mjs';

const names = rs => rs.map(r => `${r.name}${r.collapsed ? '[c]' : ''}:${r.count}`).join(' ');

const VK = { RETURN: 13, ESCAPE: 27 };

// Two more tabs, so there are tabs to put in folders: [one, two, three, four].
const first = (await pages()).find(t => t.url === 'https://example.com/');
await browserCdp('Target.createTarget', { url: 'https://example.com/?three', background: true });
await browserCdp('Target.createTarget', { url: 'https://example.com/?four', background: true });
await activate(first);
let tabs = await visibleTabs();
check('Four tabs to start with', tabs.length === 4, `${tabs.length}`);

// "+" -> Create Folder: an empty folder, being renamed, like Zen.
await withView(/^KoanWorkspaceButton$/, 0, async (s) => {
  for (const n of findAll(s, /^KoanWorkspaceButton$/)) {
    if ((await props(s, n)).Label === '+') await click(s, n);
  }
});
check('"+" opens the Create New menu with Create Folder',
      await clickMenuItem('kKoanCreateFolderItem'));
let r = await rows();
check('Create Folder adds an empty "New Folder" row', r.length === 1 &&
      r[0].name === 'New Folder' && r[0].count === 0, names(r));
check('The new folder starts renaming', r[0]?.renaming === true);
await typeInto(/^KoanFolderNameField$/, 'Work');
await pressKey(/^KoanFolderNameField$/, VK.RETURN);
await sleep(500);
r = await rows();
check('Typing a name and Enter renames it', r[0]?.name === 'Work' && !r[0]?.renaming,
      names(r));

// Tab menu: Move to Folder > Work, for the second tab.
await withView(/^TabView$/, 1, (s, n) => click(s, n, 'right'));
check('The tab menu has "Move to Folder"', await clickMenuItem('kKoanMoveToFolderItem'));
check('...listing the folder', await clickMenuItem('kKoanTabFolderItem'));
r = await rows();
check('The tab is in the folder', r[0]?.count === 1, names(r));
tabs = await visibleTabs();
const folderRow = r[0];
const minX = Math.min(...tabs.map(t => t.x)), maxX = Math.max(...tabs.map(t => t.x));
const indented = tabs.filter(t => t.x === maxX);
check('The folder row comes first, its tab indented 14px below it',
      tabs.length === 4 && maxX - minX === 14 && indented.length === 1 &&
      indented[0].y > (folderRow?.y ?? 1e9) && indented[0].y === Math.min(...tabs.map(t => t.y)),
      JSON.stringify({ row: folderRow?.y, tabs }));

// Tab menu: Move to Folder > New Folder, for the third tab (now the
// third row of tabs too: [two (in Work), one, three, four]).
await withView(/^TabView$/, 2, (s, n) => click(s, n, 'right'));
await clickMenuItem('kKoanMoveToFolderItem');
check('"New Folder" in the tab menu', await clickMenuItem('kKoanTabNewFolderItem'));
await pressKey(/^KoanFolderNameField$/, VK.ESCAPE);
await sleep(500);
r = await rows();
check('...makes a folder holding that tab', r.length === 2 && r[1].name === 'New Folder' &&
      r[1].count === 1, names(r));

// Collapse "Work" (its tab isn't the active one): the tab hides.
await withView(/^KoanFolderRowView$/, 0, async (s, n) => {
  for (const type of ['mousePressed', 'mouseReleased']) {
    await s.c.send('DOM.dispatchMouseEvent', { nodeId: n.nodeId,
      event: { type, x: 20, y: 10, button: 'left', wheelDirection: 'none' } });
  }
});
await sleep(1200);
r = await rows();
tabs = await visibleTabs();
check('Clicking a folder collapses it', r[0]?.collapsed === true, names(r));
check('A collapsed folder hides its tabs', tabs.length === 3, `${tabs.length} visible`);

// Selecting a tab of a collapsed folder shows it under the folder (Zen):
// activating each tab in turn, exactly one (the folder's) comes back.
const seen = [];
let loose = null;
for (const page of await pages()) {
  await activate(page);
  await sleep(800);
  const n = (await visibleTabs()).length;
  seen.push(n);
  if (n === 3) loose ??= page;
}
r = await rows();
check('...except its active tab', seen.filter(n => n === 4).length === 1 && r[0]?.collapsed,
      `visible per active tab: ${seen.join(' ')}`);
if (loose) await activate(loose);
await sleep(800);
tabs = await visibleTabs();
check('...which hides again when another tab is active', tabs.length === 3,
      `${tabs.length} visible`);

// Folder search: hovering a collapsed folder opens it.
await withView(/^KoanFolderRowView$/, 0, (s, n) => s.c.send('DOM.dispatchMouseEvent', {
  nodeId: n.nodeId, event: { type: 'mouseEntered', x: 20, y: 10, button: 'none', wheelDirection: 'none' } }));
await sleep(1500);
let found = await withView(/^KoanFolderSearchItem$/, 0, () => true);
check('Hovering a collapsed folder opens its search', found);
await withView(/^KoanFolderSearchView$/, 0, async (s, bubble) => {
  const field = findAll(s, /^Textfield$/).find(n => {
    for (let v = n; v; v = v.parent) if (v === bubble) return true;
    return false;
  });
  for (const ch of 'zzz') {
    await s.c.send('DOM.dispatchKeyEvent', { nodeId: field.nodeId,
      event: { type: 'keyPressed', keyCode: 0, code: 0, flags: 0, key: ch.codePointAt(0), isChar: true } });
  }
});
await sleep(500);
found = await withView(/^KoanFolderSearchItem$/, 0, async (s, n) => (await shown(s, n)));
check('...which filters as you type', found === false);
found = await withView(/^Label$/, 0, async (s) => {
  for (const n of findAll(s, /^Label$/)) {
    const p = await props(s, n);
    if ((p.Text ?? '').startsWith('No tabs matching') && await shown(s, n)) return true;
  }
  return false;
});
check('...and says when nothing matches', found === true);

// Expand again.
await withView(/^KoanFolderRowView$/, 0, async (s, n) => {
  for (const type of ['mousePressed', 'mouseReleased']) {
    await s.c.send('DOM.dispatchMouseEvent', { nodeId: n.nodeId,
      event: { type, x: 20, y: 10, button: 'left', wheelDirection: 'none' } });
  }
});
await sleep(1200);
tabs = await visibleTabs();
check('Clicking again expands it', tabs.length === 4, `${tabs.length} visible`);

// Folder menu: New Subfolder, then Rename.
check('Folder menu: New Subfolder', await folderMenu('Work', 'kKoanFolderNewSubfolderItem'));
await pressKey(/^KoanFolderNameField$/, VK.ESCAPE);
await sleep(500);
r = await rows();
const work = r.find(x => x.name === 'Work');
const sub = r.find(x => x.name === 'Subfolder');
check('...adds an indented "Subfolder" inside Work', r.length === 3 && sub &&
      sub.x - work.x === 14, names(r));
check('Folder menu: Rename Folder', await folderMenu('Subfolder', 'kKoanFolderRenameItem'));
await typeInto(/^KoanFolderNameField$/, 'Ideas');
await pressKey(/^KoanFolderNameField$/, VK.RETURN);
await sleep(500);
r = await rows();
check('...renames the subfolder', r.some(x => x.name === 'Ideas') &&
      !r.some(x => x.name === 'Subfolder'), names(r));

// Unpack: the subfolder's tab moves up into Work; the subfolder goes.
check('Move to Folder lists subfolders too',
      await moveToFolder(ts => loosest(ts).at(-1), 'kKoanTabFolderItem', 1));
r = await rows();
const workCount = r.find(x => x.name === 'Work')?.count;
check('...the tab is in the subfolder (and counts for Work)',
      r.find(x => x.name === 'Ideas')?.count === 1 && workCount === 2, names(r));
const visibleBefore = (await visibleTabs()).length;
check('Folder menu: Unpack Folder', await folderMenu('Ideas', 'kKoanFolderUnpackItem'));
r = await rows();
check('...removes the subfolder and keeps its tab, now in Work',
      !r.some(x => x.name === 'Ideas') && r.find(x => x.name === 'Work')?.count === 2 &&
          (await visibleTabs()).length === visibleBefore,
      names(r));

// Unload All Tabs: discards the folder's tabs and collapses it (Zen).
check('Folder menu: Unload All Tabs', await folderMenu('Work', 'kKoanFolderUnloadItem'));
r = await rows();
const unloaded = r.find(x => x.name === 'Work');
check('...collapses the folder and keeps its tabs',
      unloaded?.collapsed && unloaded?.count === 2, names(r));
const work2 = await pagesInCollapsedFolders();
const discarded = [];
for (const page of work2.inFolders) {
  await activate(page);
  await sleep(1500);
  discarded.push(await evalIn((await pages()).find(p => p.id === page.id) ?? page,
                              'document.wasDiscarded'));
}
if (work2.loose) await activate(work2.loose);
check('...which were discarded (they reload when shown)',
      discarded.length === 2 && discarded.every(Boolean), JSON.stringify(discarded));

// Convert "Work" (now with an empty subfolder again) to a Space: a new
// workspace with its tabs; the subfolder stays a folder there, like Zen's.
await folderMenu('Work', 'kKoanFolderNewSubfolderItem');
await pressKey(/^KoanFolderNameField$/, VK.ESCAPE);
await sleep(500);
check('Folder menu: Convert folder to Space', await folderMenu('Work', 'kKoanFolderConvertItem'));
await sleep(1000);
let labels = (await switcherLabels()).map(l => l.name);
tabs = await visibleTabs();
r = await rows();
check('...creates a Space named after the folder', labels.includes('Work'), JSON.stringify(labels));
check('...showing the folder\'s tabs', tabs.length === 2, `${tabs.length} visible`);
check('...and the folder is gone; its subfolder stays a folder in the Space',
      !r.some(x => x.name === 'Work') && r.some(x => x.name === 'Subfolder' && x.visible),
      names(r));

// Back in "All tabs", move "New Folder" to the Work Space (Change Space).
await switcher('∞');
check('Folder menu: Change Space', await folderMenu('New Folder', 'kKoanFolderChangeSpaceItem'));
check('...lists the Spaces', await clickMenuItem('kKoanFolderSpaceItem'));
const inSpace = await rowNamed('New Folder', async (s, n) => {
  for (let v = n; v; v = v.parent) {
    if (cls(v) === 'TabGroupView') return true;
  }
  return false;
});
check('...moves the folder into that Space', inSpace === true);

// Delete Folder closes exactly its tabs.
await rowNamed('New Folder', (s, n) => click(s, n));  // Collapse it.
await sleep(1000);
const doomed = (await pagesInCollapsedFolders()).inFolders.map(p => p.id);
const before = (await pages()).map(p => p.id);
check('Folder menu: Delete Folder', await folderMenu('New Folder', 'kKoanFolderDeleteItem'));
await sleep(1000);
const after = (await pages()).map(p => p.id);
const closed = before.filter(id => !after.includes(id));
r = await rows();
check('...removes the folder and closes its tab (and only it)',
      !r.some(x => x.name === 'New Folder') && doomed.length === 1 &&
          JSON.stringify(closed) === JSON.stringify(doomed),
      `closed ${closed.length}, in folder ${doomed.length}; rows: ${names(r)}`);

// A Space survives its folder taking all its tabs away (a Space is a tab
// group, which would go with its last tab; Zen's Space stays): Delete,
// Change Space and Convert to Space each leave it a fresh tab.
await switcher('+');
await clickMenuItem('kKoanCreateSpaceItem');
await sleep(1500);
const spaceTwo = (await switcherLabels()).map(l => l.name).find(n => /Space \d/.test(n));
async function folderOfOnlyTab() {
  await moveToFolder(ts => ts[0], 'kKoanTabNewFolderItem');
  await pressKey(/^KoanFolderNameField$/, VK.ESCAPE);
  await sleep(800);
}
await folderOfOnlyTab();
await folderMenu('New Folder', 'kKoanFolderDeleteItem');
await sleep(1500);
labels = (await switcherLabels()).map(l => l.name);
check('Deleting the folder holding all of a Space\'s tabs keeps the Space',
      !!spaceTwo && labels.includes(spaceTwo) && (await visibleTabs()).length === 1,
      `${JSON.stringify(labels)}, ${(await visibleTabs()).length} tab(s)`);
await folderOfOnlyTab();
await folderMenu('New Folder', 'kKoanFolderChangeSpaceItem');
// The menu lists the Spaces in the switcher's order.
const spaces = (await switcherLabels()).filter(l => l.label !== '\u221e' && l.label !== '+');
await clickMenuItem('kKoanFolderSpaceItem', spaces.findIndex(l => l.name === 'Work'));
await sleep(1500);
labels = (await switcherLabels()).map(l => l.name);
check('...and so does moving it to another Space', labels.includes(spaceTwo),
      JSON.stringify(labels));
await switcher(spaceTwo);
await sleep(1000);
await folderOfOnlyTab();
await folderMenu('New Folder', 'kKoanFolderConvertItem');
await sleep(1500);
labels = (await switcherLabels()).map(l => l.name);
check('...and converting it to a Space', labels.includes(spaceTwo) &&
      labels.includes('New Folder'), JSON.stringify(labels));

// Clear (Ctrl+Shift+K) closes the Space's loose tabs but keeps its folders'
// tabs, like Zen's (they're pinned there).
await switcher(spaceTwo);
await sleep(1000);
for (const q of ['c1', 'c2']) {
  await browserCdp('Target.createTarget', { url: `https://example.com/?${q}`, background: false });
}
await sleep(2000);
await moveToFolder(ts => ts.at(-1), 'kKoanTabNewFolderItem');
await typeInto(/^KoanFolderNameField$/, 'Keep');
await pressKey(/^KoanFolderNameField$/, VK.RETURN);
await sleep(800);
// c2, the Space's last tab, went into "Keep" (activating every page to
// find out would switch Spaces).
const kept = (await pages()).filter(p => p.url.endsWith('?c2')).map(p => p.url);
const c1 = (await pages()).find(p => p.url.endsWith('?c1'));
if (c1) await activate(c1);
await pageKey(c1 ?? (await pages())[0], 75, 2 | 8, 'K');  // Ctrl+Shift+K
const urls = (await pages()).map(p => p.url);
r = await rows();
check('Clear keeps the folder\'s tab', kept.length === 1 && urls.includes(kept[0]) &&
      r.find(x => x.name === 'Keep')?.count === 1, `${JSON.stringify(kept)} in ${JSON.stringify(urls)}`);
check('...and closes the loose tabs but the selected one', urls.includes(c1?.url) &&
      (await visibleTabs()).length === 2, `${(await visibleTabs()).length} shown`);

// (Dragging tabs in and out of folders: folders_drag.mjs, with real input;
// headless Ozone can't run a tab drag.)

// Remove from Folder in the tab menu.
await moveToFolder(ts => ts.find(t => t.x > Math.min(...ts.map(u => u.x))),
                   'kKoanTabRemoveFromFolderItem');
r = await rows();
check('Tab menu: Remove from Folder', r.find(x => x.name === 'Keep')?.count === 0, names(r));

// A Space's pinned tabs come first, then its folders, then the other tabs,
// like Zen's createFolder: pinning c1 (Ctrl+Shift+D) puts it above "Keep".
// (One UI DevTools connection at a time: rows and tabs from one snapshot.)
const order = () => withView(/^TabView$/, 0, async (s) => {
  const ts = await shownTabs(s);
  let row;
  for (const n of findAll(s, /^KoanFolderRowView$/)) {
    if ((await props(s, n)).FolderName === 'Keep' && await shown(s, n)) {
      row = (await origin(s, n)).y;
    }
  }
  return { ts: ts.map(t => t.ay), row };
});
await activate(c1);
await pageKey(c1, 68, 2 | 8, 'D');
await sleep(1500);
let o = await order();
check('A pinned tab goes above the folders, loose tabs below',
      o.ts.length === 2 && o.ts[0] < o.row && o.row < o.ts[1], JSON.stringify(o));
// Moved into a folder, it isn't pinned any more and goes under the row.
await moveToFolder(ts => ts[0], 'kKoanTabFolderItem', 0);
r = await rows();
o = await order();
check('...and moved into a folder, it\'s unpinned and under the row',
      r.find(x => x.name === 'Keep')?.count === 1 && o.row < o.ts[0], JSON.stringify(o));
// A folder tab closed on its own reopens in its folder (Ctrl+Shift+T).
const c1Now = (await pages()).find(p => p.url.endsWith('?c1'));
await browserCdp('Target.closeTarget', { targetId: c1Now?.id });
await sleep(1000);
r = await rows();
const closedOk = r.find(x => x.name === 'Keep')?.count === 0;
await pageKey((await pages()).find(p => p.url.endsWith('?c2')), 84, 2 | 8, 'T');
await sleep(1500);
r = await rows();
check('A folder tab closed and reopened comes back in its folder',
      closedOk && r.find(x => x.name === 'Keep')?.count === 1, names(r));
await moveToFolder(ts => ts.find(t => t.x > Math.min(...ts.map(u => u.x))),
                   'kKoanTabRemoveFromFolderItem');

// Folders come back after a restart: Koan restarted on the same profile
// (KOAN_CHROME_ARGS, see run.sh) restores the session; the second restart
// reads the session file Koan rebuilt after the first. "Keep" (in the Space,
// collapsed) holds a tab and the subfolder "Inner", which holds another.
await moveToFolder(ts => loosest(ts).at(-1), 'kKoanTabFolderItem', 0);
await folderMenu('Keep', 'kKoanFolderNewSubfolderItem');
await typeInto(/^KoanFolderNameField$/, 'Inner');
await pressKey(/^KoanFolderNameField$/, VK.RETURN);
await sleep(800);
await moveToFolder(ts => loosest(ts).at(-1), 'kKoanTabFolderItem', 1);
await rowNamed('Keep', (s, n) => click(s, n));
await sleep(3000);
const state = async () => {
  const rs = await rows();
  const keep = rs.find(x => x.name === 'Keep');
  const inner = rs.find(x => x.name === 'Inner');
  const inGroup = await rowNamed('Keep', async (s, n) => {
    for (let v = n; v; v = v.parent) if (cls(v) === 'TabGroupView') return true;
    return false;
  });
  return { ok: !!keep?.collapsed && keep?.count === 2 && inner?.count === 1 && inGroup === true,
           text: names(rs) };
};
let st = await state();
check('A collapsed folder with a subfolder, in a Space, before restarting', st.ok, st.text);
let relaunched = null;
let exitCode = null;
process.on('exit', () => relaunched?.kill());
for (const time of ['first', 'second']) {
  if (time === 'second') {
    // Closing the window while a folder is being renamed (S6 of the review).
    await folderMenu('Keep', 'kKoanFolderRenameItem');
  }
  const exited = relaunched ? new Promise(r => relaunched.once('exit', c => r(c))) : null;
  await browserCdp('Browser.close', {}).catch(() => {});
  for (let i = 0; i < 40; i++) {
    await sleep(500);
    if (!await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).catch(() => null)) break;
  }
  if (exited) exitCode = await Promise.race([exited, sleep(10000).then(() => 'timeout')]);
  await sleep(2000);
  const args = process.env.KOAN_CHROME_ARGS.split('\n').filter(Boolean);
  relaunched = spawn(args[0], args.slice(1), { stdio: 'ignore' });
  for (let i = 0; i < 60; i++) {
    await sleep(500);
    if (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).catch(() => null)) break;
  }
  await sleep(5000);
  st = await state();
  check(`The folders are back after the ${time} restart`, st.ok, st.text);
  await sleep(3000);
}
check('Closing the window while renaming a folder exits cleanly', exitCode === 0,
      String(exitCode));

finish();
