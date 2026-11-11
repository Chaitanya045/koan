// End-to-end test of dragging tabs across Spaces (#224, SD0 in
// scripts/mac/steps/space_drag.md, the maintainer's high-risk step): one
// tab, then a 3-tab selection, dragged to another Space over its button in
// the bottom bar and by holding at the sidebar's edge, then dropped; and a
// drag ended with Esc at the edge. Pass: Koan alive, nothing fatal in its log
// (KoanSpaceDrag::SwitchWithDraggedTabs moves tabs while TabDragController
// is active), the Space that shows is the one dragged to, and the dragged
// tabs are in it and no longer in the Space they came from.
//
// The drag is a held-button drag of the real pointer (ptr down / dragto /
// up) when suite.sh allows real input. Without it the events go to the
// window's root view through UI DevTools, which routes them like real
// input (the sidebar's pre-target handler, the tab's drag controller); a
// probe checks first that such a drag reaches the drag controller at all,
// and the multi-select (Cmd+click) and Esc legs, which need AppKit's
// events, are skipped. Run by scripts/mac/suite.sh.
import { browserCall, check, finish, pages, sleep, viewTree, waitFor } from './lib/koan.mjs';
import { alive, calibrate, createSpace, fatalInLog, ptr, realInput, realInputWhyNot, realKey,
         rootMouse, rootOf, skipped, spaceButtons, startServer, tabViewTitled } from './lib/mac.mjs';

const server = await startServer();
const U = t => server.url(`/p?t=${encodeURIComponent(t)}`);

const titles = ['Drag one', 'Drag two', 'Drag three', 'Drag four', 'Drag five'];
for (const t of titles) {
  await browserCall('Target.createTarget', { url: U(t), background: false });
  await sleep(500);
}
await sleep(1500);
const anyPage = async () => (await pages()).find(p => p.url.startsWith(server.url('/')));
let vs = await viewTree();
const first = spaceButtons(vs).find(b => b.active)?.name ?? '';
check('Space 1 to start with', true, first || '(no bottom bar yet: one Space)');
check('Create Space "Two"', await createSpace(await anyPage(), 'Two'));
check('Create Space "Three"', await createSpace(await anyPage(), 'Three'));
vs = await viewTree();
const names = spaceButtons(vs).map(b => b.name);
check('Three Spaces in the bottom bar', names.length === 3, JSON.stringify(names));
const [S1, S2, S3] = names;

const active = v => spaceButtons(v).find(b => b.active)?.name;
// Shows Space `name` with its button (a plain click).
async function show(name) {
  const v = await viewTree();
  const b = spaceButtons(v).find(x => x.name === name);
  if (!b) return false;
  const root = rootOf({ views: v.filter(x => x.widget === b.view.widget) });
  await rootMouse(root, ['mousePressed', 'mouseReleased'], b.view.ax + b.view.w / 2,
                  b.view.ay + b.view.h / 2);
  return waitFor(async () => active(await viewTree()) === name, 3000);
}
const shownTitles = v => v.filter(x => x.cls === 'TabTitle' && x.shown).map(x => x.props.Text);

// Real pointer: where the root view is on screen.
let origin = null;
if (realInput()) {
  origin = await calibrate(await anyPage(), await viewTree());
  if (!origin) skipped('real pointer', 'calibration failed; using synthetic drags');
}
const real = () => realInput() && origin;

// A drag from the tab titled `title` to `to` (root view coordinates), held
// there `holdMs` with a 1 px wiggle, then dropped (or ended with Esc).
async function drag(title, to, { holdMs = 0, esc = false } = {}) {
  const v = await viewTree();
  const tab = tabViewTitled(v, title);
  if (!tab) return `no tab "${title}"`;
  const root = rootOf({ views: v.filter(x => x.widget === tab.widget) });
  const from = { x: tab.ax + Math.min(40, tab.w / 2), y: tab.ay + tab.h / 2 };
  if (real()) {
    const X = p => Math.round(origin.x + p.x), Y = p => Math.round(origin.y + p.y);
    await ptr('down', X(from), Y(from));
    await sleep(150);
    await ptr('dragto', X(from) + 6, Y(from) + 4, 120);
    await ptr('dragto', X(to), Y(to), 450);
    for (let t = 0; t < holdMs; t += 100) {
      await ptr('dragto', X(to) + (t / 100) % 2, Y(to), 16);
      await sleep(84);
    }
    if (esc) await realKey('Escape', '', 300);
    await ptr('up', X(to), Y(to));
  } else {
    await rootMouse(root, ['mousePressed'], from.x, from.y);
    for (let i = 1; i <= 12; i++) {
      const f = i / 12;
      await rootMouse(root, ['mouseDragged'], from.x + (to.x - from.x) * f, from.y + (to.y - from.y) * f);
      await sleep(30);
    }
    for (let t = 0; t < holdMs; t += 100) {
      await rootMouse(root, ['mouseDragged'], to.x + (t / 100) % 2, to.y);
      await sleep(100);
    }
    await rootMouse(root, ['mouseReleased'], to.x, to.y);
  }
  await sleep(1500);
  return null;
}
const buttonPoint = (v, name) => {
  const b = spaceButtons(v).find(x => x.name === name)?.view;
  return b && { x: b.ax + b.w / 2, y: b.ay + b.h / 2 };
};
// Just inside the sidebar's right (or left) edge, over the tab list.
const edgePoint = (v, side) => {
  const r = v.find(x => x.cls === 'VerticalTabStripRegionView' && x.shown);
  return r && { x: side === 'right' ? r.ax + r.w - 6 : r.ax + 6, y: r.ay + r.h * 0.6 };
};

// A drag case: `tabs` dragged from `from` (the first is pressed), then
// checks the Space that shows and where the tabs are.
async function dragCase(name, { from, tabs, target, expect, holdMs, esc }) {
  await show(from);
  let v = await viewTree();
  const to = target(v);
  if (!to) {
    check(`${name}: the drop point is there`, false);
    return;
  }
  const why = await drag(tabs[0], to, { holdMs, esc });
  v = await viewTree();
  const now = active(v);
  const here = shownTitles(v);
  check(`${name}: Koan is alive, nothing fatal in the log`, (await alive()) && !fatalInLog(),
        fatalInLog() || why || '');
  check(`${name}: ${expect} shows`, now === expect, `showing ${now}`);
  check(`${name}: the tab${tabs.length > 1 ? 's are' : ' is'} in it`,
        tabs.every(t => here.includes(t)), `here: ${here.join(', ')}`);
  if (now !== from) {
    await show(from);
    const there = shownTitles(await viewTree());
    check(`${name}: ...and no longer in ${from}`, tabs.every(t => !there.includes(t)),
          `${from}: ${there.join(', ')}`);
  }
}

// Probe (synthetic only): a short drag down the list must reorder, or the
// synthetic events don't reach the drag controller on this build and the
// drag legs can't say anything.
let canDrag = true;
if (!real()) {
  await show(S1);
  let v = await viewTree();
  const order = () => shownTitles(v).filter(t => titles.includes(t));
  const before = order();
  const last = tabViewTitled(v, before.at(-1));
  if (last) await drag(before[0], { x: last.ax + 40, y: last.ay + last.h - 2 });
  v = await viewTree();
  const after = order();
  canDrag = JSON.stringify(before) !== JSON.stringify(after);
  if (!canDrag) {
    skipped('every drag leg', `a synthetic drag did not reorder the tabs (${before.join(',')}); ` +
            `${realInputWhyNot()}`);
  }
}

if (canDrag) {
  await dragCase('One tab over Space "Two"\'s button', {
    from: S1, tabs: ['Drag one'], target: v => buttonPoint(v, S2), expect: S2 });
  await dragCase('One tab held at the right edge', {
    from: S1, tabs: ['Drag two'], target: v => edgePoint(v, 'right'), expect: S2, holdMs: 750 });
  await dragCase('One tab held at the left edge (wraps or stops)', {
    from: S2, tabs: ['Drag two'], target: v => edgePoint(v, 'left'), expect: S1, holdMs: 750 });

  // A 3-tab selection: a click on the first, Cmd+click on the other two
  // (real pointer only).
  if (real()) {
    const sel = ['Drag three', 'Drag four', 'Drag five'];
    const select = async () => {
      const v = await viewTree();
      for (const [i, t] of sel.entries()) {
        const tab = tabViewTitled(v, t);
        if (!tab) return false;
        await ptr(i === 0 ? 'click' : 'cmdclick', Math.round(origin.x + tab.ax + 40),
                  Math.round(origin.y + tab.ay + tab.h / 2));
        await sleep(400);
      }
      return true;
    };
    await show(S1);
    check('Three tabs selected in Space 1', await select());
    await dragCase('Three selected tabs over Space "Three"\'s button', {
      from: S1, tabs: sel, target: w => buttonPoint(w, S3), expect: S3 });
    await show(S3);
    check('The three selected again, in Space 3', await select());
    await dragCase('Three selected tabs held at the left edge', {
      from: S3, tabs: sel, target: w => edgePoint(w, 'left'), holdMs: 750, expect: S2 });
    // Esc at the edge after the switch: the drag ends and the tab stays in
    // the Space it was carried to (SD7). No crash is the point.
    await dragCase('Esc mid-drag at the edge', {
      from: S2, tabs: ['Drag three'], target: w => edgePoint(w, 'right'), holdMs: 750,
      esc: true, expect: S3 });
  } else {
    skipped('3-tab selection and Esc mid-drag', `need the real pointer and keyboard: ${realInputWhyNot()}`);
  }
}

check('At the end: Koan is alive', await alive());
check('...nothing fatal in the log', !fatalInLog(), fatalInLog());
server.close();
finish();
