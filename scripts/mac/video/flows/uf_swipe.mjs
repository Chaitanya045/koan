// User flow: the two-finger swipe between Spaces over the sidebar (#91, the
// direction fixed in #436, the Library's swipe with the sidebar on the right
// #453). Test cases UF-SW-* in .agent/notes/user-flows.md.
// Three Spaces; a trackpad-like gesture (tools/swipe.m: continuous scroll
// events with Began/Changed/Ended phases, posted at the sidebar) the way a
// person's fingers move:
// - right to left brings in the Space on the right (the next one), left to
//   right the one on the left;
// - a short, slow drag springs back;
// - past the last Space, a long swipe brings in the + badge and opens the
//   Create-a-Space form;
// - past the first Space, towards the Library (on the left), the Library;
// - with the sidebar on the right, past the last Space, one swipe right to
//   left opens the Library (the first swipe used to go to the page instead),
//   and one left to right closes it.
// The strip following the fingers is checked in the recording's contact
// sheets (the marks of each step), not here. swipe.m prints how AppKit reads
// its events (isDirectionInvertedFromDevice), so the flow posts the deltas a
// real trackpad gives for that finger direction. A real trackpad pass is
// still scripts/mac/full-flow.md step 7 (both natural scrolling settings).
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { createSpace, setKoanPref } from '../../../e2e/lib/koan.mjs';
import { TOOLS } from './common.mjs';
import { begin, clickView, end, keyPress, openAnchor, parkPointer, pointer, skipFlow, sleep, step, viewRect,
         viewTree, waitFor } from './uf_lib.mjs';

const fixtures = await begin('uf_swipe', 'Swiping between Spaces follows the fingers', '#91, #436');
const SWIPE = path.join(TOOLS ?? '', 'swipe');
if (!TOOLS || !fs.existsSync(SWIPE)) skipFlow(`no swipe tool in ${TOOLS} (build scripts/mac/tools/swipe.m)`);

const anchor = await openAnchor(fixtures.url('/p?t=Space%20one'));
await parkPointer();

async function spaces() {
  const vs = await viewTree();
  const buttons = vs.filter(v => v.cls === 'KoanSpaceButton' && v.shown).sort((a, b) => a.ax - b.ax);
  const header = vs.find(v => v.cls === 'KoanSpaceHeaderView' && v.shown && v.h > 0);
  const library = vs.find(v => v.cls === 'KoanLibraryView');
  return {
    vs, buttons, header,
    active: buttons.findIndex(b => b.props.Active === 'true'),
    name: header?.props.Name ?? '',
    libraryOpen: library?.props.Open === 'true',
    form: vs.some(v => v.shown && v.path?.includes?.('KoanSpaceCreationView')) ||
          vs.some(v => v.shown && v.cls === 'KoanSpaceCreationView'),
    badge: vs.some(v => v.shown && v.cls === 'KoanSpaceAddSwipeView'),
  };
}
const say = s => JSON.stringify({ active: s.active, name: s.name, spaces: s.buttons.length, library: s.libraryOpen,
                                  form: s.form, badge: s.badge });

// Where to swipe: the middle of the sidebar's tab list (below the header).
async function sidebarPoint() {
  const s = await spaces();
  const r = s.header && await viewRect(s.header, s.vs);
  if (!r) return null;
  return [Math.round(r.x + r.w / 2), Math.round(r.y + r.h + 120)];
}

// How AppKit reads swipe.m's events: inverted=1 is natural scrolling (the
// deltas go the fingers' way), 0 the other way.
let inverted = null;
function readInverted() {
  const line = execFileSync(SWIPE, ['0', '0', '-10', '1', '1', '--info'], { encoding: 'utf8', timeout: 5000 });
  const m = line.match(/inverted=(\d)/);
  return m ? m[1] === '1' : null;
}
// A swipe of the fingers by `travel` points (negative: right to left),
// in `steps` events `ms` apart, at the sidebar.
async function fingers(travel, steps = 12, ms = 16) {
  const at = await sidebarPoint();
  if (!at) throw new Error('no sidebar to swipe on');
  await pointer('move', at[0], at[1], 150);  // the gates, and the pointer there
  const dx = inverted ? travel : -travel;
  execFileSync(SWIPE, [String(at[0]), String(at[1]), String(dx), String(steps), String(ms)], { timeout: 20000 });
  await sleep(900);  // the slide (250 ms) and the switch
}

await step('UF-SW-1', 'Three Spaces; the swipe tool and how AppKit reads it', async t => {
  inverted = readInverted();
  t.check('swipe.m: AppKit reads its events as precise, phased scrolls', inverted !== null,
          'no "inverted=" in swipe --info');
  t.info(`isDirectionInvertedFromDevice=${inverted ? 1 : 0} (${inverted ? 'natural scrolling' : 'classic'})`);
  for (const name of ['Space two', 'Space three']) {
    // (Typed into the form's field, then Enter, through UI DevTools.)
    t.check(`created "${name}"`, await createSpace(anchor, name));
    await sleep(1000);
  }
  let s = await waitFor(async () => { const x = await spaces(); return x.buttons.length >= 3 && x; }, 6000);
  t.check('three Space buttons in the sidebar', !!s, say(await spaces()));
  s = await spaces();
  // Start in the middle one, with a real click on its button.
  const c = await clickView(s.buttons[1], s.vs, 1000);
  t.check('a real click on the second Space\'s button', c.ok, c.why);
  s = await spaces();
  t.check('...the second Space is active', s.active === 1, say(s));
  await parkPointer();
  await t.shot('middle-space');
}, { needs: ['real input'] });

await step('UF-SW-2', 'Fingers right to left: the Space on the right comes in (the next one)', async t => {
  const before = await spaces();
  await fingers(-300);
  const s = await spaces();
  t.check('the next Space (the one on the right) is active', s.active === before.active + 1,
          `${before.active} -> ${say(s)}`);
  await t.shot('after-right-to-left');
}, { needs: ['real input'] });

await step('UF-SW-3', 'Fingers left to right: the Space on the left comes back', async t => {
  const before = await spaces();
  await fingers(300);
  const s = await spaces();
  t.check('the previous Space (the one on the left) is active', s.active === before.active - 1,
          `${before.active} -> ${say(s)}`);
  await t.shot('after-left-to-right');
}, { needs: ['real input'] });

await step('UF-SW-4', 'A short, slow drag springs back', async t => {
  const before = await spaces();
  await fingers(-60, 15, 60);
  const s = await spaces();
  t.check('the same Space stays active (60 pt, slow: under the threshold)', s.active === before.active,
          `${before.active} -> ${say(s)}`);
  await t.shot('sprang-back');
}, { needs: ['real input'] });

await step('UF-SW-5', 'Past the last Space: the + badge, and the Create-a-Space form', async t => {
  await fingers(-300);
  let s = await spaces();
  t.check('on the last Space', s.active === s.buttons.length - 1, say(s));
  await fingers(-480, 24, 16);
  s = await waitFor(async () => { const x = await spaces(); return x.form && x; }, 3000) ?? await spaces();
  t.check('a long swipe on past it opens the Create-a-Space form', s.form, say(s));
  t.check('...and creates no Space by itself', s.buttons.length === 3, say(s));
  await t.shot('create-form');
  await keyPress('Escape', '', 900);
  s = await spaces();
  t.check('Esc closes the form', !s.form, say(s));
}, { needs: ['real input'] });

await step('UF-SW-6', 'Past the first Space, towards the Library: the Library opens, and closes again', async t => {
  const s0 = await spaces();
  const c = await clickView(s0.buttons[0], s0.vs, 1000);
  t.check('a real click on the first Space\'s button', c.ok, c.why);
  let s = await spaces();
  t.check('on the first Space', s.active === 0, say(s));
  await fingers(300);
  s = await waitFor(async () => { const x = await spaces(); return x.libraryOpen && x; }, 3000) ?? await spaces();
  t.check('fingers left to right past the first Space open the Library (on the left)', s.libraryOpen, say(s));
  await t.shot('library');
  await fingers(-300);
  s = await waitFor(async () => { const x = await spaces(); return !x.libraryOpen && x; }, 3000) ?? await spaces();
  t.check('swiping back right to left closes it', !s.libraryOpen, say(s));
  t.check('...on the first Space still', s.active === 0, say(s));
  await t.shot('library-closed');
}, { needs: ['real input'] });

await step('UF-SW-7', 'Sidebar on the right: one swipe right to left opens the Library, one left to right closes it', async t => {
  t.check('koan.tabs.on_right set', await setKoanPref('koan.tabs.on_right', true) !== false);
  await sleep(1500);  // the sidebar moves over
  const s0 = await spaces();
  const c = await clickView(s0.buttons[s0.buttons.length - 1], s0.vs, 1000);
  t.check('a real click on the last Space\'s button', c.ok, c.why);
  let s = await spaces();
  t.check('on the last Space (the Library is past it, on the right)', s.active === s.buttons.length - 1, say(s));
  t.check('the Library is shut', !s.libraryOpen, say(s));
  await t.shot('right-sidebar');
  // One swipe only: the first one used to be lost (the Library's window came up
  // under the fingers and the rest of the gesture went to the page).
  await fingers(-300);
  s = await waitFor(async () => { const x = await spaces(); return x.libraryOpen && x; }, 3000) ?? await spaces();
  t.check('one swipe, fingers right to left, opens the Library (on the right)', s.libraryOpen, say(s));
  t.check('...on the last Space still', s.active === s.buttons.length - 1, say(s));
  await t.shot('library-right');
  await fingers(300);
  s = await waitFor(async () => { const x = await spaces(); return !x.libraryOpen && x; }, 3000) ?? await spaces();
  t.check('one swipe back, left to right, closes it', !s.libraryOpen, say(s));
  t.check('...on the last Space still', s.active === s.buttons.length - 1, say(s));
  await t.shot('library-right-closed');
  await setKoanPref('koan.tabs.on_right', false);
  await sleep(1000);
}, { needs: ['real input'] });

end();
