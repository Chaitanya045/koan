// End-to-end test of Koan's look (issue #48): the default is the shadcn style
// in dark mode, drawn from shadcn's tokens everywhere (the window's frame as
// pixels, the settings page as computed styles), and the look switches live:
// light mode, the Zen style, a base colour, an accent and a pasted theme.
//
//   scripts/e2e/run.sh look
//
// shadcn's Neutral palette (ui.shadcn.com/docs/theming), as sRGB: dark
// background #0a0a0a, sidebar #171717, primary #e5e5e5; light background
// #ffffff, sidebar #fafafa.
import {
  CTRL, check, evalIn, finish, front, key, pages, settingsCall, sleep,
  waitFor, windowFrame,
} from './lib/koan.mjs';

const settingsPages = async () =>
  (await pages()).filter(p => p.url.startsWith('chrome://koan-settings'));

let [other] = await pages();
await front(other);
await key(other, 188, CTRL, ',');
let [settings] = await waitFor(settingsPages);
await waitFor(() => evalIn(settings, `document.querySelectorAll('.category').length > 0`));

const pref = name => evalIn(settings, `import('chrome://resources/js/cr.js').then(
    m => m.sendWithPromise('getSettings')).then(s => s.prefs[${JSON.stringify(name)}])`);
const set = (name, value) => settingsCall(settings, 'setPref', name, value);
const bg = () => evalIn(settings, `getComputedStyle(document.body).backgroundColor`);
const near = ([r, g, b], [er, eg, eb], tolerance = 3) =>
    Math.abs(r - er) <= tolerance && Math.abs(g - eg) <= tolerance &&
    Math.abs(b - eb) <= tolerance;
// A pixel of the sidebar's left edge, in the middle of the window's height.
const sidebarPixel = async () => {
  await sleep(1500);
  const frame = windowFrame();
  return frame.pixel(3, Math.floor(frame.height / 2));
};

check('The default style is shadcn', await pref('koan.look.style') === 'shadcn');
check('...dark', await pref('koan.look.mode') === 'dark');
check('...with the Neutral base colour', await pref('koan.look.base') === 'neutral');
check('The page draws from shadcn\'s dark background',
      await waitFor(async () => await bg() === 'rgb(10, 10, 10)'), await bg());
const sidebar = await sidebarPixel();
check('The window\'s sidebar and frame are shadcn\'s dark sidebar colour',
      near(sidebar, [23, 23, 23]), `${sidebar}`);
check('The page has the Look settings',
      await evalIn(settings, `['koan.look.style', 'koan.look.mode', 'koan.look.base',
          'koan.look.accent', 'koan.look.custom_css'].every(
              p => document.querySelector('[data-pref="' + p + '"]'))`));

// Light mode, live.
check('Setting the mode to light works', await set('koan.look.mode', 'light'));
check('...the page switches to the light background',
      await waitFor(async () => await bg() === 'rgb(255, 255, 255)'), await bg());
const light = await sidebarPixel();
check('...and so does the window (sidebar #fafafa)', near(light, [250, 250, 250]),
      `${light}`);

// A base colour and an accent.
await set('koan.look.mode', 'dark');
await set('koan.look.base', 'zinc');
check('The Zinc base colour changes the page (#09090b)',
      await waitFor(async () => await bg() === 'rgb(9, 9, 11)'), await bg());
await set('koan.look.base', 'neutral');
await set('koan.look.accent', 'blue');
check('The Blue accent changes the primary colour',
      await waitFor(async () => (await evalIn(settings,
          `getComputedStyle(document.documentElement).getPropertyValue('--primary').trim()`)) !== '#e5e5e5'));
await set('koan.look.accent', 'none');

// A pasted theme: validated, applied to the page and the native UI, removable.
check('A pasted text that is not a theme is refused',
      await set('koan.look.custom_css', 'body { color: red }') === false);
check('...and leaves the look alone', await pref('koan.look.custom_css') === '');
const theme = ':root { --sidebar: #00ff00; } .dark { --sidebar: #ff0000; ' +
    '--background: #0000ff; --radius: 0.25rem; }';
check('A pasted theme is accepted', await set('koan.look.custom_css', theme));
check('...its dark background reaches the page',
      await waitFor(async () => await bg() === 'rgb(0, 0, 255)'), await bg());
const pasted = await sidebarPixel();
check('...and its sidebar colour the window\'s frame', near(pasted, [255, 0, 0]),
      `${pasted}`);
await set('koan.look.custom_css', '');
check('Removing it restores the base colour',
      await waitFor(async () => await bg() === 'rgb(10, 10, 10)'), await bg());

// The Zen style, live.
check('Switching to Zen works', await set('koan.look.style', 'zen'));
check('...the page keeps Zen\'s own colours',
      await waitFor(async () => (await bg()) !== 'rgb(10, 10, 10)'), await bg());
const zen = await sidebarPixel();
check('...and the window leaves shadcn\'s sidebar colour',
      !near(zen, [23, 23, 23], 1), `${zen}`);
await set('koan.look.style', 'shadcn');
check('Back to shadcn', await waitFor(async () => await bg() === 'rgb(10, 10, 10)'));

finish();
