// Runs Speedometer 3.1 in a Chromium-based browser started with
// --remote-debugging-port=<port> and prints the score as JSON.
//
//   node scripts/bench/speedometer.mjs <port>
const port = process.argv[2];
const URL = 'https://browserbench.org/Speedometer3.1/?startAutomatically=true';
const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
const ws = new WebSocket(targets.find(t => t.type === 'page').webSocketDebuggerUrl);
let id = 0;
const pending = new Map();
const send = (method, params = {}) => new Promise(resolve => {
  pending.set(++id, resolve);
  ws.send(JSON.stringify({ id, method, params }));
});
ws.onmessage = e => {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
};
await new Promise(r => (ws.onopen = r));
await send('Page.navigate', { url: URL });
const deadline = Date.now() + 14 * 60 * 1000;
let score = null;
while (Date.now() < deadline) {
  await new Promise(r => setTimeout(r, 5000));
  const r = await send('Runtime.evaluate', {
    expression: `(() => { const e = document.querySelector('#result-number');
                         return e && e.textContent.trim(); })()`,
    returnByValue: true,
  });
  const text = r.result?.result?.value;
  if (text && !Number.isNaN(parseFloat(text))) {
    score = parseFloat(text);
    break;
  }
}
console.log(JSON.stringify(score));
ws.close();
process.exit(score === null ? 1 : 0);
