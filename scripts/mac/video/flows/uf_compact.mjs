// User flow: compact mode with no black notch or edge (#399). Test cases
// UF-CMP-* in .agent/notes/user-flows.md. The person toggles compact mode
// with ⌘S (a real key) and peeks the hidden sidebar with the real pointer at
// the window's left edge, twice, over a blank white page. Each step checks
// the state in the view tree and in the pixels (the sidebar's strip with
// and without it). The black-edge check itself reads the recording:
// run_flows.sh runs edge_check.mjs on this flow's video and marks (each
// action mark has the window's rect, the sidebar's width and the pointer's
// path, as compact_edge.mjs's do) and adds its verdict as UF-CMP-5.
import { compactOn, out, regionOf, save } from './common.mjs';
import { begin, diffPixels, end, keyPress, mark, openAnchor, pageById, pointer, recalibrate, sleep, step,
         viewTree, windowBounds } from './uf_lib.mjs';

const ROUNDS = Number(process.argv[2] ?? 2);
const fixtures = await begin('uf_compact', 'Compact mode on and off, and the sidebar peeked, with no black edge', '#399');
const anchor = await openAnchor(fixtures.url('/blank'));
let sidebarW = (regionOf(await viewTree()))?.w ?? 260;
out.sidebar = { w: sidebarW };
save();

let pointerAt = null;
async function moveTo(x, y, ms) {
  const from = pointerAt ?? [x, y];
  await pointer('move', Math.round(x), Math.round(y), ms);
  pointerAt = [Math.round(x), Math.round(y)];
  return [from, pointerAt];
}
const away = b => [b.left + b.width * 0.7, b.top + b.height / 2];
// An action for edge_check.mjs: its mark (rect, sidebar width, the
// pointer's path), then `settle` ms for the transition.
async function act(label, fn, settle = 1800) {
  const b = await windowBounds();
  const m = mark(label, { rect: { x: b.left, y: b.top, w: b.width, h: b.height, state: 'normal' }, sidebarW,
                          pointer: null }, true);
  m.pointer = (await fn(b)) ?? null;
  save();
  await sleep(settle);
}
const strip = b => ({ x: b.left, y: b.top + 60, w: Math.min(sidebarW, 200), h: b.height - 120 });

await recalibrate(anchor);
await moveTo(...away(await windowBounds()), 200);
await sleep(500);
for (let round = 1; round <= ROUNDS; round++) {
  let hidden = null;
  await step(`UF-CMP-1.${round}`, `⌘S: compact mode on, the sidebar slides away (round ${round})`, async t => {
    const docked = await t.shot('docked', { pixels: true });
    await act('compact-on', () => keyPress('s', 'c', 0));
    t.check('compact mode is on (the hot zone spans the window)', compactOn(await viewTree()));
    hidden = await t.shot('compact', { pixels: true });
    const b = await windowBounds();
    if (docked.frame && hidden.frame) {
      const d = diffPixels(docked, hidden, strip(b));
      t.check('the pixels: the sidebar\'s strip changed (the sidebar left)', d.changed / d.n > 0.05,
              `${d.changed} of ${d.n} px changed`);
    } else {
      t.check('screenshots', false, docked.error ?? hidden.error);
    }
  }, { needs: ['real input', 'recording'] });

  for (let peek = 1; peek <= 2; peek++) {
    await step(`UF-CMP-2.${round}.${peek}`, `The pointer at the left edge peeks the sidebar, away hides it`, async t => {
      await act('peek-in', b => moveTo(b.left + 1, b.top + b.height / 2, 150));
      const region = regionOf(await viewTree());
      if (region) sidebarW = Math.max(sidebarW, region.w);
      t.check('the sidebar is out (its region shown)', !!region && region.w > 100, JSON.stringify(region && { w: region.w }));
      const shown = await t.shot('peek-in', { pixels: true });
      await act('peek-out', b => moveTo(...away(b), 200));
      const gone = await t.shot('peek-out', { pixels: true });
      const b = await windowBounds();
      if (hidden?.frame && shown.frame && gone.frame) {
        const inn = diffPixels(hidden, shown, strip(b)), outt = diffPixels(hidden, gone, strip(b));
        t.check('the pixels: peeked, the strip shows the sidebar', inn.changed / inn.n > 0.05, `${inn.changed}/${inn.n}`);
        t.check('the pixels: hidden again, the strip is as before the peek', outt.changed / outt.n < 0.01,
                `${outt.changed}/${outt.n}`);
      } else {
        t.check('screenshots', false, shown.error ?? gone.error ?? 'no compact shot');
      }
    }, { needs: ['real input', 'recording'] });
  }

  await step(`UF-CMP-3.${round}`, '⌘S: compact mode off, the sidebar docked again', async t => {
    await act('compact-off', () => keyPress('s', 'c', 0));
    t.check('compact mode is off', !compactOn(await viewTree()));
    const region = regionOf(await viewTree());
    t.check('the sidebar is docked (shown)', !!region && region.w > 100);
    await t.shot('docked-again');
  }, { needs: ['real input', 'recording'] });
}
await step('UF-CMP-4', 'Koan answers after the toggles', async t => {
  const p = await pageById(anchor.id);
  t.check('the page is still there and visible', !!p);
});

end();
