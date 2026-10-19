// Small DevTools-protocol helpers for Koan's performance scripts and tests:
// a browser-level session, tracing of Koan's views and the browser's main
// thread, and per-process CPU time.
const sleep = ms => new Promise(r => setTimeout(r, ms));
export { sleep };

// Connects to a DevTools WebSocket. `on(method, fn)` subscribes to events.
export async function connect(url) {
  const ws = new WebSocket(url);
  let id = 0;
  const pending = new Map();
  const listeners = new Map();
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) {
      pending.get(m.id)(m);
      pending.delete(m.id);
    } else if (m.method) {
      for (const fn of listeners.get(m.method) ?? []) fn(m.params, m.sessionId);
    }
  };
  await new Promise(r => (ws.onopen = r));
  const send = (method, params = {}, sessionId, timeout = 10000) =>
    new Promise(resolve => {
      pending.set(++id, resolve);
      ws.send(JSON.stringify({ id, method, params, sessionId }));
      setTimeout(() => resolve({ timedOut: true }), timeout);
    });
  const on = (method, fn) => {
    if (!listeners.has(method)) listeners.set(method, []);
    listeners.get(method).push(fn);
  };
  return { send, on, close: () => ws.close() };
}

export async function browserSession(port) {
  const v = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
  return connect(v.webSocketDebuggerUrl);
}

export async function pageTargets(port) {
  return (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json())
      .filter(t => t.type === 'page');
}

// Evaluates `expression` in the page target matching `match` (a predicate
// on the target); undefined after 5 s, so a frozen page fails instead of
// hanging the run.
export async function evaluate(port, match, expression) {
  const target = (await pageTargets(port)).find(match);
  if (!target) return undefined;
  const page = await connect(target.webSocketDebuggerUrl);
  const r = await page.send('Runtime.evaluate',
                            { expression, returnByValue: true, awaitPromise: true },
                            undefined, 5000);
  page.close();
  return r.result?.result?.value;
}

// Sends a key press to a page (by default the first one); keys it doesn't
// handle reach its window's accelerators, as real input does. Modifiers:
// Alt=1, Ctrl=2, Shift=8.
export async function key(port, keyCode, modifiers = 0, match = () => true) {
  const target = (await pageTargets(port)).find(match);
  const page = await connect(target.webSocketDebuggerUrl);
  const base = { windowsVirtualKeyCode: keyCode, nativeVirtualKeyCode: keyCode, modifiers };
  await page.send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
  await page.send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
  page.close();
}

// CPU seconds used so far by each process, keyed by "type:pid".
export async function processCpu(browser) {
  const r = await browser.send('SystemInfo.getProcessInfo');
  return Object.fromEntries(
      (r.result?.processInfo ?? []).map(p => [`${p.type}:${p.id}`, p.cpuTime]));
}

// Records a trace while `during()` runs and summarises what Koan's UI did:
// view paints by class (a paint of a widget's root view is one UI frame),
// layouts, and tasks run on the browser's main thread by where they were
// posted from.
export async function traceUi(browser, during) {
  const events = [];
  browser.on('Tracing.dataCollected', p => events.push(...p.value));
  const done = new Promise(r => browser.on('Tracing.tracingComplete', r));
  await browser.send('Tracing.start', {
    transferMode: 'ReportEvents',
    traceConfig: {
      recordMode: 'recordAsMuchAsPossible',
      includedCategories: ['views', 'toplevel', 'ui'],
    },
  });
  const cpuBefore = await processCpu(browser);
  const started = Date.now();
  await during();
  const seconds = (Date.now() - started) / 1000;
  const cpuAfter = await processCpu(browser);
  await browser.send('Tracing.end');
  await done;

  const threads = new Map();
  for (const e of events) {
    if (e.ph === 'M' && e.name === 'thread_name') {
      threads.set(`${e.pid}:${e.tid}`, e.args.name);
    }
  }
  const count = (map, k) => map.set(k, (map.get(k) ?? 0) + 1);
  const paints = new Map();
  const layouts = new Map();
  const tasks = new Map();
  let mainTasks = 0;
  for (const e of events) {
    if (e.ph !== 'X' && e.ph !== 'B') continue;
    const thread = threads.get(`${e.pid}:${e.tid}`);
    if (e.name === 'View::OnPaint') count(paints, e.args?.class);
    if (e.name.startsWith('View::Layout')) count(layouts, e.args?.class);
    if (thread === 'CrBrowserMain' && e.name === 'ThreadControllerImpl::RunTask') {
      mainTasks++;
      const from = e.args?.src_func ?
          `${e.args.src_file?.split('/').pop()}:${e.args.src_func}` : '?';
      count(tasks, from);
    }
  }
  const sorted = m => [...m.entries()].sort((a, b) => b[1] - a[1]);
  const cpu = {};
  for (const [k, v] of Object.entries(cpuAfter)) {
    const type = k.split(':')[0];
    cpu[type] = (cpu[type] ?? 0) + v - (cpuBefore[k] ?? 0);
  }
  return {
    seconds,
    paints: sorted(paints),
    layouts: sorted(layouts),
    mainTasks,
    tasks: sorted(tasks),
    cpu,
  };
}

export function printSummary(label, s, top = 12) {
  const r1 = x => Math.round(x * 1000) / 1000;
  console.log(`\n== ${label} (${s.seconds.toFixed(1)} s)`);
  console.log(`  CPU s by process type: ${JSON.stringify(
      Object.fromEntries(Object.entries(s.cpu).map(([k, v]) => [k, r1(v)])))}`);
  console.log(`  browser main-thread tasks: ${s.mainTasks}`);
  for (const [k, v] of s.tasks.slice(0, top)) console.log(`    ${v}\t${k}`);
  const paintTotal = s.paints.reduce((a, [, v]) => a + v, 0);
  console.log(`  view paints: ${paintTotal}`);
  for (const [k, v] of s.paints.slice(0, top)) console.log(`    ${v}\t${k}`);
  const layoutTotal = s.layouts.reduce((a, [, v]) => a + v, 0);
  console.log(`  view layouts: ${layoutTotal}`);
  for (const [k, v] of s.layouts.slice(0, top)) console.log(`    ${v}\t${k}`);
}
