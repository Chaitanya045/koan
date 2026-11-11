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
import { ACCEL, ALT, CTRL, SHIFT, browserCall, key, sleep, viewTree, waitFor } from '../../../e2e/lib/koan.mjs';
import { askTestHelper, realKey, windowOf } from '../../../e2e/lib/mac.mjs';
import { activate, clearButtonOf, clickAt, closeTabs, compactOn, findPage, finish, mark, modelTabs,
         out, pointer, save, screenRect, setRound, setup, sidebarRows, startFlow,
         startPages } from './common.mjs';

const VK_S = 83, VK_D = 68, VK_N = 78;
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
  // The grid's tiles: Zen keeps them a grid (at most four across), also in
  // compact mode's revealed sidebar.
  const tiles = vs.filter(v => v.cls === 'TabView' && v.shown && v.w > 0 &&
                               v.path.includes('PinnedTabContainerView'));
  const s = {
    label,
    essentials: tabs.filter(t => t.pinned).length,
    pinned: tabs.find(t => t.url === url)?.pinned ?? null,
    views: rows.length,
    inGrid: rows.map(v => v.grid),
    gridH: grid?.h ?? 0,
    headerBelowGrid: !grid || !header || header.y >= grid.y + grid.h,
    tiles: tiles.length,
    tileRows: new Set(tiles.map(t => t.y)).size,
    tileMaxW: Math.max(0, ...tiles.map(t => t.w)),
    gridW: grid?.w ?? 0,
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
// takes it out ('unpin-tab': back into the Space, as an extension or a drag
// does; 'toggle-pin': the user's Toggle Pin, Cmd+Shift+D, on the selected
// Essential, which is Remove from Essentials), and counts.
async function change(name, url, action) {
  const page = await findPage(x => x.url === url);
  const rec = { round: out.rounds.length + 1, name, url, action };
  setRound(rec.round);
  if (!page) {
    rec.skipped = 'no page';
    out.rounds.push(rec);
    return rec;
  }
  if (action === 'toggle-pin') {
    await activate(page);
    await sleep(600);
  }
  mark(action === 'pin-tab' ? 'make-essential' : 'remove-essential', { name, action }, true);
  if (action === 'toggle-pin') {
    await key(page, VK_D, ACCEL | SHIFT, 'd');
  } else {
    await askTestHelper(page, action, 0);
  }
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
await change('Remove from Essentials (Toggle Pin)', urls[6], 'toggle-pin');

// Compact mode with Essentials and listed tabs: the revealed sidebar keeps
// the Essentials a grid of tiles and shows Clear; turning compact mode off
// with the sidebar out leaves it where it is, and with it hidden slides it in
// once, over no page.
setRound(out.rounds.length + 1);
let compactRec = null;
{
  const rec = compactRec = { round: out.rounds.length + 1, name: 'compact' };
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
  await sleep(300);
  await pointer('move', w.left + 60, Math.round(w.top + w.height / 2), 150);
  await sleep(900);
  rec.revealed = await snapshot('compact-revealed', urls[0]);
  // Off with the sidebar out (the pointer on it): Zen leaves it there.
  mark('compact-off-revealed', {}, true);
  await key(anchor, VK_S, ACCEL, 's');
  await sleep(1500);
  rec.offRevealed = await snapshot('compact-off-revealed', urls[0]);
  // On again, the sidebar hidden, then off: Zen slides it in once.
  mark('compact-on', {}, true);
  await key(anchor, VK_S, ACCEL, 's');
  await sleep(600);
  await pointer('move', Math.round(w.left + w.width * 0.7), Math.round(w.top + w.height / 2), 200);
  await sleep(1200);
  mark('compact-off', {}, true);
  await key(anchor, VK_S, ACCEL, 's');
  await sleep(1500);
  rec.off = await snapshot('compact-off', urls[0]);
  const grid = s => s.tiles === 0 || (s.tileRows === 1 && s.tileMaxW * 2 < s.gridW);
  rec.ok = rec.revealed.compact && rec.revealed.clearShown && grid(rec.revealed) &&
           !rec.offRevealed.compact && !rec.off.compact && rec.off.clearShown && grid(rec.off);
  mark('round-result', { ok: rec.ok });
  out.rounds.push(rec);
  save();
}

// The lead on #295: a real Clear click in compact mode's reveal, compact mode
// off, then Ctrl+Alt+N must still open the new-Space form.
setRound(out.rounds.length + 1);
{
  const rec = { round: out.rounds.length + 1, name: 'clear in reveal, then Ctrl+Alt+N' };
  await activate(anchor);
  await key(anchor, VK_S, ACCEL, 's');
  await sleep(1200);
  const w = (await windowOf(anchor.id))?.bounds;
  mark('reveal-sidebar', {}, true);
  await pointer('move', w.left + 2, Math.round(w.top + w.height / 2), 250);
  await sleep(900);
  let vs = await viewTree();
  const button = clearButtonOf(vs);
  const r = button && await screenRect(button, anchor);
  if (r) {
    await pointer('move', Math.round(r.x + r.w / 2), Math.round(r.y + r.h / 2), 250);
    await sleep(500);
    mark('clear-click', {}, true);
    await clickAt(Math.round(r.x + r.w / 2), Math.round(r.y + r.h / 2));
    await sleep(1000);
  } else {
    mark('no-clear-button');
  }
  await pointer('move', Math.round(w.left + w.width * 0.7), Math.round(w.top + w.height / 2), 200);
  await sleep(800);
  mark('compact-off', {}, true);
  await key(anchor, VK_S, ACCEL, 's');
  await sleep(1500);
  rec.forms = [];
  for (let i = 0; i < 3; i++) {
    mark('new-space-key', { try: i + 1 }, true);
    await key(anchor, VK_N, CTRL | ALT, 'n');
    await sleep(1200);
    vs = await viewTree();
    const shown = vs.some(v => v.cls === 'KoanSpaceCreationView' && v.shown);
    rec.forms.push(shown);
    mark('count-new-space-form', { shown, any: vs.some(v => v.cls === 'KoanSpaceCreationView') });
    // Closed again with a real Escape (the form's name field has the focus;
    // CDP's keys go to the page), so that each try opens it anew.
    if (shown) {
      await realKey('Escape', '', 800);
      vs = await viewTree();
      mark('count-new-space-form-closed',
           { closed: !vs.some(v => v.cls === 'KoanSpaceCreationView' && v.shown) });
    }
  }
  rec.ok = rec.forms.every(Boolean);
  mark('round-result', { ok: rec.ok });
  out.rounds.push(rec);
  save();
}

// --- Summary: one line per round.
mark('end');
console.log('\nround  change                              ok     views  essentials');
for (const r of out.rounds) {
  const last = r.after?.at(-1) ?? r.off ?? {};
  console.log(`${String(r.round).padStart(5)}  ${r.name.padEnd(35)} ${String(r.ok ?? r.skipped).padEnd(6)} ` +
              `${String(last?.views ?? '-').padStart(5)}  ${last?.essentials ?? '-'}`);
}
console.log(`marks: ${MARKS}`);
finish();
process.exit(0);
