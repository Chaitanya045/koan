// End-to-end test of Zen's media controller against a real build: media
// playing in a background tab gets a card at the bottom of the sidebar
// (title, artist, playback buttons), the buttons control the page's media,
// hovering expands it, and the card hides while its tab is selected.
//
//   scripts/e2e/run.sh media        (opens scripts/e2e/pages/media.html)
import { browserCall, check, evalIn, finish, mouseOn, pages, saveShot, shownViews,
         sleep, views } from './lib/koan.mjs';

const [mediaPage] = await pages();
const cards = async () => shownViews(await views(), 'KoanMediaCardView');
// The card's buttons: site icon, previous, play/pause, next, mute,
// picture-in-picture, close.
const cardButtons = async () => (await views()).filter(
    v => v.path.includes('KoanMediaCardView') && v.cls === 'ImageButton');
const press = async (view, wait) => {
  await mouseOn(view, 'mousePressed');
  await mouseOn(view, 'mouseReleased');
  await sleep(wait);
};

const playing = await evalIn(mediaPage, 'song.play().then(() => !song.paused)', true);
check('The page plays its song', playing === true);
await sleep(2000);
check('No card while the media tab is selected', (await cards()).length === 0);

// A second tab: the song now plays in the background.
await browserCall('Target.createTarget', { url: 'about:blank' });
await sleep(2000);
let vs = await views();
const controls = vs.find(v => v.cls === 'KoanMediaControlsView');
let card = shownViews(vs, 'KoanMediaCardView')[0];
check('A media card at the bottom of the sidebar once the tab is in the background',
      controls?.shown && card && controls.path.includes('VerticalTabStripRegionView'),
      JSON.stringify(controls && { shown: controls.shown, h: controls.h }));
const labels = vs.filter(v => v.path.includes('KoanMediaCardView') && v.cls === 'Label')
    .map(v => v.props.Text);
check('...with the title and artist',
      labels.includes('Koan test song') && labels.includes('The Koans'), labels.join(' | '));
let buttons = await cardButtons();
check('...and its buttons', buttons.length === 7, `${buttons.length} buttons`);
check('...collapsed: just the controls row', card?.h > 0 && card.h < 50, `height ${card?.h}`);
saveShot('media-card');

// Hovering expands it to show the title and progress.
await mouseOn(controls, 'mouseEntered');
await sleep(800);
card = (await cards())[0];
check('Hovering expands the card', card?.h > 70, `height ${card?.h}`);
saveShot('media-card-expanded');
await mouseOn(controls, 'mouseExited');
await sleep(800);
card = (await cards())[0];
check('...and leaving collapses it', card?.h < 50, `height ${card?.h}`);

// The buttons drive the page's media session.
buttons = await cardButtons();
await press(buttons[2], 1500);
check('Pause pauses the page\'s media', await evalIn(mediaPage, 'song.paused') === true);
await press(buttons[3], 1000);
check('Next track reaches the page', await evalIn(mediaPage, 'tracks.join()') === 'next');
await press(buttons[2], 1500);
check('Play resumes it', await evalIn(mediaPage, 'song.paused') === false);
await press(buttons[4], 800);
const mute = (await cardButtons())[4];
check('Mute mutes the tab', mute?.props.TooltipText === 'Unmute', mute?.props.TooltipText);

// The site icon selects the media's tab, which hides its card.
await press(buttons[0], 1500);
check('Clicking the card selects its tab',
      await evalIn(mediaPage, 'document.visibilityState') === 'visible');
check('...and its card hides', (await cards()).length === 0);
finish();
