// End-to-end test of the welcome screen in a MacBook Air's default window
// (welcome_small.flags: Chromium's 1050 px default width, about 830 px of a
// 1470x956 screen's height): on every page the text stays clear of the
// buttons and everything stays inside the card. SHOTS= saves each page.
//
//   scripts/e2e/run.sh welcome_small
import { check, finish, mouseOn, saveShot, shownViews, sleep, views } from './lib/koan.mjs';

const press = async (view, wait = 1200) => {
  await mouseOn(view, 'mousePressed');
  await mouseOn(view, 'mouseReleased');
  await sleep(wait);
};
const bottom = v => v.y + v.h;
const inside = (v, box) => v.x >= box.x && v.y >= box.y && v.x + v.w <= box.x + box.w &&
                           bottom(v) <= bottom(box);

await sleep(3500);
let vs = await views();
const screen = shownViews(vs, 'KoanWelcomeView')[0];
check('The welcome screen shows in a 1050x830 window', !!screen,
      screen && `${screen.w}x${screen.h}`);
const chars = vs.filter(v => v.cls === 'Label' && v.path.at(-1) === 'KoanWelcomeView');
const start = shownViews(vs, 'KoanWelcomeStartButton')[0];
check('...its title fits the window, above the start button',
      chars.every(c => c.x >= 0 && c.x + c.w <= screen.w && bottom(c) <= start.y),
      JSON.stringify(chars.map(c => [c.x, c.w]).filter(([x, w]) => x < 0 || x + w > screen.w)));
saveShot('small-start');
await press(start, 1500);

const pages = ['import', 'colours', 'search', 'essentials', 'default', 'done'];
for (const [index, name] of pages.entries()) {
  vs = await views();
  const card = shownViews(vs, 'KoanWelcomePagesView')[0];
  const text = vs.filter(v => v.cls === 'Label' && v.path.at(-1) === 'KoanWelcomePagesView');
  const buttons = shownViews(vs, 'KoanWelcomeButton');
  const firstButton = Math.min(...buttons.map(b => b.y));
  const lowestText = Math.max(...text.map(bottom));
  check(`${name}: the text ends above the buttons`, lowestText <= firstButton,
        `text to ${lowestText}, buttons from ${firstButton}`);
  const parts = vs.filter(v => v.shown && v.path.at(-1) === 'KoanWelcomePagesView' &&
                               v.w > 0 && v.h > 0);
  // Both in the screen widget's coordinates, like the card's own.
  const out = parts.filter(v => !inside(v, card))
      .map(v => `${v.cls} ${v.x},${v.y} ${v.w}x${v.h}`);
  check(`${name}: everything stays inside the ${card.w}x${card.h} card`, !out.length,
        out.join('; '));
  saveShot(`small-${name}`);
  if (index < pages.length - 1) {
    await press(buttons[0]);
  }
}
finish();
