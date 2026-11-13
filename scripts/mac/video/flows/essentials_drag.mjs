// Video flow: Space tabs dragged into the Essentials with the real pointer
// (held-button drags through hold.m), while record.sh records the window (see
// ../README.md). It re-checks the bug the user hit (2026-11-12, "nothing
// lights up"): with no Essentials the grid is 0 px tall, and the promo only
// came over the top half of the Space name, so a tab dragged straight up
// over the URL pill never became an Essential.
//
// Rounds (each drags the last fully shown row of the Space, never the anchor):
//   1 pill      no Essentials, straight up over the URL pill: the promo shows
//               and is lit, the drop makes the tab the first Essential
//   2 band      back to no Essentials (test_helper unpins it), held over the
//               Space name's top half: the promo, lit; the drop makes it one
//   3 above     with an Essential, straight up over the URL pill, above the
//               grid: Zen's gap (the placeholder tile) before the first tile,
//               the tab the first Essential (Koan's choice: the slot above
//               the grid's first row, as above a Space's first row; #380)
//   4 page      released over the page: no new Essential, no new window, the
//               tab still in the window
// Each round records `over` (while held: promo shown and lit, the gap)
// and `after` (1.5 s after the release: the model's Essentials, the dragged
// tab's pinned state and place, the windows); `ok` is the round's verdict.
//
// Screen points: UI DevTools' box model returns nothing for these views on
// the Mac, so screenRect() falls back to the root view's origin that setup()
// measures (the window has no title bar: the window's origin plus the view's
// ax/ay if that measurement failed too).
//
// Needs what essentials.mjs needs (launch.sh's koan.env, the user away for
// 600 s, the screen granted) plus `hold` in $KOAN_MAC_TOOLS (../README.md).
//
//   set -a; . <rec dir>/koan.env; set +a
//   MARKS=<rec dir>/essentials_drag.marks.json node scripts/mac/video/flows/essentials_drag.mjs
import { browserCall, sleep, viewTree, waitFor } from '../../../e2e/lib/koan.mjs';
import { askTestHelper, windowOf, windows } from '../../../e2e/lib/mac.mjs';
import { drag, findPage, finish, mark, modelTabs, out, regionOf, save, screenRect, setRound, setup,
         sidebarRows, startFlow, startPages, stop } from './common.mjs';

const MARKS = startFlow('essentials_drag');
const { origin, pageUrl } = await startPages();
mark('start', { origin });
const anchorUrl = `${origin}/p?t=Anchor&b=%23ffffff&anchor=A`;
const anchor = await setup(anchorUrl);

const shown = (vs, cls) => vs.find(v => v.cls === cls && v.shown && v.w > 0);
// Where `view` is on screen (points), its middle in `mid`.
async function at(view) {
  let r = await screenRect(view, anchor);
  if (!r) {
    const w = (await windowOf(anchor.id))?.bounds;
    r = { x: w.left + view.ax, y: w.top + view.ay, w: view.w, h: view.h, via: 'window' };
  }
  return { ...r, mid: { x: r.x + r.w / 2, y: r.y + r.h / 2 } };
}

// The row to drag: the last Space row (not the anchor) whose TabView is
// wholly in the visible list, between the Space header and the Space bar
// (the list may scroll).
function pickRow(vs) {
  const header = shown(vs, 'KoanSpaceHeaderView');
  const region = regionOf(vs);
  const bar = shown(vs, 'KoanSpaceBarView');
  if (!header || !region) return null;
  const top = header.ay + header.h;
  const bottom = (bar ? bar.ay : region.ay + region.h) - 4;
  return sidebarRows(vs).filter(r => !r.grid && !(r.props.Url ?? '').endsWith('anchor=A') &&
                                     r.tabView.ay >= top && r.tabView.ay + r.tabView.h <= bottom).at(-1) ?? null;
}

// What the sidebar shows while the tab is held.
async function over(label) {
  const vs = await viewTree();
  const promo = shown(vs, 'KoanEssentialsPromoView');
  const indicator = shown(vs, 'KoanTabDropIndicatorView');
  const tiles = vs.filter(v => v.cls === 'TabView' && v.shown && v.w > 0 && v.path.includes('PinnedTabContainerView'))
      .sort((a, b) => a.ay - b.ay || a.ax - b.ax);
  const gap = vs.find(v => v.cls === 'KoanEssentialsGapView' && v.shown && v.w > 0);
  const s = {
    label,
    promo: !!promo,
    promoLit: promo ? String(promo.props.DragOver) === 'true' : false,
    indicator: indicator ? { x: indicator.ax, y: indicator.ay, w: indicator.w, h: indicator.h } : null,
    gap: gap ? { x: gap.ax, y: gap.ay, w: gap.w, h: gap.h } : null,
    // Zen's gap: the placeholder tile takes the first slot, the tiles moved
    // aside (its left in LTR), and no line over the grid.
    gapFirst: !!gap && !!tiles[0] && !indicator && Math.abs(gap.ay - tiles[0].ay) <= 2 &&
        gap.ax < tiles[0].ax,
  };
  mark(`count-${label}`, s);
  return s;
}

// The model and the windows after the drop.
async function after(label, url) {
  const tabs = (await modelTabs(anchor)) ?? [];
  const pinned = tabs.filter(t => t.pinned);
  const s = {
    label,
    essentials: pinned.length,
    tabPresent: tabs.some(t => t.url === url),
    pinned: tabs.find(t => t.url === url)?.pinned ?? null,
    essentialIndex: pinned.findIndex(t => t.url === url),
    windows: (await windows()).length,
  };
  mark(`count-${label}`, s);
  return s;
}

// Picks up the row, takes it to `target(vs, from)` (a screen point, worked
// out after the pick-up, as the promo may move things; `from` is where the
// row was picked up), holds there, releases.
async function dragRound(name, target, check) {
  const rec = { round: out.rounds.length + 1, name };
  setRound(rec.round);
  let vs = await viewTree();
  const row = pickRow(vs);
  if (!row) {
    rec.skipped = 'no visible row';
    out.rounds.push(rec);
    save();
    return rec;
  }
  rec.url = row.props.Url;
  rec.before = await after('before', rec.url);
  const from = (await at(row.tabView)).mid;
  mark('drag-down', { url: rec.url, from }, true);
  await drag('down', from.x, from.y);
  await sleep(250);
  // Past the drag threshold, so upstream's drag starts.
  await drag('to', from.x + 6, from.y - 12, 200);
  await sleep(300);
  vs = await viewTree();
  const to = await target(vs, from);
  if (!to) {
    await drag('up', from.x, from.y);
    rec.skipped = 'no target';
    out.rounds.push(rec);
    save();
    return rec;
  }
  mark('drag-over', { to }, true);
  await drag('to', to.x, to.y, 700);
  await sleep(700);
  rec.over = await over('over');
  mark('drop', { to }, true);
  await drag('up', to.x, to.y);
  await sleep(1500);
  rec.after = await after('after', rec.url);
  rec.ok = check(rec);
  mark('round-result', { ok: rec.ok });
  out.rounds.push(rec);
  save();
  return rec;
}

// Straight up from the pick-up point, over the URL pill's middle.
const overPill = async (vs, from) => {
  const pill = shown(vs, 'KoanUrlPill') ?? shown(vs, 'KoanSidebarNavView');
  return pill ? { x: from.x, y: (await at(pill)).mid.y } : null;
};

// --- Setup: the anchor and 5 tabs, no Essentials.
const urls = [];
for (let i = 0; i < 5; i++) {
  urls.push(pageUrl(`D ${i + 1}`, i));
  await browserCall('Target.createTarget', { url: urls[i], background: i !== 4 });
  await waitFor(() => findPage(p => p.url === urls[i]), 5000);
  await sleep(500);
}
await sleep(1000);
const start = (await modelTabs(anchor)) ?? [];
if (start.some(t => t.pinned)) stop('the profile already has Essentials (launch.sh gives a fresh one)', 2);
const windows0 = (await windows()).length;

// 1: no Essentials, straight up over the URL pill.
await dragRound('pill', overPill, r =>
  r.over.promo && r.over.promoLit && r.after.essentials === 1 && r.after.pinned === true);

// 2: back to none, then held over the Space name's top half.
{
  const pinned = ((await modelTabs(anchor)) ?? []).filter(t => t.pinned);
  for (const t of pinned) {
    const page = await findPage(p => p.url === t.url);
    if (page) await askTestHelper(page, 'unpin-tab');
  }
  await sleep(800);
  mark('reset-essentials', { left: ((await modelTabs(anchor)) ?? []).filter(t => t.pinned).length });
}
await dragRound('band', async (vs, from) => {
  const header = shown(vs, 'KoanSpaceHeaderView');
  if (!header) return null;
  // (The promo, if it already shows, pushed the header down; its top half
  // is where it was before.)
  const promo = shown(vs, 'KoanEssentialsPromoView');
  const h = await at(header);
  return { x: from.x, y: promo ? (await at(promo)).y + 6 : h.y + h.h * 0.25 };
}, r => r.over.promo && r.over.promoLit && r.after.essentials === r.before.essentials + 1 &&
        r.after.pinned === true);

// 3: with an Essential, straight up over the URL pill (above the grid).
await dragRound('above', overPill, r =>
  !r.over.promo && r.over.gapFirst && r.after.essentials === r.before.essentials + 1 &&
  r.after.pinned === true && r.after.essentialIndex === 0);

// 4: released over the page.
await dragRound('page', async vs => {
  const page = vs.filter(v => v.cls === 'ContentsWebView' && v.shown && v.w > 100)
      .sort((a, b) => b.w * b.h - a.w * a.h)[0];
  return page ? (await at(page)).mid : null;
}, r => r.after.essentials === r.before.essentials && r.after.pinned === false && r.after.tabPresent &&
        r.after.windows === windows0);

// --- Summary: one line per round.
mark('end');
console.log('\nround  drag    ok     promo  lit    gap-first   essentials  windows');
for (const r of out.rounds) {
  console.log(`${String(r.round).padStart(5)}  ${r.name.padEnd(6)}  ${String(r.ok ?? r.skipped).padEnd(6)} ` +
              `${String(r.over?.promo ?? '-').padEnd(6)} ${String(r.over?.promoLit ?? '-').padEnd(6)} ` +
              `${String(r.over?.gapFirst ?? '-').padEnd(11)} ` +
              `${r.before?.essentials ?? '-'}->${r.after?.essentials ?? '-'}`.padEnd(11) +
              ` ${r.after?.windows ?? '-'}`);
}
console.log(out.rounds.every(r => r.ok) ? 'PASS' : 'FAIL');
console.log(`marks: ${MARKS}`);
finish();
process.exit(out.rounds.every(r => r.ok) ? 0 : 1);
