// End-to-end test of Zen's welcome screen against a real build, on a first
// run (welcome.flags): the title and start button, then the pages (import,
// colours, search engine, Essentials, default browser, done); the chosen
// Essentials end up pinned.
//
//   scripts/e2e/run.sh welcome
import { check, finish, mouseOn, pages, saveShot, shownViews, sleep,
         views } from './lib/koan.mjs';

const press = async (view, wait = 1000) => {
  await mouseOn(view, 'mousePressed');
  await mouseOn(view, 'mouseReleased');
  await sleep(wait);
};

let vs = await views();
check('A first run shows the welcome screen over the window',
      shownViews(vs, 'KoanWelcomeView').length === 1);
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
check('...with YES / NO', shownViews(vs, 'KoanWelcomeOption').length === 2);
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
await next();
check('Next: the search engine', heading() === 'Pick your search engine.', heading());
await next();
check('Next: Essentials', heading() === 'Choose the apps you use most.', heading());
const tiles = shownViews(vs, 'KoanWelcomeEssentialTile');
check('...nine apps to choose from', tiles.length === 9);
await press(tiles[0], 300);   // Obsidian
await press(tiles[4], 300);   // GitHub
saveShot('welcome-essentials');
await next();
check('Next: default browser', heading() === 'Make Koan your default?', heading());
await next();
check('Next: done', heading() === 'All set? Let’s get rolling!', heading());
await next();
await sleep(1000);
vs = await views();
check('"Dive in!" closes the welcome screen', !vs.some(v => v.cls === 'KoanWelcomeView'));
const urls = (await pages()).map(p => p.url).join(' ');
check('...and pins the chosen Essentials',
      urls.includes('obsidian.md') && urls.includes('github.com'), urls);
finish();
