// Video flow: pinning and unpinning by dragging with the real pointer
// (held-button drags through hold.m), while record.sh records the window (see
// ../README.md). Zen's rule (ZenPinnedTabManager's
// moveToAnotherTabContainerIfNecessary()): a tab dropped in the Space's
// pinned section (its header, the pinned tabs, the separator) is pinned
// there, one dropped in the normal section (the New Tab row, the normal tabs,
// the empty space below) is a normal tab; an Essential leaves the grid for
// the section it is dropped in, keeping its pinned URL only as a pinned tab.
// It re-checks the user's report (2026-11-12): "drag and drop from essentials
// to normal tabs should also work, also pinning tabs should work".
//
// Rounds (the Space starts with the anchor and 5 tabs, no pinned tabs, no
// Essentials):
//   1 sep-pin      the last normal row up onto the separator (no pinned tabs
//                  yet): pinned, the Space's first pinned tab
//   2 between      the last normal row up over the pinned row's top quarter:
//                  pinned, before it
//   3 reorder      the second pinned row up over the first's top quarter:
//                  both still pinned, swapped
//   4 unpin        the last pinned row down onto the New Tab row: a normal
//                  tab, its pinned URL gone, the first normal one
//   5 ess-normal   an Essential (test_helper pins it) down onto the last
//                  normal row's lower half: a normal tab, last, no pinned URL
//   6 ess-top      an Essential down onto the New Tab row, right under the
//                  separator: a normal tab, the first one (the old bug made
//                  it a pinned tab: the same place as the pinned tabs' end)
//   7 ess-pinned   an Essential down onto the separator: a pinned tab, last
//                  of them, its pinned URL kept
//   8 to-grid      a pinned row up over the Essentials grid: an Essential
// While held, `over` records how far the separator moved since the pick-up
// (Zen's separator moves past a row that would drop on its other side: down
// for one going into the pinned section, up for one leaving it) and, for an
// Essential, the drop line; `after` (1.5 s after the release) the tab's
// kind, its pinned URL and its place among the Space's rows, and the
// windows. `ok` is the round's verdict.
//
// Needs what essentials_drag.mjs needs (launch.sh's koan.env, the user away
// for 600 s, the screen granted, `hold` in $KOAN_MAC_TOOLS).
//
//   set -a; . <rec dir>/koan.env; set +a
//   MARKS=<rec dir>/pin_drag.marks.json node scripts/mac/video/flows/pin_drag.mjs
import { browserCall, sleep, viewTree, waitFor } from '../../../e2e/lib/koan.mjs';
import { askTestHelper, windowOf, windows } from '../../../e2e/lib/mac.mjs';
import { drag, findPage, finish, mark, modelTabs, out, save, screenRect, setRound, setup,
         sidebarRows, startFlow, startPages, stop } from './common.mjs';

const MARKS = startFlow('pin_drag');
const { origin, pageUrl } = await startPages();
mark('start', { origin });
const anchorUrl = `${origin}/p?t=Anchor&b=%23ffffff&anchor=A`;
const anchor = await setup(anchorUrl);

const shown = (vs, cls) => vs.find(v => v.cls === cls && v.shown && v.w > 0 && v.h > 0);
// Where `view` is on screen (points), its middle in `mid`.
async function at(view) {
  let r = await screenRect(view, anchor);
  if (!r) {
    const w = (await windowOf(anchor.id))?.bounds;
    r = { x: w.left + view.ax, y: w.top + view.ay, w: view.w, h: view.h, via: 'window' };
  }
  return { ...r, mid: { x: r.x + r.w / 2, y: r.y + r.h / 2 } };
}

// The Space's rows top to bottom (not the anchor's, not the Essentials),
// each { url, pinned, pinnedUrl, essential, tabView }, and the separator and
// New Tab rows.
function space(vs) {
  const rows = sidebarRows(vs).filter(r => !r.grid && !(r.props.Url ?? '').endsWith('anchor=A'))
      .map(r => ({ url: r.props.Url, pinned: r.props.PinnedInSpace === 'true',
                   pinnedUrl: r.props.PinnedUrl ?? '', tabView: r.tabView }))
      .sort((a, b) => a.tabView.ay - b.tabView.ay);
  const tiles = sidebarRows(vs).filter(r => r.grid).map(r => ({ url: r.props.Url, tabView: r.tabView }));
  return { rows, tiles, separator: shown(vs, 'KoanClearTabsRow'), newTab: shown(vs, 'KoanNewTabRow'),
           header: shown(vs, 'KoanSpaceHeaderView'),
           grid: vs.find(v => v.cls === 'PinnedTabContainerView' && v.shown && v.h > 0) };
}

// The separator's top (the New Tab row's without it).
const separatorTop = s => s.separator?.ay ?? s.newTab?.ay ?? null;
// What the sidebar shows while the tab is held (`pickedUp`: the separator's
// top at the pick-up).
async function over(label, pickedUp) {
  const vs = await viewTree();
  const s = space(vs);
  const indicator = shown(vs, 'KoanTabDropIndicatorView');
  const top = separatorTop(s);
  const r = {
    label,
    separatorShift: top !== null && pickedUp !== null ? top - pickedUp : null,
    indicator: indicator ? { y: indicator.ay, h: indicator.h, w: indicator.w } : null,
    separator: s.separator ? [s.separator.ay, s.separator.h] : null,
    newTab: s.newTab ? [s.newTab.ay, s.newTab.h] : null,
  };
  mark(`count-${label}`, r);
  return r;
}

// The dropped tab after the drop.
async function after(label, url) {
  const vs = await viewTree();
  const s = space(vs);
  const tabs = (await modelTabs(anchor)) ?? [];
  const i = s.rows.findIndex(r => r.url === url);
  const r = {
    label,
    essential: tabs.find(t => t.url === url)?.pinned ?? null,
    pinned: i >= 0 ? s.rows[i].pinned : null,
    hasPinnedUrl: i >= 0 ? !!s.rows[i].pinnedUrl : null,
    index: i,
    pinnedCount: s.rows.filter(x => x.pinned).length,
    // Pinned rows first, then the normal ones.
    sectionsOk: s.rows.findIndex(x => !x.pinned) === -1 ||
        !s.rows.slice(s.rows.findIndex(x => !x.pinned)).some(x => x.pinned),
    order: s.rows.map(x => `${x.pinned ? 'P' : 'N'}:${new URL(x.url).searchParams.get('t')}`),
    windows: (await windows()).length,
  };
  mark(`count-${label}`, r);
  return r;
}

// Picks up the view `pick(s)` (a row's or a tile's TabView), takes it to
// `target(s, from)` (a screen point, worked out after the pick-up), holds
// there, releases, and checks.
async function dragRound(name, pick, target, check) {
  const rec = { round: out.rounds.length + 1, name };
  setRound(rec.round);
  let s = space(await viewTree());
  const picked = pick(s);
  if (!picked) {
    rec.skipped = 'nothing to pick up';
    out.rounds.push(rec);
    save();
    return rec;
  }
  rec.url = picked.url;
  rec.before = await after('before', rec.url);
  const from = (await at(picked.tabView)).mid;
  mark('drag-down', { url: rec.url, from }, true);
  await drag('down', from.x, from.y);
  await sleep(250);
  // Past the drag threshold, so upstream's drag starts.
  await drag('to', from.x + 4, from.y + (from.y > 300 ? -12 : 12), 200);
  await sleep(300);
  s = space(await viewTree());
  const pickedUp = separatorTop(s);
  const to = await target(s, from);
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
  rec.over = await over('over', pickedUp);
  mark('drop', { to }, true);
  await drag('up', to.x, to.y);
  await sleep(1500);
  rec.after = await after('after', rec.url);
  rec.ok = !!check(rec) && rec.after.windows === windows0 && rec.after.sectionsOk;
  mark('round-result', { ok: rec.ok });
  out.rounds.push(rec);
  save();
  return rec;
}

// A point `f` of the way down `view` (screen points), at the pick-up's x.
const down = async (view, f, from) => {
  if (!view) return null;
  const r = await at(view);
  return { x: from.x, y: r.y + r.h * f };
};
const lastNormal = s => s.rows.filter(r => !r.pinned).at(-1);
const lastPinned = s => s.rows.filter(r => r.pinned).at(-1);

// Makes the tab showing `url` an Essential (Chromium's pin, as an extension
// does; Koan keeps its URL as the pinned one).
async function toEssential(url) {
  const page = await findPage(p => p.url === url);
  if (page) await askTestHelper(page, 'pin-tab');
  await sleep(1000);
  mark('to-essential', { url, ok: !!page });
}

// --- Setup: the anchor and 5 tabs, nothing pinned.
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

// 1: onto the separator with no pinned tabs yet.
await dragRound('sep-pin', lastNormal, (s, from) => down(s.separator, 0.5, from),
                r => r.after.pinned === true && r.after.hasPinnedUrl && r.after.index === 0 &&
                     r.over.separatorShift > 10);

// 2: over the pinned row's top quarter.
await dragRound('between', lastNormal, (s, from) => down(s.rows.find(x => x.pinned)?.tabView, 0.25, from),
                r => r.after.pinned === true && r.after.index === 0 && r.after.pinnedCount === 2);

// 3: the second pinned row over the first's top quarter.
await dragRound('reorder', s => s.rows.filter(r => r.pinned)[1],
                (s, from) => down(s.rows.find(x => x.pinned)?.tabView, 0.25, from),
                r => r.after.pinned === true && r.after.index === 0 && r.after.pinnedCount === 2);

// 4: the last pinned row onto the New Tab row.
await dragRound('unpin', lastPinned, (s, from) => down(s.newTab, 0.5, from),
                r => r.after.pinned === false && !r.after.hasPinnedUrl && r.after.essential === false &&
                     r.after.index === r.after.pinnedCount && r.over.separatorShift < -10);

// 5: an Essential onto the last normal row's lower half.
await toEssential(urls[0]);
await dragRound('ess-normal', s => s.tiles[0], (s, from) => down(lastNormal(s)?.tabView, 0.75, from),
                r => r.after.essential === false && r.after.pinned === false && !r.after.hasPinnedUrl &&
                     r.after.index === r.after.order.length - 1 && !!r.over.indicator);

// 6: an Essential onto the New Tab row, right under the separator.
await toEssential(urls[1]);
await dragRound('ess-top', s => s.tiles[0], (s, from) => down(s.newTab, 0.5, from),
                r => r.after.essential === false && r.after.pinned === false && !r.after.hasPinnedUrl &&
                     r.after.index === r.after.pinnedCount && !!r.over.indicator);

// 7: an Essential onto the separator.
await toEssential(urls[2]);
await dragRound('ess-pinned', s => s.tiles[0], (s, from) => down(s.separator, 0.5, from),
                r => r.after.essential === false && r.after.pinned === true && r.after.hasPinnedUrl &&
                     r.after.index === r.after.pinnedCount - 1 && !!r.over.indicator);

// 8: a pinned row up over the Essentials grid (empty now: the promo, over
// the Space header's top half).
await dragRound('to-grid', lastPinned, async (s, from) => {
  if (s.grid) return down(s.grid, 0.5, from);
  const h = s.header && await at(s.header);
  return h ? { x: from.x, y: h.y + h.h * 0.25 } : null;
}, r => r.after.essential === true && r.after.hasPinnedUrl !== false);

// --- Summary: one line per round.
mark('end');
console.log('\nround  drag        ok     sep-shift  line  kind-after  index  order');
for (const r of out.rounds) {
  const kind = r.after ? (r.after.essential ? 'essential' : r.after.pinned ? 'pinned' : 'normal') : '-';
  console.log(`${String(r.round).padStart(5)}  ${r.name.padEnd(10)}  ${String(r.ok ?? r.skipped).padEnd(6)} ` +
              `${String(r.over?.separatorShift ?? '-').padEnd(10)} ${String(!!r.over?.indicator).padEnd(5)} ` +
              `${kind.padEnd(11)} ${String(r.after?.index ?? '-').padEnd(6)} ${(r.after?.order ?? []).join(' ')}`);
}
console.log(out.rounds.every(r => r.ok) ? 'PASS' : 'FAIL');
console.log(`marks: ${MARKS}`);
finish();
process.exit(out.rounds.every(r => r.ok) ? 0 : 1);
