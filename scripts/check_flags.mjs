// Usage: node scripts/check_flags.mjs <port>
// Opens chrome://flags in a running headless Koan (started with
// --remote-debugging-port=<port>) and prints the Koan flag entries.
// Headless Chrome refuses chrome:// URLs on the command line; CDP works.
const port = process.argv[2];
const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
const page = targets.find(t => t.type === 'page');
const ws = new WebSocket(page.webSocketDebuggerUrl);
let id = 0; const pending = new Map();
const send = (method, params = {}) => new Promise(r => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
ws.onmessage = e => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } };
await new Promise(r => ws.onopen = r);
await send('Page.enable');
await send('Page.navigate', { url: 'chrome://flags/#koan-sidebar' });
await new Promise(r => setTimeout(r, 3000));
const expr = `(() => {
  const out = [];
  const walk = root => { root.querySelectorAll('*').forEach(el => { if (el.shadowRoot) walk(el.shadowRoot);
    if (el.id === 'koan-sidebar' || el.id === 'koan-compact-mode') out.push(el.id + ': ' + el.innerText.replace(/\\s+/g,' ').slice(0,200)); }); };
  walk(document);
  return location.href + '\\n' + (out.join('\\n') || 'NO KOAN ENTRIES');
})()`;
const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true });
console.log(r.result.result.value);
await send('Page.captureScreenshot', { format: 'png' }).then(s => import('node:fs').then(fs => fs.writeFileSync('process.argv[3] ?? '/tmp/koan-flags.png'', Buffer.from(s.result.data, 'base64'))));
ws.close();
