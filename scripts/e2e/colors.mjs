// End-to-end test that every colour resolves in every look: it opens
// chrome://theme/colors.css (what WebUI pages, e.g. the omnibox popup on
// macOS, load; it asks the colour provider for EVERY colour id) with Zen's
// look and with shadcn's in dark, light and automatic mode, and with a pasted
// theme. A cycle between colour recipes overflows the stack and kills the
// browser (the 6de98f7e launch crash), so the check is that the browser
// answers, with the colours the looks give.
//
//   scripts/e2e/run.sh colors
import {
  browserCall, check, evalIn, finish, pages, setKoanPref, sleep, waitFor,
} from './lib/koan.mjs';

async function colorsCss() {
  const targetId = (await browserCall('Target.createTarget', {
    url: 'chrome://theme/colors.css?sets=ui,chrome', background: true,
  })).result.targetId;
  const page = await waitFor(async () => (await pages()).find(p => p.id === targetId));
  const text = await waitFor(() => evalIn(page, 'document.body?.innerText'));
  await browserCall('Target.closeTarget', { targetId });
  return text;
}

// `--color-omnibox-results-background-selected` is one of the colours the
// looks redefine (the suggestions' selected row).
const NAME = '--color-omnibox-results-background-selected';
const value = css => css.match(new RegExp(NAME + ':\\s*([^;]+);'))?.[1];

const looks = [
  ['Zen', { 'koan.look.style': 'zen' }],
  ['shadcn dark', { 'koan.look.style': 'shadcn', 'koan.look.mode': 'dark' }],
  ['shadcn light', { 'koan.look.style': 'shadcn', 'koan.look.mode': 'light' }],
  ['shadcn automatic', { 'koan.look.style': 'shadcn', 'koan.look.mode': 'auto' }],
  ['shadcn, Zinc with a pasted theme', {
    'koan.look.style': 'shadcn', 'koan.look.mode': 'dark', 'koan.look.base': 'zinc',
    'koan.look.custom_css': ':root { --primary: #ff0000; } .dark { --primary: #00ff00; }',
  }],
];
const seen = new Map();
for (const [name, prefs] of looks) {
  for (const [pref, v] of Object.entries(prefs)) {
    await setKoanPref(pref, v);
  }
  await sleep(500);
  const css = await colorsCss();
  check(`${name}: every colour resolves (colors.css has ${css?.length ?? 0} characters)`,
        typeof css === 'string' && css.length > 10000 && value(css) !== undefined,
        css?.slice(0, 120));
  seen.set(name, value(css));
}
check('The looks give different colours for the selected suggestion',
      new Set(seen.values()).size >= 3, JSON.stringify([...seen]));
await setKoanPref('koan.look.custom_css', '');
await setKoanPref('koan.look.base', 'black');
await setKoanPref('koan.look.style', 'zen');
finish();
