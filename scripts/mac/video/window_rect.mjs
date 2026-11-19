// Prints the biggest window of the Koan on $CDP_PORT as "x,y,w,h" (screen
// points, the window frame included), for record.sh --cdp.
// With --screen: the whole main screen instead, "0,0,w,h", for a flow that
// goes fullscreen (compact_edge.mjs).
import { browserCall, onPage, pages } from '../../e2e/lib/koan.mjs';

if (process.argv.includes('--screen')) {
  const page = (await pages()).find(p => p.type === 'page');
  const r = page && await onPage(page, c => c.send('Runtime.evaluate', {
    expression: '[screen.width, screen.height]', returnByValue: true }));
  const size = r?.result?.result?.value;
  if (!Array.isArray(size)) {
    console.error('window_rect.mjs: no page to ask the screen size');
    process.exit(1);
  }
  console.log([0, 0, ...size].join(','));
  process.exit(0);
}

let best = null;
for (const page of await pages()) {
  const b = (await browserCall('Browser.getWindowForTarget', { targetId: page.id })).result?.bounds;
  if (b && b.windowState === 'normal' && (!best || b.width * b.height > best.width * best.height)) best = b;
}
if (!best) {
  console.error('window_rect.mjs: no normal window');
  process.exit(1);
}
console.log([best.left, best.top, best.width, best.height].join(','));
