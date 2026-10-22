// End-to-end test of Zen's "New background tab opened!" toast: in compact
// mode with the sidebar hidden, a link opened in a background tab shows the
// toast with a button that switches to the new tab.
//
//   scripts/e2e/run.sh background_tab_toast
import { CTRL, check, evalIn, finish, front, key, mouseOn, onPage, pages, saveShot, shownViews,
         sleep, views } from './lib/koan.mjs';

const [page] = await pages();
await key(page, 83, CTRL, 's');   // Ctrl+S: compact mode
await sleep(800);

// Ctrl+click opens the link in a background tab.
const box = await evalIn(page, `(() => {
  const r = document.querySelector('#link').getBoundingClientRect();
  return { x: r.x + r.width / 2, y: r.y + r.height / 2 };
})()`);
await onPage(page, async c => {
  for (const type of ['mousePressed', 'mouseReleased']) {
    await c.send('Input.dispatchMouseEvent', { type, x: box.x, y: box.y, button: 'left',
                                               clickCount: 1, modifiers: CTRL });
  }
});
await sleep(800);
let vs = await views();
const texts = vs.filter(v => v.path.includes('KoanToastView') && v.cls === 'Label')
    .map(v => v.props.Text);
check('A background tab in compact mode shows Zen\'s toast',
      texts.join(' ') === 'New background tab opened!', texts.join(' | '));
const button = vs.find(v => v.path.includes('KoanToastView') && v.cls === 'ImageButton');
check('...with a button to switch to it', button?.props.TooltipText === 'Switch to tab',
      button?.props.TooltipText);
saveShot('background-tab-toast');

await mouseOn(button, 'mousePressed');
await mouseOn(button, 'mouseReleased');
await sleep(1000);
const background = (await pages()).find(p => p.url.endsWith('?background'));
check('The button switches to the new tab',
      background && await evalIn(background, 'document.visibilityState') === 'visible');
vs = await views();
check('...and the toast goes', !shownViews(vs, 'KoanToastView').length);

// With the sidebar showing there's no toast (Zen).
await front(page);
await key(page, 83, CTRL, 's');   // compact mode off
await sleep(800);
await onPage(page, async c => {
  for (const type of ['mousePressed', 'mouseReleased']) {
    await c.send('Input.dispatchMouseEvent', { type, x: box.x, y: box.y, button: 'left',
                                               clickCount: 1, modifiers: CTRL });
  }
});
await sleep(800);
check('Outside compact mode a background tab shows no toast',
      !shownViews(await views(), 'KoanToastView').length);
finish();
