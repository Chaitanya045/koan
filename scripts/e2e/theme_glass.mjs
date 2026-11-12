// End-to-end test of a Space theme over the glass (Koan's translucent
// window), on Linux through --koan-test-window-glass (theme_glass.flags):
// the window paints Zen's maths for a transparent window (no opaque base,
// each colour at the opacity slider's value, Zen 1.24b's default theme),
// the slider goes on to 1 there and fades the opaque paint in until the
// blur is covered, it works without colours too, and the value is saved
// with the Space. Without glass the paint stays opaque (theme.mjs).
//
//   scripts/e2e/run.sh theme_glass
import { IS_MAC, check, finish } from './lib/koan.mjs';
import {
  VK, button, click, closePicker, pagePrefersDark, palette, picker, pickerKey, tree,
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
// at 40% in dark mode; in light mode white at 40%, nothing at all on macOS
// (gh-15334). The mode is the browser's (System by default), which the
// showing website follows.
const dark = await pagePrefersDark();
const want = dark ? 'glass: rgba(0, 0, 0, 0.4)'
                  : IS_MAC ? 'glass: ' : 'glass: rgba(255, 255, 255, 0.4)';
let text = await background();
check(`The default theme on glass is Zen's (no base; ${dark ? 'dark' : 'light'}: ${JSON.stringify(want)})`,
      text === want, JSON.stringify(text));

// The slider works without colours on glass. Tab to it; End is 1.
async function focusSlider() {
  for (let i = 0; i < 20; i++) {
    await pickerKey(VK.TAB);
    await pickerKey(VK.END);
    if (near(await opacity(), 1)) return true;
  }
  return false;
}
const found = await focusSlider();
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
// white overlay: the dot's own colour). In the dark scheme a theme that makes
// the window light is painted at least at its contrast floor (#291, below).
const floorOf = text => +(text.match(/ \/ floor ([\d.]+)%/)?.[1] ?? 0) / 100;
const painted = (text, opacity) => Math.max(opacity, floorOf(text));
let t = await tree();
await click(t.find(v => v.cls === 'KoanThemeSwatch' && v.p.Index === '0'), 13, 13);
text = await background();
check("A preset's colour at the slider's opacity, nothing under it",
      text.startsWith('glass: ') && alphas(text).length > 0 &&
      alphas(text).every(a => near(a, painted(text, min))), text);
await pickerKey(VK.RIGHT);
await pickerKey(VK.RIGHT);
text = await background();
check('Moving the slider changes the painted alpha live',
      alphas(text).every(a => near(a, painted(text, min + 0.1))) && !text.includes('cover'),
      text);

// #291: a light theme (Zen's first preset makes the window light) over the
// blur in the dark scheme. Zen paints it at the slider's opacity, which over
// the dark blur is a mid-grey its dark text fades into; Koan paints it at
// least as opaque as it takes for the text to keep 4.5:1. In the light
// scheme (a light blur) it is the slider's opacity, as in Zen.
await pickerKey(VK.HOME);
t = await tree();
await click(button(t, 'Dark'));
text = await background();
let mode = picker(await tree())?.p.WindowMode;
check('Dark scheme, light preset at the minimum: painted at a contrast floor above it',
      mode === 'light' && floorOf(text) > min + 0.05 && floorOf(text) <= 0.8 &&
      alphas(text).length > 0 && alphas(text).every(a => near(a, floorOf(text))),
      `${mode} ${text}`);
check('...while the slider keeps its value', near(await opacity(), min), `${await opacity()}`);
// The click took the focus: Tab back to the slider (End is 1), then 0.8.
await focusSlider();
for (let i = 0; i < 4; i++) await pickerKey(VK.LEFT);
text = await background();
check('...and from the slider\'s 0.8 no floor (its own opacity reads)',
      near(await opacity(), 0.8) && floorOf(text) === 0 && alphas(text).every(a => near(a, 0.8)),
      `${await opacity()} ${text}`);
await pickerKey(VK.HOME);
await click(button(await tree(), 'Light'));
text = await background();
mode = picker(await tree())?.p.WindowMode;
check('Light scheme: the light preset at the slider\'s minimum, no floor',
      mode === 'light' && floorOf(text) === 0 && alphas(text).every(a => near(a, min)),
      `${mode} ${text}`);
await click(button(await tree(), 'Automatic'));
await focusSlider();

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
