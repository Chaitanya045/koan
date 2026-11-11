// Video flow: tabs becoming Essentials (and one going back), then compact
// mode on and off, while record.sh records the window (see ../README.md).
// It re-checks what the first recording (2026-11-11) caught:
//
// * make-essential: the tile appeared at once while the tab's row was still
//   in the Space's list (about 100 ms), and the list jumped down a row in the
//   same frame. Zen: the row leaves as the tile comes in, the Space slides.
// * reveal-sidebar (compact on): no separator and no Clear between the Space
//   and "+ New Tab". Zen shows them.
// * compact-off: for a frame the page showed through at the sidebar's edge
//   as the sidebar came back.
//
// Per change it counts, right away and 1 s later, the model's Essentials
// (chrome.tabs, through the e2e test_helper extension) and the sidebar's rows
// for the tab: `views` (shown TabViews with its URL: 1 once the transition is
// over; 2 means its old row stayed), `inGrid` (is that view a tile), and
// whether the Space header is below the grid. The frames in between are for
// the contact sheets (frames.sh --crop sidebar).
//
// Needs what clear.mjs needs: the Koan that launch.sh started (its
// koan.env), the user away (usergate.sh >= 600 s) and the screen granted.
//
//   set -a; . <rec dir>/koan.env; set +a
//   MARKS=<rec dir>/essentials.marks.json node scripts/mac/video/flows/essentials.mjs
import { ACCEL, browserCall, key, sleep, viewTree, waitFor } from '../../../e2e/lib/koan.mjs';
import { askTestHelper, windowOf } from '../../../e2e/lib/mac.mjs';
import { activate, clearButtonOf, closeTabs, compactOn, findPage, finish, mark, modelTabs, out,
         pointer, save, setRound, setup, sidebarRows, startFlow, startPages } from './common.mjs';

const VK_S = 83;
const MARKS = startFlow('essentials');
const { origin, pageUrl } = await startPages();
mark('start', { origin });
const anchorUrl = `${origin}/p?t=Anchor&b=%23ffffff&anchor=A`;
const anchor = await setup(anchorUrl);

// The window's tabs, and the sidebar's views of the tab showing `url`.
async function snapshot(label, url) {
  const tabs = (await modelTabs(anchor)) ?? [];
  const vs = await viewTree();
  const rows = sidebarRows(vs).filter(v => (v.props.Url ?? '') === url);
  const grid = vs.find(v => v.cls === 'PinnedTabContainerView' && v.shown);
  const header = vs.find(v => v.cls === 'KoanSpaceHeaderView' && v.shown);
  const s = {
    label,
    essentials: tabs.filter(t => t.pinned).length,
    pinned: tabs.find(t => t.url === url)?.pinned ?? null,
    views: rows.length,
    inGrid: rows.map(v => v.grid),
    gridH: grid?.h ?? 0,
    headerBelowGrid: !grid || !header || header.y >= grid.y + grid.h,
    clearShown: !!clearButtonOf(vs),
    compact: compactOn(vs),
  };
  mark(`count-${label}`, s);
  return s;
}

// Opens a tab on `url` (foreground or not) and waits for it to load.
async function openTab(url, foreground) {
  await browserCall('Target.createTarget', { url, background: !foreground });
  const p = await waitFor(() => findPage(x => x.url === url), 5000);
  await sleep(800);
  return p;
}

// Makes the tab on `url` an Essential (`action` 'pin-tab', test_helper pins
// it the Chrome way, which Koan makes an Essential, as in the recording) or
// takes it out ('unpin-tab': back into the Space), and counts.
async function change(name, url, action) {
  const page = await findPage(x => x.url === url);
  const rec = { round: out.rounds.length + 1, name, url, action };
  setRound(rec.round);
  if (!page) {
    rec.skipped = 'no page';
    out.rounds.push(rec);
    return rec;
  }
  mark(action === 'pin-tab' ? 'make-essential' : 'remove-essential', { name }, true);
  await askTestHelper(page, action, 0);
  rec.after = [await snapshot('after', url)];
  await sleep(1000);
  rec.after.push(await snapshot('after-1s', url));
  const last = rec.after.at(-1);
  // Over: one view of the tab, a tile exactly when it is pinned.
  rec.ok = last.views === 1 && last.inGrid[0] === (action === 'pin-tab') && last.headerBelowGrid;
  mark('round-result', { ok: rec.ok });
  out.rounds.push(rec);
  save();
  return rec;
}

// --- From the anchor alone.
{
  const tabs = (await modelTabs(anchor)) ?? [];
  await closeTabs(anchor, tabs.filter(t => !t.url.endsWith('anchor=A')).map(t => t.id));
}
const urls = [];
for (let i = 0; i < 7; i++) {
  urls.push(pageUrl(`E ${i + 1}`, i));
  await openTab(urls[i], i === 6);
}
await sleep(1000);

// The first Essential (the recording's case: the grid was empty), the
// selected tab, then one beside it, then until the fifth starts a second row
// (Zen's four per row), one in the background, and one back to the Space.
await change('first (selected)', urls[6], 'pin-tab');
await activate(await findPage(x => x.url === urls[5]));
await change('second, same row (selected)', urls[5], 'pin-tab');
await change('third (background)', urls[4], 'pin-tab');
await change('fourth (background)', urls[3], 'pin-tab');
await change('fifth, a new row (background)', urls[2], 'pin-tab');
await change('back to the Space (row goes)', urls[2], 'unpin-tab');
await change('the last one out', urls[3], 'unpin-tab');

// Compact mode with Essentials and a listed tab: the reveal shows Clear, and
// turning it off brings the sidebar back without the page showing through.
setRound(out.rounds.length + 1);
{
  const rec = { round: out.rounds.length + 1, name: 'compact' };
  await activate(anchor);
  mark('compact-on', {}, true);
  await key(anchor, VK_S, ACCEL, 's');
  await sleep(1200);
  const w = (await windowOf(anchor.id))?.bounds;
  await pointer('move', Math.round(w.left + w.width * 0.7), Math.round(w.top + w.height / 2), 200);
  await sleep(800);
  rec.hidden = await snapshot('compact-hidden', urls[0]);
  mark('reveal-sidebar', {}, true);
  await pointer('move', w.left + 2, Math.round(w.top + w.height / 2), 250);
  await sleep(900);
  rec.revealed = await snapshot('compact-revealed', urls[0]);
  await pointer('move', Math.round(w.left + w.width * 0.7), Math.round(w.top + w.height / 2), 200);
  await sleep(800);
  mark('compact-off', {}, true);
  await key(anchor, VK_S, ACCEL, 's');
  await sleep(1500);
  rec.off = await snapshot('compact-off', urls[0]);
  rec.ok = rec.revealed.compact && rec.revealed.clearShown && !rec.off.compact && rec.off.clearShown;
  mark('round-result', { ok: rec.ok });
  out.rounds.push(rec);
  save();
}

// --- Summary: one line per round.
mark('end');
console.log('\nround  change                              ok     views  essentials');
for (const r of out.rounds) {
  const last = r.after?.at(-1) ?? r.off;
  console.log(`${String(r.round).padStart(5)}  ${r.name.padEnd(35)} ${String(r.ok ?? r.skipped).padEnd(6)} ` +
              `${String(last?.views ?? '-').padStart(5)}  ${last?.essentials ?? '-'}`);
}
console.log(`marks: ${MARKS}`);
finish();
process.exit(0);
