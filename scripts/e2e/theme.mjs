// End-to-end test of Zen's theme picker against a real build: the command
// palette's "Open Theme Picker" and a Space's "Theme…" (its header
// menu, and the Create-a-Space form's for a new Space) open the panel
// beside the sidebar; its layout and controls match Zen's; a preset gives
// exactly Zen's dot colours and window gradient; the colour actions, opacity
// slider and grain dial work; the theme is saved per Space when the panel
// closes and survives a restart; switching Spaces switches the gradient;
// the scheme buttons set the website appearance, which a theme that makes
// the window light or dark doesn't change (chrome:// pages follow the
// window); the field, slider and dial work from the keyboard; the Settings
// switch turns the themes off; the Night dark theme style applies.
//
//   scripts/e2e/run.sh theme
//
// Native views are read and driven through UI DevTools; keys go to a
// widget's node, so they take the path of real keys (Esc closes the panel,
// Enter runs a palette action).
//
// On macOS (scripts/mac/suite.sh): the Space header's menu is a native
// NSMenu (skipped: the palette opens the picker), the panel is placed and
// sized as Zen's NSPopover on macOS, and local_state/theme.json turns the
// translucent window off (theme_glass.mjs checks the glass; macOS 26+ has
// it on by default).
import { readFileSync, writeFileSync } from 'node:fs';
import { spawn } from 'node:child_process';
import { ACCEL, CDP_PORT, IS_MAC, NATIVE_MENUS, NATIVE_MENU_WHY, browserCall, createSpace,
         menuMouse, openMenus, pages, sleep, typeInto, viewMouse, viewTree, waitFor, check,
         finish } from './lib/koan.mjs';
import { skipped } from './lib/mac.mjs';
import {
  ALT, CTRL, VK, appearance, bigDot, browserWidget, button, click, closePicker, drag,
  lastPage, mouse, navigateShowing, pagePrefersDark, palette, paletteRows, picker,
  pickerKey, pickerWidget, setKoanPref, shortcut, showingTab, shown, tree,
  typeIntoOmnibox, ui, unquote, widgetKey,
} from './lib/theme_picker.mjs';

// Restarts Koan on the same profile, restoring the session.
const args = process.env.KOAN_CHROME_ARGS.split('\n').filter(Boolean);
const profile = args.find(a => a.startsWith('--user-data-dir=')).split('=')[1];
let relaunched = null;
process.on('exit', () => relaunched?.kill());
const browserUp = () => fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).then(() => true, () => false);
async function restart() {
  await browserCall('Browser.close');
  await waitFor(async () => !(await browserUp()), 20000);
  await sleep(2000);
  const file = `${profile}/Default/Preferences`;
  const prefs = JSON.parse(readFileSync(file, 'utf8'));
  prefs.session = { ...prefs.session, restore_on_startup: 1 };
  // Zen's hidden zen.theme.gradient.show-custom-colors (no settings row,
  // so setKoanPref can't set it): checked after the restart.
  prefs.koan = { ...prefs.koan, theme: { ...prefs.koan?.theme,
    gradient: { ...prefs.koan?.theme?.gradient, show_custom_colors: true } } };
  writeFileSync(file, JSON.stringify(prefs));
  relaunched = spawn(args[0], args.slice(1), { stdio: 'ignore' });
  await waitFor(browserUp, 30000);
  await waitFor(async () => (await pages()).length, 15000);
  await sleep(3000);
}

// Zen's colours for the analogous preset of page 2 (data-position 234,173,
// lightness 70), measured in Zen 1.23.3b, and the gradient Zen paints for
// it in a dark window.
const ZEN_DOTS = '234,123,137@234,173;234,204,123@219,217;233,124,229@210,133';
const ZEN_DARK_GRADIENT =
    'linear-gradient(-5deg, rgba(128, 74, 128, 1) 10%, transparent 80%), ' +
    'radial-gradient(circle at 95% 0%, rgba(129, 114, 75, 1) 0%, transparent 75%), ' +
    'radial-gradient(circle at 0% 0%, rgba(129, 73, 82, 1) 10%, transparent 70%)';

// Zen's values for the first preset of page 1 (#f4efdf, data-position
// 240,240, lightness 90) in a dark window at opacity 0.8 (the Space's,
// set above): its dot, the window's colour, and that it makes the window
// light (zen-should-be-dark-mode false); and the colour of that dot dragged
// to 200,180. Measured in Zen 1.23.3b.
const ZEN_LIGHT_DOT = '244,237,215@240,240';
const ZEN_LIGHT_BACKGROUND = 'rgba(200, 194, 177, 1)';
// The same preset at the default opacity (0.5), as for a new Space.
const ZEN_LIGHT_BACKGROUND_DEFAULT_OPACITY = 'rgba(134, 130, 121, 1)';
const ZEN_DRAGGED_DOT = '252,207,207@200,180';

// Where Zen 1.24b opens the panel (ZenGradientGenerator.mjs
// openThemePicker(), at #TabsToolbar: the sidebar below the URL pill inside
// the toolbox's padding, 5px, 6px on macOS). Off macOS a XUL popup at its
// top-right, "topright topleft", x -10. On macOS an NSPopover on its right
// edge (the x offset unused), centred on it along that edge, 13pt from it
// (the hidden arrow's distance; koan_theme_picker.cc PlaceBeside()).
// Returns [ok, details].
function placement(t) {
  const [x, y, w, h] = (picker(t)?.p.PanelInWindow ?? '').split(',').map(Number);
  const region = shown(t, 'VerticalTabStripRegionView')[0];
  const nav = shown(t, 'KoanSidebarNavView')[0];
  if (!region || !nav || Number.isNaN(x)) return [false, `${picker(t)?.p.PanelInWindow} no sidebar`];
  const padding = IS_MAC ? 6 : 5;
  const top = nav.ay + nav.h;
  const bottom = region.ay + region.h - padding;
  const wantX = IS_MAC ? region.ax + region.w - padding + 13 : region.ax + region.w - padding - 10;
  const wantY = IS_MAC ? top + Math.trunc((bottom - top - h) / 2) : top;
  return [Math.abs(x - wantX) <= 1 && Math.abs(y - wantY) <= 1,
          `panel ${x},${y} ${w}x${h}; want ${wantX},${wantY}`];
}

// The Space header's menu (Spaces, #57): Change Name…, Change Icon…, Edit
// Theme…, ….
async function headerMenu() {
  const header = (await viewTree()).find(v => v.cls === 'KoanSpaceHeaderView' && v.shown);
  for (const type of ['mousePressed', 'mouseReleased']) {
    await viewMouse(header, type, 20, 20, 'right');
  }
  await sleep(1500);
  return openMenus(await viewTree())[0];
}

// The active Space's name, from its header.
async function spaceName() {
  const header = (await viewTree()).find(v => v.cls === 'KoanSpaceHeaderView' && v.shown);
  return header?.props.Name?.replace(/^"(.*)"$/, '$1') ?? '';
}
// Ctrl+Alt+Left/Right (Cmd+Option on macOS), until the header shows `name`.
async function switchSpace(code, name) {
  await shortcut(code, ACCEL | ALT, code === VK.LEFT ? 'ArrowLeft' : 'ArrowRight');
  const ok = await waitFor(async () => await spaceName() === name);
  check(`Switched to the Space "${name}"`, ok, await spaceName());
  await sleep(500);
}

// The window starts in one Space ("Space", the default theme); a second one
// gets the themes below. Its tab is a new-tab page (WebUI, coloured like
// the window); the website checks need a website.
await createSpace(await showingTab(), 'Second');
await sleep(1500);
await navigateShowing('data:text/html,<title>Space 1</title>A website');
if (NATIVE_MENUS) {
  skipped('The Space header\'s "Theme…" opens the theme picker', NATIVE_MENU_WHY);
} else {
  const menu = await headerMenu();
  check('The Space header has a menu', menu?.items.length >= 3, `${menu?.items.length} item(s)`);
  if (menu) await menuMouse(menu, menu.items[2]);  // Theme…
  const t = await tree();
  check('Its "Theme…" opens the theme picker', !!picker(t));
  check("...at the tab list's top-right, as Zen's (x -10)", ...placement(t));
  await closePicker();
}

await palette('Open Theme Picker');
let t = await tree();
let p = picker(t);
check('The palette\'s "Open Theme Picker" opens the theme picker', !!p);
if (!p) finish();
check('...in the same place', ...placement(t));

// 426 + the grain dial (73px, 66px on macOS: Zen's macOS panel is 380 x 514
// with its padding and border).
const PANEL_HEIGHT = IS_MAC ? 492 : 499;
check("The panel has Zen's size (358px wide inside 10px padding)",
      p.w === 358 && p.h === PANEL_HEIGHT, `${p.w}x${p.h}`);
const field = shown(t, 'KoanThemeGradientField')[0];
check('The colour field is 358x358', field?.w === 358 && field?.h === 358,
      `${field?.w}x${field?.h}`);
check('Scheme buttons: Automatic, Light, Dark',
      ['Automatic', 'Light', 'Dark'].every(l => button(t, l)));
check('Without colours the colour actions are disabled',
      ['Add color', 'Remove color'].every(l => button(t, l)?.p.Enabled === 'false'));
check('"Click to add a color" shows in an empty field',
      shown(t, 'Label').some(l => l.p.Text === 'Click to add a color'));
const swatches = t.filter(v => v.cls === 'KoanThemeSwatch');
check("Five preset pages with Zen's 41 swatches", swatches.length === 41, `${swatches.length}`);
check('Opacity slider and grain dial',
      !!shown(t, 'KoanThemeOpacitySlider')[0] && !!shown(t, 'KoanThemeGrainDial')[0]);

await click(button(t, 'Dark'));
check('The Dark scheme button makes websites dark', await pagePrefersDark() === true);

await click(button(await tree(), 'Next colors'));
t = await tree();
p = picker(t);
check('The right arrow pages the presets', p.p.Page === '1', p.p.Page);
await click(t.find(v => v.cls === 'KoanThemeSwatch' && v.p.Index === '11'), 13, 13);
t = await tree();
p = picker(t);
check("A preset gives Zen's dots", p.p.Dots === ZEN_DOTS, p.p.Dots);
check('The preset uses the analogous harmony', p.p.Algorithm === 'analogous', p.p.Algorithm);
check("The window shows Zen's gradient for it", p.p.WindowBackground === ZEN_DARK_GRADIENT,
      p.p.WindowBackground);
check('The colour actions follow the dots (3: no add)',
      button(t, 'Add color')?.p.Enabled === 'false' &&
      button(t, 'Remove color')?.p.Enabled !== 'false');

await click(button(t, 'Remove color'));
t = await tree();
p = picker(t);
check('Remove drops a dot and re-harmonises (complementary)',
      p.p.Dots.split(';').length === 2 && p.p.Algorithm === 'complementary',
      `${p.p.Dots} ${p.p.Algorithm}`);
await click(button(t, 'Complementary'));
t = await tree();
p = picker(t);
check('The harmony button cycles (Analogous)', p.p.Algorithm === 'singleAnalogous', p.p.Algorithm);
await click(button(t, 'Add color'));
t = await tree();
p = picker(t);
check('Add brings a third dot', p.p.Dots.split(';').length === 3, p.p.Dots);

const slider = shown(t, 'KoanThemeOpacitySlider')[0];
await click(slider, slider.w - 6, Math.round(slider.h / 2));
t = await tree();
p = picker(t);
check('The opacity slider goes up to 0.8', Math.abs(+p.p.Opacity - 0.8) < 0.001, p.p.Opacity);

const dial = shown(t, 'KoanThemeGrainDial')[0];
// The handle sits at the top of the dial (8px margin around the dial, 73px,
// 66px on macOS): from there to 3 o'clock.
const dialCentre = Math.floor(dial.w / 2);
await drag(dial, [dialCentre, 8], [dial.w - 2, dialCentre]);
t = await tree();
p = picker(t);
check("Dragging the grain handle to 3 o'clock sets 25% grain", +p.p.Texture === 0.25, p.p.Texture);
const edited = p.p.Dots;

await closePicker();
check('Esc closes the panel', !picker(await tree()));

await switchSpace(VK.LEFT, 'Space');
await palette('Open Theme Picker');
t = await tree();
p = picker(t);
check('The first Space has its own (default) theme',
      p?.p.Dots === '' && p?.p.WindowBackground === 'rgba(19, 19, 19, 1)',
      `${p?.p.Dots} ${p?.p.WindowBackground}`);

// The keyboard: Tab reaches the colour field first; Enter adds a colour in
// its middle, the arrows move it, Delete removes it.
await pickerKey(VK.TAB);
await pickerKey(VK.ENTER);
p = picker(await tree());
const middle = bigDot(p);
check('Tab, Enter: a colour in the middle of the field',
      middle?.every(c => Math.abs(c - 179) <= 1), p?.p.Dots);
await pickerKey(VK.RIGHT);
await pickerKey(VK.RIGHT);
p = picker(await tree());
check('...the arrows move it', bigDot(p)?.[0] === middle?.[0] + 8 && bigDot(p)?.[1] === middle?.[1],
      p?.p.Dots);
await pickerKey(VK.DELETE);
check('...and Delete removes it', picker(await tree())?.p.Dots === '');
await click(button(t, 'Light'));
check('The Light scheme button makes websites light', await pagePrefersDark() === false,
      await appearance());
p = picker(await tree());
check("A light window without a theme is Zen's #e9e9e9",
      p?.p.WindowBackground === 'rgba(233, 233, 233, 1)', p?.p.WindowBackground);
await click(button(await tree(), 'Dark'));
check('The Dark scheme button makes websites dark again', await pagePrefersDark() === true,
      await appearance());
await closePicker();

await switchSpace(VK.RIGHT, 'Second');
await palette('Open Theme Picker');
p = picker(await tree());
check('The Space kept its theme', p?.p.Dots === edited && +p?.p.Texture === 0.25,
      `${p?.p.Dots} ${p?.p.Texture}`);
check('Websites follow the Dark appearance', await pagePrefersDark() === true, await appearance());

// A theme that makes the window light: websites keep the appearance.
t = await tree();
await click(t.find(v => v.cls === 'KoanThemeSwatch' && v.p.Index === '0'), 13, 13);
p = picker(await tree());
check("A light preset: Zen's dot and window colour",
      p?.p.Dots === ZEN_LIGHT_DOT && p?.p.WindowBackground === ZEN_LIGHT_BACKGROUND,
      `${p?.p.Dots} ${p?.p.WindowBackground}`);
check('...and it makes the window light, as in Zen', p?.p.WindowMode === 'light', p?.p.WindowMode);
// #291's contrast floor is for a translucent window over a dark blur only:
// an opaque window blends the theme into its own colour and decides the mode
// on that (theme_glass.mjs checks the floor).
check('...at the slider\'s opacity on an opaque window, with no contrast floor',
      !(p?.p.WindowBackground ?? '').includes(' / floor '), p?.p.WindowBackground);
check('...while websites keep the Dark appearance', await pagePrefersDark() === true,
      await appearance());
await closePicker();

// chrome:// pages are coloured like the window, so they follow this theme
// (light), and the appearance (Dark) once Space themes are off in Settings.
// (In the showing tab: Chromium updates a hidden tab's colour scheme when
// it shows.)
await navigateShowing('chrome://version');
const webuiDark = async () => {
  const dark = await pagePrefersDark();
  return lastPage().startsWith('chrome://') ? dark : undefined;
};
check('A chrome:// page follows the window\'s light theme',
      await waitFor(async () => await webuiDark() === false), lastPage());
check('Settings switch Space themes off', await setKoanPref('koan.workspaces.tint', false));
check('...which takes the theme off the window (chrome:// pages turn dark)',
      await waitFor(async () => await webuiDark() === true), lastPage());
await setKoanPref('koan.workspaces.tint', true);
check('...and back on', await waitFor(async () => await webuiDark() === false), lastPage());
await navigateShowing('data:text/html,<title>Space 1</title>A website');
await palette('Open Theme Picker');

// Dragging the big dot.
await drag(shown(await tree(), 'KoanThemeGradientField')[0], [240, 240], [200, 180]);
p = picker(await tree());
check("Dragging the big dot moves it and gives Zen's colour there", p?.p.Dots === ZEN_DRAGGED_DOT,
      p?.p.Dots);

// The slider and the dial from the keyboard: Tab to the slider (Home sets
// its minimum), then the dial.
let found = false;
for (let i = 0; i < 20 && !found; i++) {
  await pickerKey(VK.TAB);
  await pickerKey(VK.HOME);
  found = Math.abs(+picker(await tree())?.p.Opacity - 0.25) < 0.001;
}
check('Tab reaches the opacity slider; Home sets 0.25', found);
await pickerKey(VK.RIGHT);
p = picker(await tree());
check('...Right adds 0.05', Math.abs(+p?.p.Opacity - 0.3) < 0.001, p?.p.Opacity);
await pickerKey(VK.TAB);
await pickerKey(VK.HOME);
await pickerKey(VK.LEFT);
p = picker(await tree());
check('The grain dial: Home, then Left wraps to the last step',
      +p?.p.Texture === 15 / 16, p?.p.Texture);
await pickerKey(VK.RIGHT);
await pickerKey(VK.RIGHT);
p = picker(await tree());
check('...and Right steps on (1/16)', +p?.p.Texture === 1 / 16, p?.p.Texture);
const saved = p?.p.Dots;
await closePicker();


// Settings > Look and Feel > Dark Theme Styles: Night Theme.
check('Settings take the dark theme style', await setKoanPref('koan.theme.dark_style', 1));
await switchSpace(VK.LEFT, 'Space');
await palette('Open Theme Picker');
p = picker(await tree());
check('Night theme: a black window', p?.p.WindowBackground === 'rgba(0, 0, 0, 1)',
      p?.p.WindowBackground);
await closePicker();

// The Space's theme after a restart (kept with its tabs in the session).
await restart();
await switchSpace(VK.RIGHT, 'Second');
await palette('Open Theme Picker');
p = picker(await tree());
check('After a restart, the Space has its theme', !!saved && p?.p.Dots === saved,
      `${saved} / ${p?.p.Dots}`);

// The custom colours (shown with the pref set above): type a colour, +
// adds it as a row with its hex, the slider still works with the row
// shown, and the row's button removes it. Zen adds with the + button only.
{
  const inCustom = v => v.shown && v.path.includes('KoanThemeCustomColors');
  let t = await tree();
  const input = t.find(v => v.cls === 'Textfield' && inCustom(v));
  check('With the pref set, the picker shows the custom colours', !!input);
  if (input) {
    await click(input, 20, 10);
    await typeInto(input, '#ff8800');
    t = await tree();
    const add = t.find(v => v.cls === 'KoanThemePickerButton' && inCustom(v));
    if (add) await click(add, 13, 13);
    t = await tree();
    p = picker(t);
    check('+ adds the typed colour', p?.p.CustomColors === '#ff8800', p?.p.CustomColors);
    check('...as a row with its hex',
          t.some(v => v.cls === 'Label' && inCustom(v) && v.p.Text === '#ff8800'),
          t.filter(v => v.cls === 'Label' && inCustom(v)).map(v => v.p.Text).join('|'));
    const slider = t.find(v => v.cls === 'KoanThemeOpacitySlider' && v.shown);
    for (const x of [0.2, 0.7, 0.4]) {
      if (slider) await click(slider, Math.round(slider.w * x), Math.round(slider.h / 2));
    }
    check('The slider works with the row shown', !!picker(await tree()));
    t = await tree();
    const buttons = t.filter(v => v.cls === 'KoanThemePickerButton' && inCustom(v));
    // The add button comes first, then each row's remove button.
    if (buttons.length > 1) await click(buttons.at(-1), 8, 8);
    p = picker(await tree());
    check("The row's button removes it", p?.p.CustomColors === '', p?.p.CustomColors);
  }
}
await closePicker();

// The Create-a-Space form's Theme…: the picker for the Space to be,
// which the new Space gets. (Back to the default dark style, which Zen's
// value below is for.)
await setKoanPref('koan.theme.dark_style', 0);
await shortcut(VK.N, CTRL | ALT, 'n');
{
  const vs = await viewTree();
  const editTheme = vs.find(v => v.cls === 'KoanSpaceFormButton' && v.shown);
  check('The Create-a-Space form has Theme…', !!editTheme);
  if (editTheme) {
    for (const type of ['mousePressed', 'mouseReleased']) {
      await viewMouse(editTheme, type, 20, 10);
    }
    await sleep(1500);
  }
  const t = await tree();
  check('...which opens the theme picker', !!picker(t));
  const swatch = t.find(v => v.cls === 'KoanThemeSwatch' && v.p.Index === '0');
  if (swatch) await click(swatch, 13, 13);
  check('...previewing on the window', picker(await tree())?.p.WindowBackground === ZEN_LIGHT_BACKGROUND_DEFAULT_OPACITY,
        picker(await tree())?.p.WindowBackground);
  await closePicker();
  const name = (await viewTree()).find(
      v => v.cls === 'Textfield' && v.shown && v.path.includes('KoanSpaceCreationView'));
  if (name) await typeInto(name, 'Third', VK.ENTER);
  await sleep(1500);
}
await palette('Open Theme Picker');
p = picker(await tree());
check('The new Space has the theme picked in the form', p?.p.Dots === ZEN_LIGHT_DOT,
      p?.p.Dots);
await closePicker();

finish();
