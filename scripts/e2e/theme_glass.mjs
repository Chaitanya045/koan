// End-to-end test of a Space theme over the glass (Koan's translucent
// window), on Linux through --koan-test-window-glass (theme_glass.flags):
// the window paints Zen's maths for a transparent window (no opaque base,
// each colour at the opacity slider's value, Zen 1.24b's default theme),
// the slider goes on to 1 there and fades the opaque paint in until the
// blur is covered, it works without colours too, and the value is saved
// with the Space. Without glass the paint stays opaque (theme.mjs).
//
//   scripts/e2e/run.sh theme_glass
import { check, finish } from './lib/koan.mjs';
import {
  VK, click, closePicker, palette, picker, pickerKey, tree,
} from './lib/theme_picker.mjs';

const background = async () => picker(await tree())?.p.WindowBackground ?? '';
const opacity = async () => +picker(await tree())?.p.Opacity;
// The alphas of the colours a background description paints.
const alphas = text => [...text.matchAll(/rgba\(\d+, \d+, \d+, ([\d.]+)\)/g)].map(m => +m[1]);
const near = (a, b) => Math.abs(a - b) < 0.006;

await palette('Open Theme Picker');
if (!picker(await tree())) {
  check('The palette opens the theme picker', false);
  finish();
}

// Zen 1.24b's default theme on a transparent window: nothing opaque, black
// at 40% in dark mode (white at 40% on Linux in light mode).
let text = await background();
check("The default theme on glass is Zen's (no base, black or white at 40%)",
      /^glass: rgba\((0, 0, 0|255, 255, 255), 0\.4\)$/.test(text), text);

// The slider works without colours on glass. Tab to it; End is 1.
let found = false;
for (let i = 0; i < 20 && !found; i++) {
  await pickerKey(VK.TAB);
  await pickerKey(VK.END);
  found = near(await opacity(), 1);
}
check('On glass the opacity slider takes the focus without colours and goes to 1', found,
      `${await opacity()}`);
text = await background();
check('...where the opaque paint covers the blur', text.includes(' / cover 100%: '), text);
await pickerKey(VK.HOME);
const min = await opacity();
text = await background();
check("Home is Zen's minimum, with no cover", near(min, 0.25) && !text.includes('cover'),
      `${min} ${text}`);

// A preset: its colour at the slider's opacity, over the blur (Linux has no
// white overlay: the dot's own colour).
const t = await tree();
await click(t.find(v => v.cls === 'KoanThemeSwatch' && v.p.Index === '0'), 13, 13);
text = await background();
check("A preset's colour at the slider's opacity, nothing under it",
      text.startsWith('glass: ') && alphas(text).length > 0 &&
      alphas(text).every(a => near(a, min)), text);
await pickerKey(VK.RIGHT);
await pickerKey(VK.RIGHT);
text = await background();
check('Moving the slider changes the painted alpha live',
      alphas(text).every(a => near(a, min + 0.1)) && !text.includes('cover'), text);

// Above Zen's 0.8 the opaque paint fades in: 0.9 is half.
await pickerKey(VK.END);
await pickerKey(VK.LEFT);
await pickerKey(VK.LEFT);
text = await background();
check('At 0.9 the opaque paint is half faded in', near(await opacity(), 0.9) &&
      text.includes(' / cover 50%: '), `${await opacity()} ${text}`);

// Saved with the Space, like Zen's theme.opacity.
await closePicker();
await palette('Open Theme Picker');
check('The Space keeps its glass amount', near(await opacity(), 0.9), `${await opacity()}`);
await closePicker();

finish();
