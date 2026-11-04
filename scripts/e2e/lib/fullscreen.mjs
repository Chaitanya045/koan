// Helpers for the native fullscreen tests (fullscreen_single.mjs,
// fullscreen_compact.mjs): macOS only, where fullscreen has Chromium's
// immersive mode.
import { CDP_PORT, META, CTRL, browserCall, sleep, visiblePage, waitFor }
    from './koan.mjs';

export async function isMac() {
  const version = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json();
  return /Macintosh/.test(version['User-Agent']);
}
// The modifier of the platform's primary shortcuts (⌘ on macOS).
export const primary = async () => (await isMac() ? META : CTRL);

const windowOf = async targetId =>
    (await browserCall('Browser.getWindowForTarget', { targetId })).result;

export async function setWindowState(windowState, targetId) {
  const id = targetId ?? (await visiblePage()).id;
  const { windowId } = await windowOf(id);
  await browserCall('Browser.setWindowBounds', { windowId, bounds: { windowState } });
}

export async function windowState(targetId) {
  const id = targetId ?? (await visiblePage()).id;
  return (await windowOf(id)).bounds?.windowState;
}

// Fullscreen (or back), once the transition has finished.
export async function goFullscreen(on, targetId) {
  await setWindowState(on ? 'fullscreen' : 'normal', targetId);
  await waitFor(async () => ((await windowState(targetId)) === 'fullscreen') === on, 8000);
  await sleep(2500);
}

// Whether the browser still answers.
export async function alive() {
  try {
    const r = await browserCall('Browser.getVersion');
    return !!r.result?.product;
  } catch {
    return false;
  }
}
