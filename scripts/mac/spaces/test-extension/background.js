// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.
//
// Spaces lockdown checks (PR #159; .agent/notes/upstream/
// tab-groups-omnibox-fullscreen.md section 1.6, tests 6, 11 and 15). Load
// unpacked in a throwaway profile (--load-extension=<this dir>). From the
// service worker's console (chrome://extensions > this extension > service
// worker):
//   run('test15')  in a Koan window with Spaces: G3, G12 and the healed
//                  chrome.tabs/tabGroups cases; one PASS/FAIL line each.
//   run('seed6')   with --disable-features=KoanWorkspaces: Chrome-style
//                  groups for test 6 (see seed_profiles.py).
//   run('seed11')  with --disable-features=KoanWorkspaces: saved groups for
//                  test 11.
//   run('splitUngroup')  test 15b, by hand: a split-only Space ungrouped.
// The toolbar button runs test15. Results also go to chrome.storage.local
// ("result").

const sleep = ms => new Promise(r => setTimeout(r, ms));
// Koan heals a group change in its next update (a posted task); give it time.
const SETTLE = 1500;

async function newTabs(windowId, count, tag) {
  const tabs = [];
  for (let i = 0; i < count; i++) {
    tabs.push(await chrome.tabs.create(
        {windowId, url: `https://example.com/?${tag}-${i}`, active: false}));
  }
  return tabs;
}

async function seed6() {
  // A profile shaped like a copied Chrome profile: three groups (blue "Work",
  // red "Play" collapsed, an unnamed grey one) and four ungrouped tabs.
  const win = await chrome.windows.getCurrent();
  const [w1, w2, p1, g1, ...loose] = await newTabs(win.id, 7, 'seed6');
  const work = await chrome.tabs.group({tabIds: [w1.id, w2.id]});
  await chrome.tabGroups.update(work, {title: 'Work', color: 'blue'});
  const play = await chrome.tabs.group({tabIds: [p1.id]});
  await chrome.tabGroups.update(play, {title: 'Play', color: 'red'});
  await chrome.tabGroups.update(play, {collapsed: true});
  const grey = await chrome.tabs.group({tabIds: [g1.id]});
  await chrome.tabGroups.update(grey, {color: 'grey'});
  // The window's first tab plus three of ours stay ungrouped.
  await chrome.tabs.remove(loose[3].id);
  return {groups: (await chrome.tabGroups.query({windowId: win.id})).length,
          ungrouped: (await chrome.tabs.query({windowId: win.id}))
                         .filter(t => t.groupId === -1).length};
}

async function seed11() {
  // Saved groups (with Koan's Spaces off, upstream saves every new group):
  // "Saved A" stays open, "Saved B" is closed but still saved.
  const win = await chrome.windows.getCurrent();
  const [a1, a2, b1] = await newTabs(win.id, 3, 'seed11');
  const a = await chrome.tabs.group({tabIds: [a1.id, a2.id]});
  await chrome.tabGroups.update(a, {title: 'Saved A', color: 'green'});
  const b = await chrome.tabs.group({tabIds: [b1.id]});
  await chrome.tabGroups.update(b, {title: 'Saved B', color: 'yellow'});
  // Let the saved-groups service write them before one closes.
  await sleep(3000);
  // B is closed by closing its window: closing a saved group's last TAB
  // deletes the saved group on desktop, closing its window keeps it, closed.
  const w2 = await chrome.windows.create({url: 'about:blank'});
  await chrome.tabGroups.move(b, {windowId: w2.id, index: -1});
  await sleep(2500);
  await chrome.windows.remove(w2.id);
  await sleep(4000);
  return {open: (await chrome.tabGroups.query({windowId: win.id})).map(g => g.title)};
}

async function test15() {
  const lines = [];
  const check = (name, ok, detail) => {
    lines.push(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  (' + detail + ')' : ''}`);
  };
  const win = await chrome.windows.getCurrent({populate: true});
  const active = win.tabs.find(t => t.active);
  const space = active.groupId;
  check('The window shows a Space (the active tab is in a group)', space !== -1,
        `groupId ${space}`);

  // G12: a group an extension makes becomes a Space with the default theme.
  const [t1] = await newTabs(win.id, 1, 't15-group');
  const g1 = await chrome.tabs.group({tabIds: [t1.id]});
  await sleep(SETTLE);
  check('G12: tabs.group() makes a grey (default-theme) Space',
        (await chrome.tabGroups.get(g1)).color === 'grey',
        (await chrome.tabGroups.get(g1)).color);

  // G12: ...unless the extension sets its colour itself.
  const [t2] = await newTabs(win.id, 1, 't15-colour');
  const g2 = await chrome.tabs.group({tabIds: [t2.id]});
  await chrome.tabGroups.update(g2, {color: 'red', title: 'Ext'});
  await sleep(SETTLE);
  const g2info = await chrome.tabGroups.get(g2);
  check('G12: a colour the extension sets stays', g2info.color === 'red' &&
        g2info.title === 'Ext', `${g2info.color} / ${g2info.title}`);

  // G3: a Space is never collapsed.
  await chrome.tabGroups.update(space, {collapsed: true});
  await sleep(SETTLE);
  check('G3: tabGroups.update({collapsed: true}) is undone',
        !(await chrome.tabGroups.get(space)).collapsed);

  // Healed: an ungrouped tab goes back into a Space.
  const [t3] = await newTabs(win.id, 1, 't15-ungroup');
  await sleep(SETTLE);
  await chrome.tabs.ungroup([t3.id]);
  await sleep(SETTLE);
  const t3after = await chrome.tabs.get(t3.id);
  check('tabs.ungroup(): the tab is back in its Space', t3after.groupId === space,
        `groupId ${t3after.groupId}, Space ${space}`);

  // Ungrouping a tab of a Space that isn't shown puts it back there, not in
  // the Space shown; t1 is its Space's only tab, so the Space comes back with
  // it (same id) rather than with a new tab.
  await chrome.tabs.ungroup([t1.id]);
  await sleep(SETTLE);
  const t1after = await chrome.tabs.get(t1.id);
  const g1tabs = await chrome.tabs.query({groupId: t1after.groupId});
  check('tabs.ungroup() in another Space: the tab is back in that Space',
        t1after.groupId === g1 && g1tabs.length === 1,
        `groupId ${t1after.groupId} (was ${g1}), ${g1tabs.length} tab(s)`);

  // tabGroups.move() to another window: the Space moves, and the source
  // window keeps at least one Space.
  const other = await chrome.windows.create({url: 'https://example.com/?t15-other'});
  await sleep(SETTLE);
  await chrome.tabGroups.move(g2, {windowId: other.id, index: -1});
  await sleep(SETTLE);
  const moved = await chrome.tabGroups.get(g2);
  const left = await chrome.tabGroups.query({windowId: win.id});
  check('tabGroups.move(): the Space is in the other window',
        moved.windowId === other.id, `window ${moved.windowId}`);
  check('...and the source window still has a Space', left.length >= 1,
        `${left.length} group(s)`);

  // tabs.highlight() across Spaces: the window shows that tab's Space.
  await chrome.tabs.highlight({windowId: win.id, tabs: [
      (await chrome.tabs.get(t1.id)).index]});
  await sleep(SETTLE);
  const now = (await chrome.tabs.query({windowId: win.id, active: true}))[0];
  check('tabs.highlight() in another Space activates that tab',
        now.id === t1.id && now.groupId === g1, `active ${now.id} in ${now.groupId}`);

  await chrome.windows.remove(other.id);
  return lines;
}

// By hand (README, test 15b): make a Space whose only tabs are one split
// view (Create Space, then New Split from the "+" menu, and close the
// Space's other tab), show it, then run('splitUngroup'). Both tabs are
// ungrouped at once; the Space must come back with both, still one split,
// never a one-tab split.
async function splitUngroup() {
  const lines = [];
  const check = (name, ok, detail) => {
    lines.push(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  (' + detail + ')' : ''}`);
  };
  const win = await chrome.windows.getCurrent({populate: true});
  const space = win.tabs.find(t => t.active).groupId;
  const tabs = await chrome.tabs.query({groupId: space});
  const split = tabs.length ? tabs[0].splitViewId : undefined;
  check('Setup: the shown Space is two tabs in one split view',
        tabs.length === 2 && split !== undefined && split !== -1 &&
        tabs.every(t => t.splitViewId === split),
        `${tabs.length} tab(s), splitViewId ${tabs.map(t => t.splitViewId)}`);
  await chrome.tabs.ungroup(tabs.map(t => t.id));
  await sleep(SETTLE);
  const after = await Promise.all(tabs.map(t => chrome.tabs.get(t.id)));
  check('Both tabs are back in the Space (same group id)',
        after.every(t => t.groupId === space), after.map(t => t.groupId).join());
  const splits = new Set(after.map(t => t.splitViewId));
  // (Upstream may unsplit them on the ungroup; then both are plain tabs. What
  // must never happen is one tab left in a split of its own.)
  check('...still one split of two, or both unsplit (no one-tab split)',
        splits.size === 1 && !splits.has(undefined),
        after.map(t => t.splitViewId).join());
  const inSpace = await chrome.tabs.query({groupId: space});
  check('...with no extra new tab', inSpace.length === 2, `${inSpace.length} tab(s)`);
  return lines;
}

async function run(action = 'test15') {
  const result = {action};
  try {
    if (action === 'seed6') {
      result.seed = await seed6();
    } else if (action === 'seed11') {
      result.seed = await seed11();
    } else if (action === 'splitUngroup') {
      result.lines = await splitUngroup();
      result.lines.forEach(l => console.log(l));
    } else {
      result.lines = await test15();
      result.lines.forEach(l => console.log(l));
    }
  } catch (e) {
    result.error = String(e);
  }
  await chrome.storage.local.set({result});
  console.log(`koan-spaces-ext ${action} done`, JSON.stringify(result));
  return result;
}

chrome.action.onClicked.addListener(() => run('test15'));
chrome.runtime.onMessage.addListener(m => { if (typeof m === 'string') run(m); });
// So `run('...')` works in the service worker console.
globalThis.run = run;
