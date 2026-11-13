// End-to-end test of Zen's split pane header (#373): with two pages split
// side by side, the pointer over a pane shows that pane's header (grip and
// unsplit button), and only that one; off the panes none shows. The unsplit
// button takes the pane's tab out of the split and selects it. Dragging the
// grip shows the rearrange view; dropped on the other pane's middle the panes
// swap, on its bottom edge they stack; Esc during the drag drops nothing. In
// the page's own fullscreen no header shows.
//
//   scripts/e2e/run.sh split_header          (Linux: the split only)
//   scripts/mac/suite.sh split_header        (macOS)
//
// The hover and the drag need the real pointer (the header watches the
// window's own mouse events, which CDP's and UI DevTools' synthetic events
// don't go through): those checks run on the Mac bench with real input
// (KOAN_INPUT=real) and are reported as skipped otherwise. A pane's order is
// read from the sidebar's split row (its titles left to right), which
// follows the split's tab order.
import { ACCEL, ALT, check, evalIn, finish, front, key, onPage, pages, sleep,
         viewTree, waitFor, browserCall } from './lib/koan.mjs';
import { calibrate, hold, makeKey, ptr, realClick, realInput, realInputWhyNot,
         realKey, skipped, startServer } from './lib/mac.mjs';

const server = await startServer();
const shown = (vs, cls) => vs.filter(v => v.cls === cls && v.shown);

// The panes of the window's split (its two shown contents containers), left
// to right, then top to bottom.
const panes = vs => shown(vs, 'ContentsContainerView').filter(v => v.w > 100 && v.h > 100)
    .sort((a, b) => a.ax - b.ax || a.ay - b.ay);
// The split row's titles, left to right.
const splitOrder = vs => shown(vs, 'TabTitle').filter(v => v.path.includes('SplitTabView'))
    .sort((a, b) => a.ax - b.ax).map(v => v.props.Text);
const headers = vs => shown(vs, 'KoanSplitHeaderView');
const rearranging = vs => shown(vs, 'KoanSplitRearrangeView').length > 0;

async function open(url, page) {
  await onPage(page, c => c.send('Page.navigate', { url }));
  await waitFor(async () => (await evalIn(page, 'document.readyState')) === 'complete');
}

const pageTitled = async title => (await pages()).find(p => p.title === title);
const visibleTitle = async () => {
  for (const p of await pages()) {
    if (['Alpha', 'Beta'].includes(p.title) &&
        (await evalIn(p, 'document.visibilityState')) === 'visible') {
      return p.title;
    }
  }
  return '';
};

// Alpha in the first tab, Beta in a new tab after it; Alpha selected.
const [first] = await pages();
await open(server.url('/p?t=Alpha'), first);
await browserCall('Target.createTarget', { url: server.url('/p?t=Beta') });
const beta = await waitFor(() => pageTitled('Beta'), 8000);
const alpha = await pageTitled('Alpha');
check('Two pages to split', alpha && beta, (await pages()).map(p => p.title).join(', '));

// The window's offset on screen, from the single page (for the real pointer).
await front(alpha);
const calibration = realInput() ? await calibrate(alpha, await viewTree()) : null;

// Zen's split side by side (Cmd+Option+V): the active tab with the next one.
const split = async page => {
  await front(page);
  await key(page, 86, ACCEL | ALT, 'v');
  return waitFor(async () => panes(await viewTree()).length === 2, 5000);
};
check('Cmd+Option+V splits Alpha with Beta', await split(alpha),
      splitOrder(await viewTree()).join(' | '));
let vs = await viewTree();
check('No header shows before the pointer comes', headers(vs).length === 0,
      `${headers(vs).length} shown`);

const screen = v => ({ x: calibration.x + v.ax, y: calibration.y + v.ay });
const middle = v => ({ x: screen(v).x + v.w / 2, y: screen(v).y + v.h / 2 });
// The header's buttons: 54 x 23, centred on the pane's top, 5 up into the gap;
// the grip 7 in, the unsplit button 8 after it (koan_split_header.cc).
const grip = v => ({ x: screen(v).x + v.w / 2 - 12, y: screen(v).y + 7 });
const unsplit = v => ({ x: screen(v).x + v.w / 2 + 12, y: screen(v).y + 7 });
const move = async p => {
  await ptr('move', Math.round(p.x), Math.round(p.y), 150);
  await sleep(500);
};
// A grip drag to `to`, `between` run with the button still held.
async function dragGrip(from, to, between = async () => {}) {
  await move(from);
  await hold('down', from.x, from.y);
  await hold('to', from.x + 20, from.y + 20, 150);
  await hold('to', to.x, to.y, 500);
  await sleep(300);
  const result = await between();
  await hold('up', to.x, to.y);
  await sleep(800);
  return result;
}

if (!realInput() || !calibration) {
  skipped('Hover, unsplit, rearrange and fullscreen checks',
          realInput() ? 'could not find the window on screen' : realInputWhyNot());
} else {
  await makeKey(alpha);
  let [left, right] = panes(await viewTree());

  // Zen's .browserSidebarContainer:hover: over a pane, its header only.
  await move(middle(left));
  vs = await viewTree();
  check('The pointer over a pane shows one header', headers(vs).length === 1,
        `${headers(vs).length} shown`);
  await move(middle(right));
  vs = await viewTree();
  check('Over the other pane: one header still (the other pane\'s)', headers(vs).length === 1,
        `${headers(vs).length} shown`);
  await move({ x: calibration.x + 20, y: middle(left).y });
  await sleep(300);
  vs = await viewTree();
  check('Off the panes (over the sidebar) no header shows', headers(vs).length === 0,
        `${headers(vs).length} shown`);

  // The unsplit button: the left pane's tab leaves the split, selected.
  const order = splitOrder(await viewTree());
  await move(middle(left));
  await realClick(unsplit(left).x, unsplit(left).y, 1000);
  vs = await viewTree();
  check('The unsplit button separates the split', panes(vs).length < 2 &&
        !shown(vs, 'SplitTabView').length, `${panes(vs).length} pane(s)`);
  check('...and selects the pane\'s tab', (await visibleTitle()) === order[0],
        `selected ${await visibleTitle()}, the left pane was ${order[0]}`);

  // Again, then the grip dropped on the other pane's middle: swapped.
  const leftPage = await pageTitled(order[0]);
  check('Split again', await split(leftPage));
  await makeKey(leftPage);
  [left, right] = panes(await viewTree());
  const before = splitOrder(await viewTree());
  const shownDuring = await dragGrip(grip(left), middle(right),
                                     async () => rearranging(await viewTree()));
  check('Dragging the grip shows the rearrange view', shownDuring);
  vs = await viewTree();
  check('Dropped on the other pane\'s middle, the panes swap',
        splitOrder(vs).join() === [...before].reverse().join(),
        `${before.join(' | ')} -> ${splitOrder(vs).join(' | ')}`);
  check('...and the rearrange view goes', !rearranging(vs));

  // On the other pane's bottom edge: stacked, the dragged pane below.
  [left, right] = panes(await viewTree());
  await dragGrip(grip(left), { x: middle(right).x, y: screen(right).y + right.h - 20 });
  vs = await viewTree();
  const [top, bottom] = panes(vs);
  check('Dropped on the other pane\'s bottom edge, the panes stack',
        top && bottom && Math.abs(top.ax - bottom.ax) < 2 && bottom.ay > top.ay,
        top && bottom ? `(${top.ax},${top.ay}) (${bottom.ax},${bottom.ay})` : 'no split');

  // Esc during the drag: nothing moves.
  if (top && bottom) {
    const stacked = splitOrder(vs);
    await dragGrip(grip(top), middle(bottom), async () => {
      await realKey('Escape');
      return rearranging(await viewTree());
    });
    vs = await viewTree();
    check('Esc during a drag drops nothing', splitOrder(vs).join() === stacked.join() &&
          panes(vs).length === 2 && Math.abs(panes(vs)[0].ax - panes(vs)[1].ax) < 2,
          `${stacked.join(' | ')} -> ${splitOrder(vs).join(' | ')}`);
    check('...and the rearrange view goes', !rearranging(vs));
  }

  // The page's own fullscreen: no header (Zen's inDOMFullscreen).
  const fullscreenPage = await pageTitled(splitOrder(await viewTree())[0]);
  await makeKey(fullscreenPage);
  await evalIn(fullscreenPage, 'document.documentElement.requestFullscreen().then(() => true)',
               true);
  await sleep(1500);
  const full = panes(await viewTree())[0];
  if (full) {
    await move(middle(full));
    await move({ x: middle(full).x + 30, y: middle(full).y });
  }
  vs = await viewTree();
  check('No header in the page\'s fullscreen', headers(vs).length === 0,
        `${headers(vs).length} shown`);
  await evalIn(fullscreenPage, 'document.exitFullscreen().then(() => true)');
  await sleep(1500);
}

server.close();
finish();
