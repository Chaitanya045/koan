// End-to-end test of Zen's welcome screen against a real build, on a first
// run (welcome.flags): the title and start button, then the pages (import,
// colours, search engine, Essentials, default browser, done); the chosen
// Essentials end up pinned.
//
//   scripts/e2e/run.sh welcome
import { check, evalIn, finish, mouseOn, pages, saveShot, shownViews, sleep, viewEvent,
         views } from './lib/koan.mjs';

// A key on a view (UI DevTools), e.g. 89 for Y, 40 for Down, 13 for Enter.
const viewKey = async (view, keyCode) => {
  await viewEvent(view, 'DOM.dispatchKeyEvent',
                  { type: 'keyPressed', keyCode, code: 0, flags: 0, key: 0, isChar: false });
  await sleep(500);
};
// The chosen choices of a list (class `cls`), by name.
const chosen = (vs, cls) => shownViews(vs, cls).filter(v => v.props.Selected === 'true')
    .map(v => v.props.ChoiceName).join(', ');

const press = async (view, wait = 1000) => {
  await mouseOn(view, 'mousePressed');
  await mouseOn(view, 'mouseReleased');
  await sleep(wait);
};

let vs = await views();
check('A first run shows the welcome screen over the window',
      shownViews(vs, 'KoanWelcomeView').length === 1);
const screen = shownViews(vs, 'KoanWelcomeView')[0];
const frame = vs.find(v => v.cls === 'BrowserView');
check('...but not over the strip with the window buttons at the top',
      screen && frame && screen.h < frame.h, `${screen?.h} of ${frame?.h}`);
const title = vs.filter(v => v.cls === 'Label' && v.path.at(-1) === 'KoanWelcomeView')
    .map(v => v.props.Text).join('');
check('..."Welcome to a calmer internet"', title === 'Welcome toa calmer internet', title);

// The characters rise in, then the button.
await sleep(3500);
saveShot('welcome-start');
vs = await views();
await press(shownViews(vs, 'KoanWelcomeStartButton')[0], 1500);
vs = await views();
check('Start shows the pages', shownViews(vs, 'KoanWelcomePagesView').length === 1);
const heading = () => vs.filter(v => v.cls === 'Label' &&
                                     v.path.at(-1) === 'KoanWelcomePagesView')[0]?.props.Text;
check('...first, importing', heading() === 'Bring your stuff along.', heading());
// The keyboard moved from Start to the page's Next without passing through
// the web page (which would focus its link).
const [page] = await pages();
const active = await evalIn(page, 'document.activeElement?.tagName');
check('...and the keyboard stays on the screen, not in the page', active !== 'A', active);
check('...with YES / NO', shownViews(vs, 'KoanWelcomeOption').length === 2);
// The keyboard: typing a choice's first letter picks it, the arrows move.
let options = shownViews(vs, 'KoanWelcomeOption');
await viewKey(options[1], 89);   // Y
vs = await views();
check('Typing Y picks YES', chosen(vs, 'KoanWelcomeOption') === 'YES',
      chosen(vs, 'KoanWelcomeOption'));
await viewKey(options[0], 40);   // Down
vs = await views();
check('...and Down moves to NO', chosen(vs, 'KoanWelcomeOption') === 'NO',
      chosen(vs, 'KoanWelcomeOption'));
saveShot('welcome-import');

const next = async () => {
  await press(shownViews(vs, 'KoanWelcomeButton')[0]);
  vs = await views();
};
await next();
check('Next: colours', heading() === 'Your space, your colors.', heading());
check('...to pick from', shownViews(vs, 'KoanWelcomeSwatch').length === 9);
await press(shownViews(vs, 'KoanWelcomeSwatch')[4], 800);
vs = await views();
check('Clicking a colour picks it', chosen(vs, 'KoanWelcomeSwatch') === 'Blue',
      chosen(vs, 'KoanWelcomeSwatch'));
await viewKey(shownViews(vs, 'KoanWelcomeSwatch')[4], 39);   // Right
vs = await views();
check('...Right picks the next one', chosen(vs, 'KoanWelcomeSwatch') === 'Violet',
      chosen(vs, 'KoanWelcomeSwatch'));
await viewKey(shownViews(vs, 'KoanWelcomeSwatch')[5], 84);   // T
vs = await views();
check('...and typing T picks Teal', chosen(vs, 'KoanWelcomeSwatch') === 'Teal',
      chosen(vs, 'KoanWelcomeSwatch'));
await next();
check('Next: the search engine', heading() === 'Pick your search engine.', heading());
options = shownViews(vs, 'KoanWelcomeOption');
const names = options.map(o => o.props.ChoiceName ?? '');
const initial = n => n.slice(0, 1).toLowerCase();
const target = names.find((n, i) => options[i].props.Selected !== 'true' &&
                                    /[a-z]/.test(initial(n)) &&
                                    names.filter(m => initial(m) === initial(n)).length === 1);
check('...a list of engines', options.length >= 2, names.join(', '));
if (target) {
  await viewKey(options.find(o => o.props.Selected === 'true') ?? options[0],
                initial(target).toUpperCase().charCodeAt(0));
  vs = await views();
  check(`...typing ${initial(target).toUpperCase()} picks ${target}`,
        chosen(vs, 'KoanWelcomeOption') === target, chosen(vs, 'KoanWelcomeOption'));
}
await next();
check('Next: Essentials', heading() === 'Choose the apps you use most.', heading());
const tiles = shownViews(vs, 'KoanWelcomeEssentialTile');
check('...nine apps to choose from', tiles.length === 9);
await press(tiles[0], 300);   // Obsidian
await press(tiles[4], 300);   // GitHub
await viewKey(tiles[6], 13);  // Enter on Notion adds it...
vs = await views();
check('Enter adds an Essential', chosen(vs, 'KoanWelcomeEssentialTile') ===
      'Obsidian, GitHub, Notion', chosen(vs, 'KoanWelcomeEssentialTile'));
await viewKey(tiles[6], 13);  // ...and again removes it
vs = await views();
check('...and removes it again', chosen(vs, 'KoanWelcomeEssentialTile') === 'Obsidian, GitHub',
      chosen(vs, 'KoanWelcomeEssentialTile'));
saveShot('welcome-essentials');
await next();
check('Next: default browser', heading() === 'Make Koan your default?', heading());
await next();
check('Next: done', heading() === 'All set? Let’s get rolling!', heading());
// "Dive in!" double-clicked: the button stays during the fade, but the
// choices apply once.
const diveIn = shownViews(vs, 'KoanWelcomeButton')[0];
for (let i = 0; i < 2; ++i) {
  await mouseOn(diveIn, 'mousePressed');
  await mouseOn(diveIn, 'mouseReleased');
}
await sleep(2000);
vs = await views();
check('"Dive in!" closes the welcome screen', !vs.some(v => v.cls === 'KoanWelcomeView'));
const urls = (await pages()).map(p => p.url);
const opened = site => urls.filter(url => url.includes(site)).length;
check('...and opens each chosen Essential once, even after a double-click',
      opened('obsidian.md') === 1 && opened('github.com') === 1, urls.join(' '));
const pinned = vs.filter(v => v.cls === 'TabView' && v.path.includes('PinnedTabContainerView'));
check('...pinned', pinned.length === 2, `${pinned.length} pinned tabs`);
finish();
