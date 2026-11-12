// End-to-end test of Koan's look (issues #48, #296): the default is the
// shadcn style (dark mode), drawn from shadcn's tokens everywhere (the
// window's frame as pixels, the settings page as computed styles), and the
// look switches live: light mode, the Zen style, a base colour, an accent
// and a pasted theme.
//
//   scripts/e2e/run.sh look
//
// The window's pixels: headless Ozone's frame dumps off macOS (look.flags);
// on macOS (scripts/mac/suite.sh) a screencapture of the window
// (lib/mac.mjs captureWindow()), with the translucent window off
// (local_state/look.json) so the sidebar's colour is Koan's alone. The
// settings tab opens through CDP there (Cmd+, is a main-menu key).
//
// Koan's default "Black" base colour (shadcn's Zinc with near-black dark
// surfaces), as sRGB: dark background and sidebar #09090b, card #18181b,
// border #27272a; light background #ffffff, sidebar #fafafa. Neutral's dark
// background is #0a0a0a.
import {
  ACCEL, IS_MAC, browserCall, check, evalIn, finish, front, key, pages, settingsCall, sleep,
  waitFor, windowFrame,
} from './lib/koan.mjs';
import { captureWindow } from './lib/mac.mjs';

const settingsPages = async () =>
  (await pages()).filter(p => p.url.startsWith('chrome://settings'));

let [other] = await pages();
await front(other);
if (IS_MAC) await browserCall('Target.createTarget', { url: 'chrome://settings/' });
else await key(other, 188, ACCEL, ',');
let [settings] = await waitFor(settingsPages);
await waitFor(() => evalIn(settings, `koanSettingsRoot?.getElementById('pane')?.childElementCount > 0`));

const pref = name => evalIn(settings, `import('chrome://resources/js/cr.js').then(
    m => m.sendWithPromise('koanGetSettings')).then(s => s.prefs[${JSON.stringify(name)}])`);
const set = (name, value) => settingsCall(settings, 'koanSetPref', name, value);
// The page's background: the look paints it on the root (koan_tokens.css).
const bg = () => evalIn(settings, `getComputedStyle(document.documentElement).backgroundColor`);
// A pixel of the sidebar's left edge (3 points in), in the middle of the
// window's height; null (and the reason printed) if the window couldn't be
// captured.
const sidebarPixel = async () => {
  await sleep(1500);
  let frame = null;
  if (IS_MAC) {
    const shot = await captureWindow(settings);
    if (shot.error) console.log(`  (no capture of the window: ${shot.error})`);
    frame = shot.frame ?? null;
  } else {
    frame = { ...windowFrame(), scale: 1 };
  }
  return frame && frame.pixel(Math.round(3 * frame.scale), Math.floor(frame.height / 2));
};
const near = (pixel, [er, eg, eb], tolerance = 3) => !!pixel &&
    Math.abs(pixel[0] - er) <= tolerance && Math.abs(pixel[1] - eg) <= tolerance &&
    Math.abs(pixel[2] - eb) <= tolerance;

// The default look is shadcn's (the user's decision, 2026-11-11: #296).
check('The default style is shadcn', await pref('koan.look.style') === 'shadcn',
      await pref('koan.look.style'));
check('...dark', await pref('koan.look.mode') === 'dark');
check('...with the Black base colour', await pref('koan.look.base') === 'black');
check('The page draws from shadcn\'s dark background',
      await waitFor(async () => await bg() === 'rgb(9, 9, 11)'), await bg());
const sidebar = await sidebarPixel();
check('The window\'s sidebar and frame are near-black too (#09090b)',
      near(sidebar, [9, 9, 11]), `${sidebar}`);
check('...and the page\'s cards are #18181b',
      await evalIn(settings, `getComputedStyle(document.documentElement)
          .getPropertyValue('--card').trim()`) === '#18181b');
check('...and its borders #27272a',
      await evalIn(settings, `getComputedStyle(document.documentElement)
          .getPropertyValue('--border').trim()`) === '#27272a');
check('The page has the Look settings',
      await evalIn(settings, `['koan.look.style', 'koan.look.mode', 'koan.look.base',
          'koan.look.accent', 'koan.look.custom_css'].every(
              p => koanSettingsRoot.querySelector('[data-pref="' + p + '"]'))`));

// Light mode, live.
check('Setting the mode to light works', await set('koan.look.mode', 'light'));
check('...the page switches to the light background',
      await waitFor(async () => await bg() === 'rgb(255, 255, 255)'), await bg());
const light = await sidebarPixel();
check('...and so does the window (sidebar #fafafa)', near(light, [250, 250, 250]),
      `${light}`);

// A base colour and an accent.
await set('koan.look.mode', 'dark');
await set('koan.look.base', 'neutral');
check('The Neutral base colour changes the page (#0a0a0a)',
      await waitFor(async () => await bg() === 'rgb(10, 10, 10)'), await bg());
await set('koan.look.base', 'black');
await set('koan.look.accent', 'blue');
check('The Blue accent changes the primary colour',
      await waitFor(async () => (await evalIn(settings,
          `getComputedStyle(document.documentElement).getPropertyValue('--primary').trim()`)) !== '#e4e4e7'));
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
      await waitFor(async () => await bg() === 'rgb(9, 9, 11)'), await bg());

// The Zen style, live.
check('Switching to Zen works', await set('koan.look.style', 'zen'));
check('...the page keeps Zen\'s own colours',
      await waitFor(async () => (await bg()) !== 'rgb(9, 9, 11)'), await bg());
const zen = await sidebarPixel();
check('...and the window leaves shadcn\'s sidebar colour',
      !!zen && !near(zen, [9, 9, 11], 1), `${zen}`);
await set('koan.look.style', 'shadcn');
check('Back to shadcn', await waitFor(async () => await bg() === 'rgb(9, 9, 11)'));

finish();
