// End-to-end test of Koan's icons (koan_icons.h): the key surfaces paint
// Zen-vocabulary Koan icons (koan::kKoan...Icon, Lucide glyphs) where Chromium
// asks for its Material ones, and at the sizes Zen draws them (17 in the
// sidebar's top row, 16 favicons / the "+ New Tab" row / the URL pill, 20 for
// the bottom bar's "+", 10 for Clear, 18 for the Library button).
//
// icons.flags makes the browser trace every icon it is asked for to a file
// (--koan-icon-trace, builds with DCHECKs only), "<icon>\t<status>\t<size>",
// status `mapped:<koan icon>`, `koan` or `unmapped`. SHOTS=<dir> saves the
// window.
//
//   scripts/e2e/run.sh icons
import fs from 'node:fs';
import path from 'node:path';
import { check, createSpace, finish, front, mouseOn, pages, saveShot, shownViews, sleep,
         views, waitFor } from './lib/koan.mjs';

const file = path.join(process.env.KOAN_DUMP_DIR, 'icon-trace.txt');
// {icon@size -> status}, and the status by icon name for the unsized checks.
const trace = () => {
  const out = new Map();
  if (fs.existsSync(file)) {
    for (const line of fs.readFileSync(file, 'utf8').split('\n')) {
      const [icon, status, size] = line.split('\t');
      if (icon) out.set(`${icon}@${size}`, status);
    }
  }
  return out;
};

await sleep(2000);
// The first paint of the window.
await waitFor(() => trace().size > 0, 15000);
await front((await pages())[0]);
// A second Space, so that the bottom bar (and its "+") is shown.
await createSpace((await pages())[0], 'Icons');
await sleep(1000);

// Reveals a tab's close button (it shows on hover).
const tab = shownViews(await views(), 'TabView')[0];
if (tab) {
  await mouseOn(tab, 'mouseMoved', 20, 10, 'none');
  await sleep(800);
}
saveShot('icons');

const icons = trace();
// A Chromium icon painted as a Koan icon, at the size Zen draws it.
const expectIcon = (surface, from, to, size) => {
  const got = icons.get(`${from}@${size}`);
  check(`${surface}: ${from} paints as ${to} at ${size}`, got === `mapped:${to}`,
        got ?? `only ${[...icons.keys()].filter(k => k.startsWith(from + '@')).join(' ') || 'not painted'}`);
};
// A Koan icon painted directly.
const expectKoan = (surface, icon, size) => {
  const got = icons.get(`${icon}@${size}`);
  check(`${surface}: ${icon} at ${size}`, got === 'koan',
        got ?? `only ${[...icons.keys()].filter(k => k.startsWith(icon + '@')).join(' ') || 'not painted'}`);
};

expectIcon('top row sidebar toggle', 'kMenuCloseCustomIcon', 'kKoanSidebarIcon', 17);
expectIcon('top row Back', 'kBackArrowChromeRefreshOldIcon', 'kKoanBackIcon', 17);
expectIcon('top row Forward', 'kForwardArrowChromeRefreshOldIcon', 'kKoanForwardIcon', 17);
expectIcon('top row Reload', 'kReloadChromeRefreshOldIcon', 'kKoanReloadIcon', 17);
expectIcon('"+ New Tab" row', 'kAdd2Icon', 'kKoanPlusIcon', 16);
expectIcon('bottom bar "+"', 'kAddIcon', 'kKoanPlusIcon', 20);
expectIcon('Space header actions', 'kMoreHorizIcon', 'kKoanMenuIcon', 16);
expectKoan('Clear button', 'kKoanDartDownIcon', 10);
expectKoan('URL pill Copy URL', 'kKoanLinkIcon', 16);
expectIcon('URL pill site information', 'kTuneIcon', 'kKoanPermissionsIcon', 16);
expectKoan('URL pill magnifier (empty tab)', 'kKoanUrlbarSearchGlassIcon', 16);
expectKoan('Library button', 'kKoanLibraryIcon', 19);

const closes = [...icons].filter(([name]) => /^k(Tab)?Close/.test(name));
check('tab close button: a Koan icon', closes.some(([, s]) => s === 'mapped:kKoanCloseIcon'),
      closes.join(' '));

// Nothing the sidebar shows may still be a Material icon of Zen's vocabulary:
// a variant of back/forward/reload/plus/search/... missing from the table in
// koan_icons.cc.
const material = /^k(BackArrow|ForwardArrow|ArrowBack|ArrowForward|Reload|Add|Search|Download|MoreHoriz|MoreVert)/;
const left = [...icons].filter(([name, s]) => s === 'unmapped' && material.test(name));
check('no Material icon of Zen\'s vocabulary is left', left.length === 0,
      left.map(([name]) => name).join(' '));

console.log(`${icons.size} icon/size pairs asked for; unmapped: ` +
    [...icons].filter(([, s]) => s === 'unmapped').map(([n]) => n).join(' '));
finish();
