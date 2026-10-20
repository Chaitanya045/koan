// Profiles what Koan's UI does in common situations, off-screen against a
// real build: view paints and layouts by class, browser main-thread tasks
// by where they were posted from, and CPU time by process type.
//
//   scripts/e2e/run.sh scripts/perf/profile_ui.mjs
//   KOAN_FLAGS=--disable-features=InfiniteTabsFreezing \
//     scripts/e2e/run.sh scripts/perf/profile_ui.mjs
//
// Numbers from the dev build (component, DCHECKs on) are only good for
// comparisons within it. See docs/PERFORMANCE.md.
import { browserSession, connect, key, pageTargets, printSummary, sleep, traceUi } from './cdp.mjs';
import { servePages } from './pages_server.mjs';

const CDP_PORT = process.env.CDP_PORT;
const only = process.env.SCENARIOS?.split(',');
const pages = await servePages();
const browser = await browserSession(CDP_PORT);

async function activePage() {
  return (await pageTargets(CDP_PORT))[0];
}
async function navigate(url) {
  const t = await activePage();
  await browser.send('Target.activateTarget', { targetId: t.id });
  const page = await connect(t.webSocketDebuggerUrl);
  await page.send('Page.navigate', { url });
  page.close();
}

const scenarios = {
  async idle() {
    await navigate(`${pages.origin}/power.html`);
    await sleep(3000);
    printSummary('idle: static page, default layout', await traceUi(browser, () => sleep(10000)));
  },
  async title() {
    await navigate(`${pages.origin}/power.html?title`);
    await sleep(2000);
    printSummary('active tab changes its title 4x/s',
                 await traceUi(browser, () => sleep(10000)));
  },
  async loads() {
    printSummary('5 page loads', await traceUi(browser, async () => {
      for (let i = 0; i < 5; i++) {
        await navigate(`${pages.origin}/power.html?load${i}`);
        await sleep(2000);
      }
    }));
  },
  async compact() {
    await navigate(`${pages.origin}/power.html`);
    await key(CDP_PORT, 83, 2);  // Ctrl+S
    await sleep(2000);
    printSummary('idle: compact mode', await traceUi(browser, () => sleep(10000)));
    printSummary('compact mode: peek the sidebar (Ctrl+Alt+S), until it hides',
                 await traceUi(browser, async () => {
                   await key(CDP_PORT, 83, 3);
                   await sleep(6000);
                 }));
    await key(CDP_PORT, 83, 2);
    await sleep(1000);
  },
  async background() {
    await navigate(`${pages.origin}/power.html`);
    for (let i = 0; i < 8; i++) {
      await browser.send('Target.createTarget',
                         { url: `${pages.origin}/power.html?busy&tab${i}`, background: true });
    }
    await sleep(5000);
    printSummary('8 busy background tabs', await traceUi(browser, () => sleep(20000)));
  },
};

for (const [name, run] of Object.entries(scenarios)) {
  if (!only || only.includes(name)) await run();
}
browser.close();
pages.close();
process.exit(0);
