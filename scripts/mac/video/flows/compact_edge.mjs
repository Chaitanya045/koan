// Video flow: compact mode toggled and the hidden sidebar peeked in and out,
// several times, in a window and in native fullscreen, while record.sh
// records the whole screen (see ../README.md). edge_check.mjs then samples
// the window's top rows, left edge and bottom rows in every frame and fails
// on any black that isn't the sidebar.
//
// The bug (user, 2026-11-20): "Whenever I'm toggling compact mode I see an
// edge glitching in black at the top", and the same while peeking the hidden
// sidebar at the screen's left edge. Upstream's floating corners beside the
// vertical tab strip (frame colour) were laid out at the strip's layout
// bounds, which Koan's slide doesn't move, and animated on upstream's own
// timeline: a dark notch at the top edge ahead of the sliding sidebar (see
// koan_sidebar_corners.h).
//
// The page is plain white with nothing on it, so any dark pixel outside the
// sidebar during a transition is a glitch. The pointer and the sidebar's own
// columns are left out by the checker (both in the marks).
//
// Needs what essentials.mjs needs: the Koan that launch.sh started (its
// koan.env), the user away (usergate.sh >= 600 s) and the screen granted;
// record the whole screen, as the flow goes fullscreen:
//
//   set -a; . <rec dir>/koan.env; set +a
//   H=scripts/mac/video
//   $H/record.sh start "$REC_DIR/edge.mp4" --rect "$(node $H/window_rect.mjs --screen)"
//   MARKS="$REC_DIR/edge.marks.json" node $H/flows/compact_edge.mjs [rounds]
//   $H/record.sh stop "$REC_DIR/edge.mp4"
//   node $H/edge_check.mjs "$REC_DIR/edge.mp4" "$REC_DIR/edge.marks.json"
//
// [rounds]: how many toggles and peeks per phase (default 4).
import { ACCEL, browserCall, key, sleep, viewTree } from '../../../e2e/lib/koan.mjs';
import { setWindowState, windowOf } from '../../../e2e/lib/mac.mjs';
import { activate, compactOn, finish, mark, out, pointer, regionOf, save, setRound, setup,
         startFlow, startPages, stop } from './common.mjs';

const VK_S = 83;
const ROUNDS = Number(process.argv[2] ?? 4);
startFlow('compact_edge');
const { origin } = await startPages();
mark('start', { origin, rounds: ROUNDS });
// A blank white page: a space for the title draws nothing.
const anchor = await setup(`${origin}/p?t=%20&b=%23ffffff`);

// The window's rect (screen points) and, while compact mode reveals it, the
// sidebar's width: what the checker crops to and leaves out.
const rectOf = async () => {
  const b = (await windowOf(anchor.id))?.bounds;
  return { x: b.left, y: b.top, w: b.width, h: b.height, state: b.windowState };
};
let sidebarW = out.sidebar?.w ?? 260;
const isCompact = async () => compactOn(await viewTree());

// Pointer moves, recorded in the action marks so the checker skips the
// cursor's path.
let pointerAt = null;
async function moveTo(x, y, ms) {
  const from = pointerAt ?? [x, y];
  await pointer('move', Math.round(x), Math.round(y), ms);
  pointerAt = [Math.round(x), Math.round(y)];
  return [from, pointerAt];
}
const away = r => [r.x + r.w * 0.7, r.y + r.h / 2];

// One action: the mark (with the rect, the sidebar's width and the
// pointer's path) before it starts, then `settle` ms for the transition to
// end; the checker compares the frames in between with the ones just before
// the mark and at its end.
async function act(label, fn, settle = 1800) {
  const rect = await rectOf();
  const m = mark(label, { rect, sidebarW, pointer: null }, true);
  m.pointer = (await fn(rect)) ?? null;
  save();
  await sleep(settle);
}

async function phase(name) {
  setRound(out.rounds.length + 1);
  const rec = { round: out.rounds.length + 1, name, toggles: 0, peeks: 0, compactErrors: [] };
  await activate(anchor);
  let r = await rectOf();
  await moveTo(...away(r), 200);
  await sleep(600);
  for (let i = 0; i < ROUNDS; i++) {
    // Compact mode on: the docked sidebar slides away.
    await act('compact-on', () => key(anchor, VK_S, ACCEL, 's'));
    if (!(await isCompact())) rec.compactErrors.push(`round ${i}: not on`);
    rec.toggles++;
    // Peek: the pointer at the left edge, then away again (Zen keeps it out
    // 150 ms after the pointer leaves).
    for (let j = 0; j < 2; j++) {
      r = await rectOf();
      await act('peek-in', rect => moveTo(rect.x + 1, rect.y + rect.h / 2, 150));
      const region = regionOf(await viewTree());
      if (region) sidebarW = Math.max(sidebarW, region.w);
      await act('peek-out', rect => moveTo(...away(rect), 200));
      rec.peeks++;
    }
    // Compact mode off: the page is laid out beside the sidebar, which
    // slides in.
    await act('compact-off', () => key(anchor, VK_S, ACCEL, 's'));
    if (await isCompact()) rec.compactErrors.push(`round ${i}: not off`);
    rec.toggles++;
  }
  rec.ok = rec.compactErrors.length === 0;
  mark('round-result', rec);
  out.rounds.push(rec);
  save();
}

await phase('window');

// Native fullscreen (the green button's): Koan keeps its layout there, the
// page fills the screen in compact mode, and the top floating corner rests
// at 1 (Koan has no fullscreen toolbar), which is where the peek showed it.
const { windowId } = await windowOf(anchor.id);
mark('fullscreen', {}, false);
await setWindowState(windowId, 'fullscreen');
await sleep(3000);
if ((await rectOf()).state !== 'fullscreen') stop('the window did not go fullscreen', 4);
await phase('fullscreen');
await setWindowState(windowId, 'normal');
await sleep(2500);

mark('done', {});
console.log(out.rounds.map(r => `${r.name}: ${r.toggles} toggles, ${r.peeks} peeks` +
                                 (r.ok ? '' : ` (${r.compactErrors.join(', ')})`)).join('\n'));
console.log(`now: node scripts/mac/video/edge_check.mjs <video> ${process.env.MARKS ?? '<marks>'}`);
finish();
process.exit(out.rounds.every(r => r.ok) ? 0 : 1);
