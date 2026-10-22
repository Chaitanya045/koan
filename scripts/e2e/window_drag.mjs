// End-to-end test of dragging the window from the page, like Zen: a press
// on empty space near the top of the page that moves past the threshold
// becomes a window move (the page gets the button released and no more of
// the gesture; moves below the threshold still reach it, as in Zen); text,
// links and the rest of the page keep their own drags.
//
//   scripts/e2e/run.sh window_drag  (opens scripts/e2e/pages/window_drag.html)
//
// Off-screen there is no window manager to move the window, so this checks
// what the page sees.
import { check, evalIn, finish, onPage, pages, sleep } from './lib/koan.mjs';

const [page] = await pages();

// Presses the left button at (x, y), moves `distance` DIP down in steps
// and releases; returns what the page saw before the release.
async function drag(x, y, distance = 40) {
  await evalIn(page, 'seen.length = 0');
  let before;
  await onPage(page, async c => {
    const send = (type, py, extra = {}) =>
        c.send('Input.dispatchMouseEvent', { type, x, y: py, button: 'left', ...extra });
    await send('mouseMoved', y, { button: 'none' });
    await send('mousePressed', y, { buttons: 1, clickCount: 1 });
    // The page is asked what's under the pointer.
    await sleep(300);
    for (let dy = 2; dy <= distance; dy += 2) {
      await send('mouseMoved', y + dy, { buttons: 1 });
    }
    await sleep(300);
    before = await evalIn(page, 'seen.join(" ")');
    await send('mouseReleased', y + distance, { buttons: 0, clickCount: 1 });
  });
  return before;
}

const squash = s => s.replace(/(move ?)+/g, 'move ').trim();

let seen = await drag(300, 10);
check('Dragging empty space at the top of the page moves the window',
      squash(seen) === 'down move up', squash(seen));

seen = await drag(620, 12);
check('...but not from text (it selects)', squash(seen) === 'down move', squash(seen));

seen = await drag(910, 12);
check('...nor from a link', squash(seen) === 'down move', squash(seen));

seen = await drag(300, 300);
check('...nor from further down the page', squash(seen) === 'down move', squash(seen));

seen = await drag(300, 10, 0);
check('...and a click there stays a click', squash(seen) === 'down', squash(seen));
finish();
