// End-to-end test of a Space theme over the glass (Koan's translucent
// window), on Linux through --koan-test-window-glass (theme_glass.flags):
// the window paints Zen's maths for a transparent window (no opaque base,
// each colour at the opacity slider's value, Zen 1.24b's default theme),
// the slider goes on to 1 there and fades the opaque paint in until the
// blur is covered, it works without colours too, and the value is saved
// with the Space. Without glass the paint stays opaque (theme.mjs). In the
// Zen look (local_state/theme_glass.json): the default shadcn style has no
// Space themes (KoanThemeController::IsEnabled()).
//
//
// The slider is driven from the keyboard where its controls take the focus;
// on macOS without Full Keyboard Access they don't (lib/theme_picker.mjs
// keyboardFocus()), and the same values are set by pressing the slider.
//
//   scripts/e2e/run.sh theme_glass
import { IS_MAC, check, finish } from './lib/koan.mjs';
import { skipped } from './lib/mac.mjs';
import {
  NO_KEYBOARD_FOCUS, VK, click, closePicker, keyboardFocus, pagePrefersDark, palette, picker,
  pickerKey, tree,
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

// Without the focus: a press on the slider at the point for `value`
// (KoanThemeOpacitySlider::SetFromX(): the thumb's centre travels from
// 5 + width/2 to the end less as much, the thumb growing with the value; the
// range ends at 1 on glass), clamped at its ends.
const keys = keyboardFocus(picker(await tree()));
if (!keys) {
  skipped('The opacity slider from the keyboard (Tab, Home, End, Left, Right)',
          `${NO_KEYBOARD_FOCUS}; the checks below press the slider instead`);
}
const pressSlider = async value => {
  const t = await tree();
  const slider = t.find(v => v.cls === 'KoanThemeOpacitySlider' && v.shown);
  const progress = v => Math.min(Math.max((v - 0.25) / (1 - 0.25), 0), 1);
  const thumb = 10 + 15 * progress(+picker(t)?.p.Opacity || 0.25);
  // The ends exactly: a press past them.
  const x = value <= 0.25 ? 0 : value >= 1 ? slider.w - 1
                          : Math.round(5 + thumb / 2 + progress(value) * (slider.w - 10 - thumb));
  await click(slider, x, Math.round(slider.h / 2));
};

// The slider works without colours on glass. Tab to it; End is 1.
let found = false;
for (let i = 0; i < 20 && !found && keys; i++) {
  await pickerKey(VK.TAB);
  await pickerKey(VK.END);
  found = near(await opacity(), 1);
}
if (!keys) {
  await pressSlider(1);
  found = near(await opacity(), 1);
}
check(keys ? 'On glass the opacity slider takes the focus without colours and goes to 1'
           : 'On glass the opacity slider works without colours and goes to 1', found,
      `${await opacity()}`);
text = await background();
check('...where the opaque paint covers the blur', text.includes(' / cover 100%: '), text);
if (keys) await pickerKey(VK.HOME);
else await pressSlider(0.25);
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
if (keys) {
  await pickerKey(VK.RIGHT);
  await pickerKey(VK.RIGHT);
} else {
  await pressSlider(min + 0.1);
}
text = await background();
check('Moving the slider changes the painted alpha live',
      alphas(text).every(a => near(a, min + 0.1)) && !text.includes('cover'), text);

// Above Zen's 0.8 the opaque paint fades in: 0.9 is half.
if (keys) {
  await pickerKey(VK.END);
  await pickerKey(VK.LEFT);
  await pickerKey(VK.LEFT);
} else {
  await pressSlider(0.9);
}
text = await background();
// (A press lands on a pixel: within 0.002 of 0.9, the cover by as much.)
const glassAmount = await opacity();
const cover = keys ? '50%' : `${Math.round((glassAmount - 0.8) / (1 - 0.8) * 1000) / 10}%`;
check('At 0.9 the opaque paint is half faded in', near(glassAmount, 0.9) &&
      text.includes(` / cover ${cover}: `), `${glassAmount} ${text}`);

// Saved with the Space, like Zen's theme.opacity.
await closePicker();
await palette('Open Theme Picker');
check('The Space keeps its glass amount', Math.abs(await opacity() - glassAmount) < 0.0005,
      `${await opacity()} (set ${glassAmount})`);
await closePicker();

finish();
