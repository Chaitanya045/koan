// Prints the biggest window of the Koan on $CDP_PORT as "x,y,w,h" (screen
// points, the window frame included), for record.sh --cdp.
import { browserCall, pages } from '../../e2e/lib/koan.mjs';

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
